/*
 *  p o r t . c			-- ports implementation
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
 *            Author: Erick Gallesio [eg@unice.fr]
 *    Creation date: 17-Feb-1993 12:27
 * Last file update:  1-Feb-2006 17:11 (eg)
 *
 */

#include <ctype.h>
#include "stklos.h"
#include "../pcre/pcreposix.h"



#define INITIAL_LINE_SIZE 256		/* Initial size for readline */

static SCM CrLf;			/* used in read-line only */

static SCM io_error, io_port_error, io_read_error, io_write_error,
  io_closed_error, io_fn_error, io_malformed, io_prot_error, 
  io_ro_error, io_exists_error, io_no_file_error, io_bad_param;


static void general_io_error(SCM type, char *format, SCM f)
{
  STk_raise_exception(STk_make_C_cond(type,
				      4,
				      STk_false,
				      STk_vm_bt(),
				      STk_format_error(format, f),
				      f));
}

static void error_closed_port(SCM p)
{
  general_io_error(io_closed_error, "port ~S is closed", p);
}
	    
void STk_error_bad_io_param(char *fmt, SCM p)
{
  general_io_error(io_bad_param, fmt, p);
}

void STk_error_file_name(char *fmt, SCM fn)
{
  general_io_error(io_fn_error, fmt, fn);
}


void STk_error_bad_port(SCM p)
{
  general_io_error(io_port_error, "bad port ~S", p);
}


void STk_error_bad_file_name(SCM f)
{
  general_io_error(io_malformed, "bad file name ~S", f);
}

void STk_error_cannot_load(SCM f)
{
  general_io_error(io_prot_error, "cannot load file ~S", f);
}



static SCM verify_port(SCM port, int mode)
{
  if (mode == PORT_WRITE) {
    if (!port) return STk_curr_oport;
    if (!OPORTP(port)) STk_error_bad_port(port);
  } else {
    if (!port) return STk_curr_iport;
    if (!IPORTP(port)) STk_error_bad_port(port);
  }
  if (PORT_IS_CLOSEDP(port)) error_closed_port(port);
  return port;
}


/*
<doc  input-port? output-port?
 * (input-port? obj)
 * (output-port? obj)
 *
 * Returns |#t| if |obj| is an input port or output port respectively, 
 * otherwise returns #f.
doc>
 */
DEFINE_PRIMITIVE("input-port?", input_portp, subr1, (SCM port))
{
  return MAKE_BOOLEAN(IPORTP(port));
}

DEFINE_PRIMITIVE("output-port?", output_portp, subr1, (SCM port))
{
  return MAKE_BOOLEAN(OPORTP(port));
}


/*
<doc EXT interactive-port?
 * (interactive-port? port)
 *
 * Returns |#t| if |port| is connected to a terminal and |#f| otherwise.
doc>
 */
DEFINE_PRIMITIVE("interactive-port?", interactive_portp, subr1, (SCM port))
{
  if (!PORTP(port)) STk_error_bad_port(port);

  return MAKE_BOOLEAN(PORT_FLAGS(port) & PORT_IS_INTERACTIVE);
}


/*
<doc  current-input-port current-output-port
 * (current-input-port obj)
 * (current-output-port obj)
 *
 * Returns the current default input or output port.
doc>
 */
DEFINE_PRIMITIVE("current-input-port",current_input_port, subr0, (void))
{
  return STk_curr_iport;
}

DEFINE_PRIMITIVE("current-output-port",current_output_port, subr0, (void))
{
  return STk_curr_oport;
}

/*
<doc EXT current-error-port
 * (current-error-port obj)
 *
 * Returns the current default error port.
doc>
 */
DEFINE_PRIMITIVE("current-error-port",current_error_port, subr0, (void))
{
  return STk_curr_eport;
}


DEFINE_PRIMITIVE("%set-std-port!", set_std_port, subr2, (SCM index, SCM port))
{
  switch (AS_LONG(index)) {
    case SCM_LONG(0): if (!IPORTP(port)) goto badport; STk_curr_iport = port; break;
    case SCM_LONG(1): if (!OPORTP(port)) goto badport; STk_curr_oport = port; break;
    case SCM_LONG(2): if (!OPORTP(port)) goto badport; STk_curr_eport = port; break;
    default: STk_error_bad_io_param("bad code ~S", index);
  }
  return STk_void;
badport:
  STk_error_bad_port(port);
  return STk_void;
}


/*=============================================================================*\
 * 				Read
\*=============================================================================*/

/*
<doc read
 * (read)
 * (read port)
 *
 * |Read| converts external representations of Scheme objects into the
 * objects themselves. |Read| returns the next object parsable from the given
 * input port, updating port to point to the first character past the end of
 * the external representation of the object.
 * �
 * If an end of file is encountered in the input before any characters are found
 * that can begin an object, then an end of file object is returned. The port
 * remains open, and further attempts to read will also return an end of file
 * object. If an end of file is encountered after the beginning of an object's
 * external representation, but the external representation is incomplete 
 * and therefore not parsable, an error is signalled.
 * �
 * The port argument may be omitted, in which case it defaults to the value
 * returned by |current-input-port|. It is an error to read from a closed port.
 * �
 * ,(stklos) |read| supports the ,(link-srfi 10) |#,()| form that can be used
 * to denote values that do not have a convenient printed representation. See 
 * the SRFI document for more information.
doc>
 */
/*
<doc EXT read-with-shared-structure
 * (read-with-shared-structure)
 * (read-with-shared-structure  port)
 * (read/ss)
 * (read/ss port)
 *
 * |read-with-shared-structure| is identical to |read|. It has been added to 
 * be compatible with ,(link-srfi 38). STklos always knew how to deal with 
 * recursive input data. |read/ss| is only a shorter name for 
 * |read-with-shared-structure|.
 * 
doc>
<doc EXT define-reader-ctor
 * (define-reader-ctor tag proc)
 * 
 * This procedure permits to define a new user to reader constructor procedure
 * at run-time. It is defined in ,(link-srfi 10) document. See  SRFI document 
 * for more information.
 * @lisp
 * (define-reader-ctor 'rev (lambda (x y) (cons y x)))
 * (with-input-from-string "#,(rev 1 2)" read)
 *                              => (2 . 1)
 * @end lisp
doc>
 */
DEFINE_PRIMITIVE("read", scheme_read, subr01, (SCM port))
{
  port = verify_port(port, PORT_READ);
  return STk_read(port, STk_read_case_sensitive);
}



/* The same one but for reading code => code is really constant */
DEFINE_PRIMITIVE("%read", scheme_read_cst, subr01, (SCM port))
{
  port = verify_port(port, PORT_READ);
  return STk_read_constant(port, STk_read_case_sensitive);
}


/*
<doc  read-char
 * (read-char)
 * (read-char port)
 *
 * Returns the next character available from the input |port|, updating the |port|
 * to point to the following character. If no more characters are available, 
 * an end of file object is returned. |Port| may be omitted, in which case 
 * it defaults to the value returned by |current-input-port|.
doc>
 */
DEFINE_PRIMITIVE("read-char", read_char, subr01, (SCM port))
{
  int c;

  port = verify_port(port, PORT_READ);
  c = STk_getc(port);
  return (c == EOF) ? STk_eof : MAKE_CHARACTER(c);
}

/*
<doc EXT read-chars
 * (read-chars size)
 * (read-chars size port)
 *
 * Returns a newly allocated string made of |size| characters read from |port|.
 * If less than |size| characters are available on the input port, the returned
 * string is smaller than |size| and its size is the number of available 
 * characters. |Port| may be omitted, in which case it defaults to the
 * value returned by |current-input-port|.
doc>
 */
DEFINE_PRIMITIVE("read-chars", read_chars, subr12, (SCM size, SCM port))
{
  int count, n = STk_integer_value(size);
  SCM z;

  port = verify_port(port, PORT_READ);
  if (n < 0) STk_error("bad length");

  /* Allocate a new string for result  */
  z     = STk_makestring(n, NULL);
  count = STk_read_buffer(port, STRING_CHARS(z), n);
  
  if (count == 0) 
    return STk_eof;
  if (count < n) {
    /* String is shorter than the allocated one */
    STRING_CHARS(z)[count] = '\0';
    return STk_makestring(count, STRING_CHARS(z));
  }
  return z;
}

/*
<doc EXT read-chars!
 * (read-chars! str)
 * (read-chars! str port)
 *
 * This function reads the characters available from |port| in the string |str|
 * by chuncks whose size is equal to the length of |str|.
 * The value returned by |read-chars!|is an integer indicating the number
 * of characters read. |Port| may be omitted, in which case it defaults to the
 * value returned by |current-input-port|. 
 * �
 * This function is similar to |read-chars| except that it avoids to allocate 
 * a new string for each read. 
 * @lisp
 * (define (copy-file from to)
 *   (let* ((size 1024)
 *          (in  (open-input-file from))
 *          (out (open-output-file to))
 *          (s   (make-string size)))
 *     (let Loop ()
 *       (let ((n (read-chars! s in)))
 *         (cond
 *           ((= n size)
 *              (write-chars s out)
 *              (Loop))
 *           (else
 *              (write-chars (substring s 0 n) out)
 *              (close-port out)))))))
 * @end lisp
doc>
 */
DEFINE_PRIMITIVE("read-chars!", d_read_chars, subr12, (SCM str, SCM port))
{
  port = verify_port(port, PORT_READ);
  if (!STRINGP(str)) STk_error("bad string ~S", str);

  return MAKE_INT(STk_read_buffer(port, STRING_CHARS(str), STRING_SIZE(str)));
}

/*
<doc  peek-char
 * (peek-char)
 * (peek-char port)
 *
 * Returns the next character available from the input |port|, without updating 
 * the port to point to the following character. If no more characters are
 * available, an end of file object is returned. |Port| may be omitted, in
 * which case it defaults to the value returned by |current-input-port|.
 * �
 * ,(bold "Note:") The value returned by a call to |peek-char| is the same as the
 * value that would have been returned by a call to |read-char| with the same
 * port. The only difference is that the very next call to |read-char| or 
 * |peek-char| on that port will return the value returned by the preceding
 * call to |peek-char|. In particular, a call to |peek-char| on an interactive
 * port will hang waiting for input whenever a call to |read-char| would have
 * hung.
doc>
 */
DEFINE_PRIMITIVE("peek-char", peek_char, subr01, (SCM port))
{
  int c;

  port = verify_port(port, PORT_READ);
  c = STk_getc(port);
  STk_ungetc(c, port);

  return (c == EOF) ? STk_eof : MAKE_CHARACTER(c);
}


/*
<doc  eof-object?
 * (eof-object? obj)
 *
 * Returns |#t| if |obj| is an end of file object, otherwise returns |#f|. 
doc>
 */
DEFINE_PRIMITIVE("eof-object?", eof_objectp, subr1, (SCM obj))
{
  return MAKE_BOOLEAN(obj == STk_eof);
}


/*
<doc EXT eof-object
 * (eof-object)
 * 
 * ,(index "#eof")
 * Returns an end of file object. Note that the special notation |#eof| is 
 * another way to return such an end of file object.
doc>
 */
DEFINE_PRIMITIVE("eof-object", eof_object, subr0, (void))
{
  return STk_eof;
}


/*
<doc  char-ready?
 * (char-ready?)
 * (char-ready? port)
 *
 * Returns |#t| if a character is ready on the input port and returns |#f|
 * otherwise. If char-ready returns |#t| then the next read-char operation on
 * the given port is guaranteed not to hang. If the port is at end of file
 * then |char-ready?| returns |#t|. Port may be omitted, in which case it
 * defaults to the value returned by |current-input-port|.
doc>
 */
DEFINE_PRIMITIVE("char-ready?", char_readyp, subr01, (SCM port))
{
  port = verify_port(port, PORT_READ);
  return MAKE_BOOLEAN(STk_readyp(port));
}

/*=============================================================================*\
 * 				Write
\*=============================================================================*/


/*
<doc  write
 * (write obj)
 * (write obj port)
 *
 * Writes a written representation of |obj| to the given |port|. Strings that
 * appear in the written representation are enclosed in doublequotes, and 
 * within those strings backslash and doublequote characters are escaped
 * by backslashes. Character objects are written using the ,(emph "#\\") notation. 
 * |Write| returns an unspecified value. The |port| argument may be omitted, in
 * which case it defaults to the value returned by |current-output-port|.
doc>
 */
DEFINE_PRIMITIVE("write", write, subr12, (SCM expr, SCM port))
{
  port = verify_port(port, PORT_WRITE);
  STk_print(expr, port, WRT_MODE);
  return STk_void;
}


/*
<doc EXT write*
 * (write* obj)
 * (write* obj port)
 *
 * Writes a written representation of |obj| to the given port.  The
 * main difference with the |write| procedure is that |write*|
 * handles data structures with cycles. Circular structure written by 
 * this procedure use the ,(code (q "#n=")) and ,(code (q "#n#"))
 * notations (see ,(ref :mark "Circular structure")).
 * 
doc>
<doc EXT write-with-shared-structure
 * (write-with-shared-structure obj)
 * (write-with-shared-structure obj port)
 * (write-with-shared-structure obj port optarg)
 * (write/ss obj)
 * (write/ss obj port)
 * (write/ss obj port optarg)
 *
 * |write-with-shared-structure| has been added to be compatible with 
 * ,(link-srfi 38). It is is identical to |write*|, except that it accepts one 
 * more parameter (|optarg|). This parameter, which is not specified 
 * in ,(srfi 38), is always ignored. |write/ss| is only a shorter name for
 * |write-with-shared-structure|.
 * 
doc>
*/
DEFINE_PRIMITIVE("write*", write_star, subr12, (SCM expr, SCM port))
{
  port = verify_port(port, PORT_WRITE);
  STk_print_star(expr, port);
  return STk_void;
}

/*
<doc  display
 * (display obj)
 * (display obj port)
 *
 * Writes a representation of |obj| to the given |port|. Strings that
 * appear in the written representation are not enclosed in
 * doublequotes, and no characters are escaped within those
 * strings. Character objects appear in the representation as if
 * written by |write-char| instead of by |write|. |Display| returns an
 * unspecified value. The |port| argument may be omitted, in which
 * case it defaults to the value returned by |current-output-port|.
 * �
 * ,(bold "Rationale:") |Write| is intended for producing machine-readable
 * output and |display| is for producing human-readable output. 
doc>
 */
DEFINE_PRIMITIVE("display", display, subr12, (SCM expr, SCM port))
{
  port = verify_port(port, PORT_WRITE);
  STk_print(expr, port, DSP_MODE);
  return STk_void;
}

/*
<doc  newline
 * (newline)
 * (newline port)
 *
 * Writes an end of line to |port|. Exactly how this is done differs from
 * one operating system to another. Returns an unspecified value. The |port|
 * argument may be omitted, in which case it defaults to the value returned
 * by |current-output-port|.
doc>
 */
DEFINE_PRIMITIVE("newline", newline, subr01, (SCM port))
{
  port = verify_port(port, PORT_WRITE);
  STk_putc('\n', port);
  return STk_void;
}



/*
<doc  write-char
 * (write-char char)
 * (write-char char port)
 *
 * Writes the character |char| (not an external representation of the
 * character) to the given |port| and returns an unspecified value. 
 * The |port| argument may be omitted, in which case it defaults to the
 * value returned by |current-output-port|.
doc>
 */
DEFINE_PRIMITIVE("write-char", write_char, subr12, (SCM c, SCM port))
{
  if (!CHARACTERP(c)) STk_error_bad_io_param("bad character ~S", c);
  port = verify_port(port, PORT_WRITE);
  STk_putc(CHARACTER_VAL(c), port);
  return STk_void;
}


/*
<doc EXT write-chars
 * (write-chars str)
 * (write-char str port)
 * 
 * Writes the character of string |str| to the given |port| and
 * returns an unspecified value.  The |port| argument may be omitted,
 * in which case it defaults to the value returned by
 * |current-output-port|. ,(bold "Note:") This function is generally 
 * faster than |display| for strings. Furthermore, this primitive does 
 * not use the buffer associated to |port|.
 * 
doc>
 */
DEFINE_PRIMITIVE("write-chars", write_chars, subr12, (SCM str, SCM port))
{
  if (!STRINGP(str)) STk_error_bad_io_param("bad string ~S", str);
  port = verify_port(port, PORT_WRITE);
  STk_write_buffer(port, STRING_CHARS(str), STRING_SIZE(str));
  return STk_void;
}


/*===========================================================================*\
 * 
 * 			S T k   b o n u s
 *
\*===========================================================================*/
#define FMT_SIZE 7


static SCM internal_format(int argc, SCM *argv, int error)
     /* a very simple and poor format */ 
{
  SCM port, fmt;
  int format_in_string = 0;
  char *p, *start_fmt = "", prev_char;
  
  if (error) {
    if (argc < 1) goto Bad_list;
    format_in_string = 1;
    port = STk_open_output_string();
    argc -= 1;
  }
  else {
    if (STRINGP(*argv)) {
      /* This is a SRFI-28 format */
      format_in_string = 1;
      port = STk_open_output_string();
      argc -= 1;
    } else {
      if (argc < 2) goto Bad_list;
      port = *argv--; 
      argc -= 2;
      
      if (BOOLEANP(port)){
	if (port == STk_true) port = STk_curr_oport;
	else {
	  format_in_string = 1;
	  port = STk_open_output_string();
	}
      } else {
	verify_port(port, PORT_WRITE);
      }
    }
  }

  fmt = *argv--;
  if (!STRINGP(fmt)) STk_error_bad_io_param("bad format string ~S", fmt);

  /* Parse the format string */
  start_fmt = STRING_CHARS(fmt);
  prev_char = '\n';

  for(p = start_fmt; *p; p++) {
    if (*p == '~') {
      switch(*(++p)) {
        case 'A':
	case 'a': {
		    SCM tmp;

		    if (argc-- <= 0) goto TooMuch;
		    tmp = *argv--;
		    if (STRINGP(tmp)) {
		      if (STRING_SIZE(tmp) > 0)
			prev_char = STRING_CHARS(tmp)[STRING_SIZE(tmp) - 1];
		    }
		    else if (CHARACTERP(tmp))
		      prev_char= CHARACTER_VAL(tmp);
		    
		    STk_print(tmp, port, DSP_MODE);
		    continue;		/* because we set ourselves prev_char */
		  }
        case 'S':
        case 's': if (argc-- <= 0) goto TooMuch;
                  STk_print(*argv--, port, WRT_MODE);
	          break;
        case 'W':
        case 'w': if (argc-- <= 0) goto TooMuch;
	  	  STk_print_star(*argv--, port);
	          break;
        case 'X':
        case 'x': if (argc-- <= 0) goto TooMuch; 
	  	  STk_print(STk_number2string(*argv--, MAKE_INT(16)),port,DSP_MODE); 
		  break;
        case 'D':
        case 'd': if (argc-- <= 0) goto TooMuch; 
	  	  STk_print(STk_number2string(*argv--, MAKE_INT(10)),port,DSP_MODE); 
		  break;
        case 'O': 
        case 'o': if (argc-- <= 0) goto TooMuch; 
	  	  STk_print(STk_number2string(*argv--, MAKE_INT(8)),port,DSP_MODE); 
		  break;
        case 'B':
        case 'b': if (argc-- <= 0) goto TooMuch; 
	  	  STk_print(STk_number2string(*argv--, MAKE_INT(2)),port,DSP_MODE); 
		  break;
        case 'C':
        case 'c': if (argc-- <= 0) goto TooMuch; 
	  	  if (!CHARACTERP(*argv)) 
		    STk_error_bad_io_param("bad character ~S", *argv);
		  prev_char = CHARACTER_VAL(*argv);
		  STk_print(*argv--, port, DSP_MODE);
		  continue;	/* because we set ourselves prev_char */
        case 'Y':
	case 'y': {					/* Yuppify */
		      SCM ref, pp;
	  	      
		      if (argc-- <= 0) goto TooMuch;
		      pp = STk_lookup(STk_intern("pp"), 
				      STk_current_module, 
				      &ref, 
				      TRUE);
		      STk_print(STk_C_apply(pp, 3, *argv--, 
					    STk_makekey("port"),
					    STk_false),
				port,
				WRT_MODE);
		      prev_char = '\n'; /* since our pp always add a newline */
		      continue;		/* because we set ourselves prev_char */
	}
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9': {
	  	  char width[FMT_SIZE], digits[FMT_SIZE];
		  SCM ff, ref, tmp;
		  int i;
		  
		  if (argc-- <= 0) goto TooMuch;
		  
		  for (i=0; isdigit(*p); i++) {
		    if (i >= FMT_SIZE) goto Incorrect_format_width;
		    width[i] = *p++;
		  }
		  width[i] = '\0';

		  if (*p == ',') {
		    p++;
		    for (i=0; isdigit(*p); i++) {
		      if (i >= FMT_SIZE) goto Incorrect_format_width;
		      digits[i] = *p++;
		    }
		    digits[i] = '\0';
		  }
		  if (*p != 'f' && *p != 'F') goto Incorrect_format_width;

		  /* width and digits are strings which contains the width 
		   * and the number of digits for the format
		   * Call the Scheme routine srfi48:format-fixed
		   */
		  ff = STk_lookup(STk_intern("srfi48:format-fixed"), 
				  STk_current_module, 
				  &ref, 
				  TRUE);
		  tmp = STk_C_apply(ff, 3, 
					*argv--,
					STk_Cstr2number(width, 10L),
				    STk_Cstr2number(digits, 10L));
		  if (STRINGP(tmp)) {
		    if (STRING_SIZE(tmp) > 0)
		      prev_char = STRING_CHARS(tmp)[STRING_SIZE(tmp) - 1];
		  }
		  STk_print(tmp, port, DSP_MODE);
		  continue;
	}
	case '?':
        case 'K':
        case 'k': {
	  	    SCM fmt, ref,args;
		    int len;
		    
		    if (argc-- <= 0) goto TooMuch;
		    fmt = *argv--;
		    if (!STRINGP(fmt)) 
		      STk_error_bad_io_param("bad string for ~~? format ~S", fmt);

		    if (argc-- <= 0) goto TooMuch;
		    args = *argv--;
		    len  = STk_int_length(args);
		    if (len < 0) 
		      STk_error_bad_io_param("bad list for ~~? format ~S", args);

		    /* Do (apply format port fmt args) */
		    STk_C_apply(STk_lookup(STk_intern("apply"),
					   STk_current_module, &ref, TRUE),
				4,
				STk_lookup(STk_intern("format"),
					   STk_current_module, &ref, TRUE),
				port,
				fmt,
				args);
		    break;
		  }
        case 'H':
        case 'h': {					/* Help */
	  	     SCM ref, help;
	  	      
		      help = STk_lookup(STk_intern("srfi48:help"), 
					STk_current_module, 
					&ref, 
					TRUE);
		      STk_C_apply(help, 1, port);
		      break;
	}
        case 'T': 
      	case 't': STk_putc('\t', port);
		  break;
        case '_': STk_putc(' ',port);
		  break;
        case '&': if (prev_char == '\n') continue;
        case '%': STk_putc('\n', port);
	  	  prev_char = '\n'; 
                  continue;
        case '~': STk_putc('~', port);
                  break;
        default:  STk_putc('~',  port);
	  	  STk_putc(*p, port);
      }
      prev_char = '?';
    } else {
      /* Not a ~ sequence */
      prev_char = *p;
      STk_putc(*p, port);
    }
  }

  /* Verify that it doesn't remain arguments on the list */
  if (argc) 
    STk_error_bad_io_param("too few ``~~'' in format string %S", start_fmt);

  return format_in_string ? STk_get_output_string(port) : STk_void;

TooMuch:
  STk_error_bad_io_param("too many ``~~'' in format string %S", start_fmt);
Bad_list:
  STk_error_bad_io_param("bad list of parameters ~S", *argv);
Incorrect_format_width:
  STk_error_bad_io_param("Format too long or 'f' expected in %S", start_fmt);

  return STk_void;
}

/*
<doc EXT format 
 * (format port str obj ...)
 * (format str obj)
 *
 * Writes the |obj|s to the given |port|, according to the format
 * string |str|. |Str| is written literally, except for the following 
 * sequences:
 *
 * ,(itemize
 * (item [|~a| or |~A| is replaced by the printed representation
 * of the next |obj|.])
 * 
 * (item [|~s| or |~S| is replaced by the ``slashified'' printed
 * representation of the next |obj|.])
 *
 * (item [|~w| or |~W| is replaced by the printed representation
 * of the next |obj| (circular structures are correctly handled and
 * printed using |write*|).])
 *
 * (item [|~d| or |~D| is replaced by the decimal printed representation
 * of the next |obj| (which must be a number).])
 *
 * (item [|~x| or |~X| is replaced by the hexadecimal printed representation
 * of the next |obj| (which must be a number).])
 * 
 * (item [|~o| or |~O| is replaced by the octal printed representation
 * of the next |obj| (which must be a number).])
 * 
 * (item [|~b| or |~B| is replaced by the binary printed representation
 * of the next |obj| (which must be a number).])
 * 
 * (item [|~c| or |~C| is replaced by the printed representation
 * of the next |obj| (which must be a character).])
 * 
 * (item [|~y| or |~Y| is replaced by the pretty-printed representation
 * of the next |obj|. The standard pretty-printer is used here.])
 * 
 * (item [|~?| is replaced by the result of the recursive call of |format|
 * with the two next |obj|.])
 * 
 * (item [|~k| or |~K| is another name for |~?|])
 * 
 * (item [|~\[w\[,d\]\]f| or |~\[w\[,d\]\]F| is replaced by the printed
 * representation of next |obj| (which must be a number) with width |w|
 * and |d| digits after the decimal. Eventually, |d| may be omitted.])
 *
 * (item [|~~| is replaced by a single tilde character.])
 *
 * (item [|~%| is replaced by a newline])
 *
 * (item [|~t| or |~t| is replaced by a tabulation character.])
 * 
 * (item [|~&| is replaced by a newline character if it is known that the
 * previous character was not a newline])
 * 
 * (item [|~_| is replaced by a space])
 * 
 * (item [|~h| or |~H| provides some help])
 * 
 * )
 *
 * |Port| can be a boolean or a port. If |port| is |#t|, output goes to
 * the current output port; if |port| is |#f|, the output is returned as a
 * string.  Otherwise, the output is printed on the specified port.
 * @lisp
 *    (format #f "A test.")        => "A test."
 *    (format #f "A ~a." "test")   => "A test."
 *    (format #f "A ~s." "test")   => "A \\"test\\"."
 *    (format "~8,2F" 1/3)         => "    0.33"
 *    (format "~6F" 32)            => "    32"
 *    (format "~1,2F" 4321)        => "4321.00"
 *    (format "~1,2F" (sqrt -3.9)) => "0.00+1.97i"
 *    (format "#d~d #x~x #o~o #b~b~%" 32 32 32 32)
 *                                 => "#d32 #x20 #o40 #b100000\\n"
 *    (format #f "~&1~&~&2~&~&~&3~%")
 *                                 => "1\\n2\\n3\\n"
 *    (format "~a ~? ~a" 'a "~s" '(new) 'test)
 *                                 => "a new test"
 * @end lisp
 * 
 * ,(bold "Note:") The second form of |format| is compliant with 
 * ,(link-srfi 28). That is, when
 * |port| is omitted, the output is returned as a string as if |port| was 
 * given the value |#f|.
 * �
 * ,(bold "Note:") Since version 0.58, |format| is also compliant with 
 * ,(link-srfi 48).
doc>
 */
DEFINE_PRIMITIVE("format", format, vsubr, (int argc, SCM *argv))
{
  return internal_format(argc, argv, FALSE);
}


/*
<doc EXT error
 * (error str obj ...)
 * (error name str obj ...)
 *
 * |error| is used to signal an error to the user. The second form 
 * of |error| takes  a symbol as first parameter; it is generally used for the 
 * name of the procedure which raises the error.
 * �
 * ,(bold "Note:") The specification string may follow the 
 * ,(emph "tilde conventions") 
 * of |format| (see ,(ref :mark "format")); in this case this procedure builds an 
 * error message according to the specification given in |str|. Otherwise, 
 * this procedure is conform to the |error| procedure defined in 
 * ,(link-srfi 23) and  |str| is printed with the |display| procedure, 
 * whereas the |obj|s are printed  with the |write| procedure. 
 *
 * �
 * Hereafter, are some calls of the |error| procedure using a formatted string
 * @lisp
 * (error "bad integer ~A" "a")
 *                      @print{} bad integer a
 * (error 'vector-ref "bad integer ~S" "a") 
 *                      @print{} vector-ref: bad integer "a"
 * (error 'foo "~A is not between ~A and ~A" "bar" 0 5)
 *                      @print{} foo: bar is not between 0 and 5
 * @end lisp
 *
 * and some conform to ,(srfi 23)
 * @lisp
 * (error "bad integer" "a")
 *                     @print{} bad integer "a"
 * (error 'vector-ref "bad integer" "a")
 *                    @print{} vector-ref: bad integer "a"
 * (error "bar" "is not between" 0 "and" 5)
 *                    @print{} bar "is not between" 0 "and" 5
 * @end lisp
doc>
 */
static SCM srfi_23_error(int argc, SCM *argv)
{
  SCM port = STk_open_output_string();

  STk_print(*argv--, port, DSP_MODE); /* the message (we know that it exists) */
  for (argc--; argc; argc--) {
    STk_putc(' ', port);
    STk_print(*argv--, port, WRT_MODE);
  }
  STk_close_port(port);
  return STk_get_output_string(port);
}

static int msg_use_tilde(char *s)
{
  char *p;
  
  p = strchr(s, '~');
  return p ? (p[1] && strchr("aAsSwW~", p[1]) != NULL): 0;
}


DEFINE_PRIMITIVE("error", scheme_error, vsubr, (int argc, SCM *argv))
{
  SCM who = STk_false;

  if (argc > 0) {
    if (SYMBOLP(*argv)) {
      who = *argv;
      argc -= 1;
      argv -= 1;
    }
    if (argc > 0) {
      SCM msg;
      
      /* See if we have a formatted message or a plain SRFI-23 call */
      if (STRINGP(*argv) && !msg_use_tilde(STRING_CHARS(*argv)))
	msg = srfi_23_error(argc, argv);
      else 
	msg = internal_format(argc, argv, TRUE);
      STk_signal_error(who, msg);
    }
  }
  STk_signal_error(who, STk_Cstring2string(""));
  return STk_void;
}



/*
<doc close-input-port close-output-port
 * (close-input-port port)
 * (close-output-port port)
 *
 * Closes the port associated with |port|, rendering the port incapable of
 * delivering or accepting characters. These routines have no effect if the
 * port has already been closed. The value returned is ,(emph "void").
doc>
 */
DEFINE_PRIMITIVE("close-input-port", close_input_port, subr1, (SCM port))
{
  if (!IPORTP(port)) STk_error_bad_port(port);
  STk_close(port);
  return STk_void;
}

DEFINE_PRIMITIVE("close-output-port", close_output_port, subr1, (SCM port))
{
  if (!OPORTP(port)) STk_error_bad_port(port);
  STk_close(port);
  return STk_void;
}


/*
<doc EXT close-port
 * (close-port port)
 *
 * Closes the port associated with |port|.
doc>
 */
DEFINE_PRIMITIVE("close-port", close_port, subr1, (SCM port))
{
  if (!PORTP(port)) STk_error_bad_port(port);
  
  STk_close(port);
  return STk_void;
}

/*
<doc EXT port-closed?
 * (port-closed? port)
 *
 * Returns |#t| if |port| is closed and |#f| otherwise.
doc>
*/
DEFINE_PRIMITIVE("port-closed?", port_closed, subr1, (SCM port))
{
  if (!PORTP(port)) STk_error_bad_port(port);

  return MAKE_BOOLEAN(PORT_IS_CLOSEDP(port));
}


/*
<doc EXT read-line
 * (read-line)
 * (read-line port)
 *
 * Reads the next line available from the input port |port|. This function
 * returns 2 values: the first one is is the string which contains the line
 * read, and the second one is the end of line delimiter. The end of line
 * delimiter can be an end of file object, a character or a string in case 
 * of a multiple character delimiter. If no more characters are available 
 * on |port|, an end of file object is returned.  |Port| may be omitted, 
 * in which case it defaults to the value returned by |current-input-port|.
 * �
 * ,(bold "Note:") As said in ,(ref :mark "values"), if |read-line| is not
 * used in  the context of |call-with-values|, the second value returned by 
 * this procedure is ignored.
doc> 
*/
DEFINE_PRIMITIVE("read-line", read_line, subr01, (SCM port))
{
  int prev, c;
  char buffer[INITIAL_LINE_SIZE], *buff;
  size_t i, size = INITIAL_LINE_SIZE;
  SCM res, delim;

  port = verify_port(port, PORT_READ);
  buff = buffer;
  prev = ' ';

  for (i = 0; ; i++) {
    if (i == size) {
      /* We must enlarge the buffer */
      size += size / 2;
      if (i == INITIAL_LINE_SIZE) {
	/* This is the first resize. Pass from static to dynamic allocation */
	buff = STk_must_malloc(size);
	strncpy(buff, buffer, INITIAL_LINE_SIZE);
      }
      else 
	buff = STk_must_realloc(buff, size);
    }
    switch (c = STk_getc(port)) {
      case EOF:  res = (i == 0) ? STk_eof : STk_chars2string(buff, i);
		 if (buff != buffer) STk_free(buff);
		 return STk_n_values(2, res, STk_eof);

      case '\n': if (prev == '\r') 
		   { i -= 1; delim = CrLf; }
      		 else 
		   delim = MAKE_CHARACTER('\n');
	
		 res = STk_chars2string(buff, i);
		 if (buff != buffer) STk_free(buff);
		 return STk_n_values(2, res, delim);

      default:  buff[i] = prev = c; 
    }
  }
}

/*
<doc EXT copy-port
 * (copy-port in out)
 * (copy-port in out max)
 *
 * Copy the content of port |in|, which must be opened for readind, on
 * port |out|, which must be opened for writing. If |max| is nont specified,
 * All the characters from the input port are copied on ouput port. If |max|
 * is specified, it must be an integer indicatin the maximum number of characters
 * which are copied from |in| to |out|.
doc>
*/
#define COPY_PORT_SIZE 4096
DEFINE_PRIMITIVE("copy-port", copy_port, subr23, (SCM p1, SCM p2, SCM max))
{
  char buffer[COPY_PORT_SIZE];
  int n, m, sz = -1;

  if (!IPORTP(p1)) STk_error_bad_port(p1);
  if (!OPORTP(p2)) STk_error_bad_port(p2);
  if (max) {
    sz = STk_integer_value(max);
    if (sz < 0) 
      STk_error("bad size ~S", max);
  }
  
  /* Copy at most sz characters dorm p1 to p2 */
  for ( ; ; ) {
    if (sz < 0) {
      n = COPY_PORT_SIZE;
    } else if (sz > COPY_PORT_SIZE) {
      n = COPY_PORT_SIZE;
      sz -= COPY_PORT_SIZE;
    } else {
      n = sz;
      sz = 0;
    }
    
    if (n == 0) break;

    if ((n = STk_read_buffer(p1, buffer, n)) > 0) {
      m = STk_write_buffer(p2, buffer, n);
      if (n != m) goto Error;
    }
    if (n <= 0) break;
  }
  if (n != 0) goto Error;
  return STk_void;

 Error: 
  STk_error("problem while copying port ~S on port ~S", p1 , p2);
  return STk_void;
}

/*
<doc EXT flush-output-port
 * (flush-output-port)
 * (flush-output-port port)
 *
 * Flushes the buffer associated with the given output |port|. The
 * |port| argument may be omitted, in which case it defaults to the value
 * returned by |current-output-port|
doc>
 */
DEFINE_PRIMITIVE("flush-output-port", port_flush, subr01, (SCM port))
{
  port = verify_port(port, PORT_WRITE);
  if (STk_flush(port))
    general_io_error(io_write_error, "cannot flush port ~S", port);
  return STk_void;
}


/*
<doc EXT port-current-line
 * (port-current-line)
 * (port-current-line port)
 *
 * Returns the current line number associated to the given input |port| as an
 * integer. The |port| argument may be omitted, in which case it defaults to
 * the value returned by |current-input-port|. 
 * �
 * ,(bold "Note"): The |port-seek|, |read-chars| and |read-chars!| procedures
 * generally break the line-number. After using one of theses procedures, the 
 * value returned by |port-current-line| will be |-1| (except a |port-seek| 
 * at the beginning of the port reinitializes the line counter).
doc>
 */
DEFINE_PRIMITIVE("port-current-line", port_current_line, subr01, (SCM port))
{
  port = verify_port(port, PORT_READ);
  return MAKE_INT(PORT_LINE(port));
}


/*
<doc EXT port-current-position
 * (port-current-position)
 * (port-current-position port)
 *
 * Returns the position associated to the given input |port| as an
 * integer (i.e. number of characters from the beginning of the port). 
 * The |port| argument may be omitted, in which case it defaults to
 * the value returned by |current-input-port|.
doc>
 */
DEFINE_PRIMITIVE("port-current-position", port_position, subr01, (SCM port))
{
  if (!PORTP(port)) STk_error_bad_port(port);
  return MAKE_INT(STk_tell(port));
}


/*
<doc EXT seek-file-port
 * (port-seek port pos)
 * (port-seek port pos whence)
 *
 * Sets the file position for the given |port| to the position |pos|.
 * The new position, measured in bytes, is obtained by adding |pos|
 * bytes to the position specified by |whence|. If passed, |whence|
 * must be one of |:start|, |:current| or |:end|. The resulting
 * position is relative to the start of the file, the current position
 * indicator, or end-of-file, respectively. If |whence| is omitted, it
 * defaults to |:start|.
 * �
 * ,(bold "Note"): After using port-seek, the value returned by 
 * |port-current-line| may be incorrect. 
doc>
 */
DEFINE_PRIMITIVE("port-seek", port_seek, subr23, (SCM port, SCM pos, SCM w))
{
  off_t n;
  long p = STk_integer_value(pos);
  int whence = -1;

  if (!PORTP(port))  STk_error_bad_port(port);
  if (p == LONG_MIN) STk_error_bad_io_param("bad offset ~S", pos);
  if (w) {
    if (KEYWORDP(w)) {
      char *s = KEYWORD_PNAME(w);
      
      if (strcmp(s, "start") == 0) whence = SEEK_SET;
      else if (strcmp(s, "end") == 0) whence = SEEK_END;
      else if (strcmp(s, "current") == 0) whence = SEEK_CUR;
    }
  } 
  else 
    whence = SEEK_SET;
  
  if (whence < 0)
     STk_error_bad_io_param("bad keyword position ~S", w);
  
  /* ----------*/ 
  STk_flush(port);
  n = STk_seek(port, (off_t) p, whence);

  if (n < 0)
    general_io_error(io_malformed, "cannot seek position ~S", pos);
  
  return STk_long2integer((long) n);
}

/*
<doc EXT port-rewind
 * (port-rewind port)
 *
 * Sets the port position to the beginning of |port|. The value returned by 
 * |port-rewind| is ,(emph "void").
doc>
 */
DEFINE_PRIMITIVE("port-rewind", port_rewind, subr1, (SCM port))
{
  if (!PORTP(port)) STk_error_bad_port(port);
  STk_rewind(port);
  return STk_void;
}

/*===========================================================================*\
 * 
 * Initializations
 * 
\*===========================================================================*/
static void initialize_io_conditions(void)
{
  SCM module = STk_current_module;

#define DEFCOND(x, name, parent, slots)			\
  x = STk_defcond_type(name, parent, slots, module)

  DEFCOND(io_error, "&i/o-error", STk_err_mess_condition, STk_nil);

  DEFCOND(io_port_error, "&i/o-port-error", io_error, LIST1(STk_intern("port")));
  DEFCOND(io_read_error, "&i/o-read-error", io_port_error, STk_nil);
  DEFCOND(io_write_error, "&i/o-write-error", io_port_error, STk_nil);
  DEFCOND(io_closed_error, "&i/o-closed-error", io_port_error, STk_nil);

  DEFCOND(io_fn_error,"&i/o-filename-error",io_error,LIST1(STk_intern("filename")));
  DEFCOND(io_malformed, "&i/o-malformed-filename-error", io_fn_error, STk_nil);
  DEFCOND(io_prot_error, "&i/o-file-protection-error", io_fn_error, STk_nil);
  DEFCOND(io_ro_error, "&i/o-file-is-read-only-error", io_prot_error, STk_nil);
  DEFCOND(io_exists_error, "&i/o-file-already-exists-error", io_fn_error, STk_nil);
  DEFCOND(io_no_file_error, "&i/o-no-such-file-error", io_fn_error, STk_nil);
  DEFCOND(io_bad_param,"&i/o-bad-parameter",io_error,LIST1(STk_intern("parameter")));
}


static void print_port(SCM obj, SCM port, int mode)
{
  PORT_PRINT(obj)(obj, port);
}


/* The stucture which describes the port type */
static struct extended_type_descr xtype_port = {
  "port",			/* name */
  print_port			/* print function */
};



/*===========================================================================*/

int STk_init_port(void)
{
  /* Define a constant for lines terminated by CR/LF to avoid multiple 
   * allocations. Make it constant to avoid the user break it 
   */
  CrLf		       = STk_Cstring2string("\r\n");
  BOXED_INFO(CrLf)    |= STRING_CONST;

  /* Define the port file */
  DEFINE_XTYPE(port, &xtype_port);

  /* Initialize  I/O Condition (aka SRFI-36) */
  initialize_io_conditions();

  /* and its associated primitives */
  ADD_PRIMITIVE(input_portp);
  ADD_PRIMITIVE(output_portp);
  ADD_PRIMITIVE(interactive_portp);
  ADD_PRIMITIVE(current_input_port);
  ADD_PRIMITIVE(current_output_port);
  ADD_PRIMITIVE(current_error_port);
  ADD_PRIMITIVE(set_std_port);
  ADD_PRIMITIVE(scheme_read);
  ADD_PRIMITIVE(scheme_read_cst);
  ADD_PRIMITIVE(read_char);
  ADD_PRIMITIVE(read_chars);
  ADD_PRIMITIVE(d_read_chars);
  ADD_PRIMITIVE(peek_char);
  ADD_PRIMITIVE(eof_objectp);
  ADD_PRIMITIVE(eof_object);
  ADD_PRIMITIVE(char_readyp);

  ADD_PRIMITIVE(write);
  ADD_PRIMITIVE(display);
  ADD_PRIMITIVE(newline);
  ADD_PRIMITIVE(write_char);
  ADD_PRIMITIVE(write_chars);

  ADD_PRIMITIVE(write_star);
  ADD_PRIMITIVE(format);
  ADD_PRIMITIVE(scheme_error);

  ADD_PRIMITIVE(close_input_port);
  ADD_PRIMITIVE(close_output_port);
  ADD_PRIMITIVE(close_port);
  ADD_PRIMITIVE(port_closed);
  ADD_PRIMITIVE(copy_port);

  ADD_PRIMITIVE(read_line);
  ADD_PRIMITIVE(port_flush);
  ADD_PRIMITIVE(port_current_line);
  ADD_PRIMITIVE(port_position);
  ADD_PRIMITIVE(port_seek);
  ADD_PRIMITIVE(port_rewind);

  return STk_init_fport() && 
    	 STk_init_sport() &&
    	 STk_init_vport();
}