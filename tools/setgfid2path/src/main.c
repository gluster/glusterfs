/*
   Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
   */
#include <stdio.h>
#include <libgen.h>

#include "common-utils.h"
#include "syscall.h"

#define MAX_GFID2PATH_LINK_SUP 500
#define GFID_SIZE 16
#define GFID_XATTR_KEY "trusted.gfid"


int main(int argc, char **argv)
{
        int             ret                               = 0;
        struct stat     st;
        char           *dname                             = NULL;
        char           *bname                             = NULL;
        ssize_t         ret_size                          = 0;
        uuid_t          pgfid_raw                         = {0,};
        char            pgfid[36]                         = "";
        char            xxh64[GF_XXH64_DIGEST_LENGTH*2+1] = {0,};
        char            pgfid_bname[1024]                 = {0,};
        char           *key                               = NULL;
        char           *val                               = NULL;
        size_t          key_size                          = 0;
        size_t          val_size                          = 0;
        const char     *file_path                         = NULL;
        char           *file_path1                        = NULL;
        char           *file_path2                        = NULL;

        if (argc != 2) {
                fprintf (stderr, "Usage: setgfid2path <file-path>\n");
                return -1;
        }

        ret = sys_lstat (argv[1], &st);
        if (ret != 0) {
                fprintf (stderr, "Invalid File Path\n");
                return -1;
        }

        if (st.st_nlink >= MAX_GFID2PATH_LINK_SUP) {
                fprintf (stderr,
                         "Number of Hardlink support exceeded. "
                         "max=%d\n", MAX_GFID2PATH_LINK_SUP);
                return -1;
        }

        file_path = argv[1];
        file_path1 = strdup (file_path);
        file_path2 = strdup (file_path);

        dname = dirname (file_path1);
        bname = basename (file_path2);

        /* Get GFID of Parent directory */
        ret_size = sys_lgetxattr (dname, GFID_XATTR_KEY, pgfid_raw, GFID_SIZE);
        if (ret_size != GFID_SIZE) {
                fprintf (stderr,
                         "Failed to get GFID of parent directory. dir=%s\n",
                         dname);
                ret = -1;
                goto out;
        }

        /* Convert to UUID format */
        if (uuid_utoa_r (pgfid_raw, pgfid) == NULL) {
                fprintf (stderr,
                         "Failed to format GFID of parent directory. "
                         "dir=%s GFID=%s\n", dname, pgfid_raw);
                ret = -1;
                goto out;
        }

        /* Find xxhash for PGFID/BaseName */
        snprintf (pgfid_bname, sizeof (pgfid_bname), "%s/%s", pgfid, bname);
        gf_xxh64_wrapper (
                        (unsigned char *)pgfid_bname,
                        strlen (pgfid_bname),
                        GF_XXHSUM64_DEFAULT_SEED,
                        xxh64
                        );

        key_size = strlen(GFID2PATH_XATTR_KEY_PREFIX) +
                GF_XXH64_DIGEST_LENGTH*2+1;
        key = alloca (key_size);
        snprintf (key, key_size, GFID2PATH_XATTR_KEY_PREFIX"%s", xxh64);

        val_size = UUID_CANONICAL_FORM_LEN + NAME_MAX + 2;
        val = alloca (val_size);
        snprintf (val, val_size, "%s/%s", pgfid, bname);

        /* Set the Xattr, ignore if same key xattr already exists */
        ret = sys_lsetxattr (file_path, key, val, strlen(val), XATTR_CREATE);
        if (ret == -1) {
                if (errno == EEXIST) {
                        printf ("Xattr already exists, ignoring..\n");
                        ret = 0;
                        goto out;
                }

                fprintf (stderr,
                         "Failed to set gfid2path xattr. errno=%d\n error=%s",
                         errno, strerror(errno));
                ret = -1;
                goto out;
        }

        printf ("Success. file=%s key=%s value=%s\n", file_path, key, val);

out:
        if (file_path1 != NULL)
                free (file_path1);

        if (file_path2 != NULL)
                free (file_path2);

        return ret;
}
