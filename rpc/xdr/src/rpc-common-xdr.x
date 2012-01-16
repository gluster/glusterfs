/* This file has definition of few XDR structures which are
 * not captured in any section specific file */

struct auth_glusterfs_parms_v2 {
        int pid;
        unsigned int uid;
        unsigned int gid;
        unsigned int groups<>;
        opaque lk_owner<>;
};

struct auth_glusterfs_parms {
        unsigned hyper lk_owner;
        unsigned int pid;
        unsigned int uid;
	unsigned int gid;
	unsigned int ngrps;
	unsigned groups[16];
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
        int op_ret;
	int op_errno;
	struct gf_prog_detail *prog;
};
