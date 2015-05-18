/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "rpcsvc.h"
#include "dict.h"
#include "xlator.h"
#include "xdr-nfs3.h"
#include "msg-nfs3.h"
#include "iobuf.h"
#include "nfs3-fh.h"
#include "nfs-common.h"
#include "iatt.h"
#include "common-utils.h"
#include "nfs-messages.h"


int
nfs3_fh_validate (struct nfs3_fh *fh)
{
	if (!fh)
		return 0;

	if (fh->ident[0] != GF_NFSFH_IDENT0)
		return 0;

	if (fh->ident[1] != GF_NFSFH_IDENT1)
		return 0;

       if (fh->ident[2] != GF_NFSFH_IDENT2)
               return 0;

       if (fh->ident[3] != GF_NFSFH_IDENT3)
               return 0;

        return 1;
}


void
nfs3_fh_init (struct nfs3_fh *fh, struct iatt *buf)
{
        if ((!fh) || (!buf))
                return;

        fh->ident[0] = GF_NFSFH_IDENT0;
        fh->ident[1] = GF_NFSFH_IDENT1;
        fh->ident[2] = GF_NFSFH_IDENT2;
        fh->ident[3] = GF_NFSFH_IDENT3;

        gf_uuid_copy (fh->gfid, buf->ia_gfid);
}


struct nfs3_fh
nfs3_fh_build_indexed_root_fh (xlator_list_t *cl, xlator_t *xl)
{
        struct nfs3_fh  fh = {{0}, };
        struct iatt     buf = {0, };
        uuid_t          root = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};

        if ((!cl) || (!xl))
                return fh;

        gf_uuid_copy (buf.ia_gfid, root);
        nfs3_fh_init (&fh, &buf);
        fh.exportid [15] = nfs_xlator_to_xlid (cl, xl);

        return fh;
}


struct nfs3_fh
nfs3_fh_build_uuid_root_fh (uuid_t volumeid, uuid_t mountid)
{
        struct nfs3_fh  fh = {{0}, };
        struct iatt     buf = {0, };
        uuid_t          root = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};

        gf_uuid_copy (buf.ia_gfid, root);
        nfs3_fh_init (&fh, &buf);
        gf_uuid_copy (fh.exportid, volumeid);
        gf_uuid_copy (fh.mountid, mountid);

        return fh;
}


int
nfs3_fh_is_root_fh (struct nfs3_fh *fh)
{
        uuid_t  rootgfid = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};

        if (!fh)
                return 0;

        if (gf_uuid_compare (fh->gfid, rootgfid) == 0)
                return 1;

        return 0;
}


void
nfs3_fh_to_str (struct nfs3_fh *fh, char *str, size_t len)
{
        char            gfid[GF_UUID_BUF_SIZE];
        char            exportid[GF_UUID_BUF_SIZE];
        char            mountid[GF_UUID_BUF_SIZE];

        if ((!fh) || (!str))
                return;

        snprintf (str, len, "FH: exportid %s, gfid %s, mountid %s",
                  uuid_utoa_r (fh->exportid, exportid),
                  uuid_utoa_r (fh->gfid, gfid),
                  uuid_utoa_r (fh->mountid, mountid));
}

void
nfs3_log_fh (struct nfs3_fh *fh)
{
        char    gfidstr[512];
        char    exportidstr[512];

        if (!fh)
                return;

        gf_msg_trace ("nfs3-fh", 0, "filehandle: exportid 0x%s, gfid 0x%s",
                      uuid_utoa_r (fh->exportid, exportidstr),
                      uuid_utoa_r (fh->gfid, gfidstr));
}

int
nfs3_fh_build_parent_fh (struct nfs3_fh *child, struct iatt *newstat,
                         struct nfs3_fh *newfh)
{
        if ((!child) || (!newstat) || (!newfh))
                return -1;

        nfs3_fh_init (newfh, newstat);
        gf_uuid_copy (newfh->exportid, child->exportid);
        return 0;
}

int
nfs3_build_fh (inode_t *inode, uuid_t exportid, struct nfs3_fh *newfh)
{
        if (!newfh || !inode)
                return -1;

        newfh->ident[0] = GF_NFSFH_IDENT0;
        newfh->ident[1] = GF_NFSFH_IDENT1;
        newfh->ident[2] = GF_NFSFH_IDENT2;
        newfh->ident[3] = GF_NFSFH_IDENT3;
        gf_uuid_copy (newfh->gfid, inode->gfid);
        gf_uuid_copy (newfh->exportid, exportid);
        /*gf_uuid_copy (newfh->mountid, mountid);*/
        return 0;
}

int
nfs3_fh_build_child_fh (struct nfs3_fh *parent, struct iatt *newstat,
                        struct nfs3_fh *newfh)
{
        if ((!parent) || (!newstat) || (!newfh))
                return -1;

        nfs3_fh_init (newfh, newstat);
        gf_uuid_copy (newfh->exportid, parent->exportid);
        gf_uuid_copy (newfh->mountid, parent->mountid);
        return 0;
}


uint32_t
nfs3_fh_compute_size ()
{
        return GF_NFSFH_STATIC_SIZE;
}
