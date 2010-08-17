/*
  Copyright (c) 2007-2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/


#ifndef _GLUSTERFS3_H
#define _GLUSTERFS3_H

#include <sys/uio.h>

#include "glusterfs3-xdr.h"

#define xdr_decoded_remaining_addr(xdr)        ((&xdr)->x_private)
#define xdr_decoded_remaining_len(xdr)         ((&xdr)->x_handy)
#define xdr_encoded_length(xdr) (((size_t)(&xdr)->x_private) - ((size_t)(&xdr)->x_base))
#define xdr_decoded_length(xdr) (((size_t)(&xdr)->x_private) - ((size_t)(&xdr)->x_base))


/* FOPS */
ssize_t
xdr_serialize_lookup_rsp (struct iovec outmsg, void *resp);

ssize_t
xdr_serialize_getspec_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_common_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_setvolume_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_open_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_create_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_mknod_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_mkdir_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_symlink_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_link_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_rename_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_writev_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_readv_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_readdir_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_readdirp_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_opendir_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_setattr_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_fsetattr_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_truncate_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_ftruncate_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_statfs_rsp (struct iovec outmsg, void *rsp);


ssize_t
xdr_serialize_lk_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_xattrop_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_fxattrop_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_getxattr_rsp (struct iovec outmsg, void *rsp);


ssize_t
xdr_serialize_fgetxattr_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_unlink_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_rmdir_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_rchecksum_rsp (struct iovec outmsg, void *rsp);


ssize_t
xdr_serialize_fstat_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_fsync_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_readlink_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_stat_rsp (struct iovec outmsg, void *rsp);


ssize_t
xdr_to_lookup_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_getspec_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_setvolume_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_statfs_req (struct iovec inmsg, void *args);


ssize_t
xdr_to_stat_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_getattr_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_fstat_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_setattr_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_fsetattr_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_readv_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_writev_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_fsetattr_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_readlink_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_create_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_open_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_release_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_xattrop_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_fxattrop_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_setxattr_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_fsetxattr_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_flush_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_unlink_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_fsync_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_ftruncate_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_truncate_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_getxattr_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_fgetxattr_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_removexattr_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_entrylk_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_fentrylk_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_inodelk_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_finodelk_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_lk_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_access_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_opendir_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_readdirp_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_readdir_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_fsyncdir_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_mknod_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_mkdir_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_symlink_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_rmdir_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_rchecksum_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_rename_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_link_req (struct iovec inmsg, void *args);

ssize_t
xdr_from_lookup_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_getspec_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_stat_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_access_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_truncate_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_ftruncate_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_readlink_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_writev_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_readv_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_flush_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_fstat_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_fsync_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_open_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_unlink_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_rmdir_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_fsyncdir_req (struct iovec outmsg, void *args);


ssize_t
xdr_from_fsetxattr_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_setxattr_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_getxattr_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_fgetxattr_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_statfs_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_opendir_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_lk_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_inodelk_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_finodelk_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_entrylk_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_fentrylk_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_removexattr_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_xattrop_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_fxattrop_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_rchecksum_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_readdir_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_readdirp_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_setattr_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_fsetattr_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_symlink_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_rename_req (struct iovec outmsg, void *args);


ssize_t
xdr_from_link_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_rename_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_create_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_mkdir_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_mknod_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_releasedir_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_release_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_setvolume_req (struct iovec outmsg, void *args);

ssize_t
xdr_to_setvolume_rsp (struct iovec inmsg, void *args);



ssize_t
xdr_to_statfs_rsp (struct iovec inmsg, void *args);


ssize_t
xdr_to_stat_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_fstat_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_rename_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_readlink_rsp (struct iovec inmsg, void *args);


ssize_t
xdr_to_link_rsp (struct iovec inmsg, void *args);


ssize_t
xdr_to_access_rsp (struct iovec inmsg, void *args);


ssize_t
xdr_to_truncate_rsp (struct iovec inmsg, void *args);


ssize_t
xdr_to_ftruncate_rsp (struct iovec inmsg, void *args);


ssize_t
xdr_to_unlink_rsp (struct iovec inmsg, void *args);


ssize_t
xdr_to_rmdir_rsp (struct iovec inmsg, void *args);


ssize_t
xdr_to_open_rsp (struct iovec inmsg, void *args);


ssize_t
xdr_to_create_rsp (struct iovec inmsg, void *args);


ssize_t
xdr_to_mkdir_rsp (struct iovec inmsg, void *args);


ssize_t
xdr_to_mknod_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_setattr_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_fsetattr_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_common_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_getxattr_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_fxattrop_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_xattrop_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_symlink_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_fgetxattr_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_rchecksum_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_lk_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_readdirp_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_readdir_rsp (struct iovec inmsg, void *args);
ssize_t
xdr_to_opendir_rsp (struct iovec inmsg, void *args);
ssize_t
xdr_to_lookup_rsp (struct iovec inmsg, void *args);
ssize_t
xdr_to_readv_rsp (struct iovec inmsg, void *args);
ssize_t
xdr_to_getspec_rsp (struct iovec inmsg, void *args);

#endif /* !_GLUSTERFS3_H */
