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
  License aint64_t with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

#ifndef _EXTENSIONS_H_
#define _EXTENSIONS_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "shell.h"
#include "fops.h"
extern SCM ex_gf_hi;
extern SCM ex_gf_command_hook;

void gf_load (const char *file);
void register_hooks (void);
void register_primitives (void);
#endif /* _EXTENSIONS_H_ */
