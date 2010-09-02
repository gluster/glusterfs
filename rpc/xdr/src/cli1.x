 enum gf1_cluster_type {
        GF_CLUSTER_TYPE_NONE = 0,
        GF_CLUSTER_TYPE_STRIPE,
        GF_CLUSTER_TYPE_REPLICATE
} ;

 enum gf1_cli_replace_op {
        GF_REPLACE_OP_NONE = 0,
        GF_REPLACE_OP_START,
        GF_REPLACE_OP_COMMIT,
        GF_REPLACE_OP_PAUSE,
        GF_REPLACE_OP_ABORT,
        GF_REPLACE_OP_STATUS
} ;

enum gf1_cli_friends_list {
        GF_CLI_LIST_ALL = 1
} ;

enum gf1_cli_get_volume {
        GF_CLI_GET_VOLUME_ALL = 1,
        GF_CLI_GET_VOLUME,
        GF_CLI_GET_NEXT_VOLUME
} ;

enum gf1_cli_op_flags {
        GF_CLI_FLAG_OP_FORCE = 1
};

 struct gf1_cli_probe_req {
        string  hostname<>;
	int	port;
}  ;

 struct gf1_cli_probe_rsp {
        int     op_ret;
        int     op_errno;
	int	port;
        string  hostname<>;
}  ;

 struct gf1_cli_deprobe_req {
        string  hostname<>;
	int	port;
}  ;

 struct gf1_cli_deprobe_rsp {
        int     op_ret;
        int     op_errno;
        string  hostname<>;
}  ;

struct gf1_cli_peer_list_req {
        int     flags;
        opaque  dict<>;
}  ;

struct gf1_cli_peer_list_rsp {
        int     op_ret;
        int     op_errno;
        opaque  friends<>;
} ;

struct gf1_cli_get_vol_req {
        int     flags;
        opaque  dict<>;
}  ;

struct gf1_cli_get_vol_rsp {
        int     op_ret;
        int     op_errno;
        opaque  volumes<>;
} ;

 struct gf1_cli_create_vol_req {
        string  volname<>;
        gf1_cluster_type type;
        int     count;
        opaque  bricks<>;
}  ;

 struct gf1_cli_create_vol_rsp {
        int     op_ret;
        int     op_errno;
        string  volname<>;
	 string  op_errstr<>;
}  ;

 struct gf1_cli_delete_vol_req {
        string volname<>;
}  ;

 struct gf1_cli_delete_vol_rsp {
        int     op_ret;
        int     op_errno;
        string  volname<>;
}  ;

 struct gf1_cli_start_vol_req {
        string volname<>;
}  ;


 struct gf1_cli_start_vol_rsp {
        int     op_ret;
        int     op_errno;
        string  volname<>;
}  ;

 struct gf1_cli_stop_vol_req {
        string volname<>;
        int flags;
}  ;


 struct gf1_cli_stop_vol_rsp {
        int     op_ret;
        int     op_errno;
        string  volname<>;
}  ;


 struct gf1_cli_rename_vol_req {
        string old_volname<>;
        string new_volname<>;
}  ;

 struct gf1_cli_rename_vol_rsp {
        int     op_ret;
        int     op_errno;
        string  volname<>;
}  ;

 struct gf1_cli_defrag_vol_req {
        int    cmd;
        string volname<>;
}  ;

 struct gf1_cli_defrag_vol_rsp {
        int     op_ret;
        int     op_errno;
        string  volname<>;
        unsigned hyper   files;
        unsigned hyper   size;
        unsigned hyper   lookedup_files;
}  ;

 struct gf1_cli_add_brick_req {
        string volname<>;
        gf1_cluster_type type;
        int    count;
        opaque bricks<>;
}  ;

 struct gf1_cli_add_brick_rsp {
        int     op_ret;
        int     op_errno;
        string  volname<>;
	 string  op_errstr<>;
}  ;

 struct gf1_cli_remove_brick_req {
        string volname<>;
        gf1_cluster_type type;
        int    count;
        opaque bricks<>;
}  ;


 struct gf1_cli_remove_brick_rsp {
        int     op_ret;
        int     op_errno;
        string  volname<>;
        string  op_errstr<>;
}  ;

 struct gf1_cli_replace_brick_req {
        string volname<>;
        gf1_cli_replace_op op;
        opaque bricks<>;
}  ;

 struct gf1_cli_replace_brick_rsp {
        int     op_ret;
        int     op_errno;
        string  volname<>;
        string  status<>;
}  ;


struct gf1_cli_set_vol_req {
        string volname<>;
        opaque dict<>;
} ;


 struct gf1_cli_set_vol_rsp {
        int     op_ret;
        int     op_errno;
        string  volname<>;
}  ;

struct gf1_cli_log_filename_req {
        string volname<>;
        string brick<>;
        string path<>;
};

struct gf1_cli_log_filename_rsp {
	int op_ret;
	int op_errno;
        string errstr<>;
};

struct gf1_cli_log_locate_req {
	string volname<>;
        string brick<>;
};

struct gf1_cli_log_locate_rsp {
	int op_ret;
	int op_errno;
        string path<>;
};

struct gf1_cli_log_rotate_req {
	string volname<>;
        string brick<>;
};

struct gf1_cli_log_rotate_rsp {
	int op_ret;
	int op_errno;
        string errstr<>;
};
