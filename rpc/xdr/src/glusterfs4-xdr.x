/*
 * Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>
 * This file is part of GlusterFS.
 *
 * This file is licensed to you under your choice of the GNU Lesser
 * General Public License, version 3 or any later version (LGPLv3 or
 * later), or the GNU General Public License, version 2 (GPLv2), in all
 * cases as published by the Free Software Foundation.
 */

#ifdef RPC_XDR
%#include "rpc-pragmas.h"
#endif
%#include "compat.h"
%#include "glusterfs-fops.h"
%#include "glusterfs3-xdr.h"


/* Need to consume iattx and new dict in all the fops */
struct gfx_iattx {
        opaque       ia_gfid[16];

        unsigned hyper     ia_flags;
        unsigned hyper     ia_ino;        /* inode number */
        unsigned hyper     ia_dev;        /* backing device ID */
        unsigned hyper     ia_rdev;       /* device ID (if special file) */
        unsigned hyper     ia_size;       /* file size in bytes */
        unsigned hyper     ia_blocks;     /* number of 512B blocks allocated */
        unsigned hyper     ia_attributes; /* chattr related:compressed, immutable,
                                     * append only, encrypted etc.*/
        unsigned hyper     ia_attributes_mask; /* Mask for the attributes */

        hyper      ia_atime;      /* last access time */
        hyper      ia_mtime;      /* last modification time */
        hyper      ia_ctime;      /* last status change time */
        hyper      ia_btime;      /* creation time. Fill using statx */

        unsigned int     ia_atime_nsec;
        unsigned int     ia_mtime_nsec;
        unsigned int     ia_ctime_nsec;
        unsigned int     ia_btime_nsec;
        unsigned int     ia_nlink;      /* Link count */
        unsigned int     ia_uid;        /* user ID of owner */
        unsigned int     ia_gid;        /* group ID of owner */
        unsigned int     ia_blksize;    /* blocksize for filesystem I/O */
        unsigned int     mode;          /* type of file and rwx mode */
};

union gfx_value switch (gf_dict_data_type_t type) {
        case GF_DATA_TYPE_INT:
                hyper value_int;
        case GF_DATA_TYPE_UINT:
                unsigned hyper value_uint;
        case GF_DATA_TYPE_DOUBLE:
                double value_dbl;
        case GF_DATA_TYPE_STR:
                opaque val_string<>;
        case GF_DATA_TYPE_IATT:
                gfx_iattx iatt;
        case GF_DATA_TYPE_GFUUID:
                opaque uuid[16];
        case GF_DATA_TYPE_PTR:
                opaque other<>;
};

/* AUTH */
/* This is used in the rpc header part itself, And not program payload.
   Avoid sending large data load here. Allowed maximum is 400 bytes.
   Ref: http://tools.ietf.org/html/rfc5531#section-8.2
   this is also handled in xdr-common.h
*/
struct auth_glusterfs_params_v3 {
        int pid;
        unsigned int uid;
        unsigned int gid;

        /* flags */
        /* Makes sense to use it for each bits */
        /* 0x1 == IS_INTERNAL? */
        /* Another 31 bits are reserved */
        unsigned int flags;

        /* birth time of the frame / call */
        unsigned int ctime_nsec; /* good to have 32bit for this */
        unsigned hyper ctime_sec;

        unsigned int groups<>;
        opaque lk_owner<>;
};

struct gfx_dict_pair {
       opaque key<>;
       gfx_value value;
};

struct gfx_dict {
       unsigned int xdr_size;
       int count;
       gfx_dict_pair pairs<>;
};

/* FOPS */
struct gfx_common_rsp {
       int    op_ret;
       int    op_errno;
       gfx_dict xdata; /* Extra data */
};

struct gfx_common_iatt_rsp {
       int op_ret;
       int op_errno;
       gfx_dict xdata;
       gfx_iattx stat;
};

struct gfx_common_2iatt_rsp {
       int op_ret;
       int op_errno;
       gfx_dict xdata;
       gfx_iattx prestat;
       gfx_iattx poststat;
};

struct gfx_common_3iatt_rsp {
        int    op_ret;
        int    op_errno;
        gfx_dict xdata; /* Extra data */
        gfx_iattx stat;
        gfx_iattx preparent;
        gfx_iattx postparent;
};

struct gfx_fsetattr_req {
        opaque gfid[16];
        hyper        fd;
        gfx_iattx stbuf;
        int        valid;
        gfx_dict xdata; /* Extra data */
};

struct gfx_rchecksum_req {
        opaque gfid[16];
        hyper   fd;
        unsigned hyper  offset;
        unsigned int  len;
        unsigned int  flags;
        gfx_dict xdata; /* Extra data */
};

struct gfx_icreate_req {
       opaque gfid[16];
       unsigned int mode;
       gfx_dict xdata;
};

struct gfx_put_req {
        opaque       pargfid[16];
        string       bname<>;
        unsigned int mode;
        unsigned int umask;
        unsigned int flag;
        u_quad_t     offset;
        unsigned int size;
        gfx_dict     xattr;
        gfx_dict     xdata;
};

struct gfx_namelink_req {
       opaque pargfid[16];
       string bname<>;
       gfx_dict xdata;
};

/* Define every fops */
/* Changes from Version 3:
  1. Dict has its own type instead of being opaque
  2. Iattx instead of iatt on wire
  3. gfid has 4 extra bytes so it can be used for future
*/
struct gfx_stat_req {
        opaque gfid[16];
        gfx_dict xdata;
};

struct gfx_readlink_req {
        opaque gfid[16];
        unsigned int   size;
        gfx_dict xdata; /* Extra data */
};

struct gfx_readlink_rsp {
        int    op_ret;
        int    op_errno;
        gfx_dict xdata; /* Extra data */
        gfx_iattx buf;
        string path<>; /* NULL terminated */
};

struct gfx_mknod_req {
        opaque  pargfid[16];
        u_quad_t dev;
        unsigned int mode;
        unsigned int umask;
        string     bname<>; /* NULL terminated */
        gfx_dict xdata; /* Extra data */
};

struct  gfx_mkdir_req {
        opaque  pargfid[16];
        unsigned int mode;
        unsigned int umask;
        string     bname<>; /* NULL terminated */
        gfx_dict xdata; /* Extra data */
};

struct gfx_unlink_req {
        opaque  pargfid[16];
        string     bname<>; /* NULL terminated */
        unsigned int xflags;
        gfx_dict xdata; /* Extra data */
};


struct gfx_rmdir_req {
        opaque  pargfid[16];
        int        xflags;
        string     bname<>; /* NULL terminated */
        gfx_dict xdata; /* Extra data */
};

struct gfx_symlink_req {
        opaque  pargfid[16];
        string     bname<>;
        unsigned int umask;
        string     linkname<>;
        gfx_dict xdata; /* Extra data */
};

struct  gfx_rename_req {
        opaque  oldgfid[16];
        opaque  newgfid[16];
        string       oldbname<>; /* NULL terminated */
        string       newbname<>; /* NULL terminated */
        gfx_dict xdata; /* Extra data */
};

struct   gfx_rename_rsp {
        int    op_ret;
        int    op_errno;
        gfx_dict xdata; /* Extra data */
        gfx_iattx stat;
        gfx_iattx preoldparent;
        gfx_iattx postoldparent;
        gfx_iattx prenewparent;
        gfx_iattx postnewparent;
};


 struct  gfx_link_req {
        opaque  oldgfid[16];
        opaque  newgfid[16];
        string       newbname<>;
        gfx_dict xdata; /* Extra data */
};

 struct   gfx_truncate_req {
        opaque gfid[16];
        u_quad_t offset;
        gfx_dict xdata; /* Extra data */
};

 struct   gfx_open_req {
        opaque gfid[16];
        unsigned int flags;
        gfx_dict xdata; /* Extra data */
};

struct   gfx_open_rsp {
        int    op_ret;
        int    op_errno;
        gfx_dict xdata; /* Extra data */
        quad_t fd;
};

struct gfx_opendir_req {
        opaque gfid[16];
        gfx_dict xdata; /* Extra data */
}  ;


 struct   gfx_read_req {
        opaque gfid[16];
        quad_t  fd;
        u_quad_t offset;
        unsigned int size;
        unsigned int flag;
        gfx_dict xdata; /* Extra data */
};
 struct  gfx_read_rsp {
        int    op_ret;
        int    op_errno;
        gfx_iattx stat;
        unsigned int size;
        gfx_dict xdata; /* Extra data */
} ;

struct   gfx_lookup_req {
        opaque gfid[16];
        opaque  pargfid[16];
        unsigned int flags;
        string     bname<>;
        gfx_dict xdata; /* Extra data */
};


 struct   gfx_write_req {
        opaque gfid[16];
        quad_t  fd;
        u_quad_t offset;
        unsigned int size;
        unsigned int flag;
        gfx_dict xdata; /* Extra data */
};

 struct gfx_statfs_req  {
        opaque gfid[16];
        gfx_dict xdata; /* Extra data */
}  ;
 struct gfx_statfs_rsp {
        int    op_ret;
        int    op_errno;
        gfx_dict xdata; /* Extra data */
        gf_statfs statfs;
}  ;

 struct gfx_lk_req {
        opaque gfid[16];
        int64_t         fd;
        unsigned int        cmd;
        unsigned int        type;
        gf_proto_flock flock;
        gfx_dict xdata; /* Extra data */
}  ;
 struct gfx_lk_rsp {
        int    op_ret;
        int    op_errno;
        gfx_dict xdata; /* Extra data */
        gf_proto_flock flock;
}  ;

struct gfx_lease_req {
        opaque gfid[16];
        gf_proto_lease lease;
        gfx_dict xdata; /* Extra data */
}  ;

struct gfx_lease_rsp {
        int    op_ret;
        int    op_errno;
        gfx_dict xdata; /* Extra data */
        gf_proto_lease lease;
}  ;

struct gfx_recall_lease_req {
        opaque       gfid[16];
        unsigned int lease_type;
        opaque       tid[16];
        gfx_dict xdata; /* Extra data */
}  ;

 struct gfx_inodelk_req {
        opaque gfid[16];
        unsigned int cmd;
        unsigned int type;
        gf_proto_flock flock;
        string     volume<>;
        gfx_dict xdata; /* Extra data */
}  ;

struct   gfx_finodelk_req {
        opaque gfid[16];
        quad_t  fd;
        unsigned int cmd;
        unsigned int type;
        gf_proto_flock flock;
        string volume<>;
        gfx_dict xdata; /* Extra data */
} ;


 struct gfx_flush_req {
        opaque gfid[16];
        quad_t  fd;
        gfx_dict xdata; /* Extra data */
}  ;


 struct gfx_fsync_req {
        opaque gfid[16];
        quad_t  fd;
        unsigned int data;
        gfx_dict xdata; /* Extra data */
}  ;

 struct gfx_setxattr_req {
        opaque gfid[16];
        unsigned int flags;
        gfx_dict dict;
        gfx_dict xdata; /* Extra data */
}  ;



 struct gfx_fsetxattr_req {
        opaque gfid[16];
        int64_t  fd;
        unsigned int flags;
        gfx_dict dict;
        gfx_dict xdata; /* Extra data */
}  ;



 struct gfx_xattrop_req {
        opaque gfid[16];
        unsigned int flags;
        gfx_dict dict;
        gfx_dict xdata; /* Extra data */
}  ;

struct gfx_common_dict_rsp  {
        int    op_ret;
        int    op_errno;
        gfx_dict xdata; /* Extra data */
        gfx_dict dict;
        gfx_iattx prestat;
        gfx_iattx poststat;
};


 struct gfx_fxattrop_req {
        opaque gfid[16];
        quad_t  fd;
        unsigned int flags;
        gfx_dict dict;
        gfx_dict xdata; /* Extra data */
}  ;

 struct gfx_getxattr_req  {
        opaque gfid[16];
        unsigned int namelen;
        string     name<>;
        gfx_dict xdata; /* Extra data */
}  ;


 struct gfx_fgetxattr_req  {
        opaque gfid[16];
        quad_t  fd;
        unsigned int namelen;
        string     name<>;
        gfx_dict xdata; /* Extra data */
}  ;

 struct gfx_removexattr_req {
        opaque gfid[16];
        string     name<>;
        gfx_dict xdata; /* Extra data */
}  ;

 struct gfx_fremovexattr_req {
        opaque gfid[16];
        quad_t  fd;
        string     name<>;
        gfx_dict xdata; /* Extra data */
}  ;


 struct gfx_fsyncdir_req {
        opaque gfid[16];
        quad_t  fd;
        int  data;
        gfx_dict xdata; /* Extra data */
}  ;

 struct   gfx_readdir_req  {
        opaque gfid[16];
        quad_t  fd;
        u_quad_t offset;
        unsigned int size;
        gfx_dict xdata; /* Extra data */
};

 struct gfx_readdirp_req {
        opaque gfid[16];
        quad_t  fd;
        u_quad_t offset;
        unsigned int size;
        gfx_dict xdata;
}  ;


struct gfx_access_req  {
        opaque gfid[16];
        unsigned int mask;
        gfx_dict xdata; /* Extra data */
} ;


struct gfx_create_req {
        opaque  pargfid[16];
        unsigned int flags;
        unsigned int mode;
        unsigned int umask;
        string     bname<>;
        gfx_dict xdata; /* Extra data */
}  ;
struct  gfx_create_rsp {
        int    op_ret;
        int    op_errno;
        gfx_dict xdata; /* Extra data */
        gfx_iattx stat;
        u_quad_t       fd;
        gfx_iattx preparent;
        gfx_iattx postparent;
} ;

struct   gfx_ftruncate_req  {
        opaque gfid[16];
        quad_t  fd;
        u_quad_t offset;
        gfx_dict xdata; /* Extra data */
} ;


struct gfx_fstat_req {
        opaque gfid[16];
        quad_t  fd;
        gfx_dict xdata; /* Extra data */
}  ;


struct gfx_entrylk_req {
        opaque gfid[16];
        unsigned int  cmd;
        unsigned int  type;
        u_quad_t  namelen;
        string      name<>;
        string      volume<>;
        gfx_dict xdata; /* Extra data */
};

struct gfx_fentrylk_req {
        opaque gfid[16];
        quad_t   fd;
        unsigned int  cmd;
        unsigned int  type;
        u_quad_t  namelen;
        string      name<>;
        string      volume<>;
        gfx_dict xdata; /* Extra data */
};

 struct gfx_setattr_req {
        opaque gfid[16];
        gfx_iattx stbuf;
        int        valid;
        gfx_dict xdata; /* Extra data */
}  ;

 struct gfx_fallocate_req {
        opaque          gfid[16];
        quad_t          fd;
        unsigned int    flags;
        u_quad_t        offset;
        u_quad_t        size;
        gfx_dict xdata; /* Extra data */
}  ;

struct gfx_discard_req {
        opaque          gfid[16];
        quad_t          fd;
        u_quad_t        offset;
        u_quad_t        size;
        gfx_dict xdata; /* Extra data */
}  ;

struct gfx_zerofill_req {
        opaque          gfid[16];
        quad_t           fd;
        u_quad_t  offset;
        u_quad_t  size;
        gfx_dict xdata;
}  ;

struct gfx_rchecksum_rsp {
        int    op_ret;
        int    op_errno;
        gfx_dict xdata; /* Extra data */
        unsigned int flags;
        unsigned int weak_checksum;
        opaque   strong_checksum<>;
}  ;


struct gfx_ipc_req {
        int     op;
        gfx_dict xdata;
};


struct gfx_seek_req {
        opaque    gfid[16];
        quad_t    fd;
        u_quad_t  offset;
        int       what;
        gfx_dict xdata;
};

struct gfx_seek_rsp {
        int       op_ret;
        int       op_errno;
        gfx_dict xdata;
        u_quad_t  offset;
};


 struct gfx_setvolume_req {
        gfx_dict dict;
}  ;
 struct  gfx_setvolume_rsp {
        int    op_ret;
        int    op_errno;
        gfx_dict dict;
} ;


 struct gfx_getspec_req {
        unsigned int flags;
        string     key<>;
        gfx_dict xdata; /* Extra data */
}  ;
 struct  gfx_getspec_rsp {
        int    op_ret;
        int    op_errno;
        string spec<>;
        gfx_dict xdata; /* Extra data */
} ;


 struct gfx_notify_req {
        unsigned int  flags;
        string buf<>;
        gfx_dict xdata; /* Extra data */
}  ;
 struct gfx_notify_rsp {
        int    op_ret;
        int    op_errno;
        unsigned int  flags;
        string buf<>;
        gfx_dict xdata; /* Extra data */
}  ;

struct gfx_releasedir_req {
        opaque gfid[16];
        quad_t  fd;
        gfx_dict xdata; /* Extra data */
}  ;

struct gfx_release_req {
        opaque gfid[16];
        quad_t  fd;
        gfx_dict xdata; /* Extra data */
}  ;

struct gfx_dirlist {
       u_quad_t d_ino;
       u_quad_t d_off;
       unsigned int d_len;
       unsigned int d_type;
       string name<>;
       gfx_dirlist *nextentry;
};


struct gfx_readdir_rsp {
       int op_ret;
       int op_errno;
       gfx_dict xdata; /* Extra data */
       gfx_dirlist *reply;
};

struct gfx_dirplist {
       u_quad_t d_ino;
       u_quad_t d_off;
       unsigned int d_len;
       unsigned int d_type;
       string name<>;
       gfx_iattx stat;
       gfx_dict dict;
       gfx_dirplist *nextentry;
};

struct gfx_readdirp_rsp {
       int op_ret;
       int op_errno;
       gfx_dict xdata; /* Extra data */
       gfx_dirplist *reply;
};

struct gfx_set_lk_ver_rsp {
       int op_ret;
       int op_errno;
       gfx_dict xdata;
       int lk_ver;
};

struct gfx_set_lk_ver_req {
       string uid<>;
       int lk_ver;
};

struct gfx_event_notify_req {
        int op;
        gfx_dict dict;
};


struct gfx_getsnap_name_uuid_req {
        gfx_dict dict;
};

struct gfx_getsnap_name_uuid_rsp {
        int op_ret;
        int op_errno;
        gfx_dict dict;
        string op_errstr<>;
};

struct gfx_getactivelk_rsp {
        int op_ret;
        int op_errno;
        gfx_dict xdata;
        gfs3_locklist *reply;
};

struct gfx_getactivelk_req {
        opaque gfid[16];
        gfx_dict xdata;
};

struct gfx_setactivelk_req {
        opaque gfid[16];
        gfs3_locklist *request;
        gfx_dict xdata;
};

union compound_req_v2 switch (glusterfs_fop_t fop_enum) {
        case GF_FOP_STAT:         gfx_stat_req compound_stat_req;
        case GF_FOP_READLINK:     gfx_readlink_req compound_readlink_req;
        case GF_FOP_MKNOD:        gfx_mknod_req compound_mknod_req;
        case GF_FOP_MKDIR:        gfx_mkdir_req compound_mkdir_req;
        case GF_FOP_UNLINK:       gfx_unlink_req compound_unlink_req;
        case GF_FOP_RMDIR:        gfx_rmdir_req compound_rmdir_req;
        case GF_FOP_SYMLINK:      gfx_symlink_req compound_symlink_req;
        case GF_FOP_RENAME:       gfx_rename_req compound_rename_req;
        case GF_FOP_LINK:         gfx_link_req compound_link_req;
        case GF_FOP_TRUNCATE:     gfx_truncate_req compound_truncate_req;
        case GF_FOP_OPEN:         gfx_open_req compound_open_req;
        case GF_FOP_READ:         gfx_read_req compound_read_req;
        case GF_FOP_WRITE:        gfx_write_req compound_write_req;
        case GF_FOP_STATFS:       gfx_statfs_req compound_statfs_req;
        case GF_FOP_FLUSH:        gfx_flush_req compound_flush_req;
        case GF_FOP_FSYNC:        gfx_fsync_req compound_fsync_req;
        case GF_FOP_GETXATTR:     gfx_getxattr_req compound_getxattr_req;
        case GF_FOP_SETXATTR:     gfx_setxattr_req compound_setxattr_req;
        case GF_FOP_REMOVEXATTR:  gfx_removexattr_req compound_removexattr_req;
        case GF_FOP_OPENDIR:      gfx_opendir_req compound_opendir_req;
        case GF_FOP_FSYNCDIR:     gfx_fsyncdir_req compound_fsyncdir_req;
        case GF_FOP_ACCESS:       gfx_access_req compound_access_req;
        case GF_FOP_CREATE:       gfx_create_req compound_create_req;
        case GF_FOP_FTRUNCATE:    gfx_ftruncate_req compound_ftruncate_req;
        case GF_FOP_FSTAT:        gfx_fstat_req compound_fstat_req;
        case GF_FOP_LK:           gfx_lk_req compound_lk_req;
        case GF_FOP_LOOKUP:       gfx_lookup_req compound_lookup_req;
        case GF_FOP_READDIR:      gfx_readdir_req compound_readdir_req;
        case GF_FOP_INODELK:      gfx_inodelk_req compound_inodelk_req;
        case GF_FOP_FINODELK:     gfx_finodelk_req compound_finodelk_req;
        case GF_FOP_ENTRYLK:      gfx_entrylk_req compound_entrylk_req;
        case GF_FOP_FENTRYLK:     gfx_fentrylk_req compound_fentrylk_req;
        case GF_FOP_XATTROP:      gfx_xattrop_req compound_xattrop_req;
        case GF_FOP_FXATTROP:     gfx_fxattrop_req compound_fxattrop_req;
        case GF_FOP_FGETXATTR:    gfx_fgetxattr_req compound_fgetxattr_req;
        case GF_FOP_FSETXATTR:    gfx_fsetxattr_req compound_fsetxattr_req;
        case GF_FOP_RCHECKSUM:    gfx_rchecksum_req compound_rchecksum_req;
        case GF_FOP_SETATTR:      gfx_setattr_req compound_setattr_req;
        case GF_FOP_FSETATTR:     gfx_fsetattr_req compound_fsetattr_req;
        case GF_FOP_READDIRP:     gfx_readdirp_req compound_readdirp_req;
        case GF_FOP_RELEASE:      gfx_release_req compound_release_req;
        case GF_FOP_RELEASEDIR:   gfx_releasedir_req compound_releasedir_req;
        case GF_FOP_FREMOVEXATTR: gfx_fremovexattr_req compound_fremovexattr_req;
        case GF_FOP_FALLOCATE:    gfx_fallocate_req compound_fallocate_req;
        case GF_FOP_DISCARD:      gfx_discard_req compound_discard_req;
        case GF_FOP_ZEROFILL:     gfx_zerofill_req compound_zerofill_req;
        case GF_FOP_IPC:          gfx_ipc_req compound_ipc_req;
        case GF_FOP_SEEK:         gfx_seek_req compound_seek_req;
        case GF_FOP_LEASE:         gfx_lease_req compound_lease_req;
        default:                  void;
};

struct gfx_compound_req {
        int                       compound_version;
        glusterfs_compound_fop_t  compound_fop_enum;
        compound_req_v2           compound_req_array<>;
        gfx_dict                  xdata;
};

union compound_rsp_v2 switch (glusterfs_fop_t fop_enum) {
        case GF_FOP_STAT:         gfx_common_iatt_rsp compound_stat_rsp;
        case GF_FOP_READLINK:     gfx_readlink_rsp compound_readlink_rsp;
        case GF_FOP_MKNOD:        gfx_common_3iatt_rsp compound_mknod_rsp;
        case GF_FOP_MKDIR:        gfx_common_3iatt_rsp compound_mkdir_rsp;
        case GF_FOP_UNLINK:       gfx_common_2iatt_rsp compound_unlink_rsp;
        case GF_FOP_RMDIR:        gfx_common_2iatt_rsp compound_rmdir_rsp;
        case GF_FOP_SYMLINK:      gfx_common_3iatt_rsp compound_symlink_rsp;
        case GF_FOP_RENAME:       gfx_rename_rsp compound_rename_rsp;
        case GF_FOP_LINK:         gfx_common_3iatt_rsp compound_link_rsp;
        case GF_FOP_TRUNCATE:     gfx_common_2iatt_rsp compound_truncate_rsp;
        case GF_FOP_OPEN:         gfx_open_rsp compound_open_rsp;
        case GF_FOP_READ:         gfx_read_rsp compound_read_rsp;
        case GF_FOP_WRITE:        gfx_common_2iatt_rsp compound_write_rsp;
        case GF_FOP_STATFS:       gfx_statfs_rsp compound_statfs_rsp;
        case GF_FOP_FLUSH:        gfx_common_rsp compound_flush_rsp;
        case GF_FOP_FSYNC:        gfx_common_2iatt_rsp compound_fsync_rsp;
        case GF_FOP_GETXATTR:     gfx_common_dict_rsp compound_getxattr_rsp;
        case GF_FOP_SETXATTR:     gfx_common_rsp compound_setxattr_rsp;
        case GF_FOP_REMOVEXATTR:  gfx_common_rsp compound_removexattr_rsp;
        case GF_FOP_OPENDIR:      gfx_open_rsp compound_opendir_rsp;
        case GF_FOP_FSYNCDIR:     gfx_common_rsp compound_fsyncdir_rsp;
        case GF_FOP_ACCESS:       gfx_common_rsp compound_access_rsp;
        case GF_FOP_CREATE:       gfx_create_rsp compound_create_rsp;
        case GF_FOP_FTRUNCATE:    gfx_common_2iatt_rsp compound_ftruncate_rsp;
        case GF_FOP_FSTAT:        gfx_common_iatt_rsp compound_fstat_rsp;
        case GF_FOP_LK:           gfx_lk_rsp compound_lk_rsp;
        case GF_FOP_LOOKUP:       gfx_common_2iatt_rsp compound_lookup_rsp;
        case GF_FOP_READDIR:      gfx_readdir_rsp compound_readdir_rsp;
        case GF_FOP_INODELK:      gfx_common_rsp compound_inodelk_rsp;
        case GF_FOP_FINODELK:     gfx_common_rsp compound_finodelk_rsp;
        case GF_FOP_ENTRYLK:      gfx_common_rsp compound_entrylk_rsp;
        case GF_FOP_FENTRYLK:     gfx_common_rsp compound_fentrylk_rsp;
        case GF_FOP_XATTROP:      gfx_common_dict_rsp compound_xattrop_rsp;
        case GF_FOP_FXATTROP:     gfx_common_dict_rsp compound_fxattrop_rsp;
        case GF_FOP_FGETXATTR:    gfx_common_dict_rsp compound_fgetxattr_rsp;
        case GF_FOP_FSETXATTR:    gfx_common_rsp compound_fsetxattr_rsp;
        case GF_FOP_RCHECKSUM:    gfx_rchecksum_rsp compound_rchecksum_rsp;
        case GF_FOP_SETATTR:      gfx_common_2iatt_rsp compound_setattr_rsp;
        case GF_FOP_FSETATTR:     gfx_common_2iatt_rsp compound_fsetattr_rsp;
        case GF_FOP_READDIRP:     gfx_readdirp_rsp compound_readdirp_rsp;
        case GF_FOP_RELEASE:      gfx_common_rsp compound_release_rsp;
        case GF_FOP_RELEASEDIR:   gfx_common_rsp compound_releasedir_rsp;
        case GF_FOP_FREMOVEXATTR: gfx_common_rsp compound_fremovexattr_rsp;
        case GF_FOP_FALLOCATE:    gfx_common_2iatt_rsp compound_fallocate_rsp;
        case GF_FOP_DISCARD:      gfx_common_2iatt_rsp compound_discard_rsp;
        case GF_FOP_ZEROFILL:     gfx_common_2iatt_rsp compound_zerofill_rsp;
        case GF_FOP_IPC:          gfx_common_rsp compound_ipc_rsp;
        case GF_FOP_SEEK:         gfx_seek_rsp compound_seek_rsp;
        case GF_FOP_LEASE:        gfx_lease_rsp compound_lease_rsp;
        default:                  void;
};

struct gfx_compound_rsp {
        int           op_ret;
        int           op_errno;
        compound_rsp_v2  compound_rsp_array<>;
        gfx_dict      xdata;
};

struct gfs4_inodelk_contention_req {
        opaque                gfid[16];
        struct gf_proto_flock flock;
        unsigned int          pid;
        string                domain<>;
        opaque                xdata<>;
};

struct gfs4_entrylk_contention_req {
        opaque                gfid[16];
        unsigned int          type;
        unsigned int          pid;
        string                name<>;
        string                domain<>;
        opaque                xdata<>;
};
