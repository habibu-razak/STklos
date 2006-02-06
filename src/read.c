/*
 * r e a d  . c				-- reading stuff
 *
 * Copyright � 1993-2006 Erick Gallesio - I3S-CNRS/ESSI <eg@unice.fr>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, 
 * USA.
 * 
 *           Author: Erick Gallesio [eg@unice.fr]
 *    Creation date: ??-Oct-1993 ??:?? 
 * Last file update:  1-Feb-2006 17:13 (eg)
 *
 */

#include <ctype.h>
#include "stklos.h"

static SCM read_srfi10(SCM port, SCM l);
static SCM read_rec(SCM port, int case_significant, int constant, int inlist);


static SCM cycles;		/* used for reading circular data */
static SCM sym_quasiquote, sym_unquote, sym_unquote_splicing, sym_dot;
static SCM sym_read_brace, sym_read_bracket, read_error;
SCM STk_sym_quote;
static int comment_level;
int STk_read_case_sensitive = 0;


#define PLACEHOLDERP(x) 	(CONSP(x) && (BOXED_INFO(x) & CONS_PLACEHOLDER))
#define PLACEHOLDER_VAL(x)	(CDR(x))

#define SYMBOL_VALUE(x,ref)	STk_lookup((x), STk_current_module, &(ref), FALSE)

/*===========================================================================*\
 * 
 * Utilities
 *
\*===========================================================================*/
static void signal_error(SCM port, char *format, SCM param)
{
  STk_raise_exception(STk_make_C_cond(read_error,
				      7,
				      STk_false,
				      STk_vm_bt(),
				      STk_format_error(format, param),
				      MAKE_INT(PORT_LINE(port)),    /* line */
				      STk_false,		    /* column */
				      MAKE_INT(PORT_POS(port)),	    /* position */
				      STk_false));		    /* span */
}


static void error_token_too_large(SCM port, char *tok)
{
  tok[MAX_TOKEN_SIZE-1] = '\0'; /* truncate the name (should be sufficient !!) */
  signal_error(port, "token too large ~S", STk_Cstring2string(tok));
}

static void error_bad_sharp_syntax(SCM port, char *tok)
{
  signal_error(port, "bad sharp syntax in ~S", STk_Cstring2string(tok));
}

static void error_key_not_defined(SCM port, SCM key)
{
  signal_error(port, "key ``#~a='' not defined", key);
}

static void error_bad_dotted_list(SCM port)
{
  signal_error(port, "bad dotted list", STk_nil);
}


static void warning_parenthesis(SCM port)
{
  STk_warning("bad closing parenthesis on line %d of ~S", PORT_LINE(port), port);
}


static int flush_spaces(SCM port, char *message, SCM file)
{
  int c;

  for ( ; ; ) {
    switch (c = STk_getc(port)) {
      case EOF:  if (message) signal_error(port, message, file); else return(c);
      case ';':  do 
		   c = STk_getc(port); 
		 while (c != '\n' && c != EOF);
		 continue;
      default:   if (!isspace(c)) return(c);
    }
  }
}


static SCM read_list(SCM port, char delim, int case_significant, int constant)
/* Read a list ended by the `delim' char */
{
  static char *eof_seen = "end of file encountered on ~S";
  int c, line;
  SCM tmp, cell, cdr;

  line = PORT_LINE(port);
  c    = flush_spaces(port, eof_seen, port);

  if (c == delim) return(STk_nil);
Top:
  /* Read the car */
  STk_ungetc(c, port);
  tmp = read_rec(port, case_significant, constant, TRUE);
  
  /* See if we don't have a special car */
  if (tmp == STk_close_par) {
    /* We are here when reading a list such as (1 ..... #;XXX) */
    c = STk_getc(port);
    if (c == delim)
      return STk_nil;
    else {
      warning_parenthesis(port);
      goto Top;
    }
  }
  
  if (tmp == STk_dot) {
    tmp = read_rec(port, case_significant, constant, TRUE);
    if (tmp == STk_close_par) 
      error_bad_dotted_list(port);

    c = flush_spaces(port, eof_seen, port);
    if (c == '#') {
      SCM tmp;
      
      STk_ungetc(c, port);
      tmp = read_rec(port, case_significant, constant, TRUE);
      if (tmp != STk_close_par) warning_parenthesis(port);
      c = flush_spaces(port, eof_seen, port);
    }
    if (c != delim) 
      STk_warning("missing close parenthesis (line %d)", PORT_LINE(port));
    return(tmp);
  }
  
  /* OK, read now the cdr */
  cdr = read_list(port, delim, case_significant, constant);
  if (cdr == STk_close_par)
    error_bad_dotted_list(port);

  if (constant) {
    /* Constant read uses extended cons instead of cons */
    cell = STk_econs(tmp, cdr, PORT_FNAME(port), line, PORT_POS(port));
    BOXED_INFO(cell) |= CONS_CONST;
  } else {
    cell = STk_cons(tmp, cdr);
  }

  return cell;
}


static int read_word(SCM port, int c, char *tok, int case_significant) 
/* read an item whose 1st char is in c. Return its length */
{
  register int j = 0;
  int allchars   = 0;

  for( ; ; ) {
    allchars  ^= (c == '|');
    if (c != '|') 
      tok[j++]  = (allchars || case_significant) ? c : tolower(c);

    c = STk_getc(port);
    if (c == EOF) break;
    if (!allchars) {
      if (strchr("()[]{}'`,;\"\n\r \t\f", c)) {
	STk_ungetc(c, port);
	break;
      }
      //      if (isspace(c)) break; //FIXME:
    }
    if (j >= MAX_TOKEN_SIZE-1) error_token_too_large(port, tok);
  }

  tok[j] = '\0';
  return j;
}

static SCM read_token(SCM port, int c, int case_significant) 
{
  char tok[MAX_TOKEN_SIZE];
  SCM z;
  int len;

  len = read_word(port, c, tok, case_significant);
  z   = STk_Cstr2number(tok, 10L);

  if (z == STk_false) {
    /* It is not a number */
    switch (*tok) {
      case ':': return STk_makekey(tok);
      case '#': if (strcasecmp(tok+1, "eof") == 0)
	           return STk_eof;
      		else
		  error_bad_sharp_syntax(port, tok);
      default : return (tok[len-1] == ':') ? STk_makekey(tok) : STk_intern(tok);
    }
  }
  /* Return the number read */
  return z;
}

static SCM read_char(SCM port, int c)
/* read an char (or a char name) item whose 1st char is in c */
{
  char tok[MAX_TOKEN_SIZE];
  register int j = 0;
  
  for( ; ; ) {
    tok[j++] = c;
    c = STk_getc(port);
    if (c == EOF || isspace(c)) break;
    if (strchr("()[]'`,;\"", c)) {
      STk_ungetc(c, port);
      break;
    }
    if (j >= MAX_TOKEN_SIZE-1) error_token_too_large(port, tok);
  }
  tok[j] = '\0';
  
  /* convert the character contained in token in a character */
  return MAKE_CHARACTER(STk_string2char(tok));
  
}

static SCM read_address(SCM port)
{
  char *end, tok[MAX_TOKEN_SIZE] = "0";
  unsigned long address;
  
  read_word(port, 'x', tok+1, FALSE);
  address = strtoul(tok, &end, 16);
  if (*end) 
    signal_error(port, "bad address specifier #p~a", STk_Cstring2string(tok+2));
 
  return (SCM) address;
}


static SCM read_cycle(SCM port, int c, int case_significant, int constant)
/* read a #xx# or #xx= cycle item whose 1st char is in c. */
{ 
  char buffer[MAX_TOKEN_SIZE];
  int  j = 0;
  SCM k, tmp, val;

  for( ; ; ) {
    buffer[j++] = c;
    c = STk_getc(port);
    if (c == EOF || !isdigit(c)) break;
    if (j >= MAX_TOKEN_SIZE-1) error_token_too_large(port, buffer);
  }
  buffer[j] = '\0';
  k = MAKE_INT(atoi(buffer));

  switch (c) {
    case '#': if ((tmp = STk_assv(k, cycles)) != STk_false)
		return CDR(tmp);
	      else
		error_key_not_defined(port, k);

    case '=': if ((tmp = STk_assv(k, cycles)) == STk_false) {
      		/* This is a little bit tricky here: We create a fake cell
		 * that serves as a place-holder. In some cases this is not 
		 * useful (e.g. (#0=(1 2) 3 4 . #0#) ), but in some other 
		 * cases such as 
		 *    (#0=(1 2 . #0#) #0#)
		 * the first reference will use the placeholder cell, whereas 
		 * the second one will be correct. 
		 * The function "patch_references" will correct all the 
		 * remaining references that musr be modified. 
		 */
		 
      		 /* create the fake cell */
      		 tmp      = STk_cons(STk_void, k);
		 BOXED_INFO(tmp) |= CONS_PLACEHOLDER;

		 /* Add the couple (k . <fake-cell>) to the cycles list */
		 tmp      = STk_cons(k, tmp);
		 cycles   = STk_cons(tmp, cycles);

		 /* Read item */
		 val      = read_rec(port, case_significant, constant, FALSE);

		 /* Patch the list of cycles with the correct value */
		 CDR(tmp) = val;

		 return val;
    	       } 
    	       else 
		 signal_error(port, "key ``#~a='' already defined", k);

  default:  STk_ungetc(c, port); error_bad_sharp_syntax(port, buffer);
  }
  
  return STk_void; /* for the compiler */
}



static SCM find_references(SCM *obj, SCM to_correct)
{
  if (PLACEHOLDERP(*obj)) {
    /* place it in the list of references to correct */
    if (STk_memq((SCM) obj, to_correct) == STk_false){
      to_correct = STk_cons((SCM) obj, to_correct);
    }
    return to_correct;
  }

  if (CONSP(*obj)) {
    to_correct = find_references(&CAR(*obj), to_correct);
    to_correct = find_references(&CDR(*obj), to_correct);
    return to_correct;
  }
  
  if (VECTORP(*obj)) {
    int i, l = VECTOR_SIZE(*obj);
    SCM *p;
    
    for (i=0, p=VECTOR_DATA(*obj); i < l; i++, p++) {
      to_correct = find_references(p, to_correct);
    }
    return to_correct;
  }
  
  return to_correct;
}

static void patch_references(SCM port, SCM l)
{
  for ( ; !NULLP(l); l = CDR(l)) {
    SCM k, tmp;
    
    tmp = *((SCM *) CAR(l));
    k   = PLACEHOLDER_VAL(tmp);
    if ((tmp = STk_assv(k, cycles)) != STk_false)
      *((SCM *) CAR(l)) = CDR(tmp);
    else
      error_key_not_defined(port, k);
  }
}
    

static SCM read_string(SCM port, int constant)
{
  int k ,c, n, hexa;
  size_t j, len;
  char *p, *buffer;
  SCM z;

  hexa = 0;
  j    = 0;
  len  = 100;
  p    = buffer = STk_must_malloc(len);

  while(((c = STk_getc(port)) != '"') && (c != EOF)) { 
    if (c == '\\') {
      c = STk_getc(port);
      if (c == EOF) signal_error(port, "eof encountered after \\", STk_nil);
      switch(c) {
        case 'a' : c = '\a'; break;     /* Bell */
        case 'b' : c = '\b'; break;	/* Bs   */
	case 'e' : c = 0x1b; break;	/* Esc  */
	case 'f' : c = '\f'; break;	/* FF   */
	case 'n' : c = '\n'; break;	/* Lf   */
	case 'r' : c = '\r'; break;	/* Cr   */
	case 't' : c = '\t'; break;	/* Tab  */
	case 'v' : c = '\v'; break;	/* VTab */
        case '\n': continue;
	case 'x' : hexa = 1;
	case '0' : for( k=n=0 ; ; k++ ) {
		     c = STk_getc(port);
		     if (c == EOF) 
		       signal_error(port, 
				    "eof encountered when reading char in string",
				    STk_nil);
		     
		     /* if hexa 2 digits max, if octal 3 digit max */
		     if (hexa && isxdigit(c) && k < 2) {
		       /* because of a GCC bug, factorisation is not possible */
		       c = tolower(c);
		       n = n * 16 + (isdigit(c) ? (c - '0'): (c - 'a' + 10));
		     }
		     else if (!hexa && isdigit(c) && (c < '8') && k < 3)
		        n = n * 8 + c - '0';
		     else {
		       STk_ungetc(c, port);
		       break;
		     }
		   }
		   hexa = 0;
	           c    = n & 0xff;
      }
    }

    if ((j + 1) >= len) {
      len = len + len / 2;
      buffer = STk_must_realloc(buffer, len);
      p = buffer + j;
    }
    j++;
    *p++ = c;
  }
  if (c == EOF) signal_error(port,"end of file while reading a string on ~S", port);
  *p = '\0';
  
  z = STk_makestring(j, buffer);
  if (constant) 
    BOXED_INFO(z) |= STRING_CONST;

  STk_free(buffer);
  return z;
}


static SCM read_vector(SCM port, int case_significant, int constant)
{
  SCM v = STk_list2vector(read_list(port, ')', case_significant, constant));
 
  if (constant) BOXED_INFO(v) |= VECTOR_CONST;
  return v;
}

static SCM maybe_read_uniform_vector(SCM port, int c, int case_significant)
{
  char tok[MAX_TOKEN_SIZE];
  int tag, len;
  SCM v;
  
  len = read_word(port, c, tok, case_significant);
  if (len == 1 && (*tok == 'F' || *tok == 'f')) {
    /* This is the #f constant */
    return STk_false;
  } else {
    if (len == 2 || len == 3) {
      c = STk_getc(port);
      if (c != '(') goto bad_spec;
      tag = STk_uniform_vector_tag(tok);
      if (tag >= 0) {
	/* Ok that's seems correct read the list of values (this IS a constant) */
	v =  STk_list2uvector(tag, read_list(port, ')', case_significant, TRUE));
	BOXED_INFO(v) |= VECTOR_CONST;
	return v;
      }
    }
  }
 bad_spec:
  signal_error(port, "bad uniform vector specification ~A",STk_Cstring2string(tok));
  return STk_void;
}


static SCM read_rec(SCM port, int case_significant, int constant, int inlist)
{
  int c;

  for ( ; ; ) {
    c = flush_spaces(port, (char *) NULL, (SCM) NULL);
    
    switch (c) {
      case '(':
        return(read_list(port, ')', case_significant, constant));

      case '[': {
	SCM ref, read_bracket_func = SYMBOL_VALUE(sym_read_bracket, ref);

	if (read_bracket_func != STk_void) {
	  STk_ungetc(c, port);
	  return STk_C_apply(read_bracket_func, 1, port);
	}
	return(read_list(port, ']', case_significant, constant));
      }

    case '{': {
      SCM ref, read_brace_func = SYMBOL_VALUE(sym_read_brace, ref);

	if (read_brace_func != STk_void) {
	  STk_ungetc(c, port);
	  return STk_C_apply(read_brace_func, 1, port);
	}
	goto default_case;
      }

      case ')':
      case ']':
      case '}':
	if (inlist) {
	  STk_ungetc(c, port);
	  return STk_close_par;
	}  
	warning_parenthesis(port);
	break;
      case '\'':
 	return LIST2(STk_sym_quote, 
		     read_rec(port, case_significant, constant, inlist));
      case '`':
 	return LIST2(sym_quasiquote, 
		     read_rec(port, case_significant, constant, inlist));
      case '#':
	switch(c=STk_getc(port)) {
	  case 't':
          case 'T':  return STk_true;
	  case 'f':
 	  case 'F':  if (STk_uvectors_allowed) 
	    	       return maybe_read_uniform_vector(port, c, case_significant);
	  	     else
		       return STk_false;
 	  case '\\': return read_char(port, STk_getc(port));
	  case '(' : return read_vector(port, case_significant, constant);
	  case '!' : { /* This can be a comment or a DSSSL keyword */
	    	       c = STk_getc(port);
		       if (c == 'o' || c == 'k' || c == 'r') {
			 SCM word = read_token(port, c, FALSE);

			 if (SYMBOLP(word)) {
			   char *s = SYMBOL_PNAME(word);
			   
			   if ((strcmp(s, "optional") == 0) ||
			       (strcmp(s, "key")      == 0) ||
			       (strcmp(s, "rest")     == 0))
			     return STk_makekey(s);
			 }
		       }
		       /* if we are here, consider the rest of the line 
			* as a comment*/
		       do {
			 if (c == EOF) return STk_eof;
		       } while ((c=STk_getc(port)) != '\n');
		       STk_ungetc(c, port);
		       continue;
	  	     }
	  case '|':  {
		       char prev = ' ';

		       comment_level += 1;
		       for ( ; ; ) {
			 switch (c = STk_getc(port)) {
			   case EOF:
			     goto end_comment;
			   case '\n':
			     break;
			   case '#': 
			     if (prev == '|') {
			       comment_level -= 1;
			       if (!comment_level) goto end_comment;
			     }
			     break;
			   case '|':
			     if (prev == '#')
			       comment_level += 1;
			     break;
			   default: ;
			 }
			 prev = c;
		       }
		       end_comment:
		       c = flush_spaces(port, (char *) NULL, (SCM) NULL);
		       if (c == EOF) {
			 if (comment_level)
			   signal_error(port, 
					"eof encountered when reading a comment",
					STk_nil);
			 else
			   return STk_eof;
		       } else {
			 STk_ungetc(c,port);
			 continue;
		       }
		    }
	  case 'p':
	  case 'P': return read_address(port);
	  case 'S':
	  case 's':
	  case 'U':
	  case 'u': if (STk_uvectors_allowed)
		      return maybe_read_uniform_vector(port, c, case_significant);
	  	    else 
		      goto unknown_sharp;
		      
	 case ';': /* R6RS comments */
	   	   read_rec(port, case_significant, constant, FALSE);
		   c = flush_spaces(port, NULL, NULL);
		   STk_ungetc(c, port);
		   if (inlist && (c == ')' || c == ']' || c == '}'))
		     return STk_close_par;
		   continue;
	  case ',': /* SRFI-10 */
	    	    return read_srfi10(port, 
				       read_rec(port, case_significant, 
						constant, inlist));
	  case '0':
	  case '1':
	  case '2':
	  case '3':
	  case '4':
	  case '5':
	  case '6':
	  case '7':
	  case '8':
	  case '9': return read_cycle(port, c, case_significant, constant);
	  default:  
	unknown_sharp:
	  	    STk_ungetc(c, port); return read_token(port, '#', FALSE);
	}
      case ',': {
	SCM symb;

	c = STk_getc(port);
 	if (c == '@')
	  symb = sym_unquote_splicing;
 	else {
 	  symb = sym_unquote; 
 	  STk_ungetc(c, port);
 	}
	return LIST2(symb, read_rec(port, case_significant, constant, inlist));
      }
      case '"':
	return read_string(port, constant);
      default:
    default_case: {
	  SCM tmp = read_token(port, c, case_significant);

	  return ((tmp == sym_dot) && inlist) ? STk_dot : tmp;
	}
    }
  }
}

/*===========================================================================*\
 * 
 * STk_read 
 * STk_read_constant
 * 
 * 	The two entry points of the reader. STk_read_constant just set a
 *      flag to say that the object read (for strings, lists and vectors) is a 
 * 	constant
 * 
\*===========================================================================*/
static SCM read_it(SCM port, int case_significant, int constant)
{
  int c;
  SCM l, res;
  
  cycles        = STk_nil;
  comment_level = 0;

  c = flush_spaces(port, (char *) NULL, (SCM) NULL);
  if (c == EOF) return(STk_eof);
  STk_ungetc(c, port);

  res = read_rec(port, case_significant, constant, FALSE);

  if (!NULLP(cycles)) {
    l = find_references(&res, STk_nil);
    patch_references(port, l);
  }
  return res;
}


SCM STk_read(SCM port, int case_significant)
{
  return read_it(port, case_significant, FALSE);
}


SCM STk_read_constant(SCM port, int case_significant)
{
  return read_it(port, case_significant, TRUE);
}


char *STk_quote2str(SCM symb)
{
  if (symb == STk_sym_quote) 		return "\'";
  if (symb == sym_quasiquote) 		return "`";
  if (symb == sym_unquote) 		return ",";
  if (symb == sym_unquote_splicing) 	return ",@";
  return NULL;
}

/*=======================================================================*\
 *
 * SRFI-10 support
 *
\* ======================================================================*/
static SCM ctor_table = STk_nil;

DEFINE_PRIMITIVE("define-reader-ctor",reader_ctor, subr2, (SCM symbol, SCM proc))
{
  SCM tmp;

  if (!SYMBOLP(symbol))
    STk_error("bad symbol ~S", symbol);
  if (STk_procedurep(proc) == STk_false)
    STk_error("bad procedure ~S", proc);
  
  tmp = STk_int_assq(symbol, ctor_table);
  if (tmp != STk_false)
    CDR(tmp) = proc;
  else
    ctor_table = STk_cons(STk_cons(symbol, proc), ctor_table);
  
  return STk_void;
}


static SCM read_srfi10(SCM port, SCM l)
{
  int len = STk_int_length(l);
  SCM tmp, ref;

  if (len < 0) 
    signal_error(port, "bad list in a #,(...) form ~S", l);

  tmp = STk_int_assq(CAR(l), ctor_table);
  if (tmp == STk_false)
    signal_error(port, "bad tag in a #,(...) form ~S", CAR(l));
  else 
    /* result is (apply (cdr tmp) (cdr l)) */
    return STk_C_apply(STk_lookup(STk_intern("apply"),
				  STk_current_module, &ref, TRUE),
		       2,
		       CDR(tmp),
		       CDR (l));
  
  return STk_void;	/* For the C compiler */
}


/*
<doc EXT read-case-sensitive
 * (read-case-sensitive)
 * (read-case-sensitive value)
 *
 * This parameter object permits to change the default behaviour of 
 * the |read| primitive when reading a symbol. If this parameter has a 
 * a true value a symbol is not converted to a default case when interned. 
 * Since ,(rfive) requires that symbol are case insignificant, the default 
 * value  of this parameter is �f.
 * @lisp
 * (read-case-sensitive)        => �f
 * (define x 'Symbol)
 * (display x)             @print{} symbol
 * (read-case-sensitive �t)
 * (define y 'Symbol)
 * (display y)             @print{} Symbol
 * @end lisp
 * ,(bold "Note:")  Default behaviour can be changed for a whole execution 
 * with the |--case-sensitive| option.
 * �
 * ,(bold "Note:") See also syntax for ,(ref :mark "bar-in-symbol" :text  
 * [special characters]) in symbols.
 *
doc>
*/
static SCM read_case_sensitive_conv(SCM value)
{
  STk_read_case_sensitive = (value != STk_false);
  return MAKE_BOOLEAN(STk_read_case_sensitive);
}



/*===========================================================================*\
 * 
 *			I n i t i a l i z a t i o n 
 * 
\*===========================================================================*/
int STk_init_reader(void)
{
  STk_sym_quote        = STk_intern("quote");
  sym_quasiquote       = STk_intern("quasiquote");
  sym_unquote	       = STk_intern("unquote");
  sym_unquote_splicing = STk_intern("unquote-splicing");
  sym_dot	       = STk_intern(".");
  sym_read_bracket     = STk_intern("%read-bracket");
  sym_read_brace       = STk_intern("%read-brace");
  
  /* read-error condition */
  read_error = STk_defcond_type("&read-error", STk_err_mess_condition,
				LIST4(STk_intern("line"),
				      STk_intern("column"),
				      STk_intern("position"),
				      STk_intern("span")),
				STk_current_module);

  
  /* Declare SRFI-10 support function */
  ADD_PRIMITIVE(reader_ctor);

  /* Declare parameter read-case-sensitve */
  STk_make_C_parameter("read-case-sensitive",
		       MAKE_BOOLEAN(STk_read_case_sensitive),
		       read_case_sensitive_conv);
  return TRUE;
}