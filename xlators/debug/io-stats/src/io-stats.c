/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "xlator.h"

/**
 * xlators/debug/io_stats :
 *    This translator maintains statistics of all filesystem activity
 *    happening through it. The kind of statistics include:
 *
 *  a) total read data - since process start, last interval and per fd
 *  b) total write data - since process start, last interval and per fd
 *  c) counts of read IO block size - since process start, last interval and per fd
 *  d) counts of write IO block size - since process start, last interval and per fd
 *  e) counts of all FOP types passing through it
 *
 *  Usage: setfattr -n io-stats-dump /tmp/filename /mnt/gluster
 *
 */

#include <fnmatch.h>
#include <errno.h>
#include "glusterfs.h"
#include "xlator.h"
#include "io-stats-mem-types.h"
#include <stdarg.h>
#include "defaults.h"
#include "logging.h"
#include "cli1-xdr.h"
#include "statedump.h"
#include "io-stats.h"
#include "syncop.h"
#include "hashfn.h"
#include "syscall.h"
#include <pwd.h>
#include <grp.h>

const char *get_ns_from_hash (struct ios_conf *conf, const ns_info_t *info);
int ios_conf_load_namespace_conf (struct ios_conf *conf);

gf_boolean_t should_throttle_fop (call_frame_t *frame, xlator_t *this);

int _ios_swap_sample_buf (xlator_t *this, ios_sample_buf_t **dest);

/* A lot of io-stats logging code depends on the io-stats translator needing
 * the top xlator's name, which is special. For example, on bricks, the name
 * is the path to the backend storage directory.
 *
 * Fails gracefully if there is no xlator name, returning "Unknown". */
const char *
get_top_xlator_name (xlator_t *this)
{
        const char *name = NULL;

        while (this->parents && this->parents->xlator) {
                this = this->parents->xlator;
        }

        this = FIRST_CHILD (this);

        if (this && this->name) {
                return this->name;
        } else {
                gf_log (GF_IO_STATS, GF_LOG_ERROR,
                        "Cannot retrieve top-of-stack translator name.");
                return "Unknown";
        }
}

static void ios_free_local (call_frame_t *frame)
{
        struct ios_local *local = frame->local;
        ios_local_free (local);
        frame->local = NULL;
}

static void ios_track_loc (call_frame_t *frame, loc_t *loc)
{
        struct ios_local *local = NULL;
        if (loc && loc->path) {
                /* Check if frame->local is already set (it should
                 * only be set by either ios_track_loc() or
                 * ios_track_fd()). In other words, this check
                 * allows us to chain calls to ios_track_loc()
                 * and ios_track_fd() without clobbering frame->local
                 * in the process.
                 */
                if (frame->local) {
                        local = frame->local;
                } else {
                        local = ios_local_new ();
                }
                loc_copy (&local->loc, loc);
                frame->local = local;
        }
}

static void ios_track_fd (call_frame_t *frame, fd_t *fd)
{
        struct ios_local *local = NULL;
        if (fd && fd->inode) {
                if (frame->local) {
                        local = frame->local;
                } else {
                        local = ios_local_new ();
                }
                local->fd = fd_ref (fd);
                local->inode = inode_ref (fd->inode);
                frame->local = local;
        }
}

#ifdef HAVE_ATOMIC_BUILTINS
#define INC_COUNTER(counter, inc, lock)                 \
        (__sync_fetch_and_add (&(counter), (inc)))
#else
#define INC_COUNTER(counter, inc, lock)                 \
        do {                                            \
                LOCK (&(lock));                         \
                {                                       \
                        counter += (inc);               \
                }                                       \
                UNLOCK (&(lock));                       \
        } while (0)
#endif

#define FOP_THROTTLE(frame, this) frame_set_throttling (frame, should_throttle_fop (frame, this))

#define END_FOP_LATENCY(frame, op)                                      \
        do {                                                            \
                struct ios_conf  *conf = NULL;                          \
                                                                        \
                conf = this->private;                                   \
                if (conf && conf->measure_latency) {                    \
                        gettimeofday (&frame->end, NULL);               \
                        update_ios_latency (conf, frame, GF_FOP_##op, 0, 0);  \
                }                                                       \
        } while (0)

#define START_FOP_LATENCY(frame)                                         \
        do {                                                             \
                struct ios_conf  *conf = NULL;                           \
                                                                         \
                conf = this->private;                                    \
                if (conf && conf->measure_latency) {                     \
                        INC_COUNTER (conf->outstanding_req, 1, conf->lock);\
                        gettimeofday (&frame->begin, NULL);              \
                } else {                                                 \
                        memset (&frame->begin, 0, sizeof (frame->begin));\
                }                                                        \
        } while (0)


#define BUMP_FOP(op)                                                    \
        do {                                                            \
                struct ios_conf  *conf = NULL;                          \
                                                                        \
                conf = this->private;                                   \
                if (!conf)                                              \
                        break;                                          \
                conf->cumulative.fop_hits[GF_FOP_##op]++;               \
                conf->incremental.fop_hits[GF_FOP_##op]++;              \
        } while (0)

#ifdef HAVE_ATOMIC_BUILTINS
#define UPDATE_PROFILE_STATS(frame, op, op_ret, op_errno)                     \
        do {                                                                  \
                struct ios_conf  *conf = NULL;                                \
                                                                              \
                if (!is_fop_latency_started (frame))                          \
                        break;                                                \
                conf = this->private;                                         \
                if (conf && conf->measure_latency &&                          \
                    conf->count_fop_hits) {                                   \
                        BUMP_FOP (op);                                        \
                        __sync_fetch_and_sub (&conf->outstanding_req, 1);     \
                        if (op_ret != 0 && op_errno > 0                       \
                            && op_errno < IOS_MAX_ERRORS) {                   \
                                __sync_fetch_and_add (                        \
                                  &conf->cumulative.errno_count[op_errno], 1); \
                                __sync_fetch_and_add (                        \
                                  &conf->incremental.errno_count[op_errno], 1); \
                        }                                                     \
                        gettimeofday (&frame->end, NULL);                     \
                        update_ios_latency (conf, frame, GF_FOP_##op, op_ret, op_errno);        \
                }                                                             \
        } while (0)
#else
#define UPDATE_PROFILE_STATS(frame, op, op_ret, op_errno)                     \
        do {                                                                  \
                struct ios_conf  *conf = NULL;                                \
                                                                              \
                if (!is_fop_latency_started (frame))                          \
                        break;                                                \
                conf = this->private;                                         \
                LOCK (&conf->lock);                                           \
                {                                                             \
                        if (conf && conf->measure_latency &&                  \
                            conf->count_fop_hits) {                           \
                                BUMP_FOP(op);                                 \
                                conf->outstanding_req -= 1;                   \
                                if (op_ret != 0 && op_errno > 0               \
                                    && op_errno < IOS_MAX_ERRORS) {           \
                                        conf->cumulative.errno_count[op_errno]++; \
                                        conf->incremental.errno_count[op_errno]++; \
                                }                                             \
                                gettimeofday (&frame->end, NULL);             \
                                update_ios_latency (conf, frame, GF_FOP_##op, op_ret, op_errno);\
                        }                                                     \
                }                                                             \
                UNLOCK (&conf->lock);                                         \
        } while (0)
#endif

#ifdef HAVE_ATOMIC_BUILTINS
#define BUMP_READ(fd, len)                                              \
        do {                                                            \
                struct ios_conf  *conf = NULL;                          \
                struct ios_fd    *iosfd = NULL;                         \
                int               lb2 = 0;                              \
                                                                        \
                conf = this->private;                                   \
                lb2 = log_base2 (len);                                  \
                ios_fd_ctx_get (fd, this, &iosfd);                      \
                if (!conf)                                              \
                        break;                                          \
                                                                        \
                __sync_fetch_and_add (&conf->cumulative.data_read,      \
                    len);                                               \
                __sync_fetch_and_add (&conf->incremental.data_read,     \
                    len);                                               \
                __sync_fetch_and_add (                                  \
                    &conf->cumulative.block_count_read[lb2], 1);        \
                __sync_fetch_and_add (                                  \
                      &conf->incremental.block_count_read[lb2], 1);     \
                if (iosfd) {                                            \
                        __sync_fetch_and_add (&iosfd->data_read, len);  \
                        __sync_fetch_and_add (                          \
                            &iosfd->block_count_read[lb2], 1);          \
                }                                                       \
        } while (0)
#else
#define BUMP_READ(fd, len)                                              \
        do {                                                            \
                struct ios_conf  *conf = NULL;                          \
                struct ios_fd    *iosfd = NULL;                         \
                int               lb2 = 0;                              \
                                                                        \
                conf = this->private;                                   \
                lb2 = log_base2 (len);                                  \
                ios_fd_ctx_get (fd, this, &iosfd);                      \
                if (!conf)                                              \
                        break;                                          \
                                                                        \
                LOCK (&conf->lock);                                     \
                {                                                       \
                        conf->cumulative.data_read += len;              \
                        conf->incremental.data_read += len;             \
                        conf->cumulative.block_count_read[lb2]++;       \
                        conf->incremental.block_count_read[lb2]++;      \
                                                                        \
                        if (iosfd) {                                    \
                                iosfd->data_read += len;                \
                                iosfd->block_count_read[lb2]++;         \
                        }                                               \
                }                                                       \
                UNLOCK (&conf->lock);                                   \
        } while (0)
#endif

#ifdef HAVE_ATOMIC_BUILTINS
#define BUMP_WRITE(fd, len)                                             \
        do {                                                            \
                struct ios_conf  *conf = NULL;                          \
                struct ios_fd    *iosfd = NULL;                         \
                int               lb2 = 0;                              \
                                                                        \
                conf = this->private;                                   \
                lb2 = log_base2 (len);                                  \
                ios_fd_ctx_get (fd, this, &iosfd);                      \
                if (!conf)                                              \
                        break;                                          \
                __sync_fetch_and_add (&conf->cumulative.data_written,   \
                        len);                                           \
                __sync_fetch_and_add (&conf->incremental.data_written,  \
                        len);                                           \
                __sync_fetch_and_add (                                  \
                        &conf->cumulative.block_count_write[lb2], 1);   \
                __sync_fetch_and_add (                                  \
                        &conf->incremental.block_count_write[lb2], 1);  \
                                                                        \
                if (iosfd) {                                            \
                        __sync_fetch_and_add (&iosfd->data_written,     \
                                len);                                   \
                        __sync_fetch_and_add (                          \
                                &iosfd->block_count_write[lb2], 1);     \
                }                                                       \
        } while (0)
#else
#define BUMP_WRITE(fd, len)                                             \
        do {                                                            \
                struct ios_conf  *conf = NULL;                          \
                struct ios_fd    *iosfd = NULL;                         \
                int               lb2 = 0;                              \
                                                                        \
                conf = this->private;                                   \
                lb2 = log_base2 (len);                                  \
                ios_fd_ctx_get (fd, this, &iosfd);                      \
                if (!conf)                                              \
                        break;                                          \
                LOCK (&conf->lock);                                     \
                {                                                       \
                        conf->cumulative.data_written += len;           \
                        conf->incremental.data_written += len;          \
                        conf->cumulative.block_count_write[lb2]++;      \
                        conf->incremental.block_count_write[lb2]++;     \
                                                                        \
                        if (iosfd) {                                    \
                                iosfd->data_written += len;             \
                                iosfd->block_count_write[lb2]++;        \
                        }                                               \
                }                                                       \
                UNLOCK (&conf->lock);                                   \
        } while (0)
#endif

#ifdef HAVE_ATOMIC_BUILTINS
#define BUMP_STATS(iosstat, type)                                              \
        do {                                                                   \
                struct ios_conf         *conf = NULL;                          \
                uint64_t                 value = 0;                            \
                                                                               \
                conf = this->private;                                          \
                value = __sync_add_and_fetch (&iosstat->counters[type], 1);    \
                ios_stat_add_to_list (&conf->list[type],                       \
                                     value, iosstat);                          \
                                                                               \
        } while (0)
#else
#define BUMP_STATS(iosstat, type)                                               \
        do {                                                                    \
                struct ios_conf         *conf = NULL;                           \
                uint64_t                 value = 0;                             \
                                                                                \
                conf = this->private;                                           \
                                                                                \
                LOCK(&iosstat->lock);                                           \
                {                                                               \
                        iosstat->counters[type]++;                              \
                        value = iosstat->counters[type];                        \
                }                                                               \
                UNLOCK (&iosstat->lock);                                        \
                ios_stat_add_to_list (&conf->list[type],                        \
                                     value, iosstat);                           \
                                                                                \
        } while (0)
#endif

#ifdef HAVE_ATOMIC_BUILTINS
#define BUMP_THROUGHPUT(iosstat, type)                                         \
        do {                                                                   \
                struct ios_conf         *conf = NULL;                          \
                double                   elapsed;                              \
                struct timeval          *begin, *end;                          \
                double                   throughput;                           \
                int                      flag = 0;                             \
                                                                               \
                begin = &frame->begin;                                         \
                end   = &frame->end;                                           \
                                                                               \
                elapsed = (end->tv_sec - begin->tv_sec) * 1e6                  \
                        + (end->tv_usec - begin->tv_usec);                     \
                throughput = op_ret / elapsed;                                 \
                                                                               \
                conf = this->private;                                          \
                                                                               \
                if (iosstat->thru_counters[type].throughput                    \
                        <= throughput) {                                       \
                        iosstat->thru_counters[type].throughput =              \
                                                        throughput;            \
                        gettimeofday (&iosstat->                               \
                                     thru_counters[type].time, NULL);          \
                       flag = 1;                                               \
               }                                                               \
               if (flag)                                                       \
                       ios_stat_add_to_list (&conf->thru_list[type],           \
                                               throughput, iosstat);           \
        } while (0)
#else
#define BUMP_THROUGHPUT(iosstat, type)                                         \
        do {                                                                   \
                struct ios_conf         *conf = NULL;                          \
                double                   elapsed;                              \
                struct timeval          *begin, *end;                          \
                double                   throughput;                           \
                int                      flag = 0;                             \
                                                                               \
                begin = &frame->begin;                                         \
                end   = &frame->end;                                           \
                                                                               \
                elapsed = (end->tv_sec - begin->tv_sec) * 1e6                  \
                        + (end->tv_usec - begin->tv_usec);                     \
                throughput = op_ret / elapsed;                                 \
                                                                               \
                conf = this->private;                                          \
                LOCK (&iosstat->lock);                                         \
                {                                                              \
                        if (iosstat->thru_counters[type].throughput            \
                                <= throughput) {                               \
                                iosstat->thru_counters[type].throughput =      \
                                                                throughput;    \
                                gettimeofday (&iosstat->                       \
                                             thru_counters[type].time, NULL);  \
                               flag = 1;                                       \
                        }                                                      \
                }                                                              \
                UNLOCK (&iosstat->lock);                                       \
               if (flag)                                                       \
                       ios_stat_add_to_list (&conf->thru_list[type],           \
                                               throughput, iosstat);           \
        } while (0)
#endif


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
ios_create_sample_buf (size_t buf_size)
{
        ios_sample_buf_t *ios_sample_buf = NULL;
        ios_sample_t     *ios_samples = NULL;

        ios_sample_buf = GF_CALLOC (1,
                sizeof (*ios_sample_buf),
                gf_io_stats_mt_ios_sample_buf);
        if (!ios_sample_buf)
                goto err;

        ios_samples = GF_CALLOC (buf_size,
                sizeof (*ios_samples),
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
        GF_FREE (ios_sample_buf);
        return NULL;
}

void
ios_destroy_sample_buf (ios_sample_buf_t *ios_sample_buf)
{
        GF_FREE (ios_sample_buf->ios_samples);
        GF_FREE (ios_sample_buf);
}

static int
ios_init_sample_buf (struct ios_conf *conf)
{
        int32_t        ret = -1;

        GF_ASSERT (conf);
        LOCK (&conf->lock);
        conf->ios_sample_buf = ios_create_sample_buf (
                conf->ios_sample_buf_size);
        if (!conf->ios_sample_buf)
                goto out;
        ret = 0;
out:
        UNLOCK (&conf->lock);
        return ret;
}

int
ios_fd_ctx_get (fd_t *fd, xlator_t *this, struct ios_fd **iosfd)
{
        uint64_t      iosfd64 = 0;
        unsigned long iosfdlong = 0;
        int           ret = 0;

        ret = fd_ctx_get (fd, this, &iosfd64);
        iosfdlong = iosfd64;
        if (ret != -1)
                *iosfd = (void *) iosfdlong;

        return ret;
}



int
ios_fd_ctx_set (fd_t *fd, xlator_t *this, struct ios_fd *iosfd)
{
        uint64_t   iosfd64 = 0;
        int        ret = 0;

        iosfd64 = (unsigned long) iosfd;
        ret = fd_ctx_set (fd, this, iosfd64);

        return ret;
}

int
ios_stat_ref (struct ios_stat *iosstat)
{
        LOCK (&iosstat->lock);
        {
                iosstat->refcnt++;
        }
        UNLOCK (&iosstat->lock);

        return iosstat->refcnt;
}

int
ios_stat_unref (struct ios_stat *iosstat)
{
        int cleanup = 0;
        LOCK (&iosstat->lock);
        {
                iosstat->refcnt--;
                if (iosstat->refcnt == 0) {
                        if (iosstat->filename) {
                                GF_FREE (iosstat->filename);
                                iosstat->filename = NULL;
                        }
                        cleanup = 1;
                }
        }
        UNLOCK (&iosstat->lock);

        if (cleanup) {
                LOCK_DESTROY (&iosstat->lock);
                GF_FREE (iosstat);
                iosstat = NULL;
        }

        return 0;
}

int
ios_inode_ctx_set (inode_t *inode, xlator_t *this, struct ios_stat *iosstat)
{
        uint64_t   iosstat64 = 0;
        int        ret     = 0;

        ios_stat_ref (iosstat);
        iosstat64 = (unsigned long )iosstat;
        ret = inode_ctx_put (inode, this, iosstat64);
        return ret;
}

int
ios_inode_ctx_get (inode_t *inode, xlator_t *this, struct ios_stat **iosstat)
{
        uint64_t      iosstat64 = 0;
        unsigned long iosstatlong = 0;
        int           ret = 0;

        ret = inode_ctx_get (inode, this, &iosstat64);
        iosstatlong = iosstat64;
        if (ret != -1)
                *iosstat = (void *) iosstatlong;

        return ret;

}

int
ios_stat_add_to_list (struct ios_stat_head *list_head, uint64_t value,
                            struct ios_stat *iosstat)
{
        struct ios_stat_list *new = NULL;
        struct ios_stat_list *entry = NULL;
        struct ios_stat_list *t = NULL;
        struct ios_stat_list *list_entry = NULL;
        struct ios_stat_list *tmp = NULL;
        struct ios_stat_list *last = NULL;
        struct ios_stat      *stat = NULL;
        int                   cnt = 0;
        int                   found = 0;
        int                   reposition = 0;
        double                min_count = 0;

        LOCK (&list_head->lock);
        {

                if (list_head->min_cnt == 0)
                        list_head->min_cnt = value;
                if ((list_head->members == MAX_LIST_MEMBERS) &&
                     (list_head->min_cnt > value))
                        goto out;

                list_for_each_entry_safe (entry, t,
                              &list_head->iosstats->list, list) {
                        cnt++;
                        if (cnt == list_head->members)
                                last = entry;

                        if (!gf_uuid_compare (iosstat->gfid,
                            entry->iosstat->gfid)) {
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
                        list_del (&list_entry->list);
                        list_add_tail (&list_entry->list, &tmp->list);
                        if (min_count)
                                list_head->min_cnt = min_count;
                        goto out;
                } else if (list_head->members == MAX_LIST_MEMBERS && reposition) {
                        new = GF_CALLOC (1, sizeof (*new),
                                        gf_io_stats_mt_ios_stat_list);
                        new->iosstat = iosstat;
                        new->value = value;
                        ios_stat_ref (iosstat);
                        list_add_tail (&new->list, &tmp->list);
                        if (last) {
                                stat = last->iosstat;
                                last->iosstat = NULL;
                                ios_stat_unref (stat);
                                list_del (&last->list);
                                GF_FREE (last);
                        }
                        if (reposition == MAX_LIST_MEMBERS)
                                list_head->min_cnt = value;
                        else if (min_count) {
                                list_head->min_cnt = min_count;
                        }
                } else if (list_head->members < MAX_LIST_MEMBERS) {
                        new  = GF_CALLOC (1, sizeof (*new),
                                          gf_io_stats_mt_ios_stat_list);
                        new->iosstat = iosstat;
                        new->value = value;
                        ios_stat_ref (iosstat);
                        if (reposition) {
                                list_add_tail (&new->list, &tmp->list);
                        } else {
                                list_add_tail (&new->list, &entry->list);
                        }
                        list_head->members++;
                        if (list_head->min_cnt > value)
                                list_head->min_cnt = value;
                }
        }
out:
        UNLOCK (&list_head->lock);
        return 0;
}

static int
ios_stats_cleanup (xlator_t *this, inode_t *inode)
{

        struct ios_stat *iosstat = NULL;
        uint64_t         iosstat64 = 0;

        inode_ctx_del (inode, this, &iosstat64);
        if (!iosstat64) {
                gf_log (this->name, GF_LOG_WARNING,
                        "could not get inode ctx");
                return 0;
        }
        iosstat = (void *) (long)iosstat64;
        if (iosstat) {
                ios_stat_unref (iosstat);
        }
        return 0;
}

#define ios_log(this, logfp, fmt ...)                           \
        do {                                                    \
                if (logfp) {                                    \
                        fprintf (logfp, fmt);                   \
                        fprintf (logfp, "\n");                  \
                }                                               \
                gf_log (this->name, GF_LOG_TRACE, fmt);         \
        } while (0)


void
io_stats_log_px_stat (xlator_t *this, ios_sample_buf_t *sample_buf,
                      FILE* logfp, float pval, const char *desc,
                      int *num_fop, double **ios_latencies,
                      double *global_ios_latency)
{
        int    pn_idx = 0;
        double pn_val = 0;
        int    collected = 0;
        int    i = 0;
        int    px = 0;
        struct ios_conf *conf = NULL;

        collected = sample_buf->collected;

        px = (int)(pval * 100);
        pn_idx = (int)(pval * collected);
        pn_val = global_ios_latency[pn_idx];

        ios_log (this, logfp, "\"%s.%s.p%d_latency_usec\":\"%0.4lf\",", desc, "global", px, pn_val);

        for (i = 0; i < GF_FOP_MAXVALUE; i++) {
                qsort (ios_latencies[i], num_fop[i], sizeof (double),
                       gf_compare_double);
                pn_idx = (int)(pval*num_fop[i]);
                pn_val = ios_latencies[i][pn_idx];
                ios_log (this, logfp, "\"%s.%s.p%d_latency_usec\":\"%0.4lf\",", desc, gf_fop_list[i], px, pn_val);
        }
}

double
io_stats_prepare_latency_samples (ios_sample_buf_t *sample_buf, int *num_fop,
                                  double **ios_latencies,
                                  double *global_ios_latency)
{
        int             ret = 0;
        int             collected = 0;
        int             fop_idx = 0;
        int             i = 0;
        ios_sample_t   *ios_samples = NULL;
        glusterfs_fop_t fop_type = 0;

        collected = sample_buf->collected;
        ios_samples = sample_buf->ios_samples;

        for (i = 0; i < collected; i++) {
                global_ios_latency[i] = ios_samples[i].elapsed;
                fop_type = ios_samples[i].fop_type;
                fop_idx = num_fop[fop_type];
                ios_latencies[fop_type][fop_idx] = ios_samples[i].elapsed;
                num_fop[fop_type]++;
        }

        qsort (global_ios_latency, collected, sizeof (double),
               gf_compare_double);

        return ret;
}

double
io_stats_dump_pn_latencies (xlator_t *this, ios_sample_buf_t *sample_buf,
                            FILE* logfp, char * key_prefix,
                            char * str_prefix)
{
        double *ios_latencies[GF_FOP_MAXVALUE] = {NULL, };
        double *global_ios_latency = NULL;
        int     num_fop[GF_FOP_MAXVALUE] = {0, };
        int     ret = 0;
        int     i = 0;
        int     j = 0;
        int     collected = 0;
        char   *prefix = NULL;

        collected = sample_buf->collected;

        /* allocate memory */
        global_ios_latency = GF_CALLOC (collected, sizeof (double), 0);
        if (!global_ios_latency) {
                ret = -ENOMEM;
                goto out;
        }

        for (i = 0; i < GF_FOP_MAXVALUE; i++) {
                ios_latencies[i] = GF_CALLOC (collected,
                                              sizeof (double),
                                              0);
                if (!ios_latencies[i]) {
                        ret = -ENOMEM;
                        goto out;
                }
        }

        /* gather data */
        ret = io_stats_prepare_latency_samples (sample_buf,
                                                num_fop,
                                                ios_latencies,
                                                global_ios_latency);
        if (ret) {
                goto out;
        }

        /* build prefix and log data */
        ret = gf_asprintf (&prefix, "%s.%s", key_prefix, str_prefix);
        if (ret <= 0) {
                ret = -ENOMEM;
                goto out;
        }

        io_stats_log_px_stat (this, sample_buf, logfp, 0.50, prefix,
                              num_fop, ios_latencies, global_ios_latency);
        io_stats_log_px_stat (this, sample_buf, logfp, 0.90, prefix,
                              num_fop, ios_latencies, global_ios_latency);
        io_stats_log_px_stat (this, sample_buf, logfp, 0.95, prefix,
                              num_fop, ios_latencies, global_ios_latency);
        io_stats_log_px_stat (this, sample_buf, logfp, 0.99, prefix,
                              num_fop, ios_latencies, global_ios_latency);

out:

        GF_FREE (prefix);

        GF_FREE (global_ios_latency);
        for (j = 0; j < i; j++) {
                GF_FREE (ios_latencies[j]);
        }

        return ret;
}

int
ios_dump_file_stats (struct ios_stat_head *list_head, xlator_t *this,
                     FILE *logfp)
{
        struct ios_stat_list *entry = NULL;

        LOCK (&list_head->lock);
        {
                list_for_each_entry (entry, &list_head->iosstats->list, list) {
                        ios_log (this, logfp, "%-12.0f %s",
                                entry->value, entry->iosstat->filename);
                }
        }
        UNLOCK (&list_head->lock);
        return 0;
}

int
ios_dump_throughput_stats (struct ios_stat_head *list_head, xlator_t *this,
                           FILE *logfp, ios_stats_thru_t type)
{
        struct ios_stat_list *entry = NULL;
        struct timeval        time  = {0, };
        char                  timestr[256] = {0, };

        LOCK (&list_head->lock);
        {
                list_for_each_entry (entry, &list_head->iosstats->list, list) {
                        gf_time_fmt (timestr, sizeof timestr,
                                     entry->iosstat->thru_counters[type].time.tv_sec,
                                     gf_timefmt_FT);
                        snprintf (timestr + strlen (timestr), sizeof timestr - strlen (timestr),
                          ".%"GF_PRI_SUSECONDS, time.tv_usec);

                        ios_log (this, logfp, "%s \t %-10.2f  \t  %s",
                                 timestr, entry->value, entry->iosstat->filename);
                }
        }
        UNLOCK (&list_head->lock);
        return 0;
}

int
_io_stats_get_key_prefix (xlator_t *this, char **key_prefix) {
        struct ios_conf       *conf = NULL;
        char                  *key_root = "storage.gluster";
        char                  *xlator_name = NULL;
        char                  *instance_name = NULL;
        size_t                key_len = 0;
        int                   bytes_written = 0;
        int                   i = 0;
        int                   ret = 0;

        conf = this->private;
        xlator_name = strdupa (get_top_xlator_name (this));

        for (i = 0; i < strlen (xlator_name); i++) {
                if (xlator_name[i] == '/')
                        xlator_name[i] = '_';
        }

        instance_name = this->instance_name;
        if (conf->iamshd) {
                xlator_name = "shd";
        } else if (conf->iamnfsd) {
                instance_name = xlator_name;
                xlator_name = "nfsd";
        } else if (conf->iamgfproxyd) {
                instance_name = xlator_name;
                xlator_name = "gfproxyd";
        } else if (conf->iambrickd) {
                key_root = "storage.gluster.brick";
        }

        if (instance_name) {
                /* +3 for 2 x "." + NULL */
                key_len = strlen (key_root) + strlen (xlator_name) +
                        strlen (instance_name) + 3;
                *key_prefix = GF_CALLOC (key_len, sizeof (char),
                        gf_common_mt_char);
                if (!key_prefix) {
                        ret = -ENOMEM;
                        goto err;
                }
                bytes_written = snprintf (*key_prefix, key_len, "%s.%s.%s",
                        key_root, xlator_name, instance_name);
                if (bytes_written != key_len - 1) {
                        ret = -EINVAL;
                        goto err;
                }
        } else {
                /* +2 for 1 x "." + NULL */
                key_len = strlen (key_root) + strlen (xlator_name) + 2;
                *key_prefix = GF_CALLOC (key_len, sizeof (char),
                        gf_common_mt_char);
                if (!key_prefix) {
                        ret = -ENOMEM;
                        goto err;
                }
                bytes_written = snprintf (*key_prefix, key_len, "%s.%s",
                        key_root, xlator_name);
                if (bytes_written != key_len - 1) {
                        ret = -EINVAL;
                        goto err;
                }
        }
        return 0;
err:
        GF_FREE (*key_prefix);
        *key_prefix = NULL;
        return ret;
}

int
io_stats_dump_global_to_json_logfp (xlator_t *this,
    struct ios_global_stats *stats, ios_sample_buf_t *sample_buf,
    struct timeval *now, int interval, FILE* logfp)
{
        int                   i = 0;
        int                   j = 0;
        int32_t               inodelk_count = -1;
        int32_t               entrylk_count = -1;
        dict_t                *xattr = NULL;
        struct ios_conf       *conf = NULL;
        char                  *key_prefix = NULL;
        char                  *str_prefix = NULL;
        char                  *lc_fop_name = NULL;
        int                   ret = 1;  /* Default to error */
        int                   rw_size;
        char                  *rw_unit = NULL;
        const char            *errno_name = NULL;
        long                  fop_hits;
        long                  error_count;
        float                 fop_lat_ave;
        float                 fop_lat_min;
        float                 fop_lat_max;
        double                interval_sec;
        double                fop_ave_usec = 0.0;
        double                fop_ave_usec_sum = 0.0;
        double                weighted_fop_ave_usec = 0.0;
        double                weighted_fop_ave_usec_sum = 0.0;
        long                  total_fop_hits = 0;
        loc_t                 unused_loc = {0, };

        interval_sec = ((now->tv_sec * 1000000.0 + now->tv_usec) -
                (stats->started_at.tv_sec * 1000000.0 +
                 stats->started_at.tv_usec)) / 1000000.0;

        conf = this->private;

        ret = _io_stats_get_key_prefix (this, &key_prefix);
        if (ret) {
                goto out;
        }

        if (interval == -1) {
                str_prefix = "aggr";

        } else {
                str_prefix = "inter";
        }
        ios_log (this, logfp, "{");
        ios_log (this, logfp, "\"%s.%s.outstanding_req\": \"%u\",",
                 key_prefix, str_prefix, conf->outstanding_req);

        if (conf->iambrickd) {
                ret =  syncop_getxattr (FIRST_CHILD (this), &unused_loc,
                                        &xattr, GLUSTERFS_INODELK_COUNT,
                                        NULL, NULL);
                if (ret == 0 && xattr) {
                        if (dict_get_int32 (xattr, GLUSTERFS_INODELK_COUNT,
                                            &inodelk_count) != 0) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "Error fetching %s",
                                        GLUSTERFS_INODELK_COUNT);
                        }
                } else {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Unable to get inode lock count counts "
                                "from the posix-locks translator!");
                }

                ios_log (this, logfp, "\"%s.%s.inodelk_count\": \"%u\",",
                         key_prefix, str_prefix, inodelk_count);

                ret = syncop_getxattr (FIRST_CHILD (this), &unused_loc,
                                       &xattr, GLUSTERFS_ENTRYLK_COUNT,
                                       NULL, NULL);
                if (ret == 0 && xattr) {
                        if (dict_get_int32 (xattr, GLUSTERFS_ENTRYLK_COUNT,
                                            &entrylk_count) != 0) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "Error fetching %s",
                                        GLUSTERFS_ENTRYLK_COUNT);
                        }
                        dict_unref (xattr);
                } else {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Unable to get entry lock count counts "
                                "from the posix-locks translator!");
                }

                ios_log (this, logfp, "\"%s.%s.entrylk_count\": \"%u\",",
                         key_prefix, str_prefix, entrylk_count);
        }

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
                        ios_log (this, logfp,
                                "\"%s.%s.read_%d%s\": \"%"PRId64"\",",
                                key_prefix, str_prefix, rw_size, rw_unit,
                                stats->block_count_read[i]);
                        ios_log (this, logfp,
                                "\"%s.%s.write_%d%s\": \"%"PRId64"\",",
                                key_prefix, str_prefix, rw_size, rw_unit,
                                stats->block_count_write[i]);
                } else {
                        ios_log (this, logfp,
                                "\"%s.%s.read_%d%s_per_sec\": \"%0.2lf\",",
                                key_prefix, str_prefix, rw_size, rw_unit,
                                (double)(stats->block_count_read[i] /
                                        interval_sec));
                        ios_log (this, logfp,
                                "\"%s.%s.write_%d%s_per_sec\": \"%0.2lf\",",
                                key_prefix, str_prefix, rw_size, rw_unit,
                                (double)(stats->block_count_write[i] /
                                        interval_sec));
                }
        }

        if (interval == -1) {
                ios_log (this, logfp, "\"%s.%s.fds.open_count\": \"%"PRId64
                        "\",", key_prefix, str_prefix,
                        conf->cumulative.nr_opens);
                ios_log (this, logfp,
                        "\"%s.%s.fds.max_open_count\": \"%"PRId64"\",",
                        key_prefix, str_prefix, conf->cumulative.max_nr_opens);
        }

        for (i = 0; i < IOS_MAX_ERRORS; i++) {
                errno_name = errno_to_name[i];
                error_count = stats->errno_count[i];
                if (interval == -1) {
                        ios_log (this, logfp,
                                "\"%s.%s.errors.%s.count\": \"%"PRId64"\",",
                                key_prefix, str_prefix, errno_name,
                                error_count);
                } else {
                        ios_log (this, logfp,
                                "\"%s.%s.errors.%s.per_sec\": \"%0.2lf\",",
                                key_prefix, str_prefix, errno_name,
                                (double)(error_count/interval_sec));
                }
        }

        for (i = 0; i < GF_FOP_MAXVALUE; i++) {
                lc_fop_name = strdupa (gf_fop_list[i]);
                for (j = 0; lc_fop_name[j]; j++) {
                        lc_fop_name[j] = tolower (lc_fop_name[j]);
                }

                fop_hits = 0;
                fop_lat_ave = 0.0;
                fop_lat_min = 0.0;
                fop_lat_max = 0.0;
                if (stats->fop_hits[i]) {
                        fop_hits = stats->fop_hits[i];
                        if (stats->latency[i].avg) {
                                fop_lat_ave = stats->latency[i].avg;
                                fop_lat_min = stats->latency[i].min;
                                fop_lat_max = stats->latency[i].max;
                        }
                }
                ios_log (this, logfp,
                        "\"%s.%s.fop.%s.count\": \"%"PRId64"\",",
                        key_prefix, str_prefix, lc_fop_name,
                        fop_hits);
                if (interval != -1) {
                        ios_log (this, logfp,
                                "\"%s.%s.fop.%s.per_sec\": \"%0.2lf\",",
                                key_prefix, str_prefix, lc_fop_name,
                                (double)(fop_hits / interval_sec));
                }

                ios_log (this, logfp,
                        "\"%s.%s.fop.%s.latency_ave_usec\": \"%0.2lf\",",
                         key_prefix, str_prefix, lc_fop_name, fop_lat_ave);
                ios_log (this, logfp,
                        "\"%s.%s.fop.%s.latency_min_usec\": \"%0.2lf\",",
                         key_prefix, str_prefix, lc_fop_name, fop_lat_min);
                ios_log (this, logfp,
                        "\"%s.%s.fop.%s.latency_max_usec\": \"%0.2lf\",",
                        key_prefix, str_prefix, lc_fop_name, fop_lat_max);

                fop_ave_usec_sum += fop_lat_ave;
                weighted_fop_ave_usec_sum += fop_hits * fop_lat_ave;
                total_fop_hits += fop_hits;
        }

        if (total_fop_hits) {
                weighted_fop_ave_usec = weighted_fop_ave_usec_sum/total_fop_hits;
                /* Extra key that does not print out an entry w/ 0.00 for
                 * intervals with no data
                 */
                ios_log (this, logfp,
                        "\"%s.%s.fop.weighted_latency_ave_usec_nozerofill\": \"%0.4lf\",",
                        key_prefix, str_prefix, weighted_fop_ave_usec);
        }

        ios_log (this, logfp,
                "\"%s.%s.fop.weighted_latency_ave_usec\": \"%0.4lf\",",
                key_prefix, str_prefix, weighted_fop_ave_usec);
        ios_log (this, logfp,
                "\"%s.%s.fop.weighted_fop_count\": \"%ld\",",
                key_prefix, str_prefix, total_fop_hits);

        fop_ave_usec = fop_ave_usec_sum/GF_FOP_MAXVALUE;
        ios_log (this, logfp,
                "\"%s.%s.fop.unweighted_latency_ave_usec\":\"%0.4lf\",",
                key_prefix, str_prefix, fop_ave_usec);

        ios_log (this, logfp,
                "\"%s.%s.fop.GOT1\":\"%0.4lf\",",
                key_prefix, str_prefix, fop_ave_usec);
        if (conf->dump_percentile_latencies) {
                io_stats_dump_pn_latencies (this, sample_buf, logfp, key_prefix,
                                             str_prefix);
                ios_log (this, logfp,
                        "\"%s.%s.fop.GOT2\":\"%0.4lf\",",
                        key_prefix, str_prefix, fop_ave_usec);
        }
        ios_log (this, logfp,
                "\"%s.%s.fop.GOT3\":\"%0.4lf\",",
                key_prefix, str_prefix, fop_ave_usec);

        if (conf->iamnfsd || conf->iambrickd) {
                dict_t *xattr = NULL;
                ret = syncop_getxattr (FIRST_CHILD (this), &unused_loc, &xattr,
                                       IO_THREADS_QUEUE_SIZE_KEY, NULL, NULL);
                if (xattr) {
                        /*
                         * Iterate over the dictionary returned to us by
                         * io-threads and dump the results to the stats file.
                         */
                        data_pair_t *curr = NULL;
                        dict_foreach_inline (xattr, curr) {
                                ios_log (this, logfp,
                                         "\"%s.%s.%s.queue_size\": \"%d\",",
                                         key_prefix, str_prefix, curr->key,
                                         data_to_int32 (curr->value));
                        }

                        /* Free the dictionary! */
                        dict_unref (xattr);
                } else {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Unable to get queue size counts "
                                "from the io-threads translator!");
                }
        }

        if (interval == -1) {
                ios_log (this, logfp, "\"%s.%s.uptime\": \"%"PRId64"\",",
                         key_prefix, str_prefix,
                         (uint64_t) (now->tv_sec - stats->started_at.tv_sec));
                ios_log (this, logfp, "\"%s.%s.bytes_read\": \"%"PRId64"\",",
                         key_prefix, str_prefix, stats->data_read);
                ios_log (this, logfp, "\"%s.%s.bytes_written\": \"%"PRId64"\"",
                         key_prefix, str_prefix, stats->data_written);
        } else {
                ios_log (this, logfp,
                         "\"%s.%s.sample_interval_sec\": \"%0.2lf\",",
                         key_prefix, str_prefix,
                         interval_sec);
                ios_log (this, logfp,
                         "\"%s.%s.bytes_read_per_sec\": \"%0.2lf\",",
                         key_prefix, str_prefix,
                         (double)(stats->data_read / interval_sec));
                ios_log (this, logfp,
                         "\"%s.%s.bytes_written_per_sec\": \"%0.2lf\"",
                         key_prefix, str_prefix,
                         (double)(stats->data_written / interval_sec));
        }

#if OFFSET_WRITE_DETECTOR
        if (conf->iamnfsd) {
                ios_log (this, logfp,
                         ",\"%s.%s.offset_writes\": \"%"PRId64"\",",
                         key_prefix, str_prefix, stats->offset_write_ops);
                ios_log (this, logfp,
                         "\"%s.%s.total_writes\": \"%"PRId64"\"",
                         key_prefix, str_prefix, stats->total_write_ops);
        }
#endif

        ios_log (this, logfp, "}");

        ret = 0;
out:
        GF_FREE (key_prefix);
        return ret;
}

char *
_resolve_username (xlator_t *this, uid_t uid)
{
        struct passwd pwd;
        struct passwd *pwd_result = NULL;
        size_t pwd_buf_len;
        char   *pwd_buf = NULL;
        char   *ret = NULL;

        // Prepare our buffer for the uid->username translation
#ifdef _SC_GETGR_R_SIZE_MAX
        pwd_buf_len = sysconf (_SC_GETGR_R_SIZE_MAX);
#else
        pwd_buf_len = -1;
#endif
        if (pwd_buf_len == -1) {
                pwd_buf_len = DEFAULT_PWD_BUF_SZ;  // per the man page
        }

        pwd_buf = alloca (pwd_buf_len);
        if (!pwd_buf)
                goto err;

        getpwuid_r (uid, &pwd, pwd_buf, pwd_buf_len,
                   &pwd_result);
        if (!pwd_result)
                goto err;

        ret = gf_strdup (pwd.pw_name);
        if (ret)
                return ret;
        else
                gf_log (this->name, GF_LOG_ERROR,
                        "gf_strdup failed, failing username "
                        "resolution.");
err:
        return ret;
}

char *
_resolve_group_name (xlator_t *this, gid_t gid)
{
        struct group grp;
        struct group *grp_result = NULL;
        size_t grp_buf_len;
        char   *grp_buf = NULL;
        char   *ret = NULL;

        // Prepare our buffer for the gid->group name translation
#ifdef _SC_GETGR_R_SIZE_MAX
        grp_buf_len = sysconf (_SC_GETGR_R_SIZE_MAX);
#else
        grp_buf_len = -1;
#endif
        if (grp_buf_len == -1) {
                grp_buf_len = DEFAULT_GRP_BUF_SZ;  // per the man page
        }

        grp_buf = alloca (grp_buf_len);
        if (!grp_buf) {
                goto err;
        }

        getgrgid_r (gid, &grp, grp_buf, grp_buf_len,
                   &grp_result);
        if (!grp_result)
                goto err;

        ret = gf_strdup (grp.gr_name);
        if (ret)
                return ret;
        else
                gf_log (this->name, GF_LOG_ERROR,
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
_io_stats_write_latency_sample (xlator_t *this, ios_sample_t *sample,
                                FILE* logfp)
{
        double epoch_time = 0.00;
        char   *xlator_name = NULL;
        char   *instance_name = NULL;
        char   *hostname = NULL;
        char   *identifier = NULL;
        char   *port = NULL;
        char   *port_pos = NULL;
        char   *group_name = NULL;
        char   *username = NULL;
        struct ios_conf *conf = NULL;
        const char *pri_str = NULL;
        const char *ns_name = NULL;
        const char *path = NULL;
        const char *name = NULL;
        loc_t  *loc = NULL;
        const char *error_string = NULL;
        int32_t op_errno = 0;

        conf = this->private;

        epoch_time = (sample->timestamp).tv_sec +
          ((sample->timestamp).tv_usec / 1000000.0);

        if (!sample->identifier || (strlen (sample->identifier) == 0)) {
                hostname = "Unknown";
                port = "Unknown";
        } else {
                identifier = strdupa (sample->identifier);
                port_pos = strrchr (identifier, ':');
                if (!port_pos || strlen(port_pos) < 2)
                        goto err;
                port = strdupa (port_pos + 1);
                if (!port)
                        goto err;
                *port_pos = '\0';
                hostname = gf_rev_dns_lookup (identifier);
                if (!hostname)
                        hostname ="Unknown";
        }

        xlator_name = strdupa (get_top_xlator_name (this));
        instance_name = this->instance_name;
        if (!instance_name || strlen (instance_name) == 0)
                instance_name = "N/A";

        /* Resolve the UID to a string username */
        username = _resolve_username (this, sample->uid);
        if (!username) {
                username = GF_MALLOC (30, gf_common_mt_char);
                sprintf (username, "%d", (int32_t)sample->uid);
        }

        /* Resolve the GID to a string group name */
        group_name = _resolve_group_name (this, sample->gid);
        if (!group_name) {
                group_name = GF_MALLOC (30, gf_common_mt_char);
                sprintf (group_name, "%d", (int32_t)sample->gid);
        }

        /*
         * If the priority was unspecified, print out actual priority of the FOP,
         * otherwise print the priority given on the frame.
         */
        if (sample->fop_pri != GF_FOP_PRI_UNSPEC) {
                pri_str = fop_pri_to_string (sample->fop_pri);
        } else {
                pri_str = fop_enum_to_pri_string (sample->fop_type);
        }

        ns_name = get_ns_from_hash (conf, &sample->ns_info);
        if (!ns_name) {
                ns_name = "Unknown";
        } else if (strcmp (ns_name, "/") == 0) {
                /* Give the root a readable name in the samples. */
                ns_name = "Root";
        }

        path = "Unknown";
        if (sample->have_path)
                path = sample->path;

        error_string = "No Error";
        if (sample->op_ret != 0) {
                op_errno = abs (sample->op_errno);
                error_string = strerror (op_errno);
        }

        ios_log (this, logfp,
                 "%0.6lf,%s,%s,%0.4lf,%s,%s,%s,%s,%s,%s,%s,%s,%d,%s",
                 epoch_time, pri_str,
                 fop_enum_to_string (sample->fop_type),
                 sample->elapsed, xlator_name, instance_name, username,
                 group_name, hostname, port, ns_name, path, op_errno, error_string);
        goto out;
err:
        gf_log (this->name, GF_LOG_ERROR,
                "Error parsing socket identifier");
out:
        GF_FREE (group_name);
        GF_FREE (username);
}

/**
 * get_ns_from_hash
 *
 * Given a hash (produced by the namespace hashing xlator), retrieve a string
 * that can be used as a key for a few other dictionaries to retrieve crucial
 * info for NS throttling.
 *
 * @conf: Translator configuration struct.
 * @info: Namespace hash information.
 */
const char *
get_ns_from_hash (struct ios_conf *conf, const ns_info_t *info)
{
        char  ns_key[DICT_UINT32_KEY_SIZE];
        char *ns_name;
        int   ret;

        /* Handle the special case hashes. */
        if (info->found == _gf_false) {
                gf_log (GF_IO_STATS, GF_LOG_DEBUG, "NS > H: NO HASH.");
                return NULL;
        }

        dict_uint32_to_key (info->hash, ns_key);
        ret = dict_get_str (conf->hash_to_ns, ns_key, &ns_name);

        if (ret == 0) {
                gf_log (GF_IO_STATS, GF_LOG_DEBUG,
                        "NS > H: %u -> %s.",
                        info->hash, ns_name);
                return ns_name;
        } else {
                gf_log (GF_IO_STATS, GF_LOG_DEBUG,
                        "NS > H: HASH %u NOT FOUND.",
                        info->hash);
                return NULL;
        }
}

/**
 * get_fop_hits_for_ns
 *
 * Given a namespace, returns the ios_fop_hits_t structure tracking
 * fop averages for this namespace.
 *
 * @this: Pointer to the io-stats xlator.
 * @ns_name: Namespace we want to find the fop_hits structure for.
 */
ios_fop_hits_t *
get_fop_hits_for_ns (const xlator_t *this, const char *ns_name)
{
        struct ios_conf *conf = this->private;
        data_t          *fop_hits_dict_entry_data;
        ios_fop_hits_t  *fop_hits;

        fop_hits_dict_entry_data = dict_get (conf->fop_hits_dict, (char *)ns_name);

        if (fop_hits_dict_entry_data) {
                fop_hits = (ios_fop_hits_t *)fop_hits_dict_entry_data->data;

                if (fop_hits) {
                        return fop_hits;
                }
        }
        return NULL;
}

/**
 * get_fop_rate_for_ns
 *
 * Returns the currently computed fop rate for the given namespace.
 *
 * @this: Pointer to the io-stats xlator.
 * @ns_name: Namespace we want to find the fop rate for.
 */
double
get_fop_rate_for_ns (const xlator_t *this, const char *ns_name)
{
        ios_fop_hits_t *fop_hits = get_fop_hits_for_ns (this, ns_name);
        return fop_hits ? fop_hits->avg_fops : -1;
}

int64_t get_allowed_fop_rate_for_ns (struct ios_conf *conf, const char *ns_name)
{
        int64_t rate;

        if (dict_get_int64 (conf->throttling_rates,
                            (char *)ns_name, &rate) == 0) {
                return rate;
        } else {
                return -1;
        }
}

gf_boolean_t
should_throttle_fop (call_frame_t *frame, xlator_t *this)
{
        struct ios_conf *conf = this->private;
        const char      *ns_name = NULL;
        double          fop_rate = 0;
        int64_t         allowed_fop_rate = 0;
        int             ret = 0;

        /*
         * If throttling is not enabled, of course, do not throttle.
         */
        if (!conf->throttling_enabled) {
                goto no_throttle;
        }

        /*
         * If no throttling rates are defined, default to no throttling.
         */
        if (!conf->throttling_rates) {
                goto no_throttle;
        }

        /*
         * Attempt to find the namespace directory name from the id, and if
         * cannot find it, default to no throttling.
         */
        ns_name = get_ns_from_hash (conf, &frame->root->ns_info);
        if (!ns_name) {
                goto no_throttle;
        }

        /*
         * Attempt to find the current fop rate for the directory.
         * If for some reason we cannot find it, default to no throttling.
         */
        fop_rate = get_fop_rate_for_ns (this, ns_name);

        if (fop_rate < 0) {
                goto no_throttle;
        }

        allowed_fop_rate = get_allowed_fop_rate_for_ns (conf, ns_name);

        /*
         * If we can't find an allowed fop rate for the namespace, or if it
         * is set to (-1), then default to no throttling.
         */
        if (allowed_fop_rate < 0) {
                goto no_throttle;
        }

        /*
         * Finally, if the share has exceeded the fop rate,
         * we should throttle it.
         */
        if (fop_rate <= allowed_fop_rate) {
                goto no_throttle;
        }

        /* If we are here after all of that, finally throttle the namespace. */
        gf_log (this->name, GF_LOG_DEBUG,
                "%f > %ld | Throttling FOP to namespace '%s'",
                fop_rate, allowed_fop_rate, ns_name);
        return _gf_true;

no_throttle:
        return _gf_false;
}

static inline ios_fop_hits_t *ios_fop_hits_create ()
{
        return GF_CALLOC (1, sizeof (ios_fop_hits_t), gf_io_stats_mt_ios_sample_buf);
}

int
io_stats_process_ios_sample (const xlator_t *this, const ios_sample_t *sample)
{
        struct ios_conf *conf     = this->private;
        ios_fop_hits_t  *fop_hits = NULL;
        const char      *ns_name  = NULL;
        int              ret      = 0;
        data_t          *fop_hits_dict_entry_data = NULL;

        if (!conf) {
                ret = -1;
                goto out;
        }

        /* Silently skip if ns_info isn't initialized yet. Sometimes samples
         * get dumped before the dict can be enabled, and while it's not bad,
         * it's quite noisy on the brick logs. */
        if (!conf->measure_namespace_rates || !conf->fop_hits_dict) {
                return 0;
        }

        ns_name = get_ns_from_hash (conf, &sample->ns_info);
        if (!ns_name) {
                /* It is still useful to collect statistics on "unknown" samples
                 * so we know how many FOPs are untagged by the namespace xlator. */
                ns_name = "unknown";
        }

        fop_hits_dict_entry_data = dict_get (conf->fop_hits_dict,
                                             (char *)ns_name);

        if (!fop_hits_dict_entry_data) {
                /*
                 *  If no FOP hits are detected for a namespace, lets create
                 *  one and place it in the dictionary
                 */
                fop_hits = ios_fop_hits_create ();
                fop_hits_dict_entry_data = bin_to_data (fop_hits,
                                                        sizeof (*fop_hits));
                ret = dict_set (conf->fop_hits_dict, (char *)ns_name,
                                fop_hits_dict_entry_data);
                if (ret != 0) {
                        goto out;
                }
        }

        /*
         * Let's now take the fop_hits and increment the appropriate counter.
         */
        fop_hits = (ios_fop_hits_t *)fop_hits_dict_entry_data->data;
        fop_hits->hits[fop_hits->idx]++;
        fop_hits->elapsed[fop_hits->idx] += sample->elapsed;

out:
        return ret;
}

int
io_stats_compute_sma_for_ns (struct ios_dump_args *args, const char *ns_name,
                                        ios_fop_hits_t *fop_hits, dict_t *dict, uint64_t idx)
{
        xlator_t        *this = args->this;
        struct ios_conf *conf = this->private;
        int              ret  = -1;
        int              hit_idx, i;
        int              num_samples = 0;
        double           avg_fops = 0;
        double           elapsed = 0;
        double           avg_latency = 0;
        char            *key_prefix = NULL;

        GF_VALIDATE_OR_GOTO (this->name, this, out);
        GF_VALIDATE_OR_GOTO (this->name, ns_name, out);
        GF_VALIDATE_OR_GOTO (this->name, fop_hits, out);

        ret = _io_stats_get_key_prefix (this, &key_prefix);
        if (ret) {
                goto out;
        }

        /* For each i'th window before our current sliding index (e.g. the
         * 0th window, 1st window, 2nd window preceding the current index),
         * we want to try to sum the counted hits */
        for (i = 0; i < conf->ns_rate_window; i++) {
                /* This gets the window at `idx - i` and wraps correctly around
                 * the FOP_HITS_SIZE-sized array. */
                hit_idx = (fop_hits->idx - i + FOP_HITS_SIZE) % FOP_HITS_SIZE;
                gf_log (this->name, GF_LOG_DEBUG, "%s.hits[%d] = %lu * %d",
                        ns_name, hit_idx, fop_hits->hits[hit_idx],
                        conf->ios_sample_interval);
                gf_log (this->name, GF_LOG_DEBUG, "%s.latency[%d] = %lf",
                        ns_name, hit_idx, fop_hits->elapsed[hit_idx]);
                /* Accumulate the number of samples that we've seen. */
                num_samples += fop_hits->hits[hit_idx];
                /* We add the total elapsed time for all samples in our window,
                 * which we will divide later by the total number of processed
                 * samples. */
                elapsed += fop_hits->elapsed[hit_idx];
        }

        /* The number of fops is given by the samples multiplied by the sample
         * interval, divided by the total length in time of the window. */
        avg_fops = (double)num_samples * conf->ios_sample_interval /
                       ((double)conf->ns_rate_window * conf->ios_dump_interval);
        gf_log (this->name, GF_LOG_DEBUG,
                "Average fop rate for %s = %lf / sec", ns_name, avg_fops);

        /* The average latency is given by the total elapsed time for all of
         * the samples divided by the total number of samples. If we have 0
         * samples, just return 0. */
        avg_latency = (num_samples > 0) ? (elapsed / (double)num_samples) : 0;
        gf_log (this->name, GF_LOG_DEBUG,
                "Average fop latency for %s = %lf usec / fop", ns_name, avg_latency);

        if (strcmp ("/", ns_name) == 0) {
                /* Give the root namespace a readable name. */
                ns_name = "root";
        }

        /* Include a ',' unless we're at the end of the dict. */
        ios_log (this, args->u.logfp,
                "\"%s.namespaces.%s.fop_rate\": \"%lf\",",
                key_prefix, ns_name, avg_fops);
        ios_log (this, args->u.logfp,
                "\"%s.namespaces.%s.avg_fop_latency\": \"%lf\"%s",
                key_prefix, ns_name, avg_latency, (idx == dict->count - 1) ? "" : ",");

        fop_hits->avg_fops = avg_fops;
        fop_hits->avg_latency = avg_latency;
        ret = 0;
out:
        if (key_prefix) {
                GF_FREE (key_prefix);
        }

        return ret;
}

int
__io_stats_compute_moving_average_foreach (dict_t *this, char *key,
                                                data_t *value, void *data, uint64_t idx)
{
        struct ios_dump_args *args = data;
        xlator_t *x_this = args->this;
        if (key && value) {
                gf_log (x_this->name, GF_LOG_TRACE, "Will compute averages for %s", key);
                ios_fop_hits_t *fop_hits = (ios_fop_hits_t *)value->data;
                io_stats_compute_sma_for_ns (args, key, fop_hits, this, idx);
        }
        return 0;
}

int
ios_compute_moving_average (struct ios_dump_args *args)
{
        xlator_t *this = args->this;
        struct ios_conf *conf = this->private;

        if (!conf->measure_namespace_rates) {
                return 0;
        }

        if (!conf->fop_hits_dict) {
                return 0;
        }

        if (conf->fop_hits_dict->count > 0) {
                ios_log (args->this, args->u.logfp, "{");
                gf_log (this->name, GF_LOG_TRACE, __func__);
                dict_foreach_with_idx (conf->fop_hits_dict, __io_stats_compute_moving_average_foreach, args);
                ios_log (args->this, args->u.logfp, "}");
        }

        return 0;
}


int
io_stats_compute_ema_for_dir (xlator_t *this, const char *dirname,
                                        ios_fop_hits_t *fop_hits)
{
        return 0;
}

int
__io_stats_bump_ctr_foreach (dict_t *this, char *key, data_t *value, void *data)
{
        xlator_t       *x_this   = (xlator_t *)data;
        ios_fop_hits_t *fop_hits = (ios_fop_hits_t *)value->data;

        /* Increment counter (and wrap), then reset hits for that index. */
        fop_hits->idx = (fop_hits->idx + 1) % FOP_HITS_SIZE;
        fop_hits->hits[fop_hits->idx] = 0;
        fop_hits->elapsed[fop_hits->idx] = 0;

        gf_log (x_this->name, GF_LOG_TRACE, "Moved counters for %s to %ld",
                key, fop_hits->idx);

        return 0;
}

void
io_stats_bump_ctrs (xlator_t *this)
{
        struct ios_conf *conf = this->private;

        if (!conf->measure_namespace_rates) {
                return;
        }

        gf_log (this->name, GF_LOG_TRACE, __func__);
        dict_foreach (conf->fop_hits_dict, __io_stats_bump_ctr_foreach,
                        this);
}

int
__io_stats_destroy_fop_hits_dict_foreach (dict_t *this, char *key,
                                                data_t *value, void *data)
{
        if (key && value) {
                gf_log (GF_IO_STATS, GF_LOG_DEBUG, "Will destroy values for %s", key);
                GF_FREE (value->data);
                value->data = NULL;
        }
        return 0;
}

static inline void
io_stats_destroy_fop_hits_dict (dict_t **fop_hits_dict)
{
        if (!fop_hits_dict) return;

        /**
         * Loop through each of the entries in the dict and destroy
         * allocated values.
         */
        dict_foreach (*fop_hits_dict, __io_stats_destroy_fop_hits_dict_foreach, NULL);
        dict_destroy (*fop_hits_dict);
        *fop_hits_dict = NULL;
}

void
io_stats_dump_foprate_stats (struct ios_dump_args *args)
{
        /* Snapshot a moving average for this interval */
        ios_compute_moving_average (args);

        /* Bump some counters */
        io_stats_bump_ctrs (args->this);
}

/*
 * Takes in our sample buffer and then dumps out the contents
 */
int
io_stats_dump_latency_samples_logfp (xlator_t *this, FILE* logfp,
                                     ios_sample_buf_t *sample_buf)
{
        uint64_t              i = 0;
        int                   ret = 1;  /* Default to error */

        /* Empty case, nothing to do, exit. */
        if (sample_buf == NULL || sample_buf->collected == 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "No samples, dump not required.");
                ret =0;
                goto out;
        }

        /* If we've collected more samples than the buffer can hold, then we
         * should dump the whole buffer, starting from the oldest entries which
         * begin at sample_buf->pos. */
        if (sample_buf->collected > sample_buf->size) {
                for (i = sample_buf->pos; i < sample_buf->size; i++) {
                        ios_sample_t *sample = &(sample_buf->ios_samples[i]);
                        io_stats_process_ios_sample (this, sample);
                        _io_stats_write_latency_sample (this, sample, logfp);
                }
        }

        /* Simple case: Dump from 0 to sample_buf->pos. These are the newest
         * entries if we have wrapped around our buffer, as well. */
        for (i = 0; i < sample_buf->pos; i++) {
                ios_sample_t *sample = &(sample_buf->ios_samples[i]);
                io_stats_process_ios_sample (this, sample);
                _io_stats_write_latency_sample (this, sample, logfp);
        }

out:
        return ret;
}


int
io_stats_dump_global_to_logfp (xlator_t *this, struct ios_global_stats *stats,
                               struct timeval *now, int interval, FILE *logfp)
{
        int                   i = 0;
        int                   per_line = 0;
        int                   index = 0;
        struct ios_stat_head *list_head = NULL;
        struct ios_conf      *conf = NULL;
        char                  timestr[256] = {0, };
        char                  str_header[128] = {0};
        char                  str_read[128] = {0};
        char                  str_write[128] = {0};

        conf = this->private;

        if (interval == -1)
                ios_log (this, logfp, "\n=== Cumulative stats ===");
        else
                ios_log (this, logfp, "\n=== Interval %d stats ===",
                         interval);
        ios_log (this, logfp, "      Duration : %"PRId64" secs",
                 (uint64_t) (now->tv_sec - stats->started_at.tv_sec));
        ios_log (this, logfp, "     BytesRead : %"PRId64,
                 stats->data_read);
        ios_log (this, logfp, "  BytesWritten : %"PRId64"\n",
                 stats->data_written);

        snprintf (str_header, sizeof (str_header), "%-12s %c", "Block Size", ':');
        snprintf (str_read, sizeof (str_read), "%-12s %c", "Read Count", ':');
        snprintf (str_write, sizeof (str_write), "%-12s %c", "Write Count", ':');
        index = 14;
        for (i = 0; i < 32; i++) {
                if ((stats->block_count_read[i] == 0) &&
                    (stats->block_count_write[i] == 0))
                        continue;
                per_line++;

                snprintf (str_header+index, sizeof (str_header)-index,
                          "%16dB+", (1<<i));
                if (stats->block_count_read[i])
                        snprintf (str_read+index, sizeof (str_read)-index,
                                  "%18"PRId64, stats->block_count_read[i]);
                else    snprintf (str_read+index, sizeof (str_read)-index,
                                  "%18s", "0");
                if (stats->block_count_write[i])
                        snprintf (str_write+index, sizeof (str_write)-index,
                                  "%18"PRId64, stats->block_count_write[i]);
                else    snprintf (str_write+index, sizeof (str_write)-index,
                                  "%18s", "0");

                index += 18;
                if (per_line == 3) {
                        ios_log (this, logfp, "%s", str_header);
                        ios_log (this, logfp, "%s", str_read);
                        ios_log (this, logfp, "%s\n", str_write);

                        memset (str_header, 0, sizeof (str_header));
                        memset (str_read, 0, sizeof (str_read));
                        memset (str_write, 0, sizeof (str_write));

                        snprintf (str_header, sizeof (str_header), "%-12s %c",
                                  "Block Size", ':');
                        snprintf (str_read, sizeof (str_read), "%-12s %c",
                                  "Read Count", ':');
                        snprintf (str_write, sizeof (str_write), "%-12s %c",
                                  "Write Count", ':');

                        index = 14;
                        per_line = 0;
                }
        }

        if (per_line != 0) {
                ios_log (this, logfp, "%s", str_header);
                ios_log (this, logfp, "%s", str_read);
                ios_log (this, logfp, "%s\n", str_write);
        }

        ios_log (this, logfp, "%-13s %10s %14s %14s %14s", "Fop",
                 "Call Count", "Avg-Latency", "Min-Latency",
                 "Max-Latency");
        ios_log (this, logfp, "%-13s %10s %14s %14s %14s", "---", "----------",
                 "-----------", "-----------", "-----------");

        for (i = 0; i < GF_FOP_MAXVALUE; i++) {
                if (stats->fop_hits[i] && !stats->latency[i].avg)
                        ios_log (this, logfp, "%-13s %10"PRId64" %11s "
                                 "us %11s us %11s us", gf_fop_list[i],
                                 stats->fop_hits[i], "0", "0", "0");
                else if (stats->fop_hits[i] && stats->latency[i].avg)
                        ios_log (this, logfp, "%-13s %10"PRId64" %11.2lf us "
                                 "%11.2lf us %11.2lf us", gf_fop_list[i],
                                 stats->fop_hits[i], stats->latency[i].avg,
                                 stats->latency[i].min, stats->latency[i].max);
        }
        ios_log (this, logfp, "------ ----- ----- ----- ----- ----- ----- ----- "
                 " ----- ----- ----- -----\n");

        if (interval == -1) {
                LOCK (&conf->lock);
                {
                        gf_time_fmt (timestr, sizeof timestr,
                                     conf->cumulative.max_openfd_time.tv_sec,
                                     gf_timefmt_FT);
                        snprintf (timestr + strlen (timestr), sizeof timestr - strlen (timestr),
                                  ".%"GF_PRI_SUSECONDS,
                                  conf->cumulative.max_openfd_time.tv_usec);
                        ios_log (this, logfp, "Current open fd's: %"PRId64
                                 " Max open fd's: %"PRId64" time %s",
                                 conf->cumulative.nr_opens,
                                 conf->cumulative.max_nr_opens, timestr);
                }
                UNLOCK (&conf->lock);
                ios_log (this, logfp, "\n==========Open File Stats========");
                ios_log (this, logfp, "\nCOUNT:  \t  FILE NAME");
                list_head = &conf->list[IOS_STATS_TYPE_OPEN];
                ios_dump_file_stats (list_head, this, logfp);


                ios_log (this, logfp, "\n==========Read File Stats========");
                ios_log (this, logfp, "\nCOUNT:  \t  FILE NAME");
                list_head = &conf->list[IOS_STATS_TYPE_READ];
                ios_dump_file_stats (list_head, this, logfp);

                ios_log (this, logfp, "\n==========Write File Stats========");
                ios_log (this, logfp, "\nCOUNT:  \t  FILE NAME");
                list_head = &conf->list[IOS_STATS_TYPE_WRITE];
                ios_dump_file_stats (list_head, this, logfp);

                ios_log (this, logfp, "\n==========Directory open stats========");
                ios_log (this, logfp, "\nCOUNT:  \t  DIRECTORY NAME");
                list_head = &conf->list[IOS_STATS_TYPE_OPENDIR];
                ios_dump_file_stats (list_head, this, logfp);

                ios_log (this, logfp, "\n========Directory readdirp Stats=======");
                ios_log (this, logfp, "\nCOUNT:  \t  DIRECTORY NAME");
                list_head = &conf->list[IOS_STATS_TYPE_READDIRP];
                ios_dump_file_stats (list_head, this, logfp);

                ios_log (this, logfp, "\n========Read Throughput File Stats=====");
                ios_log (this, logfp, "\nTIMESTAMP \t\t\t THROUGHPUT(KBPS)"
                         "\tFILE NAME");
                list_head = &conf->thru_list[IOS_STATS_THRU_READ];
                ios_dump_throughput_stats(list_head, this, logfp,
                                          IOS_STATS_THRU_READ);

                ios_log (this, logfp, "\n======Write Throughput File Stats======");
                ios_log (this, logfp, "\nTIMESTAMP \t\t\t THROUGHPUT(KBPS)"
                         "\tFILE NAME");
                list_head = &conf->thru_list[IOS_STATS_THRU_WRITE];
                ios_dump_throughput_stats (list_head, this, logfp,
                                           IOS_STATS_THRU_WRITE);
        }
        return 0;
}

int
io_stats_dump_global_to_dict (xlator_t *this, struct ios_global_stats *stats,
                              struct timeval *now, int interval, dict_t *dict)
{
        int             ret = 0;
        char            key[256] = {0};
        uint64_t        sec = 0;
        int             i = 0;
        uint64_t        count = 0;

        GF_ASSERT (stats);
        GF_ASSERT (now);
        GF_ASSERT (dict);
        GF_ASSERT (this);

        if (interval == -1)
                snprintf (key, sizeof (key), "cumulative");
        else
                snprintf (key, sizeof (key), "interval");
        ret = dict_set_int32 (dict, key, interval);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR, "failed to set "
                        "interval %d", interval);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%d-duration", interval);
        sec = (uint64_t) (now->tv_sec - stats->started_at.tv_sec);
        ret = dict_set_uint64 (dict, key, sec);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to set "
                        "duration(%d) - %"PRId64, interval, sec);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%d-total-read", interval);
        ret = dict_set_uint64 (dict, key, stats->data_read);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to set total "
                       "read(%d) - %"PRId64, interval, stats->data_read);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%d-total-write", interval);
        ret = dict_set_uint64 (dict, key, stats->data_written);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to set total "
                        "write(%d) - %"PRId64, interval, stats->data_written);
                goto out;
        }
        for (i = 0; i < 32; i++) {
                if (stats->block_count_read[i]) {
                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "%d-read-%d", interval,
                                  (1 << i));
                        count = stats->block_count_read[i];
                        ret = dict_set_uint64 (dict, key, count);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR, "failed to "
                                        "set read-%db+, with: %"PRId64,
                                        (1<<i), count);
                                goto out;
                        }
                }
        }

        for (i = 0; i < 32; i++) {
                if (stats->block_count_write[i]) {
                        snprintf (key, sizeof (key), "%d-write-%d", interval,
                                  (1<<i));
                        count = stats->block_count_write[i];
                        ret = dict_set_uint64 (dict, key, count);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR, "failed to "
                                        "set write-%db+, with: %"PRId64,
                                        (1<<i), count);
                                goto out;
                        }
                }
        }

        for (i = 0; i < GF_FOP_MAXVALUE; i++) {
                if (stats->fop_hits[i] == 0)
                        continue;
                snprintf (key, sizeof (key), "%d-%d-hits", interval, i);
                ret = dict_set_uint64 (dict, key, stats->fop_hits[i]);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to "
                                "set %s-fop-hits: %"PRIu64, gf_fop_list[i],
                                stats->fop_hits[i]);
                        goto out;
                }

                if (stats->latency[i].avg == 0)
                        continue;
                snprintf (key, sizeof (key), "%d-%d-avglatency", interval, i);
                ret = dict_set_double (dict, key, stats->latency[i].avg);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to set %s "
                                "avglatency(%d) with %f", gf_fop_list[i],
                                interval, stats->latency[i].avg);
                        goto out;
                }
                snprintf (key, sizeof (key), "%d-%d-minlatency", interval, i);
                ret = dict_set_double (dict, key, stats->latency[i].min);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to set %s "
                                "minlatency(%d) with %f", gf_fop_list[i],
                                interval, stats->latency[i].min);
                        goto out;
                }
                snprintf (key, sizeof (key), "%d-%d-maxlatency", interval, i);
                ret = dict_set_double (dict, key, stats->latency[i].max);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to set %s "
                                "maxlatency(%d) with %f", gf_fop_list[i],
                                interval, stats->latency[i].max);
                        goto out;
                }
        }
out:
        gf_log (this->name, GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int
io_stats_dump_global (xlator_t *this, struct ios_global_stats *stats,
                      ios_sample_buf_t *sample_buf,
                      struct timeval *now, int interval,
                      struct ios_dump_args *args)
{
        int     ret = -1;

        GF_ASSERT (args);
        GF_ASSERT (now);
        GF_ASSERT (stats);
        GF_ASSERT (this);



        switch (args->type) {
        case IOS_DUMP_TYPE_JSON_FILE:
                ret = io_stats_dump_global_to_json_logfp (
                    this, stats, sample_buf, now, interval, args->u.logfp);
        break;
        case IOS_DUMP_TYPE_FILE:
                ret = io_stats_dump_global_to_logfp (this, stats, now,
                                                     interval, args->u.logfp);
        break;
        case IOS_DUMP_TYPE_DICT:
                ret = io_stats_dump_global_to_dict (this, stats, now,
                                                    interval, args->u.dict);
        break;
        default:
                GF_ASSERT (0);
                ret = -1;
        break;
        }
        return ret;
}

int
ios_dump_args_init (struct ios_dump_args *args, ios_dump_type_t type,
                    void *output)
{
        int             ret = 0;

        GF_ASSERT (args);
        GF_ASSERT (type > IOS_DUMP_TYPE_NONE && type < IOS_DUMP_TYPE_MAX);
        GF_ASSERT (output);

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
                GF_ASSERT (0);
                ret = -1;
        }

        return ret;
}

static void
ios_global_stats_clear (struct ios_global_stats *stats, struct timeval *now)
{
        GF_ASSERT (stats);
        GF_ASSERT (now);

        memset (stats, 0, sizeof (*stats));
        stats->started_at = *now;
}

int
io_stats_dump (xlator_t *this, ios_sample_buf_t *sample_buf,
               struct ios_dump_args *args, gf1_cli_info_op op,
               gf_boolean_t is_peek)
{
        struct ios_conf         *conf = NULL;
        struct ios_global_stats  cumulative = {0, };
        struct ios_global_stats  incremental = {0, };
        int                      increment = 0;
        struct timeval           now;

        GF_ASSERT (this);
        GF_ASSERT (args);
        GF_ASSERT (args->type > IOS_DUMP_TYPE_NONE);
        GF_ASSERT (args->type < IOS_DUMP_TYPE_MAX);

        conf = this->private;

        gettimeofday (&now, NULL);
        LOCK (&conf->lock);
        {
                if (op == GF_CLI_INFO_ALL ||
                    op == GF_CLI_INFO_CUMULATIVE)
                        cumulative  = conf->cumulative;

                if (op == GF_CLI_INFO_ALL ||
                    op == GF_CLI_INFO_INCREMENTAL) {
                        incremental = conf->incremental;
                        increment = conf->increment;

                        if (!is_peek) {
                                increment = conf->increment++;

                                ios_global_stats_clear (&conf->incremental,
                                                        &now);
                        }
                }
        }
        UNLOCK (&conf->lock);

        if (op == GF_CLI_INFO_ALL ||
            op == GF_CLI_INFO_CUMULATIVE)
                io_stats_dump_global (this, &cumulative, sample_buf, &now,
                                      -1, args);

        if (op == GF_CLI_INFO_ALL ||
            op == GF_CLI_INFO_INCREMENTAL)
                io_stats_dump_global (this, &incremental, sample_buf, &now,
                                      increment, args);

        return 0;
}


int
io_stats_dump_fd (xlator_t *this, struct ios_fd *iosfd)
{
        struct ios_conf         *conf = NULL;
        struct timeval           now;
        uint64_t                 sec = 0;
        uint64_t                 usec = 0;
        int                      i = 0;

        conf = this->private;

        if (!conf->dump_fd_stats)
                return 0;

        if (!iosfd)
                return 0;

        gettimeofday (&now, NULL);

        if (iosfd->opened_at.tv_usec > now.tv_usec) {
                now.tv_usec += 1000000;
                now.tv_usec--;
        }

        sec = now.tv_sec - iosfd->opened_at.tv_sec;
        usec = now.tv_usec - iosfd->opened_at.tv_usec;

        gf_log (this->name, GF_LOG_INFO,
                "--- fd stats ---");

        if (iosfd->filename)
                gf_log (this->name, GF_LOG_INFO,
                        "      Filename : %s",
                        iosfd->filename);

        if (sec)
                gf_log (this->name, GF_LOG_INFO,
                        "      Lifetime : %"PRId64"secs, %"PRId64"usecs",
                        sec, usec);

        if (iosfd->data_read)
                gf_log (this->name, GF_LOG_INFO,
                        "     BytesRead : %"PRId64" bytes",
                        iosfd->data_read);

        if (iosfd->data_written)
                gf_log (this->name, GF_LOG_INFO,
                        "  BytesWritten : %"PRId64" bytes",
                        iosfd->data_written);

        for (i = 0; i < 32; i++) {
                if (iosfd->block_count_read[i])
                        gf_log (this->name, GF_LOG_INFO,
                                " Read %06db+ : %"PRId64,
                                (1 << i), iosfd->block_count_read[i]);
        }
        for (i = 0; i < 32; i++) {
                if (iosfd->block_count_write[i])
                        gf_log (this->name, GF_LOG_INFO,
                                "Write %06db+ : %"PRId64,
                                (1 << i), iosfd->block_count_write[i]);
        }
        return 0;
}

gf_boolean_t _should_sample (struct ios_conf *conf, glusterfs_fop_t fop_type, ios_sample_buf_t* ios_sample_buf,
                             int32_t op_ret, int32_t op_errno)
{
        int i;

        /* If sampling is disabled, return false */
        if (conf->ios_sample_interval == 0)
                return _gf_false;

        /* Sometimes it's useful to sample errors. If `fop-sample-all-errors`
         * is active, then we should sample ALL errors. */
        if (op_ret < 0 && op_errno != 0 && conf->sample_all_errors) {
                return _gf_true;
        }

        /* If `fop-sample-hard-errors` is active, we only look through a small
         * subset of errno values to sample, those which are critical to Gluster
         * functioning. */
        if (op_ret < 0 && op_errno != 0 && conf->sample_hard_errors) {
                for (i = 0; i < IOS_HARD_ERROR_LIST_SIZE; i++) {
                        if (abs (op_errno) == ios_hard_error_list[i]) {
                                return _gf_true;
                        }
                }
        }

        /* If auditing is on, sample TRUNCATE, CREATE, UNLINK, RMDIR, MKDIR 1:1 */
        if (conf->audit_creates_and_unlinks) {
                switch (fop_type) {
                        case GF_FOP_TRUNCATE:
                        case GF_FOP_CREATE:
                        case GF_FOP_UNLINK:
                        case GF_FOP_MKDIR:
                        case GF_FOP_RMDIR:
                                return _gf_true;
                        default:
                                break;
                }
        }

        /* Sample only 1 out of ios_sample_interval number of fops. */
        return (ios_sample_buf->observed % conf->ios_sample_interval == 0);
}

void ios_local_get_inode (struct ios_local *local, inode_t **inode)
{
        if (!local)
                return;

        /* In the cases that a loc is given to us,
         * we should use that as the source of truth
         * for the inode.
         */
        if (local->loc.inode) {
                *inode = local->loc.inode;
                return;
        }

        /* Fall back to the inode in the local struct,
         * but there is no guarantee this will be a valid
         * pointer.
         */
        *inode = local->inode;
}

void ios_local_get_path (call_frame_t *frame, const char **path)
{
        struct ios_stat  *iosstat  = NULL;
        struct ios_local *local    = NULL;
        inode_t          *inode    = NULL;

        local = frame->local;
        if (!local)
                goto out;

        ios_local_get_inode (local, &inode);

        if (inode) {
                /* Each inode shold have an iosstat struct attached to it.
                 * This is the preferred way to retrieve the path.
                 */
                ios_inode_ctx_get (inode, frame->this, &iosstat);
                if (iosstat) {
                        gf_log ("io-stats", GF_LOG_DEBUG, "[%s] Getting path from iostat struct",
                                fop_enum_to_string (frame->op));
                        *path = iosstat->filename;
                        goto out;
                }
        }

        /* If we don't have the iosstat attached to the inode,
         * fall back to retrieving the path via the loc struct
         * inside the local.
         */
        if (local->loc.path) {
                gf_log ("io-stats", GF_LOG_DEBUG, "[%s] Getting path from loc_t",
                        fop_enum_to_string (frame->op));
                *path = local->loc.path;
                goto out;
        }

out:
        /* If the inode and the loc don't have the path, we're out of luck.
         */
        if (!*path) {
                gf_log ("io-stats", GF_LOG_DEBUG, "Unable to get path for fop: %s",
                        fop_enum_to_string (frame->op));
        }

        return;
}

void collect_ios_latency_sample (struct ios_conf *conf,
                glusterfs_fop_t fop_type, double elapsed,
                call_frame_t *frame, int32_t op_ret, int32_t op_errno)
{
        struct ios_local *ios_local      = NULL;
        ios_sample_buf_t *ios_sample_buf = NULL;
        ios_sample_t     *ios_sample = NULL;
        struct timeval   *timestamp = NULL;
        call_stack_t     *root = NULL;
        const char       *path = NULL;

        ios_sample_buf = conf->ios_sample_buf;
        LOCK (&conf->ios_sampling_lock);

        if (!_should_sample (conf, fop_type, ios_sample_buf, op_ret, op_errno))
                goto out;

        timestamp = &frame->begin;
        root = frame->root;

        ios_sample = &(ios_sample_buf->ios_samples[ios_sample_buf->pos]);
        ios_sample->elapsed = elapsed;
        ios_sample->fop_type = fop_type;

        /* Eventually every FOP will be supported (i.e., the frame->local will be
         * of type struct ios_local), but for now, this is a safety.
         */
        switch (ios_sample->fop_type) {
                case GF_FOP_CREATE:
                case GF_FOP_OPEN:
                case GF_FOP_STAT:
                case GF_FOP_FSTAT:
                case GF_FOP_READ:
                case GF_FOP_WRITE:
                case GF_FOP_OPENDIR:
                case GF_FOP_READDIRP:
                case GF_FOP_READDIR:
                case GF_FOP_FLUSH:
                case GF_FOP_ACCESS:
                case GF_FOP_UNLINK:
                case GF_FOP_TRUNCATE:
                case GF_FOP_MKDIR:
                case GF_FOP_RMDIR:
                case GF_FOP_SETATTR:
                case GF_FOP_LOOKUP:
                case GF_FOP_INODELK:
                case GF_FOP_FINODELK:
                case GF_FOP_ENTRYLK:
                case GF_FOP_FXATTROP:
                case GF_FOP_XATTROP:
                case GF_FOP_GETXATTR:
                case GF_FOP_FGETXATTR:
                case GF_FOP_SETXATTR:
                case GF_FOP_FSETXATTR:
                case GF_FOP_STATFS:
                case GF_FOP_FSYNC:
                        ios_local_get_path (frame, &path);
                        break;
                default:
                        path = NULL;
                        break;
        }

        if (path) {
                strncpy (ios_sample->path, path, sizeof (ios_sample->path));
                ios_sample->have_path = _gf_true;
        }

        ios_sample->op_ret = op_ret;
        ios_sample->op_errno = op_errno;
        ios_sample->fop_pri = frame->pri;
        ios_sample->uid = root->uid;
        ios_sample->gid = root->gid;
        ios_sample->ns_info = frame->root->ns_info;
        (ios_sample->timestamp).tv_sec = timestamp->tv_sec;
        (ios_sample->timestamp).tv_usec = timestamp->tv_usec;
        memcpy (&ios_sample->identifier, &root->identifier, sizeof (root->identifier));

        /* Increment, making sure that if we've reached the end of the circular
         * buffer, then start from the beginning (modulo). */
        ios_sample_buf->pos = (ios_sample_buf->pos + 1) % ios_sample_buf->size;
        ios_sample_buf->collected++;
out:
        ios_sample_buf->observed++;
        UNLOCK (&conf->ios_sampling_lock);
        return;
}

static void
update_ios_latency_stats (struct ios_global_stats   *stats, double elapsed,
                          glusterfs_fop_t op)
{
        double avg;

        GF_ASSERT (stats);

        stats->latency[op].total += elapsed;

        if (!stats->latency[op].min)
                stats->latency[op].min = elapsed;
        if (stats->latency[op].min > elapsed)
                stats->latency[op].min = elapsed;
        if (stats->latency[op].max < elapsed)
                stats->latency[op].max = elapsed;

        avg = stats->latency[op].avg;

        stats->latency[op].avg = avg + (elapsed - avg) / stats->fop_hits[op];
}

int
update_ios_latency (struct ios_conf *conf, call_frame_t *frame,
                    glusterfs_fop_t op, int32_t op_ret, int32_t op_errno)
{
        double elapsed;
        struct timeval *begin, *end;

        begin = &frame->begin;
        end   = &frame->end;

        elapsed = (end->tv_sec - begin->tv_sec) * 1e6
                + (end->tv_usec - begin->tv_usec);

        update_ios_latency_stats (&conf->cumulative, elapsed, op);
        update_ios_latency_stats (&conf->incremental, elapsed, op);
        collect_ios_latency_sample (conf, op, elapsed, frame, op_ret, op_errno);

        return 0;
}

int32_t
io_stats_dump_stats_to_dict (xlator_t *this, dict_t *resp,
                             ios_stats_type_t flags, int32_t list_cnt)
{
        struct ios_conf         *conf = NULL;
        int                      cnt  = 0;
        char                     key[256];
        struct ios_stat_head    *list_head = NULL;
        struct ios_stat_list    *entry = NULL;
        int                      ret = -1;
        ios_stats_thru_t         index = IOS_STATS_THRU_MAX;
        char                     timestr[256] = {0, };
        char                    *dict_timestr = NULL;

        conf = this->private;

        switch (flags) {
                case IOS_STATS_TYPE_OPEN:
                        list_head = &conf->list[IOS_STATS_TYPE_OPEN];
                        LOCK (&conf->lock);
                        {
                                ret = dict_set_uint64 (resp, "current-open",
                                                     conf->cumulative.nr_opens);
                                if (ret)
                                        goto unlock;
                                ret = dict_set_uint64 (resp, "max-open",
                                                       conf->cumulative.max_nr_opens);

                                gf_time_fmt (timestr, sizeof timestr,
                                             conf->cumulative.max_openfd_time.tv_sec,
                                             gf_timefmt_FT);
                                if (conf->cumulative.max_openfd_time.tv_sec)
                                        snprintf (timestr + strlen (timestr), sizeof timestr - strlen (timestr),
                                                  ".%"GF_PRI_SUSECONDS,
                                                  conf->cumulative.max_openfd_time.tv_usec);

                                dict_timestr = gf_strdup (timestr);
                                if (!dict_timestr)
                                        goto unlock;
                                ret = dict_set_dynstr (resp, "max-openfd-time",
                                                       dict_timestr);
                                if (ret)
                                        goto unlock;
                        }
        unlock:
                        UNLOCK (&conf->lock);
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
        ret = dict_set_int32 (resp, "top-op", flags);
        if (!list_cnt)
                goto out;
        LOCK (&list_head->lock);
        {
                list_for_each_entry (entry, &list_head->iosstats->list, list) {

                        cnt++;
                        snprintf (key, 256, "%s-%d", "filename", cnt);
                        ret = dict_set_str (resp, key, entry->iosstat->filename);
                        if (ret)
                                goto unlock_list_head;
                         snprintf (key, 256, "%s-%d", "value",cnt);
                         ret = dict_set_uint64 (resp, key, entry->value);
                         if (ret)
                                 goto unlock_list_head;
                         if (index != IOS_STATS_THRU_MAX) {
                                 snprintf (key, 256, "%s-%d", "time-sec", cnt);
                                 ret = dict_set_int32 (resp, key,
                                         entry->iosstat->thru_counters[index].time.tv_sec);
                                 if (ret)
                                         goto unlock_list_head;
                                 snprintf (key, 256, "%s-%d", "time-usec", cnt);
                                 ret = dict_set_int32 (resp, key,
                                         entry->iosstat->thru_counters[index].time.tv_usec);
                                 if (ret)
                                         goto unlock_list_head;
                         }
                         if (cnt == list_cnt)
                                 break;

                }
        }
unlock_list_head:
        UNLOCK (&list_head->lock);
        /* ret is !=0 if some dict operation in the above critical region
         * failed. */
        if (ret)
                goto out;
        ret = dict_set_int32 (resp, "members", cnt);
 out:
        return ret;
}

static int
attach_iosstat_to_inode (xlator_t *this, inode_t *inode, const char *path, const uuid_t gfid) {
        struct   ios_stat *iosstat = NULL;

        if (!inode) {
                return -EINVAL;
        }

        ios_inode_ctx_get (inode, this, &iosstat);
        if (!iosstat) {
                iosstat = GF_CALLOC (1, sizeof (*iosstat), gf_io_stats_mt_ios_stat);
                if (!iosstat) {
                        return -ENOMEM;
                }
                iosstat->filename = gf_strdup (path);
                gf_uuid_copy (iosstat->gfid, gfid);
                LOCK_INIT (&iosstat->lock);
#if OFFSET_WRITE_DETECTOR
                iosstat->expected_write_offset = UNKNOWN_WRITE_OFFSET;
#endif
                ios_inode_ctx_set (inode, this, iosstat);
        }

        return 0;
}

int
ios_build_fd (xlator_t *this, const char *path, fd_t *fd, struct ios_fd **iosfd)
{
        struct ios_fd *ifd = NULL;
        int            ret = 0;

        ifd = GF_CALLOC (1, sizeof (*ifd), gf_io_stats_mt_ios_fd);
        if (!ifd) {
                ret = -ENOMEM;
                goto free_and_out;
        }

        if (path) {
                ifd->filename = gf_strdup (path);
                if (!ifd->filename) {
                        ret = -ENOMEM;
                        goto free_and_out;
                }
        }

        gettimeofday (&ifd->opened_at, NULL);

        if (fd)
                ios_fd_ctx_set (fd, this, ifd);

        *iosfd = ifd;

        return ret;

        /* Failure path */
free_and_out:
        if (ifd) {
                GF_FREE (ifd->filename);
                GF_FREE (ifd);
        }

        *iosfd = NULL;

        return ret;
}

int
io_stats_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, fd_t *fd,
                     inode_t *inode, struct iatt *buf,
                     struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
        struct ios_local *local = NULL;
        struct ios_conf  *conf  = NULL;
        struct ios_fd    *iosfd = NULL;

        if (op_ret < 0) {
                goto unwind;
        }

        local = frame->local;
        if (!local) {
                goto unwind;
        }

        conf = this->private;

        ios_build_fd (this, local->loc.path, fd, &iosfd);
        if (!iosfd) {
                goto unwind;
        }

        LOCK (&conf->lock);
        {
                conf->cumulative.nr_opens++;
                if (conf->cumulative.nr_opens > conf->cumulative.max_nr_opens) {
                        conf->cumulative.max_nr_opens = conf->cumulative.nr_opens;
                        conf->cumulative.max_openfd_time = iosfd->opened_at;
                }
        }
        UNLOCK (&conf->lock);

        attach_iosstat_to_inode (this, local->loc.inode, local->loc.path, buf->ia_gfid);
unwind:
        UPDATE_PROFILE_STATS (frame, CREATE, op_ret, op_errno);

        ios_free_local (frame);

        STACK_UNWIND_STRICT (create, frame, op_ret, op_errno, fd, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}


int
io_stats_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        struct ios_stat  *iosstat = NULL;
        struct ios_local *local   = NULL;
        struct ios_conf  *conf    = NULL;
        struct ios_fd    *iosfd   = NULL;

        if (op_ret < 0) {
                goto unwind;
        }

        local = frame->local;
        if (!local) {
                goto unwind;
        }

        conf = this->private;

        ios_build_fd (this, local->loc.path, fd, &iosfd);
        if (!iosfd) {
                goto unwind;
        }

        LOCK (&conf->lock);
        {
                conf->cumulative.nr_opens++;
                if (conf->cumulative.nr_opens > conf->cumulative.max_nr_opens) {
                        conf->cumulative.max_nr_opens = conf->cumulative.nr_opens;
                        conf->cumulative.max_openfd_time = iosfd->opened_at;
                }
        }
        UNLOCK (&conf->lock);

        ios_inode_ctx_get (fd->inode, this, &iosstat);
        if (iosstat) {
              BUMP_STATS (iosstat, IOS_STATS_TYPE_OPEN);
        }

        attach_iosstat_to_inode (this, local->loc.inode, local->loc.path, local->loc.inode->gfid);
unwind:
        UPDATE_PROFILE_STATS (frame, OPEN, op_ret, op_errno);

        ios_free_local (frame);

        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd, xdata);

        return 0;
}


int
io_stats_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *buf, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, STAT, op_ret, op_errno);

        ios_free_local (frame);

        STACK_UNWIND_STRICT (stat, frame, op_ret, op_errno, buf, xdata);
        return 0;
}


int
io_stats_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    struct iovec *vector, int32_t count,
                    struct iatt *buf, struct iobref *iobref, dict_t *xdata)
{
        int              len = 0;
        struct ios_stat *iosstat = NULL;
        struct ios_local *local = NULL;

        local = frame->local;
        if (!local || !local->fd)
                goto unwind;

        if (op_ret > 0) {
                len = iov_length (vector, count);
                BUMP_READ (local->fd, len);
        }

        UPDATE_PROFILE_STATS (frame, READ, op_ret, op_errno);

        ios_inode_ctx_get (local->fd->inode, this, &iosstat);
        if (iosstat) {
                BUMP_STATS (iosstat, IOS_STATS_TYPE_READ);
                BUMP_THROUGHPUT (iosstat, IOS_STATS_THRU_READ);
        }

unwind:
        ios_free_local (frame);
        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno,
                             vector, count, buf, iobref, xdata);
        return 0;

}


int
io_stats_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        struct ios_stat *iosstat = NULL;
        struct ios_local *local = NULL;
        inode_t         *inode   = NULL;

        local = frame->local;
        if (!local || !local->fd)
                goto unwind;

        UPDATE_PROFILE_STATS (frame, WRITE, op_ret, op_errno);

        ios_inode_ctx_get (local->inode, this, &iosstat);
        if (iosstat) {
                BUMP_STATS (iosstat, IOS_STATS_TYPE_WRITE);
                BUMP_THROUGHPUT (iosstat, IOS_STATS_THRU_WRITE);
        }

unwind:
        ios_free_local (frame);
        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf, xdata);
        return 0;
}

int
io_stats_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, gf_dirent_t *buf, dict_t *xdata)
{
        struct ios_local *local  = NULL;
        struct ios_stat *iosstat = NULL;

        local = frame->local;

        UPDATE_PROFILE_STATS (frame, READDIRP, op_ret, op_errno);

        ios_inode_ctx_get (local->inode, this, &iosstat);
        if (iosstat) {
                BUMP_STATS (iosstat, IOS_STATS_TYPE_READDIRP);
        }

        ios_free_local (frame);
        STACK_UNWIND_STRICT (readdirp, frame, op_ret, op_errno, buf, xdata);
        return 0;
}


int
io_stats_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, gf_dirent_t *buf, dict_t *xdata)
{
        struct ios_local *local  = NULL;
        struct ios_stat *iosstat = NULL;

        local = frame->local;

        UPDATE_PROFILE_STATS (frame, READDIR, op_ret, op_errno);

        ios_free_local (frame);

        STACK_UNWIND_STRICT (readdir, frame, op_ret, op_errno, buf, xdata);
        return 0;
}


int
io_stats_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, FSYNC, op_ret, op_errno);
        ios_free_local (frame);
        STACK_UNWIND_STRICT (fsync, frame, op_ret, op_errno, prebuf, postbuf, xdata);
        return 0;
}


int
io_stats_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      struct iatt *preop, struct iatt *postop, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, SETATTR, op_ret, op_errno);
        ios_free_local (frame);
        STACK_UNWIND_STRICT (setattr, frame, op_ret, op_errno, preop, postop, xdata);
        return 0;
}


int
io_stats_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, UNLINK, op_ret, op_errno);
        ios_free_local (frame);
        STACK_UNWIND_STRICT (unlink, frame, op_ret, op_errno,
                             preparent, postparent, xdata);
        return 0;

}


int
io_stats_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *buf,
                     struct iatt *preoldparent, struct iatt *postoldparent,
                     struct iatt *prenewparent, struct iatt *postnewparent, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, RENAME, op_ret, op_errno);
        STACK_UNWIND_STRICT (rename, frame, op_ret, op_errno, buf,
                             preoldparent, postoldparent,
                             prenewparent, postnewparent, xdata);
        return 0;
}


int
io_stats_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, const char *buf,
                       struct iatt *sbuf, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, READLINK, op_ret, op_errno);
        STACK_UNWIND_STRICT (readlink, frame, op_ret, op_errno, buf, sbuf, xdata);
        return 0;
}


int
io_stats_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     inode_t *inode, struct iatt *buf,
                     dict_t *xdata, struct iatt *postparent)
{
        struct ios_local *local = frame->local;
        if (local && local->loc.path && inode && op_ret >= 0) {
                attach_iosstat_to_inode (this, inode, local->loc.path, inode->gfid);
        }

        UPDATE_PROFILE_STATS (frame, LOOKUP, op_ret, op_errno);
        ios_free_local (frame);
        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, inode, buf, xdata,
                             postparent);
        return 0;
}


int
io_stats_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      inode_t *inode, struct iatt *buf,
                      struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, SYMLINK, op_ret, op_errno);
        STACK_UNWIND_STRICT (symlink, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}


int
io_stats_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    inode_t *inode, struct iatt *buf,
                    struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, MKNOD, op_ret, op_errno);
        STACK_UNWIND_STRICT (mknod, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}


int
io_stats_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    inode_t *inode, struct iatt *buf,
                    struct iatt *preparent, struct iatt *postparent,
                    dict_t *xdata)
{
        struct ios_local *local = frame->local;

        if (local && local->loc.path) {
                local->inode = inode_ref (inode); /* Just for consistency's
                                                   * sake.
                                                   */

                attach_iosstat_to_inode (this, inode, local->loc.path, buf->ia_gfid);
        }

        UPDATE_PROFILE_STATS (frame, MKDIR, op_ret, op_errno);
        ios_free_local (frame);
        STACK_UNWIND_STRICT (mkdir, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}


int
io_stats_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno,
                   inode_t *inode, struct iatt *buf,
                   struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, LINK, op_ret, op_errno);
        STACK_UNWIND_STRICT (link, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}


int
io_stats_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, FLUSH, op_ret, op_errno);
        ios_free_local (frame);
        STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno, xdata);
        return 0;
}


int
io_stats_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        struct ios_local *local   = NULL;
        struct ios_stat  *iosstat = NULL;
        int               ret     = -1;

        local = frame->local;
        if (!local || !local->fd)
                goto unwind;

        if (op_ret < 0)
                goto unwind;

        attach_iosstat_to_inode (this, local->inode, local->loc.path, local->inode->gfid);

        ios_fd_ctx_set (local->fd, this, 0);
        ios_inode_ctx_get (local->fd->inode, this, &iosstat);
        if (iosstat)
                BUMP_STATS (iosstat, IOS_STATS_TYPE_OPENDIR);
unwind:
        UPDATE_PROFILE_STATS (frame, OPENDIR, op_ret, op_errno);
        ios_free_local (frame);
        STACK_UNWIND_STRICT (opendir, frame, op_ret, op_errno, fd, xdata);
        return 0;
}


int
io_stats_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, RMDIR, op_ret, op_errno);
        ios_free_local (frame);
        STACK_UNWIND_STRICT (rmdir, frame, op_ret, op_errno,
                             preparent, postparent, xdata);
        return 0;
}


int
io_stats_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno,
                       struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, TRUNCATE, op_ret, op_errno);
        ios_free_local (frame);
        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno,
                             prebuf, postbuf, xdata);
        return 0;
}


int
io_stats_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct statvfs *buf, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, STATFS, op_ret, op_errno);
        ios_free_local (frame);
        STACK_UNWIND_STRICT (statfs, frame, op_ret, op_errno, buf, xdata);
        return 0;
}


int
io_stats_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, SETXATTR, op_ret, op_errno);
        ios_free_local (frame);
        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno, xdata);
        return 0;
}


int
io_stats_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, GETXATTR, op_ret, op_errno);
        ios_free_local (frame);
        STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict, xdata);
        return 0;
}


int
io_stats_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, REMOVEXATTR, op_ret, op_errno);
        ios_free_local (frame);
        STACK_UNWIND_STRICT (removexattr, frame, op_ret, op_errno, xdata);
        return 0;
}

int
io_stats_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, FSETXATTR, op_ret, op_errno);
        ios_free_local (frame);
        STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno, xdata);
        return 0;
}


int
io_stats_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, FGETXATTR, op_ret, op_errno);
        ios_free_local (frame);
        STACK_UNWIND_STRICT (fgetxattr, frame, op_ret, op_errno, dict, xdata);
        return 0;
}


int
io_stats_fremovexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, FREMOVEXATTR, op_ret, op_errno);
        STACK_UNWIND_STRICT (fremovexattr, frame, op_ret, op_errno, xdata);
        return 0;
}


int
io_stats_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, FSYNCDIR, op_ret, op_errno);
        STACK_UNWIND_STRICT (fsyncdir, frame, op_ret, op_errno, xdata);
        return 0;
}


int
io_stats_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        struct ios_local *local = frame->local;

        /* ACCESS is called before a READ when a fop fails over
         * in NFS. We need to make sure that we are attaching the
         * data correctly to this inode.
         */
        if (local && local->loc.inode && local->loc.path) {
                attach_iosstat_to_inode (this, local->loc.inode, local->loc.path, local->loc.inode->gfid);
        }

        UPDATE_PROFILE_STATS (frame, ACCESS, op_ret, op_errno);
        ios_free_local (frame);
        STACK_UNWIND_STRICT (access, frame, op_ret, op_errno, xdata);

        return 0;
}


int
io_stats_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno,
                        struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, FTRUNCATE, op_ret, op_errno);
        STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno,
                             prebuf, postbuf, xdata);
        return 0;
}


int
io_stats_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *buf, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, FSTAT, op_ret, op_errno);
        ios_free_local (frame);
        STACK_UNWIND_STRICT (fstat, frame, op_ret, op_errno, buf, xdata);
        return 0;
}


int
io_stats_fallocate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
		       int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
		       struct iatt *postbuf, dict_t *xdata)
{
	UPDATE_PROFILE_STATS (frame, FALLOCATE, op_ret, op_errno);
	STACK_UNWIND_STRICT(fallocate, frame, op_ret, op_errno, prebuf, postbuf,
			    xdata);
	return 0;
}


int
io_stats_discard_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
		     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
		     struct iatt *postbuf, dict_t *xdata)
{
	UPDATE_PROFILE_STATS (frame, DISCARD, op_ret, op_errno);
	STACK_UNWIND_STRICT(discard, frame, op_ret, op_errno, prebuf, postbuf,
			    xdata);
	return 0;
}

int
io_stats_zerofill_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, ZEROFILL, op_ret, op_errno);
        STACK_UNWIND_STRICT(zerofill, frame, op_ret, op_errno, prebuf, postbuf,
                            xdata);
        return 0;
}

int
io_stats_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct gf_flock *lock, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, LK, op_ret, op_errno);
        STACK_UNWIND_STRICT (lk, frame, op_ret, op_errno, lock, xdata);
        return 0;
}


int
io_stats_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, ENTRYLK, op_ret, op_errno);
        ios_free_local (frame);
        STACK_UNWIND_STRICT (entrylk, frame, op_ret, op_errno, xdata);
        return 0;
}


int
io_stats_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, XATTROP, op_ret, op_errno);
        ios_free_local (frame);
        STACK_UNWIND_STRICT (xattrop, frame, op_ret, op_errno, dict, xdata);
        return 0;
}


int
io_stats_fxattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, FXATTROP, op_ret, op_errno);
        ios_free_local (frame);
        STACK_UNWIND_STRICT (fxattrop, frame, op_ret, op_errno, dict, xdata);
        return 0;
}


int
io_stats_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, INODELK, op_ret, op_errno);
        ios_free_local (frame);
        STACK_UNWIND_STRICT (inodelk, frame, op_ret, op_errno, xdata);
        return 0;
}

int
io_stats_entrylk (call_frame_t *frame, xlator_t *this,
                  const char *volume, loc_t *loc, const char *basename,
                  entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
        ios_track_loc (frame, loc);

        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_entrylk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->entrylk,
                    volume, loc, basename, cmd, type, xdata);
        return 0;
}


int
io_stats_inodelk (call_frame_t *frame, xlator_t *this,
                  const char *volume, loc_t *loc, int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
        ios_track_loc (frame, loc);

        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_inodelk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->inodelk,
                    volume, loc, cmd, flock, xdata);
        return 0;
}


int
io_stats_finodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, FINODELK, op_ret, op_errno);
        ios_free_local (frame);
        STACK_UNWIND_STRICT (finodelk, frame, op_ret, op_errno, xdata);
        return 0;
}


int
io_stats_finodelk (call_frame_t *frame, xlator_t *this, const char *volume,
                   fd_t *fd, int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
        ios_track_fd (frame, fd);

        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_finodelk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->finodelk,
                    volume, fd, cmd, flock, xdata);
        return 0;
}


int
io_stats_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
                  gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
        ios_track_loc (frame, loc);

        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_xattrop_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->xattrop,
                    loc, flags, dict, xdata);
        return 0;
}


int
io_stats_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
                   gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
        ios_track_fd (frame, fd);

        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_fxattrop_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fxattrop,
                    fd, flags, dict, xdata);
        return 0;
}


int
io_stats_lookup (call_frame_t *frame, xlator_t *this,
                 loc_t *loc, dict_t *xdata)
{
        ios_track_loc (frame, loc);

        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_lookup_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup,
                    loc, xdata);
        return 0;
}


int
io_stats_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        ios_track_loc (frame, loc);

        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_stat_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->stat,
                    loc, xdata);
        return 0;
}


int
io_stats_readlink (call_frame_t *frame, xlator_t *this,
                   loc_t *loc, size_t size, dict_t *xdata)
{
        START_FOP_LATENCY (frame);

        STACK_WIND (frame, io_stats_readlink_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readlink,
                    loc, size, xdata);
        return 0;
}


int
io_stats_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc,
                mode_t mode, dev_t dev, mode_t umask, dict_t *xdata)
{
        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_mknod_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mknod,
                    loc, mode, dev, umask, xdata);
        return 0;
}


int
io_stats_mkdir (call_frame_t *frame, xlator_t *this,
                loc_t *loc, mode_t mode, mode_t umask, dict_t *xdata)
{
        ios_track_loc (frame, loc);

        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_mkdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mkdir,
                    loc, mode, umask, xdata);
        return 0;
}


int
io_stats_unlink (call_frame_t *frame, xlator_t *this,
                 loc_t *loc, int xflag, dict_t *xdata)
{
        ios_track_loc (frame, loc);

        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND_COOKIE (frame, io_stats_unlink_cbk, loc,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink,
                    loc, xflag, xdata);
        return 0;
}


int
io_stats_rmdir (call_frame_t *frame, xlator_t *this,
                loc_t *loc, int flags, dict_t *xdata)
{
        ios_track_loc (frame, loc);

        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND_COOKIE (frame, io_stats_rmdir_cbk, loc,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rmdir,
                    loc, flags, xdata);
        return 0;
}


int
io_stats_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
                  loc_t *loc, mode_t umask, dict_t *xdata)
{
        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_symlink_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->symlink,
                    linkpath, loc, umask, xdata);
        return 0;
}


int
io_stats_rename (call_frame_t *frame, xlator_t *this,
                 loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_rename_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rename,
                    oldloc, newloc, xdata);
        return 0;
}


int
io_stats_link (call_frame_t *frame, xlator_t *this,
               loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_link_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->link,
                    oldloc, newloc, xdata);
        return 0;
}


int
io_stats_setattr (call_frame_t *frame, xlator_t *this,
                  loc_t *loc, struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        ios_track_loc (frame, loc);

        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_setattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setattr,
                    loc, stbuf, valid, xdata);
        return 0;
}


int
io_stats_truncate (call_frame_t *frame, xlator_t *this,
                   loc_t *loc, off_t offset, dict_t *xdata)
{
        ios_track_loc (frame, loc);

        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_truncate_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->truncate,
                    loc, offset, xdata);
        return 0;
}


int
io_stats_open (call_frame_t *frame, xlator_t *this, loc_t *loc,
               int32_t flags, fd_t *fd, dict_t *xdata)
{
        ios_track_loc (frame, loc);
        ios_track_fd (frame, fd);

        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_open_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open,
                    loc, flags, fd, xdata);
        return 0;
}


int
io_stats_create (call_frame_t *frame, xlator_t *this,
                 loc_t *loc, int32_t flags, mode_t mode,
                 mode_t umask, fd_t *fd, dict_t *xdata)
{
        ios_track_loc (frame, loc);
        ios_track_fd (frame, fd);

        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_create_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->create,
                    loc, flags, mode, umask, fd, xdata);
        return 0;
}


int
io_stats_readv (call_frame_t *frame, xlator_t *this,
                fd_t *fd, size_t size, off_t offset, uint32_t flags, dict_t *xdata)
{
        ios_track_fd (frame, fd);

        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_readv_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readv,
                    fd, size, offset, flags, xdata);
        return 0;
}


int
io_stats_writev (call_frame_t *frame, xlator_t *this,
                 fd_t *fd, struct iovec *vector,
                 int32_t count, off_t offset,
                 uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
        int                 len = 0;
        struct ios_conf     *conf = NULL;
        struct ios_local    *local  = NULL;
        int                 ret = 0;

        ios_track_fd (frame, fd);

        len = iov_length (vector, count);

#if OFFSET_WRITE_DETECTOR
        if (conf->iamnfsd && fd->inode) {
                struct ios_stat *iosstat = NULL;

                ios_inode_ctx_get (fd->inode, this, &iosstat);
                if (iosstat) {
                        int is_offset = iosstat->expected_write_offset != UNKNOWN_WRITE_OFFSET &&
                                        iosstat->expected_write_offset != offset;

                        if (is_offset) {
                                ++conf->cumulative.offset_write_ops;
                                ++conf->incremental.offset_write_ops;
                        }
                        ++conf->cumulative.total_write_ops;
                        ++conf->incremental.total_write_ops;
                        iosstat->expected_write_offset = offset + len;
                }
        }
#endif

        BUMP_WRITE (fd, len);
        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_writev_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev,
                    fd, vector, count, offset, flags, iobref, xdata);

        return 0;

}


int
io_stats_statfs (call_frame_t *frame, xlator_t *this,
                 loc_t *loc, dict_t *xdata)
{
        ios_track_loc (frame, loc);

        START_FOP_LATENCY (frame);

        STACK_WIND (frame, io_stats_statfs_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->statfs,
                    loc, xdata);
        return 0;
}


int
io_stats_flush (call_frame_t *frame, xlator_t *this,
                fd_t *fd, dict_t *xdata)
{
        ios_track_fd (frame, fd);

        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_flush_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->flush,
                    fd, xdata);
        return 0;
}


int
io_stats_fsync (call_frame_t *frame, xlator_t *this,
                fd_t *fd, int32_t flags, dict_t *xdata)
{
        ios_track_fd (frame, fd);

        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_fsync_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsync,
                    fd, flags, xdata);
        return 0;
}


int
conditional_dump (dict_t *dict, char *key, data_t *value, void *data)
{
        struct {
                xlator_t       *this;
                inode_t        *inode;
                const char     *path;
        } *stub;
        xlator_t             *this = NULL;
        char                 *filename = NULL;
        FILE                 *logfp = NULL;
        struct ios_dump_args args = {0};
        int                   pid;
        char                  dump_key[100];
        struct ios_conf      *conf;

        stub  = data;
        this  = stub->this;
        conf = this->private;

        filename = alloca (value->len + 1);
        memset (filename, 0, value->len + 1);
        memcpy (filename, data_to_str (value), value->len);

        pid = getpid ();

        if (!strncmp (filename, "", 1)) {
                gf_log (this->name, GF_LOG_ERROR, "No filename given");
                return -1;
        }
        logfp = fopen (filename, "w+");
        if (!logfp) {
                gf_log (this->name, GF_LOG_ERROR, "failed to open %s "
                                 "for writing", filename);

                return -1;
        }
        sprintf (dump_key, "*io*stat*%d_json_dump", pid);
        if (fnmatch (dump_key, key, 0) == 0) {
                (void) ios_dump_args_init (
                                &args, IOS_DUMP_TYPE_JSON_FILE,
                                logfp);
        } else {
                (void) ios_dump_args_init (&args, IOS_DUMP_TYPE_FILE,
                                logfp);
        }
        io_stats_dump (this, conf->ios_sample_buf, &args, GF_CLI_INFO_ALL,
                       _gf_false);
        fclose (logfp);

        return 0;
}

int
_ios_destroy_dump_thread (struct ios_conf *conf) {
        conf->dump_thread_should_die = _gf_true;

        if (conf->dump_thread) {
                (void) pthread_join (conf->dump_thread, NULL);
        }
        return 0;
}

int
_ios_swap_sample_buf (xlator_t *this, ios_sample_buf_t **dest)
{
        struct ios_conf *conf = NULL;
        int ret = 0;
        ios_sample_buf_t *sample_buf;

        conf = this->private;

        /*
         * Save pointer to old buffer; the CS equivalent of
         * Indiana Jones: https://www.youtube.com/watch?v=Pr-8AP0To4k,
         * though ours will end better I hope!
         *
         */
        sample_buf = conf->ios_sample_buf;
        if (!sample_buf) {
                ret = 0;
                gf_log (this->name, GF_LOG_WARNING,
                        "Sampling buffer is null, bailing!");
                goto out;
        }

        /*
         * Init a new buffer, so we are free to work on the one we saved a
         * reference to above.
         *
         */
        if (ios_init_sample_buf (conf) != 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Failed to init new sampling buffer, out of memory?");
                ret = -ENOMEM;
                goto out;
        }

        *dest = sample_buf;

out:
        return ret;
}

void *
_ios_dump_thread (xlator_t *this) {
        struct ios_conf         *conf = NULL;
        FILE                    *stats_logfp = NULL;
        FILE                    *samples_logfp = NULL;
        FILE                    *foprate_logfp = NULL;
        struct ios_dump_args args = {0};
        ios_sample_buf_t        *sample_buf = NULL;
        int                     i;
        int                     stats_bytes_written = 0;
        int                     samples_bytes_written = 0;
        int                     foprate_bytes_written = 0;
        char                    stats_filename[PATH_MAX];
        char                    samples_filename[PATH_MAX];
        char                    foprate_filename[PATH_MAX];
        char                    *xlator_name = NULL;
        char                    *instance_name = NULL;
        time_t                  namespace_conf_mtime;
        int                     ret = 0;

        conf = this->private;
        xlator_name = strdupa (get_top_xlator_name (this));

        gf_log (this->name, GF_LOG_INFO, "IO stats dump thread started, "
                "polling IO stats every %d seconds", conf->ios_dump_interval);

        for (i = 0; i < strlen (xlator_name); i++) {
                if (xlator_name[i] == '/')
                        xlator_name[i] = '_';
        }

        instance_name = this->instance_name;
        if (conf->iamshd) {
                xlator_name = "shd";
        } else if (conf->iamnfsd) {
                instance_name = xlator_name;
                xlator_name = "nfsd";
        } else if (conf->iamgfproxyd) {
                instance_name = xlator_name;
                xlator_name = "gfproxyd";
        }

        sys_mkdir (_IOS_DUMP_DIR, S_IRWXU | S_IRWXO | S_IRWXG);
        sys_mkdir (_IOS_SAMP_DIR, S_IRWXU | S_IRWXO | S_IRWXG);

        if (instance_name) {
                stats_bytes_written = snprintf (stats_filename, PATH_MAX,
                        "%s/%s_%s_%s.dump", _IOS_DUMP_DIR,
                        __progname, xlator_name, instance_name);
                samples_bytes_written = snprintf (samples_filename,
                        PATH_MAX, "%s/%s_%s_%s.samp", _IOS_SAMP_DIR,
                        __progname, xlator_name, instance_name);
                foprate_bytes_written = snprintf (foprate_filename,
                        PATH_MAX, "%s/%s_%s_%s_namespace_stats.dump", _IOS_DUMP_DIR,
                        __progname, xlator_name, instance_name);
        } else {
                stats_bytes_written = snprintf (stats_filename, PATH_MAX,
                        "%s/%s_%s.dump", _IOS_DUMP_DIR, __progname,
                        xlator_name);
                samples_bytes_written = snprintf (samples_filename, PATH_MAX,
                        "%s/%s_%s.samp", _IOS_SAMP_DIR, __progname,
                        xlator_name);
                foprate_bytes_written = snprintf (foprate_filename, PATH_MAX,
                        "%s/%s_%s_namespace_stats.dump", _IOS_DUMP_DIR, __progname,
                        xlator_name);
        }
        if (stats_bytes_written >= PATH_MAX ||
            samples_bytes_written >= PATH_MAX) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Invalid path for stats dump (%s) and/or latency "
                        "samples (%s)", stats_filename, samples_filename);
                goto out;
        }

        gf_log (this->name, GF_LOG_INFO,
                "Writing to files: stats - %s, samples - %s, foprate - %s",
                stats_filename, samples_filename, foprate_filename);

        while (_gf_true) {
                if (conf->dump_thread_should_die) {
                        break;
                }

                sleep (conf->ios_dump_interval);

                /* First try and load the throttling conf file */
                if (conf->measure_namespace_rates) {
                        ret = ios_conf_load_namespace_conf (conf);

                        if (ret != 0) {
                                gf_log (GF_IO_STATS, GF_LOG_WARNING,
                                        "Failed to re-load & parse \"%s\", error=%s.",
                                        _IOS_NAMESPACE_CONF, strerror (ret));
                        }
                }

                /* Swap buffers */
                ret = _ios_swap_sample_buf (this, &sample_buf);
                if (ret != 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                               "Failed to swap sample buffer");
                        goto out;
                }

                stats_logfp = fopen (stats_filename, "w+");

                /* Dump statistics */
                if (stats_logfp) {
                        (void) ios_dump_args_init (
                                &args, IOS_DUMP_TYPE_JSON_FILE,
                                stats_logfp);
                        io_stats_dump (this, sample_buf, &args, GF_CLI_INFO_ALL, _gf_false);
                        fclose (stats_logfp);
                } else {
                        gf_log (this->name, GF_LOG_ERROR, "%s",
                                strerror (errno));
                }

                /* Dump latency samples */
                samples_logfp = fopen (samples_filename, "a");
                if (samples_logfp) {
                        io_stats_dump_latency_samples_logfp (this,
                                samples_logfp, sample_buf);
                        fclose (samples_logfp);
                } else {
                        gf_log (this->name, GF_LOG_ERROR, "%s",
                                strerror (errno));
                }

                /* Log throttling stats only if we were asked to measure namespace rates */
                if (conf->measure_namespace_rates) {
                        foprate_logfp = fopen (foprate_filename, "w+");
                        if (foprate_logfp) {
                               (void) ios_dump_args_init (&args, IOS_DUMP_TYPE_JSON_FILE, foprate_logfp);
                               args.this = this;
                               io_stats_dump_foprate_stats (&args);
                               fclose (foprate_logfp);
                        } else {
                               gf_log (this->name, GF_LOG_ERROR, "%s", strerror (errno));
                        }
                }

                // cleanup sample buf
                ios_destroy_sample_buf (sample_buf);
        }
out:
        gf_log (this->name, GF_LOG_INFO, "IO stats dump thread terminated");
        return NULL;
}

static gf_boolean_t
match_special_xattr (dict_t *d, char *k, data_t *val, void *mdata)
{
        gf_boolean_t ret = _gf_false;
        if (fnmatch ("*io*stat*dump", k, 0) == 0) {
                ret = _gf_true;
        }

        return ret;
}

int
io_stats_setxattr (call_frame_t *frame, xlator_t *this,
                   loc_t *loc, dict_t *dict,
                   int32_t flags, dict_t *xdata)
{
        struct {
                xlator_t     *this;
                inode_t      *inode;
                const char   *path;
        } stub;
        int ret = 0;

        stub.this  = this;
        stub.inode = loc->inode;
        stub.path  = loc->path;

        ret = dict_foreach_match (dict, match_special_xattr, NULL,
                                  conditional_dump, &stub);
        if (ret > 0) {
                /* Setxattr was on key 'io-stat-dump', hence dump and unwind
                 * from here */
                goto out;
        }

        ios_track_loc (frame, loc);

        START_FOP_LATENCY (frame);

        STACK_WIND (frame, io_stats_setxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr,
                    loc, dict, flags, xdata);
        return 0;

out:
        STACK_UNWIND_STRICT (setxattr, frame, 0, 0, NULL);
        return 0;
}


int
io_stats_getxattr (call_frame_t *frame, xlator_t *this,
                   loc_t *loc, const char *name, dict_t *xdata)
{
        ios_track_loc (frame, loc);

        START_FOP_LATENCY (frame);

        STACK_WIND (frame, io_stats_getxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->getxattr,
                    loc, name, xdata);
        return 0;
}


int
io_stats_removexattr (call_frame_t *frame, xlator_t *this,
                      loc_t *loc, const char *name, dict_t *xdata)
{
        ios_track_loc (frame, loc);

        START_FOP_LATENCY (frame);

        STACK_WIND (frame, io_stats_removexattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->removexattr,
                    loc, name, xdata);
        return 0;
}


int
io_stats_fsetxattr (call_frame_t *frame, xlator_t *this,
                    fd_t *fd, dict_t *dict,
                    int32_t flags, dict_t *xdata)
{
        ios_track_fd (frame, fd);

        START_FOP_LATENCY (frame);

        STACK_WIND (frame, io_stats_fsetxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsetxattr,
                    fd, dict, flags, xdata);
        return 0;
}


int
io_stats_fgetxattr (call_frame_t *frame, xlator_t *this,
                    fd_t *fd, const char *name, dict_t *xdata)
{
        ios_track_fd (frame, fd);

        START_FOP_LATENCY (frame);

        STACK_WIND (frame, io_stats_fgetxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fgetxattr,
                    fd, name, xdata);
        return 0;
}


int
io_stats_fremovexattr (call_frame_t *frame, xlator_t *this,
                       fd_t *fd, const char *name, dict_t *xdata)
{
        START_FOP_LATENCY (frame);

        STACK_WIND (frame, io_stats_fremovexattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fremovexattr,
                    fd, name, xdata);
        return 0;
}


int
io_stats_opendir (call_frame_t *frame, xlator_t *this,
                  loc_t *loc, fd_t *fd, dict_t *xdata)
{
        ios_track_loc (frame, loc);
        ios_track_fd (frame, fd);

        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_opendir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->opendir,
                    loc, fd, xdata);
        return 0;
}

int
io_stats_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                   off_t offset, dict_t *dict)
{
        ios_track_fd (frame, fd);

        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_readdirp_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdirp,
                    fd, size, offset, dict);
        return 0;
}


int
io_stats_readdir (call_frame_t *frame, xlator_t *this,
                  fd_t *fd, size_t size, off_t offset, dict_t *xdata)
{
        ios_track_fd (frame, fd);

        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_readdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdir,
                    fd, size, offset, xdata);
        return 0;
}


int
io_stats_fsyncdir (call_frame_t *frame, xlator_t *this,
                   fd_t *fd, int32_t datasync, dict_t *xdata)
{
        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_fsyncdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsyncdir,
                    fd, datasync, xdata);
        return 0;
}


int
io_stats_access (call_frame_t *frame, xlator_t *this,
                 loc_t *loc, int32_t mask, dict_t *xdata)
{
        ios_track_loc (frame, loc);

        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_access_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->access,
                    loc, mask, xdata);
        return 0;
}


int
io_stats_ftruncate (call_frame_t *frame, xlator_t *this,
                    fd_t *fd, off_t offset, dict_t *xdata)
{
        START_FOP_LATENCY (frame);

        FOP_THROTTLE (frame, this);

        STACK_WIND (frame, io_stats_ftruncate_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->ftruncate,
                    fd, offset, xdata);
        return 0;
}


int
io_stats_fsetattr (call_frame_t *frame, xlator_t *this,
                   fd_t *fd, struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        START_FOP_LATENCY (frame);

        STACK_WIND (frame, io_stats_setattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsetattr,
                    fd, stbuf, valid, xdata);
        return 0;
}


int
io_stats_fstat (call_frame_t *frame, xlator_t *this,
                fd_t *fd, dict_t *xdata)
{
        ios_track_fd (frame, fd);

        START_FOP_LATENCY (frame);

        STACK_WIND (frame, io_stats_fstat_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fstat,
                    fd, xdata);
        return 0;
}


int
io_stats_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t mode,
		   off_t offset, size_t len, dict_t *xdata)
{
	START_FOP_LATENCY(frame);

        FOP_THROTTLE (frame, this);

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


int
io_stats_lk (call_frame_t *frame, xlator_t *this,
             fd_t *fd, int32_t cmd, struct gf_flock *lock, dict_t *xdata)
{
        START_FOP_LATENCY (frame);

        STACK_WIND (frame, io_stats_lk_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lk,
                    fd, cmd, lock, xdata);
        return 0;
}


int
io_stats_release (xlator_t *this, fd_t *fd)
{
        struct ios_fd  *iosfd = NULL;
        struct ios_conf *conf = NULL;

        BUMP_FOP (RELEASE);

        conf = this->private;

        LOCK (&conf->lock);
        {
                conf->cumulative.nr_opens--;
        }
        UNLOCK (&conf->lock);

        ios_fd_ctx_get (fd, this, &iosfd);
        if (iosfd) {
                io_stats_dump_fd (this, iosfd);

                GF_FREE (iosfd->filename);
                GF_FREE (iosfd);
        }

        return 0;
}


int
io_stats_releasedir (xlator_t *this, fd_t *fd)
{
        BUMP_FOP (RELEASEDIR);

        return 0;
}


int
io_stats_forget (xlator_t *this, inode_t *inode)
{
        BUMP_FOP (FORGET);
        ios_stats_cleanup (this, inode);
        return 0;
}

static int
ios_init_top_stats (struct ios_conf *conf)
{
        int     i = 0;

        GF_ASSERT (conf);

        for (i = 0; i <IOS_STATS_TYPE_MAX; i++) {
                conf->list[i].iosstats = GF_CALLOC (1,
                                         sizeof(*conf->list[i].iosstats),
                                         gf_io_stats_mt_ios_stat);

                if (!conf->list[i].iosstats)
                        return -1;

                INIT_LIST_HEAD(&conf->list[i].iosstats->list);
                LOCK_INIT (&conf->list[i].lock);
        }

        for (i = 0; i < IOS_STATS_THRU_MAX; i ++) {
                conf->thru_list[i].iosstats = GF_CALLOC (1,
                                 sizeof (*conf->thru_list[i].iosstats),
                                 gf_io_stats_mt_ios_stat);

                if (!conf->thru_list[i].iosstats)
                        return -1;

                INIT_LIST_HEAD(&conf->thru_list[i].iosstats->list);
                LOCK_INIT (&conf->thru_list[i].lock);
        }

        return 0;
}

static void
ios_destroy_top_stats (struct ios_conf *conf)
{
        int                     i = 0;
        struct ios_stat_head    *list_head = NULL;
        struct ios_stat_list    *entry     = NULL;
        struct ios_stat_list    *tmp       = NULL;
        struct ios_stat_list    *list      = NULL;
        struct ios_stat         *stat      = NULL;

        GF_ASSERT (conf);

        LOCK (&conf->lock);

        conf->cumulative.nr_opens = 0;
        conf->cumulative.max_nr_opens = 0;
        conf->cumulative.max_openfd_time.tv_sec = 0;
        conf->cumulative.max_openfd_time.tv_usec = 0;

        for (i = 0; i < IOS_STATS_TYPE_MAX; i++) {
                list_head = &conf->list[i];
                if (!list_head)
                        continue;
                list_for_each_entry_safe (entry, tmp,
                                          &list_head->iosstats->list, list) {
                        list = entry;
                        stat = list->iosstat;
                        ios_stat_unref (stat);
                        list_del (&list->list);
                        GF_FREE (list);
                        list_head->members--;
                }
        }

        for (i = 0; i < IOS_STATS_THRU_MAX; i++) {
                list_head = &conf->thru_list[i];
                if (!list_head)
                        continue;
                list_for_each_entry_safe (entry, tmp,
                                          &list_head->iosstats->list, list) {
                        list = entry;
                        stat = list->iosstat;
                        ios_stat_unref (stat);
                        list_del (&list->list);
                        GF_FREE (list);
                        list_head->members--;
                }
        }

        UNLOCK (&conf->lock);

        return;
}

static int
io_stats_clear (struct ios_conf *conf)
{
        struct timeval      now;
        int                 ret = -1;

        GF_ASSERT (conf);

        if (!gettimeofday (&now, NULL))
        {
            LOCK (&conf->lock);
            {
                    ios_global_stats_clear (&conf->cumulative, &now);
                    ios_global_stats_clear (&conf->incremental, &now);
                    conf->increment = 0;
            }
            UNLOCK (&conf->lock);
            ret = 0;
        }

        return ret;
}

int32_t
io_priv (xlator_t *this)
{
        int                 i;
        char                key[GF_DUMP_MAX_BUF_LEN];
        char                key_prefix_cumulative[GF_DUMP_MAX_BUF_LEN];
        char                key_prefix_incremental[GF_DUMP_MAX_BUF_LEN];
        double              min, max, avg;
        uint64_t            count, total;
        struct ios_conf    *conf = NULL;

        conf = this->private;
        if (!conf)
                return -1;

        if(!conf->count_fop_hits || !conf->measure_latency)
                return -1;

        gf_proc_dump_write("cumulative.data_read", "%"PRIu64,
                                                conf->cumulative.data_read);
        gf_proc_dump_write("cumulative.data_written", "%"PRIu64,
                                                conf->cumulative.data_written);

        gf_proc_dump_write("incremental.data_read", "%"PRIu64,
                                                conf->incremental.data_read);
        gf_proc_dump_write("incremental.data_written", "%"PRIu64,
                                                conf->incremental.data_written);

        snprintf (key_prefix_cumulative, GF_DUMP_MAX_BUF_LEN, "%s.cumulative",
                                                                    this->name);
        snprintf (key_prefix_incremental, GF_DUMP_MAX_BUF_LEN, "%s.incremental",
                                                                    this->name);

        for (i = 0; i < GF_FOP_MAXVALUE; i++) {
                count = conf->cumulative.fop_hits[i];
                total = conf->cumulative.latency[i].total;
                min = conf->cumulative.latency[i].min;
                max = conf->cumulative.latency[i].max;
                avg = conf->cumulative.latency[i].avg;

                gf_proc_dump_build_key (key, key_prefix_cumulative,
                                        (char *)gf_fop_list[i]);

                gf_proc_dump_write (key,"%"PRId64",%"PRId64",%.03f,%.03f,%.03f",
                                    count, total, min, max, avg);

                count = conf->incremental.fop_hits[i];
                total = conf->incremental.latency[i].total;
                min = conf->incremental.latency[i].min;
                max = conf->incremental.latency[i].max;
                avg = conf->incremental.latency[i].avg;

                gf_proc_dump_build_key (key, key_prefix_incremental,
                                        (char *)gf_fop_list[i]);

                gf_proc_dump_write (key,"%"PRId64",%"PRId64",%.03f,%.03f,%.03f",
                                    count, total, min, max, avg);

        }

        return 0;
}

int
init_namespace_conf (struct ios_conf *conf)
{
        int ret;

        gf_log (GF_IO_STATS, GF_LOG_INFO,
                "Initializing structures for namespace stats tracking.");

        /* Load namespace conf for the first time. */
        ret = ios_conf_load_namespace_conf (conf);
        if (ret < 0) {
                goto out;
        }

        ret = 0;
out:
        return ret;
}

void
init_namespace_options (struct ios_conf *conf)
{
        if (!conf->iambrickd) {
                conf->throttling_enabled = _gf_false;
                conf->measure_namespace_rates = _gf_false;
                gf_log (GF_IO_STATS, GF_LOG_DEBUG,
                        "Disabling throttling because I'm not a brick.");
                return;
        }

        /* Can't throttle anything without measuring namespace rates */
        /* TODO: is there a way to do this same enforcement with the namespace
         * collection option? */
        if (conf->throttling_enabled) {
                gf_log (GF_IO_STATS, GF_LOG_DEBUG, "Throttling enabled.");
                conf->measure_namespace_rates = _gf_true;
        }

        if (conf->measure_namespace_rates) {
                if (init_namespace_conf (conf) != 0) {
                        gf_log (GF_IO_STATS, GF_LOG_WARNING,
                                "Disabling namespace measurements even though "
                                "it was marked as enabled, due to failures "
                                "during initialization!");
                        conf->throttling_enabled = _gf_false;
                        conf->measure_namespace_rates = _gf_false;
                } else {
                        gf_log (GF_IO_STATS, GF_LOG_DEBUG, "Namespace rates enabled.");
                }

                if (conf->audit_creates_and_unlinks || conf->sample_all_errors ||
                    conf->sample_hard_errors) {
                        gf_log (GF_IO_STATS, GF_LOG_WARNING,
                                "Fop sample auditing or 1:1 error sampling is "
                                "enabled, which means that namespace rates might "
                                "be reported incorrectly in some cases!");
                }
        }
}

int
reconfigure (xlator_t *this, dict_t *options)
{
        struct ios_conf    *conf = NULL;
        int                 ret = -1;
        char               *sys_log_str = NULL;
        char               *log_format_str = NULL;
        char               *logger_str = NULL;
        int                 sys_log_level = -1;
        char               *log_str = NULL;
        int                 log_level = -1;
        int                 log_format = -1;
        int                 logger = -1;
        uint32_t            log_buf_size = 0;
        uint32_t            log_flush_timeout = 0;
        int32_t             old_dump_interval;

        if (!this || !this->private)
                goto out;

        conf = this->private;

        GF_OPTION_RECONF ("dump-fd-stats", conf->dump_fd_stats, options, bool,
                          out);

        GF_OPTION_RECONF ("count-fop-hits", conf->count_fop_hits, options, bool,
                          out);

        GF_OPTION_RECONF ("latency-measurement", conf->measure_latency,
                          options, bool, out);

        old_dump_interval = conf->ios_dump_interval;
        GF_OPTION_RECONF ("ios-dump-interval", conf->ios_dump_interval, options,
                          int32, out);
        if ((old_dump_interval <= 0) && (conf->ios_dump_interval > 0)) {
                pthread_create (&conf->dump_thread, NULL,
                                (void *) &_ios_dump_thread, this);
        } else if ((old_dump_interval > 0) && (conf->ios_dump_interval <= 0)) {
                (void) _ios_destroy_dump_thread (conf);
        }

        GF_OPTION_RECONF ("ns-rate-window", conf->ns_rate_window, options,
                          int32, out);

        GF_OPTION_RECONF ("ios-sample-interval", conf->ios_sample_interval,
                         options, int32, out);
        GF_OPTION_RECONF ("ios-sample-buf-size", conf->ios_sample_buf_size,
                         options, int32, out);
        GF_OPTION_RECONF ("sys-log-level", sys_log_str, options, str, out);
        if (sys_log_str) {
                sys_log_level = glusterd_check_log_level (sys_log_str);
                set_sys_log_level (sys_log_level);
        }

        GF_OPTION_RECONF ("log-level", log_str, options, str, out);
        if (log_str) {
                log_level = glusterd_check_log_level (log_str);
                gf_log_set_loglevel (log_level);
        }

        GF_OPTION_RECONF ("logger", logger_str, options, str, out);
        if (logger_str) {
                logger = gf_check_logger (logger_str);
                gf_log_set_logger (logger);
        }

        GF_OPTION_RECONF ("log-format", log_format_str, options, str, out);
        if (log_format_str) {
                log_format = gf_check_log_format (log_format_str);
                gf_log_set_logformat (log_format);
        }

        GF_OPTION_RECONF ("log-buf-size", log_buf_size, options, uint32, out);
        gf_log_set_log_buf_size (log_buf_size);

        GF_OPTION_RECONF ("log-flush-timeout", log_flush_timeout, options,
                          time, out);
        gf_log_set_log_flush_timeout (log_flush_timeout);

        GF_OPTION_RECONF ("fop-sample-enable-audit", conf->audit_creates_and_unlinks, options, bool, out);

        GF_OPTION_RECONF ("fop-sample-all-errors", conf->sample_all_errors, options, bool, out);

        GF_OPTION_RECONF ("fop-sample-hard-errors", conf->sample_hard_errors, options, bool, out);

        GF_OPTION_RECONF ("dump-percentile-latencies", conf->dump_percentile_latencies, options, bool, out);

        GF_OPTION_RECONF ("iam-gfproxy-daemon", conf->iamgfproxyd, options, bool, out);

        GF_OPTION_RECONF ("fop-throttling-enable", conf->throttling_enabled, options, bool, out);

        GF_OPTION_RECONF ("measure-namespace-rates", conf->measure_namespace_rates, options, bool, out);

        init_namespace_options (conf);

        ret = 0;
out:
        gf_log (this ? this->name : "io-stats",
                        GF_LOG_DEBUG, "reconfigure returning %d", ret);
        return ret;
}


int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_io_stats_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
                        " failed");
                return ret;
        }

        return ret;
}

void
ios_conf_destroy (struct ios_conf *conf)
{
        if (!conf)
                return;

        dict_destroy (conf->hash_to_ns);
        dict_destroy (conf->throttling_rates);
        io_stats_destroy_fop_hits_dict (&conf->fop_hits_dict);
        ios_destroy_top_stats (conf);
        _ios_destroy_dump_thread (conf);
        LOCK_DESTROY (&conf->lock);
        GF_FREE (conf);
}

int
ios_conf_parse_namespace_conf_line (char *file_line, dict_t *hash_to_ns,
                                     dict_t *throttle_rates)
{
        int       ret       = 0;
        int       scanned   = 0;
        uint32_t  ns_hash   = 0;
        int64_t   fop_rate  = 0;
        char     *ns_name   = NULL;
        char      ns_key[DICT_UINT32_KEY_SIZE];

        ns_name = GF_CALLOC (strlen (file_line), sizeof (char), 0);

        if (!ns_name) {
                gf_log (GF_IO_STATS, GF_LOG_CRITICAL,
                        "Memory allocation error!");
                ret = ENOMEM;
                goto out;
        }

        scanned = sscanf (file_line, "%s %ld", ns_name, &fop_rate);

        if (scanned < 1 || strlen (ns_name) < 1) {
                /* TODO(mgoulet): Log this line as skipped. */
                goto out;
        }

        ns_hash = SuperFastHash (ns_name, strlen (ns_name));
        dict_uint32_to_key (ns_hash, ns_key);
        ret = dict_set_dynstr_with_alloc (hash_to_ns, ns_key, ns_name);

        if (ret != 0) {
                gf_log (GF_IO_STATS, GF_LOG_INFO,
                        "Failed to set hash/namespace pair %u -> \'%s\'",
                        ns_hash, ns_name);
                goto out;
        } else {
                gf_log (GF_IO_STATS, GF_LOG_INFO,
                        "Loaded namespace %u -> \'%s\'",
                        ns_hash, ns_name);
        }

        if (scanned == 1) {
                /* We only detected a namespace, but no throttle rate,
                 * so let's quit now. */
                goto out;
        }

        ret = dict_set_int64 (throttle_rates, ns_name, fop_rate);

        if (ret != 0) {
                gf_log (GF_IO_STATS, GF_LOG_INFO,
                        "Failed to set fop rate \'%s\' -> %ld",
                        ns_name, fop_rate);
                goto out;
        } else {
                gf_log (GF_IO_STATS, GF_LOG_INFO,
                        "Loaded fop rate \'%s\' -> %ld",
                        ns_name, fop_rate);
        }

out:
        if (ns_name) {
                GF_FREE (ns_name);
        }

        return ret;
}

int
ios_conf_load_namespace_conf (struct ios_conf *conf)
{
        int ret = 0;
        FILE *fp = NULL;
        char *line = NULL;
        size_t len = 0;
        time_t curr_mtime = {0};

        get_file_mtime (_IOS_NAMESPACE_CONF, &curr_mtime);
        if (conf->namespace_conf_mtime == curr_mtime) {
                gf_log (GF_IO_STATS, GF_LOG_DEBUG,
                        "%s has not changed, not reloading.",
                        _IOS_NAMESPACE_CONF);
                return 0;
        }

        if (!conf->fop_hits_dict) {
                conf->fop_hits_dict = dict_new ();
                if (!conf->fop_hits_dict) {
                        ret = ENOMEM;
                        goto done;
                }
        }

        if (!conf->hash_to_ns) {
                conf->hash_to_ns = dict_new ();
                if (!conf->hash_to_ns) {
                        ret = ENOMEM;
                        goto done;
                }
        }

        if (!conf->throttling_rates) {
                conf->throttling_rates = dict_new ();
                if (!conf->throttling_rates) {
                        ret = ENOMEM;
                        goto done;
                }
        }

        fp = fopen (_IOS_NAMESPACE_CONF, "r");
        if (!fp) {
                ret = ENOENT;
                goto done;
        }

        gf_log (GF_IO_STATS, GF_LOG_INFO,
                "Reloading %s from disk.",
                _IOS_NAMESPACE_CONF);

        while (getline (&line, &len, fp) != -1) {
                ios_conf_parse_namespace_conf_line (line, conf->hash_to_ns,
                                                          conf->throttling_rates);
        }

        free (line);
        conf->namespace_conf_mtime = curr_mtime;

done:
        if (fp) {
                fclose (fp);
        }

        return ret;
}

int
init (xlator_t *this)
{
        struct ios_conf    *conf = NULL;
        char               *sys_log_str = NULL;
        char               *logger_str  = NULL;
        char               *log_format_str  = NULL;
        int                 logger = -1;
        int                 log_format = -1;
        int                 sys_log_level = -1;
        char               *log_str = NULL;
        int                 log_level = -1;
        int                 ret = -1;
        uint32_t            log_buf_size = 0;
        uint32_t            log_flush_timeout = 0;
        glusterfs_ctx_t    *ctx = NULL;

        if (!this)
                return -1;

        if (!this->children) {
                gf_log (this->name, GF_LOG_ERROR,
                        "io_stats translator requires atleast one subvolume");
                return -1;
        }

        if (!this->parents) {
                /* This is very much valid as io-stats currently is loaded
                 * on top of volumes on both client and server, hence this is
                 * not an warning message */
                gf_log (this->name, GF_LOG_DEBUG,
                        "dangling volume. check volfile ");
        }

        ctx = this->ctx;
        GF_ASSERT (ctx);

        if (ctx->cmd_args.stats_instance_name) {
                this->instance_name = ctx->cmd_args.stats_instance_name;
        }

        conf = GF_CALLOC (1, sizeof(*conf), gf_io_stats_mt_ios_conf);

        if (!conf)
                goto out;


        /*
         * Init it just after calloc, so that we are sure the lock is inited
         * in case of error paths.
         */
        LOCK_INIT (&conf->lock);
        LOCK_INIT (&conf->ios_sampling_lock);

        gettimeofday (&conf->cumulative.started_at, NULL);
        gettimeofday (&conf->incremental.started_at, NULL);

        ret = ios_init_top_stats (conf);
        if (ret)
                goto out;

        GF_OPTION_INIT ("iam-self-heal-daemon", conf->iamshd, bool, out);

        GF_OPTION_INIT ("iam-nfs-daemon", conf->iamnfsd, bool, out);

        GF_OPTION_INIT ("iam-brick-daemon", conf->iambrickd, bool, out);

        GF_OPTION_INIT ("iam-gfproxy-daemon", conf->iamgfproxyd, bool, out);

        GF_OPTION_INIT ("fop-throttling-enable", conf->throttling_enabled, bool, out);

        GF_OPTION_INIT ("measure-namespace-rates", conf->measure_namespace_rates, bool, out);

        GF_OPTION_INIT ("fop-sample-enable-audit", conf->audit_creates_and_unlinks, bool, out);

        GF_OPTION_INIT ("fop-sample-all-errors", conf->sample_all_errors, bool, out);

        GF_OPTION_INIT ("fop-sample-hard-errors", conf->sample_hard_errors, bool, out);

        GF_OPTION_INIT ("dump-percentile-latencies", conf->dump_percentile_latencies, bool, out);

        init_namespace_options (conf);

        GF_OPTION_INIT ("dump-fd-stats", conf->dump_fd_stats, bool, out);

        GF_OPTION_INIT ("count-fop-hits", conf->count_fop_hits, bool, out);

        GF_OPTION_INIT ("latency-measurement", conf->measure_latency,
                        bool, out);

        GF_OPTION_INIT ("ios-dump-interval", conf->ios_dump_interval,
                        int32, out);

        GF_OPTION_INIT ("ns-rate-window", conf->ns_rate_window,
                        int32, out);

        GF_OPTION_INIT ("ios-sample-interval", conf->ios_sample_interval,
                        int32, out);

        GF_OPTION_INIT ("ios-sample-buf-size", conf->ios_sample_buf_size,
                        int32, out);

        if (ios_init_sample_buf (conf) != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory.");
                return -1;
        }

        GF_OPTION_INIT ("ios-dnscache-ttl-sec", conf->ios_dnscache_ttl_sec,
                        int32, out);
        conf->dnscache = gf_dnscache_init (conf->ios_dnscache_ttl_sec);

        GF_OPTION_INIT ("sys-log-level", sys_log_str, str, out);
        if (sys_log_str) {
                sys_log_level = glusterd_check_log_level (sys_log_str);
                set_sys_log_level (sys_log_level);
        }

        GF_OPTION_INIT ("log-level", log_str, str, out);
        if (log_str) {
                log_level = glusterd_check_log_level (log_str);
                if (DEFAULT_LOG_LEVEL != log_level)
                        gf_log_set_loglevel (log_level);
        }

        GF_OPTION_INIT ("logger", logger_str, str, out);
        if (logger_str) {
                logger = gf_check_logger (logger_str);
                gf_log_set_logger (logger);
        }

        GF_OPTION_INIT ("log-format", log_format_str, str, out);
        if (log_format_str) {
                log_format = gf_check_log_format (log_format_str);
                gf_log_set_logformat (log_format);
        }

        GF_OPTION_INIT ("log-buf-size", log_buf_size, uint32, out);
        gf_log_set_log_buf_size (log_buf_size);

        GF_OPTION_INIT ("log-flush-timeout", log_flush_timeout, time, out);
        gf_log_set_log_flush_timeout (log_flush_timeout);


        this->private = conf;
        if (conf->ios_dump_interval > 0) {
                pthread_create (&conf->dump_thread, NULL,
                                (void *) &_ios_dump_thread, this);
        }
        ret = 0;
out:
        if (!this->private) {
                ios_conf_destroy (conf);
                ret = -1;
        }

        return ret;
}

void
fini (xlator_t *this)
{
        struct ios_conf *conf = NULL;

        if (!this)
                return;

        conf = this->private;
        this->private = NULL;
        ios_conf_destroy (conf);
        gf_log (this->name, GF_LOG_INFO,
                "io-stats translator unloaded");
        return;
}

int
notify (xlator_t *this, int32_t event, void *data, ...)
{
        int          ret = 0;
        struct ios_dump_args args = {0};
        dict_t       *output = NULL;
        dict_t       *dict = NULL;
        int32_t       op = 0;
        int32_t       list_cnt = 0;
        double        throughput = 0;
        double        time = 0;
        gf_boolean_t  is_peek = _gf_false;

        va_list ap;

        dict = data;
        va_start (ap, data);
        output = va_arg (ap, dict_t*);
        va_end (ap);
        switch (event) {
        case GF_EVENT_TRANSLATOR_INFO:
                ret = dict_get_str_boolean (dict, "clear-stats", _gf_false);
                if (ret) {
                        ret = dict_set_int32 (output, "top-op", op);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to set top-op in dict");
                                goto out;
                        }
                        ios_destroy_top_stats (this->private);
                        ret = ios_init_top_stats (this->private);
                        if (ret)
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to reset top stats");
                        ret = dict_set_int32 (output, "stats-cleared",
                                              ret ? 0 : 1);
                        if (ret)
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to set stats-cleared"
                                        " in dict");
                        goto out;
                }

                ret = dict_get_int32 (dict, "top-op", &op);
                if (!ret) {
                        ret = dict_get_int32 (dict, "list-cnt", &list_cnt);
                        if (op > IOS_STATS_TYPE_NONE &&
                            op < IOS_STATS_TYPE_MAX)
                                ret = io_stats_dump_stats_to_dict (this, output,
                                                             op, list_cnt);
                        if (op == IOS_STATS_TYPE_READ_THROUGHPUT ||
                                op == IOS_STATS_TYPE_WRITE_THROUGHPUT) {
                                ret = dict_get_double (dict, "throughput",
                                                        &throughput);
                                if (!ret) {
                                        ret = dict_get_double (dict, "time",
                                                                &time);
                                        if (ret)
                                                goto out;
                                        ret = dict_set_double (output,
                                                "throughput", throughput);
                                        if (ret)
                                                goto out;
                                        ret = dict_set_double (output, "time",
                                                        time);
                                        if (ret)
                                                goto out;
                                }
                                ret = 0;

                        }
                } else {
                        ret = dict_get_int32 (dict, "info-op", &op);
                        if (ret || op < GF_CLI_INFO_ALL ||
                            GF_CLI_INFO_CLEAR < op)
                            op = GF_CLI_INFO_ALL;

                        ret = dict_set_int32 (output, "info-op", op);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to set info-op in dict");
                                goto out;
                        }

                        if (GF_CLI_INFO_CLEAR == op) {
                                ret = io_stats_clear (this->private);
                                if (ret)
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "Failed to clear info stats");

                                ret = dict_set_int32 (output, "stats-cleared",
                                              ret ? 0 : 1);
                                if (ret)
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "Failed to set stats-cleared"
                                                " in dict");
                        }
                        else {
                                ret = dict_get_str_boolean (dict, "peek",
                                                            _gf_false);
                                if (-1 != ret)
                                        is_peek = ret;

                                (void) ios_dump_args_init (&args,
                                                IOS_DUMP_TYPE_DICT, output);
                                ret = io_stats_dump (this, NULL, &args,
                                                     op, is_peek);
                        }
                }
                break;
        default:
                default_notify (this, event, data);
                break;

        }
out:
        return ret;
}

struct xlator_dumpops dumpops = {
        .priv    = io_priv
};

struct xlator_fops fops = {
        .stat        = io_stats_stat,
        .readlink    = io_stats_readlink,
        .mknod       = io_stats_mknod,
        .mkdir       = io_stats_mkdir,
        .unlink      = io_stats_unlink,
        .rmdir       = io_stats_rmdir,
        .symlink     = io_stats_symlink,
        .rename      = io_stats_rename,
        .link        = io_stats_link,
        .truncate    = io_stats_truncate,
        .open        = io_stats_open,
        .readv       = io_stats_readv,
        .writev      = io_stats_writev,
        .statfs      = io_stats_statfs,
        .flush       = io_stats_flush,
        .fsync       = io_stats_fsync,
        .setxattr    = io_stats_setxattr,
        .getxattr    = io_stats_getxattr,
        .removexattr = io_stats_removexattr,
        .fsetxattr    = io_stats_fsetxattr,
        .fgetxattr    = io_stats_fgetxattr,
        .fremovexattr = io_stats_fremovexattr,
        .opendir     = io_stats_opendir,
        .readdir     = io_stats_readdir,
        .readdirp    = io_stats_readdirp,
        .fsyncdir    = io_stats_fsyncdir,
        .access      = io_stats_access,
        .ftruncate   = io_stats_ftruncate,
        .fstat       = io_stats_fstat,
        .create      = io_stats_create,
        .lk          = io_stats_lk,
        .inodelk     = io_stats_inodelk,
        .finodelk    = io_stats_finodelk,
        .entrylk     = io_stats_entrylk,
        .lookup      = io_stats_lookup,
        .xattrop     = io_stats_xattrop,
        .fxattrop    = io_stats_fxattrop,
        .setattr     = io_stats_setattr,
        .fsetattr    = io_stats_fsetattr,
	.fallocate   = io_stats_fallocate,
	.discard     = io_stats_discard,
        .zerofill    = io_stats_zerofill,
};

struct xlator_cbks cbks = {
        .release     = io_stats_release,
        .releasedir  = io_stats_releasedir,
        .forget      = io_stats_forget,
};

struct volume_options options[] = {
        { .key  = {"dump-fd-stats"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "If on stats related to file-operations would be "
                         "tracked inside GlusterFS data-structures."
        },
        { .key  = { "ios-sample-interval" },
          .type = GF_OPTION_TYPE_INT,
          .min = 0,
          .max = 65535,
          .default_value = "100",
          .description = "Interval in which we want to collect FOP latency "
                         "samples. 2 means collect a sample every 2nd FOP."
        },
        { .key  = { "ios-sample-buf-size" },
          .type = GF_OPTION_TYPE_INT,
          .min = 1024,
          .max = 1024*1024,
          .default_value = "65535",
          .description = "The maximum size of our FOP sampling ring buffer."
        },
        { .key  = { "ios-dump-interval" },
          .type = GF_OPTION_TYPE_INT,
          .min = 0,
          .max = 3600,
          .default_value = "0",
          .description = "Interval (in seconds) at which to auto-dump "
                         "statistics. Zero disables automatic dumping."
        },
        { .key  = { "ns-rate-window" },
          .type = GF_OPTION_TYPE_INT,
          .min = 1,
          .max = FOP_HITS_SIZE,
          .default_value = "5",
          .description = "Number of ios-dump-interval length windows to calculate "
                         "our namespace FOP sma with."
        },
        { .key  = { "ios-dnscache-ttl-sec" },
          .type = GF_OPTION_TYPE_INT,
          .min = 1,
          .max = 3600 * 72,
          .default_value = "43200",
          .description = "Interval in which we want to auto-dump statistics."
        },
        { .key  = { "latency-measurement" },
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "If on stats related to the latency of each operation "
                         "would be tracked inside GlusterFS data-structures. "
        },
        { .key  = {"count-fop-hits"},
          .type = GF_OPTION_TYPE_BOOL,
        },
        { .key = {"log-level"},
          .type = GF_OPTION_TYPE_STR,
          .value = { "DEBUG", "WARNING", "ERROR", "INFO",
                     "CRITICAL", "NONE", "TRACE"}
        },

        /* These are synthetic entries to assist validation of CLI's  *
         *  volume set  command                                       */
        { .key = {"client-log-level"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = "INFO",
          .description = "Changes the log-level of the clients",
          .value = { "DEBUG", "WARNING", "ERROR", "INFO",
                     "CRITICAL", "NONE", "TRACE"}
        },
        { .key = {"sys-log-level"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = "CRITICAL",
          .description = "Gluster's syslog log-level",
          .value = { "WARNING", "ERROR", "INFO", "CRITICAL"}
        },
        { .key = {"brick-log-level"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = "INFO",
          .description = "Changes the log-level of the bricks",
          .value = { "DEBUG", "WARNING", "ERROR", "INFO",
                     "CRITICAL", "NONE", "TRACE"}
        },
        { .key = {"logger"},
          .type = GF_OPTION_TYPE_STR,
          .value = { GF_LOGGER_GLUSTER_LOG, GF_LOGGER_SYSLOG}
        },
        { .key = {"client-logger"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = GF_LOGGER_GLUSTER_LOG,
          .description = "Changes the logging sub-system to log to, for the "
                         "clients",
          .value = { GF_LOGGER_GLUSTER_LOG, GF_LOGGER_SYSLOG}
        },
        { .key = {"brick-logger"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = GF_LOGGER_GLUSTER_LOG,
          .description = "Changes the logging sub-system to log to, for the "
                         "bricks",
          .value = { GF_LOGGER_GLUSTER_LOG, GF_LOGGER_SYSLOG}
        },
        { .key = {"log-format"},
          .type = GF_OPTION_TYPE_STR,
          .value = { GF_LOG_FORMAT_NO_MSG_ID, GF_LOG_FORMAT_WITH_MSG_ID}
        },
        { .key = {"client-log-format"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = GF_LOG_FORMAT_WITH_MSG_ID,
          .description = "Changes log format for the clients",
          .value = { GF_LOG_FORMAT_NO_MSG_ID, GF_LOG_FORMAT_WITH_MSG_ID}
        },
        { .key = {"brick-log-format"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = GF_LOG_FORMAT_WITH_MSG_ID,
          .description = "Changes the log format for the bricks",
          .value = { GF_LOG_FORMAT_NO_MSG_ID, GF_LOG_FORMAT_WITH_MSG_ID}
        },
        { .key  = {"log-buf-size"},
          .type = GF_OPTION_TYPE_INT,
          .min  = GF_LOG_LRU_BUFSIZE_MIN,
          .max  = GF_LOG_LRU_BUFSIZE_MAX,
          .default_value = "5",
        },
        { .key  = {"client-log-buf-size"},
          .type = GF_OPTION_TYPE_INT,
          .min  = GF_LOG_LRU_BUFSIZE_MIN,
          .max  = GF_LOG_LRU_BUFSIZE_MAX,
          .default_value = "5",
          .description = "This option determines the maximum number of unique "
                         "log messages that can be buffered for a time equal to"
                         " the value of the option client-log-flush-timeout."
        },
        { .key  = {"brick-log-buf-size"},
          .type = GF_OPTION_TYPE_INT,
          .min  = GF_LOG_LRU_BUFSIZE_MIN,
          .max  = GF_LOG_LRU_BUFSIZE_MAX,
          .default_value = "5",
          .description = "This option determines the maximum number of unique "
                         "log messages that can be buffered for a time equal to"
                         " the value of the option brick-log-flush-timeout."
        },
        { .key  = {"log-flush-timeout"},
          .type = GF_OPTION_TYPE_TIME,
          .min  = GF_LOG_FLUSH_TIMEOUT_MIN,
          .max  = GF_LOG_FLUSH_TIMEOUT_MAX,
          .default_value = "120",
        },
        { .key  = {"client-log-flush-timeout"},
          .type = GF_OPTION_TYPE_TIME,
          .min  = GF_LOG_FLUSH_TIMEOUT_MIN,
          .max  = GF_LOG_FLUSH_TIMEOUT_MAX,
          .default_value = "120",
          .description = "This option determines the maximum number of unique "
                         "log messages that can be buffered for a time equal to"
                         " the value of the option client-log-flush-timeout."
        },
        { .key  = {"brick-log-flush-timeout"},
          .type = GF_OPTION_TYPE_TIME,
          .min  = GF_LOG_FLUSH_TIMEOUT_MIN,
          .max  = GF_LOG_FLUSH_TIMEOUT_MAX,
          .default_value = "120",
          .description = "This option determines the maximum number of unique "
                         "log messages that can be buffered for a time equal to"
                         " the value of the option brick-log-flush-timeout."
        },
        { .key = {"iam-self-heal-daemon"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "This option differentiates if the io-stats "
                         "translator is running as part of self-heal-daemon "
                         "or not."
        },
        { .key = {"iam-nfs-daemon"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "This option differentiates if the io-stats "
                         "translator is running as part of an NFS daemon "
                         "or not."
        },
        {
            .key = {"iam-brick-daemon"},
            .type = GF_OPTION_TYPE_BOOL,
            .default_value = "off",
            .description = "This option differentiates if the io-stats "
                           "translator is running as part of brick daemon "
                           "or not."
        },
        { .key = {"iam-gfproxy-daemon"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "This option differentiates if the io-stats "
                         "translator is running as part of an NFS daemon "
                         "or not."
        },
        { .key = {"fop-throttling-enable"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "This option enables whether certain namespaces "
                         "(exports) will be throttled when they exceed their "
                         "allowed FOPs/sec rate."
        },
        { .key = {"measure-namespace-rates"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "This option enables tracking of FOP rates for individual namespaces. "
                         "It is automatically set to true if fop-throttling-enable is enabled."
        },
        { .key = {"fop-sample-enable-audit"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "This option samples the following FOPs 1:1: CREATE, UNLINK, MKDIR, RMDIR, TRUNCATE."
                         " fop-sample-interval must be set to something non-zero."
        },
        { .key = {"fop-sample-all-errors"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "This option samples all fops with a non-zero op_errno "
                         "value 1:1."
        },
        { .key = {"fop-sample-hard-errors"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "This option samples all fops with \"hard errors\" 1:1,"
                         "including EROFS, ENOSPC, etc."
        },
        { .key  = { "dump-percentile-latencies" },
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "If on the p50, p90, p95, and p99 latency of each operation "
                         "and all types is dumped at each sample interval. "
        },
        { .key  = {NULL} },
};
