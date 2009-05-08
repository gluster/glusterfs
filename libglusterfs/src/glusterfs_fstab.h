/* Utilities for reading/writing fstab, mtab, etc.
   Copyright (C) 1995, 1996, 1997, 1998, 1999 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

#ifndef	GLUSTERFS_FSTAB_MNTENT_H
#define	GLUSTERFS_FSTAB_MNTENT_H	1

#include <features.h>
#include <stdio.h>
#include <paths.h>


/* General filesystem types.  */
#define GF_MNTTYPE_IGNORE	"ignore"	/* Ignore this entry.  */
#define GF_MNTTYPE_NFS	"nfs"		/* Network file system.  */
#define GF_MNTTYPE_SWAP	"swap"		/* Swap device.  */


/* Generic mount options.  */
#define GF_MNTOPT_DEFAULTS	"defaults"	/* Use all default options.  */
#define GF_MNTOPT_RO	        "ro"		/* Read only.  */
#define GF_MNTOPT_RW	        "rw"		/* Read/write.  */
#define GF_MNTOPT_SUID	        "suid"		/* Set uid allowed.  */
#define GF_MNTOPT_NOSUID	"nosuid"	/* No set uid allowed.  */
#define GF_MNTOPT_NOAUTO	"noauto"	/* Do not auto mount.  */


/* Structure describing a mount table entry.  */
struct glusterfs_mntent
  {
    char *mnt_fsname;		/* Device or server for filesystem.  */
    char *mnt_dir;		/* Directory mounted on.  */
    char *mnt_type;		/* Type of filesystem: ufs, nfs, etc.  */
    char *mnt_opts;		/* Comma-separated options for fs.  */
    int mnt_freq;		/* Dump frequency (in days).  */
    int mnt_passno;		/* Pass number for `fsck'.  */
  };

#define GF_MNTENT_BUFSIZE       1024
typedef struct glusterfs_fstab_handle {
        FILE *fp;
        char buf[GF_MNTENT_BUFSIZE];
        struct glusterfs_mntent tmpent;
}glusterfs_fstab_t;


/* Prepare to begin reading and/or writing mount table entries from the
   beginning of FILE.  MODE is as for `fopen'.  */
extern glusterfs_fstab_t *glusterfs_fstab_init (const char *file,
                                                const char *mode);

extern struct glusterfs_mntent *glusterfs_fstab_getent (glusterfs_fstab_t *h);

/* Write the mount table entry described by MNT to STREAM.
   Return zero on success, nonzero on failure.  */
extern int glusterfs_fstab_addent (glusterfs_fstab_t *h,
		                   const struct glusterfs_mntent *mnt);

/* Close a stream opened with `glusterfs_fstab_init'.  */
extern int glusterfs_fstab_close (glusterfs_fstab_t *h);

/* Search MNT->mnt_opts for an option matching OPT.
   Returns the address of the substring, or null if none found.  */
extern char *glusterfs_fstab_hasoption (const struct glusterfs_mntent *mnt,
                                        const char *opt);

#endif
