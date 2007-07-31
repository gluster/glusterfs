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

#ifndef _GFYSH_H_
#define _GFYSH_H_

#include <stdio.h>
#include <guile/gh.h>
#include <readline/readline.h>

#include "primitives.h"
#include "extensions.h"
#include "interpreter.h"

/* prompt to display */
#define GPROMPT "gf:O "

#define FALSE 0
#define TRUE  1

void gf_init (void);

#endif /* _GFYSH_H_ */
