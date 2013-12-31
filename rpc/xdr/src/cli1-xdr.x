 enum gf_cli_defrag_type {
        GF_DEFRAG_CMD_START = 1,
        GF_DEFRAG_CMD_STOP,
        GF_DEFRAG_CMD_STATUS,
        GF_DEFRAG_CMD_START_LAYOUT_FIX,
        GF_DEFRAG_CMD_START_FORCE /* used by remove-brick data migration */
} ;

 enum gf_defrag_status_t {
        GF_DEFRAG_STATUS_NOT_STARTED,
        GF_DEFRAG_STATUS_STARTED,
        GF_DEFRAG_STATUS_STOPPED,
        GF_DEFRAG_STATUS_COMPLETE,
        GF_DEFRAG_STATUS_FAILED,
        GF_DEFRAG_STATUS_LAYOUT_FIX_STARTED,
        GF_DEFRAG_STATUS_LAYOUT_FIX_STOPPED,
        GF_DEFRAG_STATUS_LAYOUT_FIX_COMPLETE,
        GF_DEFRAG_STATUS_LAYOUT_FIX_FAILED,
        GF_DEFRAG_STATUS_MAX
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
        GF_OP_CMD_STOP,
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
        GF_QUOTA_OPTION_TYPE_VERSION,
        GF_QUOTA_OPTION_TYPE_ALERT_TIME,
        GF_QUOTA_OPTION_TYPE_SOFT_TIMEOUT,
        GF_QUOTA_OPTION_TYPE_HARD_TIMEOUT,
        GF_QUOTA_OPTION_TYPE_DEFAULT_SOFT_LIMIT
};

enum gf1_cli_friends_list {
        GF_CLI_LIST_PEERS = 1,
        GF_CLI_LIST_POOL_NODES = 2
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
        GF_GSYNC_OPTION_TYPE_STATUS,
        GF_GSYNC_OPTION_TYPE_ROTATE,
        GF_GSYNC_OPTION_TYPE_CREATE,
        GF_GSYNC_OPTION_TYPE_DELETE
};

enum gf1_cli_stats_op {
        GF_CLI_STATS_NONE  = 0,
        GF_CLI_STATS_START = 1,
        GF_CLI_STATS_STOP  = 2,
        GF_CLI_STATS_INFO  = 3,
        GF_CLI_STATS_TOP = 4
};

enum gf1_cli_info_op {
	    GF_CLI_INFO_NONE = 0,
	    GF_CLI_INFO_ALL = 1,
	    GF_CLI_INFO_INCREMENTAL = 2,
	    GF_CLI_INFO_CUMULATIVE = 3,
	    GF_CLI_INFO_CLEAR = 4
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

/* The unconventional hex numbers help us perform
   bit-wise operations which reduces complexity */
enum gf_cli_status_type {
        GF_CLI_STATUS_NONE         = 0x0000,
        GF_CLI_STATUS_MEM          = 0x0001,    /*00000000000001*/
        GF_CLI_STATUS_CLIENTS      = 0x0002,    /*00000000000010*/
        GF_CLI_STATUS_INODE        = 0x0004,    /*00000000000100*/
        GF_CLI_STATUS_FD           = 0x0008,    /*00000000001000*/
        GF_CLI_STATUS_CALLPOOL     = 0x0010,    /*00000000010000*/
        GF_CLI_STATUS_DETAIL       = 0x0020,    /*00000000100000*/
        GF_CLI_STATUS_TASKS        = 0x0040,    /*0000001000000*/
        GF_CLI_STATUS_MASK         = 0x00FF,    /*00000011111111 Used to get the op*/
        GF_CLI_STATUS_VOL          = 0x0100,    /*00000100000000*/
        GF_CLI_STATUS_ALL          = 0x0200,    /*00001000000000*/
        GF_CLI_STATUS_BRICK        = 0x0400,    /*00010000000000*/
        GF_CLI_STATUS_NFS          = 0x0800,    /*00100000000000*/
        GF_CLI_STATUS_SHD          = 0x1000,    /*01000000000000*/
        GF_CLI_STATUS_QUOTAD       = 0x2000     /*10000000000000*/
};

 struct gf_cli_req {
        opaque  dict<>;
}  ;

 struct gf_cli_rsp {
        int     op_ret;
        int     op_errno;
        string  op_errstr<>;
        opaque  dict<>;
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

struct gf1_cli_fsm_log_req {
        string name<>;
};

struct gf1_cli_fsm_log_rsp {
        int op_ret;
        int op_errno;
        string op_errstr<>;
        opaque fsm_log<>;
};

struct gf1_cli_getwd_req {
        int     unused;
} ;

struct gf1_cli_getwd_rsp {
        int     op_ret;
        int     op_errno;
        string  wd<>;
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

struct gf1_cli_umount_rsp {
       int op_ret;
       int op_errno;
};
