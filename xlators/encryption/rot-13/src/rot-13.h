/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __ROT_13_H__
#define __ROT_13_H__

typedef struct {
	gf_boolean_t encrypt_write;
	gf_boolean_t decrypt_read;
} rot_13_private_t;

#endif /* __ROT_13_H__ */
