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

struct gf_flock {
	unsigned int   type;
	unsigned int   whence;
	unsigned hyper start;
	unsigned hyper len;
        unsigned int   pid;
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
        unsigned hyper gfs_id;
        opaque gfid[16];
        string         path<>;     /* NULL terminated */

};
struct gfs3_stat_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
} ;


struct gfs3_readlink_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
	unsigned int   size;
	string         path<>;     /* NULL terminated */
}  ;
 struct gfs3_readlink_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
        struct gf_iatt buf;
        string      path<>; /* NULL terminated */
} ;


 struct gfs3_mknod_req {
        unsigned hyper gfs_id;
        opaque  pargfid[16];
	unsigned hyper dev;
	unsigned int mode;
	string     path<>;     /* NULL terminated */
	string     bname<>; /* NULL terminated */
        opaque     dict<>;
} ;
 struct gfs3_mknod_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
        struct gf_iatt preparent;
        struct gf_iatt postparent;
};


 struct  gfs3_mkdir_req {
        unsigned hyper gfs_id;
        opaque  pargfid[16];
	unsigned int mode;
	string     path<>;     /* NULL terminated */
	string     bname<>; /* NULL terminated */
        opaque     dict<>;
} ;
 struct  gfs3_mkdir_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
        struct gf_iatt preparent;
        struct gf_iatt postparent;
} ;


 struct   gfs3_unlink_req {
        unsigned hyper gfs_id;
        opaque  pargfid[16];
	string     path<>;     /* NULL terminated */
	string     bname<>; /* NULL terminated */
};
 struct   gfs3_unlink_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
        struct gf_iatt preparent;
        struct gf_iatt postparent;
};


 struct   gfs3_rmdir_req {
        unsigned hyper gfs_id;
        opaque  pargfid[16];
	string     path<>;
	string     bname<>; /* NULL terminated */
};
 struct   gfs3_rmdir_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
        struct gf_iatt preparent;
        struct gf_iatt postparent;
};


 struct   gfs3_symlink_req {
        unsigned hyper gfs_id;
        opaque  pargfid[16];
	string     path<>;
	string     bname<>;
	string     linkname<>;
        opaque     dict<>;
};
 struct  gfs3_symlink_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
        struct gf_iatt preparent;
        struct gf_iatt postparent;
};


 struct   gfs3_rename_req {
        unsigned hyper gfs_id;
        opaque  oldgfid[16];
        opaque  newgfid[16];
	string       oldpath<>;
	string       oldbname<>; /* NULL terminated */
	string       newpath<>;
	string       newbname<>; /* NULL terminated */
};
 struct   gfs3_rename_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
        struct gf_iatt preoldparent;
        struct gf_iatt postoldparent;
        struct gf_iatt prenewparent;
        struct gf_iatt postnewparent;
};


 struct  gfs3_link_req {
        unsigned hyper gfs_id;
        opaque  oldgfid[16];
        opaque  newgfid[16];
	string       oldpath<>;
	string       newpath<>;
	string       newbname<>;
};
 struct   gfs3_link_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
        struct gf_iatt preparent;
        struct gf_iatt postparent;
};

 struct   gfs3_truncate_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
	unsigned hyper offset;
	string     path<>;
};
 struct   gfs3_truncate_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
	struct gf_iatt prestat;
        struct gf_iatt poststat;
};


 struct   gfs3_open_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
	unsigned int flags;
        unsigned int wbflags;
	string     path<>;
};
 struct   gfs3_open_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
	hyper fd;
};


 struct   gfs3_read_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
	hyper  fd;
	unsigned hyper offset;
	unsigned int size;
};
 struct  gfs3_read_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
        unsigned int size;
} ;

struct   gfs3_lookup_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
        opaque  pargfid[16];
	unsigned int flags;
	string     path<>;
	string     bname<>;
        opaque     dict<>;
};
 struct   gfs3_lookup_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
        struct gf_iatt postparent;
	opaque             dict<>;
} ;



 struct   gfs3_write_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
	hyper  fd;
	unsigned hyper offset;
	unsigned int size;
};
 struct gfs3_write_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
	struct gf_iatt prestat;
        struct gf_iatt poststat;
} ;


 struct gfs3_statfs_req  {
        unsigned hyper gfs_id;
        opaque gfid[16];
	string     path<>;
}  ;
 struct gfs3_statfs_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
	struct gf_statfs statfs;
}  ;

 struct gfs3_lk_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
	hyper         fd;
	unsigned int        cmd;
	unsigned int        type;
	struct gf_flock flock;
}  ;
 struct gfs3_lk_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
	struct gf_flock flock;
}  ;

 struct gfs3_inodelk_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
	unsigned int cmd;
	unsigned int type;
	struct gf_flock flock;
	string     path<>;
        string     volume<>;
}  ;

struct   gfs3_finodelk_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
	hyper  fd;
	unsigned int cmd;
	unsigned int type;
	struct gf_flock flock;
        string volume<>;
} ;


 struct gfs3_flush_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
	hyper  fd;
}  ;


 struct gfs3_fsync_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
	hyper  fd;
	unsigned int data;
}  ;
 struct gfs3_fsync_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
        struct gf_iatt prestat;
        struct gf_iatt poststat;
}  ;


 struct gfs3_setxattr_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
	unsigned int flags;
        opaque     dict<>;
	string     path<>;
}  ;



 struct gfs3_fsetxattr_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
	hyper  fd;
	unsigned int flags;
        opaque     dict<>;
}  ;



 struct gfs3_xattrop_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
	unsigned int flags;
        opaque     dict<>;
	string     path<>;
}  ;

 struct gfs3_xattrop_rsp  {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
	opaque  dict<>;
}  ;


 struct gfs3_fxattrop_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
	hyper  fd;
	unsigned int flags;
	opaque     dict<>;
}  ;

 struct gfs3_fxattrop_rsp  {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
	opaque  dict<>;
}  ;


 struct gfs3_getxattr_req  {
        unsigned hyper gfs_id;
        opaque gfid[16];
	unsigned int namelen;
	string     path<>;
	string     name<>;
}  ;
 struct gfs3_getxattr_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
	opaque     dict<>;
}  ;


 struct gfs3_fgetxattr_req  {
        unsigned hyper gfs_id;
        opaque gfid[16];
	hyper  fd;
        unsigned int namelen;
	string     name<>;
}  ;
 struct gfs3_fgetxattr_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
        opaque     dict<>;
}  ;


 struct gfs3_removexattr_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
	string     path<>;
	string     name<>;
}  ;



 struct gfs3_opendir_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
	string     path<>;
}  ;
 struct gfs3_opendir_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
	hyper fd;
}  ;


 struct gfs3_fsyncdir_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
	hyper  fd;
	int  data;
}  ;

 struct   gfs3_readdir_req  {
        unsigned hyper gfs_id;
        opaque gfid[16];
	hyper  fd;
	unsigned hyper offset;
	unsigned int size;
};

 struct gfs3_readdirp_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
	hyper  fd;
	unsigned hyper offset;
	unsigned int size;
}  ;


 struct gf_setvolume_req {
        unsigned hyper gfs_id;
        opaque dict<>;
}  ;
 struct  gf_setvolume_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
        opaque dict<>;
} ;

struct gfs3_access_req  {
        unsigned hyper gfs_id;
        opaque gfid[16];
	unsigned int mask;
	string     path<>;
} ;


struct gfs3_create_req {
        unsigned hyper gfs_id;
        opaque  pargfid[16];
	unsigned int flags;
	unsigned int mode;
	string     path<>;
	string     bname<>;
        opaque     dict<>;
}  ;
struct  gfs3_create_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
	unsigned hyper       fd;
        struct gf_iatt preparent;
        struct gf_iatt postparent;
} ;



struct   gfs3_ftruncate_req  {
        unsigned hyper gfs_id;
        opaque gfid[16];
	hyper  fd;
	unsigned hyper offset;
} ;
struct   gfs3_ftruncate_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
	struct gf_iatt prestat;
        struct gf_iatt poststat;
} ;


struct gfs3_fstat_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
	hyper  fd;
}  ;
 struct gfs3_fstat_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
	struct gf_iatt stat;
}  ;



 struct   gfs3_entrylk_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
	unsigned int  cmd;
	unsigned int  type;
	unsigned hyper  namelen;
	string      path<>;
	string      name<>;
        string      volume<>;
};

 struct   gfs3_fentrylk_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
	hyper   fd;
	unsigned int  cmd;
	unsigned int  type;
	unsigned hyper  namelen;
	string      name<>;
        string      volume<>;
};


 struct gfs3_setattr_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
        struct gf_iatt stbuf;
        int        valid;
        string           path<>;
}  ;
 struct gfs3_setattr_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
        struct gf_iatt statpre;
        struct gf_iatt statpost;
}  ;

 struct gfs3_fsetattr_req {
        unsigned hyper gfs_id;
        hyper        fd;
        struct gf_iatt stbuf;
        int        valid;
}  ;
 struct gfs3_fsetattr_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
        struct gf_iatt statpre;
        struct gf_iatt statpost;
}  ;

 struct gfs3_rchecksum_req {
        unsigned hyper gfs_id;
        hyper   fd;
        unsigned hyper  offset;
        unsigned int  len;
}  ;
 struct gfs3_rchecksum_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
        unsigned int weak_checksum;
        opaque   strong_checksum<>;
}  ;


 struct gf_getspec_req {
        unsigned hyper gfs_id;
	unsigned int flags;
	string     key<>;
}  ;
 struct  gf_getspec_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
	string spec<>;
} ;


 struct   gf_log_req {
        unsigned hyper gfs_id;
	opaque     msg<>;
};

 struct gf_notify_req {
        unsigned hyper gfs_id;
	unsigned int  flags;
        string buf<>;
}  ;
 struct gf_notify_rsp {
        unsigned hyper gfs_id;
        int    op_ret;
        int    op_errno;
	unsigned int  flags;
        string buf<>;
}  ;

struct gfs3_releasedir_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
	hyper  fd;
}  ;

struct gfs3_release_req {
        unsigned hyper gfs_id;
        opaque gfid[16];
	hyper  fd;
}  ;

struct gf_common_rsp {
       unsigned hyper gfs_id;
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
       unsigned hyper gfs_id;
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
       unsigned hyper gfs_id;
       int op_ret;
       int op_errno;
       struct gfs3_dirplist *reply;
};

struct gf_dump_req {
       unsigned hyper gfs_id;
};

struct gf_prog_detail {
       string progname<>;
       unsigned hyper prognum;
       unsigned hyper progver;
       struct gf_prog_detail *next;
};

struct gf_dump_rsp {
       unsigned hyper gfs_id;
       struct gf_prog_detail *prog;
};

struct auth_glusterfs_parms {
       unsigned int pid;
       unsigned int uid;
       unsigned int gid;

        /* Number of groups being sent through the array above. */
        unsigned int ngrps;

        /* Array of groups to which the uid belongs apart from the primary group
         * in gid.
         */
        unsigned int groups[GF_REQUEST_MAXGROUPS];

        unsigned hyper lk_owner;
};
