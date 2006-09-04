/*
  (C) 2006 Gluster core team <http://www.gluster.org/>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

/*
  (C) 2006 Gluster core team <http://www.gluster.org/>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
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
