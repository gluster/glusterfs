/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _NFS_FH_H_
#define _NFS_FH_H_

#include "xlator.h"
#include "xdr-nfs3.h"
#include "iatt.h"
#include <sys/types.h>
#include "compat-uuid.h"

/* BIG FAT WARNING: The file handle code is tightly coupled to NFSv3 file
 * handles for now. This will change if and when we need v4. */
#define GF_NFSFH_IDENT0         ':'
#define GF_NFSFH_IDENT1         'O'
#define GF_NFSFH_IDENT2         'G'
#define GF_NFSFH_IDENT3         'L'
#define GF_NFSFH_IDENT_SIZE     (sizeof(char) * 4)
#define GF_NFSFH_STATIC_SIZE    (GF_NFSFH_IDENT_SIZE + (3*sizeof (uuid_t)))

#define nfs3_fh_exportid_to_index(exprtid)      ((uint16_t)exprtid[15])
/* ATTENTION: Change in size of the structure below should be reflected in the
 * GF_NFSFH_STATIC_SIZE.
 */
struct nfs3_fh {

        /* Used to ensure that a bunch of bytes are actually a GlusterFS NFS
         * file handle. Should contain ":OGL"
         */
        char                    ident[4];

        /* UUID that identifies an export. The value stored in exportid
         * depends on the usage of gluster nfs. If the DVM is enabled using
         * the nfs.dynamic-volumes option then exportid will contain the UUID
         * of the volume so that gnfs is able to identify volumes uniquely
         * through volume additions,deletions,migrations, etc.
         *
         * When not using dvm, exportid contains the index of the volume
         * based on the position of the volume in the list of subvolumes
         * for gnfs.
         */
        uuid_t              exportid;

        /* File/dir gfid. */
        uuid_t                  gfid;
        uuid_t                  mountid;
        /* This structure must be exactly NFS3_FHSIZE (64) bytes long.
           Having the structure shorter results in buffer overflows
           during XDR decoding.
        */
        unsigned char padding[NFS3_FHSIZE - GF_NFSFH_STATIC_SIZE];
} __attribute__((__packed__));

#define GF_NFS3FH_STATIC_INITIALIZER    {{0},}

extern uint32_t
nfs3_fh_compute_size ();

extern uint16_t
nfs3_fh_hash_entry (uuid_t gfid);

extern int
nfs3_fh_validate (struct nfs3_fh *fh);

extern struct nfs3_fh
nfs3_fh_build_indexed_root_fh (xlator_list_t *cl, xlator_t *xl);

extern int
nfs3_fh_is_root_fh (struct nfs3_fh *fh);

extern int
nfs3_fh_build_child_fh (struct nfs3_fh *parent, struct iatt *newstat,
                        struct nfs3_fh *newfh);

extern void
nfs3_log_fh (struct nfs3_fh *fh);

extern void
nfs3_fh_to_str (struct nfs3_fh *fh, char *str, size_t len);

extern int
nfs3_fh_build_parent_fh (struct nfs3_fh *child, struct iatt *newstat,
                         struct nfs3_fh *newfh);

extern struct nfs3_fh
nfs3_fh_build_uuid_root_fh (uuid_t volumeid, uuid_t mountid);

extern int
nfs3_build_fh (inode_t *inode, uuid_t exportid,
               struct nfs3_fh *newfh);

#endif
