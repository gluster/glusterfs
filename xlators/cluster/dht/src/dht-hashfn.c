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
#include "dht.h"


#define DELTA 0x9E3779B9
#define FULLROUNDS 10		/* 32 is overkill, 16 is strong crypto */
#define PARTROUNDS 6		/* 6 gets complete mixing */

/* a, b, c, d - data; h0, h1 - accumulated hash */
#define TEACORE(rounds)							\
	do {								\
		u32 sum = 0;						\
		int n = rounds;						\
		u32 b0, b1;						\
									\
		b0 = h0;						\
		b1 = h1;						\
									\
		do							\
		{							\
			sum += DELTA;					\
			b0 += ((b1 << 4)+a) ^ (b1+sum) ^ ((b1 >> 5)+b);	\
			b1 += ((b0 << 4)+c) ^ (b0+sum) ^ ((b0 >> 5)+d);	\
		} while(--n);						\
									\
		h0 += b0;						\
		h1 += b1;						\
	} while(0)

typedef unsigned int __u32;
typedef unsigned int u32;


u32 reiserfs_tea(const char *msg, int len)
{
	u32 k[] = { 0x9464a485, 0x542e1a94, 0x3e846bff, 0xb75bcfc3 };

	u32 h0 = k[0], h1 = k[1];
	u32 a, b, c, d;
	u32 pad;
	int i;

	//      assert(len >= 0 && len < 256);

	pad = (u32) len | ((u32) len << 8);
	pad |= pad << 16;

	while (len >= 16) {
		a = (u32) msg[0] |
		    (u32) msg[1] << 8 | (u32) msg[2] << 16 | (u32) msg[3] << 24;
		b = (u32) msg[4] |
		    (u32) msg[5] << 8 | (u32) msg[6] << 16 | (u32) msg[7] << 24;
		c = (u32) msg[8] |
		    (u32) msg[9] << 8 |
		    (u32) msg[10] << 16 | (u32) msg[11] << 24;
		d = (u32) msg[12] |
		    (u32) msg[13] << 8 |
		    (u32) msg[14] << 16 | (u32) msg[15] << 24;

		TEACORE(PARTROUNDS);

		len -= 16;
		msg += 16;
	}

	if (len >= 12) {
		a = (u32) msg[0] |
		    (u32) msg[1] << 8 | (u32) msg[2] << 16 | (u32) msg[3] << 24;
		b = (u32) msg[4] |
		    (u32) msg[5] << 8 | (u32) msg[6] << 16 | (u32) msg[7] << 24;
		c = (u32) msg[8] |
		    (u32) msg[9] << 8 |
		    (u32) msg[10] << 16 | (u32) msg[11] << 24;

		d = pad;
		for (i = 12; i < len; i++) {
			d <<= 8;
			d |= msg[i];
		}
	} else if (len >= 8) {
		a = (u32) msg[0] |
		    (u32) msg[1] << 8 | (u32) msg[2] << 16 | (u32) msg[3] << 24;
		b = (u32) msg[4] |
		    (u32) msg[5] << 8 | (u32) msg[6] << 16 | (u32) msg[7] << 24;

		c = d = pad;
		for (i = 8; i < len; i++) {
			c <<= 8;
			c |= msg[i];
		}
	} else if (len >= 4) {
		a = (u32) msg[0] |
		    (u32) msg[1] << 8 | (u32) msg[2] << 16 | (u32) msg[3] << 24;

		b = c = d = pad;
		for (i = 4; i < len; i++) {
			b <<= 8;
			b |= msg[i];
		}
	} else {
		a = b = c = d = pad;
		for (i = 0; i < len; i++) {
			a <<= 8;
			a |= msg[i];
		}
	}

	TEACORE(FULLROUNDS);

/*	return 0;*/
	return h0 ^ h1;
}


typedef enum {
	DHT_HASH_TYPE_REISERFS_TEA,
} dht_hashfn_type_t;


int
dht_hash_compute_internal (int type, const char *name, uint32_t *hash_p)
{
	int      ret = 0;
	uint32_t hash = 0;

	switch (type) {
	case DHT_HASH_TYPE_REISERFS_TEA:
		hash = reiserfs_tea (name, strlen (name));
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
