/*
  Copyright (c) 2007-2010 Gluster, Inc. <http://www.gluster.com>
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


#include "glusterfs3.h"
#include "xdr-generic.h"


/* Encode */

ssize_t
xdr_serialize_getspec_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf_getspec_rsp);

}

ssize_t
xdr_serialize_lookup_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_lookup_rsp);

}

ssize_t
xdr_serialize_common_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf_common_rsp);

}

ssize_t
xdr_serialize_setvolume_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf_setvolume_rsp);

}
ssize_t
xdr_serialize_statfs_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_statfs_rsp);

}
ssize_t
xdr_serialize_stat_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_stat_rsp);

}
ssize_t
xdr_serialize_fstat_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_fstat_rsp);

}
ssize_t
xdr_serialize_open_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_open_rsp);

}
ssize_t
xdr_serialize_read_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_read_rsp);

}
ssize_t
xdr_serialize_write_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_write_rsp);

}
ssize_t
xdr_serialize_rename_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_rename_rsp);

}
ssize_t
xdr_serialize_fsync_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_fsync_rsp);

}
ssize_t
xdr_serialize_rmdir_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_rmdir_rsp);
}
ssize_t
xdr_serialize_unlink_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_unlink_rsp);
}
ssize_t
xdr_serialize_writev_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_write_rsp);
}
ssize_t
xdr_serialize_readv_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_read_rsp);
}
ssize_t
xdr_serialize_readdir_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_readdir_rsp);
}
ssize_t
xdr_serialize_readdirp_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_readdirp_rsp);
}
ssize_t
xdr_serialize_rchecksum_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_rchecksum_rsp);
}
ssize_t
xdr_serialize_setattr_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_setattr_rsp);
}
ssize_t
xdr_serialize_fsetattr_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_fsetattr_rsp);
}

ssize_t
xdr_serialize_readlink_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_readlink_rsp);

}
ssize_t
xdr_serialize_symlink_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_symlink_rsp);

}
ssize_t
xdr_serialize_create_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_create_rsp);

}
ssize_t
xdr_serialize_link_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_link_rsp);

}
ssize_t
xdr_serialize_mkdir_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_mkdir_rsp);

}
ssize_t
xdr_serialize_mknod_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_mknod_rsp);

}
ssize_t
xdr_serialize_getxattr_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_getxattr_rsp);

}
ssize_t
xdr_serialize_fgetxattr_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_fgetxattr_rsp);

}
ssize_t
xdr_serialize_xattrop_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_xattrop_rsp);

}
ssize_t
xdr_serialize_fxattrop_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_fxattrop_rsp);
}

ssize_t
xdr_serialize_truncate_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_truncate_rsp);
}

ssize_t
xdr_serialize_lk_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_lk_rsp);
}

ssize_t
xdr_serialize_opendir_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_opendir_rsp);
}

ssize_t
xdr_serialize_ftruncate_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_ftruncate_rsp);
}


ssize_t
xdr_to_lookup_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_lookup_req);
}

ssize_t
xdr_to_getspec_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                                      (xdrproc_t)xdr_gf_getspec_req);

}

ssize_t
xdr_to_setvolume_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                                      (xdrproc_t)xdr_gf_setvolume_req);

}

ssize_t
xdr_to_statfs_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_statfs_req);

}

ssize_t
xdr_to_fsync_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_fsync_req);

}

ssize_t
xdr_to_flush_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_flush_req);

}

ssize_t
xdr_to_xattrop_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_xattrop_req);

}

ssize_t
xdr_to_fxattrop_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_fxattrop_req);

}

ssize_t
xdr_to_getxattr_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_getxattr_req);

}
ssize_t
xdr_to_fgetxattr_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_fgetxattr_req);

}
ssize_t
xdr_to_open_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_open_req);

}
ssize_t
xdr_to_create_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_create_req);

}
ssize_t
xdr_to_symlink_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_symlink_req);
}
ssize_t
xdr_to_link_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_link_req);
}
ssize_t
xdr_to_readlink_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_readlink_req);
}
ssize_t
xdr_to_rename_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_rename_req);
}
ssize_t
xdr_to_mkdir_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_mkdir_req);
}
ssize_t
xdr_to_mknod_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_mknod_req);
}
ssize_t
xdr_to_readv_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_read_req);
}
ssize_t
xdr_to_writev_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_write_req);
}

ssize_t
xdr_to_readdir_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_readdir_req);
}

ssize_t
xdr_to_opendir_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_opendir_req);
}

ssize_t
xdr_to_rmdir_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_rmdir_req);
}

ssize_t
xdr_to_fsetxattr_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_fsetxattr_req);
}
ssize_t
xdr_to_setattr_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_setattr_req);
}
ssize_t
xdr_to_fsetattr_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_fsetattr_req);
}

ssize_t
xdr_to_finodelk_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_finodelk_req);
}

ssize_t
xdr_to_inodelk_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_inodelk_req);
}

ssize_t
xdr_to_ftruncate_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_ftruncate_req);
}

ssize_t
xdr_to_fsyncdir_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_fsyncdir_req);
}

ssize_t
xdr_to_fstat_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_fstat_req);
}
ssize_t
xdr_to_rchecksum_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_rchecksum_req);
}
ssize_t
xdr_to_removexattr_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_removexattr_req);
}
ssize_t
xdr_to_setxattr_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_setxattr_req);
}

ssize_t
xdr_to_fentrylk_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_fentrylk_req);
}

ssize_t
xdr_to_entrylk_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_entrylk_req);
}

ssize_t
xdr_to_lk_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_lk_req);
}

ssize_t
xdr_to_stat_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_stat_req);
}

ssize_t
xdr_to_release_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_release_req);
}

ssize_t
xdr_to_readdirp_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_readdirp_req);
}
ssize_t
xdr_to_truncate_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_truncate_req);
}
ssize_t
xdr_to_access_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_access_req);
}
ssize_t
xdr_to_unlink_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gfs3_unlink_req);
}

ssize_t
xdr_from_lookup_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_lookup_req);

}

ssize_t
xdr_from_stat_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_stat_req);

}

ssize_t
xdr_from_fstat_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_fstat_req);

}

ssize_t
xdr_from_mkdir_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_mkdir_req);

}

ssize_t
xdr_from_mknod_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_mknod_req);

}

ssize_t
xdr_from_symlink_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_symlink_req);

}

ssize_t
xdr_from_readlink_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_readlink_req);

}

ssize_t
xdr_from_rename_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_rename_req);

}

ssize_t
xdr_from_link_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_link_req);

}

ssize_t
xdr_from_create_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_create_req);

}

ssize_t
xdr_from_open_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_open_req);

}

ssize_t
xdr_from_opendir_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_opendir_req);

}

ssize_t
xdr_from_readdir_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_readdir_req);

}

ssize_t
xdr_from_readdirp_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_readdirp_req);

}

ssize_t
xdr_from_fsyncdir_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_fsyncdir_req);

}
ssize_t
xdr_from_releasedir_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_releasedir_req);

}
ssize_t
xdr_from_release_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_release_req);

}
ssize_t
xdr_from_lk_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_lk_req);

}
ssize_t
xdr_from_entrylk_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_entrylk_req);

}
ssize_t
xdr_from_fentrylk_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_fentrylk_req);

}
ssize_t
xdr_from_inodelk_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_inodelk_req);

}
ssize_t
xdr_from_finodelk_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_finodelk_req);

}
ssize_t
xdr_from_setxattr_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_setxattr_req);

}
ssize_t
xdr_from_fsetxattr_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_fsetxattr_req);

}
ssize_t
xdr_from_getxattr_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_getxattr_req);

}
ssize_t
xdr_from_fgetxattr_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_fgetxattr_req);

}
ssize_t
xdr_from_removexattr_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_removexattr_req);

}
ssize_t
xdr_from_xattrop_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_xattrop_req);

}
ssize_t
xdr_from_fxattrop_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_fxattrop_req);

}
ssize_t
xdr_from_access_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_access_req);

}
ssize_t
xdr_from_setattr_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_setattr_req);

}
ssize_t
xdr_from_truncate_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_truncate_req);

}
ssize_t
xdr_from_ftruncate_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_ftruncate_req);

}
ssize_t
xdr_from_fsetattr_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_fsetattr_req);

}
ssize_t
xdr_from_readv_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_read_req);

}
ssize_t
xdr_from_writev_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_write_req);

}
ssize_t
xdr_from_fsync_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_fsync_req);

}
ssize_t
xdr_from_flush_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_flush_req);

}
ssize_t
xdr_from_statfs_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_statfs_req);

}
ssize_t
xdr_from_rchecksum_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_rchecksum_req);

}
ssize_t
xdr_from_getspec_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gf_getspec_req);

}
ssize_t
xdr_from_setvolume_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gf_setvolume_req);

}
ssize_t
xdr_from_rmdir_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_rmdir_req);

}
ssize_t
xdr_from_unlink_req (struct iovec outmsg, void *req)
{
        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_unlink_req);

}

/* Client decode */

ssize_t
xdr_to_lookup_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_lookup_rsp);

}

ssize_t
xdr_to_stat_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_stat_rsp);

}

ssize_t
xdr_to_fstat_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_fstat_rsp);

}

ssize_t
xdr_to_mkdir_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_mkdir_rsp);

}

ssize_t
xdr_to_mknod_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_mknod_rsp);

}

ssize_t
xdr_to_symlink_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_symlink_rsp);

}

ssize_t
xdr_to_readlink_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_readlink_rsp);

}

ssize_t
xdr_to_rename_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_rename_rsp);

}

ssize_t
xdr_to_link_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_link_rsp);

}

ssize_t
xdr_to_create_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_create_rsp);

}

ssize_t
xdr_to_open_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_open_rsp);

}

ssize_t
xdr_to_opendir_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_opendir_rsp);

}

ssize_t
xdr_to_readdir_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_readdir_rsp);

}

ssize_t
xdr_to_readdirp_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_readdirp_rsp);

}
ssize_t
xdr_to_lk_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_lk_rsp);

}
ssize_t
xdr_to_getxattr_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_getxattr_rsp);

}
ssize_t
xdr_to_fgetxattr_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_fgetxattr_rsp);

}
ssize_t
xdr_to_xattrop_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_xattrop_rsp);

}
ssize_t
xdr_to_fxattrop_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_fxattrop_rsp);

}
ssize_t
xdr_to_setattr_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_setattr_rsp);

}
ssize_t
xdr_to_truncate_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_truncate_rsp);

}
ssize_t
xdr_to_ftruncate_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_ftruncate_rsp);

}
ssize_t
xdr_to_fsetattr_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_fsetattr_rsp);

}
ssize_t
xdr_to_readv_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_read_rsp);

}
ssize_t
xdr_to_writev_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_write_rsp);

}
ssize_t
xdr_to_fsync_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_fsync_rsp);

}
ssize_t
xdr_to_statfs_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_statfs_rsp);

}
ssize_t
xdr_to_rchecksum_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gfs3_rchecksum_rsp);

}
ssize_t
xdr_to_getspec_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf_getspec_rsp);

}
ssize_t
xdr_to_setvolume_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf_setvolume_rsp);

}
ssize_t
xdr_to_rmdir_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                               (xdrproc_t)xdr_gfs3_rmdir_rsp);

}
ssize_t
xdr_to_unlink_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                               (xdrproc_t)xdr_gfs3_unlink_rsp);

}
ssize_t
xdr_to_common_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_to_generic (outmsg, (void *)rsp,
                               (xdrproc_t)xdr_gf_common_rsp);

}

ssize_t
xdr_to_mgmt_probe_query_req (struct iovec outmsg, void *req)
{

        return xdr_serialize_generic (outmsg, (void *)req,
                                      (xdrproc_t)xdr_gfs3_setattr_req);
}
