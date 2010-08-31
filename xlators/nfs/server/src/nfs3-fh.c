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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "rpcsvc.h"
#include "dict.h"
#include "xlator.h"
#include "xdr-nfs3.h"
#include "msg-nfs3.h"
#include "iobuf.h"
#include "nfs3-fh.h"
#include "nfs-common.h"
#include "iatt.h"


int
nfs3_fh_validate (struct nfs3_fh *fh)
{
	if (!fh)
		return 0;

	if (fh->ident[0] != GF_NFSFH_IDENT0)
		return 0;

	if (fh->ident[1] != GF_NFSFH_IDENT1)
		return 0;

        return 1;
}


xlator_t *
nfs3_fh_to_xlator (xlator_list_t *cl, struct nfs3_fh *fh)
{
	if ((!cl) || (!fh))
		return NULL;

	return nfs_xlid_to_xlator (cl, fh->xlatorid);
}

void
nfs3_fh_init (struct nfs3_fh *fh, struct iatt *buf)
{
        if ((!fh) || (!buf))
                return;

        fh->ident[0] = GF_NFSFH_IDENT0;
        fh->ident[1] = GF_NFSFH_IDENT1;

        fh->hashcount = 0;
        fh->gen = buf->ia_gen;
        fh->ino = buf->ia_ino;

}


struct nfs3_fh
nfs3_fh_build_root_fh (xlator_list_t *cl, xlator_t *xl)
{
        struct nfs3_fh  fh = {{0}, };
        struct iatt     buf = {0, };
        if ((!cl) || (!xl))
                return fh;

        nfs3_fh_init (&fh, &buf);
        fh.xlatorid = nfs_xlator_to_xlid (cl, xl);
        fh.ino = 1;
        fh.gen = 0;
        return fh;
}


int
nfs3_fh_is_root_fh (struct nfs3_fh *fh)
{
        if (!fh)
                return 0;

        if (fh->hashcount == 0)
                return 1;

        return 0;
}


nfs3_hash_entry_t
nfs3_fh_hash_entry (ino_t ino, uint64_t gen)
{
        nfs3_hash_entry_t       hash = 0;
        int                     shiftsize = 48;
        nfs3_hash_entry_t       inomsb = 0;
        nfs3_hash_entry_t       inolsb = 0;
        nfs3_hash_entry_t       inols23b = 0;

        nfs3_hash_entry_t       genmsb = 0;
        nfs3_hash_entry_t       genlsb = 0;
        nfs3_hash_entry_t       genls23b = 0;

        hash = ino;
        while (shiftsize != 0) {
                hash ^= (ino >> shiftsize);
                shiftsize -= 16;
        }
/*
        gf_log ("FILEHANDLE", GF_LOG_TRACE, "INO %"PRIu64, ino);
        gf_log ("FILEHANDLE",GF_LOG_TRACE, "PRI HASH %d", hash);
*/
        inomsb = (ino >> 56);
//        gf_log ("FILEHANDLE", GF_LOG_TRACE, "inomsb %d", inomsb);

        inolsb = ((ino << 56) >> 56);
//        gf_log ("FILEHANDLE", GF_LOG_TRACE, "inolsb %d", inolsb);

        inolsb = (inolsb << 8);
//        gf_log ("FILEHANDLE", GF_LOG_TRACE, "inolsb to inomsb %d", inolsb);
        inols23b = ((ino << 40) >> 48);
//        gf_log ("FILEHANDLE", GF_LOG_TRACE, "inols23b %d", inols23b);

        inols23b = (inols23b << 8);
//        gf_log ("FILEHDNALE", GF_LOG_TRACE, "inols23b  %d", inols23b);

        genmsb = (gen >> 56);
//        gf_log ("FILEHANDLE", GF_LOG_TRACE, "inomsb %d", inomsb);

        genlsb = ((gen << 56) >> 56);
//        gf_log ("FILEHANDLE", GF_LOG_TRACE, "inolsb %d", inolsb);

        genlsb = (genlsb << 8);
//        gf_log ("FILEHANDLE", GF_LOG_TRACE, "inolsb to inomsb %d", inolsb);

        genls23b = ((gen << 40) >> 48);
//        gf_log ("FILEHANDLE", GF_LOG_TRACE, "inols23b %d", inols23b);

        genls23b = (genls23b << 8);
//        gf_log ("FILEHDNALE", GF_LOG_TRACE, "inols23b  %d", inols23b);

        hash ^= inolsb ^ inomsb ^ inols23b ^ genmsb ^ genlsb ^ genls23b;
        return hash;

}


void
nfs3_fh_to_str (struct nfs3_fh *fh, char *str)
{
        if ((!fh) || (!str))
                return;

        sprintf (str, "FH: hashcount %d, xlid %d, gen %"PRIu64", ino %"PRIu64,
                 fh->hashcount, fh->xlatorid, fh->gen, fh->ino);
}


void
nfs3_log_fh (struct nfs3_fh *fh)
{
//        int     x = 0;
        if (!fh)
                return;

        gf_log ("nfs3-fh", GF_LOG_TRACE, "filehandle: hashcount %d, xlid %d, "
                "gen %"PRIu64", ino %"PRIu64, fh->hashcount, fh->xlatorid,
                fh->gen, fh->ino);
/*
        for (; x < fh->hashcount; ++x)
                gf_log ("FILEHANDLE", GF_LOG_TRACE, "Hash %d: %d", x,
                        fh->entryhash[x]);
*/
}


int
nfs3_fh_build_parent_fh (struct nfs3_fh *child, struct iatt *newstat,
                         struct nfs3_fh *newfh)
{
        if ((!child) || (!newstat) || (!newfh))
                return -1;

        nfs3_fh_init (newfh, newstat);
        newfh->xlatorid = child->xlatorid;
        if ((newstat->ia_ino == 1) && (newstat->ia_gen == 0)) {
                newfh->ino = 1;
                newfh->gen = 0;
                goto done;
        }

        newfh->hashcount = child->hashcount - 1;
        memcpy (newfh->entryhash, child->entryhash,
                newfh->hashcount * GF_NFSFH_ENTRYHASH_SIZE);

done:
//        nfs3_log_fh (newfh);

        return 0;
}



int
nfs3_fh_build_child_fh (struct nfs3_fh *parent, struct iatt *newstat,
                        struct nfs3_fh *newfh)
{
        int             hashcount = 0;
        int             entry = 0;

        if ((!parent) || (!newstat) || (!newfh))
                return -1;

        nfs3_fh_init (newfh, newstat);
        newfh->xlatorid = parent->xlatorid;
        if ((newstat->ia_ino == 1) && (newstat->ia_gen == 0)) {
                newfh->ino = 1;
                newfh->gen = 0;
                goto done;
        }

        newfh->hashcount = parent->hashcount + 1;
        /* Only copy the hashes that are available in the parent file
         * handle. */
        if (parent->hashcount > GF_NFSFH_MAXHASHES)
                hashcount = GF_NFSFH_MAXHASHES;
        else
                hashcount = parent->hashcount;

        memcpy (newfh->entryhash, parent->entryhash,
                hashcount * GF_NFSFH_ENTRYHASH_SIZE);

        /* Do not insert parent dir hash if there is no space left in the hash
         * array of the child entry. */
        if (newfh->hashcount <= GF_NFSFH_MAXHASHES) {
                entry = newfh->hashcount - 1;
                newfh->entryhash[entry] = nfs3_fh_hash_entry (parent->ino,
                                                              parent->gen);
        }

done:
//        nfs3_log_fh (newfh);

        return 0;
}


uint32_t
nfs3_fh_compute_size (struct nfs3_fh *fh)
{
        uint32_t        fhlen = 0;

        if (!fh)
                return 0;

        if (fh->hashcount <= GF_NFSFH_MAXHASHES)
                fhlen = nfs3_fh_hashcounted_size (fh->hashcount);
        else
                fhlen = nfs3_fh_hashcounted_size (GF_NFSFH_MAXHASHES);

        return fhlen;
}


/* There is no point searching at a directory level which is beyond that of
 * the hashcount given in the file handle.
 */
int
nfs3_fh_hash_index_is_beyond (struct nfs3_fh *fh, int hashidx)
{
        if (!fh)
                return 1;

        if (fh->hashcount >= hashidx)
                return 0;
        else
                return 1;

        return 1;
}

