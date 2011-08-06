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

#include <sys/uio.h>
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <sys/types.h>

#include "xdr-nfs3.h"
#include "msg-nfs3.h"
#include "xdr-common.h"


/* Decode the mount path from the network message in inmsg
 * into the memory referenced by outpath.iov_base.
 * The size allocated for outpath.iov_base is outpath.iov_len.
 * The size of the path extracted from the message is returned.
 */
ssize_t
xdr_to_mountpath (struct iovec outpath, struct iovec inmsg)
{
        XDR     xdr;
        ssize_t ret = -1;
        char    *mntpath = NULL;

        if ((!outpath.iov_base) || (!inmsg.iov_base))
                return -1;

        xdrmem_create (&xdr, inmsg.iov_base, (unsigned int)inmsg.iov_len,
                       XDR_DECODE);

        mntpath = outpath.iov_base;
        if (!xdr_dirpath (&xdr, (dirpath *)&mntpath)) {
                ret = -1;
                goto ret;
        }

        ret = nfs_xdr_decoded_length (xdr);

ret:
        return ret;
}


ssize_t
nfs_xdr_serialize_generic (struct iovec outmsg, void *res, xdrproc_t proc)
{
        ssize_t ret = -1;
        XDR     xdr;

        if ((!outmsg.iov_base) || (!res) || (!proc))
                return -1;

        xdrmem_create (&xdr, outmsg.iov_base, (unsigned int)outmsg.iov_len,
                       XDR_ENCODE);

        if (!proc (&xdr, res)) {
                ret = -1;
                goto ret;
        }

        ret = nfs_xdr_encoded_length (xdr);

ret:
        return ret;
}


ssize_t
nfs_xdr_to_generic (struct iovec inmsg, void *args, xdrproc_t proc)
{
        XDR     xdr;
        ssize_t ret = -1;

        if ((!inmsg.iov_base) || (!args) || (!proc))
                return -1;

        xdrmem_create (&xdr, inmsg.iov_base, (unsigned int)inmsg.iov_len,
                       XDR_DECODE);

        if (!proc (&xdr, args)) {
                ret  = -1;
                goto ret;
        }

        ret = nfs_xdr_decoded_length (xdr);
ret:
        return ret;
}


ssize_t
nfs_xdr_to_generic_payload (struct iovec inmsg, void *args, xdrproc_t proc,
                            struct iovec *pendingpayload)
{
        XDR     xdr;
        ssize_t ret = -1;

        if ((!inmsg.iov_base) || (!args) || (!proc))
                return -1;

        xdrmem_create (&xdr, inmsg.iov_base, (unsigned int)inmsg.iov_len,
                       XDR_DECODE);

        if (!proc (&xdr, args)) {
                ret  = -1;
                goto ret;
        }

        ret = nfs_xdr_decoded_length (xdr);

        if (pendingpayload) {
                pendingpayload->iov_base = nfs_xdr_decoded_remaining_addr (xdr);
                pendingpayload->iov_len = nfs_xdr_decoded_remaining_len (xdr);
        }

ret:
        return ret;
}


/* Translate the mountres3 structure in res into XDR format into memory
 * referenced by outmsg.iov_base.
 * Returns the number of bytes used in encoding into XDR format.
 */
ssize_t
xdr_serialize_mountres3 (struct iovec outmsg, mountres3 *res)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_mountres3);
}


ssize_t
xdr_serialize_mountbody (struct iovec outmsg, mountbody *mb)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)mb,
                                          (xdrproc_t)xdr_mountbody);
}

ssize_t
xdr_serialize_mountlist (struct iovec outmsg, mountlist *ml)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)ml,
                                          (xdrproc_t)xdr_mountlist);
}


ssize_t
xdr_serialize_mountstat3 (struct iovec outmsg, mountstat3 *m)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)m,
                                          (xdrproc_t)xdr_mountstat3);
}


ssize_t
xdr_to_getattr3args (struct iovec inmsg, getattr3args *ga)
{
        return nfs_xdr_to_generic (inmsg, (void *)ga,
                                   (xdrproc_t)xdr_getattr3args);
}


ssize_t
xdr_serialize_getattr3res (struct iovec outmsg, getattr3res *res)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_getattr3res);
}


ssize_t
xdr_serialize_setattr3res (struct iovec outmsg, setattr3res *res)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_setattr3res);
}


ssize_t
xdr_to_setattr3args (struct iovec inmsg, setattr3args *sa)
{
        return nfs_xdr_to_generic (inmsg, (void *)sa,
                                   (xdrproc_t)xdr_setattr3args);
}


ssize_t
xdr_serialize_lookup3res (struct iovec outmsg, lookup3res *res)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_lookup3res);
}


ssize_t
xdr_to_lookup3args (struct iovec inmsg, lookup3args *la)
{
        return nfs_xdr_to_generic (inmsg, (void *)la,
                                   (xdrproc_t)xdr_lookup3args);
}


ssize_t
xdr_to_access3args (struct iovec inmsg, access3args *ac)
{
        return nfs_xdr_to_generic (inmsg,(void *)ac,
                                   (xdrproc_t)xdr_access3args);
}


ssize_t
xdr_serialize_access3res (struct iovec outmsg, access3res *res)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_access3res);
}


ssize_t
xdr_to_readlink3args (struct iovec inmsg, readlink3args *ra)
{
        return nfs_xdr_to_generic (inmsg, (void *)ra,
                                   (xdrproc_t)xdr_readlink3args);
}


ssize_t
xdr_serialize_readlink3res (struct iovec outmsg, readlink3res *res)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_readlink3res);
}


ssize_t
xdr_to_read3args (struct iovec inmsg, read3args *ra)
{
        return nfs_xdr_to_generic (inmsg, (void *)ra, (xdrproc_t)xdr_read3args);
}


ssize_t
xdr_serialize_read3res (struct iovec outmsg, read3res *res)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_read3res);
}

ssize_t
xdr_serialize_read3res_nocopy (struct iovec outmsg, read3res *res)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_read3res_nocopy);
}


ssize_t
xdr_to_write3args (struct iovec inmsg, write3args *wa)
{
        return nfs_xdr_to_generic (inmsg, (void *)wa,(xdrproc_t)xdr_write3args);
}


ssize_t
xdr_to_write3args_nocopy (struct iovec inmsg, write3args *wa,
                          struct iovec *payload)
{
        return nfs_xdr_to_generic_payload (inmsg, (void *)wa,
                                           (xdrproc_t)xdr_write3args, payload);
}


ssize_t
xdr_serialize_write3res (struct iovec outmsg, write3res *res)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_write3res);
}


ssize_t
xdr_to_create3args (struct iovec inmsg, create3args *ca)
{
        return nfs_xdr_to_generic (inmsg, (void *)ca,
                                   (xdrproc_t)xdr_create3args);
}


ssize_t
xdr_serialize_create3res (struct iovec outmsg, create3res *res)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_create3res);
}


ssize_t
xdr_serialize_mkdir3res (struct iovec outmsg, mkdir3res *res)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_mkdir3res);
}


ssize_t
xdr_to_mkdir3args (struct iovec inmsg, mkdir3args *ma)
{
        return nfs_xdr_to_generic (inmsg, (void *)ma,
                                   (xdrproc_t)xdr_mkdir3args);
}


ssize_t
xdr_to_symlink3args (struct iovec inmsg, symlink3args *sa)
{
        return nfs_xdr_to_generic (inmsg, (void *)sa,
                                   (xdrproc_t)xdr_symlink3args);
}


ssize_t
xdr_serialize_symlink3res (struct iovec outmsg, symlink3res *res)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_symlink3res);
}


ssize_t
xdr_to_mknod3args (struct iovec inmsg, mknod3args *ma)
{
        return nfs_xdr_to_generic (inmsg, (void *)ma,
                                   (xdrproc_t)xdr_mknod3args);
}


ssize_t
xdr_serialize_mknod3res (struct iovec outmsg, mknod3res *res)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_mknod3res);
}


ssize_t
xdr_to_remove3args (struct iovec inmsg, remove3args *ra)
{
        return nfs_xdr_to_generic (inmsg, (void *)ra,
                                   (xdrproc_t)xdr_remove3args);
}


ssize_t
xdr_serialize_remove3res (struct iovec outmsg, remove3res *res)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_remove3res);
}


ssize_t
xdr_to_rmdir3args (struct iovec inmsg, rmdir3args *ra)
{
        return nfs_xdr_to_generic (inmsg, (void *)ra,
                                   (xdrproc_t)xdr_rmdir3args);
}


ssize_t
xdr_serialize_rmdir3res (struct iovec outmsg, rmdir3res *res)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_rmdir3res);
}


ssize_t
xdr_serialize_rename3res (struct iovec outmsg, rename3res *res)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_rename3res);
}


ssize_t
xdr_to_rename3args (struct iovec inmsg, rename3args *ra)
{
        return nfs_xdr_to_generic (inmsg, (void *)ra,
                                   (xdrproc_t)xdr_rename3args);
}


ssize_t
xdr_serialize_link3res (struct iovec outmsg, link3res *li)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)li,
                                          (xdrproc_t)xdr_link3res);
}


ssize_t
xdr_to_link3args (struct iovec inmsg, link3args *la)
{
        return nfs_xdr_to_generic (inmsg, (void *)la, (xdrproc_t)xdr_link3args);
}


ssize_t
xdr_to_readdir3args (struct iovec inmsg, readdir3args *rd)
{
        return nfs_xdr_to_generic (inmsg, (void *)rd,
                                   (xdrproc_t)xdr_readdir3args);
}


ssize_t
xdr_serialize_readdir3res (struct iovec outmsg, readdir3res *res)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_readdir3res);
}


ssize_t
xdr_to_readdirp3args (struct iovec inmsg, readdirp3args *rp)
{
        return nfs_xdr_to_generic (inmsg, (void *)rp,
                                   (xdrproc_t)xdr_readdirp3args);
}


ssize_t
xdr_serialize_readdirp3res (struct iovec outmsg, readdirp3res *res)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_readdirp3res);
}


ssize_t
xdr_to_fsstat3args (struct iovec inmsg, fsstat3args *fa)
{
        return nfs_xdr_to_generic (inmsg, (void *)fa,
                                   (xdrproc_t)xdr_fsstat3args);
}


ssize_t
xdr_serialize_fsstat3res (struct iovec outmsg, fsstat3res *res)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_fsstat3res);
}

ssize_t
xdr_to_fsinfo3args (struct iovec inmsg, fsinfo3args *fi)
{
        return nfs_xdr_to_generic (inmsg, (void *)fi,
                                   (xdrproc_t)xdr_fsinfo3args);
}


ssize_t
xdr_serialize_fsinfo3res (struct iovec outmsg, fsinfo3res *res)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_fsinfo3res);
}


ssize_t
xdr_to_pathconf3args (struct iovec inmsg, pathconf3args *pc)
{
        return nfs_xdr_to_generic (inmsg, (void *)pc,
                                   (xdrproc_t)xdr_pathconf3args);}


ssize_t
xdr_serialize_pathconf3res (struct iovec outmsg, pathconf3res *res)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_pathconf3res);
}


ssize_t
xdr_to_commit3args (struct iovec inmsg, commit3args *ca)
{
        return nfs_xdr_to_generic (inmsg, (void *)ca,
                                   (xdrproc_t)xdr_commit3args);
}


ssize_t
xdr_serialize_commit3res (struct iovec outmsg, commit3res *res)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_commit3res);
}


ssize_t
xdr_serialize_exports (struct iovec outmsg, exports *elist)
{
        XDR     xdr;
        ssize_t  ret = -1;

        if ((!outmsg.iov_base) || (!elist))
                return -1;

        xdrmem_create (&xdr, outmsg.iov_base, (unsigned int)outmsg.iov_len,
                       XDR_ENCODE);

        if (!xdr_exports (&xdr, elist))
                goto ret;

        ret = nfs_xdr_decoded_length (xdr);

ret:
        return ret;
}


ssize_t
xdr_serialize_nfsstat3 (struct iovec outmsg, nfsstat3 *s)
{
        return nfs_xdr_serialize_generic (outmsg, (void *)s,
                                          (xdrproc_t)xdr_nfsstat3);
}


