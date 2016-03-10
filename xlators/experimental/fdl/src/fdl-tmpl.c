/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include "call-stub.h"
#include "iatt.h"
#include "defaults.h"
#include "syscall.h"
#include "xlator.h"
#include "jnl-types.h"

/* TBD: make tunable */
#define META_FILE_SIZE  (1 << 20)
#define DATA_FILE_SIZE  (1 << 24)

enum gf_fdl {
        gf_fdl_mt_fdl_private_t = gf_common_mt_end + 1,
        gf_fdl_mt_end
};

typedef struct {
        char            *type;
        off_t           size;
        char            *path;
        int             fd;
        void *          ptr;
        off_t           max_offset;
} log_obj_t;

typedef struct {
        struct list_head        reqs;
        pthread_mutex_t         req_lock;
        pthread_cond_t          req_cond;
        char                    *log_dir;
        pthread_t               worker;
        gf_boolean_t            should_stop;
        gf_boolean_t            change_term;
        log_obj_t               meta_log;
        log_obj_t               data_log;
        int                     term;
        int                     first_term;
} fdl_private_t;

void
fdl_enqueue (xlator_t *this, call_stub_t *stub)
{
        fdl_private_t   *priv   = this->private;

        pthread_mutex_lock (&priv->req_lock);
        list_add_tail (&stub->list, &priv->reqs);
        pthread_mutex_unlock (&priv->req_lock);

        pthread_cond_signal (&priv->req_cond);
}

#pragma generate

char *
fdl_open_term_log (xlator_t *this, log_obj_t *obj, int term)
{
        fdl_private_t   *priv   = this->private;
        int             ret;
        char *          ptr     = NULL;

        /*
         * Use .jnl instead of .log so that we don't get test info (mistakenly)
         * appended to our journal files.
         */
        if (this->ctx->cmd_args.log_ident) {
                ret = gf_asprintf (&obj->path, "%s/%s-%s-%d.jnl",
                                   priv->log_dir, this->ctx->cmd_args.log_ident,
                                   obj->type, term);
        }
        else {
                ret = gf_asprintf (&obj->path, "%s/fubar-%s-%d.jnl",
                                   priv->log_dir, obj->type, term);
        }
        if ((ret <= 0) || !obj->path) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to construct log-file path");
                goto err;
        }

        gf_log (this->name, GF_LOG_INFO, "opening %s (size %ld)",
                obj->path, obj->size);

        obj->fd = open (obj->path, O_RDWR|O_CREAT|O_TRUNC, 0666);
        if (obj->fd < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to open log file (%s)", strerror(errno));
                goto err;
        }

#if !defined(GF_BSD_HOST_OS)
        /*
         * NetBSD can just go die in a fire.  Even though it claims to support
         * fallocate/posix_fallocate they don't actually *do* anything so the
         * file size remains zero.  Then mmap succeeds anyway, but any access
         * to the mmap'ed region will segfault.  It would be acceptable for
         * fallocate to do what it says, for mmap to fail, or for access to
         * extend the file.  NetBSD managed to hit the trifecta of Getting
         * Everything Wrong, and debugging in that environment to get this far
         * has already been painful enough (systems I worked on in 1990 were
         * better that way).  We'll fall through to the lseek/write method, and
         * performance will be worse, and TOO BAD.
         */
        if (sys_fallocate(obj->fd,0,0,obj->size) < 0)
#endif
        {
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to fallocate space for log file");
                /* Have to do this the ugly page-faulty way. */
                (void) sys_lseek (obj->fd, obj->size-1, SEEK_SET);
                (void) sys_write (obj->fd, "", 1);
        }

        ptr = mmap (NULL, obj->size, PROT_WRITE, MAP_SHARED, obj->fd, 0);
        if (ptr == MAP_FAILED) {
                gf_log (this->name, GF_LOG_ERROR, "failed to mmap log (%s)",
                        strerror(errno));
                goto err;
        }

        obj->ptr = ptr;
        obj->max_offset = 0;
        return ptr;

err:
        if (obj->fd >= 0) {
                sys_close (obj->fd);
                obj->fd = (-1);
        }
        if (obj->path) {
                GF_FREE (obj->path);
                obj->path = NULL;
        }
        return ptr;
}

void
fdl_close_term_log (xlator_t *this, log_obj_t *obj)
{
        fdl_private_t   *priv           = this->private;

        if (obj->ptr) {
                (void) munmap (obj->ptr, obj->size);
                obj->ptr = NULL;
        }

        if (obj->fd >= 0) {
                gf_log (this->name, GF_LOG_INFO,
                        "truncating term %d %s journal to %ld",
                        priv->term, obj->type, obj->max_offset);
                if (sys_ftruncate(obj->fd,obj->max_offset) < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "failed to truncate journal (%s)",
                                strerror(errno));
                }
                sys_close (obj->fd);
                obj->fd = (-1);
        }

        if (obj->path) {
                GF_FREE (obj->path);
                obj->path = NULL;
        }
}

gf_boolean_t
fdl_change_term (xlator_t *this, char **meta_ptr, char **data_ptr)
{
        fdl_private_t   *priv           = this->private;

        fdl_close_term_log (this, &priv->meta_log);
        fdl_close_term_log (this, &priv->data_log);

        ++(priv->term);

        *meta_ptr = fdl_open_term_log (this, &priv->meta_log, priv->term);
        if (!*meta_ptr) {
                return _gf_false;
        }

        *data_ptr = fdl_open_term_log (this, &priv->data_log, priv->term);
        if (!*data_ptr) {
                return _gf_false;
        }

        return _gf_true;
}

void *
fdl_worker (void *arg)
{
        xlator_t        *this           = arg;
        fdl_private_t   *priv           = this->private;
        call_stub_t     *stub;
        char *          meta_ptr        = NULL;
        off_t           *meta_offset    = &priv->meta_log.max_offset;
        char *          data_ptr        = NULL;
        off_t           *data_offset    = &priv->data_log.max_offset;
        unsigned long   base_as_ul;
        void *          msync_ptr;
        size_t          msync_len;
        gf_boolean_t    recycle;
        void            *err_label      = &&err_unlocked;

        priv->meta_log.type = "meta";
        priv->meta_log.size = META_FILE_SIZE;
        priv->meta_log.path = NULL;
        priv->meta_log.fd = (-1);
        priv->meta_log.ptr = NULL;

        priv->data_log.type = "data";
        priv->data_log.size = DATA_FILE_SIZE;
        priv->data_log.path = NULL;
        priv->data_log.fd = (-1);
        priv->data_log.ptr = NULL;

        /* TBD: initial term should come from persistent storage (e.g. etcd) */
        priv->first_term = ++(priv->term);
        meta_ptr = fdl_open_term_log (this, &priv->meta_log, priv->term);
        if (!meta_ptr) {
                goto *err_label;
        }
        data_ptr = fdl_open_term_log (this, &priv->data_log, priv->term);
        if (!data_ptr) {
                fdl_close_term_log (this, &priv->meta_log);
                goto *err_label;
        }

        for (;;) {
                pthread_mutex_lock (&priv->req_lock);
                err_label = &&err_locked;
                while (list_empty(&priv->reqs)) {
                        pthread_cond_wait (&priv->req_cond, &priv->req_lock);
                        if (priv->should_stop) {
                                goto *err_label;
                        }
                        if (priv->change_term) {
                                if (!fdl_change_term(this, &meta_ptr,
                                                           &data_ptr)) {
                                        goto *err_label;
                                }
                                priv->change_term = _gf_false;
                                continue;
                        }
                }
                stub = list_entry (priv->reqs.next, call_stub_t, list);
                list_del_init (&stub->list);
                pthread_mutex_unlock (&priv->req_lock);
                err_label = &&err_unlocked;
                /*
                 * TBD: batch requests
                 *
                 * What we should do here is gather up *all* of the requests
                 * that have accumulated since we were last at this point,
                 * blast them all out in one big writev, and then dispatch them
                 * all before coming back for more.  That maximizes throughput,
                 * at some cost to latency (due to queuing effects at the log
                 * stage).  Note that we're likely to be above io-threads, so
                 * the dispatch itself will be parallelized (at further cost to
                 * latency).  For now, we just do the simplest thing and handle
                 * one request all the way through before fetching the next.
                 *
                 * So, why mmap/msync instead of writev/fdatasync?  Because it's
                 * faster.  Much faster.  So much faster that I half-suspect
                 * cheating, but it's more convenient for now than having to
                 * ensure that everything's page-aligned for O_DIRECT (the only
                 * alternative that still might avoid ridiculous levels of
                 * local-FS overhead).
                 *
                 * TBD: check that msync really does get our data to disk.
                 */
                gf_log (this->name, GF_LOG_DEBUG,
                        "logging %u+%u bytes for op %d",
                        stub->jnl_meta_len, stub->jnl_data_len, stub->fop);
                recycle = _gf_false;
                if ((*meta_offset + stub->jnl_meta_len) > priv->meta_log.size) {
                        recycle = _gf_true;
                }
                if ((*data_offset + stub->jnl_data_len) > priv->data_log.size) {
                        recycle = _gf_true;
                }
                if (recycle && !fdl_change_term(this,&meta_ptr,&data_ptr)) {
                        goto *err_label;
                }
                meta_ptr = priv->meta_log.ptr;
                data_ptr = priv->data_log.ptr;
                gf_log (this->name, GF_LOG_DEBUG, "serializing to %p/%p",
                        meta_ptr + *meta_offset, data_ptr + *data_offset);
                stub->serialize (stub, meta_ptr + *meta_offset,
                                       data_ptr + *data_offset);
                if (stub->jnl_meta_len > 0) {
                        base_as_ul = (unsigned long) (meta_ptr + *meta_offset);
                        msync_ptr = (void *) (base_as_ul & ~0x0fff);
                        msync_len = (size_t) (base_as_ul &  0x0fff);
                        if (msync (msync_ptr, msync_len+stub->jnl_meta_len,
                                              MS_SYNC) < 0) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "failed to log request meta (%s)",
                                        strerror(errno));
                        }
                        *meta_offset += stub->jnl_meta_len;
                }
                if (stub->jnl_data_len > 0) {
                        base_as_ul = (unsigned long) (data_ptr + *data_offset);
                        msync_ptr = (void *) (base_as_ul & ~0x0fff);
                        msync_len = (size_t) (base_as_ul &  0x0fff);
                        if (msync (msync_ptr, msync_len+stub->jnl_data_len,
                                              MS_SYNC) < 0) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "failed to log request data (%s)",
                                        strerror(errno));
                        }
                        *data_offset += stub->jnl_data_len;
                }
                call_resume (stub);
        }

err_locked:
        pthread_mutex_unlock (&priv->req_lock);
err_unlocked:
        fdl_close_term_log (this, &priv->meta_log);
        fdl_close_term_log (this, &priv->data_log);
        return NULL;
}

int32_t
fdl_ipc (call_frame_t *frame, xlator_t *this, int32_t op, dict_t *xdata)
{
        fdl_private_t   *priv   = this->private;
        dict_t          *tdict;
        int32_t         gt_err  = EIO;

        switch (op) {

        case FDL_IPC_CHANGE_TERM:
                gf_log (this->name, GF_LOG_INFO, "got CHANGE_TERM op");
                priv->change_term = _gf_true;
                pthread_cond_signal (&priv->req_cond);
                STACK_UNWIND_STRICT (ipc, frame, 0, 0, NULL);
                break;

        case FDL_IPC_GET_TERMS:
                gf_log (this->name, GF_LOG_INFO, "got GET_TERMS op");
                tdict = dict_new ();
                if (!tdict) {
                        gt_err = ENOMEM;
                        goto gt_done;
                }
                if (dict_set_int32(tdict,"first",priv->first_term) != 0) {
                        goto gt_done;
                }
                if (dict_set_int32(tdict,"last",priv->term) != 0) {
                        goto gt_done;
                }
                gt_err = 0;
        gt_done:
                if (gt_err) {
                        STACK_UNWIND_STRICT (ipc, frame, -1, gt_err, NULL);
                } else {
                        STACK_UNWIND_STRICT (ipc, frame, 0, 0, tdict);
                }
                if (tdict) {
                        dict_unref (tdict);
                }
                break;

        default:
                STACK_WIND_TAIL (frame,
                                 FIRST_CHILD(this),
                                 FIRST_CHILD(this)->fops->ipc,
                                 op, xdata);
        }

        return 0;
}

int
fdl_init (xlator_t *this)
{
        fdl_private_t   *priv   = NULL;

        priv = GF_CALLOC (1, sizeof (*priv), gf_fdl_mt_fdl_private_t);
        if (!priv) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to allocate fdl_private");
                goto err;
        }

        INIT_LIST_HEAD (&priv->reqs);
        if (pthread_mutex_init (&priv->req_lock, NULL) != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to initialize req_lock");
                goto err;
        }
        if (pthread_cond_init (&priv->req_cond, NULL) != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to initialize req_cond");
                goto err;
        }

        GF_OPTION_INIT ("log-path", priv->log_dir, path, err);

        this->private = priv;
        /*
         * The rest of the fop table is automatically generated, so this is a
         * bit cleaner than messing with the generation to add a hand-written
         * exception.
         */
        this->fops->ipc = fdl_ipc;

        if (pthread_create(&priv->worker,NULL,fdl_worker,this) != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to start fdl_worker");
                goto err;
        }

        return 0;

err:
        if (priv) {
                GF_FREE(priv);
        }
        return -1;
}

void
fdl_fini (xlator_t *this)
{
        fdl_private_t   *priv   = this->private;

        if (priv) {
                priv->should_stop = _gf_true;
                pthread_cond_signal (&priv->req_cond);
                pthread_join (priv->worker, NULL);
                GF_FREE(priv);
        }
}

int
fdl_reconfigure (xlator_t *this, dict_t *options)
{
        fdl_private_t   *priv   = this->private;

	GF_OPTION_RECONF ("log_dir", priv->log_dir, options, path, out);
        /* TBD: react if it changed */

out:
        return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        GF_VALIDATE_OR_GOTO ("fdl", this, out);

        ret = xlator_mem_acct_init (this, gf_fdl_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
                        "failed");
                return ret;
        }
out:
        return ret;
}

class_methods_t class_methods = {
        .init           = fdl_init,
        .fini           = fdl_fini,
        .reconfigure    = fdl_reconfigure,
        .notify         = default_notify,
};

struct volume_options options[] = {
        { .key = {"log-path"},
          .type = GF_OPTION_TYPE_PATH,
          .default_value = DEFAULT_LOG_FILE_DIRECTORY,
          .description = "Directory for FDL files."
        },
        { .key  = {NULL} },
};

struct xlator_cbks cbks = {
        .release        = default_release,
        .releasedir     = default_releasedir,
        .forget         = default_forget,
};
