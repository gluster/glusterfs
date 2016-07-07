/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <assert.h>
#include "locking.h"

#include "compat.h"
#include "list.h"
#include "syscall.h"

#define THREAD_MAX 32
#define BUMP(name) INC(name, 1)
#define DEFAULT_WORKERS 4

#define NEW(x) {                              \
        x = calloc (1, sizeof (typeof (*x))); \
        }

#define err(x ...) fprintf(stderr, x)
#define out(x ...) fprintf(stdout, x)
#define dbg(x ...) do { if (debug) fprintf(stdout, x); } while (0)
#define tout(x ...) do { out("[%ld] ", pthread_self()); out(x); } while (0)
#define terr(x ...) do { err("[%ld] ", pthread_self()); err(x); } while (0)
#define tdbg(x ...) do { dbg("[%ld] ", pthread_self()); dbg(x); } while (0)

int debug = 0;
const char *slavemnt = NULL;
int workers = 0;

struct stats {
        unsigned long long int cnt_skipped_gfids;
};

pthread_spinlock_t stats_lock;

struct stats stats_total;
int stats = 0;

#define INC(name, val) do {                             \
        if (!stats)                                     \
                break;                                  \
        pthread_spin_lock(&stats_lock);                 \
        {                                               \
                stats_total.cnt_##name += val;          \
        }                                               \
        pthread_spin_unlock(&stats_lock);               \
        } while (0)

void
stats_dump()
{
        if (!stats)
                return;

        out("-------------------------------------------\n");
        out("Skipped_Files : %10lld\n", stats_total.cnt_skipped_gfids);
        out("-------------------------------------------\n");
}

struct dirjob {
        struct list_head    list;

        char               *dirname;

        struct dirjob      *parent;
        int                 ret;    /* final status of this subtree */
        int                 refcnt; /* how many dirjobs have this as parent */

        pthread_spinlock_t  lock;
};


struct xwork {
        pthread_t        cthreads[THREAD_MAX]; /* crawler threads */
        int              count;
        int              idle;
        int              stop;

        struct dirjob    crawl;

        struct dirjob   *rootjob; /* to verify completion in xwork_fini() */

        pthread_mutex_t  mutex;
        pthread_cond_t   cond;
};


struct dirjob *
dirjob_ref (struct dirjob *job)
{
        pthread_spin_lock (&job->lock);
        {
                job->refcnt++;
        }
        pthread_spin_unlock (&job->lock);

        return job;
}


void
dirjob_free (struct dirjob *job)
{
        assert (list_empty (&job->list));

        pthread_spin_destroy (&job->lock);
        free (job->dirname);
        free (job);
}

void
dirjob_ret (struct dirjob *job, int err)
{
        int            ret = 0;
        int            refcnt = 0;
        struct dirjob *parent = NULL;

        pthread_spin_lock (&job->lock);
        {
                refcnt = --job->refcnt;
                job->ret = (job->ret || err);
        }
        pthread_spin_unlock (&job->lock);

        if (refcnt == 0) {
                ret = job->ret;

                if (ret)
                        terr ("Failed: %s (%d)\n", job->dirname, ret);
                else
                        tdbg ("Finished: %s\n", job->dirname);

                parent = job->parent;
                if (parent)
                        dirjob_ret (parent, ret);

                dirjob_free (job);
                job = NULL;
        }
}


struct dirjob *
dirjob_new (const char *dir, struct dirjob *parent)
{
        struct dirjob *job = NULL;

        NEW(job);
        if (!job)
                return NULL;

        job->dirname = strdup (dir);
        if (!job->dirname) {
                free (job);
                return NULL;
        }

        INIT_LIST_HEAD(&job->list);
        pthread_spin_init (&job->lock, PTHREAD_PROCESS_PRIVATE);
        job->ret = 0;

        if (parent)
                job->parent = dirjob_ref (parent);

        job->refcnt = 1;

        return job;
}

void
xwork_addcrawl (struct xwork *xwork, struct dirjob *job)
{
        pthread_mutex_lock (&xwork->mutex);
        {
                list_add_tail (&job->list, &xwork->crawl.list);
                pthread_cond_broadcast (&xwork->cond);
        }
        pthread_mutex_unlock (&xwork->mutex);
}

int
xwork_add (struct xwork *xwork, const char *dir, struct dirjob *parent)
{
        struct dirjob *job = NULL;

        job = dirjob_new (dir, parent);
        if (!job)
                return -1;

        xwork_addcrawl (xwork, job);

        return 0;
}


struct dirjob *
xwork_pick (struct xwork *xwork, int block)
{
        struct dirjob *job = NULL;
        struct list_head *head = NULL;

        head = &xwork->crawl.list;

        pthread_mutex_lock (&xwork->mutex);
        {
                for (;;) {
                        if (xwork->stop)
                                break;

                        if (!list_empty (head)) {
                                job = list_entry (head->next, typeof(*job),
                                                  list);
                                list_del_init (&job->list);
                                break;
                        }

                        if (((xwork->count * 2) == xwork->idle) &&
                            list_empty (&xwork->crawl.list)) {
                                /* no outstanding jobs, and no
                                   active workers
                                */
                                tdbg ("Jobless. Terminating\n");
                                xwork->stop = 1;
                                pthread_cond_broadcast (&xwork->cond);
                                break;
                        }

                        if (!block)
                                break;

                        xwork->idle++;
                        pthread_cond_wait (&xwork->cond, &xwork->mutex);
                        xwork->idle--;
                }
        }
        pthread_mutex_unlock (&xwork->mutex);

        return job;
}

int
skip_name (const char *dirname, const char *name)
{
        if (strcmp (name, ".") == 0)
                return 1;

        if (strcmp (name, "..") == 0)
                return 1;

        if (strcmp (name, "changelogs") == 0)
                return 1;

        if (strcmp (name, "health_check") == 0)
                return 1;

        if (strcmp (name, "indices") == 0)
                return 1;

        if (strcmp (name, "landfill") == 0)
                return 1;

        return 0;
}

int
skip_stat (struct dirjob *job, const char *name)
{
        if (job == NULL)
                return 0;

        if (strcmp (job->dirname, ".glusterfs") == 0) {
                tdbg ("Directly adding directories under .glusterfs "
                      "to global list: %s\n", name);
                return 1;
        }

        if (job->parent != NULL) {
                if (strcmp (job->parent->dirname, ".glusterfs") == 0) {
                        tdbg ("Directly adding directories under .glusterfs/XX "
                              "to global list: %s\n", name);
                        return 1;
                }
        }

        return 0;
}

int
xworker_do_crawl (struct xwork *xwork, struct dirjob *job)
{
        DIR            *dirp = NULL;
        int             ret = -1;
        int             boff;
        int             plen;
        char           *path = NULL;
        struct dirjob  *cjob = NULL;
        struct stat     statbuf = {0,};
        struct dirent  *entry;
        struct dirent   scratch[2] = {{0,},};
        char            gfid_path[PATH_MAX] = {0,};


        plen = strlen (job->dirname) + 256 + 2;
        path = alloca (plen);

        tdbg ("Entering: %s\n", job->dirname);

        dirp = sys_opendir (job->dirname);
        if (!dirp) {
                terr ("opendir failed on %s (%s)\n", job->dirname,
                     strerror (errno));
                goto out;
        }

        boff = sprintf (path, "%s/", job->dirname);

        for (;;) {
                errno = 0;
                entry = sys_readdir (dirp, scratch);
                if (!entry || errno != 0) {
                        if (errno != 0) {
                                err ("readdir(%s): %s\n", job->dirname,
                                     strerror (errno));
                                ret = errno;
                                goto out;
                        }
                        break;
                }

                if (entry->d_ino == 0)
                        continue;

                if (skip_name (job->dirname, entry->d_name))
                        continue;

                /* It is sure that, children and grandchildren of .glusterfs
                 * are directories, just add them to global queue.
                 */
                if (skip_stat (job, entry->d_name)) {
                        strncpy (path + boff, entry->d_name, (plen-boff));
                        cjob = dirjob_new (path, job);
                        if (!cjob) {
                                err ("dirjob_new(%s): %s\n",
                                     path, strerror (errno));
                                ret = -1;
                                goto out;
                        }
                        xwork_addcrawl (xwork, cjob);
                        continue;
                }

                (void) snprintf (gfid_path, sizeof(gfid_path), "%s/.gfid/%s",
                                 slavemnt, entry->d_name);
                ret = sys_lstat (gfid_path, &statbuf);

                if (ret && errno == ENOENT) {
                        out ("%s\n", entry->d_name);
                        BUMP (skipped_gfids);
                }

                if (ret && errno != ENOENT) {
                        err ("stat on slave failed(%s): %s\n",
                             gfid_path, strerror (errno));
                        goto out;
                }
        }

        ret = 0;
out:
        if (dirp)
                (void) sys_closedir (dirp);

        return ret;
}


void *
xworker_crawl (void *data)
{
        struct xwork *xwork = data;
        struct dirjob *job = NULL;
        int            ret = -1;

        while ((job = xwork_pick (xwork, 0))) {
                ret = xworker_do_crawl (xwork, job);
                dirjob_ret (job, ret);
        }

        return NULL;
}

int
xwork_fini (struct xwork *xwork, int stop)
{
        int i = 0;
        int ret = 0;
        void *tret = 0;

        pthread_mutex_lock (&xwork->mutex);
        {
                xwork->stop = (xwork->stop || stop);
                pthread_cond_broadcast (&xwork->cond);
        }
        pthread_mutex_unlock (&xwork->mutex);

        for (i = 0; i < xwork->count; i++) {
                pthread_join (xwork->cthreads[i], &tret);
                tdbg ("CThread id %ld returned %p\n",
                      xwork->cthreads[i], tret);
        }

        if (debug) {
                assert (xwork->rootjob->refcnt == 1);
                dirjob_ret (xwork->rootjob, 0);
        }

        if (stats)
                pthread_spin_destroy(&stats_lock);

        return ret;
}


int
xwork_init (struct xwork *xwork, int count)
{
        int  i = 0;
        int  ret = 0;
        struct dirjob *rootjob = NULL;

        if (stats)
                pthread_spin_init (&stats_lock, PTHREAD_PROCESS_PRIVATE);

        pthread_mutex_init (&xwork->mutex, NULL);
        pthread_cond_init (&xwork->cond, NULL);

        INIT_LIST_HEAD (&xwork->crawl.list);

        rootjob = dirjob_new (".glusterfs", NULL);
        if (debug)
                xwork->rootjob = dirjob_ref (rootjob);

        xwork_addcrawl (xwork, rootjob);

        xwork->count = count;
        for (i = 0; i < count; i++) {
                ret = pthread_create (&xwork->cthreads[i], NULL,
                                      xworker_crawl, xwork);
                if (ret)
                        break;
                tdbg ("Spawned crawler %d thread %ld\n", i,
                      xwork->cthreads[i]);
        }

        return ret;
}


int
xfind (const char *basedir)
{
        struct xwork xwork;
        int          ret = 0;
        char         *cwd = NULL;

        ret = chdir (basedir);
        if (ret) {
                err ("%s: %s\n", basedir, strerror (errno));
                return ret;
        }

        cwd = getcwd (0, 0);
        if (!cwd) {
                err ("getcwd(): %s\n", strerror (errno));
                return -1;
        }

        tdbg ("Working directory: %s\n", cwd);
        free (cwd);

        memset (&xwork, 0, sizeof (xwork));

        ret = xwork_init (&xwork, workers);
        if (ret == 0)
                xworker_crawl (&xwork);

        ret = xwork_fini (&xwork, ret);
        stats_dump ();

        return ret;
}

static char *
parse_and_validate_args (int argc, char *argv[])
{
        char        *basedir = NULL;
        struct stat  d = {0, };
        int          ret = -1;
#ifndef __FreeBSD__
        unsigned char volume_id[16];
#endif /* __FreeBSD__ */
        char        *slv_mnt = NULL;

        if (argc != 4) {
                err ("Usage: %s <DIR> <SLAVE-VOL-MOUNT> <CRAWL-THREAD-COUNT>\n",
                      argv[0]);
                return NULL;
        }

        basedir = argv[1];
        ret = sys_lstat (basedir, &d);
        if (ret) {
                err ("%s: %s\n", basedir, strerror (errno));
                return NULL;
        }

#ifndef __FreeBSD__
        ret = sys_lgetxattr (basedir, "trusted.glusterfs.volume-id",
                             volume_id, 16);
        if (ret != 16) {
                err ("%s:Not a valid brick path.\n", basedir);
                return NULL;
        }
#endif /* __FreeBSD__ */

        slv_mnt = argv[2];
        ret = sys_lstat (slv_mnt, &d);
        if (ret) {
                err ("%s: %s\n", slv_mnt, strerror (errno));
                return NULL;
        }
        slavemnt = argv[2];

        workers = atoi(argv[3]);
        if (workers <= 0)
                workers = DEFAULT_WORKERS;

        return basedir;
}

int
main (int argc, char *argv[])
{
        char *basedir = NULL;

        basedir = parse_and_validate_args (argc, argv);
        if (!basedir)
                return 1;

        xfind (basedir);

        return 0;
}
