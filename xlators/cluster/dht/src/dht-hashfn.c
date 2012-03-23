/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif


#include "glusterfs.h"
#include "xlator.h"
#include "dht-common.h"
#include "hashfn.h"


int
dht_hash_compute_internal (int type, const char *name, uint32_t *hash_p)
{
        int      ret = 0;
        uint32_t hash = 0;

        switch (type) {
        case DHT_HASH_TYPE_DM:
        case DHT_HASH_TYPE_DM_USER:
                hash = gf_dm_hashfn (name, strlen (name));
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
                rsync_frndly_name = (char *) name;                      \
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
