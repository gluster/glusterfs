/*
   Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _POSIX_METADATA_DISK_H
#define _POSIX_METADATA_DISK_H

typedef struct gf_timespec_disk {
        uint64_t tv_sec;
        uint64_t tv_nsec;
} gf_timespec_disk_t;

/* posix_mdata_t on disk structure */

typedef struct __attribute__ ((__packed__)) posix_mdata_disk {
        /* version of structure, bumped up if any new member is added */
        uint8_t version;
        /* flags indicates valid fields in the structure */
        uint64_t flags;
        gf_timespec_disk_t ctime;
        gf_timespec_disk_t mtime;
        gf_timespec_disk_t atime;
} posix_mdata_disk_t;

#endif /* _POSIX_METADATA_DISK_H */
