/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include <glusterfs/xlator.h>
#include <glusterfs/syscall.h>

/**
 * xlators/debug/io_stats :
 *    This translator maintains statistics of all filesystem activity
 *    happening through it. The kind of statistics include:
 *
 *  a) total read data - since process start, last interval and per fd
 *  b) total write data - since process start, last interval and per fd
 *  c) counts of read IO block size - since process start, last interval and per
 * fd d) counts of write IO block size - since process start, last interval and
 * per fd e) counts of all FOP types passing through it
 *
 *  Usage: setfattr -n trusted.io-stats-dump /tmp/filename /mnt/gluster
 *      output is written to /tmp/filename.<iostats xlator instance name>
 *
 */

#include <fnmatch.h>
#include <errno.h>
#include <glusterfs/glusterfs.h>
#include <glusterfs/xlator.h>
#include "io-stats-mem-types.h"
#include <stdarg.h>
#include <glusterfs/defaults.h>
#include <glusterfs/logging.h>
#include <glusterfs/statedump.h>
#include <glusterfs/syncop.h>
#include <pwd.h>
#include <grp.h>
#include <glusterfs/upcall-utils.h>
#include <glusterfs/async.h>

#define MAX_LIST_MEMBERS 100
#define DEFAULT_PWD_BUF_SZ 16384
#define DEFAULT_GRP_BUF_SZ 16384
#define IOS_BLOCK_COUNT_SIZE 32

#define IOS_STATS_DUMP_DIR DEFAULT_VAR_RUN_DIRECTORY

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
} ios_stats_type_t;

typedef enum {
    IOS_STATS_THRU_READ,
    IOS_STATS_THRU_WRITE,
    IOS_STATS_THRU_MAX,
} ios_stats_thru_t;

/* This is same as gf1_cli_info_op */
/* had to be defined here again, so we have modularity between
 xdr, xlator, and library functions */
typedef enum ios_info_op {
    GF_IOS_INFO_NONE = 0,
    GF_IOS_INFO_ALL = 1,
    GF_IOS_INFO_INCREMENTAL = 2,
    GF_IOS_INFO_CUMULATIVE = 3,
    GF_IOS_INFO_CLEAR = 4,
} ios_info_op_t;

struct ios_stat_lat {
    struct timeval time;
    double throughput;
};

struct ios_stat {
    gf_lock_t lock;
    uuid_t gfid;
    char *filename;
    gf_atomic_t counters[IOS_STATS_TYPE_MAX];
    struct ios_stat_lat thru_counters[IOS_STATS_THRU_MAX];
    gf_atomic_t refcnt;
};

struct ios_stat_list {
    struct list_head list;
    struct ios_stat *iosstat;
    double value;
};

struct ios_stat_head {
    gf_lock_t lock;
    double min_cnt;
    uint64_t members;
    struct ios_stat_list *iosstats;
};

typedef struct _ios_sample_t {
    uid_t uid;
    gid_t gid;
    char identifier[UNIX_PATH_MAX];
    glusterfs_fop_t fop_type;
    struct timeval timestamp;
    double elapsed;
} ios_sample_t;

typedef struct _ios_sample_buf_t {
    uint64_t pos;              /* Position in write buffer */
    uint64_t size;             /* Size of ring buffer */
    uint64_t collected;        /* Number of samples we've collected */
    uint64_t observed;         /* Number of FOPs we've observed */
    ios_sample_t *ios_samples; /* Our list of samples */
} ios_sample_buf_t;

struct ios_lat {
    double min;
    double max;
    double avg;
    uint64_t total;
};

struct ios_global_stats {
    gf_atomic_t data_written;
    gf_atomic_t data_read;
    gf_atomic_t block_count_write[IOS_BLOCK_COUNT_SIZE];
    gf_atomic_t block_count_read[IOS_BLOCK_COUNT_SIZE];
    gf_atomic_t fop_hits[GF_FOP_MAXVALUE];
    gf_atomic_t upcall_hits[GF_UPCALL_FLAGS_MAXVALUE];
    time_t started_at;
    struct ios_lat latency[GF_FOP_MAXVALUE];
    uint64_t nr_opens;
    uint64_t max_nr_opens;
    struct timeval max_openfd_time;
};

typedef enum {
    IOS_DUMP_TYPE_NONE = 0,
    IOS_DUMP_TYPE_FILE = 1,
    IOS_DUMP_TYPE_DICT = 2,
    IOS_DUMP_TYPE_JSON_FILE = 3,
    IOS_DUMP_TYPE_SAMPLES = 4,
    IOS_DUMP_TYPE_MAX = 5
} ios_dump_type_t;

struct ios_conf {
    gf_lock_t lock;
    struct ios_global_stats cumulative;
    uint64_t increment;
    struct ios_global_stats incremental;
    gf_boolean_t dump_fd_stats;
    gf_boolean_t count_fop_hits;
    gf_boolean_t measure_latency;
    struct ios_stat_head list[IOS_STATS_TYPE_MAX];
    struct ios_stat_head thru_list[IOS_STATS_THRU_MAX];
    int32_t ios_dump_interval;
    pthread_t dump_thread;
    gf_boolean_t dump_thread_should_die;
    gf_boolean_t dump_thread_running;
    gf_lock_t ios_sampling_lock;
    int32_t ios_sample_interval;
    int32_t ios_sample_buf_size;
    ios_sample_buf_t *ios_sample_buf;
    struct dnscache *dnscache;
    int32_t ios_dnscache_ttl_sec;
    /*
     * What we really need here is just a unique value to keep files
     * created by this instance distinct from those created by any other.
     * On the client side this isn't a problem, so we just use the
     * translator name.  On the server side conflicts can occur, so the
     * volfile-generation code automatically sets this (via an option)
     * to be the brick path.
     *
     * NB While the *field* name has changed, it didn't seem worth changing
     * all of the cases where "xlator_name" is used as a *variable* name.
     */
    char *unique_id;
    ios_dump_type_t dump_format;
};

struct ios_fd {
    char *filename;
    gf_atomic_t data_written;
    gf_atomic_t data_read;
    gf_atomic_t block_count_write[IOS_BLOCK_COUNT_SIZE];
    gf_atomic_t block_count_read[IOS_BLOCK_COUNT_SIZE];
    struct timeval opened_at;
};

struct ios_dump_args {
    ios_dump_type_t type;
    union {
        FILE *logfp;
        dict_t *dict;
    } u;
};

typedef int (*block_dump_func)(xlator_t *, struct ios_dump_args *, int, int,
                               uint64_t);

struct ios_local {
    struct timeval wind_at;
    struct timeval unwind_at;
};

struct volume_options options[];

static int
is_fop_latency_started(call_frame_t *frame)
{
    GF_ASSERT(frame);
    struct timeval epoch = {
        0,
    };
    return memcmp(&frame->begin, &epoch, sizeof(epoch));
}

#define _IOS_SAMP_DIR DEFAULT_LOG_FILE_DIRECTORY "/samples"
#ifdef GF_LINUX_HOST_OS
#define _IOS_DUMP_DIR DATADIR "/lib/glusterd/stats"
#else
#define _IOS_DUMP_DIR DATADIR "/db/glusterd/stats"
#endif

#define END_FOP_LATENCY(frame, op)                                             \
    do {                                                                       \
        struct ios_conf *conf = NULL;                                          \
                                                                               \
        conf = this->private;                                                  \
        if (conf && conf->measure_latency) {                                   \
            timespec_now(&frame->end);                                         \
            update_ios_latency(conf, frame, GF_FOP_##op);                      \
        }                                                                      \
    } while (0)

#define START_FOP_LATENCY(frame)                                               \
    do {                                                                       \
        struct ios_conf *conf = NULL;                                          \
                                                                               \
        conf = this->private;                                                  \
        if (conf && conf->measure_latency) {                                   \
            timespec_now(&frame->begin);                                       \
        } else {                                                               \
            memset(&frame->begin, 0, sizeof(frame->begin));                    \
        }                                                                      \
    } while (0)

#define BUMP_FOP(op)                                                           \
    do {                                                                       \
        struct ios_conf *conf = NULL;                                          \
                                                                               \
        conf = this->private;                                                  \
        if (!conf)                                                             \
            break;                                                             \
        GF_ATOMIC_INC(conf->cumulative.fop_hits[GF_FOP_##op]);                 \
        GF_ATOMIC_INC(conf->incremental.fop_hits[GF_FOP_##op]);                \
    } while (0)

#define UPDATE_PROFILE_STATS(frame, op)                                        \
    do {                                                                       \
        struct ios_conf *conf = NULL;                                          \
                                                                               \
        if (!is_fop_latency_started(frame))                                    \
            break;                                                             \
        conf = this->private;                                                  \
        if (conf && conf->measure_latency && conf->count_fop_hits) {           \
            BUMP_FOP(op);                                                      \
            timespec_now(&frame->end);                                         \
            update_ios_latency(conf, frame, GF_FOP_##op);                      \
        }                                                                      \
    } while (0)

#define BUMP_THROUGHPUT(iosstat, type)                                         \
    do {                                                                       \
        struct ios_conf *conf = NULL;                                          \
        double elapsed;                                                        \
        struct timespec *begin, *end;                                          \
        double throughput;                                                     \
        int flag = 0;                                                          \
        struct timeval tv = {                                                  \
            0,                                                                 \
        };                                                                     \
                                                                               \
        begin = &frame->begin;                                                 \
        end = &frame->end;                                                     \
                                                                               \
        elapsed = gf_tsdiff(begin, end) / 1000.0;                              \
        throughput = op_ret / elapsed;                                         \
                                                                               \
        conf = this->private;                                                  \
        gettimeofday(&tv, NULL);                                               \
        LOCK(&iosstat->lock);                                                  \
        {                                                                      \
            if (iosstat->thru_counters[type].throughput <= throughput) {       \
                iosstat->thru_counters[type].throughput = throughput;          \
                memcpy(&iosstat->thru_counters[type].time, &tv,                \
                       sizeof(struct timeval));                                \
                flag = 1;                                                      \
            }                                                                  \
        }                                                                      \
        UNLOCK(&iosstat->lock);                                                \
        if (flag)                                                              \
            ios_stat_add_to_list(&conf->thru_list[type], throughput, iosstat); \
    } while (0)

static int
ios_fd_ctx_get(fd_t *fd, xlator_t *this, struct ios_fd **iosfd)
{
    uint64_t iosfd64 = 0;
    unsigned long iosfdlong = 0;
    int ret = 0;

    ret = fd_ctx_get(fd, this, &iosfd64);
    iosfdlong = iosfd64;
    if (ret != -1)
        *iosfd = (void *)iosfdlong;

    return ret;
}

static int
ios_fd_ctx_set(fd_t *fd, xlator_t *this, struct ios_fd *iosfd)
{
    uint64_t iosfd64 = 0;
    int ret = 0;

    iosfd64 = (unsigned long)iosfd;
    ret = fd_ctx_set(fd, this, iosfd64);

    return ret;
}

static int
ios_stat_ref(struct ios_stat *iosstat)
{
    uint64_t refcnt = 0;
    refcnt = GF_ATOMIC_INC(iosstat->refcnt);

    return refcnt;
}

static int
ios_stat_unref(struct ios_stat *iosstat)
{
    int cleanup = 0;
    uint64_t refcnt = 0;

    refcnt = GF_ATOMIC_DEC(iosstat->refcnt);
    if (refcnt == 0) {
        if (iosstat->filename) {
            GF_FREE(iosstat->filename);
            iosstat->filename = NULL;
        }
        cleanup = 1;
    }

    if (cleanup) {
        LOCK_DESTROY(&iosstat->lock);
        GF_FREE(iosstat);
        iosstat = NULL;
    }

    return 0;
}

static int
ios_stat_add_to_list(struct ios_stat_head *list_head, uint64_t value,
                     struct ios_stat *iosstat)
{
    struct ios_stat_list *new = NULL;
    struct ios_stat_list *entry = NULL;
    struct ios_stat_list *t = NULL;
    struct ios_stat_list *list_entry = NULL;
    struct ios_stat_list *tmp = NULL;
    struct ios_stat_list *last = NULL;
    struct ios_stat *stat = NULL;
    int cnt = 0;
    int found = 0;
    int reposition = 0;
    double min_count = 0;

    LOCK(&list_head->lock);
    {
        if (list_head->min_cnt == 0)
            list_head->min_cnt = value;
        if ((list_head->members == MAX_LIST_MEMBERS) &&
            (list_head->min_cnt > value))
            goto out;

        list_for_each_entry_safe(entry, t, &list_head->iosstats->list, list)
        {
            cnt++;
            if (cnt == list_head->members)
                last = entry;

            if (!gf_uuid_compare(iosstat->gfid, entry->iosstat->gfid)) {
                list_entry = entry;
                found = cnt;
                entry->value = value;
                if (!reposition) {
                    if (cnt == list_head->members)
                        list_head->min_cnt = value;
                    goto out;
                }
                break;
            } else if (entry->value <= value && !reposition) {
                reposition = cnt;
                tmp = entry;
                if (cnt == list_head->members - 1)
                    min_count = entry->value;
            }
        }
        if (found) {
            list_del(&list_entry->list);
            list_add_tail(&list_entry->list, &tmp->list);
            if (min_count)
                list_head->min_cnt = min_count;
            goto out;
        } else if (list_head->members == MAX_LIST_MEMBERS && reposition) {
            new = GF_CALLOC(1, sizeof(*new), gf_io_stats_mt_ios_stat_list);
            new->iosstat = iosstat;
            new->value = value;
            ios_stat_ref(iosstat);
            list_add_tail(&new->list, &tmp->list);
            if (last) {
                stat = last->iosstat;
                last->iosstat = NULL;
                ios_stat_unref(stat);
                list_del(&last->list);
                GF_FREE(last);
            }
            if (reposition == MAX_LIST_MEMBERS)
                list_head->min_cnt = value;
            else if (min_count) {
                list_head->min_cnt = min_count;
            }
        } else if (list_head->members < MAX_LIST_MEMBERS) {
            new = GF_CALLOC(1, sizeof(*new), gf_io_stats_mt_ios_stat_list);
            new->iosstat = iosstat;
            new->value = value;
            ios_stat_ref(iosstat);
            if (reposition) {
                list_add_tail(&new->list, &tmp->list);
            } else {
                list_add_tail(&new->list, &entry->list);
            }
            list_head->members++;
            if (list_head->min_cnt > value)
                list_head->min_cnt = value;
        }
    }
out:
    UNLOCK(&list_head->lock);
    return 0;
}

static void
ios_bump_read(xlator_t *this, fd_t *fd, size_t len)
{
    struct ios_conf *conf = NULL;
    struct ios_fd *iosfd = NULL;
    int lb2 = 0;

    conf = this->private;
    lb2 = log_base2(len);
    ios_fd_ctx_get(fd, this, &iosfd);
    if (!conf)
        return;

    GF_ATOMIC_ADD(conf->cumulative.data_read, len);
    GF_ATOMIC_ADD(conf->incremental.data_read, len);
    GF_ATOMIC_INC(conf->cumulative.block_count_read[lb2]);
    GF_ATOMIC_INC(conf->incremental.block_count_read[lb2]);

    if (iosfd) {
        GF_ATOMIC_ADD(iosfd->data_read, len);
        GF_ATOMIC_INC(iosfd->block_count_read[lb2]);
    }
}

static void
ios_bump_write(xlator_t *this, fd_t *fd, size_t len)
{
    struct ios_conf *conf = NULL;
    struct ios_fd *iosfd = NULL;
    int lb2 = 0;

    conf = this->private;
    lb2 = log_base2(len);
    ios_fd_ctx_get(fd, this, &iosfd);
    if (!conf)
        return;

    GF_ATOMIC_ADD(conf->cumulative.data_written, len);
    GF_ATOMIC_ADD(conf->incremental.data_written, len);
    GF_ATOMIC_INC(conf->cumulative.block_count_write[lb2]);
    GF_ATOMIC_INC(conf->incremental.block_count_write[lb2]);

    if (iosfd) {
        GF_ATOMIC_ADD(iosfd->data_written, len);
        GF_ATOMIC_INC(iosfd->block_count_write[lb2]);
    }
}

static void
ios_bump_upcall(xlator_t *this, gf_upcall_flags_t event)
{
    struct ios_conf *conf = NULL;

    conf = this->private;
    if (!conf)
        return;
    if (conf->count_fop_hits) {
        GF_ATOMIC_INC(conf->cumulative.upcall_hits[event]);
        GF_ATOMIC_INC(conf->incremental.upcall_hits[event]);
    }
}

static void
ios_bump_stats(xlator_t *this, struct ios_stat *iosstat, ios_stats_type_t type)
{
    struct ios_conf *conf = NULL;
    uint64_t value = 0;

    conf = this->private;

    value = GF_ATOMIC_INC(iosstat->counters[type]);
    ios_stat_add_to_list(&conf->list[type], value, iosstat);
}

int
ios_inode_ctx_set(inode_t *inode, xlator_t *this, struct ios_stat *iosstat)
{
    uint64_t iosstat64 = 0;
    int ret = 0;

    ios_stat_ref(iosstat);
    iosstat64 = (unsigned long)iosstat;
    ret = inode_ctx_put(inode, this, iosstat64);
    return ret;
}

int
ios_inode_ctx_get(inode_t *inode, xlator_t *this, struct ios_stat **iosstat)
{
    uint64_t iosstat64 = 0;
    unsigned long iosstatlong = 0;
    int ret = 0;

    ret = inode_ctx_get(inode, this, &iosstat64);
    iosstatlong = iosstat64;
    if (ret != -1)
        *iosstat = (void *)iosstatlong;

    return ret;
}

/*
 * So why goto all this trouble?  Why not just queue up some samples in
 * a big list and malloc away?  Well malloc is expensive relative
 * to what we are measuring, so cannot have any malloc's (or worse
 * callocs) in our measurement code paths.  Instead, we are going to
 * pre-allocate a circular buffer and collect a maximum number of samples.
 * Prior to dumping them all we'll create a new buffer and swap the
 * old buffer with the new, and then proceed to dump the statistics
 * in our dump thread.
 *
 */
ios_sample_buf_t *
ios_create_sample_buf(size_t buf_size)
{
    ios_sample_buf_t *ios_sample_buf = NULL;
    ios_sample_t *ios_samples = NULL;

    ios_sample_buf = GF_CALLOC(1, sizeof(*ios_sample_buf),
                               gf_io_stats_mt_ios_sample_buf);
    if (!ios_sample_buf)
        goto err;

    ios_samples = GF_CALLOC(buf_size, sizeof(*ios_samples),
                            gf_io_stats_mt_ios_sample);

    if (!ios_samples)
        goto err;

    ios_sample_buf->ios_samples = ios_samples;
    ios_sample_buf->size = buf_size;
    ios_sample_buf->pos = 0;
    ios_sample_buf->observed = 0;
    ios_sample_buf->collected = 0;

    return ios_sample_buf;
err:
    GF_FREE(ios_sample_buf);
    return NULL;
}

void
ios_destroy_sample_buf(ios_sample_buf_t *ios_sample_buf)
{
    GF_FREE(ios_sample_buf->ios_samples);
    GF_FREE(ios_sample_buf);
}

static int
ios_init_sample_buf(struct ios_conf *conf)
{
    int32_t ret = -1;

    GF_ASSERT(conf);
    LOCK(&conf->lock);
    conf->ios_sample_buf = ios_create_sample_buf(conf->ios_sample_buf_size);
    if (!conf->ios_sample_buf)
        goto out;
    ret = 0;
out:
    UNLOCK(&conf->lock);
    return ret;
}

static int
ios_stats_cleanup(xlator_t *this, inode_t *inode)
{
    struct ios_stat *iosstat = NULL;
    uint64_t iosstat64 = 0;

    inode_ctx_del(inode, this, &iosstat64);
    if (!iosstat64) {
        gf_log(this->name, GF_LOG_WARNING, "could not get inode ctx");
        return 0;
    }
    iosstat = (void *)(long)iosstat64;
    if (iosstat) {
        ios_stat_unref(iosstat);
    }
    return 0;
}

#define ios_log(this, logfp, fmt...)                                           \
    do {                                                                       \
        if (logfp) {                                                           \
            fprintf(logfp, fmt);                                               \
            fprintf(logfp, "\n");                                              \
        }                                                                      \
        gf_log(this->name, GF_LOG_DEBUG, fmt);                                 \
    } while (0)

int
ios_dump_file_stats(struct ios_stat_head *list_head, xlator_t *this,
                    FILE *logfp)
{
    struct ios_stat_list *entry = NULL;

    LOCK(&list_head->lock);
    {
        list_for_each_entry(entry, &list_head->iosstats->list, list)
        {
            ios_log(this, logfp, "%-12.0f %s", entry->value,
                    entry->iosstat->filename);
        }
    }
    UNLOCK(&list_head->lock);
    return 0;
}

int
ios_dump_throughput_stats(struct ios_stat_head *list_head, xlator_t *this,
                          FILE *logfp, ios_stats_thru_t type)
{
    struct ios_stat_list *entry = NULL;
    char timestr[GF_TIMESTR_SIZE] = {
        0,
    };

    LOCK(&list_head->lock);
    {
        list_for_each_entry(entry, &list_head->iosstats->list, list)
        {
            gf_time_fmt_tv(timestr, sizeof timestr,
                           &entry->iosstat->thru_counters[type].time,
                           gf_timefmt_FT);

            ios_log(this, logfp, "%s \t %-10.2f  \t  %s", timestr, entry->value,
                    entry->iosstat->filename);
        }
    }
    UNLOCK(&list_head->lock);
    return 0;
}

int
_io_stats_get_key_prefix(xlator_t *this, char **key_prefix)
{
    char *key_root = "gluster";
    char *xlator_name = NULL;
    char *instance_name = NULL;
    size_t key_len = 0;
    int bytes_written = 0;
    int i = 0;
    int ret = 0;
    struct ios_conf *conf = this->private;

    xlator_name = strdupa(conf->unique_id);
    for (i = 0; i < strlen(xlator_name); i++) {
        if (xlator_name[i] == '/')
            xlator_name[i] = '_';
    }

    instance_name = this->instance_name;
    if (this->name && strcmp(this->name, "glustershd") == 0) {
        xlator_name = "shd";
    } else if (this->prev && strcmp(this->prev->name, "nfs-server") == 0) {
        xlator_name = "nfsd";
        if (this->prev->instance_name)
            instance_name = strdupa(this->prev->instance_name);
    }

    if (strcmp(__progname, "glusterfsd") == 0)
        key_root = "gluster.brick";

    if (instance_name) {
        /* +3 for 2 x "." + NULL */
        key_len = strlen(key_root) + strlen(xlator_name) +
                  strlen(instance_name) + 3;
        *key_prefix = GF_CALLOC(key_len, sizeof(char), gf_common_mt_char);
        if (!*key_prefix) {
            ret = -ENOMEM;
            goto err;
        }
        bytes_written = snprintf(*key_prefix, key_len, "%s.%s.%s", key_root,
                                 xlator_name, instance_name);
        if (bytes_written != key_len - 1) {
            ret = -EINVAL;
            goto err;
        }
    } else {
        /* +2 for 1 x "." + NULL */
        key_len = strlen(key_root) + strlen(xlator_name) + 2;
        *key_prefix = GF_CALLOC(key_len, sizeof(char), gf_common_mt_char);
        if (!*key_prefix) {
            ret = -ENOMEM;
            goto err;
        }
        bytes_written = snprintf(*key_prefix, key_len, "%s.%s", key_root,
                                 xlator_name);
        if (bytes_written != key_len - 1) {
            ret = -EINVAL;
            goto err;
        }
    }
    return 0;
err:
    GF_FREE(*key_prefix);
    *key_prefix = NULL;
    return ret;
}

int
io_stats_dump_global_to_json_logfp(xlator_t *this,
                                   struct ios_global_stats *stats, time_t now,
                                   int interval, FILE *logfp)
{
    int i = 0;
    int j = 0;
    struct ios_conf *conf = NULL;
    char *key_prefix = NULL;
    char *str_prefix = NULL;
    char *lc_fop_name = NULL;
    int ret = 1; /* Default to error */
    int rw_size;
    char *rw_unit = NULL;
    uint64_t fop_hits;
    float fop_lat_ave;
    float fop_lat_min;
    float fop_lat_max;
    double interval_sec;
    double fop_ave_usec = 0.0;
    double fop_ave_usec_sum = 0.0;
    double weighted_fop_ave_usec = 0.0;
    double weighted_fop_ave_usec_sum = 0.0;
    long total_fop_hits = 0;
    loc_t unused_loc = {
        0,
    };
    dict_t *xattr = NULL;

    interval_sec = (double)(now - stats->started_at);

    conf = this->private;

    ret = _io_stats_get_key_prefix(this, &key_prefix);
    if (ret) {
        goto out;
    }

    if (interval == -1) {
        str_prefix = "aggr";

    } else {
        str_prefix = "inter";
    }
    ios_log(this, logfp, "{");

    for (i = 0; i < 31; i++) {
        rw_size = (1 << i);
        if (rw_size >= 1024 * 1024) {
            rw_size = rw_size / (1024 * 1024);
            rw_unit = "mb";
        } else if (rw_size >= 1024) {
            rw_size = rw_size / 1024;
            rw_unit = "kb";
        } else {
            rw_unit = "b";
        }

        if (interval == -1) {
            ios_log(this, logfp, "\"%s.%s.read_%d%s\": %" GF_PRI_ATOMIC ",",
                    key_prefix, str_prefix, rw_size, rw_unit,
                    GF_ATOMIC_GET(stats->block_count_read[i]));
            ios_log(this, logfp, "\"%s.%s.write_%d%s\": %" GF_PRI_ATOMIC ",",
                    key_prefix, str_prefix, rw_size, rw_unit,
                    GF_ATOMIC_GET(stats->block_count_write[i]));
        } else {
            ios_log(this, logfp, "\"%s.%s.read_%d%s_per_sec\": %0.2lf,",
                    key_prefix, str_prefix, rw_size, rw_unit,
                    (double)(GF_ATOMIC_GET(stats->block_count_read[i]) /
                             interval_sec));
            ios_log(this, logfp, "\"%s.%s.write_%d%s_per_sec\": %0.2lf,",
                    key_prefix, str_prefix, rw_size, rw_unit,
                    (double)(GF_ATOMIC_GET(stats->block_count_write[i]) /
                             interval_sec));
        }
    }

    if (interval == -1) {
        ios_log(this, logfp, "\"%s.%s.fds.open_count\": %" PRId64 ",",
                key_prefix, str_prefix, conf->cumulative.nr_opens);
        ios_log(this, logfp, "\"%s.%s.fds.max_open_count\": %" PRId64 ",",
                key_prefix, str_prefix, conf->cumulative.max_nr_opens);
    }

    for (i = 0; i < GF_FOP_MAXVALUE; i++) {
        lc_fop_name = strdupa(gf_fop_list[i]);
        for (j = 0; lc_fop_name[j]; j++) {
            lc_fop_name[j] = tolower(lc_fop_name[j]);
        }

        fop_hits = GF_ATOMIC_GET(stats->fop_hits[i]);
        fop_lat_ave = 0.0;
        fop_lat_min = 0.0;
        fop_lat_max = 0.0;
        if (fop_hits) {
            if (stats->latency[i].avg) {
                fop_lat_ave = stats->latency[i].avg;
                fop_lat_min = stats->latency[i].min;
                fop_lat_max = stats->latency[i].max;
            }
        }
        if (interval == -1) {
            ios_log(this, logfp, "\"%s.%s.fop.%s.count\": %" GF_PRI_ATOMIC ",",
                    key_prefix, str_prefix, lc_fop_name, fop_hits);
        } else {
            ios_log(this, logfp, "\"%s.%s.fop.%s.per_sec\": %0.2lf,",
                    key_prefix, str_prefix, lc_fop_name,
                    (double)(fop_hits / interval_sec));
        }

        ios_log(this, logfp, "\"%s.%s.fop.%s.latency_ave_usec\": %0.2lf,",
                key_prefix, str_prefix, lc_fop_name, fop_lat_ave);
        ios_log(this, logfp, "\"%s.%s.fop.%s.latency_min_usec\": %0.2lf,",
                key_prefix, str_prefix, lc_fop_name, fop_lat_min);
        ios_log(this, logfp, "\"%s.%s.fop.%s.latency_max_usec\": %0.2lf,",
                key_prefix, str_prefix, lc_fop_name, fop_lat_max);

        fop_ave_usec_sum += fop_lat_ave;
        weighted_fop_ave_usec_sum += fop_hits * fop_lat_ave;
        total_fop_hits += fop_hits;
    }

    if (total_fop_hits) {
        weighted_fop_ave_usec = weighted_fop_ave_usec_sum / total_fop_hits;
        /* Extra key that does not print out an entry w/ 0.00 for
         * intervals with no data
         */
        ios_log(this, logfp,
                "\"%s.%s.fop.weighted_latency_ave_usec_nozerofill\": "
                "%0.4lf,",
                key_prefix, str_prefix, weighted_fop_ave_usec);
    }
    ios_log(this, logfp, "\"%s.%s.fop.weighted_latency_ave_usec\": %0.4lf,",
            key_prefix, str_prefix, weighted_fop_ave_usec);
    ios_log(this, logfp, "\"%s.%s.fop.weighted_fop_count\": %ld,", key_prefix,
            str_prefix, total_fop_hits);

    fop_ave_usec = fop_ave_usec_sum / GF_FOP_MAXVALUE;
    ios_log(this, logfp, "\"%s.%s.fop.unweighted_latency_ave_usec\":%0.4lf,",
            key_prefix, str_prefix, fop_ave_usec);

    for (i = 0; i < GF_UPCALL_FLAGS_MAXVALUE; i++) {
        lc_fop_name = strdupa(gf_upcall_list[i]);
        for (j = 0; lc_fop_name[j]; j++) {
            lc_fop_name[j] = tolower(lc_fop_name[j]);
        }
        fop_hits = GF_ATOMIC_GET(stats->upcall_hits[i]);
        if (interval == -1) {
            ios_log(this, logfp, "\"%s.%s.fop.%s.count\": %" GF_PRI_ATOMIC ",",
                    key_prefix, str_prefix, lc_fop_name, fop_hits);
        } else {
            ios_log(this, logfp, "\"%s.%s.fop.%s.per_sec\": %0.2lf,",
                    key_prefix, str_prefix, lc_fop_name,
                    (double)(fop_hits / interval_sec));
        }
    }

    ret = syncop_getxattr(this, &unused_loc, &xattr, IO_THREADS_QUEUE_SIZE_KEY,
                          NULL, NULL);
    if (xattr) {
        /*
         * Iterate over the dictionary returned to us by io-threads and
         * dump the results to the stats file.
         */
        data_pair_t *curr = NULL;

        dict_foreach_inline(xattr, curr)
        {
            ios_log(this, logfp, "\"%s.%s.%s.queue_size\": %d,", key_prefix,
                    str_prefix, curr->key, data_to_int32(curr->value));
        }

        /* Free the dictionary */
        dict_unref(xattr);
    } else {
        gf_log(this->name, GF_LOG_WARNING,
               "Unable to get queue size counts from "
               "the io-threads translator!");
    }

    if (interval == -1) {
        ios_log(this, logfp, "\"%s.%s.uptime\": %" PRIu64 ",", key_prefix,
                str_prefix, (uint64_t)(now - stats->started_at));
        ios_log(this, logfp,
                "\"%s.%s.bytes_read\": "
                "%" GF_PRI_ATOMIC ",",
                key_prefix, str_prefix, GF_ATOMIC_GET(stats->data_read));
        ios_log(this, logfp,
                "\"%s.%s.bytes_written\": "
                "%" GF_PRI_ATOMIC "",
                key_prefix, str_prefix, GF_ATOMIC_GET(stats->data_written));
    } else {
        ios_log(this, logfp, "\"%s.%s.sample_interval_sec\": %0.2lf,",
                key_prefix, str_prefix, interval_sec);
        ios_log(this, logfp, "\"%s.%s.bytes_read_per_sec\": %0.2lf,",
                key_prefix, str_prefix,
                (double)(GF_ATOMIC_GET(stats->data_read) / interval_sec));
        ios_log(this, logfp, "\"%s.%s.bytes_written_per_sec\": %0.2lf",
                key_prefix, str_prefix,
                (double)(GF_ATOMIC_GET(stats->data_written) / interval_sec));
    }

    ios_log(this, logfp, "}");
    ret = 0;
out:
    GF_FREE(key_prefix);
    return ret;
}

char *
_resolve_username(xlator_t *this, uid_t uid)
{
    struct passwd pwd;
    struct passwd *pwd_result = NULL;
    size_t pwd_buf_len;
    char *pwd_buf = NULL;
    char *ret = NULL;

    /* Prepare our buffer for the uid->username translation */
#ifdef _SC_GETGR_R_SIZE_MAX
    pwd_buf_len = sysconf(_SC_GETGR_R_SIZE_MAX);
#else
    pwd_buf_len = -1;
#endif
    if (pwd_buf_len == -1) {
        pwd_buf_len = DEFAULT_PWD_BUF_SZ; /* per the man page */
    }

    pwd_buf = alloca(pwd_buf_len);
    if (!pwd_buf)
        goto err;

    getpwuid_r(uid, &pwd, pwd_buf, pwd_buf_len, &pwd_result);
    if (!pwd_result)
        goto err;

    ret = gf_strdup(pwd.pw_name);
    if (ret)
        return ret;
    else
        gf_log(this->name, GF_LOG_ERROR,
               "gf_strdup failed, failing username "
               "resolution.");
err:
    return ret;
}

char *
_resolve_group_name(xlator_t *this, gid_t gid)
{
    struct group grp;
    struct group *grp_result = NULL;
    size_t grp_buf_len;
    char *grp_buf = NULL;
    char *ret = NULL;

    /* Prepare our buffer for the gid->group name translation */
#ifdef _SC_GETGR_R_SIZE_MAX
    grp_buf_len = sysconf(_SC_GETGR_R_SIZE_MAX);
#else
    grp_buf_len = -1;
#endif
    if (grp_buf_len == -1) {
        grp_buf_len = DEFAULT_GRP_BUF_SZ; /* per the man page */
    }

    grp_buf = alloca(grp_buf_len);
    if (!grp_buf) {
        goto err;
    }

    if (getgrgid_r(gid, &grp, grp_buf, grp_buf_len, &grp_result) != 0)
        goto err;

    if (!grp_result)
        goto err;

    ret = gf_strdup(grp.gr_name);
    if (ret)
        return ret;
    else
        gf_log(this->name, GF_LOG_ERROR,
               "gf_strdup failed, failing username "
               "resolution.");
err:
    return ret;
}

/*
 * This function writes out a latency sample to a given file descriptor
 * and beautifies the output in the process.
 */
void
_io_stats_write_latency_sample(xlator_t *this, ios_sample_t *sample,
                               FILE *logfp)
{
    double epoch_time = 0.00;
    char *xlator_name = NULL;
    char *instance_name = NULL;
    char *hostname = NULL;
    char *identifier = NULL;
    char *port = NULL;
    char *port_pos = NULL;
    char *group_name = NULL;
    char *username = NULL;
    struct ios_conf *conf = NULL;

    conf = this->private;

    epoch_time = (sample->timestamp).tv_sec +
                 ((sample->timestamp).tv_usec / 1000000.0);

    if (strlen(sample->identifier) == 0) {
        hostname = "Unknown";
        port = "Unknown";
    } else {
        identifier = strdupa(sample->identifier);
        port_pos = strrchr(identifier, ':');
        if (!port_pos || strlen(port_pos) < 2)
            goto err;
        port = strdupa(port_pos + 1);
        if (!port)
            goto err;
        *port_pos = '\0';
        hostname = gf_rev_dns_lookup_cached(identifier, conf->dnscache);
        if (!hostname)
            hostname = "Unknown";
    }

    xlator_name = conf->unique_id;
    if (!xlator_name || strlen(xlator_name) == 0)
        xlator_name = "Unknown";

    instance_name = this->instance_name;
    if (!instance_name || strlen(instance_name) == 0)
        instance_name = "N/A";

    /* Resolve the UID to a string username */
    username = _resolve_username(this, sample->uid);
    if (!username) {
        username = GF_MALLOC(30, gf_common_mt_char);
        if (!username) {
            goto out;
        }
        sprintf(username, "%d", (int32_t)sample->uid);
    }

    /* Resolve the GID to a string group name */
    group_name = _resolve_group_name(this, sample->gid);
    if (!group_name) {
        group_name = GF_MALLOC(30, gf_common_mt_char);
        if (!group_name) {
            goto out;
        }
        sprintf(group_name, "%d", (int32_t)sample->gid);
    }

    ios_log(this, logfp, "%0.6lf,%s,%s,%0.4lf,%s,%s,%s,%s,%s,%s", epoch_time,
            fop_enum_to_pri_string(sample->fop_type),
            gf_fop_string(sample->fop_type), sample->elapsed, xlator_name,
            instance_name, username, group_name, hostname, port);
    goto out;
err:
    gf_log(this->name, GF_LOG_ERROR, "Error parsing socket identifier");
out:
    GF_FREE(group_name);
    GF_FREE(username);
}

/*
 * Takes our current sample buffer in conf->io_sample_buf, and saves
 * a reference to this, init's a new buffer, and then dumps out the
 * contents of the saved reference.
 */
int
io_stats_dump_latency_samples_logfp(xlator_t *this, FILE *logfp)
{
    uint64_t i = 0;
    struct ios_conf *conf = NULL;
    ios_sample_buf_t *sample_buf = NULL;
    int ret = 1; /* Default to error */

    conf = this->private;

    /* Save pointer to old buffer; the CS equivalent of
     * Indiana Jones: https://www.youtube.com/watch?v=Pr-8AP0To4k,
     * though ours will end better I hope!
     */
    sample_buf = conf->ios_sample_buf;
    if (!sample_buf) {
        gf_log(this->name, GF_LOG_WARNING, "Sampling buffer is null, bailing!");
        goto out;
    }

    /* Empty case, nothing to do, exit. */
    if (sample_buf->collected == 0) {
        gf_log(this->name, GF_LOG_DEBUG, "No samples, dump not required.");
        ret = 0;
        goto out;
    }

    /* Init a new buffer, so we are free to work on the one we saved a
     * reference to above.
     */
    if (ios_init_sample_buf(conf) != 0) {
        gf_log(this->name, GF_LOG_WARNING,
               "Failed to init new sampling buffer, out of memory?");
        goto out;
    }

    /* Wrap-around case, dump from pos to sample_buf->size -1
     * and then from 0 to sample_buf->pos (covered off by
     * "simple case")
     */
    if (sample_buf->collected > sample_buf->pos + 1) {
        for (i = sample_buf->pos; i < sample_buf->size; i++) {
            _io_stats_write_latency_sample(this, &(sample_buf->ios_samples[i]),
                                           logfp);
        }
    }

    /* Simple case: Dump from 0 to sample_buf->pos */
    for (i = 0; i < sample_buf->pos; i++) {
        _io_stats_write_latency_sample(this, &(sample_buf->ios_samples[i]),
                                       logfp);
    }
    ios_destroy_sample_buf(sample_buf);

out:
    return ret;
}

int
io_stats_dump_global_to_logfp(xlator_t *this, struct ios_global_stats *stats,
                              time_t now, int interval, FILE *logfp)
{
    int i = 0;
    int per_line = 0;
    int index = 0;
    struct ios_stat_head *list_head = NULL;
    struct ios_conf *conf = NULL;
    char timestr[GF_TIMESTR_SIZE] = {
        0,
    };
    char str_header[128] = {0};
    char str_read[128] = {0};
    char str_write[128] = {0};
    uint64_t fop_hits = 0;
    uint64_t block_count_read = 0;
    uint64_t block_count_write = 0;

    conf = this->private;

    if (interval == -1)
        ios_log(this, logfp, "\n=== Cumulative stats ===");
    else
        ios_log(this, logfp, "\n=== Interval %d stats ===", interval);
    ios_log(this, logfp, "      Duration : %" PRIu64 " secs",
            (uint64_t)(now - stats->started_at));
    ios_log(this, logfp, "     BytesRead : %" GF_PRI_ATOMIC,
            GF_ATOMIC_GET(stats->data_read));
    ios_log(this, logfp, "  BytesWritten : %" GF_PRI_ATOMIC "\n",
            GF_ATOMIC_GET(stats->data_written));

    snprintf(str_header, sizeof(str_header), "%-12s %c", "Block Size", ':');
    snprintf(str_read, sizeof(str_read), "%-12s %c", "Read Count", ':');
    snprintf(str_write, sizeof(str_write), "%-12s %c", "Write Count", ':');
    index = 14;
    for (i = 0; i < IOS_BLOCK_COUNT_SIZE; i++) {
        block_count_read = GF_ATOMIC_GET(stats->block_count_read[i]);
        block_count_write = GF_ATOMIC_GET(stats->block_count_write[i]);
        if ((block_count_read == 0) && (block_count_write == 0))
            continue;
        per_line++;

        snprintf(str_header + index, sizeof(str_header) - index, "%16dB+",
                 (1 << i));
        if (block_count_read)
            snprintf(str_read + index, sizeof(str_read) - index, "%18" PRId64,
                     block_count_read);
        else
            snprintf(str_read + index, sizeof(str_read) - index, "%18s", "0");
        if (block_count_write)
            snprintf(str_write + index, sizeof(str_write) - index,
                     "%18" GF_PRI_ATOMIC, block_count_write);
        else
            snprintf(str_write + index, sizeof(str_write) - index, "%18s", "0");

        index += 18;
        if (per_line == 3) {
            ios_log(this, logfp, "%s", str_header);
            ios_log(this, logfp, "%s", str_read);
            ios_log(this, logfp, "%s\n", str_write);

            snprintf(str_header, sizeof(str_header), "%-12s %c", "Block Size",
                     ':');
            snprintf(str_read, sizeof(str_read), "%-12s %c", "Read Count", ':');
            snprintf(str_write, sizeof(str_write), "%-12s %c", "Write Count",
                     ':');

            index = 14;
            per_line = 0;
        }
    }

    if (per_line != 0) {
        ios_log(this, logfp, "%s", str_header);
        ios_log(this, logfp, "%s", str_read);
        ios_log(this, logfp, "%s\n", str_write);
    }

    ios_log(this, logfp, "%-13s %10s %14s %14s %14s", "Fop", "Call Count",
            "Avg-Latency", "Min-Latency", "Max-Latency");
    ios_log(this, logfp, "%-13s %10s %14s %14s %14s", "---", "----------",
            "-----------", "-----------", "-----------");

    for (i = 0; i < GF_FOP_MAXVALUE; i++) {
        fop_hits = GF_ATOMIC_GET(stats->fop_hits[i]);
        if (fop_hits && !stats->latency[i].avg)
            ios_log(this, logfp,
                    "%-13s %10" GF_PRI_ATOMIC
                    " %11s "
                    "us %11s us %11s us",
                    gf_fop_list[i], fop_hits, "0", "0", "0");
        else if (fop_hits && stats->latency[i].avg)
            ios_log(this, logfp,
                    "%-13s %10" GF_PRI_ATOMIC
                    " "
                    "%11.2lf us %11.2lf us %11.2lf us",
                    gf_fop_list[i], fop_hits, stats->latency[i].avg,
                    stats->latency[i].min, stats->latency[i].max);
    }

    for (i = 0; i < GF_UPCALL_FLAGS_MAXVALUE; i++) {
        fop_hits = GF_ATOMIC_GET(stats->upcall_hits[i]);
        if (fop_hits)
            ios_log(this, logfp,
                    "%-13s %10" PRId64
                    " %11s "
                    "us %11s us %11s us",
                    gf_upcall_list[i], fop_hits, "0", "0", "0");
    }

    ios_log(this, logfp,
            "------ ----- ----- ----- ----- ----- ----- ----- "
            " ----- ----- ----- -----\n");

    if (interval == -1) {
        LOCK(&conf->lock);
        {
            gf_time_fmt_tv(timestr, sizeof timestr,
                           &conf->cumulative.max_openfd_time, gf_timefmt_FT);
            ios_log(this, logfp,
                    "Current open fd's: %" PRId64 " Max open fd's: %" PRId64
                    " time %s",
                    conf->cumulative.nr_opens, conf->cumulative.max_nr_opens,
                    timestr);
        }
        UNLOCK(&conf->lock);
        ios_log(this, logfp, "\n==========Open File Stats========");
        ios_log(this, logfp, "\nCOUNT:  \t  FILE NAME");
        list_head = &conf->list[IOS_STATS_TYPE_OPEN];
        ios_dump_file_stats(list_head, this, logfp);

        ios_log(this, logfp, "\n==========Read File Stats========");
        ios_log(this, logfp, "\nCOUNT:  \t  FILE NAME");
        list_head = &conf->list[IOS_STATS_TYPE_READ];
        ios_dump_file_stats(list_head, this, logfp);

        ios_log(this, logfp, "\n==========Write File Stats========");
        ios_log(this, logfp, "\nCOUNT:  \t  FILE NAME");
        list_head = &conf->list[IOS_STATS_TYPE_WRITE];
        ios_dump_file_stats(list_head, this, logfp);

        ios_log(this, logfp, "\n==========Directory open stats========");
        ios_log(this, logfp, "\nCOUNT:  \t  DIRECTORY NAME");
        list_head = &conf->list[IOS_STATS_TYPE_OPENDIR];
        ios_dump_file_stats(list_head, this, logfp);

        ios_log(this, logfp, "\n========Directory readdirp Stats=======");
        ios_log(this, logfp, "\nCOUNT:  \t  DIRECTORY NAME");
        list_head = &conf->list[IOS_STATS_TYPE_READDIRP];
        ios_dump_file_stats(list_head, this, logfp);

        ios_log(this, logfp, "\n========Read Throughput File Stats=====");
        ios_log(this, logfp,
                "\nTIMESTAMP \t\t\t THROUGHPUT(KBPS)"
                "\tFILE NAME");
        list_head = &conf->thru_list[IOS_STATS_THRU_READ];
        ios_dump_throughput_stats(list_head, this, logfp, IOS_STATS_THRU_READ);

        ios_log(this, logfp, "\n======Write Throughput File Stats======");
        ios_log(this, logfp,
                "\nTIMESTAMP \t\t\t THROUGHPUT(KBPS)"
                "\tFILE NAME");
        list_head = &conf->thru_list[IOS_STATS_THRU_WRITE];
        ios_dump_throughput_stats(list_head, this, logfp, IOS_STATS_THRU_WRITE);
    }
    return 0;
}

int
io_stats_dump_global_to_dict(xlator_t *this, struct ios_global_stats *stats,
                             time_t now, int interval, dict_t *dict)
{
    int ret = 0;
    char key[64] = {0};
    uint64_t sec = 0;
    int i = 0;
    uint64_t count = 0;
    uint64_t fop_hits = 0;

    GF_ASSERT(stats);
    GF_ASSERT(now);
    GF_ASSERT(dict);
    GF_ASSERT(this);

    if (interval == -1)
        snprintf(key, sizeof(key), "cumulative");
    else
        snprintf(key, sizeof(key), "interval");
    ret = dict_set_int32(dict, key, interval);
    if (ret)
        gf_log(this->name, GF_LOG_ERROR,
               "failed to set "
               "interval %d",
               interval);

    snprintf(key, sizeof(key), "%d-duration", interval);
    sec = now - stats->started_at;
    ret = dict_set_uint64(dict, key, sec);
    if (ret) {
        gf_log(this->name, GF_LOG_ERROR,
               "failed to set "
               "duration(%d) - %" PRId64,
               interval, sec);
        goto out;
    }

    snprintf(key, sizeof(key), "%d-total-read", interval);
    ret = dict_set_uint64(dict, key, GF_ATOMIC_GET(stats->data_read));
    if (ret) {
        gf_log(this->name, GF_LOG_ERROR,
               "failed to set total "
               "read(%d) - %" GF_PRI_ATOMIC,
               interval, GF_ATOMIC_GET(stats->data_read));
        goto out;
    }

    snprintf(key, sizeof(key), "%d-total-write", interval);
    ret = dict_set_uint64(dict, key, GF_ATOMIC_GET(stats->data_written));
    if (ret) {
        gf_log(this->name, GF_LOG_ERROR,
               "failed to set total "
               "write(%d) - %" GF_PRI_ATOMIC,
               interval, GF_ATOMIC_GET(stats->data_written));
        goto out;
    }
    for (i = 0; i < 32; i++) {
        count = GF_ATOMIC_GET(stats->block_count_read[i]);
        if (count) {
            snprintf(key, sizeof(key), "%d-read-%d", interval, (1 << i));
            ret = dict_set_uint64(dict, key, count);
            if (ret) {
                gf_log(this->name, GF_LOG_ERROR,
                       "failed to "
                       "set read-%db+, with: %" PRId64,
                       (1 << i), count);
                goto out;
            }
        }
    }

    for (i = 0; i < IOS_BLOCK_COUNT_SIZE; i++) {
        count = GF_ATOMIC_GET(stats->block_count_write[i]);
        if (count) {
            snprintf(key, sizeof(key), "%d-write-%d", interval, (1 << i));
            ret = dict_set_uint64(dict, key, count);
            if (ret) {
                gf_log(this->name, GF_LOG_ERROR,
                       "failed to "
                       "set write-%db+, with: %" PRId64,
                       (1 << i), count);
                goto out;
            }
        }
    }

    for (i = 0; i < GF_FOP_MAXVALUE; i++) {
        fop_hits = GF_ATOMIC_GET(stats->fop_hits[i]);
        if (fop_hits == 0)
            continue;
        snprintf(key, sizeof(key), "%d-%d-hits", interval, i);
        ret = dict_set_uint64(dict, key, fop_hits);
        if (ret) {
            gf_log(this->name, GF_LOG_ERROR,
                   "failed to set "
                   "%s-fop-hits: %" GF_PRI_ATOMIC,
                   gf_fop_list[i], fop_hits);
            goto out;
        }

        if (stats->latency[i].avg == 0)
            continue;
        snprintf(key, sizeof(key), "%d-%d-avglatency", interval, i);
        ret = dict_set_double(dict, key, stats->latency[i].avg);
        if (ret) {
            gf_log(this->name, GF_LOG_ERROR,
                   "failed to set %s "
                   "avglatency(%d) with %f",
                   gf_fop_list[i], interval, stats->latency[i].avg);
            goto out;
        }
        snprintf(key, sizeof(key), "%d-%d-minlatency", interval, i);
        ret = dict_set_double(dict, key, stats->latency[i].min);
        if (ret) {
            gf_log(this->name, GF_LOG_ERROR,
                   "failed to set %s "
                   "minlatency(%d) with %f",
                   gf_fop_list[i], interval, stats->latency[i].min);
            goto out;
        }
        snprintf(key, sizeof(key), "%d-%d-maxlatency", interval, i);
        ret = dict_set_double(dict, key, stats->latency[i].max);
        if (ret) {
            gf_log(this->name, GF_LOG_ERROR,
                   "failed to set %s "
                   "maxlatency(%d) with %f",
                   gf_fop_list[i], interval, stats->latency[i].max);
            goto out;
        }
    }
    for (i = 0; i < GF_UPCALL_FLAGS_MAXVALUE; i++) {
        fop_hits = GF_ATOMIC_GET(stats->upcall_hits[i]);
        if (fop_hits == 0)
            continue;
        snprintf(key, sizeof(key), "%d-%d-upcall-hits", interval, i);
        ret = dict_set_uint64(dict, key, fop_hits);
        if (ret) {
            gf_log(this->name, GF_LOG_ERROR,
                   "failed to "
                   "set %s-upcall-hits: %" PRIu64,
                   gf_upcall_list[i], fop_hits);
            goto out;
        }
    }
out:
    gf_log(this->name, GF_LOG_DEBUG, "returning %d", ret);
    return ret;
}

int
io_stats_dump_global(xlator_t *this, struct ios_global_stats *stats, time_t now,
                     int interval, struct ios_dump_args *args)
{
    int ret = -1;

    GF_ASSERT(args);
    GF_ASSERT(now);
    GF_ASSERT(stats);
    GF_ASSERT(this);

    switch (args->type) {
        case IOS_DUMP_TYPE_JSON_FILE:
            ret = io_stats_dump_global_to_json_logfp(this, stats, now, interval,
                                                     args->u.logfp);
            break;
        case IOS_DUMP_TYPE_FILE:
            ret = io_stats_dump_global_to_logfp(this, stats, now, interval,
                                                args->u.logfp);
            break;
        case IOS_DUMP_TYPE_DICT:
            ret = io_stats_dump_global_to_dict(this, stats, now, interval,
                                               args->u.dict);
            break;
        default:
            GF_ASSERT(0);
            ret = -1;
            break;
    }
    return ret;
}

int
ios_dump_args_init(struct ios_dump_args *args, ios_dump_type_t type,
                   void *output)
{
    int ret = 0;

    GF_ASSERT(args);
    GF_ASSERT(type > IOS_DUMP_TYPE_NONE && type < IOS_DUMP_TYPE_MAX);
    GF_ASSERT(output);

    args->type = type;
    switch (args->type) {
        case IOS_DUMP_TYPE_JSON_FILE:
        case IOS_DUMP_TYPE_FILE:
            args->u.logfp = output;
            break;
        case IOS_DUMP_TYPE_DICT:
            args->u.dict = output;
            break;
        default:
            GF_ASSERT(0);
            ret = -1;
    }

    return ret;
}

static void
ios_global_stats_clear(struct ios_global_stats *stats, time_t now)
{
    GF_ASSERT(stats);
    GF_ASSERT(now);

    memset(stats, 0, sizeof(*stats));
    stats->started_at = now;
}

int
io_stats_dump(xlator_t *this, struct ios_dump_args *args, ios_info_op_t op,
              gf_boolean_t is_peek)
{
    struct ios_conf *conf = NULL;
    struct ios_global_stats cumulative = {};
    struct ios_global_stats incremental = {};
    int increment = 0;
    time_t now = 0;

    GF_ASSERT(this);
    GF_ASSERT(args);
    GF_ASSERT(args->type > IOS_DUMP_TYPE_NONE);
    GF_ASSERT(args->type < IOS_DUMP_TYPE_MAX);

    conf = this->private;
    now = gf_time();

    LOCK(&conf->lock);
    {
        if (op == GF_IOS_INFO_ALL || op == GF_IOS_INFO_CUMULATIVE)
            cumulative = conf->cumulative;

        if (op == GF_IOS_INFO_ALL || op == GF_IOS_INFO_INCREMENTAL) {
            incremental = conf->incremental;
            increment = conf->increment;

            if (!is_peek) {
                increment = conf->increment++;

                ios_global_stats_clear(&conf->incremental, now);
            }
        }
    }
    UNLOCK(&conf->lock);

    if (op == GF_IOS_INFO_ALL || op == GF_IOS_INFO_CUMULATIVE)
        io_stats_dump_global(this, &cumulative, now, -1, args);

    if (op == GF_IOS_INFO_ALL || op == GF_IOS_INFO_INCREMENTAL)
        io_stats_dump_global(this, &incremental, now, increment, args);

    return 0;
}

int
io_stats_dump_fd(xlator_t *this, struct ios_fd *iosfd)
{
    struct ios_conf *conf = NULL;
    struct timeval now;
    int i = 0;
    double usecs = 0;
    uint64_t data_read = 0;
    uint64_t data_written = 0;
    uint64_t block_count_read = 0;
    uint64_t block_count_write = 0;

    conf = this->private;

    if (!conf->dump_fd_stats)
        return 0;

    if (!iosfd)
        return 0;

    gettimeofday(&now, NULL);
    usecs = gf_tvdiff(&iosfd->opened_at, &now);

    gf_log(this->name, GF_LOG_INFO, "--- fd stats ---");

    if (iosfd->filename)
        gf_log(this->name, GF_LOG_INFO, "      Filename : %s", iosfd->filename);

    if (usecs)
        gf_log(this->name, GF_LOG_INFO, "      Lifetime : %lf secs", usecs);

    data_read = GF_ATOMIC_GET(iosfd->data_read);
    if (data_read)
        gf_log(this->name, GF_LOG_INFO, "     BytesRead : %" PRId64 " bytes",
               data_read);

    data_written = GF_ATOMIC_GET(iosfd->data_written);
    if (data_written)
        gf_log(this->name, GF_LOG_INFO, "  BytesWritten : %" PRId64 " bytes",
               data_written);

    for (i = 0; i < 32; i++) {
        block_count_read = GF_ATOMIC_GET(iosfd->block_count_read[i]);
        if (block_count_read)
            gf_log(this->name, GF_LOG_INFO,
                   " Read %06db+ :"
                   "%" PRId64,
                   (1 << i), block_count_read);
    }
    for (i = 0; i < IOS_BLOCK_COUNT_SIZE; i++) {
        block_count_write = GF_ATOMIC_GET(iosfd->block_count_write[i]);
        if (block_count_write)
            gf_log(this->name, GF_LOG_INFO, "Write %06db+ : %" PRId64, (1 << i),
                   block_count_write);
    }
    return 0;
}

void
collect_ios_latency_sample(struct ios_conf *conf, glusterfs_fop_t fop_type,
                           double elapsed, call_frame_t *frame)
{
    ios_sample_buf_t *ios_sample_buf = NULL;
    ios_sample_t *ios_sample = NULL;
    struct timespec *timestamp = NULL;
    call_stack_t *root = NULL;

    ios_sample_buf = conf->ios_sample_buf;
    LOCK(&conf->ios_sampling_lock);
    if (conf->ios_sample_interval == 0 ||
        ios_sample_buf->observed % conf->ios_sample_interval != 0)
        goto out;

    timestamp = &frame->begin;
    root = frame->root;

    ios_sample = &(ios_sample_buf->ios_samples[ios_sample_buf->pos]);
    ios_sample->elapsed = elapsed;
    ios_sample->fop_type = fop_type;
    ios_sample->uid = root->uid;
    ios_sample->gid = root->gid;
    (ios_sample->timestamp).tv_sec = timestamp->tv_sec;
    (ios_sample->timestamp).tv_usec = timestamp->tv_nsec / 1000;
    memcpy(&ios_sample->identifier, &root->identifier,
           sizeof(root->identifier));

    /* We've reached the end of the circular buffer, start from the
     * beginning. */
    if (ios_sample_buf->pos == (ios_sample_buf->size - 1))
        ios_sample_buf->pos = 0;
    else
        ios_sample_buf->pos++;
    ios_sample_buf->collected++;
out:
    ios_sample_buf->observed++;
    UNLOCK(&conf->ios_sampling_lock);
    return;
}

static void
update_ios_latency_stats(struct ios_global_stats *stats, double elapsed,
                         glusterfs_fop_t op)
{
    double avg;

    GF_ASSERT(stats);

    stats->latency[op].total += elapsed;

    if (!stats->latency[op].min)
        stats->latency[op].min = elapsed;
    if (stats->latency[op].min > elapsed)
        stats->latency[op].min = elapsed;
    if (stats->latency[op].max < elapsed)
        stats->latency[op].max = elapsed;

    avg = stats->latency[op].avg;

    stats->latency[op].avg = avg + (elapsed - avg) /
                                       GF_ATOMIC_GET(stats->fop_hits[op]);
}

int
update_ios_latency(struct ios_conf *conf, call_frame_t *frame,
                   glusterfs_fop_t op)
{
    double elapsed;
    struct timespec *begin, *end;

    begin = &frame->begin;
    end = &frame->end;

    elapsed = gf_tsdiff(begin, end) / 1000.0;

    update_ios_latency_stats(&conf->cumulative, elapsed, op);
    update_ios_latency_stats(&conf->incremental, elapsed, op);
    collect_ios_latency_sample(conf, op, elapsed, frame);

    return 0;
}

int32_t
io_stats_dump_stats_to_dict(xlator_t *this, dict_t *resp,
                            ios_stats_type_t flags, int32_t list_cnt)
{
    struct ios_conf *conf = NULL;
    int cnt = 0;
    char key[32];
    int keylen;
    struct ios_stat_head *list_head = NULL;
    struct ios_stat_list *entry = NULL;
    int ret = -1;
    ios_stats_thru_t index = IOS_STATS_THRU_MAX;
    char timestr[GF_TIMESTR_SIZE] = {
        0,
    };
    char *dict_timestr = NULL;

    conf = this->private;

    switch (flags) {
        case IOS_STATS_TYPE_OPEN:
            list_head = &conf->list[IOS_STATS_TYPE_OPEN];
            LOCK(&conf->lock);
            {
                ret = dict_set_uint64(resp, "current-open",
                                      conf->cumulative.nr_opens);
                if (ret)
                    goto unlock;
                ret = dict_set_uint64(resp, "max-open",
                                      conf->cumulative.max_nr_opens);

                gf_time_fmt_tv(timestr, sizeof timestr,
                               &conf->cumulative.max_openfd_time,
                               gf_timefmt_FT);

                dict_timestr = gf_strdup(timestr);
                if (!dict_timestr)
                    goto unlock;
                ret = dict_set_dynstr(resp, "max-openfd-time", dict_timestr);
                if (ret)
                    goto unlock;
            }
        unlock:
            UNLOCK(&conf->lock);
            /* Do not proceed if we came here because of some error
             * during the dict operation */
            if (ret)
                goto out;
            break;
        case IOS_STATS_TYPE_READ:
            list_head = &conf->list[IOS_STATS_TYPE_READ];
            break;
        case IOS_STATS_TYPE_WRITE:
            list_head = &conf->list[IOS_STATS_TYPE_WRITE];
            break;
        case IOS_STATS_TYPE_OPENDIR:
            list_head = &conf->list[IOS_STATS_TYPE_OPENDIR];
            break;
        case IOS_STATS_TYPE_READDIRP:
            list_head = &conf->list[IOS_STATS_TYPE_READDIRP];
            break;
        case IOS_STATS_TYPE_READ_THROUGHPUT:
            list_head = &conf->thru_list[IOS_STATS_THRU_READ];
            index = IOS_STATS_THRU_READ;
            break;
        case IOS_STATS_TYPE_WRITE_THROUGHPUT:
            list_head = &conf->thru_list[IOS_STATS_THRU_WRITE];
            index = IOS_STATS_THRU_WRITE;
            break;

        default:
            goto out;
    }
    ret = dict_set_int32_sizen(resp, "top-op", flags);
    if (!list_cnt)
        goto out;
    LOCK(&list_head->lock);
    {
        list_for_each_entry(entry, &list_head->iosstats->list, list)
        {
            cnt++;
            keylen = snprintf(key, sizeof(key), "filename-%d", cnt);
            ret = dict_set_strn(resp, key, keylen, entry->iosstat->filename);
            if (ret)
                goto unlock_list_head;
            snprintf(key, sizeof(key), "value-%d", cnt);
            ret = dict_set_uint64(resp, key, entry->value);
            if (ret)
                goto unlock_list_head;
            if (index != IOS_STATS_THRU_MAX) {
                keylen = snprintf(key, sizeof(key), "time-sec-%d", cnt);
                ret = dict_set_int32n(
                    resp, key, keylen,
                    entry->iosstat->thru_counters[index].time.tv_sec);
                if (ret)
                    goto unlock_list_head;
                keylen = snprintf(key, sizeof(key), "time-usec-%d", cnt);
                ret = dict_set_int32n(
                    resp, key, keylen,
                    entry->iosstat->thru_counters[index].time.tv_usec);
                if (ret)
                    goto unlock_list_head;
            }
            if (cnt == list_cnt)
                break;
        }
    }
unlock_list_head:
    UNLOCK(&list_head->lock);
    /* ret is !=0 if some dict operation in the above critical region
     * failed. */
    if (ret)
        goto out;
    ret = dict_set_int32_sizen(resp, "members", cnt);
out:
    return ret;
}

static struct ios_stat *
ios_init_iosstat(xlator_t *this, char *path, uuid_t gfid, inode_t *inode)
{
    struct ios_stat *iosstat = NULL;
    int i = 0;

    iosstat = GF_CALLOC(1, sizeof(*iosstat), gf_io_stats_mt_ios_stat);
    if (!iosstat)
        goto out;

    iosstat->filename = gf_strdup(path);
    gf_uuid_copy(iosstat->gfid, gfid);
    LOCK_INIT(&iosstat->lock);

    for (i = 0; i < IOS_STATS_TYPE_MAX; i++)
        GF_ATOMIC_INIT(iosstat->counters[i], 0);

    ios_inode_ctx_set(inode, this, iosstat);

out:
    return iosstat;
}

int
io_stats_create_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
{
    struct ios_fd *iosfd = NULL;
    char *path = NULL;
    struct ios_stat *iosstat = NULL;
    struct ios_conf *conf = NULL;

    conf = this->private;

    path = frame->local;
    frame->local = NULL;

    if (!path)
        goto unwind;

    if (op_ret < 0) {
        GF_FREE(path);
        goto unwind;
    }

    iosfd = GF_CALLOC(1, sizeof(*iosfd), gf_io_stats_mt_ios_fd);
    if (!iosfd) {
        GF_FREE(path);
        goto unwind;
    }

    iosfd->filename = path;
    gettimeofday(&iosfd->opened_at, NULL);

    ios_fd_ctx_set(fd, this, iosfd);
    LOCK(&conf->lock);
    {
        conf->cumulative.nr_opens++;
        if (conf->cumulative.nr_opens > conf->cumulative.max_nr_opens) {
            conf->cumulative.max_nr_opens = conf->cumulative.nr_opens;
            conf->cumulative.max_openfd_time = iosfd->opened_at;
        }
    }
    UNLOCK(&conf->lock);

    iosstat = ios_init_iosstat(this, path, buf->ia_gfid, inode);
    if (!iosstat)
        GF_FREE(path);

unwind:
    UPDATE_PROFILE_STATS(frame, CREATE);
    STACK_UNWIND_STRICT(create, frame, op_ret, op_errno, fd, inode, buf,
                        preparent, postparent, xdata);
    return 0;
}

int
io_stats_open_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
    struct ios_fd *iosfd = NULL;
    char *path = NULL;
    struct ios_stat *iosstat = NULL;
    struct ios_conf *conf = NULL;
    int i = 0;

    conf = this->private;
    path = frame->local;
    frame->local = NULL;

    if (!path)
        goto unwind;

    if (op_ret < 0) {
        GF_FREE(path);
        goto unwind;
    }

    iosfd = GF_CALLOC(1, sizeof(*iosfd), gf_io_stats_mt_ios_fd);
    if (!iosfd) {
        GF_FREE(path);
        goto unwind;
    }

    iosfd->filename = path;
    GF_ATOMIC_INIT(iosfd->data_read, 0);
    GF_ATOMIC_INIT(iosfd->data_written, 0);
    for (i = 0; i < IOS_BLOCK_COUNT_SIZE; i++) {
        GF_ATOMIC_INIT(iosfd->block_count_write[i], 0);
        GF_ATOMIC_INIT(iosfd->block_count_read[i], 0);
    }
    gettimeofday(&iosfd->opened_at, NULL);

    ios_fd_ctx_set(fd, this, iosfd);

    ios_inode_ctx_get(fd->inode, this, &iosstat);
    if (!iosstat) {
        iosstat = ios_init_iosstat(this, path, fd->inode->gfid, fd->inode);
    }

    LOCK(&conf->lock);
    {
        conf->cumulative.nr_opens++;
        if (conf->cumulative.nr_opens > conf->cumulative.max_nr_opens) {
            conf->cumulative.max_nr_opens = conf->cumulative.nr_opens;
            conf->cumulative.max_openfd_time = iosfd->opened_at;
        }
    }
    UNLOCK(&conf->lock);
    if (iosstat) {
        ios_bump_stats(this, iosstat, IOS_STATS_TYPE_OPEN);
        iosstat = NULL;
    }
unwind:
    UPDATE_PROFILE_STATS(frame, OPEN);

    STACK_UNWIND_STRICT(open, frame, op_ret, op_errno, fd, xdata);
    return 0;
}

int
io_stats_stat_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *buf,
                  dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, STAT);
    STACK_UNWIND_STRICT(stat, frame, op_ret, op_errno, buf, xdata);
    return 0;
}

int
io_stats_readv_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iovec *vector,
                   int32_t count, struct iatt *buf, struct iobref *iobref,
                   dict_t *xdata)
{
    int len = 0;
    fd_t *fd = NULL;
    struct ios_stat *iosstat = NULL;

    fd = frame->local;
    frame->local = NULL;

    if (op_ret > 0) {
        len = iov_length(vector, count);
        ios_bump_read(this, fd, len);
    }

    UPDATE_PROFILE_STATS(frame, READ);
    ios_inode_ctx_get(fd->inode, this, &iosstat);

    if (iosstat) {
        ios_bump_stats(this, iosstat, IOS_STATS_TYPE_READ);
        BUMP_THROUGHPUT(iosstat, IOS_STATS_THRU_READ);
        iosstat = NULL;
    }

    STACK_UNWIND_STRICT(readv, frame, op_ret, op_errno, vector, count, buf,
                        iobref, xdata);
    return 0;
}

int
io_stats_writev_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                    struct iatt *postbuf, dict_t *xdata)
{
    struct ios_stat *iosstat = NULL;
    inode_t *inode = NULL;

    UPDATE_PROFILE_STATS(frame, WRITE);
    if (frame->local) {
        inode = frame->local;
        frame->local = NULL;
        ios_inode_ctx_get(inode, this, &iosstat);
        if (iosstat) {
            ios_bump_stats(this, iosstat, IOS_STATS_TYPE_WRITE);
            BUMP_THROUGHPUT(iosstat, IOS_STATS_THRU_WRITE);
            inode = NULL;
            iosstat = NULL;
        }
    }

    STACK_UNWIND_STRICT(writev, frame, op_ret, op_errno, prebuf, postbuf,
                        xdata);
    return 0;
}

int
io_stats_copy_file_range_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno,
                             struct iatt *stbuf, struct iatt *prebuf_dst,
                             struct iatt *postbuf_dst, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, COPY_FILE_RANGE);

    STACK_UNWIND_STRICT(copy_file_range, frame, op_ret, op_errno, stbuf,
                        prebuf_dst, postbuf_dst, xdata);
    return 0;
}

int
io_stats_readdirp_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, gf_dirent_t *buf,
                      dict_t *xdata)
{
    struct ios_stat *iosstat = NULL;
    inode_t *inode = frame->local;

    frame->local = NULL;

    UPDATE_PROFILE_STATS(frame, READDIRP);

    ios_inode_ctx_get(inode, this, &iosstat);

    if (iosstat) {
        ios_bump_stats(this, iosstat, IOS_STATS_TYPE_READDIRP);
        iosstat = NULL;
    }

    STACK_UNWIND_STRICT(readdirp, frame, op_ret, op_errno, buf, xdata);
    return 0;
}

int
io_stats_readdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, gf_dirent_t *buf,
                     dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, READDIR);
    STACK_UNWIND_STRICT(readdir, frame, op_ret, op_errno, buf, xdata);
    return 0;
}

int
io_stats_fsync_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, FSYNC);
    STACK_UNWIND_STRICT(fsync, frame, op_ret, op_errno, prebuf, postbuf, xdata);
    return 0;
}

int
io_stats_setattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *preop,
                     struct iatt *postop, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, SETATTR);
    STACK_UNWIND_STRICT(setattr, frame, op_ret, op_errno, preop, postop, xdata);
    return 0;
}

int
io_stats_unlink_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, UNLINK);
    STACK_UNWIND_STRICT(unlink, frame, op_ret, op_errno, preparent, postparent,
                        xdata);
    return 0;
}

int
io_stats_rename_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *buf,
                    struct iatt *preoldparent, struct iatt *postoldparent,
                    struct iatt *prenewparent, struct iatt *postnewparent,
                    dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, RENAME);
    STACK_UNWIND_STRICT(rename, frame, op_ret, op_errno, buf, preoldparent,
                        postoldparent, prenewparent, postnewparent, xdata);
    return 0;
}

int
io_stats_readlink_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, const char *buf,
                      struct iatt *sbuf, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, READLINK);
    STACK_UNWIND_STRICT(readlink, frame, op_ret, op_errno, buf, sbuf, xdata);
    return 0;
}

int
io_stats_lookup_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, dict_t *xdata, struct iatt *postparent)
{
    UPDATE_PROFILE_STATS(frame, LOOKUP);
    STACK_UNWIND_STRICT(lookup, frame, op_ret, op_errno, inode, buf, xdata,
                        postparent);
    return 0;
}

int
io_stats_symlink_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, inode_t *inode,
                     struct iatt *buf, struct iatt *preparent,
                     struct iatt *postparent, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, SYMLINK);
    STACK_UNWIND_STRICT(symlink, frame, op_ret, op_errno, inode, buf, preparent,
                        postparent, xdata);
    return 0;
}

int
io_stats_mknod_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, MKNOD);
    STACK_UNWIND_STRICT(mknod, frame, op_ret, op_errno, inode, buf, preparent,
                        postparent, xdata);
    return 0;
}

int
io_stats_mkdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
    char *path = frame->local;

    if (!path)
        goto unwind;

    UPDATE_PROFILE_STATS(frame, MKDIR);
    if (op_ret < 0)
        goto unwind;

    /* allocate a struct ios_stat and set the inode ctx */
    ios_init_iosstat(this, path, buf->ia_gfid, inode);

unwind:
    /* local is assigned with path */
    GF_FREE(frame->local);
    frame->local = NULL;
    STACK_UNWIND_STRICT(mkdir, frame, op_ret, op_errno, inode, buf, preparent,
                        postparent, xdata);
    return 0;
}

int
io_stats_link_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *buf, struct iatt *preparent,
                  struct iatt *postparent, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, LINK);
    STACK_UNWIND_STRICT(link, frame, op_ret, op_errno, inode, buf, preparent,
                        postparent, xdata);
    return 0;
}

int
io_stats_flush_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, FLUSH);
    STACK_UNWIND_STRICT(flush, frame, op_ret, op_errno, xdata);
    return 0;
}

int
io_stats_opendir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
    struct ios_stat *iosstat = NULL;
    int ret = -1;

    UPDATE_PROFILE_STATS(frame, OPENDIR);
    if (op_ret < 0)
        goto unwind;

    ios_fd_ctx_set(fd, this, 0);

    ret = ios_inode_ctx_get(fd->inode, this, &iosstat);
    if (!ret)
        ios_bump_stats(this, iosstat, IOS_STATS_TYPE_OPENDIR);

unwind:
    STACK_UNWIND_STRICT(opendir, frame, op_ret, op_errno, fd, xdata);
    return 0;
}

int
io_stats_rmdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, RMDIR);

    STACK_UNWIND_STRICT(rmdir, frame, op_ret, op_errno, preparent, postparent,
                        xdata);
    return 0;
}

int
io_stats_truncate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                      struct iatt *postbuf, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, TRUNCATE);
    STACK_UNWIND_STRICT(truncate, frame, op_ret, op_errno, prebuf, postbuf,
                        xdata);
    return 0;
}

int
io_stats_statfs_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct statvfs *buf,
                    dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, STATFS);
    STACK_UNWIND_STRICT(statfs, frame, op_ret, op_errno, buf, xdata);
    return 0;
}

int
io_stats_setxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, SETXATTR);
    STACK_UNWIND_STRICT(setxattr, frame, op_ret, op_errno, xdata);
    return 0;
}

int
io_stats_getxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *dict,
                      dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, GETXATTR);
    STACK_UNWIND_STRICT(getxattr, frame, op_ret, op_errno, dict, xdata);
    return 0;
}

int
io_stats_removexattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, REMOVEXATTR);
    STACK_UNWIND_STRICT(removexattr, frame, op_ret, op_errno, xdata);
    return 0;
}

int
io_stats_fsetxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, FSETXATTR);
    STACK_UNWIND_STRICT(fsetxattr, frame, op_ret, op_errno, xdata);
    return 0;
}

int
io_stats_fgetxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *dict,
                       dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, FGETXATTR);
    STACK_UNWIND_STRICT(fgetxattr, frame, op_ret, op_errno, dict, xdata);
    return 0;
}

int
io_stats_fremovexattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, FREMOVEXATTR);
    STACK_UNWIND_STRICT(fremovexattr, frame, op_ret, op_errno, xdata);
    return 0;
}

int
io_stats_fsyncdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, FSYNCDIR);
    STACK_UNWIND_STRICT(fsyncdir, frame, op_ret, op_errno, xdata);
    return 0;
}

int
io_stats_access_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, ACCESS);
    STACK_UNWIND_STRICT(access, frame, op_ret, op_errno, xdata);
    return 0;
}

int
io_stats_ftruncate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                       struct iatt *postbuf, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, FTRUNCATE);
    STACK_UNWIND_STRICT(ftruncate, frame, op_ret, op_errno, prebuf, postbuf,
                        xdata);
    return 0;
}

int
io_stats_fstat_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *buf,
                   dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, FSTAT);
    STACK_UNWIND_STRICT(fstat, frame, op_ret, op_errno, buf, xdata);
    return 0;
}

int
io_stats_fallocate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                       struct iatt *postbuf, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, FALLOCATE);
    STACK_UNWIND_STRICT(fallocate, frame, op_ret, op_errno, prebuf, postbuf,
                        xdata);
    return 0;
}

int
io_stats_discard_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, DISCARD);
    STACK_UNWIND_STRICT(discard, frame, op_ret, op_errno, prebuf, postbuf,
                        xdata);
    return 0;
}

int
io_stats_zerofill_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                      struct iatt *postbuf, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, ZEROFILL);
    STACK_UNWIND_STRICT(zerofill, frame, op_ret, op_errno, prebuf, postbuf,
                        xdata);
    return 0;
}

int32_t
io_stats_ipc_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, IPC);
    STACK_UNWIND_STRICT(ipc, frame, op_ret, op_errno, xdata);
    return 0;
}

int
io_stats_lk_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct gf_flock *lock,
                dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, LK);
    STACK_UNWIND_STRICT(lk, frame, op_ret, op_errno, lock, xdata);
    return 0;
}

int
io_stats_entrylk_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, ENTRYLK);
    STACK_UNWIND_STRICT(entrylk, frame, op_ret, op_errno, xdata);
    return 0;
}

int
io_stats_fentrylk_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, FENTRYLK);
    STACK_UNWIND_STRICT(fentrylk, frame, op_ret, op_errno, xdata);
    return 0;
}

int
io_stats_rchecksum_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, uint32_t weak_checksum,
                       uint8_t *strong_checksum, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, RCHECKSUM);
    STACK_UNWIND_STRICT(rchecksum, frame, op_ret, op_errno, weak_checksum,
                        strong_checksum, xdata);
    return 0;
}

int
io_stats_seek_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, off_t offset, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, SEEK);
    STACK_UNWIND_STRICT(seek, frame, op_ret, op_errno, offset, xdata);
    return 0;
}

int
io_stats_lease_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct gf_lease *lease,
                   dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, LEASE);
    STACK_UNWIND_STRICT(lease, frame, op_ret, op_errno, lease, xdata);
    return 0;
}

int
io_stats_getactivelk_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno,
                         lock_migration_info_t *locklist, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, GETACTIVELK);
    STACK_UNWIND_STRICT(getactivelk, frame, op_ret, op_errno, locklist, xdata);
    return 0;
}

int
io_stats_setactivelk_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, SETACTIVELK);
    STACK_UNWIND_STRICT(setactivelk, frame, op_ret, op_errno, xdata);
    return 0;
}

int
io_stats_compound_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, void *data,
                      dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, COMPOUND);
    STACK_UNWIND_STRICT(compound, frame, op_ret, op_errno, data, xdata);
    return 0;
}

int
io_stats_xattrop_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *dict,
                     dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, XATTROP);
    STACK_UNWIND_STRICT(xattrop, frame, op_ret, op_errno, dict, xdata);
    return 0;
}

int
io_stats_fxattrop_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *dict,
                      dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, FXATTROP);
    STACK_UNWIND_STRICT(fxattrop, frame, op_ret, op_errno, dict, xdata);
    return 0;
}

int
io_stats_inodelk_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, INODELK);
    STACK_UNWIND_STRICT(inodelk, frame, op_ret, op_errno, xdata);
    return 0;
}

int
io_stats_entrylk(call_frame_t *frame, xlator_t *this, const char *volume,
                 loc_t *loc, const char *basename, entrylk_cmd cmd,
                 entrylk_type type, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_entrylk_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->entrylk, volume, loc, basename, cmd,
               type, xdata);
    return 0;
}

int
io_stats_fentrylk(call_frame_t *frame, xlator_t *this, const char *volume,
                  fd_t *fd, const char *basename, entrylk_cmd cmd,
                  entrylk_type type, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_fentrylk_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fentrylk, volume, fd, basename, cmd,
               type, xdata);
    return 0;
}

int
io_stats_inodelk(call_frame_t *frame, xlator_t *this, const char *volume,
                 loc_t *loc, int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_inodelk_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->inodelk, volume, loc, cmd, flock,
               xdata);
    return 0;
}

int
io_stats_finodelk_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    UPDATE_PROFILE_STATS(frame, FINODELK);
    STACK_UNWIND_STRICT(finodelk, frame, op_ret, op_errno, xdata);
    return 0;
}

int
io_stats_finodelk(call_frame_t *frame, xlator_t *this, const char *volume,
                  fd_t *fd, int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_finodelk_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->finodelk, volume, fd, cmd, flock,
               xdata);
    return 0;
}

int
io_stats_xattrop(call_frame_t *frame, xlator_t *this, loc_t *loc,
                 gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_xattrop_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->xattrop, loc, flags, dict, xdata);
    return 0;
}

int
io_stats_fxattrop(call_frame_t *frame, xlator_t *this, fd_t *fd,
                  gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_fxattrop_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fxattrop, fd, flags, dict, xdata);
    return 0;
}

int
io_stats_lookup(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_lookup_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->lookup, loc, xdata);
    return 0;
}

int
io_stats_stat(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_stat_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->stat, loc, xdata);
    return 0;
}

int
io_stats_readlink(call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size,
                  dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_readlink_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->readlink, loc, size, xdata);
    return 0;
}

int
io_stats_mknod(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
               dev_t dev, mode_t umask, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_mknod_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->mknod, loc, mode, dev, umask, xdata);
    return 0;
}

int
io_stats_mkdir(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
               mode_t umask, dict_t *xdata)
{
    if (loc->path)
        frame->local = gf_strdup(loc->path);

    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_mkdir_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->mkdir, loc, mode, umask, xdata);
    return 0;
}

int
io_stats_unlink(call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
                dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_unlink_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->unlink, loc, xflag, xdata);
    return 0;
}

int
io_stats_rmdir(call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
               dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_rmdir_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->rmdir, loc, flags, xdata);
    return 0;
}

int
io_stats_symlink(call_frame_t *frame, xlator_t *this, const char *linkpath,
                 loc_t *loc, mode_t umask, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_symlink_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->symlink, linkpath, loc, umask, xdata);
    return 0;
}

int
io_stats_rename(call_frame_t *frame, xlator_t *this, loc_t *oldloc,
                loc_t *newloc, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_rename_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->rename, oldloc, newloc, xdata);
    return 0;
}

int
io_stats_link(call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
              dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_link_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->link, oldloc, newloc, xdata);
    return 0;
}

int
io_stats_setattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
                 struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_setattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->setattr, loc, stbuf, valid, xdata);
    return 0;
}

int
io_stats_truncate(call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
                  dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_truncate_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->truncate, loc, offset, xdata);
    return 0;
}

int
io_stats_open(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
              fd_t *fd, dict_t *xdata)
{
    if (loc->path)
        frame->local = gf_strdup(loc->path);

    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_open_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->open, loc, flags, fd, xdata);
    return 0;
}

int
io_stats_create(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
                mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
    if (loc->path)
        frame->local = gf_strdup(loc->path);

    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_create_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->create, loc, flags, mode, umask, fd,
               xdata);
    return 0;
}

int
io_stats_readv(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
               off_t offset, uint32_t flags, dict_t *xdata)
{
    frame->local = fd;

    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_readv_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->readv, fd, size, offset, flags, xdata);
    return 0;
}

int
io_stats_writev(call_frame_t *frame, xlator_t *this, fd_t *fd,
                struct iovec *vector, int32_t count, off_t offset,
                uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
    int len = 0;

    if (fd->inode)
        frame->local = fd->inode;
    len = iov_length(vector, count);

    ios_bump_write(this, fd, len);
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_writev_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->writev, fd, vector, count, offset,
               flags, iobref, xdata);
    return 0;
}

int
io_stats_copy_file_range(call_frame_t *frame, xlator_t *this, fd_t *fd_in,
                         off_t off_in, fd_t *fd_out, off_t off_out, size_t len,
                         uint32_t flags, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_copy_file_range_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->copy_file_range, fd_in, off_in, fd_out,
               off_out, len, flags, xdata);
    return 0;
}

int
io_stats_statfs(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_statfs_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->statfs, loc, xdata);
    return 0;
}

int
io_stats_flush(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_flush_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->flush, fd, xdata);
    return 0;
}

int
io_stats_fsync(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags,
               dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_fsync_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fsync, fd, flags, xdata);
    return 0;
}

int
conditional_dump(dict_t *dict, char *key, data_t *value, void *data)
{
    struct {
        xlator_t *this;
        inode_t *inode;
        const char *path;
    } * stub;
    xlator_t *this = NULL;
    char *filename = NULL;
    FILE *logfp = NULL;
    struct ios_dump_args args = {0};
    int pid, namelen, dirlen;
    char dump_key[100];
    char *slash_ptr = NULL;
    char *path_in_value = NULL;
    char *identifier = NULL;
    struct ios_conf *conf = NULL;

    stub = data;
    this = stub->this;
    conf = this->private;

    /* Don't do this on 'brick-side', only do this on client side */
    /* Addresses CVE-2018-14659 */
    if (this->ctx->process_mode != GF_CLIENT_PROCESS) {
        gf_log(this->name, GF_LOG_DEBUG,
               "taking io-stats dump using setxattr not permitted on brick."
               " Use 'gluster profile' instead");
        return -1;
    }

    /* Create a file name that is appended with the io-stats instance
    name as well. This helps when there is more than a single io-stats
    instance in the graph, or the client and server processes are running
    on the same node */
    /* For the sanity of where the file should be located, we should make
       sure file is written only inside RUNDIR (ie, /var/run/gluster) */
    /* TODO: provide an option to dump it to different directory of
       choice, based on options */
    /* name format: /var/run/gluster/<passed in path/filename>.<xlator name
     * slashes to -> */

    path_in_value = alloca0(value->len + 1);

    /* We need a memcpy here because of the way dict_unserialize works */

    memcpy(path_in_value, data_to_str(value), value->len);
    path_in_value[value->len] = '\0';

    if (strstr(path_in_value, "../")) {
        gf_log(this->name, GF_LOG_ERROR, "%s: no \"../\" allowed in path",
               path_in_value);
        return -1;
    }

    if (path_in_value[0] == '/') {
        path_in_value = path_in_value + 1;
    }

    dirlen = strlen(IOS_STATS_DUMP_DIR);
    if (conf->unique_id) {
        /* this->name will be the same for all bricks of the volume */
        identifier = conf->unique_id;
    } else {
        identifier = this->name;
    }

    namelen = (dirlen + value->len + strlen(identifier) + 3);
    /* +3 for '/', '.' and '\0' added in snprintf below*/

    filename = alloca0(namelen);
    snprintf(filename, namelen, "%s/%s.%s", IOS_STATS_DUMP_DIR, path_in_value,
             identifier);

    /* convert any slashes to '-' so that fopen works correctly */
    slash_ptr = strchr(filename + dirlen + 1, '/');
    while (slash_ptr) {
        *slash_ptr = '-';
        slash_ptr = strchr(slash_ptr, '/');
    }

    pid = getpid();

    if (!strncmp(filename, "", 1)) {
        gf_log(this->name, GF_LOG_ERROR, "No filename given");
        return -1;
    }
    logfp = fopen(filename, "w+");
    if (!logfp) {
        gf_log(this->name, GF_LOG_ERROR,
               "failed to open %s "
               "for writing",
               filename);
        return -1;
    }
    sprintf(dump_key, "*io*stat*%d_json_dump", pid);
    if (fnmatch(dump_key, key, 0) == 0) {
        (void)ios_dump_args_init(&args, IOS_DUMP_TYPE_JSON_FILE, logfp);
    } else {
        (void)ios_dump_args_init(&args, IOS_DUMP_TYPE_FILE, logfp);
    }
    io_stats_dump(this, &args, GF_IOS_INFO_ALL, _gf_false);
    fclose(logfp);
    return 0;
}

int
_ios_destroy_dump_thread(struct ios_conf *conf)
{
    conf->dump_thread_should_die = _gf_true;
    if (conf->dump_thread_running) {
        (void)pthread_cancel(conf->dump_thread);
        (void)pthread_join(conf->dump_thread, NULL);
    }
    return 0;
}

void *
_ios_dump_thread(xlator_t *this)
{
    struct ios_conf *conf = NULL;
    FILE *stats_logfp = NULL;
    FILE *samples_logfp = NULL;
    struct ios_dump_args args = {0};
    int i;
    int stats_bytes_written = 0;
    int samples_bytes_written = 0;
    char stats_filename[PATH_MAX];
    char samples_filename[PATH_MAX];
    char *xlator_name;
    char *instance_name;
    gf_boolean_t log_stats_fopen_failure = _gf_true;
    gf_boolean_t log_samples_fopen_failure = _gf_true;
    int old_cancel_type;

    conf = this->private;
    gf_log(this->name, GF_LOG_INFO,
           "IO stats dump thread started, "
           "polling IO stats every %d seconds",
           conf->ios_dump_interval);
    xlator_name = strdupa(conf->unique_id);
    for (i = 0; i < strlen(xlator_name); i++) {
        if (xlator_name[i] == '/')
            xlator_name[i] = '_';
    }
    instance_name = this->instance_name;
    if (this->name && strcmp(this->name, "glustershd") == 0) {
        xlator_name = "shd";
    } else if (this->prev && strcmp(this->prev->name, "nfs-server") == 0) {
        xlator_name = "nfsd";
        instance_name = this->prev->instance_name;
    }
    if (sys_mkdir(_IOS_DUMP_DIR, S_IRWXU | S_IRWXO | S_IRWXG) == (-1)) {
        if (errno != EEXIST) {
            gf_log(this->name, GF_LOG_ERROR,
                   "could not create stats-dump directory %s", _IOS_DUMP_DIR);
            goto out;
        }
    }
    if (sys_mkdir(_IOS_SAMP_DIR, S_IRWXU | S_IRWXO | S_IRWXG) == (-1)) {
        if (errno != EEXIST) {
            gf_log(this->name, GF_LOG_ERROR,
                   "could not create stats-sample directory %s", _IOS_SAMP_DIR);
            goto out;
        }
    }
    if (instance_name) {
        stats_bytes_written = snprintf(stats_filename, PATH_MAX,
                                       "%s/%s_%s_%s.dump", _IOS_DUMP_DIR,
                                       __progname, xlator_name, instance_name);
        samples_bytes_written = snprintf(
            samples_filename, PATH_MAX, "%s/%s_%s_%s.samp", _IOS_SAMP_DIR,
            __progname, xlator_name, instance_name);
    } else {
        stats_bytes_written = snprintf(stats_filename, PATH_MAX,
                                       "%s/%s_%s.dump", _IOS_DUMP_DIR,
                                       __progname, xlator_name);
        samples_bytes_written = snprintf(samples_filename, PATH_MAX,
                                         "%s/%s_%s.samp", _IOS_SAMP_DIR,
                                         __progname, xlator_name);
    }
    if ((stats_bytes_written >= PATH_MAX) ||
        (samples_bytes_written >= PATH_MAX)) {
        gf_log(this->name, GF_LOG_ERROR,
               "Invalid path for stats dump (%s) and/or latency "
               "samples (%s)",
               stats_filename, samples_filename);
        goto out;
    }
    while (1) {
        if (conf->dump_thread_should_die)
            break;
        (void)pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS,
                                    &old_cancel_type);
        sleep(conf->ios_dump_interval);
        (void)pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &old_cancel_type);
        /*
         * It's not clear whether we should reopen this each time, or
         * just hold it open and rewind/truncate on each iteration.
         * Leaving it alone for now.
         */
        stats_logfp = fopen(stats_filename, "w+");
        if (stats_logfp) {
            (void)ios_dump_args_init(&args, conf->dump_format, stats_logfp);
            io_stats_dump(this, &args, GF_IOS_INFO_ALL, _gf_false);
            fclose(stats_logfp);
            log_stats_fopen_failure = _gf_true;
        } else if (log_stats_fopen_failure) {
            gf_log(this->name, GF_LOG_ERROR,
                   "could not open stats-dump file %s (%s)", stats_filename,
                   strerror(errno));
            log_stats_fopen_failure = _gf_false;
        }
        samples_logfp = fopen(samples_filename, "w+");
        if (samples_logfp) {
            io_stats_dump_latency_samples_logfp(this, samples_logfp);
            fclose(samples_logfp);
            log_samples_fopen_failure = _gf_true;
        } else if (log_samples_fopen_failure) {
            gf_log(this->name, GF_LOG_ERROR,
                   "could not open samples-dump file %s (%s)", samples_filename,
                   strerror(errno));
            log_samples_fopen_failure = _gf_false;
        }
    }
out:
    conf->dump_thread_running = _gf_false;
    gf_log(this->name, GF_LOG_INFO, "IO stats dump thread terminated");
    return NULL;
}

static gf_boolean_t
match_special_xattr(dict_t *d, char *k, data_t *val, void *mdata)
{
    gf_boolean_t ret = _gf_false;
    if (fnmatch("*io*stat*dump", k, 0) == 0) {
        ret = _gf_true;
    }

    return ret;
}

int
io_stats_setxattr(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
                  int32_t flags, dict_t *xdata)
{
    struct {
        xlator_t *this;
        inode_t *inode;
        const char *path;
    } stub;

    stub.this = this;
    stub.inode = loc->inode;
    stub.path = loc->path;

    (void)dict_foreach_match(dict, match_special_xattr, NULL, conditional_dump,
                             &stub);

    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_setxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->setxattr, loc, dict, flags, xdata);
    return 0;
}

int
io_stats_getxattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
                  const char *name, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_getxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->getxattr, loc, name, xdata);
    return 0;
}

int
io_stats_removexattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
                     const char *name, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_removexattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->removexattr, loc, name, xdata);
    return 0;
}

int
io_stats_fsetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
                   int32_t flags, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_fsetxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fsetxattr, fd, dict, flags, xdata);
    return 0;
}

int
io_stats_fgetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd,
                   const char *name, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_fgetxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fgetxattr, fd, name, xdata);
    return 0;
}

int
io_stats_fremovexattr(call_frame_t *frame, xlator_t *this, fd_t *fd,
                      const char *name, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_fremovexattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fremovexattr, fd, name, xdata);
    return 0;
}

int
io_stats_opendir(call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
                 dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_opendir_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->opendir, loc, fd, xdata);
    return 0;
}

int
io_stats_readdirp(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                  off_t offset, dict_t *dict)
{
    frame->local = fd->inode;
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_readdirp_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->readdirp, fd, size, offset, dict);
    return 0;
}

int
io_stats_readdir(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                 off_t offset, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_readdir_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->readdir, fd, size, offset, xdata);
    return 0;
}

int
io_stats_fsyncdir(call_frame_t *frame, xlator_t *this, fd_t *fd,
                  int32_t datasync, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_fsyncdir_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fsyncdir, fd, datasync, xdata);
    return 0;
}

int
io_stats_access(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask,
                dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_access_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->access, loc, mask, xdata);
    return 0;
}

int
io_stats_ftruncate(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                   dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_ftruncate_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->ftruncate, fd, offset, xdata);
    return 0;
}

int
io_stats_fsetattr(call_frame_t *frame, xlator_t *this, fd_t *fd,
                  struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_setattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fsetattr, fd, stbuf, valid, xdata);
    return 0;
}

int
io_stats_fstat(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_fstat_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fstat, fd, xdata);
    return 0;
}

int
io_stats_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t mode,
                   off_t offset, size_t len, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_fallocate_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fallocate, fd, mode, offset, len,
               xdata);

    return 0;
}

int
io_stats_discard(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                 size_t len, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_discard_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->discard, fd, offset, len, xdata);

    return 0;
}

int
io_stats_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                  off_t len, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_zerofill_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->zerofill, fd, offset, len, xdata);

    return 0;
}

int32_t
io_stats_ipc(call_frame_t *frame, xlator_t *this, int32_t op, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_ipc_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->ipc, op, xdata);
    return 0;
}

int
io_stats_lk(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
            struct gf_flock *lock, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_lk_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->lk, fd, cmd, lock, xdata);
    return 0;
}

int
io_stats_rchecksum(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                   int32_t len, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_rchecksum_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->rchecksum, fd, offset, len, xdata);
    return 0;
}

int
io_stats_seek(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
              gf_seek_what_t what, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_seek_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->seek, fd, offset, what, xdata);
    return 0;
}

int
io_stats_lease(call_frame_t *frame, xlator_t *this, loc_t *loc,
               struct gf_lease *lease, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_lease_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->lease, loc, lease, xdata);
    return 0;
}

int
io_stats_getactivelk(call_frame_t *frame, xlator_t *this, loc_t *loc,
                     dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_getactivelk_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->getactivelk, loc, xdata);
    return 0;
}

int
io_stats_setactivelk(call_frame_t *frame, xlator_t *this, loc_t *loc,
                     lock_migration_info_t *locklist, dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_setactivelk_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->setactivelk, loc, locklist, xdata);
    return 0;
}

int
io_stats_compound(call_frame_t *frame, xlator_t *this, void *args,
                  dict_t *xdata)
{
    START_FOP_LATENCY(frame);

    STACK_WIND(frame, io_stats_compound_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->compound, args, xdata);
    return 0;
}

int
io_stats_release(xlator_t *this, fd_t *fd)
{
    struct ios_fd *iosfd = NULL;
    struct ios_conf *conf = NULL;

    BUMP_FOP(RELEASE);

    conf = this->private;
    if (conf) {
        LOCK(&conf->lock);
        {
            conf->cumulative.nr_opens--;
        }
        UNLOCK(&conf->lock);
    }

    ios_fd_ctx_get(fd, this, &iosfd);
    if (iosfd) {
        io_stats_dump_fd(this, iosfd);

        GF_FREE(iosfd->filename);
        GF_FREE(iosfd);
    }

    return 0;
}

int
io_stats_releasedir(xlator_t *this, fd_t *fd)
{
    BUMP_FOP(RELEASEDIR);

    return 0;
}

int
io_stats_forget(xlator_t *this, inode_t *inode)
{
    BUMP_FOP(FORGET);
    ios_stats_cleanup(this, inode);
    return 0;
}

static int
ios_init_top_stats(struct ios_conf *conf)
{
    int i = 0;

    GF_ASSERT(conf);

    for (i = 0; i < IOS_STATS_TYPE_MAX; i++) {
        conf->list[i].iosstats = GF_CALLOC(1, sizeof(*conf->list[i].iosstats),
                                           gf_io_stats_mt_ios_stat);

        if (!conf->list[i].iosstats)
            return -1;

        INIT_LIST_HEAD(&conf->list[i].iosstats->list);
        LOCK_INIT(&conf->list[i].lock);
    }

    for (i = 0; i < IOS_STATS_THRU_MAX; i++) {
        conf->thru_list[i].iosstats = GF_CALLOC(
            1, sizeof(*conf->thru_list[i].iosstats), gf_io_stats_mt_ios_stat);

        if (!conf->thru_list[i].iosstats)
            return -1;

        INIT_LIST_HEAD(&conf->thru_list[i].iosstats->list);
        LOCK_INIT(&conf->thru_list[i].lock);
    }

    return 0;
}

static void
ios_destroy_top_stats(struct ios_conf *conf)
{
    int i = 0;
    struct ios_stat_head *list_head = NULL;
    struct ios_stat_list *entry = NULL;
    struct ios_stat_list *tmp = NULL;
    struct ios_stat_list *list = NULL;
    struct ios_stat *stat = NULL;

    GF_ASSERT(conf);

    LOCK(&conf->lock);

    conf->cumulative.nr_opens = 0;
    conf->cumulative.max_nr_opens = 0;
    conf->cumulative.max_openfd_time.tv_sec = 0;
    conf->cumulative.max_openfd_time.tv_usec = 0;

    for (i = 0; i < IOS_STATS_TYPE_MAX; i++) {
        list_head = &conf->list[i];
        if (!list_head)
            continue;
        list_for_each_entry_safe(entry, tmp, &list_head->iosstats->list, list)
        {
            list = entry;
            stat = list->iosstat;
            ios_stat_unref(stat);
            list_del(&list->list);
            GF_FREE(list);
            list_head->members--;
        }
        GF_FREE(list_head->iosstats);
    }

    for (i = 0; i < IOS_STATS_THRU_MAX; i++) {
        list_head = &conf->thru_list[i];
        if (!list_head)
            continue;
        list_for_each_entry_safe(entry, tmp, &list_head->iosstats->list, list)
        {
            list = entry;
            stat = list->iosstat;
            ios_stat_unref(stat);
            list_del(&list->list);
            GF_FREE(list);
            list_head->members--;
        }
        GF_FREE(list_head->iosstats);
    }

    UNLOCK(&conf->lock);

    return;
}

static void
io_stats_clear(struct ios_conf *conf)
{
    time_t now = 0;

    GF_ASSERT(conf);
    now = gf_time();

    LOCK(&conf->lock);
    {
        ios_global_stats_clear(&conf->cumulative, now);
        ios_global_stats_clear(&conf->incremental, now);
        conf->increment = 0;
    }
    UNLOCK(&conf->lock);
}

int32_t
io_priv(xlator_t *this)
{
    int i;
    char key[GF_DUMP_MAX_BUF_LEN];
    char key_prefix_cumulative[GF_DUMP_MAX_BUF_LEN];
    char key_prefix_incremental[GF_DUMP_MAX_BUF_LEN];
    double min, max, avg;
    uint64_t count, total;
    struct ios_conf *conf = NULL;

    conf = this->private;
    if (!conf)
        return -1;

    if (!conf->count_fop_hits || !conf->measure_latency)
        return -1;

    gf_proc_dump_write("cumulative.data_read", "%" GF_PRI_ATOMIC,
                       GF_ATOMIC_GET(conf->cumulative.data_read));
    gf_proc_dump_write("cumulative.data_written", "%" GF_PRI_ATOMIC,
                       GF_ATOMIC_GET(conf->cumulative.data_written));

    gf_proc_dump_write("incremental.data_read", "%" GF_PRI_ATOMIC,
                       GF_ATOMIC_GET(conf->incremental.data_read));
    gf_proc_dump_write("incremental.data_written", "%" GF_PRI_ATOMIC,
                       GF_ATOMIC_GET(conf->incremental.data_written));

    snprintf(key_prefix_cumulative, GF_DUMP_MAX_BUF_LEN, "%s.cumulative",
             this->name);
    snprintf(key_prefix_incremental, GF_DUMP_MAX_BUF_LEN, "%s.incremental",
             this->name);

    for (i = 0; i < GF_FOP_MAXVALUE; i++) {
        count = GF_ATOMIC_GET(conf->cumulative.fop_hits[i]);
        total = conf->cumulative.latency[i].total;
        min = conf->cumulative.latency[i].min;
        max = conf->cumulative.latency[i].max;
        avg = conf->cumulative.latency[i].avg;

        gf_proc_dump_build_key(key, key_prefix_cumulative, "%s",
                               (char *)gf_fop_list[i]);

        gf_proc_dump_write(key, "%" PRId64 ",%" PRId64 ",%.03f,%.03f,%.03f",
                           count, total, min, max, avg);

        count = GF_ATOMIC_GET(conf->incremental.fop_hits[i]);
        total = conf->incremental.latency[i].total;
        min = conf->incremental.latency[i].min;
        max = conf->incremental.latency[i].max;
        avg = conf->incremental.latency[i].avg;

        gf_proc_dump_build_key(key, key_prefix_incremental, "%s",
                               (char *)gf_fop_list[i]);

        gf_proc_dump_write(key, "%" PRId64 ",%" PRId64 ",%.03f,%.03f,%.03f",
                           count, total, min, max, avg);
    }

    return 0;
}

static void
ios_set_log_format_code(struct ios_conf *conf, char *dump_format_str)
{
    if (strcmp(dump_format_str, "json") == 0)
        conf->dump_format = IOS_DUMP_TYPE_JSON_FILE;
    else if (strcmp(dump_format_str, "text") == 0)
        conf->dump_format = IOS_DUMP_TYPE_FILE;
    else if (strcmp(dump_format_str, "dict") == 0)
        conf->dump_format = IOS_DUMP_TYPE_DICT;
    else if (strcmp(dump_format_str, "samples") == 0)
        conf->dump_format = IOS_DUMP_TYPE_SAMPLES;
}

void
xlator_set_loglevel(xlator_t *this, int log_level)
{
    glusterfs_ctx_t *ctx = NULL;
    glusterfs_graph_t *active = NULL;
    xlator_t *top = NULL;
    xlator_t *trav = this;

    ctx = this->ctx;
    GF_ASSERT(ctx);
    active = ctx->active;
    top = active->first;

    if (log_level == -1)
        return;

    if (ctx->cmd_args.brick_mux) {
        /* Set log-level for all brick xlators */
        top->loglevel = log_level;

        /* Set log-level for parent xlator */
        if (this->parents)
            this->parents->xlator->loglevel = log_level;

        while (trav) {
            trav->loglevel = log_level;
            trav = trav->next;
        }
    } else {
        gf_log_set_loglevel(this->ctx, log_level);
    }
}

void
ios_sample_buf_size_configure(char *name, struct ios_conf *conf)
{
    conf->ios_sample_buf_size = 1024;
    gf_log(name, GF_LOG_INFO,
           "Configure ios_sample_buf "
           " size is 1024 because ios_sample_interval is 0");
}

int
reconfigure(xlator_t *this, dict_t *options)
{
    struct ios_conf *conf = NULL;
    int ret = -1;
    char *sys_log_str = NULL;
    char *log_format_str = NULL;
    char *logger_str = NULL;
    char *dump_format_str = NULL;
    int sys_log_level = -1;
    char *log_str = NULL;
    int log_level = -1;
    int log_format = -1;
    int logger = -1;
    uint32_t log_buf_size = 0;
    uint32_t log_flush_timeout = 0;
    int32_t old_dump_interval;
    int32_t threads;

    if (!this || !this->private)
        goto out;

    conf = this->private;

    GF_OPTION_RECONF("dump-fd-stats", conf->dump_fd_stats, options, bool, out);

    GF_OPTION_RECONF("count-fop-hits", conf->count_fop_hits, options, bool,
                     out);

    GF_OPTION_RECONF("latency-measurement", conf->measure_latency, options,
                     bool, out);

    old_dump_interval = conf->ios_dump_interval;
    GF_OPTION_RECONF("ios-dump-interval", conf->ios_dump_interval, options,
                     int32, out);
    if ((old_dump_interval <= 0) && (conf->ios_dump_interval > 0)) {
        conf->dump_thread_running = _gf_true;
        conf->dump_thread_should_die = _gf_false;
        ret = gf_thread_create(&conf->dump_thread, NULL,
                               (void *)&_ios_dump_thread, this, "iosdump");
        if (ret) {
            conf->dump_thread_running = _gf_false;
            gf_log(this ? this->name : "io-stats", GF_LOG_ERROR,
                   "Failed to start thread"
                   "while reconfigure. Returning %d",
                   ret);
            goto out;
        }
    } else if ((old_dump_interval > 0) && (conf->ios_dump_interval == 0)) {
        _ios_destroy_dump_thread(conf);
    }

    GF_OPTION_RECONF("ios-sample-interval", conf->ios_sample_interval, options,
                     int32, out);
    GF_OPTION_RECONF("ios-dump-format", dump_format_str, options, str, out);
    ios_set_log_format_code(conf, dump_format_str);
    if (conf->ios_sample_interval) {
        GF_OPTION_RECONF("ios-sample-buf-size", conf->ios_sample_buf_size, options,
                         int32, out);
    } else {
        ios_sample_buf_size_configure (this->name, conf);
    }

    GF_OPTION_RECONF("sys-log-level", sys_log_str, options, str, out);
    if (sys_log_str) {
        sys_log_level = glusterd_check_log_level(sys_log_str);
        set_sys_log_level(sys_log_level);
    }

    GF_OPTION_RECONF("log-level", log_str, options, str, out);
    if (log_str) {
        log_level = glusterd_check_log_level(log_str);
        /* Set loglevel for all children and server xlators */
        xlator_set_loglevel(this, log_level);
    }

    GF_OPTION_RECONF("logger", logger_str, options, str, out);
    if (logger_str) {
        logger = gf_check_logger(logger_str);
        gf_log_set_logger(logger);
    }

    GF_OPTION_RECONF("log-format", log_format_str, options, str, out);
    if (log_format_str) {
        log_format = gf_check_log_format(log_format_str);
        gf_log_set_logformat(log_format);
    }

    GF_OPTION_RECONF("log-buf-size", log_buf_size, options, uint32, out);
    gf_log_set_log_buf_size(log_buf_size);

    GF_OPTION_RECONF("log-flush-timeout", log_flush_timeout, options, time,
                     out);
    gf_log_set_log_flush_timeout(log_flush_timeout);

    GF_OPTION_RECONF("threads", threads, options, int32, out);
    gf_async_adjust_threads(threads);

    ret = 0;
out:
    gf_log(this ? this->name : "io-stats", GF_LOG_DEBUG,
           "reconfigure returning %d", ret);
    return ret;
}

int32_t
mem_acct_init(xlator_t *this)
{
    int ret = -1;

    if (!this)
        return ret;

    ret = xlator_mem_acct_init(this, gf_io_stats_mt_end + 1);

    if (ret != 0) {
        gf_log(this->name, GF_LOG_ERROR,
               "Memory accounting init"
               " failed");
        return ret;
    }

    return ret;
}

void
ios_conf_destroy(struct ios_conf *conf)
{
    if (!conf)
        return;

    ios_destroy_top_stats(conf);
    _ios_destroy_dump_thread(conf);
    ios_destroy_sample_buf(conf->ios_sample_buf);
    LOCK_DESTROY(&conf->lock);
    gf_dnscache_deinit(conf->dnscache);
    GF_FREE(conf);
}

static void
ios_init_stats(struct ios_global_stats *stats)
{
    int i = 0;

    GF_ATOMIC_INIT(stats->data_read, 0);
    GF_ATOMIC_INIT(stats->data_written, 0);

    for (i = 0; i < IOS_BLOCK_COUNT_SIZE; i++) {
        GF_ATOMIC_INIT(stats->block_count_write[i], 0);
        GF_ATOMIC_INIT(stats->block_count_read[i], 0);
    }

    for (i = 0; i < GF_FOP_MAXVALUE; i++)
        GF_ATOMIC_INIT(stats->fop_hits[i], 0);

    for (i = 0; i < GF_UPCALL_FLAGS_MAXVALUE; i++)
        GF_ATOMIC_INIT(stats->upcall_hits[i], 0);

    stats->started_at = gf_time();
}

int
init(xlator_t *this)
{
    struct ios_conf *conf = NULL;
    char *volume_id = NULL;
    char *sys_log_str = NULL;
    char *logger_str = NULL;
    char *log_format_str = NULL;
    char *dump_format_str = NULL;
    int logger = -1;
    int log_format = -1;
    int sys_log_level = -1;
    char *log_str = NULL;
    int log_level = -1;
    int ret = -1;
    uint32_t log_buf_size = 0;
    uint32_t log_flush_timeout = 0;
    int32_t threads;

    if (!this)
        return -1;

    if (!this->children) {
        gf_log(this->name, GF_LOG_ERROR,
               "io_stats translator requires at least one subvolume");
        return -1;
    }

    if (!this->parents) {
        /* This is very much valid as io-stats currently is loaded
         * on top of volumes on both client and server, hence this is
         * not an warning message */
        gf_log(this->name, GF_LOG_DEBUG, "dangling volume. check volfile ");
    }

    conf = GF_CALLOC(1, sizeof(*conf), gf_io_stats_mt_ios_conf);

    if (!conf)
        goto out;

    if (dict_get_str(this->options, "unique-id", &conf->unique_id) != 0) {
        /* This is always set on servers, so we must be a client. */
        conf->unique_id = this->name;
    }

    ret = dict_get_strn(this->options, "volume-id", SLEN("volume-id"),
                        &volume_id);
    if (!ret) {
        strncpy(this->graph->volume_id, volume_id, GF_UUID_BUF_SIZE);
    }
    /*
     * Init it just after calloc, so that we are sure the lock is inited
     * in case of error paths.
     */
    LOCK_INIT(&conf->lock);
    LOCK_INIT(&conf->ios_sampling_lock);

    ios_init_stats(&conf->cumulative);
    ios_init_stats(&conf->incremental);

    ret = ios_init_top_stats(conf);
    if (ret)
        goto out;

    GF_OPTION_INIT("dump-fd-stats", conf->dump_fd_stats, bool, out);

    GF_OPTION_INIT("count-fop-hits", conf->count_fop_hits, bool, out);

    GF_OPTION_INIT("latency-measurement", conf->measure_latency, bool, out);

    GF_OPTION_INIT("ios-dump-interval", conf->ios_dump_interval, int32, out);

    GF_OPTION_INIT("ios-sample-interval", conf->ios_sample_interval, int32,
                   out);

    GF_OPTION_INIT("ios-dump-format", dump_format_str, str, out);
    ios_set_log_format_code(conf, dump_format_str);

    if (conf->ios_sample_interval) {
        GF_OPTION_INIT("ios-sample-buf-size", conf->ios_sample_buf_size, int32,
                       out);
    } else {
        ios_sample_buf_size_configure (this->name, conf);
    }

    ret = ios_init_sample_buf(conf);
    if (ret) {
        gf_log(this->name, GF_LOG_ERROR, "Out of memory.");
        goto out;
    }

    GF_OPTION_INIT("ios-dnscache-ttl-sec", conf->ios_dnscache_ttl_sec, int32,
                   out);
    conf->dnscache = gf_dnscache_init(conf->ios_dnscache_ttl_sec);
    if (!conf->dnscache) {
        ret = -1;
        goto out;
    }

    GF_OPTION_INIT("sys-log-level", sys_log_str, str, out);
    if (sys_log_str) {
        sys_log_level = glusterd_check_log_level(sys_log_str);
        set_sys_log_level(sys_log_level);
    }

    GF_OPTION_INIT("log-level", log_str, str, out);
    if (log_str) {
        log_level = glusterd_check_log_level(log_str);
        if (DEFAULT_LOG_LEVEL != log_level)
            gf_log_set_loglevel(this->ctx, log_level);
    }

    GF_OPTION_INIT("logger", logger_str, str, out);
    if (logger_str) {
        logger = gf_check_logger(logger_str);
        gf_log_set_logger(logger);
    }

    GF_OPTION_INIT("log-format", log_format_str, str, out);
    if (log_format_str) {
        log_format = gf_check_log_format(log_format_str);
        gf_log_set_logformat(log_format);
    }

    GF_OPTION_INIT("log-buf-size", log_buf_size, uint32, out);
    gf_log_set_log_buf_size(log_buf_size);

    GF_OPTION_INIT("log-flush-timeout", log_flush_timeout, time, out);
    gf_log_set_log_flush_timeout(log_flush_timeout);

    GF_OPTION_INIT("threads", threads, int32, out);
    gf_async_adjust_threads(threads);

    this->private = conf;
    if (conf->ios_dump_interval > 0) {
        conf->dump_thread_running = _gf_true;
        conf->dump_thread_should_die = _gf_false;
        ret = gf_thread_create(&conf->dump_thread, NULL,
                               (void *)&_ios_dump_thread, this, "iosdump");
        if (ret) {
            conf->dump_thread_running = _gf_false;
            gf_log(this ? this->name : "io-stats", GF_LOG_ERROR,
                   "Failed to start thread"
                   "in init. Returning %d",
                   ret);
            goto out;
        }
    }
    return 0;
out:
    ios_conf_destroy(conf);
    return ret;
}

void
fini(xlator_t *this)
{
    struct ios_conf *conf = NULL;

    if (!this)
        return;

    conf = this->private;

    ios_conf_destroy(conf);
    this->private = NULL;
    gf_log(this->name, GF_LOG_INFO, "io-stats translator unloaded");
    return;
}

int
notify(xlator_t *this, int32_t event, void *data, ...)
{
    int ret = 0;
    struct ios_dump_args args = {0};
    dict_t *output = NULL;
    dict_t *dict = NULL;
    int32_t op = 0;
    int32_t list_cnt = 0;
    double throughput = 0;
    double time = 0;
    gf_boolean_t is_peek = _gf_false;
    va_list ap;
    struct gf_upcall *up_data = NULL;
    struct gf_upcall_cache_invalidation *up_ci = NULL;

    dict = data;
    va_start(ap, data);
    output = va_arg(ap, dict_t *);
    va_end(ap);
    switch (event) {
        case GF_EVENT_TRANSLATOR_INFO:
            ret = dict_get_str_boolean(dict, "clear-stats", _gf_false);
            if (ret) {
                ret = dict_set_int32(output, "top-op", op);
                if (ret) {
                    gf_log(this->name, GF_LOG_ERROR,
                           "Failed to set top-op in dict");
                    goto out;
                }
                ios_destroy_top_stats(this->private);
                ret = ios_init_top_stats(this->private);
                if (ret)
                    gf_log(this->name, GF_LOG_ERROR,
                           "Failed to reset top stats");
                ret = dict_set_int32(output, "stats-cleared", ret ? 0 : 1);
                if (ret)
                    gf_log(this->name, GF_LOG_ERROR,
                           "Failed to set stats-cleared"
                           " in dict");
                goto out;
            }

            ret = dict_get_int32(dict, "top-op", &op);
            if (!ret) {
                ret = dict_get_int32(dict, "list-cnt", &list_cnt);
                if (op > IOS_STATS_TYPE_NONE && op < IOS_STATS_TYPE_MAX)
                    ret = io_stats_dump_stats_to_dict(this, output, op,
                                                      list_cnt);
                if (op == IOS_STATS_TYPE_READ_THROUGHPUT ||
                    op == IOS_STATS_TYPE_WRITE_THROUGHPUT) {
                    ret = dict_get_double(dict, "throughput", &throughput);
                    if (!ret) {
                        ret = dict_get_double(dict, "time", &time);
                        if (ret)
                            goto out;
                        ret = dict_set_double(output, "throughput", throughput);
                        if (ret)
                            goto out;
                        ret = dict_set_double(output, "time", time);
                        if (ret)
                            goto out;
                    }
                    ret = 0;
                }
            } else {
                ret = dict_get_int32(dict, "info-op", &op);
                if (ret || op < GF_IOS_INFO_ALL || GF_IOS_INFO_CLEAR < op)
                    op = GF_IOS_INFO_ALL;

                ret = dict_set_int32(output, "info-op", op);
                if (ret) {
                    gf_log(this->name, GF_LOG_ERROR,
                           "Failed to set info-op in dict");
                    goto out;
                }

                if (GF_IOS_INFO_CLEAR == op) {
                    io_stats_clear(this->private);

                    ret = dict_set_int32(output, "stats-cleared", 1);
                    if (ret)
                        gf_log(this->name, GF_LOG_ERROR,
                               "Failed to set stats-cleared"
                               " in dict");
                } else {
                    ret = dict_get_str_boolean(dict, "peek", _gf_false);
                    if (-1 != ret)
                        is_peek = ret;

                    (void)ios_dump_args_init(&args, IOS_DUMP_TYPE_DICT, output);
                    ret = io_stats_dump(this, &args, op, is_peek);
                }
            }
            break;
        case GF_EVENT_UPCALL:
            up_data = (struct gf_upcall *)data;
            ios_bump_upcall(this, GF_UPCALL);

            switch (up_data->event_type) {
                case GF_UPCALL_RECALL_LEASE:
                    ios_bump_upcall(this, GF_UPCALL_LEASE_RECALL);
                    break;
                case GF_UPCALL_CACHE_INVALIDATION:
                    up_ci = (struct gf_upcall_cache_invalidation *)
                                up_data->data;
                    if (up_ci->flags & (UP_XATTR | UP_XATTR_RM))
                        ios_bump_upcall(this, GF_UPCALL_CI_XATTR);
                    if (up_ci->flags & IATT_UPDATE_FLAGS)
                        ios_bump_upcall(this, GF_UPCALL_CI_STAT);
                    if (up_ci->flags & UP_RENAME_FLAGS)
                        ios_bump_upcall(this, GF_UPCALL_CI_RENAME);
                    if (up_ci->flags & UP_FORGET)
                        ios_bump_upcall(this, GF_UPCALL_CI_FORGET);
                    if (up_ci->flags & UP_NLINK)
                        ios_bump_upcall(this, GF_UPCALL_CI_NLINK);
                    break;
                default:
                    gf_msg_debug(this->name, 0,
                                 "Unknown upcall event "
                                 "type :%d",
                                 up_data->event_type);
                    break;
            }

            default_notify(this, event, data);
            break;
        default:
            default_notify(this, event, data);
            break;
    }
out:
    return ret;
}

struct xlator_dumpops dumpops = {.priv = io_priv};

struct xlator_fops fops = {
    .stat = io_stats_stat,
    .readlink = io_stats_readlink,
    .mknod = io_stats_mknod,
    .mkdir = io_stats_mkdir,
    .unlink = io_stats_unlink,
    .rmdir = io_stats_rmdir,
    .symlink = io_stats_symlink,
    .rename = io_stats_rename,
    .link = io_stats_link,
    .truncate = io_stats_truncate,
    .open = io_stats_open,
    .readv = io_stats_readv,
    .writev = io_stats_writev,
    .statfs = io_stats_statfs,
    .flush = io_stats_flush,
    .fsync = io_stats_fsync,
    .setxattr = io_stats_setxattr,
    .getxattr = io_stats_getxattr,
    .removexattr = io_stats_removexattr,
    .fsetxattr = io_stats_fsetxattr,
    .fgetxattr = io_stats_fgetxattr,
    .fremovexattr = io_stats_fremovexattr,
    .opendir = io_stats_opendir,
    .readdir = io_stats_readdir,
    .readdirp = io_stats_readdirp,
    .fsyncdir = io_stats_fsyncdir,
    .access = io_stats_access,
    .ftruncate = io_stats_ftruncate,
    .fstat = io_stats_fstat,
    .create = io_stats_create,
    .lk = io_stats_lk,
    .inodelk = io_stats_inodelk,
    .finodelk = io_stats_finodelk,
    .entrylk = io_stats_entrylk,
    .fentrylk = io_stats_fentrylk,
    .lookup = io_stats_lookup,
    .xattrop = io_stats_xattrop,
    .fxattrop = io_stats_fxattrop,
    .setattr = io_stats_setattr,
    .fsetattr = io_stats_fsetattr,
    .fallocate = io_stats_fallocate,
    .discard = io_stats_discard,
    .zerofill = io_stats_zerofill,
    .ipc = io_stats_ipc,
    .rchecksum = io_stats_rchecksum,
    .seek = io_stats_seek,
    .lease = io_stats_lease,
    .getactivelk = io_stats_getactivelk,
    .setactivelk = io_stats_setactivelk,
    .compound = io_stats_compound,
    .copy_file_range = io_stats_copy_file_range,
};

struct xlator_cbks cbks = {
    .release = io_stats_release,
    .releasedir = io_stats_releasedir,
    .forget = io_stats_forget,
};

struct volume_options options[] = {
    {.key = {"dump-fd-stats"},
     .op_version = {1},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"io-stats"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "off",
     .description = "If on stats related to file-operations would be "
                    "tracked inside GlusterFS data-structures."},
    {.key = {"ios-dump-interval"},
     .type = GF_OPTION_TYPE_INT,
     .op_version = {1},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"io-stats"},
     .min = 0,
     .max = 3600,
     .default_value = "0",
     .description = "Interval (in seconds) at which to auto-dump "
                    "statistics. Zero disables automatic dumping."},
    {.key = {"ios-sample-interval"},
     .type = GF_OPTION_TYPE_INT,
     .op_version = {1},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"io-stats"},
     .min = 0,
     .max = 65535,
     .default_value = "0",
     .description = "Interval in which we want to collect FOP latency "
                    "samples.  2 means collect a sample every 2nd FOP."},
    {.key = {"ios-dump-format"},
     .type = GF_OPTION_TYPE_STR,
     .op_version = {GD_OP_VERSION_3_12_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"io-stats"},
     .default_value = "json",
     .description = " The dump-format option specifies the format in which"
                    " to dump the statistics. Select between \"text\", "
                    "\"json\", \"dict\" and \"samples\". Default is "
                    "\"json\".",
     .value = {"text", "json", "dict", "samples"}},
    {.key = {"ios-sample-buf-size"},
     .type = GF_OPTION_TYPE_INT,
     .op_version = {1},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"io-stats"},
     .min = 1024,
     .max = 1024 * 1024,
     .default_value = "65535",
     .description = "The maximum size of our FOP sampling ring buffer."},
    {.key = {"ios-dnscache-ttl-sec"},
     .type = GF_OPTION_TYPE_INT,
     .op_version = {1},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"io-stats"},
     .min = 1,
     .max = 3600 * 72,
     .default_value = "86400",
     .description = "The interval after wish a cached DNS entry will be "
                    "re-validated.  Default: 24 hrs"},
    {.key = {"latency-measurement"},
     .type = GF_OPTION_TYPE_BOOL,
     .op_version = {1},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"io-stats"},
     .default_value = "off",
     .description = "If on stats related to the latency of each operation "
                    "would be tracked inside GlusterFS data-structures. "},
    {
        .key = {"count-fop-hits"},
        .type = GF_OPTION_TYPE_BOOL,
        .op_version = {1},
        .flags = OPT_FLAG_SETTABLE,
        .tags = {"io-stats"},
    },
    {.key = {"log-level"},
     .type = GF_OPTION_TYPE_STR,
     .value = {"DEBUG", "WARNING", "ERROR", "INFO", "CRITICAL", "NONE",
               "TRACE"}},

    /* These are synthetic entries to assist validation of CLI's  *
     *  volume set  command                                       */
    {.key = {"client-log-level"},
     .type = GF_OPTION_TYPE_STR,
     .op_version = {1},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
     .tags = {"io-stats"},
     .default_value = "INFO",
     .description = "Changes the log-level of the clients",
     .value = {"DEBUG", "WARNING", "ERROR", "INFO", "CRITICAL", "NONE",
               "TRACE"}},
    {.key = {"sys-log-level"},
     .type = GF_OPTION_TYPE_STR,
     .default_value = "CRITICAL",
     .description = "Gluster's syslog log-level",
     .value = {"WARNING", "ERROR", "INFO", "CRITICAL"}},
    {.key = {"brick-log-level"},
     .type = GF_OPTION_TYPE_STR,
     .op_version = {1},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"io-stats"},
     .default_value = "INFO",
     .description = "Changes the log-level of the bricks",
     .value = {"DEBUG", "WARNING", "ERROR", "INFO", "CRITICAL", "NONE",
               "TRACE"}},
    {.key = {"logger"},
     .type = GF_OPTION_TYPE_STR,
     .value = {GF_LOGGER_GLUSTER_LOG, GF_LOGGER_SYSLOG}},
    {.key = {"client-logger"},
     .type = GF_OPTION_TYPE_STR,
     .op_version = {GD_OP_VERSION_3_6_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
     .tags = {"io-stats"},
     .default_value = GF_LOGGER_GLUSTER_LOG,
     .description = "Changes the logging sub-system to log to, for the "
                    "clients",
     .value = {GF_LOGGER_GLUSTER_LOG, GF_LOGGER_SYSLOG}},
    {.key = {"brick-logger"},
     .type = GF_OPTION_TYPE_STR,
     .op_version = {GD_OP_VERSION_3_6_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"io-stats"},
     .default_value = GF_LOGGER_GLUSTER_LOG,
     .description = "Changes the logging sub-system to log to, for the "
                    "bricks",
     .value = {GF_LOGGER_GLUSTER_LOG, GF_LOGGER_SYSLOG}},
    {.key = {"log-format"},
     .type = GF_OPTION_TYPE_STR,
     .value = {GF_LOG_FORMAT_NO_MSG_ID, GF_LOG_FORMAT_WITH_MSG_ID}},
    {.key = {"client-log-format"},
     .type = GF_OPTION_TYPE_STR,
     .op_version = {GD_OP_VERSION_3_6_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
     .tags = {"io-stats"},
     .default_value = GF_LOG_FORMAT_WITH_MSG_ID,
     .description = "Changes log format for the clients",
     .value = {GF_LOG_FORMAT_NO_MSG_ID, GF_LOG_FORMAT_WITH_MSG_ID}},
    {.key = {"brick-log-format"},
     .type = GF_OPTION_TYPE_STR,
     .op_version = {GD_OP_VERSION_3_6_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"io-stats"},
     .default_value = GF_LOG_FORMAT_WITH_MSG_ID,
     .description = "Changes the log format for the bricks",
     .value = {GF_LOG_FORMAT_NO_MSG_ID, GF_LOG_FORMAT_WITH_MSG_ID}},
    {
        .key = {"log-buf-size"},
        .type = GF_OPTION_TYPE_INT,
        .min = GF_LOG_LRU_BUFSIZE_MIN,
        .max = GF_LOG_LRU_BUFSIZE_MAX,
        .default_value = "5",
    },
    {.key = {"client-log-buf-size"},
     .type = GF_OPTION_TYPE_INT,
     .op_version = {GD_OP_VERSION_3_6_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
     .tags = {"io-stats"},
     .min = GF_LOG_LRU_BUFSIZE_MIN,
     .max = GF_LOG_LRU_BUFSIZE_MAX,
     .default_value = "5",
     .description = "This option determines the maximum number of unique "
                    "log messages that can be buffered for a time equal to"
                    " the value of the option client-log-flush-timeout."},
    {.key = {"brick-log-buf-size"},
     .type = GF_OPTION_TYPE_INT,
     .op_version = {GD_OP_VERSION_3_6_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"io-stats"},
     .min = GF_LOG_LRU_BUFSIZE_MIN,
     .max = GF_LOG_LRU_BUFSIZE_MAX,
     .default_value = "5",
     .description = "This option determines the maximum number of unique "
                    "log messages that can be buffered for a time equal to"
                    " the value of the option brick-log-flush-timeout."},
    {
        .key = {"log-flush-timeout"},
        .type = GF_OPTION_TYPE_TIME,
        .min = GF_LOG_FLUSH_TIMEOUT_MIN,
        .max = GF_LOG_FLUSH_TIMEOUT_MAX,
        .default_value = "120",
    },
    {.key = {"client-log-flush-timeout"},
     .type = GF_OPTION_TYPE_TIME,
     .op_version = {GD_OP_VERSION_3_6_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
     .tags = {"io-stats"},
     .min = GF_LOG_FLUSH_TIMEOUT_MIN,
     .max = GF_LOG_FLUSH_TIMEOUT_MAX,
     .default_value = "120",
     .description = "This option determines the maximum number of unique "
                    "log messages that can be buffered for a time equal to"
                    " the value of the option client-log-flush-timeout."},
    {.key = {"brick-log-flush-timeout"},
     .type = GF_OPTION_TYPE_TIME,
     .op_version = {GD_OP_VERSION_3_6_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"io-stats"},
     .min = GF_LOG_FLUSH_TIMEOUT_MIN,
     .max = GF_LOG_FLUSH_TIMEOUT_MAX,
     .default_value = "120",
     .description = "This option determines the maximum number of unique "
                    "log messages that can be buffered for a time equal to"
                    " the value of the option brick-log-flush-timeout."},
    {.key = {"unique-id"},
     .type = GF_OPTION_TYPE_STR,
     .default_value = "/no/such/path",
     .description = "Unique ID for our files."},
    {.key = {"global-threading"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "off",
     .op_version = {GD_OP_VERSION_6_0},
     .flags = OPT_FLAG_SETTABLE,
     .tags = {"io-stats", "threading"},
     .description = "This option enables the global threading support for "
                    "bricks. If enabled, it's recommended to also enable "
                    "'performance.iot-pass-through'"},
    {.key = {"threads"}, .type = GF_OPTION_TYPE_INT},
    {.key = {"brick-threads"},
     .type = GF_OPTION_TYPE_INT,
     .default_value = "16",
     .min = 0,
     .max = GF_ASYNC_MAX_THREADS,
     .op_version = {GD_OP_VERSION_6_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"io-stats", "threading"},
     .description = "When global threading is used, this value determines the "
                    "maximum amount of threads that can be created on bricks"},
    {.key = {"client-threads"},
     .type = GF_OPTION_TYPE_INT,
     .default_value = "16",
     .min = 0,
     .max = GF_ASYNC_MAX_THREADS,
     .op_version = {GD_OP_VERSION_6_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC | OPT_FLAG_CLIENT_OPT,
     .tags = {"io-stats", "threading"},
     .description = "When global threading is used, this value determines the "
                    "maximum amount of threads that can be created on clients"},
    {.key = {"volume-id"},
     .type = GF_OPTION_TYPE_STR,
     .op_version = {GD_OP_VERSION_7_1},
     .tags = {"global", "volume-id"},
     .description =
         "This option points to the 'unique' UUID particular to this "
         "volume, which would be set in 'graph->volume_id'"},
    {.key = {NULL}},
};

xlator_api_t xlator_api = {
    .init = init,
    .fini = fini,
    .notify = notify,
    .reconfigure = reconfigure,
    .mem_acct_init = mem_acct_init,
    .op_version = {1}, /* Present from the initial version */
    .dumpops = &dumpops,
    .fops = &fops,
    .cbks = &cbks,
    .options = options,
    .identifier = "io-stats",
    .category = GF_MAINTAINED,
};
