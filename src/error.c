/*
 *
 * e r r o r . c 			-- The error procedure
 *
 * Copyright � 1993-2000 ESSIrick Gallesio - I3S-CNRS/ESSI <eg@unice.fr>
 * 
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
 *    Creation date: 14-Nov-1993 14:58
 * Last file update: 25-Apr-2005 14:12 (eg)
 */

#include "stklos.h"


/*===========================================================================*\
 * 
 * A simplified (and very ad hoc) version of printf for error messages.
 * Recognized formats:
 *       %c for character
 *       %s for string
 *       %S for string (surrounded by a pair of quotes)
 *       %d for decimal print 
 *       %x for hexadecimal print
 *       %% for printing a '%'
 *       ~A for printing a Scheme object in display mode
 *       ~S for printing a Scheme object in write mode
 *	 ~W for printing a Scheme object in write mode (circular)
 *	 ~% for printing a newline 
 *       ~~ for printing a tilde character
 * 
\*===========================================================================*/


static void print_int(SCM port, unsigned int x, int base)
{
  if (x >= base) print_int(port, x / base, base);
  x %= base;
  STk_putc(x + (x >= 10 ? 'a'-10: '0'), port);
}

static void print_format(SCM port,char *format, va_list ap)
{
  register char *s;
  char *str;

  /* Parse given format */
  for (s = format; *s ; s++) {
    if (*s == '%') {
      /* % format (C-like) */
      switch (*++s) {
	case '%': STk_putc('%', port); break;
	  
	case 'S': STk_putc('`', port); /* No break */
	case 's': for (str = va_arg(ap, char *); *str; str++) 
		    STk_putc(*str, port);
		  if (*s == 'S') STk_putc('\'', port);
		  break;
	case 'c': STk_putc(va_arg(ap, int), port); break;
	case 'x': print_int(port, va_arg(ap, unsigned int), 16); break;
	case 'd': {
	  	    int val =  va_arg(ap, unsigned int);

		    if (val < 0) {
		      STk_putc('-', port);
		      print_int(port, -val, 10);
		    } 
		    else  
		      print_int(port, val, 10);
		    break;
		  }
	default:  STk_putc('%', port); 
		  if (*s) STk_putc(*s, port);
		  break;
      }
    } else if (*s == '~') {
      /* ~ format (CL like) */
      switch (*++s) {
	case 'A': STk_putc('`', port); /* No break */
	case 'a': STk_print(va_arg(ap, SCM), port, DSP_MODE); 
		  if (*s == 'A') STk_putc('\'', port);
		  break;
	case 'W': STk_putc('`', port);	/* No break */
	case 'w': STk_print_star(va_arg(ap, SCM), port);
	  	  if (*s == 'W') STk_putc('\'', port);
		  break;	
	case 'S': STk_putc('`', port);	/* No break */
	case 's': STk_print(va_arg(ap, SCM), port, WRT_MODE); 
		  if (*s == 'S') STk_putc('\'', port);
		  break;
	case '~': STk_putc('~', port);  break;
	case '%': STk_putc('\n', port); break;
	default:  STk_putc('~', port); 
	  	  if (*s) STk_putc(*s, port);
		  break;
      }
    } else {
      /* Normal character */
      STk_putc(*s, port);
    }
  }
}

void STk_signal_error(SCM where, SCM str)
{
  SCM bt = STk_vm_bt();
  
  STk_raise_exception(STk_make_C_cond(STk_err_mess_condition, 3, where, bt, str));
}

SCM STk_format_error(char *format, ...)
{
  va_list ap;
  SCM out;

  /* Open a string port */
  out = STk_open_output_string();

  /* Build the message string in the string port */
  va_start(ap, format);
  print_format(out, format, ap);
  va_end(ap);

  /* Return errror message as a Scheme string */
  return STk_get_output_string(out);
}


void STk_error(char *format, ...)
{
  va_list ap;
  SCM out, bt;

  /* Grab a baktrace */
  bt = STk_vm_bt();

  /* Open a string port */
  out = STk_open_output_string();

  /* Build the message string in the string port */
  va_start(ap, format);
  print_format(out, format, ap);
  va_end(ap);

  /* and signal error */
  STk_raise_exception(STk_make_C_cond(STk_err_mess_condition,
				      3,
				      STk_false, /* no location */
				      bt,
				      STk_get_output_string(out)));

}



void STk_warning(char *format, ...)
{
  va_list ap;

  /* Print the prologue */
  STk_fprintf(STk_curr_eport, "\n**** Warning:\n");

  /* Print the message */
  va_start(ap, format);
  print_format(STk_curr_eport, format, ap);
  va_end(ap);

  STk_putc('\n', STk_curr_eport);
  STk_flush(STk_curr_eport);
}


void STk_panic(char *format, ...)
{
  va_list ap;

  /* Print the prologue */
  STk_fprintf(STk_curr_eport, "\n**** PANIC:\n");

  /* Print the message */
  va_start(ap, format);
  print_format(STk_curr_eport, format, ap);
  va_end(ap);

  STk_putc('\n', STk_curr_eport);
  STk_flush(STk_curr_eport);

  /*  And terminate execution  */
  STk_puts("ABORT.\n", STk_curr_eport);
  _exit(1);
}


void STk_signal(char *str)
{
  STk_raise_exception(STk_make_C_cond(STk_message_condition, 
				      1,
				      STk_Cstring2string(str)));
}



#ifdef STK_DEBUG
void STk_debug(char *format, ...)
{
  va_list ap;

  /* Print the prologue */
  STk_fprintf(STk_curr_eport, "**** Debug: ");
  
  va_start(ap, format);
  print_format(STk_curr_eport, format, ap);
  va_end(ap);
  
  STk_putc('\n', STk_curr_eport);
  STk_flush(STk_curr_eport);
}

void STk_gdb(SCM obj)		/* associated to the  gdb write function */
{
  STk_debug("Object 0x%lx value = ~s", (unsigned long) obj, obj);
}
#endif