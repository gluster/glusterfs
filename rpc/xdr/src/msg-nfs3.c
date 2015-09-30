/*
  Copyright (c) 2007-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <sys/uio.h>
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <sys/types.h>

#include "xdr-nfs3.h"
#include "msg-nfs3.h"
#include "xdr-generic.h"
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

        ret = xdr_decoded_length (xdr);

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
        return xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_mountres3);
}


ssize_t
xdr_serialize_mountbody (struct iovec outmsg, mountbody *mb)
{
        return xdr_serialize_generic (outmsg, (void *)mb,
                                          (xdrproc_t)xdr_mountbody);
}

ssize_t
xdr_serialize_mountlist (struct iovec outmsg, mountlist *ml)
{
        return xdr_serialize_generic (outmsg, (void *)ml,
                                          (xdrproc_t)xdr_mountlist);
}


ssize_t
xdr_serialize_mountstat3 (struct iovec outmsg, mountstat3 *m)
{
        return xdr_serialize_generic (outmsg, (void *)m,
                                          (xdrproc_t)xdr_mountstat3);
}


ssize_t
xdr_to_getattr3args (struct iovec inmsg, getattr3args *ga)
{
        return xdr_to_generic (inmsg, (void *)ga,
                                   (xdrproc_t)xdr_getattr3args);
}


ssize_t
xdr_serialize_getattr3res (struct iovec outmsg, getattr3res *res)
{
        return xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_getattr3res);
}


ssize_t
xdr_serialize_setattr3res (struct iovec outmsg, setattr3res *res)
{
        return xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_setattr3res);
}


ssize_t
xdr_to_setattr3args (struct iovec inmsg, setattr3args *sa)
{
        return xdr_to_generic (inmsg, (void *)sa,
                                   (xdrproc_t)xdr_setattr3args);
}


ssize_t
xdr_serialize_lookup3res (struct iovec outmsg, lookup3res *res)
{
        return xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_lookup3res);
}


ssize_t
xdr_to_lookup3args (struct iovec inmsg, lookup3args *la)
{
        return xdr_to_generic (inmsg, (void *)la,
                                   (xdrproc_t)xdr_lookup3args);
}


ssize_t
xdr_to_access3args (struct iovec inmsg, access3args *ac)
{
        return xdr_to_generic (inmsg,(void *)ac,
                                   (xdrproc_t)xdr_access3args);
}


ssize_t
xdr_serialize_access3res (struct iovec outmsg, access3res *res)
{
        return xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_access3res);
}


ssize_t
xdr_to_readlink3args (struct iovec inmsg, readlink3args *ra)
{
        return xdr_to_generic (inmsg, (void *)ra,
                                   (xdrproc_t)xdr_readlink3args);
}


ssize_t
xdr_serialize_readlink3res (struct iovec outmsg, readlink3res *res)
{
        return xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_readlink3res);
}


ssize_t
xdr_to_read3args (struct iovec inmsg, read3args *ra)
{
        return xdr_to_generic (inmsg, (void *)ra, (xdrproc_t)xdr_read3args);
}


ssize_t
xdr_serialize_read3res (struct iovec outmsg, read3res *res)
{
        return xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_read3res);
}

ssize_t
xdr_serialize_read3res_nocopy (struct iovec outmsg, read3res *res)
{
        return xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_read3res_nocopy);
}


ssize_t
xdr_to_write3args (struct iovec inmsg, write3args *wa)
{
        return xdr_to_generic (inmsg, (void *)wa,(xdrproc_t)xdr_write3args);
}


ssize_t
xdr_to_write3args_nocopy (struct iovec inmsg, write3args *wa,
                          struct iovec *payload)
{
        return xdr_to_generic_payload (inmsg, (void *)wa,
                                           (xdrproc_t)xdr_write3args, payload);
}


ssize_t
xdr_serialize_write3res (struct iovec outmsg, write3res *res)
{
        return xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_write3res);
}


ssize_t
xdr_to_create3args (struct iovec inmsg, create3args *ca)
{
        return xdr_to_generic (inmsg, (void *)ca,
                                   (xdrproc_t)xdr_create3args);
}


ssize_t
xdr_serialize_create3res (struct iovec outmsg, create3res *res)
{
        return xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_create3res);
}


ssize_t
xdr_serialize_mkdir3res (struct iovec outmsg, mkdir3res *res)
{
        return xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_mkdir3res);
}


ssize_t
xdr_to_mkdir3args (struct iovec inmsg, mkdir3args *ma)
{
        return xdr_to_generic (inmsg, (void *)ma,
                                   (xdrproc_t)xdr_mkdir3args);
}


ssize_t
xdr_to_symlink3args (struct iovec inmsg, symlink3args *sa)
{
        return xdr_to_generic (inmsg, (void *)sa,
                                   (xdrproc_t)xdr_symlink3args);
}


ssize_t
xdr_serialize_symlink3res (struct iovec outmsg, symlink3res *res)
{
        return xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_symlink3res);
}


ssize_t
xdr_to_mknod3args (struct iovec inmsg, mknod3args *ma)
{
        return xdr_to_generic (inmsg, (void *)ma,
                                   (xdrproc_t)xdr_mknod3args);
}


ssize_t
xdr_serialize_mknod3res (struct iovec outmsg, mknod3res *res)
{
        return xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_mknod3res);
}


ssize_t
xdr_to_remove3args (struct iovec inmsg, remove3args *ra)
{
        return xdr_to_generic (inmsg, (void *)ra,
                                   (xdrproc_t)xdr_remove3args);
}


ssize_t
xdr_serialize_remove3res (struct iovec outmsg, remove3res *res)
{
        return xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_remove3res);
}


ssize_t
xdr_to_rmdir3args (struct iovec inmsg, rmdir3args *ra)
{
        return xdr_to_generic (inmsg, (void *)ra,
                                   (xdrproc_t)xdr_rmdir3args);
}


ssize_t
xdr_serialize_rmdir3res (struct iovec outmsg, rmdir3res *res)
{
        return xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_rmdir3res);
}


ssize_t
xdr_serialize_rename3res (struct iovec outmsg, rename3res *res)
{
        return xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_rename3res);
}


ssize_t
xdr_to_rename3args (struct iovec inmsg, rename3args *ra)
{
        return xdr_to_generic (inmsg, (void *)ra,
                                   (xdrproc_t)xdr_rename3args);
}


ssize_t
xdr_serialize_link3res (struct iovec outmsg, link3res *li)
{
        return xdr_serialize_generic (outmsg, (void *)li,
                                          (xdrproc_t)xdr_link3res);
}


ssize_t
xdr_to_link3args (struct iovec inmsg, link3args *la)
{
        return xdr_to_generic (inmsg, (void *)la, (xdrproc_t)xdr_link3args);
}


ssize_t
xdr_to_readdir3args (struct iovec inmsg, readdir3args *rd)
{
        return xdr_to_generic (inmsg, (void *)rd,
                                   (xdrproc_t)xdr_readdir3args);
}


ssize_t
xdr_serialize_readdir3res (struct iovec outmsg, readdir3res *res)
{
        return xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_readdir3res);
}


ssize_t
xdr_to_readdirp3args (struct iovec inmsg, readdirp3args *rp)
{
        return xdr_to_generic (inmsg, (void *)rp,
                                   (xdrproc_t)xdr_readdirp3args);
}


ssize_t
xdr_serialize_readdirp3res (struct iovec outmsg, readdirp3res *res)
{
        return xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_readdirp3res);
}


ssize_t
xdr_to_fsstat3args (struct iovec inmsg, fsstat3args *fa)
{
        return xdr_to_generic (inmsg, (void *)fa,
                                   (xdrproc_t)xdr_fsstat3args);
}


ssize_t
xdr_serialize_fsstat3res (struct iovec outmsg, fsstat3res *res)
{
        return xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_fsstat3res);
}

ssize_t
xdr_to_fsinfo3args (struct iovec inmsg, fsinfo3args *fi)
{
        return xdr_to_generic (inmsg, (void *)fi,
                                   (xdrproc_t)xdr_fsinfo3args);
}


ssize_t
xdr_serialize_fsinfo3res (struct iovec outmsg, fsinfo3res *res)
{
        return xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_fsinfo3res);
}


ssize_t
xdr_to_pathconf3args (struct iovec inmsg, pathconf3args *pc)
{
        return xdr_to_generic (inmsg, (void *)pc,
                                   (xdrproc_t)xdr_pathconf3args);}


ssize_t
xdr_serialize_pathconf3res (struct iovec outmsg, pathconf3res *res)
{
        return xdr_serialize_generic (outmsg, (void *)res,
                                          (xdrproc_t)xdr_pathconf3res);
}


ssize_t
xdr_to_commit3args (struct iovec inmsg, commit3args *ca)
{
        return xdr_to_generic (inmsg, (void *)ca,
                                   (xdrproc_t)xdr_commit3args);
}


ssize_t
xdr_serialize_commit3res (struct iovec outmsg, commit3res *res)
{
        return xdr_serialize_generic (outmsg, (void *)res,
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

        ret = xdr_decoded_length (xdr);

ret:
        return ret;
}


ssize_t
xdr_serialize_nfsstat3 (struct iovec outmsg, nfsstat3 *s)
{
        return xdr_serialize_generic (outmsg, (void *)s,
                                          (xdrproc_t)xdr_nfsstat3);
}

ssize_t
xdr_to_nlm4_testargs (struct iovec inmsg, nlm4_testargs *args)
{
        return xdr_to_generic (inmsg, (void*)args,
                               (xdrproc_t)xdr_nlm4_testargs);
}

ssize_t
xdr_serialize_nlm4_testres (struct iovec outmsg, nlm4_testres *res)
{
        return xdr_serialize_generic (outmsg, (void*)res,
                                      (xdrproc_t)xdr_nlm4_testres);
}

ssize_t
xdr_to_nlm4_lockargs (struct iovec inmsg, nlm4_lockargs *args)
{
        return xdr_to_generic (inmsg, (void*)args,
                               (xdrproc_t)xdr_nlm4_lockargs);
}

ssize_t
xdr_serialize_nlm4_res (struct iovec outmsg, nlm4_res *res)
{
        return xdr_serialize_generic (outmsg, (void*)res,
                                      (xdrproc_t)xdr_nlm4_res);
}

ssize_t
xdr_to_nlm4_cancelargs (struct iovec inmsg, nlm4_cancargs *args)
{
        return xdr_to_generic (inmsg, (void*)args,
                               (xdrproc_t)xdr_nlm4_cancargs);
}

ssize_t
xdr_to_nlm4_unlockargs (struct iovec inmsg, nlm4_unlockargs *args)
{
        return xdr_to_generic (inmsg, (void*)args,
                               (xdrproc_t)xdr_nlm4_unlockargs);
}

ssize_t
xdr_to_nlm4_shareargs (struct iovec inmsg, nlm4_shareargs *args)
{
        return xdr_to_generic (inmsg, (void*)args,
                               (xdrproc_t)xdr_nlm4_shareargs);
}

ssize_t
xdr_serialize_nlm4_shareres (struct iovec outmsg, nlm4_shareres *res)
{
        return xdr_serialize_generic (outmsg, (void *)res,
                                      (xdrproc_t)xdr_nlm4_shareres);
}

ssize_t
xdr_serialize_nlm4_testargs (struct iovec outmsg, nlm4_testargs *args)
{
        return xdr_serialize_generic (outmsg, (void*)args,
                                      (xdrproc_t)xdr_nlm4_testargs);
}

ssize_t
xdr_to_nlm4_res (struct iovec inmsg, nlm4_res *args)
{
        return xdr_to_generic (inmsg, (void*)args,
                               (xdrproc_t)xdr_nlm4_res);
}

ssize_t
xdr_to_nlm4_freeallargs (struct iovec inmsg, nlm4_freeallargs *args)
{
        return xdr_to_generic (inmsg, (void*)args,
                               (xdrproc_t)xdr_nlm4_freeallargs);
}

ssize_t
xdr_to_getaclargs (struct iovec inmsg, getaclargs *args)
{
        return xdr_to_generic (inmsg, (void *) args,
                               (xdrproc_t)xdr_getaclargs);
}

ssize_t
xdr_to_setaclargs (struct iovec inmsg, setaclargs *args)
{
        return xdr_to_generic (inmsg, (void *) args,
                               (xdrproc_t)xdr_setaclargs);
}

ssize_t
xdr_serialize_getaclreply (struct iovec inmsg, getaclreply *res)
{
        return xdr_serialize_generic (inmsg, (void *) res,
                                      (xdrproc_t)xdr_getaclreply);
}

ssize_t
xdr_serialize_setaclreply (struct iovec inmsg, setaclreply *res)
{
        return xdr_serialize_generic (inmsg, (void *) res,
                                      (xdrproc_t)xdr_setaclreply);
}

