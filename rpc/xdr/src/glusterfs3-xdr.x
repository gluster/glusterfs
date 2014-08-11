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
