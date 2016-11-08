/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


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


static
gf_boolean_t
dht_munge_name (const char *original, char *modified,
                size_t len, regex_t *re)
{
        regmatch_t  matches[2] = {{0}, };
        size_t      new_len    = 0;
        int         ret        = 0;

        ret = regexec(re, original, 2, matches, 0);

        if (ret != REG_NOMATCH) {
                if (matches[1].rm_so != -1) {
                        new_len = matches[1].rm_eo - matches[1].rm_so;
                        /* Equal would fail due to the NUL at the end. */
                        if (new_len < len) {
                                memcpy (modified,original+matches[1].rm_so,
                                        new_len);
                                modified[new_len] = '\0';
                                return _gf_true;
                        }
                }
        }

        /* This is guaranteed safe because of how the dest was allocated. */
        strcpy(modified, original);
        return _gf_false;
}

int
dht_hash_compute (xlator_t *this, int type, const char *name, uint32_t *hash_p)
{
        char            *rsync_friendly_name    = NULL;
        dht_conf_t      *priv                   = NULL;
        size_t           len                    = 0;
        gf_boolean_t     munged                 = _gf_false;

        priv = this->private;

        LOCK (&priv->lock);
        {
                if (priv->extra_regex_valid) {
                        len = strlen(name) + 1;
                        rsync_friendly_name = alloca(len);
                        munged = dht_munge_name (name, rsync_friendly_name, len,
                                                 &priv->extra_regex);
                }

                if (!munged && priv->rsync_regex_valid) {
                        len = strlen(name) + 1;
                        rsync_friendly_name = alloca(len);
                        gf_msg_trace (this->name, 0, "trying regex for %s",
                                      name);
                        munged = dht_munge_name (name, rsync_friendly_name, len,
                                                 &priv->rsync_regex);
                        if (munged) {
                                gf_msg_debug (this->name, 0,
                                              "munged down to %s",
                                              rsync_friendly_name);
                        }
                }
        }
        UNLOCK (&priv->lock);

        if (!munged) {
                rsync_friendly_name = (char *)name;
        }

        return dht_hash_compute_internal (type, rsync_friendly_name, hash_p);
}
