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

#include "interpreter.h"

int32_t 
interpreter (char *line)
{
  char *cmd, *args;
 
  cmd = strtok (line, " ");
  if (!cmd)
    return 1;
  args = strtok (NULL, "\0");
  
  scm_run_hook (ex_gf_command_hook, gh_list (gh_str02scm (cmd),
					     gh_str02scm (args),
					     SCM_UNDEFINED));
  
  return 1;  
}
