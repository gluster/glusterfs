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

#ifndef _MSG_NFS3_H_
#define _MSG_NFS3_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xdr-nfs3.h"

#include <sys/types.h>
#include <sys/uio.h>

extern ssize_t
xdr_to_mountpath (struct iovec outpath, struct iovec inmsg);

extern ssize_t
xdr_serialize_mountres3 (struct iovec outmsg, mountres3 *res);

extern ssize_t
xdr_serialize_mountbody (struct iovec outmsg, mountbody *mb);

extern ssize_t
xdr_to_getattr3args (struct iovec inmsg, getattr3args *ga);

extern ssize_t
xdr_serialize_getattr3res (struct iovec outmsg, getattr3res *res);

extern ssize_t
xdr_serialize_setattr3res (struct iovec outmsg, setattr3res *res);

extern ssize_t
xdr_to_setattr3args (struct iovec inmsg, setattr3args *sa);

extern ssize_t
xdr_serialize_lookup3res (struct iovec outmsg, lookup3res *res);

extern ssize_t
xdr_to_lookup3args (struct iovec inmsg, lookup3args *la);

extern ssize_t
xdr_to_access3args (struct iovec inmsg, access3args *ac);

extern ssize_t
xdr_serialize_access3res (struct iovec outmsg, access3res *res);

extern ssize_t
xdr_to_readlink3args (struct iovec inmsg, readlink3args *ra);

extern ssize_t
xdr_serialize_readlink3res (struct iovec outmsg, readlink3res *res);

extern ssize_t
xdr_to_read3args (struct iovec inmsg, read3args *ra);

extern ssize_t
xdr_serialize_read3res (struct iovec outmsg, read3res *res);

extern ssize_t
xdr_serialize_read3res_nocopy (struct iovec outmsg, read3res *res);

extern ssize_t
xdr_to_write3args (struct iovec inmsg, write3args *wa);

extern ssize_t
xdr_to_write3args_nocopy (struct iovec inmsg, write3args *wa,
                          struct iovec *payload);

extern ssize_t
xdr_serialize_write3res (struct iovec outmsg, write3res *res);

extern ssize_t
xdr_to_create3args (struct iovec inmsg, create3args *ca);

extern ssize_t
xdr_serialize_create3res (struct iovec outmsg, create3res *res);

extern ssize_t
xdr_serialize_mkdir3res (struct iovec outmsg, mkdir3res *res);

extern ssize_t
xdr_to_mkdir3args (struct iovec inmsg, mkdir3args *ma);

extern ssize_t
xdr_to_symlink3args (struct iovec inmsg, symlink3args *sa);

extern ssize_t
xdr_serialize_symlink3res (struct iovec outmsg, symlink3res *res);

extern ssize_t
xdr_to_mknod3args (struct iovec inmsg, mknod3args *ma);

extern ssize_t
xdr_serialize_mknod3res (struct iovec outmsg, mknod3res *res);

extern ssize_t
xdr_to_remove3args (struct iovec inmsg, remove3args *ra);

extern ssize_t
xdr_serialize_remove3res (struct iovec outmsg, remove3res *res);

extern ssize_t
xdr_to_rmdir3args (struct iovec inmsg, rmdir3args *ra);

extern ssize_t
xdr_serialize_rmdir3res (struct iovec outmsg, rmdir3res *res);

extern ssize_t
xdr_serialize_rename3res (struct iovec outmsg, rename3res *res);

extern ssize_t
xdr_to_rename3args (struct iovec inmsg, rename3args *ra);

extern ssize_t
xdr_serialize_link3res (struct iovec outmsg, link3res *li);

extern ssize_t
xdr_to_link3args (struct iovec inmsg, link3args *la);

extern ssize_t
xdr_to_readdir3args (struct iovec inmsg, readdir3args *rd);

extern ssize_t
xdr_serialize_readdir3res (struct iovec outmsg, readdir3res *res);

extern ssize_t
xdr_to_readdirp3args (struct iovec inmsg, readdirp3args *rp);

extern ssize_t
xdr_serialize_readdirp3res (struct iovec outmsg, readdirp3res *res);

extern ssize_t
xdr_to_fsstat3args (struct iovec inmsg, fsstat3args *fa);

extern ssize_t
xdr_serialize_fsstat3res (struct iovec outmsg, fsstat3res *res);

extern ssize_t
xdr_to_fsinfo3args (struct iovec inmsg, fsinfo3args *fi);

extern ssize_t
xdr_serialize_fsinfo3res (struct iovec outmsg, fsinfo3res *res);

extern ssize_t
xdr_to_pathconf3args (struct iovec inmsg, pathconf3args *pc);

extern ssize_t
xdr_serialize_pathconf3res (struct iovec outmsg, pathconf3res *res);

extern ssize_t
xdr_to_commit3args (struct iovec inmsg, commit3args *ca);

extern ssize_t
xdr_serialize_commit3res (struct iovec outmsg, commit3res *res);

extern ssize_t
xdr_serialize_exports (struct iovec outmsg, exports *elist);

extern ssize_t
xdr_serialize_mountlist (struct iovec outmsg, mountlist *ml);

extern ssize_t
xdr_serialize_mountstat3 (struct iovec outmsg, mountstat3 *m);

extern ssize_t
xdr_serialize_nfsstat3 (struct iovec outmsg, nfsstat3 *s);
#endif
