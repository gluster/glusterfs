/*
   Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "common-utils.h"
#include "xlator.h"
#include "syscall.h"
#include "logging.h"
#include "posix-messages.h"

int32_t
posix_set_gfid2path_xattr (xlator_t *this, const char *path, uuid_t pgfid,
                           const char *bname)
{
        char        xxh64[GF_XXH64_DIGEST_LENGTH*2+1] = {0,};
        char        pgfid_bname[1024]                 = {0,};
        char       *key                               = NULL;
        char       *val                               = NULL;
        size_t      key_size                          = 0;
        size_t      val_size                          = 0;
        int         ret                               = 0;

        GF_VALIDATE_OR_GOTO ("posix", this, err);

        snprintf (pgfid_bname, sizeof (pgfid_bname), "%s/%s", uuid_utoa (pgfid),
                  bname);
        gf_xxh64_wrapper ((unsigned char *) pgfid_bname,
                          strlen(pgfid_bname), GF_XXHSUM64_DEFAULT_SEED, xxh64);
        key_size = GFID2PATH_XATTR_KEY_PREFIX_LENGTH +
                   GF_XXH64_DIGEST_LENGTH*2 + 1;
        key = alloca (key_size);
        snprintf (key, key_size, GFID2PATH_XATTR_KEY_PREFIX"%s", xxh64);

        val_size = UUID_CANONICAL_FORM_LEN + NAME_MAX + 2;
        val = alloca (val_size);
        snprintf (val, val_size, "%s/%s", uuid_utoa (pgfid), bname);

        ret = sys_lsetxattr (path, key, val, strlen(val), XATTR_CREATE);
        if (ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING, errno, P_MSG_PGFID_OP,
                        "setting gfid2path xattr failed on %s: key = %s ",
                        path, key);
                goto err;
        }
        return 0;
 err:
        return -1;
}

int32_t
posix_remove_gfid2path_xattr (xlator_t *this, const char *path,
                              uuid_t pgfid, const char *bname)
{
        char        xxh64[GF_XXH64_DIGEST_LENGTH*2+1] = {0,};
        char        pgfid_bname[1024]                 = {0,};
        int         ret                               = 0;
        char       *key                               = NULL;
        size_t      key_size                          = 0;

        GF_VALIDATE_OR_GOTO ("posix", this, err);

        snprintf (pgfid_bname, sizeof (pgfid_bname), "%s/%s", uuid_utoa (pgfid),
                  bname);
        gf_xxh64_wrapper ((unsigned char *) pgfid_bname,
                          strlen(pgfid_bname), GF_XXHSUM64_DEFAULT_SEED, xxh64);
        key_size = GFID2PATH_XATTR_KEY_PREFIX_LENGTH +
                   GF_XXH64_DIGEST_LENGTH*2 + 1;
        key = alloca (key_size);
        snprintf (key, key_size, GFID2PATH_XATTR_KEY_PREFIX"%s", xxh64);

        ret = sys_lremovexattr (path, key);
        if (ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING, errno, P_MSG_PGFID_OP,
                        "removing gfid2path xattr failed on %s: key = %s",
                        path, key);
                goto err;
        }
        return 0;
 err:
        return -1;
}

gf_boolean_t
posix_is_gfid2path_xattr (const char *name)
{
        if (name && strncmp (GFID2PATH_XATTR_KEY_PREFIX, name,
                             GFID2PATH_XATTR_KEY_PREFIX_LENGTH) == 0) {
                return _gf_true;
        } else {
                return _gf_false;
        }
}
