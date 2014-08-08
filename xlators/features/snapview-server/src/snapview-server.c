/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
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

#include "snapview-server.h"
#include "snapview-server-mem-types.h"

#include "xlator.h"
#include "rpc-clnt.h"
#include "xdr-generic.h"
#include "protocol-common.h"
#include "syscall.h"
#include <pthread.h>

static pthread_mutex_t  mutex   = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   condvar = PTHREAD_COND_INITIALIZER;
static gf_boolean_t     snap_worker_resume;

void
snaplist_refresh (void *data)
{
        xlator_t        *this   = NULL;
        int             ret     = 0;
        svs_private_t   *priv   = NULL;

        this = data;
        priv = this->private;

        ret = svs_get_snapshot_list (this);
        if (ret) {
                gf_log ("snapview-server", GF_LOG_WARNING,
                        "Error retrieving refreshed snapshot list");
        }

        return;
}

void *
snaplist_worker (void *data)
{
        xlator_t        *this   = NULL;
        int             ret     = 0;
        struct timespec timeout = {0, };
        svs_private_t   *priv   = NULL;
        glusterfs_ctx_t *ctx    = NULL;

        this = data;
        priv = this->private;
        ctx = this->ctx;
        GF_ASSERT (ctx);

        ret = pthread_mutex_lock (&priv->snaplist_lock);
        if (ret != 0) {
                goto out;
        }

        priv->is_snaplist_done = 1;

        ret = pthread_mutex_unlock (&priv->snaplist_lock);
        if (ret != 0) {
                goto out;
        }

        while (1) {
                timeout.tv_sec = 300;
                timeout.tv_nsec = 0;
                priv->snap_timer = gf_timer_call_after (ctx, timeout,
                                                        snaplist_refresh,
                                                        data);
                ret = pthread_mutex_lock (&mutex);
                if (ret != 0) {
                        goto out;
                }
                        /*
                         * We typically expect this mutex lock to succeed
                         * A corner case might be when snaplist_worker is
                         * scheduled and it tries to acquire this lock
                         * but we are in the middle of xlator _fini()
                         * when the mutex is itself being destroyed.
                         * To prevent any undefined behavior or segfault
                         * at that point, we check the ret here.
                         * If mutex is destroyed we expect a EINVAL for a
                         * mutex which is not initialized properly.
                         * Bail then.
                         * Same for the unlock case.
                         */
                while (!snap_worker_resume) {
                        pthread_cond_wait (&condvar, &mutex);
                }

                snap_worker_resume = _gf_false;

                ret = pthread_mutex_unlock (&mutex);
                if (ret != 0) {
                        goto out;
                }
        }

out:
        return NULL;
}

int
svs_mgmt_submit_request (void *req, call_frame_t *frame,
                         glusterfs_ctx_t *ctx,
                         rpc_clnt_prog_t *prog, int procnum,
                         fop_cbk_fn_t cbkfn, xdrproc_t xdrproc)
{
        int                     ret        = -1;
        int                     count      = 0;
        struct iovec            iov        = {0, };
        struct iobuf            *iobuf     = NULL;
        struct iobref           *iobref    = NULL;
        ssize_t                 xdr_size   = 0;

        GF_VALIDATE_OR_GOTO ("snapview-server", frame, out);
        GF_VALIDATE_OR_GOTO ("snapview-server", req, out);
        GF_VALIDATE_OR_GOTO ("snapview-server", ctx, out);
        GF_VALIDATE_OR_GOTO ("snapview-server", prog, out);

        GF_ASSERT (frame->this);

        iobref = iobref_new ();
        if (!iobref) {
                goto out;
        }

        if (req) {
                xdr_size = xdr_sizeof (xdrproc, req);

                iobuf = iobuf_get2 (ctx->iobuf_pool, xdr_size);
                if (!iobuf) {
                        goto out;
                }

                iobref_add (iobref, iobuf);

                iov.iov_base = iobuf->ptr;
                iov.iov_len  = iobuf_pagesize (iobuf);

                /* Create the xdr payload */
                ret = xdr_serialize_generic (iov, req, xdrproc);
                if (ret == -1) {
                        gf_log (frame->this->name, GF_LOG_WARNING,
                                "Failed to create XDR payload");
                        goto out;
                }
                iov.iov_len = ret;
                count = 1;
        }

        ret = rpc_clnt_submit (ctx->mgmt, prog, procnum, cbkfn,
                               &iov, count,
                               NULL, 0, iobref, frame, NULL, 0, NULL, 0, NULL);

out:
        if (iobref)
                iobref_unref (iobref);

        if (iobuf)
                iobuf_unref (iobuf);
        return ret;
}


int mgmt_get_snapinfo_cbk (struct rpc_req *req, struct iovec *iov,
                           int count, void *myframe)
{
        gf_getsnap_name_uuid_rsp        rsp             = {0,};
        call_frame_t                    *frame          = NULL;
        glusterfs_ctx_t                 *ctx            = NULL;
        int                             ret             = 0;
        dict_t                          *dict           = NULL;
        char                            key[1024]       = {0};
        int                             snapcount       = 0;
        svs_private_t                   *priv           = NULL;
        xlator_t                        *this           = NULL;
        int                             i               = 0;
        int                             j               = 0;
        char                            *value          = NULL;
        snap_dirent_t                   *dirents        = NULL;
        snap_dirent_t                   *old_dirents    = NULL;

        GF_VALIDATE_OR_GOTO ("snapview-server", req, error_out);
        GF_VALIDATE_OR_GOTO ("snapview-server", myframe, error_out);
        GF_VALIDATE_OR_GOTO ("snapview-server", iov, error_out);

        frame       = myframe;
        this        = frame->this;
        ctx         = frame->this->ctx;
        priv        = this->private;
        old_dirents = priv->dirents;

        if (!ctx) {
                gf_log (frame->this->name, GF_LOG_ERROR, "NULL context");
                errno = EINVAL;
                ret = -1;
                goto out;
        }

        if (-1 == req->rpc_status) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "RPC call is not successful");
                errno = EINVAL;
                ret = -1;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gf_getsnap_name_uuid_rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response, rsp.op_ret = %d",
                        rsp.op_ret);
                goto out;
        }

        if (rsp.op_ret == -1) {
                errno = rsp.op_errno;
                ret = -1;
                goto out;
        }

        if (!rsp.dict.dict_len) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Response dict is not populated");
                ret = -1;
                errno = EINVAL;
                goto out;
        }

        dict = dict_new ();
        if (!dict) {
                ret = -1;
                errno = ENOMEM;
                goto out;
        }

        ret = dict_unserialize (rsp.dict.dict_val, rsp.dict.dict_len, &dict);
        if (ret) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Failed to unserialize dictionary");
                errno = EINVAL;
                goto out;
        }

        ret = dict_get_int32 (dict, "snap-count", (int32_t*)&snapcount);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Error retrieving snapcount");
                        errno = EINVAL;
                        ret = -1;
                        goto out;
        }

        pthread_mutex_lock (&priv->snaplist_lock);

        if ((priv->num_snaps == 0) &&
            (snapcount != 0)) {
                /* first time we are fetching snap list */
                dirents = GF_CALLOC (snapcount, sizeof (snap_dirent_t),
                                     gf_svs_mt_dirents_t);
                if (!dirents) {
                        gf_log (frame->this->name, GF_LOG_ERROR,
                                "Unable to allocate memory");
                        errno = ENOMEM;
                        ret = -1;
                        goto unlock;
                }
        } else {
                /* fetch snaplist dynamically at run-time */
                dirents = GF_CALLOC (snapcount, sizeof (snap_dirent_t),
                                     gf_svs_mt_dirents_t);
                if (!dirents) {
                        gf_log (frame->this->name, GF_LOG_ERROR,
                                "Unable to allocate memory");
                                errno = ENOMEM;
                                ret = -1;
                                goto unlock;
                }
        }

        for (i = 0; i < snapcount; i++) {
                snprintf (key, sizeof (key), "snap-volname.%d", i+1);
                ret = dict_get_str (dict, key, &value);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Error retrieving snap volname %d", i+1);
                        errno = EINVAL;
                        ret = -1;
                        goto unlock;
                }
                strncpy (dirents[i].snap_volname, value,
                         sizeof (dirents[i].snap_volname));

                snprintf (key, sizeof (key), "snap-id.%d", i+1);
                ret = dict_get_str (dict, key, &value);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Error retrieving snap uuid %d", i+1);
                        errno = EINVAL;
                        ret = -1;
                        goto unlock;
                }
                strncpy (dirents[i].uuid, value, sizeof (dirents[i].uuid));

                snprintf (key, sizeof (key), "snapname.%d", i+1);
                ret = dict_get_str (dict, key, &value);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Error retrieving snap name %d", i+1);
                        errno = EINVAL;
                        ret = -1;
                        goto unlock;
                }
                strncpy (dirents[i].name, value, sizeof (dirents[i].name));
        }

        /*
         * Got the new snap list populated in dirents
         * The new snap list is either a subset or a superset of
         * the existing snaplist old_dirents which has priv->num_snaps
         * number of entries.
         *
         * If subset, then clean up the fs for entries which are
         * no longer relevant.
         *
         * For other overlapping entries set the fs for new dirents
         * entries which have a fs assigned already in old_dirents
         *
         * We do this as we don't want to do new glfs_init()s repeatedly
         * as the dirents entries for snapshot volumes get repatedly
         * cleaned up and allocated. And if we don't then that will lead
         * to memleaks
         */
        for (i = 0; i < priv->num_snaps; i++) {
                for (j = 0; j < snapcount; j++) {
                        if ((!strcmp (old_dirents[i].name,
                                      dirents[j].name)) &&
                            (!strcmp (old_dirents[i].uuid,
                                      dirents[j].uuid)))    {
                                dirents[j].fs = old_dirents[i].fs;
                                old_dirents[i].fs = NULL;
                                break;
                        }
                }
        }

        if (old_dirents) {
                for (i=0; i < priv->num_snaps; i++) {
                        if (old_dirents[i].fs)
                                glfs_fini (old_dirents[i].fs);
                }
        }

        priv->dirents = dirents;
        priv->num_snaps = snapcount;

        GF_FREE (old_dirents);

        ret = 0;

unlock:
        /*
         *
         * We will unlock the snaplist_lock here for two reasons:
         * 1. We ideally would like to avoid nested locks
         * 2. The snaplist_lock and the mutex protecting the condvar
         *    are independent of each other and don't need to be
         *    mixed together
         */
        pthread_mutex_unlock (&priv->snaplist_lock);

out:
        pthread_mutex_lock (&mutex);
        snap_worker_resume = _gf_true;
        if (priv->is_snaplist_done) {
                /*
                 * No need to signal if it is the first time
                 * refresh of the snaplist as no thread is
                 * waiting on this. It is only when the snaplist_worker
                 * is started that we have a thread waiting on this
                 */
                pthread_cond_signal (&condvar);
        }
        pthread_mutex_unlock (&mutex);

        if (dict) {
                dict_unref (dict);
        }
        free (rsp.dict.dict_val);
        free (rsp.op_errstr);

        if (ret && dirents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Could not update dirents with refreshed snap list");
                GF_FREE (dirents);
        }

        if (myframe)
                SVS_STACK_DESTROY (myframe);

error_out:
        return ret;
}

int
svs_get_snapshot_list (xlator_t *this)
{
        gf_getsnap_name_uuid_req        req             = {{0,}};
        int                             ret             = 0;
        dict_t                          *dict           = NULL;
        glusterfs_ctx_t                 *ctx            = NULL;
        call_frame_t                    *frame          = NULL;
        svs_private_t                   *priv           = NULL;
        gf_boolean_t                    frame_cleanup   = _gf_false;

        ctx  = this->ctx;
        if (!ctx) {
                gf_log (this->name, GF_LOG_ERROR,
                        "ctx is NULL");
                ret = -1;
                goto out;
        }

        frame = create_frame (this, ctx->pool);
        if (!frame) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Error allocating frame");
                ret = -1;
                goto out;
        }

        priv = this->private;

        dict = dict_new ();
        if (!dict) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR,
                        "Error allocating dictionary");
                frame_cleanup = _gf_true;
                goto out;
        }

        ret = dict_set_str (dict, "volname", priv->volname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Error setting volname in dict");
                frame_cleanup = _gf_true;
                goto out;
        }

        ret = dict_allocate_and_serialize (dict, &req.dict.dict_val,
                                           &req.dict.dict_len);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to serialize dictionary");
                ret = -1;
                frame_cleanup = _gf_true;
                goto out;
        }

        ret = svs_mgmt_submit_request (&req, frame, ctx,
                                       &svs_clnt_handshake_prog,
                                       GF_HNDSK_GET_SNAPSHOT_INFO,
                                       mgmt_get_snapinfo_cbk,
                                       (xdrproc_t)xdr_gf_getsnap_name_uuid_req);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Error sending snapshot names RPC request");
        }

out:
        if (dict) {
                dict_unref (dict);
        }
        GF_FREE (req.dict.dict_val);

        if (frame_cleanup) {
                /*
                 * Destroy the frame if we encountered an error
                 * Else we need to clean it up in
                 * mgmt_get_snapinfo_cbk
                 */
                SVS_STACK_DESTROY (frame);
        }

        return ret;
}

int
__svs_inode_ctx_set (xlator_t *this, inode_t *inode, svs_inode_t *svs_inode)
{
        uint64_t    value = 0;
        int         ret   = -1;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);
        GF_VALIDATE_OR_GOTO (this->name, svs_inode, out);

        value = (uint64_t)(long) svs_inode;

        ret = __inode_ctx_set (inode, this, &value);

out:
        return ret;
}


svs_inode_t *
__svs_inode_ctx_get (xlator_t *this, inode_t *inode)
{
        svs_inode_t *svs_inode = NULL;
        uint64_t     value     = 0;
        int          ret       = -1;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        ret = __inode_ctx_get (inode, this, &value);
        if (ret)
                goto out;

        svs_inode = (svs_inode_t *) ((long) value);

out:
        return svs_inode;
}


svs_inode_t *
svs_inode_ctx_get (xlator_t *this, inode_t *inode)
{
        svs_inode_t *svs_inode = NULL;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        LOCK (&inode->lock);
        {
                svs_inode = __svs_inode_ctx_get (this, inode);
        }
        UNLOCK (&inode->lock);

out:
        return svs_inode;
}

int32_t
svs_inode_ctx_set (xlator_t *this, inode_t *inode, svs_inode_t *svs_inode)
{
        int32_t    ret = -1;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);
        GF_VALIDATE_OR_GOTO (this->name, svs_inode, out);

        LOCK (&inode->lock);
        {
                ret = __svs_inode_ctx_set (this, inode, svs_inode);
        }
        UNLOCK (&inode->lock);

out:
        return ret;
}

svs_inode_t *
svs_inode_new ()
{
        svs_inode_t    *svs_inode = NULL;

        svs_inode = GF_CALLOC (1, sizeof (*svs_inode), gf_svs_mt_svs_inode_t);

        return svs_inode;
}

svs_inode_t *
svs_inode_ctx_get_or_new (xlator_t *this, inode_t *inode)
{
        svs_inode_t   *svs_inode = NULL;
        int            ret       = -1;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        LOCK (&inode->lock);
        {
                svs_inode = __svs_inode_ctx_get (this, inode);
                if (!svs_inode) {
                        svs_inode = svs_inode_new ();
                        if (svs_inode) {
                                ret = __svs_inode_ctx_set (this, inode,
                                                           svs_inode);
                                if (ret) {
                                        GF_FREE (svs_inode);
                                        svs_inode = NULL;
                                }
                        }
                }
        }
        UNLOCK (&inode->lock);

out:
        return svs_inode;
}

svs_fd_t *
svs_fd_new ()
{
        svs_fd_t    *svs_fd = NULL;

        svs_fd = GF_CALLOC (1, sizeof (*svs_fd), gf_svs_mt_svs_fd_t);

        return svs_fd;
}

int
__svs_fd_ctx_set (xlator_t *this, fd_t *fd, svs_fd_t *svs_fd)
{
        uint64_t    value = 0;
        int         ret   = -1;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, svs_fd, out);

        value = (uint64_t)(long) svs_fd;

        ret = __fd_ctx_set (fd, this, value);

out:
        return ret;
}


svs_fd_t *
__svs_fd_ctx_get (xlator_t *this, fd_t *fd)
{
        svs_fd_t *svs_fd = NULL;
        uint64_t  value  = 0;
        int       ret    = -1;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        ret = __fd_ctx_get (fd, this, &value);
        if (ret)
                return NULL;

        svs_fd = (svs_fd_t *) ((long) value);

out:
        return svs_fd;
}


svs_fd_t *
svs_fd_ctx_get (xlator_t *this, fd_t *fd)
{
        svs_fd_t *svs_fd = NULL;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        LOCK (&fd->lock);
        {
                svs_fd = __svs_fd_ctx_get (this, fd);
        }
        UNLOCK (&fd->lock);

out:
        return svs_fd;
}

int32_t
svs_fd_ctx_set (xlator_t *this, fd_t *fd, svs_fd_t *svs_fd)
{
        int32_t    ret = -1;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, svs_fd, out);

        LOCK (&fd->lock);
        {
                ret = __svs_fd_ctx_set (this, fd, svs_fd);
        }
        UNLOCK (&fd->lock);

out:
        return ret;
}

svs_fd_t *
__svs_fd_ctx_get_or_new (xlator_t *this, fd_t *fd)
{
        svs_fd_t        *svs_fd    = NULL;
        int              ret       = -1;
        glfs_t          *fs        = NULL;
        glfs_object_t   *object    = NULL;
        svs_inode_t     *inode_ctx = NULL;
        glfs_fd_t       *glfd      = NULL;
        inode_t         *inode     = NULL;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        inode = fd->inode;
        svs_fd = __svs_fd_ctx_get (this, fd);
        if (svs_fd) {
                ret = 0;
                goto out;
        }

        svs_fd = svs_fd_new ();
        if (!svs_fd) {
                gf_log (this->name, GF_LOG_ERROR, "failed to allocate new fd "
                        "context for gfid %s", uuid_utoa (inode->gfid));
                goto out;
        }

        if (fd_is_anonymous (fd)) {
                inode_ctx = svs_inode_ctx_get (this, inode);
                if (!inode_ctx) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to get inode "
                                "context for %s", uuid_utoa (inode->gfid));
                        goto out;
                }

                fs = inode_ctx->fs;
                object = inode_ctx->object;

                if (inode->ia_type == IA_IFDIR) {
                        glfd = glfs_h_opendir (fs, object);
                        if (!glfd) {
                                gf_log (this->name, GF_LOG_ERROR, "failed to "
                                        "open the directory %s",
                                        uuid_utoa (inode->gfid));
                                goto out;
                        }
                }

                if (inode->ia_type == IA_IFREG) {
                        glfd = glfs_h_open (fs, object, O_RDONLY|O_LARGEFILE);
                        if (!glfd) {
                                gf_log (this->name, GF_LOG_ERROR, "failed to "
                                        "open the file %s",
                                        uuid_utoa (inode->gfid));
                                goto out;
                        }
                }

                svs_fd->fd = glfd;
        }

        ret = __svs_fd_ctx_set (this, fd, svs_fd);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to set fd context "
                        "for gfid %s", uuid_utoa (inode->gfid));
                if (svs_fd->fd) {
                        if (inode->ia_type == IA_IFDIR) {
                                ret = glfs_closedir (svs_fd->fd);
                                if (ret)
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "failed to close the fd for %s",
                                                uuid_utoa (inode->gfid));
                        }
                        if (inode->ia_type == IA_IFREG) {
                                ret = glfs_close (svs_fd->fd);
                                if (ret)
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "failed to close the fd for %s",
                                                uuid_utoa (inode->gfid));
                        }
                }
                ret = -1;
        }

out:
        if (ret) {
                GF_FREE (svs_fd);
                svs_fd = NULL;
        }

        return svs_fd;
}

svs_fd_t *
svs_fd_ctx_get_or_new (xlator_t *this, fd_t *fd)
{
        svs_fd_t  *svs_fd = NULL;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        LOCK (&fd->lock);
        {
                svs_fd = __svs_fd_ctx_get_or_new (this, fd);
        }
        UNLOCK (&fd->lock);

out:
        return svs_fd;
}

void
svs_fill_ino_from_gfid (struct iatt *buf)
{
        uint64_t  temp_ino = 0;
        int       j        = 0;
        int       i        = 0;
        xlator_t *this     = NULL;

        this = THIS;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, buf, out);

        /* consider least significant 8 bytes of value out of gfid */
        if (uuid_is_null (buf->ia_gfid)) {
                buf->ia_ino = -1;
                goto out;
        }
        for (i = 15; i > (15 - 8); i--) {
                temp_ino += (uint64_t)(buf->ia_gfid[i]) << j;
                j += 8;
        }
        buf->ia_ino = temp_ino;
out:
        return;
}

void
svs_iatt_fill (uuid_t gfid, struct iatt *buf)
{
        struct timeval  tv    = {0, };
        xlator_t       *this  = NULL;

        this = THIS;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, buf, out);

        buf->ia_type = IA_IFDIR;
        buf->ia_uid  = 0;
        buf->ia_gid  = 0;
        buf->ia_size = 0;
        buf->ia_nlink = 2;
        buf->ia_blocks = 8;
        buf->ia_size = 4096;

        uuid_copy (buf->ia_gfid, gfid);
        svs_fill_ino_from_gfid (buf);

        buf->ia_prot = ia_prot_from_st_mode (0755);

        gettimeofday (&tv, 0);

        buf->ia_mtime = buf->ia_atime = buf->ia_ctime = tv.tv_sec;
        buf->ia_mtime_nsec = buf->ia_atime_nsec = buf->ia_ctime_nsec =
                (tv.tv_usec * 1000);

out:
        return;
}

snap_dirent_t *
svs_get_snap_dirent (xlator_t *this, const char *name)
{
        svs_private_t      *private     = NULL;
        int                 i           = 0;
        snap_dirent_t      *dirents     = NULL;
        snap_dirent_t      *tmp_dirent  = NULL;
        snap_dirent_t      *dirent      = NULL;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);
        GF_VALIDATE_OR_GOTO (this->name, name, out);

        private = this->private;

        pthread_mutex_lock (&private->snaplist_lock);

        dirents = private->dirents;
        if (!dirents) {
                pthread_mutex_unlock (&private->snaplist_lock);
                goto out;
        }

        tmp_dirent = dirents;
        for (i = 0; i < private->num_snaps; i++) {
                if (!strcmp (tmp_dirent->name, name)) {
                        dirent = tmp_dirent;
                        break;
                }
                tmp_dirent++;
        }

        pthread_mutex_unlock (&private->snaplist_lock);

out:
        return dirent;
}

glfs_t *
svs_initialise_snapshot_volume (xlator_t *this, const char *name)
{
        svs_private_t      *priv              = NULL;
        int32_t             ret               = -1;
        snap_dirent_t      *dirent            = NULL;
        char                volname[PATH_MAX] = {0, };
        glfs_t             *fs                = NULL;
        int                 loglevel          = GF_LOG_INFO;
        char                logfile[PATH_MAX] = {0, };

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);
        GF_VALIDATE_OR_GOTO (this->name, name, out);

        priv = this->private;

        dirent = svs_get_snap_dirent (this, name);
        if (!dirent) {
                gf_log (this->name, GF_LOG_ERROR, "snap entry for name %s "
                        "not found", name);
                goto out;
        }

        if (dirent->fs) {
                ret = 0;
                fs = dirent->fs;
                goto out;
        }

        snprintf (volname, sizeof (volname), "/snaps/%s/%s",
                  dirent->name, dirent->snap_volname);

        fs = glfs_new (volname);
        if (!fs) {
                gf_log (this->name, GF_LOG_ERROR,
                        "glfs instance for snap volume %s "
                        "failed", dirent->name);
                goto out;
        }

        ret = glfs_set_volfile_server (fs, "tcp", "localhost",
                                       24007);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "setting the "
                        "volfile srever for snap volume %s "
                        "failed", dirent->name);
                goto out;
        }

        ret = glfs_init (fs);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "initing the "
                        "fs for %s failed", dirent->name);
                goto out;
        }

        snprintf (logfile, sizeof (logfile),
                  DEFAULT_SVD_LOG_FILE_DIRECTORY "/%s-%s.log",
                  name, dirent->uuid);

        ret = glfs_set_logging(fs, logfile, loglevel);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to set the "
                        "log file path");
                goto out;
        }

        ret = 0;

out:
        if (ret && fs) {
                glfs_fini (fs);
                fs = NULL;
        }

        if (fs)
                dirent->fs = fs;

        return fs;
}

snap_dirent_t *
svs_get_latest_snap_entry (xlator_t *this)
{
        svs_private_t *priv       = NULL;
        snap_dirent_t *dirents    = NULL;
        snap_dirent_t *dirent     = NULL;

        GF_VALIDATE_OR_GOTO ("svs", this, out);

        priv = this->private;

        pthread_mutex_lock (&priv->snaplist_lock);
        dirents = priv->dirents;
        if (!dirents) {
                pthread_mutex_unlock (&priv->snaplist_lock);
                goto out;
        }
        if (priv->num_snaps)
                dirent = &dirents[priv->num_snaps - 1];

        pthread_mutex_unlock (&priv->snaplist_lock);
out:
        return dirent;
}

glfs_t *
svs_get_latest_snapshot (xlator_t *this)
{
        glfs_t        *fs         = NULL;
        snap_dirent_t *dirent     = NULL;
        svs_private_t *priv       = NULL;

        GF_VALIDATE_OR_GOTO ("svs", this, out);
        priv = this->private;

        dirent = svs_get_latest_snap_entry (this);

        if (dirent) {
                pthread_mutex_lock (&priv->snaplist_lock);
                fs = dirent->fs;
                pthread_mutex_unlock (&priv->snaplist_lock);
        }

out:
        return fs;
}

int32_t
svs_lookup_entry_point (xlator_t *this, loc_t *loc, inode_t *parent,
                        struct iatt *buf, struct iatt *postparent,
                        int32_t *op_errno)
{
        uuid_t         gfid;
        svs_inode_t   *inode_ctx = NULL;
        int            op_ret    = -1;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);
        GF_VALIDATE_OR_GOTO (this->name, buf, out);
        GF_VALIDATE_OR_GOTO (this->name, postparent, out);

        if (uuid_is_null (loc->inode->gfid)) {
                uuid_generate (gfid);
                svs_iatt_fill (gfid, buf);

                /* Here the inode context of the entry point directory
                   is filled with just the type of the inode and the gfid
                   of the parent from where the entry point was entered.
                   The glfs object and the fs instance will be NULL.
                */
                if (parent)
                        svs_iatt_fill (parent->gfid, postparent);
                else {
                        svs_iatt_fill (buf->ia_gfid, postparent);
                }

                inode_ctx = svs_inode_ctx_get_or_new (this, loc->inode);
                if (!inode_ctx) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to "
                                "allocate inode context for entry point "
                                "directory");
                        op_ret = -1;
                        *op_errno = ENOMEM;
                        goto out;
                }
                uuid_copy (inode_ctx->pargfid, loc->pargfid);
                memcpy (&inode_ctx->buf, buf, sizeof (*buf));
                inode_ctx->type = SNAP_VIEW_ENTRY_POINT_INODE;
        } else {
                if (inode_ctx) {
                        memcpy (buf, &inode_ctx->buf, sizeof (*buf));
                        svs_iatt_fill (inode_ctx->pargfid, postparent);
                } else {
                        svs_iatt_fill (loc->inode->gfid, buf);
                        if (parent)
                                svs_iatt_fill (parent->gfid,
                                               postparent);
                        else {
                                svs_iatt_fill (loc->inode->gfid,
                                               postparent);
                        }
                }
        }

        op_ret = 0;

out:
        return op_ret;
}

/* When lookup comes from client and the protocol/server tries to resolve
   the pargfid via just sending the gfid as part of lookup, if the inode
   for the parent gfid is not found. But since that gfid has not yet been
   looked  up yet, inode will not be having inode context and parent is not
   there (as it is the parent of the entry that is being resolved). So
   without parent and inode context, svs cannot know which snapshot
   to look into. In such cases, the amguity is handled by looking
   into the latest snapshot. If the directory is there in the latest
   snapshot, lookup is successful, otherwise it is a failure. So for
   any directory created after taking the latest snapshot, entry into
   snapshot world is denied. i.e you have to be part of snapshot world
   to enter it. If the gfid is not found there, then unwind with
   ESTALE
   This gets executed mainly in the situation where the snapshot entry
   point is entered from a non-root directory and that non-root directory's
   inode (or gfid) is not yet looked up. And in each case when a gfid has to
   be looked up (without any inode contex and parent context present), last
   snapshot is referred and a random gfid is not generated.
*/
int32_t
svs_lookup_gfid (xlator_t *this, loc_t *loc, struct iatt *buf,
                 struct iatt *postparent, int32_t *op_errno)
{
        int32_t         op_ret                          = -1;
        unsigned char   handle_obj[GFAPI_HANDLE_LENGTH] = {0, };
        glfs_t         *fs                              = NULL;
        glfs_object_t  *object                          = NULL;
        struct stat     statbuf                         = {0, };
        svs_inode_t    *inode_ctx                       = NULL;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);
        GF_VALIDATE_OR_GOTO (this->name, buf, out);
        GF_VALIDATE_OR_GOTO (this->name, postparent, out);

        if (uuid_is_null (loc->gfid) && uuid_is_null (loc->inode->gfid)) {
                gf_log (this->name, GF_LOG_ERROR, "gfid is NULL");
                goto out;
        }

        if (!uuid_is_null (loc->inode->gfid))
                memcpy (handle_obj, loc->inode->gfid,
                        GFAPI_HANDLE_LENGTH);
        else
                memcpy (handle_obj, loc->gfid,
                        GFAPI_HANDLE_LENGTH);

        fs = svs_get_latest_snapshot (this);
        if (!fs) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the latest "
                        "snapshot");
                op_ret = -1;
                *op_errno = EINVAL;
                goto out;
        }


        object = glfs_h_create_from_handle (fs, handle_obj, GFAPI_HANDLE_LENGTH,
                                            &statbuf);
        if (!object) {
                gf_log (this->name, GF_LOG_ERROR, "failed to do lookup and get "
                        "the handle on the snapshot %s (path: %s, gfid: %s)",
                        loc->name, loc->path, uuid_utoa (loc->gfid));
                op_ret = -1;
                *op_errno = ESTALE;
                goto out;
        }

        inode_ctx = svs_inode_ctx_get_or_new (this, loc->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "failed to allocate inode "
                        "context");
                op_ret = -1;
                *op_errno = ENOMEM;
                goto out;
        }

        iatt_from_stat (buf, &statbuf);
        if (!uuid_is_null (loc->gfid))
                uuid_copy (buf->ia_gfid, loc->gfid);
        else
                uuid_copy (buf->ia_gfid, loc->inode->gfid);

        inode_ctx->type = SNAP_VIEW_VIRTUAL_INODE;
        inode_ctx->fs = fs;
        inode_ctx->object = object;
        memcpy (&inode_ctx->buf, buf, sizeof (*buf));
        svs_iatt_fill (buf->ia_gfid, postparent);

        op_ret = 0;

out:
        return op_ret;
}

/* If the parent is an entry point inode, then create the handle for the
   snapshot on which lookup came. i.e in reality lookup came on
   the directory from which the entry point directory was entered, but
   lookup is into the past. So create the handle for it by doing
   the name-less lookup on the gfid (which can be obtained from
   parent's context
*/
int32_t
svs_lookup_snapshot (xlator_t *this, loc_t *loc, struct iatt *buf,
                     struct iatt *postparent, inode_t *parent,
                     svs_inode_t *parent_ctx, int32_t *op_errno)
{
        int32_t         op_ret                          = -1;
        unsigned char   handle_obj[GFAPI_HANDLE_LENGTH] = {0, };
        glfs_t         *fs                              = NULL;
        glfs_object_t  *object                          = NULL;
        struct stat     statbuf                         = {0, };
        svs_inode_t    *inode_ctx                       = NULL;
        uuid_t          gfid;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);
        GF_VALIDATE_OR_GOTO (this->name, buf, out);
        GF_VALIDATE_OR_GOTO (this->name, postparent, out);
        GF_VALIDATE_OR_GOTO (this->name, parent_ctx, out);
        GF_VALIDATE_OR_GOTO (this->name, parent, out);

        fs = svs_initialise_snapshot_volume (this, loc->name);
        if (!fs) {
                gf_log (this->name, GF_LOG_ERROR, "failed to "
                        "create the fs instance for snap %s",
                        loc->name);
                op_ret = -1;
                *op_errno = ESTALE;
                goto out;
        }

        memcpy (handle_obj, parent_ctx->pargfid,
                GFAPI_HANDLE_LENGTH);
        object = glfs_h_create_from_handle (fs, handle_obj, GFAPI_HANDLE_LENGTH,
                                            &statbuf);
        if (!object) {
                gf_log (this->name, GF_LOG_ERROR, "failed to do lookup and "
                        "get the handle on the snapshot %s", loc->name);
                op_ret = -1;
                *op_errno = errno;
                goto out;
        }

        inode_ctx = svs_inode_ctx_get_or_new (this, loc->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "failed to "
                        "allocate inode context");
                op_ret = -1;
                *op_errno = ENOMEM;
                goto out;
        }

        if (uuid_is_null (loc->gfid) &&
            uuid_is_null (loc->inode->gfid))
                uuid_generate (gfid);
        else {
                if (!uuid_is_null (loc->inode->gfid))
                        uuid_copy (gfid, loc->inode->gfid);
                else
                        uuid_copy (gfid, loc->gfid);
        }
        iatt_from_stat (buf, &statbuf);
        uuid_copy (buf->ia_gfid, gfid);
        svs_fill_ino_from_gfid (buf);
        inode_ctx->type = SNAP_VIEW_VIRTUAL_INODE;
        inode_ctx->fs = fs;
        inode_ctx->object = object;
        memcpy (&inode_ctx->buf, buf, sizeof (*buf));
        svs_iatt_fill (parent->gfid, postparent);

        op_ret = 0;

out:
        return op_ret;
}

/* Both parent and entry are from snapshot world */
int32_t
svs_lookup_entry (xlator_t *this, loc_t *loc, struct iatt *buf,
                  struct iatt *postparent, inode_t *parent,
                  svs_inode_t *parent_ctx, int32_t *op_errno)
{
        int32_t         op_ret                          = -1;
        glfs_t         *fs                              = NULL;
        glfs_object_t  *object                          = NULL;
        struct stat     statbuf                         = {0, };
        svs_inode_t    *inode_ctx                       = NULL;
        glfs_object_t  *parent_object                   = NULL;
        uuid_t          gfid;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);
        GF_VALIDATE_OR_GOTO (this->name, buf, out);
        GF_VALIDATE_OR_GOTO (this->name, postparent, out);
        GF_VALIDATE_OR_GOTO (this->name, parent_ctx, out);
        GF_VALIDATE_OR_GOTO (this->name, parent, out);

        parent_object = parent_ctx->object;
        fs = parent_ctx->fs;

        object = glfs_h_lookupat (fs, parent_object, loc->name,
                                  &statbuf);
        if (!object) {
                gf_log (this->name, GF_LOG_ERROR, "failed to do lookup and "
                        "get the handle for entry %s (path: %s)", loc->name,
                        loc->path);
                op_ret = -1;
                *op_errno = errno;
                goto out;
        }

        inode_ctx = svs_inode_ctx_get_or_new (this, loc->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "failed to "
                        "allocate inode context");
                op_ret = -1;
                *op_errno = ENOMEM;
                goto out;
        }

        if (uuid_is_null (loc->gfid) &&
            uuid_is_null (loc->inode->gfid))
                uuid_generate (gfid);
        else {
                if (!uuid_is_null (loc->inode->gfid))
                        uuid_copy (gfid, loc->inode->gfid);
                else
                        uuid_copy (gfid, loc->gfid);
        }

        iatt_from_stat (buf, &statbuf);
        uuid_copy (buf->ia_gfid, gfid);
        svs_fill_ino_from_gfid (buf);
        inode_ctx->type = SNAP_VIEW_VIRTUAL_INODE;
        inode_ctx->fs = fs;
        inode_ctx->object = object;
        memcpy (&inode_ctx->buf, buf, sizeof (*buf));
        svs_iatt_fill (parent->gfid, postparent);

        op_ret = 0;

out:
        return op_ret;
}

/* inode context is there means lookup has come on an object which was
   built either as part of lookup or as part of readdirp. But in readdirp
   we would not have got the handle to access the object in the gfapi
   world.
   So if inode context contains glfs_t instance for the right
   gfapi world and glfs_object_t handle for accessing it in the gfapi
   world, then unwind with success as the snapshots as of now are
   read-only.
   If the above condition is not met, then send lookup call again to
   the gfapi world. It can happen only if both parent context and
   the name of the entry are present.

   If parent is an entry point to snapshot world:
   * parent is needed for getting the gfid on which lookup has to be done
     (the gfid present in the inode is a virtual gfid) in the snapshot
     world.
   * name is required to get the right glfs_t instance on which lookup
     has to be done

   If parent is a directory from snapshot world:
   * parent context is needed to get the glfs_t instance and to get the
     handle to parent directory in the snapshot world.
   * name is needed to do the lookup on the right entry in the snapshot
     world
*/
int32_t
svs_revalidate (xlator_t *this, loc_t *loc, inode_t *parent,
                svs_inode_t *inode_ctx, svs_inode_t *parent_ctx,
                struct iatt *buf, struct iatt *postparent, int32_t *op_errno)
{
        int32_t         op_ret                          = -1;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, buf, out);
        GF_VALIDATE_OR_GOTO (this->name, postparent, out);
        GF_VALIDATE_OR_GOTO (this->name, inode_ctx, out);

        if (inode_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE) {
                svs_iatt_fill (loc->inode->gfid, buf);
                if (parent)
                        svs_iatt_fill (parent->gfid,
                                       postparent);
                else
                        svs_iatt_fill (loc->inode->gfid, postparent);
                op_ret = 0;
                goto out;
        } else {
                if (inode_ctx->fs && inode_ctx->object) {
                        memcpy (buf, &inode_ctx->buf, sizeof (*buf));
                        if (parent)
                                svs_iatt_fill (parent->gfid, postparent);
                        else
                                svs_iatt_fill (buf->ia_gfid, postparent);
                        op_ret = 0;
                        goto out;
                }

                /* To send the lookup to gfapi world, both the name of the
                   entry as well as the parent context is needed.
                */
                if (!loc->name || !parent_ctx) {
                        *op_errno = ESTALE;
                        gf_log (this->name, GF_LOG_ERROR, "%s is NULL",
                                loc->name?"parent context":"loc->name");
                        goto out;
                }

                if (parent_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE)
                        op_ret = svs_lookup_snapshot (this, loc, buf,
                                                      postparent, parent,
                                                      parent_ctx, op_errno);
                else
                        op_ret = svs_lookup_entry (this, loc, buf, postparent,
                                                   parent, parent_ctx,
                                                   op_errno);

                goto out;
        }

out:
        return op_ret;
}

int32_t
svs_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        struct iatt    buf                            = {0, };
        int32_t        op_ret                         = -1;
        int32_t        op_errno                       = EINVAL;
        struct iatt    postparent                     = {0,};
        svs_inode_t   *inode_ctx                      = NULL;
        svs_inode_t   *parent_ctx                     = NULL;
        int32_t        ret                            = -1;
        svs_private_t *private                        = NULL;
        inode_t       *parent                         = NULL;
        glfs_t        *fs                             = NULL;
        snap_dirent_t *dirent                         = NULL;
        gf_boolean_t   entry_point                    = _gf_false;

        GF_VALIDATE_OR_GOTO ("svs", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        private = this->private;

        /* For lookups sent on inodes (i.e not parent inode + basename, but
           direct inode itself which usually is a nameless lookup or revalidate
           on the inode), loc->name will not be there. Get it from path if
           it is there.
           This is the difference between nameless lookup and revalidate lookup
           on an inode:
           nameless lookup: loc->path contains gfid and strrchr on it fails
           revalidate lookup: loc->path contains the entry name of the inode
                              and strrchr gives the name of the entry from path
        */
        if (loc->path) {
                if (!loc->name || (loc->name && !strcmp (loc->name, ""))) {
                        loc->name = strrchr (loc->path, '/');
                        if (loc->name)
                                loc->name++;
                }
        }

        if (loc->name && strlen (loc->name)) {
                ret = dict_get_str_boolean (xdata, "entry-point", _gf_false);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG, "failed to get the "
                                "entry point info");
                        entry_point = _gf_false;
                } else {
                        entry_point = ret;
                }
        }

        if (loc->parent)
                parent = inode_ref (loc->parent);
        else {
                parent = inode_find (loc->inode->table, loc->pargfid);
                if (!parent)
                        parent = inode_parent (loc->inode, NULL, NULL);
        }
        if (parent)
                parent_ctx = svs_inode_ctx_get (this, parent);

        inode_ctx = svs_inode_ctx_get (this, loc->inode);

        /* Initialize latest snapshot, which is used for nameless lookups */
        dirent = svs_get_latest_snap_entry (this);

        if (dirent && !dirent->fs) {
                fs = svs_initialise_snapshot_volume (this, dirent->name);
        }

        /* lookup is on the entry point to the snapshot world */
        if (entry_point) {
                op_ret = svs_lookup_entry_point (this, loc, parent, &buf,
                                                 &postparent, &op_errno);
                goto out;
        }

        /* revalidate */
        if (inode_ctx) {
                op_ret = svs_revalidate (this, loc, parent, inode_ctx,
                                         parent_ctx, &buf, &postparent,
                                         &op_errno);
                goto out;
        }

        /* This can happen when entry point directory is entered from non-root
           directory. (ex: if /mnt/glusterfs is the mount point, then entry
           point (say .snaps) is entered from /mnt/glusterfs/dir/.snaps). Also
           it can happen when client sends a nameless lookup on just a gfid and
           the server does not have the inode in the inode table.
        */
        if (!inode_ctx && !parent_ctx) {
                op_ret = svs_lookup_gfid (this, loc, &buf, &postparent,
                                          &op_errno);
                goto out;
        }

        if (parent_ctx) {
                if (parent_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE)
                        op_ret = svs_lookup_snapshot (this, loc, &buf,
                                                      &postparent, parent,
                                                      parent_ctx, &op_errno);
                else
                        op_ret = svs_lookup_entry (this, loc, &buf,
                                                   &postparent, parent,
                                                   parent_ctx, &op_errno);
                goto out;
        }

out:
        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno,
                             loc?loc->inode:NULL, &buf, xdata, &postparent);

        if (parent)
                inode_unref (parent);

        return 0;
}

int32_t
svs_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
             dict_t *xdata)
{
        svs_inode_t   *inode_ctx  = NULL;
        int32_t        op_ret     = -1;
        int32_t        op_errno   = EINVAL;
        svs_fd_t      *svs_fd     = NULL;
        glfs_fd_t     *glfd       = NULL;
        glfs_t        *fs         = NULL;
        glfs_object_t *object     = NULL;

        GF_VALIDATE_OR_GOTO ("snap-view-daemon", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        inode_ctx = svs_inode_ctx_get (this, loc->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "inode context not found "
                        "for the inode %s", uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        /* Fake success is sent if the opendir is on the entry point directory
           or the inode is SNAP_VIEW_ENTRY_POINT_INODE
        */
        if  (inode_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE) {
                op_ret = 0;
                op_errno = 0;
                goto out;
        } else if (inode_ctx->type == SNAP_VIEW_VIRTUAL_INODE) {
                fs = inode_ctx->fs;
                object = inode_ctx->object;
                glfd = glfs_h_opendir (fs, object);
                if (!glfd) {
                        op_ret = -1;
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR, "opendir on %s "
                                "failed (gfid: %s)", loc->name,
                                uuid_utoa (loc->inode->gfid));
                        goto out;
                }
                svs_fd = svs_fd_ctx_get_or_new (this, fd);
                if (!svs_fd) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to allocate "
                                "fd context %s (gfid: %s)", loc->name,
                                uuid_utoa (fd->inode->gfid));
                        op_ret = -1;
                        op_errno = ENOMEM;
                        glfs_closedir (glfd);
                        goto out;
                }
                svs_fd->fd = glfd;

                op_ret = 0;
                op_errno = 0;
        }

out:
        STACK_UNWIND_STRICT (opendir, frame, op_ret, op_errno, fd, NULL);

        return 0;
}

/*
 * This function adds the xattr keys present in the list (@list) to the dict.
 * But the list contains only the names of the xattrs (and no value, as
 * the gfapi functions for the listxattr operations would return only the
 * names of the xattrs in the buffer provided by the caller, though they had
 * got the values of those xattrs from posix) as described in the man page of
 * listxattr. But before unwinding snapview-server has to put those names
 * back into the dict. But to get the values for those xattrs it has to do the
 * getxattr operation on each xattr which might turn out to be a costly
 * operation. So for each of the xattrs present in the list, a 0 byte value
 * ("") is set into the dict before unwinding. This can be treated as an
 * indicator to other xlators which want to cache the xattrs (as of now,
 * md-cache which caches acl and selinux related xattrs) to not to cache the
 * values of the xattrs present in the dict.
 */
int32_t
svs_add_xattrs_to_dict (xlator_t *this, dict_t *dict, char *list, ssize_t size)
{
        char           keybuffer[4096]  = {0,};
        size_t         remaining_size   = 0;
        int32_t        list_offset      = 0;
        int32_t        ret              = -1;

        GF_VALIDATE_OR_GOTO ("snapview-daemon", this, out);
        GF_VALIDATE_OR_GOTO (this->name, dict, out);
        GF_VALIDATE_OR_GOTO (this->name, list, out);

        remaining_size = size;
        list_offset = 0;
        while (remaining_size > 0) {
                strcpy (keybuffer, list + list_offset);
#ifdef GF_DARWIN_HOST_OS
                /* The protocol expect namespace for now */
                char *newkey = NULL;
                gf_add_prefix (XATTR_USER_PREFIX, keybuffer, &newkey);
                strcpy (keybuffer, newkey);
                GF_FREE (newkey);
#endif
                ret = dict_set_str (dict, keybuffer, "");
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "dict set operation "
                                "for the key %s failed.", keybuffer);
                        goto out;
                }

                remaining_size -= strlen (keybuffer) + 1;
                list_offset += strlen (keybuffer) + 1;
        } /* while (remaining_size > 0) */

        ret = 0;

out:
        return ret;
}

int32_t
svs_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, const char *name,
              dict_t *xdata)
{
        svs_inode_t   *inode_ctx        = NULL;
        int32_t        op_ret           = -1;
        int32_t        op_errno         = EINVAL;
        glfs_t        *fs               = NULL;
        glfs_object_t *object           = NULL;
        char          *value            = 0;
        ssize_t        size             = 0;
        dict_t        *dict             = NULL;

        GF_VALIDATE_OR_GOTO ("snap-view-daemon", this, out);
        GF_VALIDATE_OR_GOTO ("snap-view-daemon", frame, out);
        GF_VALIDATE_OR_GOTO ("snap-view-daemon", loc, out);
        GF_VALIDATE_OR_GOTO ("snap-view-daemon", loc->inode, out);

        inode_ctx = svs_inode_ctx_get (this, loc->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "inode context not found "
                        "for the inode %s", uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        /* EINVAL is sent if the getxattr is on entry point directory
           or the inode is SNAP_VIEW_ENTRY_POINT_INODE. Entry point is
           a virtual directory on which setxattr operations are not
           allowed. If getxattr has to be faked as success, then a value
           for the name of the xattr has to be sent which we dont have.
        */
        if (inode_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE) {
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        } else if (inode_ctx->type == SNAP_VIEW_VIRTUAL_INODE) {
                fs = inode_ctx->fs;
                object = inode_ctx->object;
                dict = dict_new ();
                if (!dict) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to "
                                "allocate dict");
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto out;
                }

                size = glfs_h_getxattrs (fs, object, name, NULL, 0);
                if (size == -1) {
                        gf_log (this->name, GF_LOG_ERROR, "getxattr "
                                "on %s failed (key: %s)", loc->name,
                                name);
                        op_ret = -1;
                        op_errno = errno;
                        goto out;
                }
                value = GF_CALLOC (size + 1, sizeof (char),
                                   gf_common_mt_char);
                if (!value) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to "
                                "allocate memory for getxattr on %s "
                                "(key: %s)", loc->name, name);
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto out;
                }

                size = glfs_h_getxattrs (fs, object, name, value, size);
                if (size == -1) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to "
                                "get the xattr %s for entry %s", name,
                                loc->name);
                        op_ret = -1;
                        op_errno = errno;
                        goto out;
                }
                value[size] = '\0';

                if (name) {
                        op_ret = dict_set_dynptr (dict, (char *)name, value,
                                                  size);
                        if (op_ret < 0) {
                                op_errno = -op_ret;
                                gf_log (this->name, GF_LOG_ERROR, "dict set "
                                        "operation for %s for the key %s "
                                        "failed.", loc->path, name);
                                GF_FREE (value);
                                value = NULL;
                                goto out;
                        }
                } else {
                        op_ret = svs_add_xattrs_to_dict (this, dict, value,
                                                         size);
                        if (op_ret == -1) {
                                gf_log (this->name, GF_LOG_ERROR, "failed to "
                                        "add the xattrs from the list to dict");
                                op_errno = ENOMEM;
                                goto out;
                        }
                        GF_FREE (value);
                }
        }

out:
        if (op_ret)
                GF_FREE (value);

        STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict, NULL);

        if (dict)
                dict_unref (dict);

        return 0;
}

int32_t
svs_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, const char *name,
               dict_t *xdata)
{
        svs_inode_t *inode_ctx  = NULL;
        int32_t      op_ret     = -1;
        int32_t      op_errno   = EINVAL;
        char        *value      = 0;
        ssize_t      size       = 0;
        dict_t      *dict       = NULL;
        svs_fd_t    *sfd        = NULL;
        glfs_fd_t   *glfd       = NULL;

        GF_VALIDATE_OR_GOTO ("snap-view-daemon", this, out);
        GF_VALIDATE_OR_GOTO ("snap-view-daemon", frame, out);
        GF_VALIDATE_OR_GOTO ("snap-view-daemon", fd, out);
        GF_VALIDATE_OR_GOTO ("snap-view-daemon", fd->inode, out);

        inode_ctx = svs_inode_ctx_get (this, fd->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "inode context not found "
                        "for the inode %s", uuid_utoa (fd->inode->gfid));
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        sfd = svs_fd_ctx_get_or_new (this, fd);
        if (!sfd) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the fd "
                        "context for %s", uuid_utoa (fd->inode->gfid));
                op_ret = -1;
                op_errno = EBADFD;
                goto out;
        }

        glfd = sfd->fd;
        /* EINVAL is sent if the getxattr is on entry point directory
           or the inode is SNAP_VIEW_ENTRY_POINT_INODE. Entry point is
           a virtual directory on which setxattr operations are not
           allowed. If getxattr has to be faked as success, then a value
           for the name of the xattr has to be sent which we dont have.
        */
        if (inode_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE) {
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (inode_ctx->type == SNAP_VIEW_VIRTUAL_INODE) {
                dict = dict_new ();
                if (!dict) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to "
                                "allocate  dict");
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto out;
                }

                if (name) {
                        size = glfs_fgetxattr (glfd, name, NULL, 0);
                        if (size == -1) {
                                gf_log (this->name, GF_LOG_ERROR, "getxattr on "
                                        "%s failed (key: %s)",
                                        uuid_utoa (fd->inode->gfid), name);
                                op_ret = -1;
                                op_errno = errno;
                                goto out;
                        }
                        value = GF_CALLOC (size + 1, sizeof (char),
                                           gf_common_mt_char);
                        if (!value) {
                                gf_log (this->name, GF_LOG_ERROR, "failed to "
                                        "allocate memory for getxattr on %s "
                                        "(key: %s)",
                                        uuid_utoa (fd->inode->gfid), name);
                                op_ret = -1;
                                op_errno = ENOMEM;
                                goto out;
                        }

                        size = glfs_fgetxattr (glfd, name, value, size);
                        if (size == -1) {
                                gf_log (this->name, GF_LOG_ERROR, "failed to "
                                        "get the xattr %s for inode %s", name,
                                        uuid_utoa (fd->inode->gfid));
                                op_ret = -1;
                                op_errno = errno;
                                goto out;
                        }
                        value[size] = '\0';

                        op_ret = dict_set_dynptr (dict, (char *)name, value,
                                                  size);
                        if (op_ret < 0) {
                                op_errno = -op_ret;
                                gf_log (this->name, GF_LOG_ERROR, "dict set "
                                        "operation for gfid %s for the key %s "
                                        "failed.",
                                        uuid_utoa (fd->inode->gfid), name);
                                GF_FREE (value);
                                goto out;
                        }
                } else {
                        size = glfs_flistxattr (glfd, NULL, 0);
                        if (size == -1) {
                                gf_log (this->name, GF_LOG_ERROR, "listxattr "
                                        "on %s failed",
                                        uuid_utoa (fd->inode->gfid));
                                goto out;
                        }

                        value = GF_CALLOC (size + 1, sizeof (char),
                                           gf_common_mt_char);
                        if (!value) {
                                op_ret = -1;
                                op_errno = ENOMEM;
                                gf_log (this->name, GF_LOG_ERROR, "failed to "
                                        "allocate buffer for xattr list (%s)",
                                        uuid_utoa (fd->inode->gfid));
                                goto out;
                        }

                        size = glfs_flistxattr (glfd, value, size);
                        if (size == -1) {
                                op_ret = -1;
                                op_errno = errno;
                                gf_log (this->name, GF_LOG_ERROR, "listxattr "
                                        "on %s failed",
                                        uuid_utoa (fd->inode->gfid));
                                goto out;
                        }

                        op_ret = svs_add_xattrs_to_dict (this, dict, value,
                                                         size);
                        if (op_ret == -1) {
                                gf_log (this->name, GF_LOG_ERROR, "failed to "
                                        "add the xattrs from the list to dict");
                                op_errno = ENOMEM;
                                goto out;
                        }
                        GF_FREE (value);
                }

                op_ret = 0;
                op_errno = 0;
        }

out:
        if (op_ret)
                GF_FREE (value);

        STACK_UNWIND_STRICT (fgetxattr, frame, op_ret, op_errno, dict, NULL);

        if (dict)
                dict_unref (dict);

        return 0;
}

int32_t
svs_releasedir (xlator_t *this, fd_t *fd)
{
        svs_fd_t *sfd      = NULL;
        uint64_t          tmp_pfd  = 0;
        int               ret      = 0;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        ret = fd_ctx_del (fd, this, &tmp_pfd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "pfd from fd=%p is NULL", fd);
                goto out;
        }

        sfd = (svs_fd_t *)(long)tmp_pfd;
        if (sfd->fd) {
                ret = glfs_closedir (sfd->fd);
                if (ret)
                        gf_log (this->name, GF_LOG_WARNING, "failed to close "
                                "the glfd for directory %s",
                                uuid_utoa (fd->inode->gfid));
        }

out:
        return 0;
}

int32_t
svs_flush (call_frame_t *frame, xlator_t *this,
           fd_t *fd, dict_t *xdata)
{
        int32_t           op_ret   = -1;
        int32_t           op_errno = 0;
        int               ret      = -1;
        uint64_t          value    = 0;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        ret = fd_ctx_get (fd, this, &value);
        if (ret < 0) {
                op_errno = EINVAL;
                gf_log (this->name, GF_LOG_WARNING,
                        "pfd is NULL on fd=%p", fd);
                goto out;
        }

        op_ret = 0;

out:
        STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno, NULL);

        return 0;
}

int32_t
svs_release (xlator_t *this, fd_t *fd)
{
        svs_fd_t *sfd      = NULL;
        uint64_t          tmp_pfd  = 0;
        int               ret      = 0;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        ret = fd_ctx_del (fd, this, &tmp_pfd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "pfd from fd=%p is NULL", fd);
                goto out;
        }

        sfd = (svs_fd_t *)(long)tmp_pfd;
        if (sfd->fd) {
                ret = glfs_close (sfd->fd);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to close "
                                "the glfd for %s",
                                uuid_utoa (fd->inode->gfid));
                }
        }

out:
        return 0;
}

int32_t
svs_forget  (xlator_t *this, inode_t *inode)
{
        int       ret = -1;
        uint64_t  value = 0;
        svs_inode_t  *inode_ctx = NULL;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        ret = inode_ctx_del (inode, this, &value);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to delte the inode "
                        "context of %s", uuid_utoa (inode->gfid));
                goto out;
        }

        inode_ctx = (svs_inode_t *)value;

        if (inode_ctx->object)
                glfs_h_close (inode_ctx->object);

        GF_FREE (inode_ctx);

out:
        return 0;
}

int
svs_fill_readdir (xlator_t *this, gf_dirent_t *entries, size_t size, off_t off)
{
        gf_dirent_t             *entry          = NULL;
        svs_private_t           *priv           = NULL;
        int                     i               = 0;
        snap_dirent_t           *dirents        = NULL;
        int                     this_size       = 0;
        int                     filled_size     = 0;
        int                     count           = 0;

        GF_VALIDATE_OR_GOTO ("snap-view-daemon", this, out);
        GF_VALIDATE_OR_GOTO ("snap-view-daemon", entries, out);

        priv = this->private;
        GF_ASSERT (priv);

        /* create the dir entries */
        pthread_mutex_lock (&priv->snaplist_lock);
        dirents = priv->dirents;

        for (i = off; i < priv->num_snaps; ) {
                this_size = sizeof (gf_dirent_t) +
                        strlen (dirents[i].name) + 1;
                if (this_size + filled_size > size )
                        goto unlock;

                entry = gf_dirent_for_name (dirents[i].name);
                if (!entry) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to allocate "
                                "dentry for %s", dirents[i].name);
                        goto unlock;
                }

                entry->d_off = i + 1;
                entry->d_ino = i + 2*42;
                entry->d_type = DT_DIR;
                list_add_tail (&entry->list, &entries->list);
                ++i;
                count++;
                filled_size += this_size;
        }

unlock:
        pthread_mutex_unlock (&priv->snaplist_lock);

out:
        return count;
}

int32_t
svs_glfs_readdir (xlator_t *this, glfs_fd_t *glfd, gf_dirent_t *entries,
                  int32_t *op_errno, struct iatt *buf, gf_boolean_t readdirplus,
                  size_t size)
{
        int              filled_size    = 0;
        int              this_size      = 0;
        int32_t          ret            = -1;
        int32_t          count          = 0;
        gf_dirent_t     *entry          = NULL;
        struct   dirent *dirents        = NULL;
        struct   dirent  de             = {0, };
        struct stat      statbuf        = {0, };
        off_t            in_case        = -1;

        GF_VALIDATE_OR_GOTO ("svs", this, out);
        GF_VALIDATE_OR_GOTO (this->name, glfd, out);
        GF_VALIDATE_OR_GOTO (this->name, entries, out);
        GF_VALIDATE_OR_GOTO (this->name, buf, out);

        while (filled_size < size) {
                in_case = glfs_telldir (glfd);
                if (in_case == -1) {
                        gf_log (this->name, GF_LOG_ERROR, "telldir failed");
                        break;
                }

                if (readdirplus)
                        ret = glfs_readdirplus_r (glfd, &statbuf, &de,
                                                  &dirents);
                else
                        ret = glfs_readdir_r (glfd, &de, &dirents);

                if (ret == 0 && dirents != NULL) {
                        if (readdirplus)
                                this_size = max (sizeof (gf_dirent_t),
                                                 sizeof (gfs3_dirplist))
                                        + strlen (de.d_name) + 1;
                        else
                                this_size = sizeof (gf_dirent_t)
                                        + strlen (de.d_name) + 1;

                        if (this_size + filled_size > size) {
                                glfs_seekdir (glfd, in_case);
                                break;
                        }

                        entry = gf_dirent_for_name (de.d_name);
                        if (!entry) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "could not create gf_dirent "
                                        "for entry %s: (%s)",
                                        entry->d_name,
                                        strerror (errno));
                                break;
                        }
                        entry->d_off = glfs_telldir (glfd);
                        entry->d_ino = de.d_ino;
                        entry->d_type = de.d_type;
                        iatt_from_stat (buf, &statbuf);
                        if (readdirplus)
                                entry->d_stat = *buf;
                        list_add_tail (&entry->list, &entries->list);

                        filled_size += this_size;
                        count++;
                } else if (ret == 0 && dirents == NULL) {
                        *op_errno = ENOENT;
                        break;
                } else if (ret != 0) {
                        *op_errno = errno;
                        break;
                }
                dirents = NULL;
                ret = -1;
        }

out:
        return count;
}

/* readdirp can be of 2 types.
   1) It can come on entry point directory where the list of snapshots
      is sent as dirents. In this case, the iatt structure is filled
      on the fly if the inode is not found for the entry or the inode
      context is NULL. Other wise if inode is found and inode context
      is there the iatt structure saved in the context is used.
   2) It can be on a directory in one of the snapshots. In this case,
      the readdirp call would have sent us a iatt structure. So the same
      structure is used with the exception that the gfid and the inode
      numbers will be newly generated and filled in.
*/
void
svs_readdirp_fill (xlator_t *this, inode_t *parent, svs_inode_t *parent_ctx,
                   gf_dirent_t *entry)
{
        inode_t                *inode          = NULL;
        uuid_t                  random_gfid    = {0,};
        struct  iatt            buf            = {0, };
        svs_inode_t            *inode_ctx      = NULL;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, parent, out);
        GF_VALIDATE_OR_GOTO (this->name, parent_ctx, out);
        GF_VALIDATE_OR_GOTO (this->name, entry, out);

        inode = inode_grep (parent->table, parent, entry->d_name);
        if (inode) {
                entry->inode = inode;
                inode_ctx = svs_inode_ctx_get (this, inode);
                if (!inode_ctx) {
                        uuid_copy (buf.ia_gfid, inode->gfid);
                        svs_iatt_fill (inode->gfid, &buf);
                        buf.ia_type = inode->ia_type;
                } else {
                        buf = inode_ctx->buf;
                }

                entry->d_ino = buf.ia_ino;

                if (parent_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE)
                        entry->d_stat = buf;
                else {
                        entry->d_stat.ia_ino = buf.ia_ino;
                        uuid_copy (entry->d_stat.ia_gfid, buf.ia_gfid);
                }
        } else {
                inode = inode_new (parent->table);
                entry->inode = inode;
                uuid_generate (random_gfid);
                uuid_copy (buf.ia_gfid, random_gfid);
                svs_fill_ino_from_gfid (&buf);
                entry->d_ino = buf.ia_ino;

                /* If inode context allocation fails, then do not send the
                   inode for that particular entry as part of readdirp
                   response. Fuse and protocol/server will link the inodes
                   in readdirp only if the entry contains inode in it.
                */
                inode_ctx = svs_inode_ctx_get_or_new (this, inode);
                if (!inode_ctx) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to allocate "
                                "inode context for %s", entry->d_name);
                        inode_unref (entry->inode);
                        entry->inode = NULL;
                        goto out;
                }

                inode_ctx->type = SNAP_VIEW_VIRTUAL_INODE;

                if (parent_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE) {
                        buf.ia_type = IA_IFDIR;
                        inode_ctx->buf = buf;
                        entry->d_stat = buf;
                } else {
                        uuid_copy (entry->d_stat.ia_gfid, buf.ia_gfid);
                        entry->d_stat.ia_ino = buf.ia_ino;
                        inode_ctx->buf = entry->d_stat;
                }
        }

out:
        return;
}

/* In readdirp, though new inode is created along with the generation of
   new gfid, the inode context created will not contain the glfs_t instance
   for the filesystem it belongs to and the handle for it in the gfapi
   world. (handle is obtained only by doing the lookup call on the entry
   and doing lookup on each entry received as part of readdir call is a
   costly operation. So the fs and handle is NULL in the inode context
   and is filled in when lookup comes on that object.
*/
int32_t
svs_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
              off_t off, dict_t *dict)
{
        gf_dirent_t             entries;
        gf_dirent_t            *entry                           = NULL;
        struct  iatt            buf                             = {0, };
        int                     count                           = 0;
        int                     op_ret                          = -1;
        int                     op_errno                        = EINVAL;
        svs_inode_t            *parent_ctx                      = NULL;
        svs_fd_t               *svs_fd                          = NULL;

        GF_VALIDATE_OR_GOTO ("snap-view-daemon", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, frame, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, unwind);

        INIT_LIST_HEAD (&entries.list);

        parent_ctx = svs_inode_ctx_get (this, fd->inode);
        if (!parent_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the inode "
                        "context for %s", uuid_utoa (fd->inode->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto unwind;
        }

        if (parent_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE) {
                LOCK (&fd->lock);
                {
                        count = svs_fill_readdir (this, &entries, size, off);
                }
                UNLOCK (&fd->lock);

                op_ret = count;

                list_for_each_entry (entry, &entries.list, list) {
                        svs_readdirp_fill (this, fd->inode, parent_ctx, entry);
                }

                goto unwind;
        } else {
                svs_fd = svs_fd_ctx_get_or_new (this, fd);
                if (!svs_fd) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to get the "
                                "fd context %s", uuid_utoa (fd->inode->gfid));
                        op_ret = -1;
                        op_errno = EBADFD;
                        goto unwind;
                }

                glfs_seekdir (svs_fd->fd, off);

                LOCK (&fd->lock);
                {
                        count = svs_glfs_readdir (this, svs_fd->fd, &entries,
                                                  &op_errno, &buf, _gf_true,
                                                  size);
                }
                UNLOCK (&fd->lock);

                op_ret = count;

                list_for_each_entry (entry, &entries.list, list) {
                        svs_readdirp_fill (this, fd->inode, parent_ctx, entry);
                }

                goto unwind;
        }

unwind:
        STACK_UNWIND_STRICT (readdirp, frame, op_ret, op_errno, &entries, dict);

        gf_dirent_free (&entries);

        return 0;
}

int32_t
svs_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t off, dict_t *xdata)
{
        svs_private_t *priv      = NULL;
        gf_dirent_t    entries;
        int            count     = 0;
        svs_inode_t   *inode_ctx = NULL;
        int            op_errno  = EINVAL;
        int            op_ret    = -1;
        svs_fd_t      *svs_fd    = NULL;
        glfs_fd_t     *glfd      = NULL;

        GF_VALIDATE_OR_GOTO ("snap-view-server", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, frame, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, unwind);

        priv = this->private;

        inode_ctx = svs_inode_ctx_get (this, fd->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "inode context not found in "
                        "the inode %s", uuid_utoa (fd->inode->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto unwind;
        }

        if (inode_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE) {
                LOCK (&fd->lock);
                {
                        count = svs_fill_readdir (this, &entries, size, off);
                }
                UNLOCK (&fd->lock);
        } else {
                svs_fd = svs_fd_ctx_get_or_new (this, fd);
                if (!svs_fd) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to get the "
                                "fd context %s", uuid_utoa (fd->inode->gfid));
                        op_ret = -1;
                        op_errno = EBADFD;
                        goto unwind;
                }

                glfd = svs_fd->fd;

                LOCK (&fd->lock);
                {
                        count = svs_glfs_readdir (this, glfd, &entries,
                                                  &op_errno, NULL, _gf_false,
                                                  size);
                }
                UNLOCK (&fd->lock);
        }

        op_ret = count;

unwind:
        STACK_UNWIND_STRICT (readdir, frame, op_ret, op_errno, &entries, xdata);

        gf_dirent_free (&entries);

        return 0;
}

int32_t
svs_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        svs_private_t *priv         = NULL;
        struct iatt    buf          = {0, };
        int32_t        op_errno     = EINVAL;
        int32_t        op_ret       = -1;
        svs_inode_t   *inode_ctx    = NULL;
        glfs_t        *fs           = NULL;
        glfs_object_t *object       = NULL;
        struct stat    stat         = {0, };
        int            ret          = -1;

        GF_VALIDATE_OR_GOTO ("snap-view-daemon", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        priv = this->private;

        /* Instead of doing the check of whether it is a entry point directory
           or not by checking the name of the entry and then deciding what
           to do, just check the inode context and decide what to be done.
        */

        inode_ctx = svs_inode_ctx_get (this, loc->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "inode context not found for"
                        " %s", uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (inode_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE) {
                svs_iatt_fill (loc->inode->gfid, &buf);
                op_ret = 0;
        } else if (inode_ctx->type == SNAP_VIEW_VIRTUAL_INODE) {
                fs = inode_ctx->fs;
                object = inode_ctx->object;
                ret = glfs_h_stat (fs, object, &stat);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "glfs_h_stat on %s "
                                "(gfid: %s) failed", loc->name,
                                uuid_utoa (loc->inode->gfid));
                        op_ret = -1;
                        op_errno = errno;
                        goto out;
                }

                iatt_from_stat (&buf, &stat);
                uuid_copy (buf.ia_gfid, loc->inode->gfid);
                svs_fill_ino_from_gfid (&buf);
                op_ret = ret;
        }

out:
        STACK_UNWIND_STRICT (stat, frame, op_ret, op_errno, &buf, xdata);
        return 0;
}

int32_t
svs_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        svs_private_t *priv        = NULL;
        struct iatt    buf         = {0, };
        int32_t        op_errno    = EINVAL;
        int32_t        op_ret      = -1;
        svs_inode_t   *inode_ctx   = NULL;
        struct stat    stat        = {0, };
        int            ret         = -1;
        glfs_fd_t     *glfd        = NULL;
        svs_fd_t      *sfd         = NULL;

        GF_VALIDATE_OR_GOTO ("snap-view-daemon", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, out);

        priv = this->private;

        /* Instead of doing the check of whether it is a entry point directory
           or not by checking the name of the entry and then deciding what
           to do, just check the inode context and decide what to be done.
        */

        inode_ctx = svs_inode_ctx_get (this, fd->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "inode context not found for"
                        " the inode %s", uuid_utoa (fd->inode->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (inode_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE) {
                svs_iatt_fill (fd->inode->gfid, &buf);
                op_ret = 0;
        } else if (inode_ctx->type == SNAP_VIEW_VIRTUAL_INODE) {
                sfd = svs_fd_ctx_get_or_new (this, fd);
                if (!sfd) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to get the "
                                "fd context for %s",
                                uuid_utoa (fd->inode->gfid));
                        op_ret = -1;
                        op_errno = EBADFD;
                        goto out;
                }

                glfd = sfd->fd;
                ret = glfs_fstat (glfd, &stat);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "glfs_fstat on "
                                "gfid: %s failed", uuid_utoa (fd->inode->gfid));
                        op_ret = -1;
                        op_errno = errno;
                        goto out;
                }

                iatt_from_stat (&buf, &stat);
                uuid_copy (buf.ia_gfid, fd->inode->gfid);
                svs_fill_ino_from_gfid (&buf);
                op_ret = ret;
        }

out:
        STACK_UNWIND_STRICT (fstat, frame, op_ret, op_errno, &buf, xdata);
        return 0;
}

int32_t
svs_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
          fd_t *fd, dict_t *xdata)
{
        svs_inode_t   *inode_ctx = NULL;
        svs_fd_t      *sfd       = NULL;
        int32_t        op_ret    = -1;
        int32_t        op_errno  = EINVAL;
        glfs_fd_t     *glfd      = NULL;
        glfs_t        *fs        = NULL;
        glfs_object_t *object    = NULL;


        GF_VALIDATE_OR_GOTO ("snap-view-daemon", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        inode_ctx = svs_inode_ctx_get (this, loc->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "inode context for %s "
                        "(gfid: %s) not found", loc->name,
                        uuid_utoa (loc->inode->gfid));
                goto out;
        }

        if (inode_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE)
                GF_ASSERT (0); // on entry point it should always be opendir

        fs = inode_ctx->fs;
        object = inode_ctx->object;

        glfd = glfs_h_open (fs, object, flags);
        if (!glfd) {
                gf_log (this->name, GF_LOG_ERROR, "glfs_h_open on %s failed "
                        "(gfid: %s)", loc->name, uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = errno;
                goto out;
        }

        sfd = svs_fd_ctx_get_or_new (this, fd);
        if (!sfd) {
                gf_log (this->name, GF_LOG_ERROR, "failed to allocate fd "
                        "context for %s (gfid: %s)", loc->name,
                        uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = ENOMEM;
                glfs_close (glfd);
                goto out;
        }
        sfd->fd = glfd;

        op_ret = 0;

out:
        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd, NULL);
        return 0;
}

int32_t
svs_readv (call_frame_t *frame, xlator_t *this,
           fd_t *fd, size_t size, off_t offset, uint32_t flags, dict_t *xdata)
{
        int32_t                op_ret     = -1;
        int32_t                op_errno   = 0;
        svs_private_t         *priv       = NULL;
        struct iobuf          *iobuf      = NULL;
        struct iobref         *iobref     = NULL;
        struct iovec           vec        = {0,};
        svs_fd_t              *sfd        = NULL;
        int                    ret        = -1;
        struct stat            fstatbuf   = {0, };
        glfs_fd_t             *glfd       = NULL;
        struct iatt            stbuf      = {0, };

        GF_VALIDATE_OR_GOTO ("snap-view-daemon", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        sfd = svs_fd_ctx_get_or_new (this, fd);
        if (!sfd) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the fd "
                        "context for %s", uuid_utoa (fd->inode->gfid));
                op_ret = -1;
                op_errno = EBADFD;
                goto out;
        }

        glfd = sfd->fd;

        iobuf = iobuf_get2 (this->ctx->iobuf_pool, size);
        if (!iobuf) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto out;
        }

        ret = glfs_pread (glfd, iobuf->ptr, size, offset, 0);
        if (ret < 0) {
                op_ret = -1;
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "glfs_read failed (%s)",
                        strerror (op_errno));
                goto out;
        }

        vec.iov_base = iobuf->ptr;
        vec.iov_len  = ret;

        iobref = iobref_new ();

        iobref_add (iobref, iobuf);

        ret = glfs_fstat (glfd, &fstatbuf);
        if (ret) {
                op_ret = -1;
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "glfs_fstat failed after "
                        "readv on %s", uuid_utoa (fd->inode->gfid));
                goto out;
        }

        iatt_from_stat (&stbuf, &fstatbuf);
        uuid_copy (stbuf.ia_gfid, fd->inode->gfid);
        svs_fill_ino_from_gfid (&stbuf);

        /* Hack to notify higher layers of EOF. */
        if (!stbuf.ia_size || (offset + vec.iov_len) >= stbuf.ia_size)
                op_errno = ENOENT;

        op_ret = vec.iov_len;

out:

        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno,
                             &vec, 1, &stbuf, iobref, NULL);

        if (iobref)
                iobref_unref (iobref);
        if (iobuf)
                iobuf_unref (iobuf);

        return 0;
}

int32_t
svs_readlink (call_frame_t *frame, xlator_t *this,
              loc_t *loc, size_t size, dict_t *xdata)
{
        svs_inode_t     *inode_ctx = NULL;
        glfs_t          *fs        = NULL;
        glfs_object_t   *object    = NULL;
        int              op_ret    = -1;
        int              op_errno  = EINVAL;
        char            *buf       = NULL;
        struct iatt      stbuf     = {0, };
        int              ret       = -1;
        struct stat      stat      = {0, };

        GF_VALIDATE_OR_GOTO ("snap-view-daemon", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        inode_ctx = svs_inode_ctx_get (this, loc->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get inode context "
                        "for %s (gfid: %s)", loc->name,
                        uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        fs = inode_ctx->fs;
        if (!fs) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the fs "
                        "instance for %s (gfid: %s)", loc->name,
                        uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        object = inode_ctx->object;
        if (!object) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the object "
                        "for %s (gfid: %s)", loc->name,
                        uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        ret = glfs_h_stat (fs, object, &stat);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "glfs_h_stat on %s "
                        "(gfid: %s) failed", loc->name,
                        uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = errno;
                goto out;
        }

        iatt_from_stat (&stbuf, &stat);
        uuid_copy (stbuf.ia_gfid, loc->inode->gfid);
        svs_fill_ino_from_gfid (&stbuf);

        buf = alloca (size + 1);
        op_ret = glfs_h_readlink (fs, object, buf, size);
        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_ERROR, "readlink on %s failed "
                        "(gfid: %s)", loc->name, uuid_utoa (loc->inode->gfid));
                op_errno = errno;
                goto out;
        }

        buf[op_ret] = 0;

out:
        STACK_UNWIND_STRICT (readlink, frame, op_ret, op_errno, buf, &stbuf,
                             NULL);

        return 0;
}

int32_t
svs_access (call_frame_t *frame, xlator_t *this, loc_t *loc, int mask,
            dict_t *xdata)
{
        int             ret          = -1;
        int32_t         op_ret       = -1;
        int32_t         op_errno     = EINVAL;
        svs_private_t  *priv         = NULL;
        glfs_t         *fs           = NULL;
        glfs_object_t  *object       = NULL;
        svs_inode_t    *inode_ctx    = NULL;
        gf_boolean_t    is_fuse_call = 0;
        int             mode         = 0;

        GF_VALIDATE_OR_GOTO ("svs", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        priv = this->private;

        inode_ctx = svs_inode_ctx_get (this, loc->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "inode context not found for"
                        " %s", uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        is_fuse_call = __is_fuse_call (frame);

        if (inode_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE) {
                if (is_fuse_call) {
                        op_ret = 0;
                        op_errno = 0;
                } else {
                        op_ret = 0;
                        mode |= POSIX_ACL_READ;
                        mode |= POSIX_ACL_EXECUTE;
                        op_errno = mode;
                }
                goto out;
        }

        fs = inode_ctx->fs;
        object = inode_ctx->object;

        if (!is_fuse_call)
                syncopctx_setfspid (&frame->root->pid);

        ret = glfs_h_access (fs, object, mask);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "failed to access %s "
                        "(gfid: %s)", loc->path, uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = errno;
                goto out;
        }

        /* The actual posix_acl xlator does acl checks differently for
           fuse and nfs. In this case how to send the information of
           whether the call came from fuse or nfs to the snapshot volume
           via gfapi?
        */

        op_ret = 0;
        op_errno = ret;

out:

        STACK_UNWIND_STRICT (access, frame, op_ret, op_errno, NULL);
        return 0;
}


int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_svs_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_WARNING, "Memory accounting"
                        " init failed");
                return ret;
        }

        return ret;
}

int32_t
init (xlator_t *this)
{
        svs_private_t   *priv           = NULL;
        int             ret             = -1;
        pthread_t       snap_thread;

        /* This can be the top of graph in certain cases */
        if (!this->parents) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "dangling volume. check volfile ");
        }

        priv = GF_CALLOC (1, sizeof (*priv), gf_svs_mt_priv_t);
        if (!priv)
                goto out;

        this->private = priv;

        GF_OPTION_INIT ("volname", priv->volname, str, out);
        pthread_mutex_init (&(priv->snaplist_lock), NULL);

        pthread_mutex_lock (&priv->snaplist_lock);
        priv->is_snaplist_done = 0;
        priv->num_snaps = 0;
        snap_worker_resume = _gf_false;
        pthread_mutex_unlock (&priv->snaplist_lock);

        /* get the list of snaps first to return to client xlator */
        ret = svs_get_snapshot_list (this);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Error initializing snaplist infrastructure");
                ret = -1;
                goto out;
        }

        if ((ret = pthread_attr_init (&priv->thr_attr)) != 0) {
                gf_log (this->name, GF_LOG_ERROR, "pthread attr init failed");
                goto out;
        }

        ret = gf_thread_create (&snap_thread,
                                &priv->thr_attr,
                                snaplist_worker,
                                this);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to create snaplist worker thread");
                goto out;
        }

        ret = 0;

out:
        if (ret && priv) {
                GF_FREE (priv->dirents);
                GF_FREE (priv);
        }

        return ret;
}

void
fini (xlator_t *this)
{
        svs_private_t   *priv   = NULL;
        glusterfs_ctx_t *ctx    = NULL;
        int             ret     = 0;

        GF_ASSERT (this);
        priv = this->private;
        this->private = NULL;
        ctx = this->ctx;
        if (!ctx)
                gf_log (this->name, GF_LOG_ERROR,
                        "Invalid ctx found");

        if (priv) {
                gf_timer_call_cancel (ctx, priv->snap_timer);
                priv->snap_timer = NULL;
                ret = pthread_mutex_destroy (&priv->snaplist_lock);
                if (ret != 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Could not destroy mutex snaplist_lock");
                }
                ret = pthread_attr_destroy (&priv->thr_attr);
                if (ret != 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Could not destroy pthread attr");
                }
                if (priv->dirents) {
                        GF_FREE (priv->dirents);
                }
                GF_FREE (priv);
        }

        ret = pthread_mutex_destroy (&mutex);
        if (ret != 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Could not destroy mutex");
        }
        pthread_cond_destroy (&condvar);
        if (ret != 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Could not destroy condition variable");
        }

        return;
}

struct xlator_fops fops = {
        .lookup     = svs_lookup,
        .stat       = svs_stat,
        .opendir    = svs_opendir,
        .readdirp   = svs_readdirp,
        .readdir    = svs_readdir,
        .open       = svs_open,
        .readv      = svs_readv,
        .flush      = svs_flush,
        .fstat      = svs_fstat,
        .getxattr   = svs_getxattr,
        .access     = svs_access,
        /* entry fops */
};

struct xlator_cbks cbks = {
        .release  = svs_release,
        .releasedir = svs_releasedir,
        .forget     = svs_forget,
};

struct volume_options options[] = {
        { .key  = {"volname"},
          .type = GF_OPTION_TYPE_STR,
        },
        { .key  = {NULL} },
};
