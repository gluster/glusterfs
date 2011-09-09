 enum gf_cli_defrag_type {
        GF_DEFRAG_CMD_START = 1,
        GF_DEFRAG_CMD_STOP,
        GF_DEFRAG_CMD_STATUS,
        GF_DEFRAG_CMD_START_LAYOUT_FIX,
        GF_DEFRAG_CMD_START_MIGRATE_DATA,
        GF_DEFRAG_CMD_START_MIGRATE_DATA_FORCE,
        GF_DEFRAG_CMD_START_FORCE /* used by remove-brick data migration */
} ;

 enum gf_defrag_status_t {
        GF_DEFRAG_STATUS_NOT_STARTED,
        GF_DEFRAG_STATUS_LAYOUT_FIX_STARTED,
        GF_DEFRAG_STATUS_MIGRATE_DATA_STARTED,
        GF_DEFRAG_STATUS_STOPPED,
        GF_DEFRAG_STATUS_COMPLETE,
        GF_DEFRAG_STATUS_FAILED,
        GF_DEFRAG_STATUS_LAYOUT_FIX_COMPLETE,
        GF_DEFRAG_STATUS_MIGRATE_DATA_COMPLETE,
        GF_DEFRAG_STATUS_PAUSED
} ;

 enum gf1_cluster_type {
        GF_CLUSTER_TYPE_NONE = 0,
        GF_CLUSTER_TYPE_STRIPE,
        GF_CLUSTER_TYPE_REPLICATE,
        GF_CLUSTER_TYPE_STRIPE_REPLICATE
} ;

 enum gf1_cli_replace_op {
        GF_REPLACE_OP_NONE = 0,
        GF_REPLACE_OP_START,
        GF_REPLACE_OP_COMMIT,
        GF_REPLACE_OP_PAUSE,
        GF_REPLACE_OP_ABORT,
        GF_REPLACE_OP_STATUS,
        GF_REPLACE_OP_COMMIT_FORCE
} ;

 enum gf1_op_commands {
        GF_OP_CMD_NONE = 0,
        GF_OP_CMD_START,
        GF_OP_CMD_COMMIT,
        GF_OP_CMD_PAUSE,
        GF_OP_CMD_ABORT,
        GF_OP_CMD_STATUS,
        GF_OP_CMD_COMMIT_FORCE
} ;

enum gf_quota_type {
        GF_QUOTA_OPTION_TYPE_NONE = 0,
        GF_QUOTA_OPTION_TYPE_ENABLE,
        GF_QUOTA_OPTION_TYPE_DISABLE,
        GF_QUOTA_OPTION_TYPE_LIMIT_USAGE,
        GF_QUOTA_OPTION_TYPE_REMOVE,
        GF_QUOTA_OPTION_TYPE_LIST,
        GF_QUOTA_OPTION_TYPE_VERSION
};

enum gf1_cli_friends_list {
        GF_CLI_LIST_ALL = 1
} ;

enum gf1_cli_get_volume {
        GF_CLI_GET_VOLUME_ALL = 1,
        GF_CLI_GET_VOLUME,
        GF_CLI_GET_NEXT_VOLUME
} ;

enum gf1_cli_sync_volume {
        GF_CLI_SYNC_ALL = 1
} ;

enum gf1_cli_op_flags {
        GF_CLI_FLAG_OP_FORCE = 1
};

enum gf1_cli_gsync_set {
        GF_GSYNC_OPTION_TYPE_NONE,
        GF_GSYNC_OPTION_TYPE_START,
        GF_GSYNC_OPTION_TYPE_STOP,
        GF_GSYNC_OPTION_TYPE_CONFIG,
        GF_GSYNC_OPTION_TYPE_STATUS
};

enum gf1_cli_stats_op {
        GF_CLI_STATS_NONE  = 0,
        GF_CLI_STATS_START = 1,
        GF_CLI_STATS_STOP  = 2,
        GF_CLI_STATS_INFO  = 3,
        GF_CLI_STATS_TOP = 4
};

enum gf1_cli_top_op {
        GF_CLI_TOP_NONE = 0,
        GF_CLI_TOP_OPEN,
        GF_CLI_TOP_READ,
        GF_CLI_TOP_WRITE,
        GF_CLI_TOP_OPENDIR,
        GF_CLI_TOP_READDIR,
        GF_CLI_TOP_READ_PERF,
        GF_CLI_TOP_WRITE_PERF
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
        string  op_errstr<>;
}  ;

 struct gf1_cli_start_vol_req {
        string volname<>;
        int flags;
}  ;


 struct gf1_cli_start_vol_rsp {
        int     op_ret;
        int     op_errno;
        string  volname<>;
        string  op_errstr<>;
}  ;

 struct gf1_cli_stop_vol_req {
        string volname<>;
        int flags;
}  ;


 struct gf1_cli_stop_vol_rsp {
        int     op_ret;
        int     op_errno;
        string  volname<>;
        string  op_errstr<>;
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


 struct gf2_cli_defrag_vol_rsp {
        int     op_ret;
        int     op_errno;
        string  op_errstr<>;
        string  volname<>;
        unsigned hyper   files;
        unsigned hyper   size;
        unsigned hyper   lookedup_files;
}  ;

 struct gf1_cli_add_brick_req {
        string volname<>;
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
        string  op_errstr<>;
        string  volname<>;
        string  status<>;
}  ;

struct gf1_cli_reset_vol_req {
        string volname<>;
        opaque dict<>;
} ;


 struct gf1_cli_reset_vol_rsp {
        int     op_ret;
        int     op_errno;
        string  volname<>;
	string  op_errstr<>;
}  ;



struct gf1_cli_set_vol_req {
        string volname<>;
        opaque dict<>;
} ;


 struct gf1_cli_set_vol_rsp {
        int     op_ret;
        int     op_errno;
        string  volname<>;
        string  op_errstr<>;
        opaque  dict<>;
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

struct gf1_cli_sync_volume_req {
        int    flags;
        string volname<>;
        string hostname<>;
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

struct gf1_cli_sync_volume_rsp {
	int op_ret;
	int op_errno;
        string op_errstr<>;
};

struct gf1_cli_fsm_log_req {
        string name<>;
};

struct gf1_cli_fsm_log_rsp {
        int op_ret;
        int op_errno;
        string op_errstr<>;
        opaque fsm_log<>;
};

struct gf1_cli_gsync_set_req {
        opaque dict<>;
};

struct gf1_cli_gsync_set_rsp {
        int     op_ret;
        int     op_errno;
        string  op_errstr<>;
        int     type;
        opaque  dict<>;
};

struct gf1_cli_stats_volume_req {
        string           volname<>;
        gf1_cli_stats_op op;
        opaque           dict_req<>;
};

struct gf1_cli_stats_volume_rsp {
	int    op_ret;
	int    op_errno;
        string op_errstr<>;
        opaque stats_info<>;
};

struct gf1_cli_quota_req {
        string volname<>;
        opaque dict<>;
} ;

struct gf1_cli_quota_rsp {
        int     op_ret;
        int     op_errno;
        string  volname<>;
        string  op_errstr<>;
        string  limit_list<>;
        gf_quota_type type;
};

struct gf1_cli_getwd_req {
        int     unused;
} ;

struct gf1_cli_getwd_rsp {
        int     op_ret;
        int     op_errno;
        string  wd<>;
};

struct gf1_cli_log_level_req {
       string volname<>;
       string xlator<>;
       string loglevel<>;
};

struct gf1_cli_log_level_rsp {
       int op_ret;
       int op_errno;
       string op_errstr<>;
};

struct gf1_cli_status_volume_req {
        string  volname<>;
        opaque  dict<>;
};

struct gf1_cli_status_volume_rsp {
       int op_ret;
       int op_errno;
       string op_errstr<>;
       opaque dict<>;
};

struct gf1_cli_mount_req {
        string label<>;
        opaque dict<>;
};

struct gf1_cli_mount_rsp {
       int op_ret;
       int op_errno;
       string path<>;
};

struct gf1_cli_umount_req {
        int lazy;
        string path<>;
};

struct gf1_cli_mount_rsp {
       int op_ret;
       int op_errno;
};
