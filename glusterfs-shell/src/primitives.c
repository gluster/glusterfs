/*
   Copyright (c) 2006 Z RESEARCH, Inc. <http://www.zresearch.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/

#include "primitives.h"
#include "extensions.h"

SCM
gf_hello (SCM scm_string)
{
  char *string = SCM_STRING_CHARS (scm_string);
  printf ("you %s", string);
  return SCM_UNSPECIFIED;
}


SCM
gf_demo (SCM scm_string)
{
  char *string = SCM_STRING_CHARS (scm_string);
  char *pstring = NULL;
  char *append = "dingdong";

  pstring = calloc (strlen (string) + strlen (append) + 1, 1);
  sprintf (pstring, "%s----%s", string, append);

  scm_run_hook (ex_gf_hi, gh_list (gh_str02scm (pstring),
				      SCM_UNDEFINED));
  free (pstring);

  return SCM_UNDEFINED;
}

SCM
gf_listlocks (SCM scm_volume)
{
  struct xlator *volume = (void *) SCM_INUM (scm_volume);

  printf ("gf_listlocks called\n");
  
  int32_t ret = volume->mops->listlocks (volume);
  
  printf ("listlocks returned %d\n", ret);

  return SCM_UNDEFINED;
}
