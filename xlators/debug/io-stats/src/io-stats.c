/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#include "xlator.h"
#endif

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

#define MAX_LIST_MEMBERS 100

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

struct ios_lat {
        double  min;
        double  max;
        double  avg;
};

struct ios_global_stats {
        uint64_t        data_written;
        uint64_t        data_read;
        uint64_t        block_count_write[32];
        uint64_t        block_count_read[32];
        uint64_t        fop_hits[GF_FOP_MAXVALUE];
        struct timeval  started_at;
        struct ios_lat  latency[GF_FOP_MAXVALUE];
        uint64_t        nr_opens;
        uint64_t        max_nr_opens;
        struct timeval  max_openfd_time;
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
        IOS_DUMP_TYPE_NONE = 0,
        IOS_DUMP_TYPE_FILE = 1,
        IOS_DUMP_TYPE_DICT = 2,
        IOS_DUMP_TYPE_MAX  = 3
} ios_dump_type_t;

struct ios_dump_args {
        ios_dump_type_t type;
        union {
                FILE *logfp;
                dict_t *dict;
        } u;
};

typedef int (*block_dump_func) (xlator_t *, struct ios_dump_args*,
                                    int , int , uint64_t ) ;

struct ios_local {
        struct timeval  wind_at;
        struct timeval  unwind_at;
};

struct volume_options options[];

inline static int
is_fop_latency_started (call_frame_t *frame)
{
        GF_ASSERT (frame);
        struct timeval epoch = {0,};
        return memcmp (&frame->begin, &epoch, sizeof (epoch));
}

#define END_FOP_LATENCY(frame, op)                                      \
        do {                                                            \
                struct ios_conf  *conf = NULL;                          \
                                                                        \
                conf = this->private;                                   \
                if (conf && conf->measure_latency) {                    \
                        gettimeofday (&frame->end, NULL);               \
                        update_ios_latency (conf, frame, GF_FOP_##op);  \
                }                                                       \
        } while (0)

#define START_FOP_LATENCY(frame)                                         \
        do {                                                             \
                struct ios_conf  *conf = NULL;                           \
                                                                         \
                conf = this->private;                                    \
                if (conf && conf->measure_latency) {                     \
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

#define UPDATE_PROFILE_STATS(frame, op)                                       \
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
                                gettimeofday (&frame->end, NULL);             \
                                update_ios_latency (conf, frame, GF_FOP_##op);\
                        }                                                     \
                }                                                             \
                UNLOCK (&conf->lock);                                         \
        } while (0)

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
                                                                        \
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


#define BUMP_THROUGHPUT(iosstat, type)						\
        do {									\
                struct ios_conf         *conf = NULL;				\
                double                   elapsed;				\
                struct timeval          *begin, *end;				\
                double                   throughput;				\
               int                      flag = 0;                              \
                                                                                \
                begin = &frame->begin;						\
                end   = &frame->end;						\
                                                                                \
                elapsed = (end->tv_sec - begin->tv_sec) * 1e6			\
                        + (end->tv_usec - begin->tv_usec);			\
                throughput = op_ret / elapsed;					\
                                                                                \
                conf = this->private;						\
                LOCK(&iosstat->lock);						\
                {								\
                        if (iosstat->thru_counters[type].throughput             \
                                <= throughput) {                                \
                                iosstat->thru_counters[type].throughput =       \
                                                                throughput;     \
                                gettimeofday (&iosstat->                        \
                                             thru_counters[type].time, NULL);   \
                               flag = 1;                                       \
                        }							\
                }								\
                UNLOCK (&iosstat->lock);					\
               if (flag)                                                       \
                       ios_stat_add_to_list (&conf->thru_list[type],           \
                                               throughput, iosstat);           \
        } while (0)

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

                        if (!uuid_compare (iosstat->gfid,
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
                        stat = last->iosstat;
                        last->iosstat = NULL;
                        ios_stat_unref (stat);
                        list_del (&last->list);
                        GF_FREE (last);
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

inline int
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
                gf_log (this->name, GF_LOG_INFO, fmt);        \
        } while (0)

int
ios_dump_file_stats (struct ios_stat_head *list_head, xlator_t *this, FILE* logfp)
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
                            FILE* logfp, ios_stats_type_t type)
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
io_stats_dump_global_to_logfp (xlator_t *this, struct ios_global_stats *stats,
                               struct timeval *now, int interval, FILE* logfp)
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
                ios_dump_throughput_stats(list_head, this, logfp, IOS_STATS_THRU_READ);

                ios_log (this, logfp, "\n======Write Throughput File Stats======");
                ios_log (this, logfp, "\nTIMESTAMP \t\t\t THROUGHPUT(KBPS)"
                         "\tFILE NAME");
                list_head = &conf->thru_list[IOS_STATS_THRU_WRITE];
                ios_dump_throughput_stats (list_head, this, logfp, IOS_STATS_THRU_WRITE);
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
                      struct timeval *now, int interval,
                      struct ios_dump_args *args)
{
        int     ret = -1;

        GF_ASSERT (args);
        GF_ASSERT (now);
        GF_ASSERT (stats);
        GF_ASSERT (this);



        switch (args->type) {
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
io_stats_dump (xlator_t *this, struct ios_dump_args *args,
               gf1_cli_stats_op op, gf_boolean_t is_peek)
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
                io_stats_dump_global (this, &cumulative, &now, -1, args);

        if (op == GF_CLI_INFO_ALL ||
            op == GF_CLI_INFO_INCREMENTAL)
                io_stats_dump_global (this, &incremental, &now, increment, args);

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

static void
update_ios_latency_stats (struct ios_global_stats   *stats, double elapsed,
                          glusterfs_fop_t op)
{
        double avg;

        GF_ASSERT (stats);

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
                    glusterfs_fop_t op)
{
        double elapsed;
        struct timeval *begin, *end;

        begin = &frame->begin;
        end   = &frame->end;

        elapsed = (end->tv_sec - begin->tv_sec) * 1e6
                + (end->tv_usec - begin->tv_usec);

        update_ios_latency_stats (&conf->cumulative, elapsed, op);
        update_ios_latency_stats (&conf->incremental, elapsed, op);

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

int
io_stats_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, fd_t *fd,
                     inode_t *inode, struct iatt *buf,
                     struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
        struct ios_fd *iosfd = NULL;
        char          *path = NULL;
        struct ios_stat *iosstat = NULL;
        struct ios_conf   *conf = NULL;

        conf = this->private;

        path = frame->local;
        frame->local = NULL;

        if (!path)
                goto unwind;

        if (op_ret < 0) {
                GF_FREE (path);
                goto unwind;
        }

        iosfd = GF_CALLOC (1, sizeof (*iosfd), gf_io_stats_mt_ios_fd);
        if (!iosfd) {
                GF_FREE (path);
                goto unwind;
        }

        iosfd->filename = path;
        gettimeofday (&iosfd->opened_at, NULL);

        ios_fd_ctx_set (fd, this, iosfd);
        LOCK (&conf->lock);
        {
                conf->cumulative.nr_opens++;
                if (conf->cumulative.nr_opens > conf->cumulative.max_nr_opens) {
                        conf->cumulative.max_nr_opens = conf->cumulative.nr_opens;
                        conf->cumulative.max_openfd_time = iosfd->opened_at;
                }
        }
        UNLOCK (&conf->lock);

        iosstat = GF_CALLOC (1, sizeof (*iosstat), gf_io_stats_mt_ios_stat);
        if (!iosstat) {
                GF_FREE (path);
                goto unwind;
        }
        iosstat->filename = gf_strdup (path);
        uuid_copy (iosstat->gfid, buf->ia_gfid);
        LOCK_INIT (&iosstat->lock);
        ios_inode_ctx_set (fd->inode, this, iosstat);

unwind:
        UPDATE_PROFILE_STATS (frame, CREATE);
        STACK_UNWIND_STRICT (create, frame, op_ret, op_errno, fd, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}


int
io_stats_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        struct ios_fd *iosfd = NULL;
        char          *path = NULL;
        struct   ios_stat *iosstat = NULL;
        struct ios_conf   *conf = NULL;

        conf = this->private;
        path = frame->local;
        frame->local = NULL;

        if (!path)
                goto unwind;

        if (op_ret < 0) {
                GF_FREE (path);
                goto unwind;
        }

        iosfd = GF_CALLOC (1, sizeof (*iosfd), gf_io_stats_mt_ios_fd);
        if (!iosfd) {
                GF_FREE (path);
                goto unwind;
        }

        iosfd->filename = path;
        gettimeofday (&iosfd->opened_at, NULL);

        ios_fd_ctx_set (fd, this, iosfd);

        ios_inode_ctx_get (fd->inode, this, &iosstat);
        if (!iosstat) {
                iosstat = GF_CALLOC (1, sizeof (*iosstat),
                                     gf_io_stats_mt_ios_stat);
                if (iosstat) {
                        iosstat->filename = gf_strdup (path);
                        uuid_copy (iosstat->gfid, fd->inode->gfid);
                        LOCK_INIT (&iosstat->lock);
                        ios_inode_ctx_set (fd->inode, this, iosstat);
                }
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
        if (iosstat) {
              BUMP_STATS (iosstat, IOS_STATS_TYPE_OPEN);
              iosstat = NULL;
        }
unwind:
        UPDATE_PROFILE_STATS (frame, OPEN);

        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd, xdata);
        return 0;

}


int
io_stats_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *buf, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, STAT);
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
        fd_t            *fd = NULL;
        struct ios_stat *iosstat = NULL;

        fd = frame->local;
        frame->local = NULL;

        if (op_ret > 0) {
                len = iov_length (vector, count);
                BUMP_READ (fd, len);
        }

        UPDATE_PROFILE_STATS (frame, READ);
        ios_inode_ctx_get (fd->inode, this, &iosstat);

        if (iosstat) {
              BUMP_STATS (iosstat, IOS_STATS_TYPE_READ);
              BUMP_THROUGHPUT (iosstat, IOS_STATS_THRU_READ);
              iosstat = NULL;
        }

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
        inode_t         *inode   = NULL;

        UPDATE_PROFILE_STATS (frame, WRITE);
        if (frame->local){
                inode = frame->local;
                frame->local = NULL;
                ios_inode_ctx_get (inode, this, &iosstat);
                if (iosstat) {
                        BUMP_STATS (iosstat, IOS_STATS_TYPE_WRITE);
                        BUMP_THROUGHPUT (iosstat, IOS_STATS_THRU_WRITE);
                        inode = NULL;
                        iosstat = NULL;
                }
        }

        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf, xdata);
        return 0;

}




int
io_stats_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, gf_dirent_t *buf, dict_t *xdata)
{
        struct ios_stat *iosstat = NULL;
        inode_t         *inode   = frame->local;

        frame->local = NULL;

        UPDATE_PROFILE_STATS (frame, READDIRP);

        ios_inode_ctx_get (inode, this, &iosstat);

        if (iosstat) {
              BUMP_STATS (iosstat, IOS_STATS_TYPE_READDIRP);
              iosstat = NULL;
        }

        STACK_UNWIND_STRICT (readdirp, frame, op_ret, op_errno, buf, xdata);
        return 0;
}


int
io_stats_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, gf_dirent_t *buf, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, READDIR);
        STACK_UNWIND_STRICT (readdir, frame, op_ret, op_errno, buf, xdata);
        return 0;
}


int
io_stats_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, FSYNC);
        STACK_UNWIND_STRICT (fsync, frame, op_ret, op_errno, prebuf, postbuf, xdata);
        return 0;
}


int
io_stats_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      struct iatt *preop, struct iatt *postop, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, SETATTR);
        STACK_UNWIND_STRICT (setattr, frame, op_ret, op_errno, preop, postop, xdata);
        return 0;
}


int
io_stats_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, UNLINK);
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
        UPDATE_PROFILE_STATS (frame, RENAME);
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
        UPDATE_PROFILE_STATS (frame, READLINK);
        STACK_UNWIND_STRICT (readlink, frame, op_ret, op_errno, buf, sbuf, xdata);
        return 0;
}


int
io_stats_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     inode_t *inode, struct iatt *buf,
                     dict_t *xdata, struct iatt *postparent)
{
        UPDATE_PROFILE_STATS (frame, LOOKUP);
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
        UPDATE_PROFILE_STATS (frame, SYMLINK);
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
        UPDATE_PROFILE_STATS (frame, MKNOD);
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
        struct ios_stat *iosstat = NULL;
        char   *path = frame->local;

        UPDATE_PROFILE_STATS (frame, MKDIR);
        if (op_ret < 0)
                goto unwind;

        iosstat = GF_CALLOC (1, sizeof (*iosstat), gf_io_stats_mt_ios_stat);
        if (iosstat) {
                LOCK_INIT (&iosstat->lock);
                iosstat->filename = gf_strdup(path);
                uuid_copy (iosstat->gfid, buf->ia_gfid);
                ios_inode_ctx_set (inode, this, iosstat);
        }

unwind:
        /* local is assigned with path */
        GF_FREE (frame->local);
        frame->local = NULL;
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
        UPDATE_PROFILE_STATS (frame, LINK);
        STACK_UNWIND_STRICT (link, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}


int
io_stats_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, FLUSH);
        STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno, xdata);
        return 0;
}


int
io_stats_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        struct ios_stat *iosstat = NULL;
        int              ret     = -1;

        UPDATE_PROFILE_STATS (frame, OPENDIR);
        if (op_ret < 0)
                goto unwind;

        ios_fd_ctx_set (fd, this, 0);

        ret = ios_inode_ctx_get (fd->inode, this, &iosstat);
        if (!ret)
                BUMP_STATS (iosstat, IOS_STATS_TYPE_OPENDIR);

unwind:
        STACK_UNWIND_STRICT (opendir, frame, op_ret, op_errno, fd, xdata);
        return 0;
}


int
io_stats_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{

        UPDATE_PROFILE_STATS (frame, RMDIR);

        STACK_UNWIND_STRICT (rmdir, frame, op_ret, op_errno,
                             preparent, postparent, xdata);
        return 0;
}


int
io_stats_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno,
                       struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, TRUNCATE);
        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno,
                             prebuf, postbuf, xdata);
        return 0;
}


int
io_stats_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct statvfs *buf, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, STATFS);
        STACK_UNWIND_STRICT (statfs, frame, op_ret, op_errno, buf, xdata);
        return 0;
}


int
io_stats_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, SETXATTR);
        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno, xdata);
        return 0;
}


int
io_stats_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, GETXATTR);
        STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict, xdata);
        return 0;
}


int
io_stats_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, REMOVEXATTR);
        STACK_UNWIND_STRICT (removexattr, frame, op_ret, op_errno, xdata);
        return 0;
}

int
io_stats_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, FSETXATTR);
        STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno, xdata);
        return 0;
}


int
io_stats_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, FGETXATTR);
        STACK_UNWIND_STRICT (fgetxattr, frame, op_ret, op_errno, dict, xdata);
        return 0;
}


int
io_stats_fremovexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, FREMOVEXATTR);
        STACK_UNWIND_STRICT (fremovexattr, frame, op_ret, op_errno, xdata);
        return 0;
}


int
io_stats_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, FSYNCDIR);
        STACK_UNWIND_STRICT (fsyncdir, frame, op_ret, op_errno, xdata);
        return 0;
}


int
io_stats_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, ACCESS);
        STACK_UNWIND_STRICT (access, frame, op_ret, op_errno, xdata);
        return 0;
}


int
io_stats_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno,
                        struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, FTRUNCATE);
        STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno,
                             prebuf, postbuf, xdata);
        return 0;
}


int
io_stats_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *buf, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, FSTAT);
        STACK_UNWIND_STRICT (fstat, frame, op_ret, op_errno, buf, xdata);
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

int
io_stats_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct gf_flock *lock, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, LK);
        STACK_UNWIND_STRICT (lk, frame, op_ret, op_errno, lock, xdata);
        return 0;
}


int
io_stats_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, ENTRYLK);
        STACK_UNWIND_STRICT (entrylk, frame, op_ret, op_errno, xdata);
        return 0;
}


int
io_stats_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, XATTROP);
        STACK_UNWIND_STRICT (xattrop, frame, op_ret, op_errno, dict, xdata);
        return 0;
}


int
io_stats_fxattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, FXATTROP);
        STACK_UNWIND_STRICT (fxattrop, frame, op_ret, op_errno, dict, xdata);
        return 0;
}


int
io_stats_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        UPDATE_PROFILE_STATS (frame, INODELK);
        STACK_UNWIND_STRICT (inodelk, frame, op_ret, op_errno, xdata);
        return 0;
}

int
io_stats_entrylk (call_frame_t *frame, xlator_t *this,
                  const char *volume, loc_t *loc, const char *basename,
                  entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
        START_FOP_LATENCY (frame);

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

        START_FOP_LATENCY (frame);

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

        UPDATE_PROFILE_STATS (frame, FINODELK);
        STACK_UNWIND_STRICT (finodelk, frame, op_ret, op_errno, xdata);
        return 0;
}


int
io_stats_finodelk (call_frame_t *frame, xlator_t *this, const char *volume,
                   fd_t *fd, int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
        START_FOP_LATENCY (frame);

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
        START_FOP_LATENCY (frame);

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
        START_FOP_LATENCY (frame);

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
        START_FOP_LATENCY (frame);

        STACK_WIND (frame, io_stats_lookup_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup,
                    loc, xdata);
        return 0;
}


int
io_stats_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        START_FOP_LATENCY (frame);

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
        frame->local = gf_strdup (loc->path);

        START_FOP_LATENCY (frame);

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
        START_FOP_LATENCY (frame);

        STACK_WIND (frame, io_stats_unlink_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink,
                    loc, xflag, xdata);
        return 0;
}


int
io_stats_rmdir (call_frame_t *frame, xlator_t *this,
                loc_t *loc, int flags, dict_t *xdata)
{
        START_FOP_LATENCY (frame);

        STACK_WIND (frame, io_stats_rmdir_cbk,
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
        START_FOP_LATENCY (frame);

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
        START_FOP_LATENCY (frame);

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
        frame->local = gf_strdup (loc->path);

        START_FOP_LATENCY (frame);

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
        frame->local = gf_strdup (loc->path);

        START_FOP_LATENCY (frame);

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
        frame->local = fd;

        START_FOP_LATENCY (frame);

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

        if (fd->inode)
                frame->local = fd->inode;
        len = iov_length (vector, count);

        BUMP_WRITE (fd, len);
        START_FOP_LATENCY (frame);

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
        START_FOP_LATENCY (frame);

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
        START_FOP_LATENCY (frame);

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

        stub  = data;
        this  = stub->this;

        filename = alloca (value->len + 1);
        memset (filename, 0, value->len + 1);
        memcpy (filename, data_to_str (value), value->len);

        if (fnmatch ("*io*stat*dump", key, 0) == 0) {

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
                (void) ios_dump_args_init (&args, IOS_DUMP_TYPE_FILE,
                                           logfp);
                io_stats_dump (this, &args, GF_CLI_INFO_ALL, _gf_false);
                fclose (logfp);
        }
        return 0;
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

        stub.this  = this;
        stub.inode = loc->inode;
        stub.path  = loc->path;

        dict_foreach (dict, conditional_dump, &stub);

        START_FOP_LATENCY (frame);

        STACK_WIND (frame, io_stats_setxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr,
                    loc, dict, flags, xdata);
        return 0;
}


int
io_stats_getxattr (call_frame_t *frame, xlator_t *this,
                   loc_t *loc, const char *name, dict_t *xdata)
{
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

        START_FOP_LATENCY (frame);

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
        frame->local = fd->inode;
        START_FOP_LATENCY (frame);

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
        START_FOP_LATENCY (frame);

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
        START_FOP_LATENCY (frame);

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

int
reconfigure (xlator_t *this, dict_t *options)
{
        struct ios_conf    *conf = NULL;
        int                 ret = -1;
        char               *sys_log_str = NULL;
        int                 sys_log_level = -1;
        char               *log_str = NULL;
        int                 log_level = -1;

        if (!this || !this->private)
                goto out;

        conf = this->private;

        GF_OPTION_RECONF ("dump-fd-stats", conf->dump_fd_stats, options, bool,
                          out);

        GF_OPTION_RECONF ("count-fop-hits", conf->count_fop_hits, options, bool,
                          out);

        GF_OPTION_RECONF ("latency-measurement", conf->measure_latency,
                          options, bool, out);

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

        ret = 0;
out:
        gf_log (this->name, GF_LOG_DEBUG, "reconfigure returning %d", ret);
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

int
init (xlator_t *this)
{
        struct ios_conf    *conf = NULL;
        char               *sys_log_str = NULL;
        int                 sys_log_level = -1;
        char               *log_str = NULL;
        int                 log_level = -1;
        int                 ret = -1;

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

        conf = GF_CALLOC (1, sizeof(*conf), gf_io_stats_mt_ios_conf);

        if (!conf) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory.");
                return -1;
        }

        LOCK_INIT (&conf->lock);

        gettimeofday (&conf->cumulative.started_at, NULL);
        gettimeofday (&conf->incremental.started_at, NULL);

        ret = ios_init_top_stats (conf);
        if (ret)
                return -1;

        GF_OPTION_INIT ("dump-fd-stats", conf->dump_fd_stats, bool, out);

        GF_OPTION_INIT ("count-fop-hits", conf->count_fop_hits, bool, out);

        GF_OPTION_INIT ("latency-measurement", conf->measure_latency,
                          bool, out);

        GF_OPTION_INIT ("sys-log-level", sys_log_str, str, out);
        if (sys_log_str) {
                sys_log_level = glusterd_check_log_level (sys_log_str);
                set_sys_log_level (sys_log_level);
        }

        GF_OPTION_INIT ("log-level", log_str, str, out);
        if (log_str) {
                log_level = glusterd_check_log_level (log_str);
                gf_log_set_loglevel (log_level);
        }

        this->private = conf;
        ret = 0;
out:
        return ret;
}


void
fini (xlator_t *this)
{
        struct ios_conf *conf = NULL;

        if (!this)
                return;

        conf = this->private;

        if (!conf)
                return;
        this->private = NULL;

        ios_destroy_top_stats (conf);

        GF_FREE(conf);

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
                                ret = io_stats_dump (this, &args, op, is_peek);
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
        { .key  = {NULL} },

};
