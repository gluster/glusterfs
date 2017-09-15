#ifndef _IO_STATS_H_
#define _IO_STATS_H_

#define OFFSET_WRITE_DETECTOR 0
#define UNKNOWN_WRITE_OFFSET ((off_t)-1)

#define FOP_HITS_SIZE 32
struct _ios_fop_hits_s {
        uint64_t idx;
        uint64_t hits[FOP_HITS_SIZE];
        double elapsed[FOP_HITS_SIZE];
        double avg_fops;
        double avg_latency;
};
typedef struct _ios_fop_hits_s ios_fop_hits_t;

ios_fop_hits_t *ios_fop_hits_init ();

#define GF_IO_STATS "io-stats"

#define MAX_LIST_MEMBERS 100
#define DEFAULT_PWD_BUF_SZ 16384
#define DEFAULT_GRP_BUF_SZ 16384
#define IOS_MAX_ERRORS 132

typedef enum {
        IOS_STATS_TYPE_NONE,
        IOS_STATS_TYPE_OPEN,
        IOS_STATS_TYPE_READ,
        IOS_STATS_TYPE_WRITE,
        IOS_STATS_TYPE_OPENDIR,
        IOS_STATS_TYPE_READDIRP,
        IOS_STATS_TYPE_READ_THROUGHPUT,
        IOS_STATS_TYPE_WRITE_THROUGHPUT,
        IOS_STATS_TYPE_MAX
}ios_stats_type_t;

typedef enum {
        IOS_STATS_THRU_READ,
        IOS_STATS_THRU_WRITE,
        IOS_STATS_THRU_MAX,
}ios_stats_thru_t;

struct ios_stat_lat {
        struct timeval  time;
        double          throughput;
};

struct ios_stat {
        gf_lock_t       lock;
        uuid_t          gfid;
        char           *filename;
        uint64_t        counters [IOS_STATS_TYPE_MAX];
        struct ios_stat_lat thru_counters [IOS_STATS_THRU_MAX];
        int             refcnt;
#if OFFSET_WRITE_DETECTOR
        off_t expected_write_offset;
#endif
};

struct ios_stat_list {
        struct list_head  list;
        struct ios_stat  *iosstat;
        double            value;
};

struct ios_stat_head {
       gf_lock_t                lock;
       double                   min_cnt;
       uint64_t                 members;
       struct ios_stat_list    *iosstats;
};

typedef struct _ios_sample_t {
        uid_t  uid;
        gid_t  gid;
        char   identifier[UNIX_PATH_MAX];
        glusterfs_fop_t fop_type;
        gf_fop_pri_t fop_pri;
        struct timeval timestamp;
        double elapsed;
        ns_info_t ns_info;
        char path[4096];
        gf_boolean_t have_path;
        int32_t op_ret;
        int32_t op_errno;
} ios_sample_t;


typedef struct _ios_sample_buf_t {
        uint64_t        pos;  /* Position in write buffer */
        uint64_t        size;  /* Size of ring buffer */
        uint64_t        collected;  /* Number of samples we've collected */
        uint64_t        observed;  /* Number of FOPs we've observed */
        ios_sample_t    *ios_samples;  /* Our list of samples */
} ios_sample_buf_t;


struct ios_lat {
        double      min;
        double      max;
        double      avg;
        uint64_t    total;
};

struct ios_global_stats {
        uint64_t        data_written;
        uint64_t        data_read;
        uint64_t        block_count_write[32];
        uint64_t        block_count_read[32];
        uint64_t        fop_hits[GF_FOP_MAXVALUE];
        struct timeval  started_at;
        struct ios_lat  latency[GF_FOP_MAXVALUE];
        uint64_t        errno_count[IOS_MAX_ERRORS];
        uint64_t        nr_opens;
        uint64_t        max_nr_opens;
        struct timeval  max_openfd_time;
#if OFFSET_WRITE_DETECTOR
        uint64_t        total_write_ops;
        uint64_t        offset_write_ops;
#endif
};

struct ios_conf {
        gf_lock_t                 lock;
        struct ios_global_stats   cumulative;
        uint64_t                  increment;
        struct ios_global_stats   incremental;
        gf_boolean_t              dump_fd_stats;
        gf_boolean_t              count_fop_hits;
        gf_boolean_t              measure_latency;
        struct ios_stat_head      list[IOS_STATS_TYPE_MAX];
        struct ios_stat_head      thru_list[IOS_STATS_THRU_MAX];
        int32_t                   ios_dump_interval;
        int32_t                   ns_rate_window;
        pthread_t                 dump_thread;
        gf_boolean_t              dump_thread_should_die;
        gf_lock_t                 ios_sampling_lock;
        int32_t                   ios_sample_interval;
        int32_t                   ios_sample_buf_size;
        ios_sample_buf_t          *ios_sample_buf;
        struct dnscache           *dnscache;
        int32_t                   ios_dnscache_ttl_sec;
        dict_t                    *hash_to_ns;       /* Hash -> NS name */
        dict_t                    *fop_hits_dict;    /* NS -> Real rate */
        dict_t                    *throttling_rates; /* NS -> Max rate */
        gf_boolean_t              iamgfproxyd;
        gf_boolean_t              iamnfsd;
        gf_boolean_t              iamshd;
        gf_boolean_t              iambrickd;
        gf_boolean_t              throttling_enabled;
        time_t                    namespace_conf_mtime;
        gf_boolean_t              measure_namespace_rates;
        gf_boolean_t              audit_creates_and_unlinks;
        gf_boolean_t              sample_hard_errors;
        gf_boolean_t              dump_percentile_latencies;
        gf_boolean_t              sample_all_errors;
        uint32_t                  outstanding_req;
};


struct ios_fd {
        char           *filename;
        uint64_t        data_written;
        uint64_t        data_read;
        uint64_t        block_count_write[32];
        uint64_t        block_count_read[32];
        struct timeval  opened_at;
};

typedef enum {
        IOS_DUMP_TYPE_NONE      = 0,
        IOS_DUMP_TYPE_FILE      = 1,
        IOS_DUMP_TYPE_DICT      = 2,
        IOS_DUMP_TYPE_JSON_FILE = 3,
        IOS_DUMP_TYPE_SAMPLES   = 4,
        IOS_DUMP_TYPE_MAX       = 5
} ios_dump_type_t;

struct ios_dump_args {
        xlator_t *this;
        ios_dump_type_t type;
        union {
                FILE *logfp;
                dict_t *dict;
        } u;
};

typedef int (*block_dump_func) (xlator_t *, struct ios_dump_args*,
                                    int, int, uint64_t);

// Will be present in frame->local
struct ios_local {
        inode_t *inode;
        loc_t loc;
        fd_t *fd;
};

#define ios_local_new() GF_CALLOC (1, sizeof (struct ios_local), gf_common_mt_char);

void
ios_local_free (struct ios_local *local)
{
        if (!local)
                return;

        inode_unref (local->inode);

        if (local->fd)
                fd_unref (local->fd);

        loc_wipe (&local->loc);
        memset (local, 0, sizeof (*local));
        GF_FREE (local);
}

struct volume_options options[];

inline static int
is_fop_latency_started (call_frame_t *frame)
{
        GF_ASSERT (frame);
        struct timeval epoch = {0, };
        return memcmp (&frame->begin, &epoch, sizeof (epoch));
}

const char *_IOS_DUMP_DIR = "/var/lib/glusterd/stats";
const char *_IOS_SAMP_DIR = "/var/log/glusterfs/samples";
const char *_IOS_NAMESPACE_CONF = TOSTRING (GLUSTERD_WORKDIR) "/namespaces.conf";

extern const char *__progname;

/* This is a list of errors which are in some way critical.
 * It is useful to sample these errors even if other errors
 * should be ignored. */
const int32_t ios_hard_error_list[] = {
        EIO,
        EROFS,
        ENOSPC,
        ENOTCONN,
        ESTALE,
};

#define IOS_HARD_ERROR_LIST_SIZE (sizeof(ios_hard_error_list) / sizeof(int32_t))

const char *errno_to_name[IOS_MAX_ERRORS] = {
      "success",       /* 0 */
      "eperm",
      "enoent",
      "esrch",
      "eintr",
      "eio",
      "enxio",
      "e2big",
      "enoexec",
      "ebadf",
      "echild",
      "eagain",
      "enomem",
      "eacces",
      "efault",
      "enotblk",
      "ebusy",
      "eexist",
      "exdev",
      "enodev",
      "enotdir",
      "eisdir",
      "einval",
      "enfile",
      "emfile",
      "enotty",
      "etxtbsy",
      "efbig",
      "enospc",
      "espipe",
      "erofs",
      "emlink",
      "epipe",
      "edom",
      "erange",
      "edeadlk",
      "enametoolong",
      "enolck",
      "enosys",
      "enotempty",
      "eloop",
      "ewouldblock",
      "enomsg",
      "eidrm",
      "echrng",
      "el2nsync",
      "el3hlt",
      "el3rst",
      "elnrng",
      "eunatch",
      "enocsi",
      "el2hlt",
      "ebade",
      "ebadr",
      "exfull",
      "enoano",
      "ebadrqc",
      "ebadslt",
      "edeadlock",
      "ebfont",
      "enostr",
      "enodata",
      "etime",
      "enosr",
      "enonet",
      "enopkg",
      "eremote",
      "enolink",
      "eadv",
      "esrmnt",
      "ecomm",
      "eproto",
      "emultihop",
      "edotdot",
      "ebadmsg",
      "eoverflow",
      "enotuniq",
      "ebadfd",
      "eremchg",
      "elibacc",
      "elibbad",
      "elibscn",
      "elibmax",
      "elibexec",
      "eilseq",
      "erestart",
      "estrpipe",
      "eusers",
      "enotsock",
      "edestaddrreq",
      "emsgsize",
      "eprototype",
      "enoprotoopt",
      "eprotonosupport",
      "esocktnosupport",
      "eopnotsupp",
      "epfnosupport",
      "eafnosupport",
      "eaddrinuse",
      "eaddrnotavail",
      "enetdown",
      "enetunreach",
      "enetreset",
      "econnaborted",
      "econnreset",
      "enobufs",
      "eisconn",
      "enotconn",
      "eshutdown",
      "etoomanyrefs",
      "etimedout",
      "econnrefused",
      "ehostdown",
      "ehostunreach",
      "ealready",
      "einprogress",
      "estale",
      "euclean",
      "enotnam",
      "enavail",
      "eisnam",
      "eremoteio",
      "edquot",
      "enomedium",
      "emediumtype",
      "ecanceled",
      "enokey",
      "ekeyexpired",
      "ekeyrevoked",
      "ekeyrejected",
      "eownerdead",
      "enotrecoverable"
};

#endif /* _IO_STATS_H_ */
