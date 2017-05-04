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

#include "shell.h"
#include "extensions.h"

void
extensions_init (void)
{
  register_hooks ();
  register_primitives ();
}

void
gf_init (void)
{
  /* Display welcome message */
  gh_eval_str ("(display \"Glusterfs Shell!!!\")");
  gh_eval_str ("(newline)");

  
  /* initiate the extension channels */
  extensions_init ();

  /* load the scm file */
  gf_load ("../extensions/hello.scm");
  gf_load ("../extensions/commands.scm");
  
}
