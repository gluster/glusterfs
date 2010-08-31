/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _NFS_FH_H_
#define _NFS_FH_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "xdr-nfs3.h"
#include "iatt.h"
#include <sys/types.h>

/* BIG FAT WARNING: The file handle code is tightly coupled to NFSv3 file
 * handles for now. This will change if and when we need v4. */
#define GF_NFSFH_IDENT0         ':'
#define GF_NFSFH_IDENT1         'O'
#define GF_NFSFH_IDENT_SIZE     (sizeof(char) * 2)
#define GF_NFSFH_STATIC_SIZE    (GF_NFSFH_IDENT_SIZE + sizeof (uint16_t) + sizeof (uint16_t) + sizeof (uint64_t) + sizeof(uint64_t))
#define GF_NFSFH_MAX_HASH_BYTES (NFS3_FHSIZE - GF_NFSFH_STATIC_SIZE)

/* Each hash element in the file handle is of 2 bytes thus giving
 * us theoretically 65536 unique entries in a directory.
 */
typedef uint16_t                nfs3_hash_entry_t;
#define GF_NFSFH_ENTRYHASH_SIZE (sizeof (nfs3_hash_entry_t))
#define GF_NFSFH_MAXHASHES      ((int)(GF_NFSFH_MAX_HASH_BYTES / GF_NFSFH_ENTRYHASH_SIZE))
#define nfs3_fh_hashcounted_size(hcount) (GF_NFSFH_STATIC_SIZE + (hcount * GF_NFSFH_ENTRYHASH_SIZE))

/* ATTENTION: Change in size of the structure below should be reflected in the
 * GF_NFSFH_STATIC_SIZE.
 */
struct nfs3_fh {

        /* Used to ensure that a bunch of bytes are actually a GlusterFS NFS
         * file handle. Should contain ":O"
         */
        char                    ident[2];

        /* Number of file/ino hash elements that follow the ino. */
        uint16_t                hashcount;

        /* Basically, the position/index of an xlator among the children of
         * the NFS xlator.
         */
        uint16_t                xlatorid;
        uint64_t                gen;
        uint64_t                ino;
        nfs3_hash_entry_t       entryhash[GF_NFSFH_MAXHASHES];
} __attribute__((__packed__));


extern uint32_t
nfs3_fh_compute_size (struct nfs3_fh *fh);

extern int
nfs3_fh_hash_index_is_beyond (struct nfs3_fh *fh, int hashidx);

extern uint16_t
nfs3_fh_hash_entry (ino_t ino, uint64_t gen);

extern int
nfs3_fh_validate (struct nfs3_fh *fh);

extern xlator_t *
nfs3_fh_to_xlator (xlator_list_t *cl, struct nfs3_fh *fh);

extern struct nfs3_fh
nfs3_fh_build_root_fh (xlator_list_t *cl, xlator_t *xl);

extern int
nfs3_fh_is_root_fh (struct nfs3_fh *fh);

extern int
nfs3_fh_build_child_fh (struct nfs3_fh *parent, struct iatt *newstat,
                        struct nfs3_fh *newfh);

extern void
nfs3_log_fh (struct nfs3_fh *fh);

extern void
nfs3_fh_to_str (struct nfs3_fh *fh, char *str);

extern int
nfs3_fh_build_parent_fh (struct nfs3_fh *child, struct iatt *newstat,
                         struct nfs3_fh *newfh);
#endif
