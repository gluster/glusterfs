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
%#include <glusterfs/glusterfs-fops.h>

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

struct gfx_mdata_iatt {
        hyper      ia_atime;      /* last access time */
        hyper      ia_mtime;      /* last modification time */
        hyper      ia_ctime;      /* last status change time */

        unsigned int     ia_atime_nsec;
        unsigned int     ia_mtime_nsec;
        unsigned int     ia_ctime_nsec;
};

union gfx_value switch (int type) {
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
        case GF_DATA_TYPE_STR_OLD:
                opaque other<>;
        case GF_DATA_TYPE_MDATA:
                gfx_mdata_iatt mdata_iatt;
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

struct gfs3_cbk_cache_invalidation_req {
        string         gfid<>;
        unsigned int   event_type; /* Upcall event type */
        unsigned int   flags;  /* or mask of events incase of inotify */
        unsigned int   expire_time_attr; /* the amount of time which client
                                          * can cache this entry */
        gf_iatt stat;  /* Updated/current stat of the file/dir */
        gf_iatt parent_stat;  /* Updated stat of the parent dir
                                      * needed in case of create, mkdir,
                                      * unlink, rmdir, rename fops */
        gf_iatt oldparent_stat;  /* Updated stat of the oldparent dir
                                           needed in case of rename fop */
        opaque   xdata<>; /* Extra data */
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

 struct   gfx_copy_file_range_req {
        opaque gfid1[16];
        opaque gfid2[16];
        quad_t  fd_in;
        quad_t  fd_out;
        u_quad_t   off_in;
        u_quad_t   off_out;
        unsigned int size;
        unsigned int flag;
        gfx_dict xdata; /* Extra data */
};

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

struct gfs3_locklist {
        gf_proto_flock flock;
        string client_uid<>;
        unsigned int lk_flags;
        gfs3_locklist *nextentry;
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

struct gfs3_recall_lease_req {
        opaque       gfid[16];
        unsigned int lease_type;
        opaque       tid[16];
        opaque       xdata<>; /* Extra data */
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
struct gf_event_notify_req {
	int op;
	opaque dict<>;
};

struct gf_event_notify_rsp {
	int op_ret;
	int op_errno;
	opaque dict<>;
};

struct gf_setvolume_req {
        opaque dict<>;
};

struct gf_setvolume_rsp {
        int    op_ret;
        int    op_errno;
        opaque dict<>;
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
struct gf_getsnap_name_uuid_req {
        opaque dict<>;
};

struct gf_getsnap_name_uuid_rsp {
        int op_ret;
        int op_errno;
        string op_errstr<>;
        opaque dict<>;
};


/* for quota */

struct gfs3_lookup_req {
        opaque gfid[16];
        opaque  pargfid[16];
	unsigned int flags;
	string     bname<>;
        opaque   xdata<>; /* Extra data */
};

struct gfs3_lookup_rsp {
        int    op_ret;
        int    op_errno;
	gf_iatt stat;
        gf_iatt postparent;
        opaque   xdata<>; /* Extra data */
};
