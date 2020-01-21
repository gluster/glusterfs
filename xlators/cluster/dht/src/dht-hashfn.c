/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "dht-common.h"
#include <glusterfs/hashfn.h>

static int
dht_hash_compute_internal(int type, const char *name, const int len,
                          uint32_t *hash_p)
{
    int ret = 0;
    uint32_t hash = 0;

    switch (type) {
        case DHT_HASH_TYPE_DM:
        case DHT_HASH_TYPE_DM_USER:
            hash = gf_dm_hashfn(name, len);
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

/* The function returns:
 * 0  : in case no munge took place
 * >0 : the length (inc. terminating NULL!) of the newly modified string,
 *      if it was munged.
 */
static int
dht_munge_name(const char *original, char *modified, size_t len, regex_t *re)
{
    regmatch_t matches[2] = {
        {0},
    };
    size_t new_len = 0;
    int ret = 0;

    ret = regexec(re, original, 2, matches, 0);

    if (ret != REG_NOMATCH) {
        if (matches[1].rm_so != -1) {
            new_len = matches[1].rm_eo - matches[1].rm_so;
            /* Equal would fail due to the NUL at the end. */
            if (new_len < len) {
                memcpy(modified, original + matches[1].rm_so, new_len);
                modified[new_len] = '\0';
                return new_len + 1; /* +1 for the terminating NULL */
            }
        }
    }

    /* This is guaranteed safe because of how the dest was allocated. */
    strcpy(modified, original);
    return 0;
}

int
dht_hash_compute(xlator_t *this, int type, const char *name, uint32_t *hash_p)
{
    char *rsync_friendly_name = NULL;
    dht_conf_t *priv = NULL;
    size_t len = 0;
    int munged = 0;

    priv = this->private;

    if (name == NULL)
        return -1;

    len = strlen(name) + 1;
    rsync_friendly_name = alloca(len);

    LOCK(&priv->lock);
    {
        if (priv->extra_regex_valid) {
            munged = dht_munge_name(name, rsync_friendly_name, len,
                                    &priv->extra_regex);
        }

        if (!munged && priv->rsync_regex_valid) {
            gf_msg_trace(this->name, 0, "trying regex for %s", name);
            munged = dht_munge_name(name, rsync_friendly_name, len,
                                    &priv->rsync_regex);
        }
    }
    UNLOCK(&priv->lock);
    if (munged) {
        gf_msg_debug(this->name, 0, "munged down to %s", rsync_friendly_name);
        len = munged;
    } else {
        rsync_friendly_name = (char *)name;
    }

    return dht_hash_compute_internal(type, rsync_friendly_name, len - 1,
                                     hash_p);
}
