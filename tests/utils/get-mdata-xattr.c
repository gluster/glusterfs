/*
   Copyright (c) 2019 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <stdlib.h>
#include <endian.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <errno.h>

typedef struct gf_timespec_disk {
    uint64_t tv_sec;
    uint64_t tv_nsec;
} gf_timespec_disk_t;

/* posix_mdata_t on disk structure */
typedef struct __attribute__((__packed__)) posix_mdata_disk {
    /* version of structure, bumped up if any new member is added */
    uint8_t version;
    /* flags indicates valid fields in the structure */
    uint64_t flags;
    gf_timespec_disk_t ctime;
    gf_timespec_disk_t mtime;
    gf_timespec_disk_t atime;
} posix_mdata_disk_t;

/* In memory representation posix metadata xattr */
typedef struct {
    /* version of structure, bumped up if any new member is added */
    uint8_t version;
    /* flags indicates valid fields in the structure */
    uint64_t flags;
    struct timespec ctime;
    struct timespec mtime;
    struct timespec atime;
} posix_mdata_t;

#define GF_XATTR_MDATA_KEY "trusted.glusterfs.mdata"

/* posix_mdata_from_disk converts posix_mdata_disk_t into host byte order
 */
static inline void
posix_mdata_from_disk(posix_mdata_t *out, posix_mdata_disk_t *in)
{
    out->version = in->version;
    out->flags = be64toh(in->flags);

    out->ctime.tv_sec = be64toh(in->ctime.tv_sec);
    out->ctime.tv_nsec = be64toh(in->ctime.tv_nsec);

    out->mtime.tv_sec = be64toh(in->mtime.tv_sec);
    out->mtime.tv_nsec = be64toh(in->mtime.tv_nsec);

    out->atime.tv_sec = be64toh(in->atime.tv_sec);
    out->atime.tv_nsec = be64toh(in->atime.tv_nsec);
}

/* posix_fetch_mdata_xattr fetches the posix_mdata_t from disk */
static int
posix_fetch_mdata_xattr(const char *real_path, posix_mdata_t *metadata)
{
    size_t size = -1;
    char *value = NULL;
    char gfid_str[64] = {0};

    char *key = GF_XATTR_MDATA_KEY;

    if (!metadata || !real_path) {
        goto err;
    }

    /* Get size */
    size = lgetxattr(real_path, key, NULL, 0);
    if (size == -1) {
        goto err;
    }

    value = calloc(size + 1, sizeof(char));
    if (!value) {
        goto err;
    }

    /* Get xattr value */
    size = lgetxattr(real_path, key, value, size);
    if (size == -1) {
        goto err;
    }
    posix_mdata_from_disk(metadata, (posix_mdata_disk_t *)value);

out:
    if (value)
        free(value);
    return 0;
err:
    if (value)
        free(value);
    return -1;
}

int
main(int argc, char *argv[])
{
    posix_mdata_t metadata;
    uint64_t result;

    if (argc != 3) {
        /*
        Usage: get_mdata_xattr -c|-m|-a <file-name>
                       where -c --> ctime
                             -m --> mtime
                             -a --> atime
        */
        printf("-1");
        goto err;
    }

    if (posix_fetch_mdata_xattr(argv[2], &metadata)) {
        printf("-1");
        goto err;
    }

    switch (argv[1][1]) {
        case 'c':
            result = metadata.ctime.tv_sec;
            break;
        case 'm':
            result = metadata.mtime.tv_sec;
            break;
        case 'a':
            result = metadata.atime.tv_sec;
            break;
        default:
            printf("-1");
            goto err;
    }
    printf("%" PRIu64, result);
    fflush(stdout);
    return 0;
err:
    fflush(stdout);
    return -1;
}
