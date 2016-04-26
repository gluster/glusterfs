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
%#include "rpc-common-xdr.h"
%#include "glusterfs-fops.h"

#define GF_REQUEST_MAXGROUPS    16
struct gf_statfs {
	u_quad_t bsize;
	u_quad_t frsize;
	u_quad_t blocks;
	u_quad_t bfree;
	u_quad_t bavail;
	u_quad_t files;
	u_quad_t ffree;
	u_quad_t favail;
	u_quad_t fsid;
	u_quad_t flag;
	u_quad_t namemax;
};


struct gf_proto_flock {
	unsigned int   type;
	unsigned int   whence;
	u_quad_t start;
	u_quad_t len;
        unsigned int   pid;
        opaque         lk_owner<>;
} ;

struct gf_proto_lease {
        unsigned int   cmd;
        unsigned int   lease_type;
        opaque         lease_id[16];
        unsigned int   lease_flags;
} ;

struct gf_iatt {
        opaque             ia_gfid[16];
        u_quad_t     ia_ino;        /* inode number */
        u_quad_t     ia_dev;        /* backing device ID */
        unsigned int       mode;          /* mode (type + protection )*/
        unsigned int       ia_nlink;      /* Link count */
        unsigned int       ia_uid;        /* user ID of owner */
        unsigned int       ia_gid;        /* group ID of owner */
        u_quad_t     ia_rdev;       /* device ID (if special file) */
        u_quad_t     ia_size;       /* file size in bytes */
        unsigned int       ia_blksize;    /* blocksize for filesystem I/O */
        u_quad_t     ia_blocks;     /* number of 512B blocks allocated */
        unsigned int       ia_atime;      /* last access time */
        unsigned int       ia_atime_nsec;
        unsigned int       ia_mtime;      /* last modification time */
        unsigned int       ia_mtime_nsec;
        unsigned int       ia_ctime;      /* last status change time */
        unsigned int       ia_ctime_nsec;
};


struct gfs3_cbk_cache_invalidation_req {
        string         gfid<>;
        unsigned int   event_type; /* Upcall event type */
        unsigned int   flags;  /* or mask of events incase of inotify */
        unsigned int   expire_time_attr; /* the amount of time which client
                                          * can cache this entry */
        struct gf_iatt stat;  /* Updated/current stat of the file/dir */
        struct gf_iatt parent_stat;  /* Updated stat of the parent dir
                                      * needed in case of create, mkdir,
                                      * unlink, rmdir, rename fops */
        struct gf_iatt oldparent_stat;  /* Updated stat of the oldparent dir
                                           needed in case of rename fop */
        opaque   xdata<>; /* Extra data */
};

struct gfs3_stat_req {
        opaque gfid[16];
        opaque   xdata<>; /* Extra data */
};
struct gfs3_stat_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
        opaque   xdata<>; /* Extra data */
} ;


struct gfs3_readlink_req {
        opaque gfid[16];
	unsigned int   size;
        opaque   xdata<>; /* Extra data */
}  ;
 struct gfs3_readlink_rsp {
        int    op_ret;
        int    op_errno;
        struct gf_iatt buf;
        string      path<>; /* NULL terminated */
        opaque   xdata<>; /* Extra data */
} ;


 struct gfs3_mknod_req {
        opaque  pargfid[16];
	u_quad_t dev;
	unsigned int mode;
        unsigned int umask;
	string     bname<>; /* NULL terminated */
        opaque   xdata<>; /* Extra data */
} ;
 struct gfs3_mknod_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
        struct gf_iatt preparent;
        struct gf_iatt postparent;
        opaque   xdata<>; /* Extra data */
};


 struct  gfs3_mkdir_req {
        opaque  pargfid[16];
	unsigned int mode;
        unsigned int umask;
	string     bname<>; /* NULL terminated */
        opaque     xdata<>; /* Extra data */
} ;
 struct  gfs3_mkdir_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
        struct gf_iatt preparent;
        struct gf_iatt postparent;
        opaque   xdata<>; /* Extra data */
} ;


 struct   gfs3_unlink_req {
        opaque  pargfid[16];
	string     bname<>; /* NULL terminated */
        unsigned int xflags;
        opaque   xdata<>; /* Extra data */
};
 struct   gfs3_unlink_rsp {
        int    op_ret;
        int    op_errno;
        struct gf_iatt preparent;
        struct gf_iatt postparent;
        opaque   xdata<>; /* Extra data */
};


 struct   gfs3_rmdir_req {
        opaque  pargfid[16];
        int        xflags;
	string     bname<>; /* NULL terminated */
        opaque     xdata<>; /* Extra data */
};
 struct   gfs3_rmdir_rsp {
        int    op_ret;
        int    op_errno;
        struct gf_iatt preparent;
        struct gf_iatt postparent;
        opaque   xdata<>; /* Extra data */
};


 struct   gfs3_symlink_req {
        opaque  pargfid[16];
	string     bname<>;
        unsigned int umask;
	string     linkname<>;
        opaque   xdata<>; /* Extra data */
};
 struct  gfs3_symlink_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
        struct gf_iatt preparent;
        struct gf_iatt postparent;
        opaque   xdata<>; /* Extra data */
};


 struct   gfs3_rename_req {
        opaque  oldgfid[16];
        opaque  newgfid[16];
	string       oldbname<>; /* NULL terminated */
	string       newbname<>; /* NULL terminated */
        opaque   xdata<>; /* Extra data */
};
 struct   gfs3_rename_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
        struct gf_iatt preoldparent;
        struct gf_iatt postoldparent;
        struct gf_iatt prenewparent;
        struct gf_iatt postnewparent;
        opaque   xdata<>; /* Extra data */
};


 struct  gfs3_link_req {
        opaque  oldgfid[16];
        opaque  newgfid[16];
	string       newbname<>;
        opaque   xdata<>; /* Extra data */
};
 struct   gfs3_link_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
        struct gf_iatt preparent;
        struct gf_iatt postparent;
        opaque   xdata<>; /* Extra data */
};

 struct   gfs3_truncate_req {
        opaque gfid[16];
	u_quad_t offset;
        opaque   xdata<>; /* Extra data */
};
 struct   gfs3_truncate_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt prestat;
        struct gf_iatt poststat;
        opaque   xdata<>; /* Extra data */
};


 struct   gfs3_open_req {
        opaque gfid[16];
	unsigned int flags;
        opaque   xdata<>; /* Extra data */
};
 struct   gfs3_open_rsp {
        int    op_ret;
        int    op_errno;
	quad_t fd;
        opaque   xdata<>; /* Extra data */
};


 struct   gfs3_read_req {
        opaque gfid[16];
	quad_t  fd;
	u_quad_t offset;
	unsigned int size;
        unsigned int flag;
        opaque   xdata<>; /* Extra data */
};
 struct  gfs3_read_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
        unsigned int size;
        opaque   xdata<>; /* Extra data */
} ;

struct   gfs3_lookup_req {
        opaque gfid[16];
        opaque  pargfid[16];
	unsigned int flags;
	string     bname<>;
        opaque   xdata<>; /* Extra data */
};
 struct   gfs3_lookup_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
        struct gf_iatt postparent;
        opaque   xdata<>; /* Extra data */
} ;



 struct   gfs3_write_req {
        opaque gfid[16];
	quad_t  fd;
	u_quad_t offset;
	unsigned int size;
        unsigned int flag;
        opaque   xdata<>; /* Extra data */
};
 struct gfs3_write_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt prestat;
        struct gf_iatt poststat;
        opaque   xdata<>; /* Extra data */
} ;


 struct gfs3_statfs_req  {
        opaque gfid[16];
        opaque   xdata<>; /* Extra data */
}  ;
 struct gfs3_statfs_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_statfs statfs;
        opaque   xdata<>; /* Extra data */
}  ;

 struct gfs3_lk_req {
        opaque gfid[16];
	int64_t         fd;
	unsigned int        cmd;
	unsigned int        type;
	struct gf_proto_flock flock;
        opaque   xdata<>; /* Extra data */
}  ;
 struct gfs3_lk_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_proto_flock flock;
        opaque   xdata<>; /* Extra data */
}  ;

struct gfs3_lease_req {
        opaque gfid[16];
        struct gf_proto_lease lease;
        opaque   xdata<>; /* Extra data */
}  ;

struct gfs3_lease_rsp {
        int    op_ret;
        int    op_errno;
        struct gf_proto_lease lease;
        opaque   xdata<>; /* Extra data */
}  ;

struct gfs3_recall_lease_req {
        opaque       gfid[16];
        unsigned int lease_type;
        opaque       tid[16];
        opaque       xdata<>; /* Extra data */
}  ;

 struct gfs3_inodelk_req {
        opaque gfid[16];
	unsigned int cmd;
	unsigned int type;
	struct gf_proto_flock flock;
        string     volume<>;
        opaque   xdata<>; /* Extra data */
}  ;

struct   gfs3_finodelk_req {
        opaque gfid[16];
	quad_t  fd;
	unsigned int cmd;
	unsigned int type;
	struct gf_proto_flock flock;
        string volume<>;
        opaque   xdata<>; /* Extra data */
} ;


 struct gfs3_flush_req {
        opaque gfid[16];
	quad_t  fd;
        opaque   xdata<>; /* Extra data */
}  ;


 struct gfs3_fsync_req {
        opaque gfid[16];
	quad_t  fd;
	unsigned int data;
        opaque   xdata<>; /* Extra data */
}  ;
 struct gfs3_fsync_rsp {
        int    op_ret;
        int    op_errno;
        struct gf_iatt prestat;
        struct gf_iatt poststat;
        opaque   xdata<>; /* Extra data */
}  ;


 struct gfs3_setxattr_req {
        opaque gfid[16];
	unsigned int flags;
        opaque     dict<>;
        opaque   xdata<>; /* Extra data */
}  ;



 struct gfs3_fsetxattr_req {
        opaque gfid[16];
	int64_t  fd;
	unsigned int flags;
        opaque     dict<>;
        opaque   xdata<>; /* Extra data */
}  ;



 struct gfs3_xattrop_req {
        opaque gfid[16];
	unsigned int flags;
        opaque     dict<>;
        opaque   xdata<>; /* Extra data */
}  ;

 struct gfs3_xattrop_rsp  {
        int    op_ret;
        int    op_errno;
	opaque  dict<>;
        opaque   xdata<>; /* Extra data */
}  ;


 struct gfs3_fxattrop_req {
        opaque gfid[16];
	quad_t  fd;
	unsigned int flags;
	opaque     dict<>;
        opaque   xdata<>; /* Extra data */
}  ;

 struct gfs3_fxattrop_rsp  {
        int    op_ret;
        int    op_errno;
	opaque  dict<>;
        opaque   xdata<>; /* Extra data */
}  ;


 struct gfs3_getxattr_req  {
        opaque gfid[16];
	unsigned int namelen;
	string     name<>;
        opaque   xdata<>; /* Extra data */
}  ;
 struct gfs3_getxattr_rsp {
        int    op_ret;
        int    op_errno;
	opaque     dict<>;
        opaque   xdata<>; /* Extra data */
}  ;


 struct gfs3_fgetxattr_req  {
        opaque gfid[16];
	quad_t  fd;
        unsigned int namelen;
	string     name<>;
        opaque   xdata<>; /* Extra data */
}  ;
 struct gfs3_fgetxattr_rsp {
        int    op_ret;
        int    op_errno;
        opaque     dict<>;
        opaque   xdata<>; /* Extra data */
}  ;


 struct gfs3_removexattr_req {
        opaque gfid[16];
	string     name<>;
        opaque   xdata<>; /* Extra data */
}  ;

 struct gfs3_fremovexattr_req {
        opaque gfid[16];
        quad_t  fd;
	string     name<>;
        opaque   xdata<>; /* Extra data */
}  ;



 struct gfs3_opendir_req {
        opaque gfid[16];
        opaque   xdata<>; /* Extra data */
}  ;
 struct gfs3_opendir_rsp {
        int    op_ret;
        int    op_errno;
	quad_t fd;
        opaque   xdata<>; /* Extra data */
}  ;


 struct gfs3_fsyncdir_req {
        opaque gfid[16];
	quad_t  fd;
	int  data;
        opaque   xdata<>; /* Extra data */
}  ;

 struct   gfs3_readdir_req  {
        opaque gfid[16];
	quad_t  fd;
	u_quad_t offset;
	unsigned int size;
        opaque   xdata<>; /* Extra data */
};

 struct gfs3_readdirp_req {
        opaque gfid[16];
	quad_t  fd;
	u_quad_t offset;
	unsigned int size;
        opaque dict<>;
}  ;


struct gfs3_access_req  {
        opaque gfid[16];
	unsigned int mask;
        opaque   xdata<>; /* Extra data */
} ;


struct gfs3_create_req {
        opaque  pargfid[16];
	unsigned int flags;
	unsigned int mode;
	unsigned int umask;
	string     bname<>;
        opaque   xdata<>; /* Extra data */
}  ;
struct  gfs3_create_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
	u_quad_t       fd;
        struct gf_iatt preparent;
        struct gf_iatt postparent;
        opaque   xdata<>; /* Extra data */
} ;



struct   gfs3_ftruncate_req  {
        opaque gfid[16];
	quad_t  fd;
	u_quad_t offset;
        opaque   xdata<>; /* Extra data */
} ;
struct   gfs3_ftruncate_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt prestat;
        struct gf_iatt poststat;
        opaque   xdata<>; /* Extra data */
} ;


struct gfs3_fstat_req {
        opaque gfid[16];
	quad_t  fd;
        opaque   xdata<>; /* Extra data */
}  ;
 struct gfs3_fstat_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
        opaque   xdata<>; /* Extra data */
}  ;



 struct   gfs3_entrylk_req {
        opaque gfid[16];
	unsigned int  cmd;
	unsigned int  type;
	u_quad_t  namelen;
	string      name<>;
        string      volume<>;
        opaque   xdata<>; /* Extra data */
};

 struct   gfs3_fentrylk_req {
        opaque gfid[16];
	quad_t   fd;
	unsigned int  cmd;
	unsigned int  type;
	u_quad_t  namelen;
	string      name<>;
        string      volume<>;
        opaque   xdata<>; /* Extra data */
};


 struct gfs3_setattr_req {
        opaque gfid[16];
        struct gf_iatt stbuf;
        int        valid;
        opaque   xdata<>; /* Extra data */
}  ;
 struct gfs3_setattr_rsp {
        int    op_ret;
        int    op_errno;
        struct gf_iatt statpre;
        struct gf_iatt statpost;
        opaque   xdata<>; /* Extra data */
}  ;

 struct gfs3_fsetattr_req {
        quad_t        fd;
        struct gf_iatt stbuf;
        int        valid;
        opaque   xdata<>; /* Extra data */
}  ;
 struct gfs3_fsetattr_rsp {
        int    op_ret;
        int    op_errno;
        struct gf_iatt statpre;
        struct gf_iatt statpost;
        opaque   xdata<>; /* Extra data */
}  ;

 struct gfs3_fallocate_req {
	opaque		gfid[16];
        quad_t		fd;
	unsigned int	flags;
	u_quad_t	offset;
	u_quad_t	size;
        opaque   xdata<>; /* Extra data */
}  ;

 struct gfs3_fallocate_rsp {
        int    op_ret;
        int    op_errno;
        struct gf_iatt statpre;
        struct gf_iatt statpost;
        opaque   xdata<>; /* Extra data */
}  ;

 struct gfs3_discard_req {
	opaque		gfid[16];
        quad_t		fd;
	u_quad_t	offset;
	u_quad_t	size;
        opaque   xdata<>; /* Extra data */
}  ;

 struct gfs3_discard_rsp {
        int    op_ret;
        int    op_errno;
        struct gf_iatt statpre;
        struct gf_iatt statpost;
        opaque   xdata<>; /* Extra data */
}  ;

 struct gfs3_zerofill_req {
        opaque          gfid[16];
        quad_t           fd;
        u_quad_t  offset;
        u_quad_t  size;
        opaque   xdata<>;
}  ;

 struct gfs3_zerofill_rsp {
        int    op_ret;
        int    op_errno;
        struct gf_iatt statpre;
        struct gf_iatt statpost;
        opaque   xdata<>;
}  ;


 struct gfs3_rchecksum_req {
        quad_t   fd;
        u_quad_t  offset;
        unsigned int  len;
        opaque   xdata<>; /* Extra data */
}  ;
 struct gfs3_rchecksum_rsp {
        int    op_ret;
        int    op_errno;
        unsigned int weak_checksum;
        opaque   strong_checksum<>;
        opaque   xdata<>; /* Extra data */
}  ;


struct gfs3_ipc_req {
	int     op;
	opaque  xdata<>;
};

struct gfs3_ipc_rsp {
	int     op_ret;
	int     op_errno;
	opaque  xdata<>;
};


struct gfs3_seek_req {
        opaque    gfid[16];
        quad_t    fd;
        u_quad_t  offset;
        int       what;
        opaque    xdata<>;
};

struct gfs3_seek_rsp {
        int       op_ret;
        int       op_errno;
        u_quad_t  offset;
        opaque    xdata<>;
};


 struct gf_setvolume_req {
        opaque dict<>;
}  ;
 struct  gf_setvolume_rsp {
        int    op_ret;
        int    op_errno;
        opaque dict<>;
} ;


 struct gf_getspec_req {
	unsigned int flags;
	string     key<>;
        opaque   xdata<>; /* Extra data */
}  ;
 struct  gf_getspec_rsp {
        int    op_ret;
        int    op_errno;
	string spec<>;
        opaque   xdata<>; /* Extra data */
} ;

 struct gf_get_volume_info_req {
        opaque   dict<>; /* Extra data */
}  ;
 struct  gf_get_volume_info_rsp {
        int    op_ret;
        int    op_errno;
        string op_errstr<>;
        opaque   dict<>; /* Extra data */
} ;

 struct gf_mgmt_hndsk_req {
        opaque   hndsk<>;
}  ;

 struct  gf_mgmt_hndsk_rsp {
        int    op_ret;
        int    op_errno;
        opaque   hndsk<>;
} ;

 struct   gf_log_req {
        opaque    msg<>;
} ;

 struct gf_notify_req {
	unsigned int  flags;
        string buf<>;
        opaque   xdata<>; /* Extra data */
}  ;
 struct gf_notify_rsp {
        int    op_ret;
        int    op_errno;
	unsigned int  flags;
        string buf<>;
        opaque   xdata<>; /* Extra data */
}  ;

struct gfs3_releasedir_req {
        opaque gfid[16];
	quad_t  fd;
        opaque   xdata<>; /* Extra data */
}  ;

struct gfs3_release_req {
        opaque gfid[16];
	quad_t  fd;
        opaque   xdata<>; /* Extra data */
}  ;

struct gfs3_dirlist {
       u_quad_t d_ino;
       u_quad_t d_off;
       unsigned int d_len;
       unsigned int d_type;
       string name<>;
       struct gfs3_dirlist *nextentry;
};


struct gfs3_readdir_rsp {
       int op_ret;
       int op_errno;
       struct gfs3_dirlist *reply;
        opaque   xdata<>; /* Extra data */
};

struct gfs3_dirplist {
       u_quad_t d_ino;
       u_quad_t d_off;
       unsigned int d_len;
       unsigned int d_type;
       string name<>;
       struct gf_iatt stat;
       opaque dict<>;
       struct gfs3_dirplist *nextentry;
};

struct gfs3_readdirp_rsp {
       int op_ret;
       int op_errno;
       struct gfs3_dirplist *reply;
        opaque   xdata<>; /* Extra data */
};

struct gf_set_lk_ver_rsp {
       int op_ret;
       int op_errno;
       int lk_ver;
};

struct gf_set_lk_ver_req {
       string uid<>;
       int lk_ver;
};

struct gf_event_notify_req {
	int op;
	opaque dict<>;
};

struct gf_event_notify_rsp {
	int op_ret;
	int op_errno;
	opaque dict<>;
};


struct gf_getsnap_name_uuid_req {
        opaque dict<>;
};

struct gf_getsnap_name_uuid_rsp {
        int op_ret;
        int op_errno;
        string op_errstr<>;
        opaque dict<>;
};

union compound_req switch (glusterfs_fop_t fop_enum) {
        case GF_FOP_STAT:         gfs3_stat_req compound_stat_req;
        case GF_FOP_READLINK:     gfs3_readlink_req compound_readlink_req;
        case GF_FOP_MKNOD:        gfs3_mknod_req compound_mknod_req;
        case GF_FOP_MKDIR:        gfs3_mkdir_req compound_mkdir_req;
        case GF_FOP_UNLINK:       gfs3_unlink_req compound_unlink_req;
        case GF_FOP_RMDIR:        gfs3_rmdir_req compound_rmdir_req;
        case GF_FOP_SYMLINK:      gfs3_symlink_req compound_symlink_req;
        case GF_FOP_RENAME:       gfs3_rename_req compound_rename_req;
        case GF_FOP_LINK:         gfs3_link_req compound_link_req;
        case GF_FOP_TRUNCATE:     gfs3_truncate_req compound_truncate_req;
        case GF_FOP_OPEN:         gfs3_open_req compound_open_req;
        case GF_FOP_READ:         gfs3_read_req compound_read_req;
        case GF_FOP_WRITE:        gfs3_write_req compound_write_req;
        case GF_FOP_STATFS:       gfs3_statfs_req compound_statfs_req;
        case GF_FOP_FLUSH:        gfs3_flush_req compound_flush_req;
        case GF_FOP_FSYNC:        gfs3_fsync_req compound_fsync_req;
        case GF_FOP_GETXATTR:     gfs3_getxattr_req compound_getxattr_req;
        case GF_FOP_SETXATTR:     gfs3_setxattr_req compound_setxattr_req;
        case GF_FOP_REMOVEXATTR:  gfs3_removexattr_req compound_removexattr_req;
        case GF_FOP_OPENDIR:      gfs3_opendir_req compound_opendir_req;
        case GF_FOP_FSYNCDIR:     gfs3_fsyncdir_req compound_fsyncdir_req;
        case GF_FOP_ACCESS:       gfs3_access_req compound_access_req;
        case GF_FOP_CREATE:       gfs3_create_req compound_create_req;
        case GF_FOP_FTRUNCATE:    gfs3_ftruncate_req compound_ftruncate_req;
        case GF_FOP_FSTAT:        gfs3_fstat_req compound_fstat_req;
        case GF_FOP_LK:           gfs3_lk_req compound_lk_req;
        case GF_FOP_LOOKUP:       gfs3_lookup_req compound_lookup_req;
        case GF_FOP_READDIR:      gfs3_readdir_req compound_readdir_req;
        case GF_FOP_INODELK:      gfs3_inodelk_req compound_inodelk_req;
        case GF_FOP_FINODELK:     gfs3_finodelk_req compound_finodelk_req;
        case GF_FOP_ENTRYLK:      gfs3_entrylk_req compound_entrylk_req;
        case GF_FOP_FENTRYLK:     gfs3_fentrylk_req compound_fentrylk_req;
        case GF_FOP_XATTROP:      gfs3_xattrop_req compound_xattrop_req;
        case GF_FOP_FXATTROP:     gfs3_fxattrop_req compound_fxattrop_req;
        case GF_FOP_FGETXATTR:    gfs3_fgetxattr_req compound_fgetxattr_req;
        case GF_FOP_FSETXATTR:    gfs3_fsetxattr_req compound_fsetxattr_req;
        case GF_FOP_RCHECKSUM:    gfs3_rchecksum_req compound_rchecksum_req;
        case GF_FOP_SETATTR:      gfs3_setattr_req compound_setattr_req;
        case GF_FOP_FSETATTR:     gfs3_fsetattr_req compound_fsetattr_req;
        case GF_FOP_READDIRP:     gfs3_readdirp_req compound_readdirp_req;
        case GF_FOP_RELEASE:      gfs3_release_req compound_release_req;
        case GF_FOP_RELEASEDIR:   gfs3_releasedir_req compound_releasedir_req;
        case GF_FOP_FREMOVEXATTR: gfs3_fremovexattr_req compound_fremovexattr_req;
        case GF_FOP_FALLOCATE:    gfs3_fallocate_req compound_fallocate_req;
        case GF_FOP_DISCARD:      gfs3_discard_req compound_discard_req;
        case GF_FOP_ZEROFILL:     gfs3_zerofill_req compound_zerofill_req;
        case GF_FOP_IPC:          gfs3_ipc_req compound_ipc_req;
        case GF_FOP_SEEK:         gfs3_seek_req compound_seek_req;
        case GF_FOP_LEASE:         gfs3_lease_req compound_lease_req;
        default:                  void;
};

struct gfs3_compound_req {
        int                       compound_version;
        glusterfs_compound_fop_t  compound_fop_enum;
        compound_req              compound_req_array<>;
        opaque                    xdata<>;
};

union compound_rsp switch (glusterfs_fop_t fop_enum) {
        case GF_FOP_STAT:         gfs3_stat_rsp compound_stat_rsp;
        case GF_FOP_READLINK:     gfs3_readlink_rsp compound_readlink_rsp;
        case GF_FOP_MKNOD:        gfs3_mknod_rsp compound_mknod_rsp;
        case GF_FOP_MKDIR:        gfs3_mkdir_rsp compound_mkdir_rsp;
        case GF_FOP_UNLINK:       gfs3_unlink_rsp compound_unlink_rsp;
        case GF_FOP_RMDIR:        gfs3_rmdir_rsp compound_rmdir_rsp;
        case GF_FOP_SYMLINK:      gfs3_symlink_rsp compound_symlink_rsp;
        case GF_FOP_RENAME:       gfs3_rename_rsp compound_rename_rsp;
        case GF_FOP_LINK:         gfs3_link_rsp compound_link_rsp;
        case GF_FOP_TRUNCATE:     gfs3_truncate_rsp compound_truncate_rsp;
        case GF_FOP_OPEN:         gfs3_open_rsp compound_open_rsp;
        case GF_FOP_READ:         gfs3_read_rsp compound_read_rsp;
        case GF_FOP_WRITE:        gfs3_write_rsp compound_write_rsp;
        case GF_FOP_STATFS:       gfs3_statfs_rsp compound_statfs_rsp;
        case GF_FOP_FLUSH:        gf_common_rsp compound_flush_rsp;
        case GF_FOP_FSYNC:        gfs3_fsync_rsp compound_fsync_rsp;
        case GF_FOP_GETXATTR:     gfs3_getxattr_rsp compound_getxattr_rsp;
        case GF_FOP_SETXATTR:     gf_common_rsp compound_setxattr_rsp;
        case GF_FOP_REMOVEXATTR:  gf_common_rsp compound_removexattr_rsp;
        case GF_FOP_OPENDIR:      gfs3_opendir_rsp compound_opendir_rsp;
        case GF_FOP_FSYNCDIR:     gf_common_rsp compound_fsyncdir_rsp;
        case GF_FOP_ACCESS:       gf_common_rsp compound_access_rsp;
        case GF_FOP_CREATE:       gfs3_create_rsp compound_create_rsp;
        case GF_FOP_FTRUNCATE:    gfs3_ftruncate_rsp compound_ftruncate_rsp;
        case GF_FOP_FSTAT:        gfs3_fstat_rsp compound_fstat_rsp;
        case GF_FOP_LK:           gfs3_lk_rsp compound_lk_rsp;
        case GF_FOP_LOOKUP:       gfs3_lookup_rsp compound_lookup_rsp;
        case GF_FOP_READDIR:      gfs3_readdir_rsp compound_readdir_rsp;
        case GF_FOP_INODELK:      gf_common_rsp compound_inodelk_rsp;
        case GF_FOP_FINODELK:     gf_common_rsp compound_finodelk_rsp;
        case GF_FOP_ENTRYLK:      gf_common_rsp compound_entrylk_rsp;
        case GF_FOP_FENTRYLK:     gf_common_rsp compound_fentrylk_rsp;
        case GF_FOP_XATTROP:      gfs3_xattrop_rsp compound_xattrop_rsp;
        case GF_FOP_FXATTROP:     gfs3_fxattrop_rsp compound_fxattrop_rsp;
        case GF_FOP_FGETXATTR:    gfs3_fgetxattr_rsp compound_fgetxattr_rsp;
        case GF_FOP_FSETXATTR:    gf_common_rsp compound_fsetxattr_rsp;
        case GF_FOP_RCHECKSUM:    gfs3_rchecksum_rsp compound_rchecksum_rsp;
        case GF_FOP_SETATTR:      gfs3_setattr_rsp compound_setattr_rsp;
        case GF_FOP_FSETATTR:     gfs3_fsetattr_rsp compound_fsetattr_rsp;
        case GF_FOP_READDIRP:     gfs3_readdirp_rsp compound_readdirp_rsp;
        case GF_FOP_RELEASE:      gf_common_rsp compound_release_rsp;
        case GF_FOP_RELEASEDIR:   gf_common_rsp compound_releasedir_rsp;
        case GF_FOP_FREMOVEXATTR: gf_common_rsp compound_fremovexattr_rsp;
        case GF_FOP_FALLOCATE:    gfs3_fallocate_rsp compound_fallocate_rsp;
        case GF_FOP_DISCARD:      gfs3_discard_rsp compound_discard_rsp;
        case GF_FOP_ZEROFILL:     gfs3_zerofill_rsp compound_zerofill_rsp;
        case GF_FOP_IPC:          gfs3_ipc_rsp compound_ipc_rsp;
        case GF_FOP_SEEK:         gfs3_seek_rsp compound_seek_rsp;
        case GF_FOP_LEASE:         gfs3_lease_rsp compound_lease_rsp;
        default:                  void;
};

struct gfs3_compound_rsp {
        int           op_ret;
        int           op_errno;
        compound_rsp  compound_rsp_array<>;
        opaque        xdata<>;
};

struct gfs3_locklist {
        struct gf_proto_flock flock;
        string client_uid<>;
        unsigned int lk_flags;
        struct gfs3_locklist *nextentry;
};

struct gfs3_getactivelk_rsp {
        int op_ret;
        int op_errno;
        struct gfs3_locklist *reply;
        opaque xdata<>;
};

struct gfs3_getactivelk_req {
        opaque gfid[16];
        opaque xdata<>;
};

struct gfs3_setactivelk_rsp {
        int    op_ret;
        int    op_errno;
        opaque   xdata<>;
};

struct gfs3_setactivelk_req {
        opaque gfid[16];
        struct gfs3_locklist *request;
        opaque xdata<>;
};
