/*
 * parameter.c	-- Parameter Objects (SRFI-39)
 * 
 * Copyright � 2003-2005 Erick Gallesio - I3S-CNRS/ESSI <eg@unice.fr>
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
 *           Author: Erick Gallesio [eg@essi.fr]
 *    Creation date:  1-Jul-2003 11:38 (eg)
 * Last file update: 26-Dec-2005 19:05 (eg)
 */


#include "stklos.h"

struct parameter_obj {
  stk_header header;
  SCM value;
  SCM converter;
  int C_type;		/* 0: parameter is expressed in Scheme
			   1: Converter is expressed in C rather than in Scheme
			   2: idem and value is aprocedure to call to get value */
};

#define PARAMETERP(o) 		(BOXED_TYPE_EQ((o), tc_parameter))
#define PARAMETER_VALUE(p)	(((struct parameter_obj *) (p))->value)
#define PARAMETER_CONV(p)	(((struct parameter_obj *) (p))->converter)
#define PARAMETER_C_TYPE(p)	(((struct parameter_obj *) (p))->C_type)

/*===========================================================================*\
 * 
 * 	Utilities
 *
\*===========================================================================*/

static void error_bad_parameter(SCM obj)
{
  STk_error("bad parameter ~S", obj);
}


SCM STk_get_parameter(SCM param)
{
  SCM val;

  if (!PARAMETERP(param)) error_bad_parameter(param);
  
  val = PARAMETER_VALUE(param);
  return (PARAMETER_C_TYPE(param) == 2) ? ((SCM (*)(void)) val)(): val;
}

SCM STk_set_parameter(SCM param, SCM value)
{
  SCM conv;

  if (!PARAMETERP(param)) error_bad_parameter(param);
  
  conv = PARAMETER_CONV(param);
  if PARAMETER_C_TYPE(param) {
    /* We have a C converter */
    PARAMETER_VALUE(param) = 
		(conv != STk_false) ? ((SCM (*) (SCM))conv)(value): value;
  } else {
    /* We have a Scheme converter */
    PARAMETER_VALUE(param) = (conv != STk_false) ? STk_C_apply(conv,1,value): value;
  }
  return STk_void;
}

SCM STk_make_C_parameter(SCM symbol, SCM value, SCM (*proc)(SCM new_value))
{
  SCM z;

  /* Define the parameter */
  NEWCELL(z, parameter);
  PARAMETER_VALUE(z)  = value;
  PARAMETER_CONV(z)   = (SCM) proc;
  PARAMETER_C_TYPE(z) = 1;

  /* Bind it to the given symbol */
  STk_define_variable(STk_intern(symbol), z, STk_current_module);

  return z;
}

SCM STk_make_C_parameter2(SCM symbol, SCM (*value)(void), SCM (*proc)(SCM new_value))
{
  SCM z = STk_make_C_parameter(symbol, (SCM) value, proc);
  
  PARAMETER_C_TYPE(z) = 2;
  return z;
}



/*===========================================================================*\
 * 
 * 	Primitives
 *
\*===========================================================================*/

/*
<doc EXT make-parameter
 * (make-parameter init)
 * (make-parameter init converter)
 * 
 * Returns a new parameter object which is bound in the global dynamic
 * environment to a cell containing the value returned by the call
 * |(converter init)|. If the conversion procedure |converter| is not
 * specified the identity function is used instead.
 * �
 * The parameter object is a procedure which accepts zero or one
 * argument. When it is called with no argument, the content of the
 * cell bound to this parameter object in the current dynamic
 * environment is returned. When it is called with one argument, the
 * content of the cell bound to this parameter object in the current
 * dynamic environment is set to the result of the call 
 * |(converter arg)|, where |arg| is the argument passed to the
 * parameter object, and
 * an unspecified value is returned.
 * 
 * @lisp
 * (define radix
 *     (make-parameter 10))
 *
 * (define write-shared
 *    (make-parameter
 *       #f
 *       (lambda (x)
 *         (if (boolean? x)
 *             x
 *             (error 'write-shared "bad boolean ~S" x)))))
 *
 *  (radix)           =>  10
 *  (radix 2)
 *  (radix)           =>  2
 *  (write-shared 0)  => error
 *
 *  (define prompt
 *    (make-parameter
 *      123
 *      (lambda (x)
 *        (if (string? x)
 *            x
 *            (with-output-to-string (lambda () (write x)))))))
 *
 *  (prompt)       =>  "123"
 *  (prompt ">")
 *  (prompt)       =>  ">"
 * @end lisp
doc>
*/
DEFINE_PRIMITIVE("make-parameter", make_parameter, subr12, (SCM value, SCM conv))
{
  SCM z, v;

  if (conv && STk_procedurep(conv) == STk_false) 
    STk_error("bad conversion function ~S", conv);
  
  /* initialize v with (conv value) */
  v = (conv) ? STk_C_apply(conv, 1, value): value;
    
  NEWCELL(z, parameter);
  PARAMETER_VALUE(z)  = v;
  PARAMETER_CONV(z)   = (conv) ? conv : STk_false;
  PARAMETER_C_TYPE(z) = 0;
  return z;
}

/*
<doc EXT parameter?
 * (parameter? obj)
 * 
 *  Returns |#t| if |obj| is a parameter object, otherwise returns |#f|.
doc> 
 */
DEFINE_PRIMITIVE("parameter?", parameterp, subr1, (SCM obj))
{
  return MAKE_BOOLEAN(PARAMETERP(obj));
}


/*===========================================================================*\
 * 
 * 	Initialization code
 *
\*===========================================================================*/

struct extended_type_descr xtype_parameter = { "parameter", NULL };

int STk_init_parameter(void)
{
  /* Define type for parameters */
  DEFINE_XTYPE(parameter, &xtype_parameter);

  /* Define primitives */
  ADD_PRIMITIVE(make_parameter);
  ADD_PRIMITIVE(parameterp);

  return TRUE;
}