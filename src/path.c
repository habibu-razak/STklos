/*
 * p a t h . c			-- Path names management
 * 
 * Copyright � 2000-2005 Erick Gallesio - I3S-CNRS/ESSI <eg@unice.fr>
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
 *    Creation date:  9-Jan-2000 14:25 (eg)
 * Last file update:  8-Nov-2005 20:20 (eg)
 */

#include "stklos.h"
#include "path.h"
#ifdef HAVE_GLOB
#  include <glob.h>
#else
#  include "gnu-glob.h"
#endif


/*===========================================================================*\
 * 
 * tilde-expand		-- expand '~' and '~user' string prefix
 * 
\*===========================================================================*/

static char *tilde_expand(char *name, char *result)
{
  char *p;
  struct passwd *pwPtr;
  register int len;
  
  if (name[0] != '~') {
    strcpy(result, name);
    return name;
  }
  
  if (ISDIRSEP(name[1]) || name[1] == '\0') {
    char *dir;

    pwPtr = getpwuid(getuid());
    if (pwPtr==NULL) {
      char *h = getenv("HOME");
      dir = (h == NULL)? "": h;
    } else 
      dir =  pwPtr->pw_dir;
    
    sprintf(result, "%s%s", dir, name+1);
  } 
  else {
    for (p=&name[1]; (*p != 0) && (*p != '/'); p++) {
      /* Null body;  just find end of name. */
    }
    len = p-(name+1);
    strncpy(result, name+1, (size_t) len);
    result[len] = '\0';

    pwPtr = getpwnam(result);
    if (pwPtr == NULL) {
      STk_error("user %S does not exist", result);
    }
    sprintf(result, "%s%s", pwPtr->pw_dir, p);
  }
  return result;
}


/*===========================================================================*\
 * 
 * absolute	-- Given a file name, return its (mostly clean) 
 *		   absolute path name
 *
\*===========================================================================*/

static void absolute(char *s, char *pathname)	
{
  char *p = pathname;
  char *t;

  if (!ISABSOLUTE(s)) {
    getcwd(pathname, MAX_PATH_LENGTH);
    p = &pathname[strlen(pathname)];     /* place p at end of pathname */ 
  }

  *p = DIRSEP;
  
  for ( ; *s; s++) {
    t = s;
    switch (*s) {
      case '.' : if (*(s+1)) {
		   switch (*++s) {
		     case '.' : if (ISDIRSEP(*p) && (*(s+1)=='\0' || 
						     ISDIRSEP(*(s+1)))) {
		                  /* We must go back to the parent */
		                  if (ISDIRSEP(*p) && p > pathname)    p --;
				  while (p > pathname && !ISDIRSEP(*p)) p--;
		                }
		                else {
				  /* There are several dots. Copy them */
				  for (s = t; *s == '.'; s++) *++p = '.';
				  s -= 1;
				}
		                break;
		     case '/' : if (!ISDIRSEP(*p)) {
		                   *++p = '.';
		       		   *++p = DIRSEP;
		     	        }
		                break;
		     default  : *++p = '.'; *++p = *s; break;
		   }
                 }
                 else { /* We have a final (single) dot */
		   if (!ISDIRSEP(*p)) *++p = '.';
		 }
                 break;
      case '/' : if (!ISDIRSEP(*p)) *++p = DIRSEP; break;
      default  : *++p = *s;
    }
  }
  
  /* Place a \0 at end. If path ends with a "/", delete it */
  if (p == pathname || !ISDIRSEP(*p)) p++;
  *p = '\0';
}

/*===========================================================================*\
 * 
 * resolve_link	-- Given a file name, return its (mostly clean) 
 *		   absolute path name taking into account symbolic links
 *
\*===========================================================================*/
#define MAXLINK 50	/* Number max of link before declaring we have a loop */

SCM STk_resolve_link(char *path, int count)
{
#ifdef WIN32
  return STk_Cstring2string(STk_expand_file_name(path));
#else
  char link[MAX_PATH_LENGTH], dst[MAX_PATH_LENGTH], *s, *d=dst;
  int n;
  
  s = STk_expand_file_name(path);
  
  for (s++, *d++='/' ;       ; s++, d++) {
    switch (*s) {
      case '\0':
      case '/' : *d = '\0';
	if ((n=readlink(dst, link, MAX_PATH_LENGTH-1)) > 0) {
	  link[n] = '\0';
	  if (link[0] == '/') 
	    /* link is absolute */
	    d = dst;
	  else {
	    /* relative link. Delete last item */
	    while (*--d != '/') {
	    }
	    d += 1;
	  }
		   
	  /* d points the place where the link must be placed */
	  if (d - dst + strlen(link) + strlen(s) < MAX_PATH_LENGTH - 1) {
	    /* we have enough room */
	    sprintf(d, "%s%s", link, s); 
	    /* Recurse. Be careful for loops (a->b and b->a) */
	    if (count < MAXLINK) 
	      return STk_resolve_link(dst, count+1);
	  }
	  return STk_false;
	}
	else {
	  if (errno != EINVAL) 
	    /* EINVAL = file is not a symlink (i.e. it's a true error) */
	    return STk_false;
	  else
	    if (*s) *d = '/'; 
	    else return STk_Cstring2string(dst);		       
	}
      default:   *d = *s;
    }
  }
#endif
}

/*===========================================================================*\
 * 
 * STk_expand_file_name  
 *
\*===========================================================================*/
char *STk_expand_file_name(char *s)
{
  char expanded[2 * MAX_PATH_LENGTH], abs[2 * MAX_PATH_LENGTH];  
  /* Warning: absolute makes no control about path overflow. Hence the "2 *" */

#ifdef WIN32
  cygwin_conv_to_full_posix_path(s, abs);
  tilde_expand(abs, expanded);
#else
  tilde_expand(s, expanded);
#endif
  absolute(expanded, abs);
  return STk_strdup(abs);
}


/*===========================================================================*\
 * 
 * STk_do_glob
 *
\*===========================================================================*/
SCM STk_do_glob(int argc, SCM *argv)
{
  int i, n, flags;
  glob_t buff;
  char expanded[2 * MAX_PATH_LENGTH];
  SCM res;


  flags = GLOB_NOSORT;
#ifdef GLOB_BRACE
  flags |= GLOB_BRACE;
#endif

  for (i = 0; i < argc; i++, argv--) {
    if (!STRINGP(*argv)) STk_error("~S is a bad pathname", *argv);
    if (i == 1) flags |= GLOB_APPEND; /* append after the 1st argument done */

    tilde_expand(STRING_CHARS(*argv), expanded);
    glob(expanded, flags, NULL, &buff);
    /* ignore the return value of glob */
  }
  
  /* Transform the argv array of C strings in a list of Scheme strings */
  n   = buff.gl_pathc; 
  res = STk_nil;
  while (n--)
    res = STk_cons(STk_Cstring2string(buff.gl_pathv[n]), res);

  globfree(&buff);
  return res;
}
