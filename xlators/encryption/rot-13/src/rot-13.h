/*
   Copyright (c) 2006-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef __ROT_13_H__
#define __ROT_13_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

typedef struct {
	gf_boolean_t encrypt_write;
	gf_boolean_t decrypt_read;
} rot_13_private_t;

#endif /* __ROT_13_H__ */
