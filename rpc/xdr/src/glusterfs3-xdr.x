#define GF_REQUEST_MAXGROUPS    16
struct gf_statfs {
	unsigned hyper bsize;
	unsigned hyper frsize;
	unsigned hyper blocks;
	unsigned hyper bfree;
	unsigned hyper bavail;
	unsigned hyper files;
	unsigned hyper ffree;
	unsigned hyper favail;
	unsigned hyper fsid;
	unsigned hyper flag;
	unsigned hyper namemax;
};

struct gf_proto_flock {
	unsigned int   type;
	unsigned int   whence;
	unsigned hyper start;
	unsigned hyper len;
        unsigned int   pid;
        unsigned hyper owner;
} ;


struct gf_iatt {
        opaque             ia_gfid[16];
        unsigned hyper     ia_ino;        /* inode number */
        unsigned hyper     ia_dev;        /* backing device ID */
        unsigned int       mode;          /* mode (type + protection )*/
        unsigned int       ia_nlink;      /* Link count */
        unsigned int       ia_uid;        /* user ID of owner */
        unsigned int       ia_gid;        /* group ID of owner */
        unsigned hyper     ia_rdev;       /* device ID (if special file) */
        unsigned hyper     ia_size;       /* file size in bytes */
        unsigned int       ia_blksize;    /* blocksize for filesystem I/O */
        unsigned hyper     ia_blocks;     /* number of 512B blocks allocated */
        unsigned int       ia_atime;      /* last access time */
        unsigned int       ia_atime_nsec;
        unsigned int       ia_mtime;      /* last modification time */
        unsigned int       ia_mtime_nsec;
        unsigned int       ia_ctime;      /* last status change time */
        unsigned int       ia_ctime_nsec;
};

struct gfs3_stat_req {
        opaque gfid[16];
        string         path<>;     /* NULL terminated */

};
struct gfs3_stat_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
} ;


struct gfs3_readlink_req {
        opaque gfid[16];
	unsigned int   size;
	string         path<>;     /* NULL terminated */
}  ;
 struct gfs3_readlink_rsp {
        int    op_ret;
        int    op_errno;
        struct gf_iatt buf;
        string      path<>; /* NULL terminated */
} ;


 struct gfs3_mknod_req {
        opaque  pargfid[16];
	unsigned hyper dev;
	unsigned int mode;
	string     path<>;     /* NULL terminated */
	string     bname<>; /* NULL terminated */
        opaque     dict<>;
} ;
 struct gfs3_mknod_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
        struct gf_iatt preparent;
        struct gf_iatt postparent;
};


 struct  gfs3_mkdir_req {
        opaque  pargfid[16];
	unsigned int mode;
	string     path<>;     /* NULL terminated */
	string     bname<>; /* NULL terminated */
        opaque     dict<>;
} ;
 struct  gfs3_mkdir_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
        struct gf_iatt preparent;
        struct gf_iatt postparent;
} ;


 struct   gfs3_unlink_req {
        opaque  pargfid[16];
	string     path<>;     /* NULL terminated */
	string     bname<>; /* NULL terminated */
};
 struct   gfs3_unlink_rsp {
        int    op_ret;
        int    op_errno;
        struct gf_iatt preparent;
        struct gf_iatt postparent;
};


 struct   gfs3_rmdir_req {
        opaque  pargfid[16];
        int        flags;
	string     path<>;
	string     bname<>; /* NULL terminated */
};
 struct   gfs3_rmdir_rsp {
        int    op_ret;
        int    op_errno;
        struct gf_iatt preparent;
        struct gf_iatt postparent;
};


 struct   gfs3_symlink_req {
        opaque  pargfid[16];
	string     path<>;
	string     bname<>;
	string     linkname<>;
        opaque     dict<>;
};
 struct  gfs3_symlink_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
        struct gf_iatt preparent;
        struct gf_iatt postparent;
};


 struct   gfs3_rename_req {
        opaque  oldgfid[16];
        opaque  newgfid[16];
	string       oldpath<>;
	string       oldbname<>; /* NULL terminated */
	string       newpath<>;
	string       newbname<>; /* NULL terminated */
};
 struct   gfs3_rename_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
        struct gf_iatt preoldparent;
        struct gf_iatt postoldparent;
        struct gf_iatt prenewparent;
        struct gf_iatt postnewparent;
};


 struct  gfs3_link_req {
        opaque  oldgfid[16];
        opaque  newgfid[16];
	string       oldpath<>;
	string       newpath<>;
	string       newbname<>;
};
 struct   gfs3_link_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
        struct gf_iatt preparent;
        struct gf_iatt postparent;
};

 struct   gfs3_truncate_req {
        opaque gfid[16];
	unsigned hyper offset;
	string     path<>;
};
 struct   gfs3_truncate_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt prestat;
        struct gf_iatt poststat;
};


 struct   gfs3_open_req {
        opaque gfid[16];
	unsigned int flags;
        unsigned int wbflags;
	string     path<>;
};
 struct   gfs3_open_rsp {
        int    op_ret;
        int    op_errno;
	hyper fd;
};


 struct   gfs3_read_req {
        opaque gfid[16];
	hyper  fd;
	unsigned hyper offset;
	unsigned int size;
};
 struct  gfs3_read_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
        unsigned int size;
} ;

struct   gfs3_lookup_req {
        opaque gfid[16];
        opaque  pargfid[16];
	unsigned int flags;
	string     path<>;
	string     bname<>;
        opaque     dict<>;
};
 struct   gfs3_lookup_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
        struct gf_iatt postparent;
	opaque             dict<>;
} ;



 struct   gfs3_write_req {
        opaque gfid[16];
	hyper  fd;
	unsigned hyper offset;
	unsigned int size;
};
 struct gfs3_write_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt prestat;
        struct gf_iatt poststat;
} ;


 struct gfs3_statfs_req  {
        opaque gfid[16];
	string     path<>;
}  ;
 struct gfs3_statfs_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_statfs statfs;
}  ;

 struct gfs3_lk_req {
        opaque gfid[16];
	hyper         fd;
	unsigned int        cmd;
	unsigned int        type;
	struct gf_proto_flock flock;
}  ;
 struct gfs3_lk_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_proto_flock flock;
}  ;

 struct gfs3_inodelk_req {
        opaque gfid[16];
	unsigned int cmd;
	unsigned int type;
	struct gf_proto_flock flock;
	string     path<>;
        string     volume<>;
}  ;

struct   gfs3_finodelk_req {
        opaque gfid[16];
	hyper  fd;
	unsigned int cmd;
	unsigned int type;
	struct gf_proto_flock flock;
        string volume<>;
} ;


 struct gfs3_flush_req {
        opaque gfid[16];
	hyper  fd;
}  ;


 struct gfs3_fsync_req {
        opaque gfid[16];
	hyper  fd;
	unsigned int data;
}  ;
 struct gfs3_fsync_rsp {
        int    op_ret;
        int    op_errno;
        struct gf_iatt prestat;
        struct gf_iatt poststat;
}  ;


 struct gfs3_setxattr_req {
        opaque gfid[16];
	unsigned int flags;
        opaque     dict<>;
	string     path<>;
}  ;



 struct gfs3_fsetxattr_req {
        opaque gfid[16];
	hyper  fd;
	unsigned int flags;
        opaque     dict<>;
}  ;



 struct gfs3_xattrop_req {
        opaque gfid[16];
	unsigned int flags;
        opaque     dict<>;
	string     path<>;
}  ;

 struct gfs3_xattrop_rsp  {
        int    op_ret;
        int    op_errno;
	opaque  dict<>;
}  ;


 struct gfs3_fxattrop_req {
        opaque gfid[16];
	hyper  fd;
	unsigned int flags;
	opaque     dict<>;
}  ;

 struct gfs3_fxattrop_rsp  {
        int    op_ret;
        int    op_errno;
	opaque  dict<>;
}  ;


 struct gfs3_getxattr_req  {
        opaque gfid[16];
	unsigned int namelen;
	string     path<>;
	string     name<>;
}  ;
 struct gfs3_getxattr_rsp {
        int    op_ret;
        int    op_errno;
	opaque     dict<>;
}  ;


 struct gfs3_fgetxattr_req  {
        opaque gfid[16];
	hyper  fd;
        unsigned int namelen;
	string     name<>;
}  ;
 struct gfs3_fgetxattr_rsp {
        int    op_ret;
        int    op_errno;
        opaque     dict<>;
}  ;


 struct gfs3_removexattr_req {
        opaque gfid[16];
	string     path<>;
	string     name<>;
}  ;



 struct gfs3_opendir_req {
        opaque gfid[16];
	string     path<>;
}  ;
 struct gfs3_opendir_rsp {
        int    op_ret;
        int    op_errno;
	hyper fd;
}  ;


 struct gfs3_fsyncdir_req {
        opaque gfid[16];
	hyper  fd;
	int  data;
}  ;

 struct   gfs3_readdir_req  {
        opaque gfid[16];
	hyper  fd;
	unsigned hyper offset;
	unsigned int size;
};

 struct gfs3_readdirp_req {
        opaque gfid[16];
	hyper  fd;
	unsigned hyper offset;
	unsigned int size;
}  ;


 struct gf_setvolume_req {
        opaque dict<>;
}  ;
 struct  gf_setvolume_rsp {
        int    op_ret;
        int    op_errno;
        opaque dict<>;
} ;

struct gfs3_access_req  {
        opaque gfid[16];
	unsigned int mask;
	string     path<>;
} ;


struct gfs3_create_req {
        opaque  pargfid[16];
	unsigned int flags;
	unsigned int mode;
	string     path<>;
	string     bname<>;
        opaque     dict<>;
}  ;
struct  gfs3_create_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
	unsigned hyper       fd;
        struct gf_iatt preparent;
        struct gf_iatt postparent;
} ;



struct   gfs3_ftruncate_req  {
        opaque gfid[16];
	hyper  fd;
	unsigned hyper offset;
} ;
struct   gfs3_ftruncate_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt prestat;
        struct gf_iatt poststat;
} ;


struct gfs3_fstat_req {
        opaque gfid[16];
	hyper  fd;
}  ;
 struct gfs3_fstat_rsp {
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
}  ;



 struct   gfs3_entrylk_req {
        opaque gfid[16];
	unsigned int  cmd;
	unsigned int  type;
	unsigned hyper  namelen;
	string      path<>;
	string      name<>;
        string      volume<>;
};

 struct   gfs3_fentrylk_req {
        opaque gfid[16];
	hyper   fd;
	unsigned int  cmd;
	unsigned int  type;
	unsigned hyper  namelen;
	string      name<>;
        string      volume<>;
};


 struct gfs3_setattr_req {
        opaque gfid[16];
        struct gf_iatt stbuf;
        int        valid;
        string           path<>;
}  ;
 struct gfs3_setattr_rsp {
        int    op_ret;
        int    op_errno;
        struct gf_iatt statpre;
        struct gf_iatt statpost;
}  ;

 struct gfs3_fsetattr_req {
        hyper        fd;
        struct gf_iatt stbuf;
        int        valid;
}  ;
 struct gfs3_fsetattr_rsp {
        int    op_ret;
        int    op_errno;
        struct gf_iatt statpre;
        struct gf_iatt statpost;
}  ;

 struct gfs3_rchecksum_req {
        hyper   fd;
        unsigned hyper  offset;
        unsigned int  len;
}  ;
 struct gfs3_rchecksum_rsp {
        int    op_ret;
        int    op_errno;
        unsigned int weak_checksum;
        opaque   strong_checksum<>;
}  ;


 struct gf_getspec_req {
	unsigned int flags;
	string     key<>;
}  ;
 struct  gf_getspec_rsp {
        int    op_ret;
        int    op_errno;
	string spec<>;
} ;


 struct   gf_log_req {
	opaque     msg<>;
};

 struct gf_notify_req {
	unsigned int  flags;
        string buf<>;
}  ;
 struct gf_notify_rsp {
        int    op_ret;
        int    op_errno;
	unsigned int  flags;
        string buf<>;
}  ;

struct gfs3_releasedir_req {
        opaque gfid[16];
	hyper  fd;
}  ;

struct gfs3_release_req {
        opaque gfid[16];
	hyper  fd;
}  ;

struct gf_common_rsp {
       int    op_ret;
       int    op_errno;
} ;

struct gfs3_dirlist {
       unsigned hyper d_ino;
       unsigned hyper d_off;
       unsigned int d_len;
       unsigned int d_type;
       string name<>;
       struct gfs3_dirlist *nextentry;
};


struct gfs3_readdir_rsp {
       int op_ret;
       int op_errno;
       struct gfs3_dirlist *reply;
};

struct gfs3_dirplist {
       unsigned hyper d_ino;
       unsigned hyper d_off;
       unsigned int d_len;
       unsigned int d_type;
       string name<>;
       struct gf_iatt stat;
       struct gfs3_dirplist *nextentry;
};

struct gfs3_readdirp_rsp {
       int op_ret;
       int op_errno;
       struct gfs3_dirplist *reply;
};

