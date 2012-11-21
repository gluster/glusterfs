/*
 * borrowed from glibc-2.12.1/string/basename.c
 * Modified to return "." for NULL or "", as required for SUSv2.
 */
#include <string.h>
#include <stdlib.h>
#ifdef THREAD_UNSAFE_BASENAME

/* Return the name-within-directory of a file name.
   Copyright (C) 1996,97,98,2002 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

char *
basename_r (filename)
     const char *filename;
{
  char *p;

  if ((filename == NULL) || (*filename == '\0'))
    return ".";

  p = strrchr (filename, '/');
  return p ? p + 1 : (char *) filename;
}
#endif /* THREAD_UNSAFE_BASENAME */
