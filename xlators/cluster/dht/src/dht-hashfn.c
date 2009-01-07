/*
   Copyright (c) 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif


#include "glusterfs.h"
#include "xlator.h"
#include "dht-common.h"


uint32_t dht_hashfn_tea (const char *name, int len);


typedef enum {
	DHT_HASH_TYPE_TEA,
} dht_hashfn_type_t;


int
dht_hash_compute_internal (int type, const char *name, uint32_t *hash_p)
{
	int      ret = 0;
	uint32_t hash = 0;

	switch (type) {
	case DHT_HASH_TYPE_TEA:
		hash = dht_hashfn_tea (name, strlen (name));
		break;
	default:
		ret = -1;
		break;
	}

	if (ret == 0) {
		*hash_p = hash;
	}

	return ret;
}


#define MAKE_RSYNC_FRIENDLY_NAME(rsync_frndly_name, name) do {          \
                rsync_frndly_name = (char *) name;			\
                if (name[0] == '.') {                                   \
                        char *dot   = 0;                                \
                        int namelen = 0;                                \
                                                                        \
                        dot = strrchr (name, '.');                      \
                        if (dot && dot > (name + 1) && *(dot + 1)) {    \
                                namelen = (dot - name);                 \
                                rsync_frndly_name = alloca (namelen);   \
                                strncpy (rsync_frndly_name, name + 1,   \
                                         namelen);                      \
                                rsync_frndly_name[namelen - 1] = 0;     \
                        }                                               \
                }                                                       \
        } while (0);


int
dht_hash_compute (int type, const char *name, uint32_t *hash_p)
{
	char     *rsync_friendly_name = NULL;

	MAKE_RSYNC_FRIENDLY_NAME (rsync_friendly_name, name);

	return dht_hash_compute_internal (type, rsync_friendly_name, hash_p);
}
