/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <ctype.h>
#include <sys/uio.h>

#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "changelog.h"
#include "compat-errno.h"
#include "call-stub.h"

#include "bit-rot-stub.h"
#include "bit-rot-stub-mem-types.h"
#include "bit-rot-stub-messages.h"
#include "bit-rot-common.h"

#define BR_STUB_REQUEST_COOKIE  0x1

void *br_stub_signth (void *);

struct br_stub_signentry {
        unsigned long v;

        call_stub_t *stub;

        struct list_head list;
};

int32_t
mem_acct_init (xlator_t *this)
{
        int32_t ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_br_stub_mt_end + 1);

        if (ret != 0) {
                gf_msg (this->name, GF_LOG_WARNING, 0, BRS_MSG_MEM_ACNT_FAILED,
                        "Memory accounting init failed");
                return ret;
        }

        return ret;
}

int32_t
br_stub_bad_object_container_init (xlator_t *this, br_stub_private_t *priv)
{
        pthread_attr_t  w_attr;
        int32_t         ret          = -1;

        ret = pthread_cond_init(&priv->container.bad_cond, NULL);
        if (ret != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        BRS_MSG_BAD_OBJ_THREAD_FAIL,
                        "pthread_cond_init failed (%d)", ret);
                goto out;
        }

        ret = pthread_mutex_init(&priv->container.bad_lock, NULL);
        if (ret != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        BRS_MSG_BAD_OBJ_THREAD_FAIL,
                        "pthread_mutex_init failed (%d)", ret);
                goto cleanup_cond;
        }

        ret = pthread_attr_init (&w_attr);
        if (ret != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        BRS_MSG_BAD_OBJ_THREAD_FAIL,
                        "pthread_attr_init failed (%d)", ret);
                goto cleanup_lock;
        }

        ret = pthread_attr_setstacksize (&w_attr, BAD_OBJECT_THREAD_STACK_SIZE);
        if (ret == EINVAL) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        BRS_MSG_BAD_OBJ_THREAD_FAIL,
                        "Using default thread stack size");
        }

        INIT_LIST_HEAD (&priv->container.bad_queue);
        ret = br_stub_dir_create (this, priv);
        if (ret < 0)
                goto cleanup_lock;

        ret = gf_thread_create (&priv->container.thread, &w_attr, br_stub_worker, this);
        if (ret)
                goto cleanup_attr;

        return 0;

cleanup_attr:
        pthread_attr_destroy (&w_attr);
cleanup_lock:
        pthread_mutex_destroy (&priv->container.bad_lock);
cleanup_cond:
        pthread_cond_destroy (&priv->container.bad_cond);
out:
        return -1;
}

#define BR_STUB_QUARANTINE_DIR GF_HIDDEN_PATH"/quanrantine"

int32_t
init (xlator_t *this)
{
        int32_t ret = 0;
        char *tmp = NULL;
        struct timeval tv = {0,};
        br_stub_private_t *priv = NULL;

        if (!this->children) {
                gf_msg (this->name, GF_LOG_ERROR, 0, BRS_MSG_NO_CHILD,
                        "FATAL: no children");
                goto error_return;
        }

        priv = GF_CALLOC (1, sizeof (*priv), gf_br_stub_mt_private_t);
        if (!priv)
                goto error_return;

        priv->local_pool = mem_pool_new (br_stub_local_t, 512);
        if (!priv->local_pool)
                goto free_priv;

        GF_OPTION_INIT ("bitrot", priv->go, bool, free_mempool);

        GF_OPTION_INIT ("export", tmp, str, free_mempool);
        memcpy (priv->export, tmp, strlen (tmp) + 1);

        (void) snprintf (priv->stub_basepath, PATH_MAX,
                         "%s/%s", priv->export, BR_STUB_QUARANTINE_DIR);

        (void) gettimeofday (&tv, NULL);

        /* boot time is in network endian format */
        priv->boot[0] = htonl (tv.tv_sec);
        priv->boot[1] = htonl (tv.tv_usec);

        pthread_mutex_init (&priv->lock, NULL);
        pthread_cond_init (&priv->cond, NULL);
        INIT_LIST_HEAD (&priv->squeue);

        /* Thread creations need 'this' to be passed so that THIS can be
         * assigned inside the thread. So setting this->private here.
         */
        this->private = priv;

        ret = gf_thread_create (&priv->signth, NULL, br_stub_signth, this);
        if (ret != 0)
                goto cleanup_lock;

        ret = br_stub_bad_object_container_init (this, priv);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, BRS_MSG_BAD_CONTAINER_FAIL,
                        "failed to launch the thread for storing bad gfids");
                goto cleanup_lock;
        }

        gf_msg_debug (this->name, 0, "bit-rot stub loaded");

        return 0;

 cleanup_lock:
        pthread_cond_destroy (&priv->cond);
        pthread_mutex_destroy (&priv->lock);
 free_mempool:
        mem_pool_destroy (priv->local_pool);
 free_priv:
        GF_FREE (priv);
        this->private = NULL;
 error_return:
        return -1;
}

void
fini (xlator_t *this)
{
        int32_t ret = 0;
	br_stub_private_t *priv = this->private;
        struct br_stub_signentry *sigstub = NULL;
        call_stub_t *stub = NULL;

        if (!priv)
                return;

        ret = gf_thread_cleanup_xint (priv->signth);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        BRS_MSG_CANCEL_SIGN_THREAD_FAILED,
                        "Could not cancel sign serializer thread");
                goto out;
        }

        while (!list_empty (&priv->squeue)) {
                sigstub = list_first_entry (&priv->squeue,
                                            struct br_stub_signentry, list);
                list_del_init (&sigstub->list);

                call_stub_destroy (sigstub->stub);
                GF_FREE (sigstub);
        }

        pthread_mutex_destroy (&priv->lock);
        pthread_cond_destroy (&priv->cond);

        ret = gf_thread_cleanup_xint (priv->container.thread);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        BRS_MSG_CANCEL_SIGN_THREAD_FAILED,
                        "Could not cancel sign serializer thread");
                goto out;
        }

        while (!list_empty (&priv->container.bad_queue)) {
                stub = list_first_entry (&priv->container.bad_queue, call_stub_t,
                                         list);
                list_del_init (&stub->list);
                call_stub_destroy (stub);
        };

        pthread_mutex_destroy (&priv->container.bad_lock);
        pthread_cond_destroy (&priv->container.bad_cond);

        this->private = NULL;
	GF_FREE (priv);

 out:
	return;
}

static int
br_stub_alloc_versions (br_version_t **obuf,
                        br_signature_t **sbuf, size_t signaturelen)
{
        void *mem = NULL;
        size_t size = 0;

        if (obuf)
                size += sizeof (br_version_t);
        if (sbuf)
                size += sizeof (br_signature_t) + signaturelen;

        mem = GF_CALLOC (1, size, gf_br_stub_mt_version_t);
        if (!mem)
                goto error_return;

        if (obuf) {
                *obuf = (br_version_t *)mem;
                mem = ((char *)mem + sizeof (br_version_t));
        }
        if (sbuf) {
                *sbuf = (br_signature_t *)mem;
        }

        return 0;

 error_return:
        return -1;
}

static void
br_stub_dealloc_versions (void *mem)
{
        GF_FREE (mem);
}

static br_stub_local_t *
br_stub_alloc_local (xlator_t *this)
{
        br_stub_private_t *priv = this->private;

        return mem_get0 (priv->local_pool);
}

static void
br_stub_dealloc_local (br_stub_local_t *ptr)
{
        mem_put (ptr);
}

static int
br_stub_prepare_version_request (xlator_t *this, dict_t *dict,
                                br_version_t *obuf, unsigned long oversion)
{
        br_stub_private_t *priv = NULL;

        priv = this->private;
        br_set_ongoingversion (obuf, oversion, priv->boot);

        return dict_set_static_bin (dict, BITROT_CURRENT_VERSION_KEY,
                                    (void *)obuf, sizeof (br_version_t));
}

static int
br_stub_prepare_signing_request (dict_t *dict,
                                 br_signature_t *sbuf,
                                 br_isignature_t *sign, size_t signaturelen)
{
        size_t size = 0;

        br_set_signature (sbuf, sign, signaturelen, &size);

        return dict_set_static_bin (dict, BITROT_SIGNING_VERSION_KEY,
                                    (void *)sbuf, size);
}

/**
 * initialize an inode context starting with a given ongoing version.
 * a fresh lookup() or a first creat() call initializes the inode
 * context, hence the inode is marked dirty. this routine also
 * initializes the transient inode version.
 */
static int
br_stub_init_inode_versions (xlator_t *this, fd_t *fd, inode_t *inode,
                             unsigned long version, gf_boolean_t markdirty,
                             gf_boolean_t bad_object)
{
        int32_t ret = 0;
        br_stub_inode_ctx_t *ctx = NULL;

        ctx = GF_CALLOC (1, sizeof (br_stub_inode_ctx_t),
                         gf_br_stub_mt_inode_ctx_t);
        if (!ctx)
                goto error_return;

        INIT_LIST_HEAD (&ctx->fd_list);
        (markdirty) ? __br_stub_mark_inode_dirty (ctx)
                : __br_stub_mark_inode_synced (ctx);
        __br_stub_set_ongoing_version (ctx, version);

        if (bad_object)
                __br_stub_mark_object_bad (ctx);

        if (fd) {
                ret = br_stub_add_fd_to_inode (this, fd, ctx);
                if (ret)
                        goto free_ctx;
        }

        ret = br_stub_set_inode_ctx (this, inode, ctx);
        if (ret)
                goto free_ctx;
        return 0;

free_ctx:
        GF_FREE (ctx);
 error_return:
        return -1;
}

/**
 * modify the ongoing version of an inode.
 */
static int
br_stub_mod_inode_versions (xlator_t *this,
                            fd_t *fd, inode_t *inode, unsigned long version)
{
        int32_t ret = -1;
        br_stub_inode_ctx_t *ctx = 0;

        LOCK (&inode->lock);
        {
                ctx = __br_stub_get_ongoing_version_ctx (this, inode, NULL);
                if (ctx == NULL)
                        goto unblock;
                if (__br_stub_is_inode_dirty (ctx)) {
                        __br_stub_set_ongoing_version (ctx, version);
                        __br_stub_mark_inode_synced (ctx);
                }

                ret = 0;
        }
unblock:
        UNLOCK (&inode->lock);

        return ret;
}

static void
br_stub_fill_local (br_stub_local_t *local,
                    call_stub_t *stub, fd_t *fd, inode_t *inode, uuid_t gfid,
                    int versioningtype, unsigned long memversion)
{
        local->fopstub = stub;
        local->versioningtype = versioningtype;
        local->u.context.version = memversion;
        if (fd)
                local->u.context.fd = fd_ref (fd);
        if (inode)
                local->u.context.inode = inode_ref (inode);
        gf_uuid_copy (local->u.context.gfid, gfid);
}

static void
br_stub_cleanup_local (br_stub_local_t *local)
{
        local->fopstub = NULL;
        local->versioningtype = 0;
        local->u.context.version = 0;
        if (local->u.context.fd) {
                fd_unref (local->u.context.fd);
                local->u.context.fd = NULL;
        }
        if (local->u.context.inode) {
                inode_unref (local->u.context.inode);
                local->u.context.inode = NULL;
        }
        memset (local->u.context.gfid, '\0', sizeof (uuid_t));
}

static int
br_stub_need_versioning (xlator_t *this,
                         fd_t *fd, gf_boolean_t *versioning,
                         gf_boolean_t *modified, br_stub_inode_ctx_t **ctx)
{
        int32_t ret      = -1;
        uint64_t ctx_addr = 0;
        br_stub_inode_ctx_t *c = NULL;

        *versioning = _gf_false;
        *modified = _gf_false;

        ret = br_stub_get_inode_ctx (this, fd->inode, &ctx_addr);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        BRS_MSG_GET_INODE_CONTEXT_FAILED, "failed to get the "
                        "inode context for the inode %s",
                        uuid_utoa (fd->inode->gfid));
                goto error_return;
        }

        c = (br_stub_inode_ctx_t *) (long) ctx_addr;

        LOCK (&fd->inode->lock);
        {
                if (__br_stub_is_inode_dirty (c))
                        *versioning = _gf_true;
                if (__br_stub_is_inode_modified (c))
                        *modified = _gf_true;
        }
        UNLOCK (&fd->inode->lock);

        if (ctx)
                *ctx = c;
        return 0;

 error_return:
        return -1;
}

static int32_t
br_stub_anon_fd_ctx (xlator_t *this, fd_t *fd, br_stub_inode_ctx_t *ctx)
{
        int32_t  ret = -1;
        br_stub_fd_t *br_stub_fd = NULL;

        br_stub_fd = br_stub_fd_ctx_get (this, fd);
        if (!br_stub_fd) {
                ret = br_stub_add_fd_to_inode (this, fd, ctx);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                BRS_MSG_ADD_FD_TO_INODE, "failed to add fd to "
                                "the inode (gfid: %s)",
                                uuid_utoa (fd->inode->gfid));
                        goto out;
                }
        }

        ret = 0;

out:
        return ret;
}

static int
br_stub_versioning_prep (call_frame_t *frame,
                         xlator_t *this, fd_t *fd, br_stub_inode_ctx_t *ctx)
{
        int32_t          ret = -1;
        br_stub_local_t *local   = NULL;

        local = br_stub_alloc_local (this);
        if (!local) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM, BRS_MSG_NO_MEMORY,
                        "local allocation failed (gfid: %s)",
                        uuid_utoa (fd->inode->gfid));
                goto error_return;
        }

        if (fd_is_anonymous (fd)) {
                ret = br_stub_anon_fd_ctx (this, fd, ctx);
                if (ret)
                        goto free_local;
        }

        frame->local = local;

        return 0;

 free_local:
        br_stub_dealloc_local (local);
 error_return:
        return -1;
}

static int
br_stub_mark_inode_modified (xlator_t *this, br_stub_local_t *local)
{
        fd_t                *fd       = NULL;
        int32_t              ret      = 0;
        uint64_t             ctx_addr = 0;
        br_stub_inode_ctx_t *ctx      = NULL;

        fd = local->u.context.fd;

        ret = br_stub_get_inode_ctx (this, fd->inode, &ctx_addr);
        if (ret < 0)
                goto error_return;

        ctx = (br_stub_inode_ctx_t *) (long) ctx_addr;

        LOCK (&fd->inode->lock);
        {
                __br_stub_set_inode_modified (ctx);
        }
        UNLOCK (&fd->inode->lock);

        return 0;

 error_return:
        return -1;
}

/**
 * The possible return values from br_stub_is_bad_object () are:
 * 1) 0  => as per the inode context object is not bad
 * 2) -1 => Failed to get the inode context itself
 * 3) -2 => As per the inode context object is bad
 * Both -ve values means the fop which called this function is failed
 * and error is returned upwards.
 */
static int
br_stub_check_bad_object (xlator_t *this, inode_t *inode, int32_t *op_ret,
                           int32_t *op_errno)
{
        int ret = -1;

        ret = br_stub_is_bad_object (this, inode);
        if (ret == -2) {
                gf_msg (this->name, GF_LOG_ERROR, 0, BRS_MSG_BAD_OBJECT_ACCESS,
                        "%s is a bad object. Returning",
                        uuid_utoa (inode->gfid));
                *op_ret = -1;
                *op_errno = EIO;
        }

        if (ret == -1) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        BRS_MSG_GET_INODE_CONTEXT_FAILED, "could not get inode"
                        " context for %s", uuid_utoa (inode->gfid));
                *op_ret = -1;
                *op_errno = EINVAL;
        }

        return ret;
}

/**
 * callback for inode/fd versioning
 */
int
br_stub_fd_incversioning_cbk (call_frame_t *frame,
                              void *cookie, xlator_t *this,
                              int op_ret, int op_errno, dict_t *xdata)
{
        fd_t            *fd      = NULL;
        inode_t         *inode   = NULL;
        unsigned long    version = 0;
        br_stub_local_t *local   = NULL;

        local = (br_stub_local_t *)frame->local;
        if (op_ret < 0)
                goto done;
        fd      = local->u.context.fd;
        inode   = local->u.context.inode;
        version = local->u.context.version;

        op_ret = br_stub_mod_inode_versions (this, fd, inode, version);
        if (op_ret < 0)
                op_errno = EINVAL;

 done:
        if (op_ret < 0) {
                frame->local = NULL;
                call_unwind_error (local->fopstub, -1, op_errno);
                br_stub_cleanup_local (local);
                br_stub_dealloc_local (local);
        } else {
                call_resume (local->fopstub);
        }
        return 0;
}

/**
 * Initial object versioning
 *
 * Version persists two (2) extended attributes as explained below:
 *   1. Current (ongoing) version: This is incremented on an writev ()
 *      or truncate () and is the running version for an object.
 *   2. Signing version: This is the version against which an object
 *      was signed (checksummed).
 *
 * During initial versioning, both ongoing and signing versions are
 * set of one and zero respectively. A write() call increments the
 * ongoing version as an indication of modification to the object.
 * Additionally this needs to be persisted on disk and needs to be
 * durable: fsync().. :-/
 * As an optimization only the first write() synchronizes the ongoing
 * version to disk, subsequent write()s before the *last* release()
 * are no-op's.
 *
 * create(), just like lookup() initializes the object versions to
 * the default. As an optimization this is not a durable operation:
 * in case of a crash, hard reboot etc.. absence of versioning xattrs
 * is ignored in scrubber along with the one time crawler explicitly
 * triggering signing for such objects.
 *
 * c.f. br_stub_writev() / br_stub_truncate()
 */

/**
 * perform full or incremental versioning on an inode pointd by an
 * fd. incremental versioning is done when an inode is dirty and a
 * writeback is trigerred.
 */

int
br_stub_fd_versioning (xlator_t *this, call_frame_t *frame,
                       call_stub_t *stub, dict_t *dict, fd_t *fd,
                       br_stub_version_cbk *callback, unsigned long memversion,
                       int versioningtype, int durable)
{
        int32_t          ret   = -1;
        int              flags = 0;
        dict_t          *xdata = NULL;
        br_stub_local_t *local = NULL;

        xdata = dict_new ();
        if (!xdata)
                goto done;

        ret = dict_set_int32 (xdata, GLUSTERFS_INTERNAL_FOP_KEY, 1);
        if (ret)
                goto dealloc_xdata;

        if (durable) {
                ret = dict_set_int32 (xdata, GLUSTERFS_DURABLE_OP, 0);
                if (ret)
                        goto dealloc_xdata;
        }

        local = frame->local;

        br_stub_fill_local (local, stub, fd,
                            fd->inode, fd->inode->gfid,
                            versioningtype, memversion);

        STACK_WIND (frame, callback,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->fsetxattr,
                    fd, dict, flags, xdata);

        ret = 0;

 dealloc_xdata:
        dict_unref (xdata);
 done:
        return ret;
}

static int
br_stub_perform_incversioning (xlator_t *this,
                               call_frame_t *frame, call_stub_t *stub,
                               fd_t *fd, br_stub_inode_ctx_t *ctx)
{
        int32_t          ret               = -1;
        dict_t          *dict              = NULL;
        br_version_t    *obuf              = NULL;
        unsigned long    writeback_version = 0;
        int              op_errno          = 0;
        br_stub_local_t *local             = NULL;

        op_errno = EINVAL;
        local = frame->local;

        writeback_version = __br_stub_writeback_version (ctx);

        op_errno = ENOMEM;
        dict = dict_new ();
        if (!dict)
                goto done;
        ret = br_stub_alloc_versions (&obuf, NULL, 0);
        if (ret)
                goto dealloc_dict;
        ret = br_stub_prepare_version_request (this, dict,
                                               obuf, writeback_version);
        if (ret)
                goto dealloc_versions;

        ret = br_stub_fd_versioning
                (this, frame, stub, dict,
                 fd, br_stub_fd_incversioning_cbk, writeback_version,
                 BR_STUB_INCREMENTAL_VERSIONING, !WRITEBACK_DURABLE);

 dealloc_versions:
        br_stub_dealloc_versions (obuf);
 dealloc_dict:
        dict_unref (dict);
 done:
        if (ret) {
                if (local)
                        frame->local = NULL;
                call_unwind_error (stub, -1, op_errno);
                if (local) {
                        br_stub_cleanup_local (local);
                        br_stub_dealloc_local (local);
                }
        }

        return ret;
}

/** {{{ */

/* fsetxattr() */

int32_t
br_stub_perform_objsign (call_frame_t *frame, xlator_t *this,
                         fd_t *fd, dict_t *dict, int flags, dict_t *xdata)
{
        STACK_WIND (frame, default_fsetxattr_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->fsetxattr, fd,
                    dict, flags, xdata);

        dict_unref (xdata);
        return 0;
}

void *
br_stub_signth (void *arg)
{
        xlator_t *this = arg;
        br_stub_private_t *priv = this->private;
        struct br_stub_signentry *sigstub = NULL;

        THIS = this;
        while (1) {
                pthread_mutex_lock (&priv->lock);
                {
                        while (list_empty (&priv->squeue))
                                pthread_cond_wait (&priv->cond, &priv->lock);

                        sigstub = list_first_entry
                                (&priv->squeue, struct br_stub_signentry, list);
                        list_del_init (&sigstub->list);
                }
                pthread_mutex_unlock (&priv->lock);

                call_resume (sigstub->stub);

                GF_FREE (sigstub);
        }

        return NULL;
}

int
orderq (struct list_head *elem1, struct list_head *elem2)
{
        struct br_stub_signentry *s1 = NULL;
        struct br_stub_signentry *s2 = NULL;

        s1 = list_entry (elem1, struct br_stub_signentry, list);
        s2 = list_entry (elem2, struct br_stub_signentry, list);

        return (s1->v > s2->v);
}

static int
br_stub_compare_sign_version (xlator_t *this,
                              inode_t *inode,
                              br_signature_t *sbuf,
                              dict_t *dict, int *fakesuccess)
{
        int32_t              ret     = -1;
        uint64_t             tmp_ctx = 0;
        gf_boolean_t         invalid = _gf_false;
        br_stub_inode_ctx_t *ctx     = NULL;

        GF_VALIDATE_OR_GOTO ("bit-rot-stub", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);
        GF_VALIDATE_OR_GOTO (this->name, sbuf, out);
        GF_VALIDATE_OR_GOTO (this->name, dict, out);

        ret = br_stub_get_inode_ctx (this, inode, &tmp_ctx);
        if (ret) {
                dict_del (dict, BITROT_SIGNING_VERSION_KEY);
                goto out;
        }

        ctx = (br_stub_inode_ctx_t *)(long)tmp_ctx;

        LOCK (&inode->lock);
        {
                if (ctx->currentversion < sbuf->signedversion) {
                        invalid = _gf_true;
                } else if (ctx->currentversion > sbuf->signedversion) {
                        gf_msg_debug (this->name, 0, "\"Signing version\" "
                                      "(%lu) lower than \"Current version \" "
                                      "(%lu)", ctx->currentversion,
                                      sbuf->signedversion);
                        *fakesuccess = 1;
                }
        }
        UNLOCK (&inode->lock);

        if (invalid) {
                ret = -1;
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        BRS_MSG_SIGN_VERSION_ERROR, "Signing version exceeds "
                        "current version [%lu > %lu]", sbuf->signedversion,
                        ctx->currentversion);
        }

 out:
        return ret;
}

static int
br_stub_prepare_signature (xlator_t *this,
                           dict_t *dict, inode_t *inode,
                           br_isignature_t *sign, int *fakesuccess)
{
        int32_t ret = 0;
        size_t signaturelen = 0;
        br_signature_t *sbuf = NULL;

        if (!br_is_signature_type_valid (sign->signaturetype))
                goto error_return;

        signaturelen = sign->signaturelen;
        ret = br_stub_alloc_versions (NULL, &sbuf, signaturelen);
        if (ret)
                goto error_return;
        ret = br_stub_prepare_signing_request (dict, sbuf, sign, signaturelen);
        if (ret)
                goto dealloc_versions;

        ret = br_stub_compare_sign_version (this, inode,
                                            sbuf, dict, fakesuccess);
        if (ret)
                goto dealloc_versions;

        return 0;

 dealloc_versions:
        br_stub_dealloc_versions (sbuf);
 error_return:
        return -1;
}

static void
br_stub_handle_object_signature (call_frame_t *frame,
                                 xlator_t *this, fd_t *fd, dict_t *dict,
                                 br_isignature_t *sign, dict_t *xdata)
{
        int32_t                   ret         = -1;
        int32_t                   op_ret      = -1;
        int32_t                   op_errno    = EINVAL;
        int                       fakesuccess = 0;
        br_stub_private_t        *priv        = NULL;
        struct br_stub_signentry *sigstub     = NULL;

        priv = this->private;

        if (frame->root->pid != GF_CLIENT_PID_BITD)
                goto dofop;

        ret = br_stub_prepare_signature (this, dict,
                                         fd->inode, sign, &fakesuccess);
        if (ret)
                goto dofop;
        if (fakesuccess) {
                op_ret = op_errno = 0;
                goto dofop;
        }

        dict_del (dict, GLUSTERFS_SET_OBJECT_SIGNATURE);

        ret = -1;
        if (!xdata) {
                xdata = dict_new ();
                if (!xdata)
                        goto dofop;
        } else {
                dict_ref (xdata);
        }

        ret = dict_set_int32 (xdata, GLUSTERFS_DURABLE_OP, 0);
        if (ret)
                goto unref_dict;

        /* prepare dispatch stub to order object signing */
        sigstub = GF_CALLOC (1, sizeof (*sigstub), gf_br_stub_mt_sigstub_t);
        if (!sigstub)
                goto unref_dict;

        INIT_LIST_HEAD (&sigstub->list);
        sigstub->v = ntohl (sign->signedversion);
        sigstub->stub = fop_fsetxattr_stub (frame, br_stub_perform_objsign,
                                            fd, dict, 0, xdata);
        if (!sigstub->stub)
                goto cleanup_stub;

        pthread_mutex_lock (&priv->lock);
        {
                list_add_order (&sigstub->list, &priv->squeue, orderq);
                pthread_cond_signal (&priv->cond);
        }
        pthread_mutex_unlock (&priv->lock);

        return;

 cleanup_stub:
        GF_FREE (sigstub);
 unref_dict:
        dict_unref (xdata);
 dofop:
        STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno, NULL);
}

int32_t
br_stub_fsetxattr_resume (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        int32_t ret = -1;
        br_stub_local_t *local = NULL;

        local = frame->local;
        frame->local = NULL;

        ret = br_stub_mark_inode_modified (this, local);
        if (ret) {
                op_ret = -1;
                op_errno = EINVAL;
        }

        STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno, xdata);

        br_stub_cleanup_local (local);
        br_stub_dealloc_local (local);

        return 0;
}

/**
 * Handles object reopens. Object reopens can be of 3 types. 2 are from
 * oneshot crawler and 1 from the regular signer.
 * ONESHOT CRAWLER:
 * For those objects which were created before bitrot was enabled. oneshow
 * crawler crawls the namespace and signs all the objects. It has to do
 * the versioning before making bit-rot-stub send a sign notification.
 * So it sends fsetxattr with BR_OBJECT_REOPEN as the value. And bit-rot-stub
 * upon getting BR_OBJECT_REOPEN value checks if the version has to be
 * increased or not. By default the version will be increased. But if the
 * object is modified before BR_OBJECT_REOPEN from oneshot crawler, then
 * versioning need not be done. In that case simply a success is returned.
 * SIGNER:
 * Signer wait for 2 minutes upon getting the notification from bit-rot-stub
 * and then it sends a dummy write (in reality a fsetxattr) call, to change
 * the state of the inode from REOPEN_WAIT to SIGN_QUICK. The funny part here
 * is though the inode's state is REOPEN_WAIT, the call sent by signer is
 * BR_OBJECT_RESIGN. Once the state is changed to SIGN_QUICK, then yet another
 * notification is sent upon release (RESIGN would have happened via fsetxattr,
 * so a fd is needed) and the object is signed truly this time.
 * There is a challenge in the above RESIGN method by signer. After sending
 * the 1st notification, the inode could be forgotten before RESIGN request
 * is received. In that case, the inode's context (the newly looked up inode)
 * would not indicate the inode as being modified (it would be in the default
 * state) and because of this, a SIGN_QUICK notification to truly sign the
 * object would not be sent. So, this is how its handled.
 * if (request == RESIGN) {
 *    if (inode->sign_info == NORMAL) {
 *        mark_inode_non_dirty;
 *        mark_inode_modified;
 *    }
 *    GOBACK (means unwind without doing versioning)
 * }
 */
static void
br_stub_handle_object_reopen (call_frame_t *frame,
                              xlator_t *this, fd_t *fd, uint32_t val)
{
        int32_t              ret         = -1;
        int32_t              op_ret      = -1;
        int32_t              op_errno    = EINVAL;
        call_stub_t         *stub        = NULL;
        gf_boolean_t         inc_version = _gf_false;
        gf_boolean_t         modified    = _gf_false;
        br_stub_inode_ctx_t *ctx         = NULL;
        br_stub_local_t     *local       = NULL;
        gf_boolean_t         goback      = _gf_true;

        ret = br_stub_need_versioning (this, fd, &inc_version, &modified, &ctx);
        if (ret)
                goto unwind;

        LOCK (&fd->inode->lock);
        {
                if ((val == BR_OBJECT_REOPEN) && inc_version)
                        goback = _gf_false;
                if (val == BR_OBJECT_RESIGN &&
                    ctx->info_sign == BR_SIGN_NORMAL) {
                        __br_stub_mark_inode_synced (ctx);
                        __br_stub_set_inode_modified (ctx);
                }
                (void) __br_stub_inode_sign_state (ctx, GF_FOP_FSETXATTR, fd);
        }
        UNLOCK (&fd->inode->lock);

        if (goback) {
                op_ret = op_errno = 0;
                goto unwind;
        }

        ret = br_stub_versioning_prep (frame, this, fd, ctx);
        if (ret)
                goto unwind;
        local = frame->local;

        stub = fop_fsetxattr_cbk_stub (frame, br_stub_fsetxattr_resume,
                                       0, 0, NULL);
        if (!stub) {
                gf_msg (this->name, GF_LOG_ERROR, 0, BRS_MSG_STUB_ALLOC_FAILED,
                        "failed to allocate stub for fsetxattr fop (gfid: %s),"
                        " unwinding", uuid_utoa (fd->inode->gfid));
                goto cleanup_local;
        }

        (void) br_stub_perform_incversioning (this, frame, stub, fd, ctx);
        return;

 cleanup_local:
        br_stub_cleanup_local (local);
        br_stub_dealloc_local (local);

 unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno, NULL);
}

/**
 * This function only handles bad file identification. Instead of checking in
 * fops like open, readv, writev whether the object is bad or not by doing
 * getxattr calls, better to catch them when scrubber marks it as bad.
 * So this callback is called only when the fsetxattr is sent by the scrubber
 * to mark the object as bad.
 */
int
br_stub_fsetxattr_bad_object_cbk (call_frame_t *frame, void *cookie,
                                  xlator_t *this, int32_t op_ret,
                                  int32_t op_errno, dict_t *xdata)
{
        br_stub_local_t *local = NULL;
        int32_t          ret   = -1;

        local = frame->local;
        frame->local = NULL;

        if (op_ret < 0)
                goto unwind;

        /*
         * What to do if marking the object as bad fails? (i.e. in memory
         * marking within the inode context. If we are here means fsetxattr
         * fop has succeeded on disk and the bad object xattr has been set).
         * We can return failure to scruber, but there is nothing the scrubber
         * can do with it (it might assume that the on disk setxattr itself has
         * failed). The main purpose of this operation is to help identify the
         * bad object by checking the inode context itself (thus avoiding the
         * necessity of doing a getxattr fop on the disk).
         *
         * So as of now, success itself is being returned even though inode
         * context set operation fails.
         * In future if there is any change in the policy which can handle this,
         * then appropriate response should be sent (i.e. success or error).
         */
        ret = br_stub_mark_object_bad (this, local->u.context.inode);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0, BRS_MSG_BAD_OBJ_MARK_FAIL,
                        "failed to mark object %s as bad",
                        uuid_utoa (local->u.context.inode->gfid));

        ret = br_stub_add (this, local->u.context.inode->gfid);

unwind:
        STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno, xdata);
        br_stub_cleanup_local (local);
        br_stub_dealloc_local (local);
        return 0;
}

static int32_t
br_stub_handle_bad_object_key (call_frame_t *frame, xlator_t *this, fd_t *fd,
                               dict_t *dict, int flags, dict_t *xdata)
{
        br_stub_local_t *local    = NULL;
        int32_t          op_ret   = -1;
        int32_t         op_errno = EINVAL;

        if (frame->root->pid != GF_CLIENT_PID_SCRUB) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        BRS_MSG_NON_SCRUB_BAD_OBJ_MARK, "bad object marking "
                        "on %s is not from the scrubber",
                        uuid_utoa (fd->inode->gfid));
                goto unwind;
        }

        local = br_stub_alloc_local (this);
        if (!local) {
                gf_msg (this->name, GF_LOG_ERROR, 0, BRS_MSG_NO_MEMORY,
                        "failed to allocate memory for fsetxattr on %s",
                        uuid_utoa (fd->inode->gfid));
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        br_stub_fill_local (local, NULL, fd, fd->inode,
                            fd->inode->gfid, BR_STUB_NO_VERSIONING, 0);
        frame->local = local;

        STACK_WIND (frame, br_stub_fsetxattr_bad_object_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsetxattr, fd, dict, flags,
                    xdata);
        return 0;
unwind:
        STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno, NULL);
        return 0;
}


/**
 * As of now, versioning is done by the stub (though as a setxattr
 * operation) as part of inode modification operations such as writev,
 * truncate, ftruncate. And signing is done by BitD by a fsetxattr call.
 * So any kind of setxattr coming on the versioning and the signing xattr is
 * not allowed (i.e. BITROT_CURRENT_VERSION_KEY and BITROT_SIGNING_VERSION_KEY).
 * In future if BitD/scrubber are allowed to change the versioning
 * xattrs (though I cannot see a reason for it as of now), then the below
 * function can be modified to block setxattr on version for only applications.
 *
 * NOTE: BitD sends sign request on GLUSTERFS_SET_OBJECT_SIGNATURE key.
 *       BITROT_SIGNING_VERSION_KEY is the xattr used to save the signature.
 *
 */
static int32_t
br_stub_handle_internal_xattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                               char *key)
{
        int32_t          op_ret   = -1;
        int32_t          op_errno = EINVAL;

        gf_msg (this->name, GF_LOG_ERROR, 0,
                BRS_MSG_SET_INTERNAL_XATTR, "setxattr called"
                " on the internal xattr %s for inode %s", key,
                uuid_utoa (fd->inode->gfid));

        STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno, NULL);
        return 0;
}

int
br_stub_fsetxattr (call_frame_t *frame, xlator_t *this,
                   fd_t *fd, dict_t *dict, int flags, dict_t *xdata)
{
        int32_t              ret      = 0;
        uint32_t             val      = 0;
        br_isignature_t     *sign     = NULL;

        if (!IA_ISREG (fd->inode->ia_type))
                goto wind;

        /* object signature request */
        ret = dict_get_bin (dict, GLUSTERFS_SET_OBJECT_SIGNATURE,
                            (void **) &sign);
        if (!ret) {
                br_stub_handle_object_signature (frame, this,
                                                 fd, dict, sign, xdata);
                goto done;
        }

        /* signing xattr */
        if (dict_get(dict, BITROT_SIGNING_VERSION_KEY)) {
                br_stub_handle_internal_xattr (frame, this, fd,
                                               BITROT_SIGNING_VERSION_KEY);
                goto done;
        }

        /* version xattr */
        if (dict_get(dict, BITROT_CURRENT_VERSION_KEY)) {
                br_stub_handle_internal_xattr (frame, this, fd,
                                               BITROT_CURRENT_VERSION_KEY);
                goto done;
        }

        if (dict_get (dict, GLUSTERFS_GET_OBJECT_SIGNATURE)) {
                br_stub_handle_internal_xattr (frame, this, fd,
                                               GLUSTERFS_GET_OBJECT_SIGNATURE);
                goto done;
        }

        /* object reopen request */
        ret = dict_get_uint32 (dict, BR_REOPEN_SIGN_HINT_KEY, &val);
        if (!ret) {
                br_stub_handle_object_reopen (frame, this, fd, val);
                goto done;
        }

        /* handle bad object */
        if (dict_get (dict, BITROT_OBJECT_BAD_KEY)) {
                br_stub_handle_bad_object_key (frame, this, fd,
                                               dict, flags, xdata);
                goto done;
        }

wind:
        STACK_WIND (frame, default_fsetxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsetxattr, fd, dict, flags,
                    xdata);
done:
        return 0;
}


/**
 * Currently BitD and scrubber are doing fsetxattr to either sign the object
 * or to mark it as bad. Hence setxattr on any of those keys is denied directly
 * without checking from where the fop is coming.
 * Later, if BitD or Scrubber does setxattr of those keys, then appropriate
 * check has to be added below.
 */
int
br_stub_setxattr (call_frame_t *frame, xlator_t *this,
                  loc_t *loc, dict_t *dict, int flags, dict_t *xdata)
{
        int32_t  op_ret                    = -1;
        int32_t  op_errno                  = EINVAL;
        char     dump[64*1024]             = {0,};
        char    *format                    = "(%s:%s)";

        if (dict_get (dict, GLUSTERFS_SET_OBJECT_SIGNATURE) ||
            dict_get (dict, GLUSTERFS_GET_OBJECT_SIGNATURE) ||
            dict_get (dict, BR_REOPEN_SIGN_HINT_KEY) ||
            dict_get (dict, BITROT_OBJECT_BAD_KEY) ||
            dict_get (dict, BITROT_SIGNING_VERSION_KEY) ||
            dict_get (dict, BITROT_CURRENT_VERSION_KEY)) {
                dict_dump_to_str (dict, dump, sizeof(dump), format);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        BRS_MSG_SET_INTERNAL_XATTR, "setxattr called on "
                        "internal xattr %s", dump);
                goto unwind;
        }


        STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                         FIRST_CHILD (this)->fops->setxattr, loc, dict, flags,
                         xdata);
        return 0;
unwind:
        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno, NULL);
        return 0;
}

/** }}} */


/** {{{ */

/* {f}removexattr() */

int32_t
br_stub_removexattr (call_frame_t *frame, xlator_t *this,
                     loc_t *loc, const char *name, dict_t *xdata)
{
        int32_t op_ret    = -1;
        int32_t op_errno  = EINVAL;

        if (!strcmp (BITROT_OBJECT_BAD_KEY, name) ||
            !strcmp (BITROT_SIGNING_VERSION_KEY, name) ||
            !strcmp (BITROT_CURRENT_VERSION_KEY, name)) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        BRS_MSG_REMOVE_INTERNAL_XATTR, "removexattr called"
                        " on internal xattr %s for file %s", name, loc->path);
                goto unwind;
        }


        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->removexattr,
                         loc, name, xdata);
        return 0;
unwind:
        STACK_UNWIND_STRICT (removexattr, frame, op_ret, op_errno, NULL);
        return 0;
}

int32_t
br_stub_fremovexattr (call_frame_t *frame, xlator_t *this,
                      fd_t *fd, const char *name, dict_t *xdata)
{
        int32_t op_ret    = -1;
        int32_t op_errno  = EINVAL;

        if (!strcmp (BITROT_OBJECT_BAD_KEY, name) ||
            !strcmp (BITROT_SIGNING_VERSION_KEY, name) ||
            !strcmp (BITROT_CURRENT_VERSION_KEY, name)) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        BRS_MSG_REMOVE_INTERNAL_XATTR, "removexattr called"
                        " on internal xattr %s for inode %s", name,
                        uuid_utoa (fd->inode->gfid));
                goto unwind;
        }


        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->fremovexattr,
                         fd, name, xdata);
        return 0;
unwind:
        STACK_UNWIND_STRICT (fremovexattr, frame, op_ret, op_errno, NULL);
        return 0;
}

/** }}} */

/** {{{ */

/* {f}getxattr() */

int
br_stub_listxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int op_ret, int op_errno, dict_t *xattr, dict_t *xdata)
{
        if (op_ret < 0)
                goto unwind;

        br_stub_remove_vxattrs (xattr);

 unwind:
        STACK_UNWIND (frame, op_ret, op_errno, xattr, xdata);
        return 0;
}

/**
 * ONE SHOT CRAWLER from BitD signs the objects that it encounters while
 * crawling, if the object is identified as stale by the stub. Stub follows
 * the below logic to mark an object as stale or not.
 * If the ongoing version and the signed_version match, then the object is not
 * stale. Just return. Otherwise if they does not match, then it means one
 * of the below things.
 * 1) If the inode does not need write back of the version and the sign state is
 *    is NORMAL, then some active i/o is going on the object. So skip it.
 *    A notification will be sent to trigger the sign once the release is
 *    received on the object.
 * 2) If inode does not need writeback of the version and the sign state is
 *    either reopen wait or quick sign, then it means:
 *    A) BitD restarted and it is not sure whether the object it encountered
 *       while crawling is in its timer wheel or not. Since there is no way to
 *       scan the timer wheel as of now, ONE SHOT CRAWLER just goes ahead and
 *       signs the object. Since the inode does not need writeback, version will
 *       not be incremented and directly the object will be signed.
 * 3) If the inode needs writeback, then it means the inode was forgotten after
 *    the versioning and it has to be signed now.
 *
 * This is the algorithm followed:
 * if (ongoing_version == signed_version); then
 *     object_is_not_stale;
 *     return;
 * else; then
 *      if (!inode_needs_writeback && inode_sign_state != NORMAL); then
 *            object_is_stale;
 *      if (inode_needs_writeback); then
 *            object_is_stale;
 *
 * For SCRUBBER, no need to check for the sign state and inode writeback.
 * If the ondisk ongoingversion and the ondisk signed version does not match,
 * then treat the object as stale.
 */
char
br_stub_is_object_stale (xlator_t *this, call_frame_t *frame, inode_t *inode,
                         br_version_t *obuf, br_signature_t *sbuf)
{
        uint64_t   ctx_addr = 0;
        br_stub_inode_ctx_t *ctx = NULL;
        int32_t  ret = -1;
        char stale = 0;

        if (obuf->ongoingversion == sbuf->signedversion)
                goto out;

        if (frame->root->pid == GF_CLIENT_PID_SCRUB) {
                stale = 1;
                goto out;
        }

        ret = br_stub_get_inode_ctx (this, inode, &ctx_addr);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        BRS_MSG_GET_INODE_CONTEXT_FAILED, "failed to get the "
                        "inode context for %s", uuid_utoa (inode->gfid));
                goto out;
        }

        ctx = (br_stub_inode_ctx_t *)(long)ctx_addr;

        LOCK (&inode->lock);
        {
                if ((!__br_stub_is_inode_dirty (ctx) &&
                     ctx->info_sign != BR_SIGN_NORMAL) ||
                    __br_stub_is_inode_dirty (ctx))
                        stale = 1;
        }
        UNLOCK (&inode->lock);

out:
        return stale;
}

int
br_stub_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, dict_t *xattr, dict_t *xdata)
{
        int32_t              ret          = 0;
        size_t               totallen     = 0;
        size_t               signaturelen = 0;
        br_version_t        *obuf         = NULL;
        br_signature_t      *sbuf         = NULL;
        br_isignature_out_t *sign         = NULL;
        br_vxattr_status_t   status;
        br_stub_local_t     *local        = NULL;
        inode_t             *inode        = NULL;
        gf_boolean_t         bad_object   = _gf_false;

        if (op_ret < 0)
                goto unwind;
        if (cookie != (void *) BR_STUB_REQUEST_COOKIE)
                goto unwind;

        local = frame->local;
        frame->local = NULL;
        inode = local->u.context.inode;

        op_ret   = -1;
        status = br_version_xattr_state (xattr, &obuf, &sbuf, &bad_object);

        op_errno = EIO;
        if (bad_object)
                goto delkeys;

        op_errno = EINVAL;
        if (status == BR_VXATTR_STATUS_INVALID)
                goto delkeys;

        op_errno = ENODATA;
        if ((status == BR_VXATTR_STATUS_MISSING)
            || (status == BR_VXATTR_STATUS_UNSIGNED))
                goto delkeys;

        /**
         * okay.. we have enough information to satisfy the request,
         * namely: version and signing extended attribute. what's
         * pending is the signature length -- that's figured out
         * indirectly via the size of the _whole_ xattr and the
         * on-disk signing xattr header size.
         */
        op_errno = EINVAL;
        ret = dict_get_uint32 (xattr, BITROT_SIGNING_XATTR_SIZE_KEY,
                               (uint32_t *)&signaturelen);
        if (ret)
                goto delkeys;

        signaturelen -= sizeof (br_signature_t);
        totallen = sizeof (br_isignature_out_t) + signaturelen;

        op_errno = ENOMEM;
        sign = GF_CALLOC (1, totallen, gf_br_stub_mt_signature_t);
        if (!sign)
                goto delkeys;

        sign->time[0] = obuf->timebuf[0];
        sign->time[1] = obuf->timebuf[1];

        /* Object's dirty state & current signed version */
        sign->version = sbuf->signedversion;
        sign->stale = br_stub_is_object_stale (this, frame, inode, obuf, sbuf);

        /* Object's signature */
        sign->signaturelen  = signaturelen;
        sign->signaturetype = sbuf->signaturetype;
        (void) memcpy (sign->signature, sbuf->signature, signaturelen);

        op_errno = EINVAL;
        ret = dict_set_bin (xattr, GLUSTERFS_GET_OBJECT_SIGNATURE,
                            (void *)sign, totallen);
        if (ret < 0) {
                GF_FREE (sign);
                goto delkeys;
        }
        op_errno = 0;
        op_ret = totallen;

 delkeys:
        br_stub_remove_vxattrs (xattr);

 unwind:
        STACK_UNWIND (frame, op_ret, op_errno, xattr, xdata);
        if (local) {
                br_stub_cleanup_local (local);
                br_stub_dealloc_local (local);
        }
        return 0;
}

static void
br_stub_send_stub_init_time (call_frame_t *frame, xlator_t *this)
{
        int op_ret = 0;
        int op_errno = 0;
        dict_t *xattr = NULL;
        br_stub_init_t stub = {{0,},};
        br_stub_private_t *priv = NULL;

        priv = this->private;

        xattr = dict_new ();
        if (!xattr) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        stub.timebuf[0] = priv->boot[0];
        stub.timebuf[1] = priv->boot[1];
        memcpy (stub.export, priv->export, strlen (priv->export) + 1);

        op_ret = dict_set_static_bin (xattr, GLUSTERFS_GET_BR_STUB_INIT_TIME,
                                      (void *) &stub, sizeof (br_stub_init_t));
        if (op_ret < 0) {
                op_errno = EINVAL;
                goto unwind;
        }

        op_ret = sizeof (br_stub_init_t);

 unwind:
        STACK_UNWIND (frame, op_ret, op_errno, xattr, NULL);

        if (xattr)
                dict_unref (xattr);
}

int
br_stub_getxattr (call_frame_t *frame, xlator_t *this,
                  loc_t *loc, const char *name, dict_t *xdata)
{
        void               *cookie   = NULL;
        uuid_t              rootgfid = {0, };
        fop_getxattr_cbk_t  cbk      = br_stub_getxattr_cbk;
        int32_t             op_ret   = -1;
        int32_t             op_errno = EINVAL;
        br_stub_local_t    *local    = NULL;

        GF_VALIDATE_OR_GOTO ("bit-rot-stub", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, unwind);

        rootgfid[15] = 1;

        if (!name) {
                cbk = br_stub_listxattr_cbk;
                goto wind;
        }

        /**
         * If xattr is node-uuid and the inode is marked bad, return EIO.
         * Returning EIO would result in AFR to choose correct node-uuid
         * coresponding to the subvolume * where the good copy of the
         * file resides.
         */
        if (IA_ISREG (loc->inode->ia_type) && XATTR_IS_NODE_UUID (name) &&
            br_stub_check_bad_object (this, loc->inode, &op_ret, &op_errno)) {
                goto unwind;
        }

        if (br_stub_is_internal_xattr (name))
                goto unwind;

        /**
         * this special extended attribute is allowed only on root
         */
        if (name
            && (strncmp (name, GLUSTERFS_GET_BR_STUB_INIT_TIME,
                          strlen (GLUSTERFS_GET_BR_STUB_INIT_TIME)) == 0)
            && ((gf_uuid_compare (loc->gfid, rootgfid) == 0)
                || (gf_uuid_compare (loc->inode->gfid, rootgfid) == 0))) {
                br_stub_send_stub_init_time (frame, this);
                return 0;
        }

        if (!IA_ISREG (loc->inode->ia_type))
                goto wind;

        if (name && (strncmp (name, GLUSTERFS_GET_OBJECT_SIGNATURE,
                              strlen (GLUSTERFS_GET_OBJECT_SIGNATURE)) == 0)) {
                cookie = (void *) BR_STUB_REQUEST_COOKIE;

                local = br_stub_alloc_local (this);
                if (!local) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto unwind;
                }

                br_stub_fill_local (local, NULL, NULL, loc->inode,
                                    loc->inode->gfid,
                                    BR_STUB_NO_VERSIONING, 0);
                frame->local = local;
        }

 wind:
        STACK_WIND_COOKIE
                      (frame, cbk, cookie, FIRST_CHILD (this),
                       FIRST_CHILD (this)->fops->getxattr, loc, name, xdata);
        return 0;
unwind:
        STACK_UNWIND (frame, op_ret, op_errno, NULL, NULL);
                return 0;
}

int
br_stub_fgetxattr (call_frame_t *frame, xlator_t *this,
                   fd_t *fd, const char *name, dict_t *xdata)
{
        void *cookie = NULL;
        uuid_t rootgfid = {0, };
        fop_fgetxattr_cbk_t cbk = br_stub_getxattr_cbk;
        int32_t op_ret = -1;
        int32_t op_errno = EINVAL;
        br_stub_local_t *local = NULL;

        rootgfid[15] = 1;

        if (!name) {
                cbk = br_stub_listxattr_cbk;
                goto wind;
        }

        /**
         * If xattr is node-uuid and the inode is marked bad, return EIO.
         * Returning EIO would result in AFR to choose correct node-uuid
         * coresponding to the subvolume * where the good copy of the
         * file resides.
         */
        if (IA_ISREG (fd->inode->ia_type) && XATTR_IS_NODE_UUID (name) &&
            br_stub_check_bad_object (this, fd->inode, &op_ret, &op_errno)) {
                goto unwind;
        }

        if (br_stub_is_internal_xattr (name))
                goto unwind;

        /**
         * this special extended attribute is allowed only on root
         */
        if (name
            && (strncmp (name, GLUSTERFS_GET_BR_STUB_INIT_TIME,
                         strlen (GLUSTERFS_GET_BR_STUB_INIT_TIME)) == 0)
            && (gf_uuid_compare (fd->inode->gfid, rootgfid) == 0)) {
                br_stub_send_stub_init_time (frame, this);
                return 0;
        }

        if (!IA_ISREG (fd->inode->ia_type))
                goto wind;

        if (name && (strncmp (name, GLUSTERFS_GET_OBJECT_SIGNATURE,
                              strlen (GLUSTERFS_GET_OBJECT_SIGNATURE)) == 0)) {
                cookie = (void *) BR_STUB_REQUEST_COOKIE;

                local = br_stub_alloc_local (this);
                if (!local) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto unwind;
                }

                br_stub_fill_local (local, NULL, fd, fd->inode,
                                    fd->inode->gfid,
                                    BR_STUB_NO_VERSIONING, 0);
                frame->local = local;
        }

 wind:
        STACK_WIND_COOKIE
                      (frame, cbk, cookie, FIRST_CHILD (this),
                       FIRST_CHILD (this)->fops->fgetxattr, fd, name, xdata);
        return 0;
unwind:
        STACK_UNWIND (frame, op_ret, op_errno, NULL, NULL);
        return 0;
}

int32_t
br_stub_readv (call_frame_t *frame, xlator_t *this,
               fd_t *fd, size_t size, off_t offset, uint32_t flags, dict_t *xdata)
{
        int32_t              op_ret   = -1;
        int32_t              op_errno = EINVAL;
        int32_t              ret      = -1;

        GF_VALIDATE_OR_GOTO ("bit-rot-stub", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, frame, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, unwind);

        ret = br_stub_check_bad_object (this, fd->inode, &op_ret, &op_errno);
        if (ret)
                goto unwind;

        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->readv, fd, size, offset,
                         flags, xdata);
        return 0;

unwind:
        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno, NULL, 0, NULL,
                             NULL, NULL);
        return 0;
}

/**
 * The first write response on the first fd in the list of fds will set
 * the flag to indicate that the inode is modified. The subsequent write
 * respnses coming on either the first fd or some other fd will not change
 * the fd. The inode-modified flag is unset only upon release of all the
 * fds.
 */
int32_t
br_stub_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                    struct iatt *postbuf, dict_t *xdata)
{
        int32_t          ret   = 0;
        br_stub_local_t *local = NULL;

        local = frame->local;
        frame->local = NULL;

        if (op_ret < 0)
                goto unwind;

        ret = br_stub_mark_inode_modified (this, local);
        if (ret) {
                op_ret = -1;
                op_errno = EINVAL;
        }

unwind:
        STACK_UNWIND_STRICT (writev, frame,
                             op_ret, op_errno, prebuf, postbuf, xdata);

        br_stub_cleanup_local (local);
        br_stub_dealloc_local (local);

        return 0;
}

int32_t
br_stub_writev_resume (call_frame_t *frame, xlator_t *this, fd_t *fd,
                       struct iovec *vector, int32_t count, off_t offset,
                       uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
        STACK_WIND (frame, br_stub_writev_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev, fd, vector, count,
                    offset, flags, iobref, xdata);
        return 0;
}

/**
 * This is probably the most crucial part about the whole versioning thing.
 * There's absolutely no differentiation as such between an anonymous fd
 * and a regular fd except the fd context initialization. Object versioning
 * is perfomed when the inode is dirty. Parallel write operations are no
 * special with each write performing object versioning followed by marking
 * the inode as non-dirty (synced). This is followed by the actual operation
 * (writev() in this case) which on a success marks the inode as modified.
 * This prevents signing of objects that have not been modified.
 */
int32_t
br_stub_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
                struct iovec *vector, int32_t count, off_t offset,
                uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
        call_stub_t         *stub        = NULL;
        int32_t              op_ret      = -1;
        int32_t              op_errno    = EINVAL;
        gf_boolean_t         inc_version = _gf_false;
        gf_boolean_t         modified    = _gf_false;
        br_stub_inode_ctx_t *ctx         = NULL;
        int32_t              ret         = -1;
        fop_writev_cbk_t     cbk         = default_writev_cbk;
        br_stub_local_t     *local       = NULL;

        GF_VALIDATE_OR_GOTO ("bit-rot-stub", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, frame, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd, unwind);

        ret = br_stub_need_versioning (this, fd, &inc_version, &modified, &ctx);
        if (ret)
                goto unwind;

        ret = br_stub_check_bad_object (this, fd->inode, &op_ret, &op_errno);
        if (ret)
                goto unwind;

        /**
         * The inode is not dirty and also witnessed atleast one successful
         * modification operation. Therefore, subsequent operations need not
         * perform any special tracking.
         */
        if (!inc_version && modified)
                goto wind;

        /**
         * okay.. so, either the inode needs versioning or the modification
         * needs to be tracked. ->cbk is set to the appropriate callback
         * routine for this.
         * NOTE: ->local needs to be deallocated on failures from here on.
         */
        ret = br_stub_versioning_prep (frame, this, fd, ctx);
        if (ret)
                goto unwind;

        local = frame->local;
        if (!inc_version) {
                br_stub_fill_local (local, NULL, fd, fd->inode,
                                    fd->inode->gfid, BR_STUB_NO_VERSIONING, 0);
                cbk = br_stub_writev_cbk;
                goto wind;
        }

        stub = fop_writev_stub (frame, br_stub_writev_resume, fd, vector, count,
                                offset, flags, iobref, xdata);

        if (!stub) {
                gf_msg (this->name, GF_LOG_ERROR, 0, BRS_MSG_STUB_ALLOC_FAILED,
                        "failed to allocate stub for write fop (gfid: %s), "
                        "unwinding", uuid_utoa (fd->inode->gfid));
                goto cleanup_local;
        }

        /* Perform Versioning */
        return br_stub_perform_incversioning (this, frame, stub, fd, ctx);

 wind:
        STACK_WIND (frame, cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev,
                    fd, vector, count, offset, flags, iobref, xdata);
        return 0;

 cleanup_local:
        br_stub_cleanup_local (local);
        br_stub_dealloc_local (local);

 unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, NULL, NULL,
                             NULL);

        return 0;
}

int32_t
br_stub_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                       struct iatt *postbuf, dict_t *xdata)
{
        int32_t          ret   = -1;
        br_stub_local_t *local = NULL;

        local = frame->local;
        frame->local = NULL;

        if (op_ret < 0)
                goto unwind;

        ret = br_stub_mark_inode_modified (this, local);
        if (ret) {
                op_ret = -1;
                op_errno = EINVAL;
        }

unwind:
        STACK_UNWIND_STRICT (ftruncate, frame,
                             op_ret, op_errno, prebuf, postbuf, xdata);

        br_stub_cleanup_local (local);
        br_stub_dealloc_local (local);

        return 0;
}

int32_t
br_stub_ftruncate_resume (call_frame_t *frame, xlator_t *this, fd_t *fd,
                          off_t offset, dict_t *xdata)
{
        STACK_WIND (frame, br_stub_ftruncate_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->ftruncate, fd, offset, xdata);
        return 0;
}

/* c.f. br_stub_writev() for explanation */
int32_t
br_stub_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd,
                   off_t offset, dict_t *xdata)
{
        br_stub_local_t     *local       = NULL;
        call_stub_t         *stub        = NULL;
        int32_t              op_ret      = -1;
        int32_t              op_errno    = EINVAL;
        gf_boolean_t         inc_version = _gf_false;
        gf_boolean_t         modified    = _gf_false;
        br_stub_inode_ctx_t *ctx         = NULL;
        int32_t              ret         = -1;
        fop_ftruncate_cbk_t  cbk         = default_ftruncate_cbk;

        GF_VALIDATE_OR_GOTO ("bit-rot-stub", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, frame, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd, unwind);

        ret = br_stub_need_versioning (this, fd, &inc_version, &modified, &ctx);
        if (ret)
                goto unwind;

        ret = br_stub_check_bad_object (this, fd->inode, &op_ret, &op_errno);
        if (ret)
                goto unwind;

        if (!inc_version && modified)
                goto wind;

        ret = br_stub_versioning_prep (frame, this, fd, ctx);
        if (ret)
                goto unwind;

        local = frame->local;
        if (!inc_version) {
                br_stub_fill_local (local, NULL, fd, fd->inode,
                                    fd->inode->gfid, BR_STUB_NO_VERSIONING, 0);
                cbk = br_stub_ftruncate_cbk;
                goto wind;
        }

        stub = fop_ftruncate_stub (frame, br_stub_ftruncate_resume, fd, offset,
                                   xdata);
        if (!stub) {
                gf_msg (this->name, GF_LOG_ERROR, 0, BRS_MSG_STUB_ALLOC_FAILED,
                        "failed to allocate stub for ftruncate fop (gfid: %s),"
                        " unwinding", uuid_utoa (fd->inode->gfid));
                goto cleanup_local;
        }

        return br_stub_perform_incversioning (this, frame, stub, fd, ctx);

 wind:
        STACK_WIND (frame, cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->ftruncate, fd, offset, xdata);
        return 0;

 cleanup_local:
        br_stub_cleanup_local (local);
        br_stub_dealloc_local (local);

 unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno, NULL, NULL,
                             NULL);

        return 0;
}

int32_t
br_stub_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                      struct iatt *postbuf, dict_t *xdata)
{
        int32_t          ret   = 0;
        br_stub_local_t *local = NULL;

        local = frame->local;
        frame->local = NULL;

        if (op_ret < 0)
                goto unwind;

        ret = br_stub_mark_inode_modified (this, local);
        if (ret) {
                op_ret = -1;
                op_errno = EINVAL;
        }

unwind:
        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno, prebuf, postbuf,
                             xdata);
        br_stub_cleanup_local (local);
        br_stub_dealloc_local (local);
        return 0;
}

int32_t
br_stub_truncate_resume (call_frame_t *frame, xlator_t *this, loc_t *loc,
                          off_t offset, dict_t *xdata)
{
        br_stub_local_t *local = frame->local;

        fd_unref (local->u.context.fd);
        STACK_WIND (frame, br_stub_ftruncate_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->truncate, loc, offset, xdata);
        return 0;
}

/**
 * Bit-rot-stub depends heavily on the fd based operations to for doing
 * versioning and sending notification. It starts tracking the operation
 * upon getting first fd based modify operation by doing versioning and
 * sends notification when last fd using which the inode was modified is
 * released.
 * But for truncate there is no fd and hence it becomes difficult to do
 * the versioning and send notification. It is handled by doing versioning
 * on an anonymous fd. The fd will be valid till the completion of the
 * truncate call. It guarantees that release on this anonymous fd will happen
 * after the truncate call and notification is sent after the truncate call.
 *
 * c.f. br_writev_cbk() for explanation
 */
int32_t
br_stub_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc,
                  off_t offset, dict_t *xdata)
{
        br_stub_local_t     *local       = NULL;
        call_stub_t         *stub        = NULL;
        int32_t              op_ret      = -1;
        int32_t              op_errno    = EINVAL;
        gf_boolean_t         inc_version = _gf_false;
        gf_boolean_t         modified    = _gf_false;
        br_stub_inode_ctx_t *ctx         = NULL;
        int32_t              ret         = -1;
        fd_t                *fd          = NULL;
        fop_truncate_cbk_t   cbk         = default_truncate_cbk;

        GF_VALIDATE_OR_GOTO ("bit-rot-stub", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, frame, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, unwind);

        fd = fd_anonymous (loc->inode);
        if (!fd) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        BRS_MSG_CREATE_ANONYMOUS_FD_FAILED, "failed to create "
                        "anonymous fd for the inode %s",
                        uuid_utoa (loc->inode->gfid));
                goto unwind;
        }

        ret = br_stub_need_versioning (this, fd, &inc_version, &modified, &ctx);
        if (ret)
                goto cleanup_fd;

        ret = br_stub_check_bad_object (this, fd->inode, &op_ret, &op_errno);
        if (ret)
                goto unwind;

        if (!inc_version && modified)
                goto wind;

        ret = br_stub_versioning_prep (frame, this, fd, ctx);
        if (ret)
                goto cleanup_fd;

        local = frame->local;
        if (!inc_version) {
                br_stub_fill_local (local, NULL, fd, fd->inode,
                                    fd->inode->gfid, BR_STUB_NO_VERSIONING, 0);
                cbk = br_stub_truncate_cbk;
                goto wind;
        }

        stub = fop_truncate_stub (frame, br_stub_truncate_resume, loc, offset,
                                  xdata);
        if (!stub) {
                gf_msg (this->name, GF_LOG_ERROR, 0, BRS_MSG_STUB_ALLOC_FAILED,
                        "failed to allocate stub for truncate fop (gfid: %s), "
                        "unwinding", uuid_utoa (fd->inode->gfid));
                goto cleanup_local;
        }

        return br_stub_perform_incversioning (this, frame, stub, fd, ctx);

 wind:
        STACK_WIND (frame, cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->truncate, loc, offset, xdata);
        fd_unref (fd);
        return 0;

 cleanup_local:
        br_stub_cleanup_local (local);
        br_stub_dealloc_local (local);
 cleanup_fd:
        fd_unref (fd);
 unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno, NULL, NULL,
                             NULL);

        return 0;
}

/** }}} */


/** {{{ */

/* open() */

/**
 * It's probably worth mentioning a bit about why some of the housekeeping
 * work is done in open() call path, rather than the callback path.
 * Two (or more) open()'s in parallel can race and lead to a situation
 * where a release() gets triggered (possibly after a series of write()
 * calls) when *other* open()'s have still not reached callback path
 * thereby having an active fd on an inode that is in process of getting
 * signed with the current version.
 *
 * Maintaining fd list in the call path ensures that a release() would
 * not be triggered if an open() call races ahead (followed by a close())
 * threby finding non-empty fd list.
 */

int
br_stub_open (call_frame_t *frame, xlator_t *this,
              loc_t *loc, int32_t flags, fd_t *fd, dict_t *xdata)
{
        int32_t              ret      = -1;
        br_stub_inode_ctx_t *ctx      = NULL;
        uint64_t             ctx_addr = 0;
        int32_t              op_ret   = -1;
        int32_t              op_errno = EINVAL;

        GF_VALIDATE_OR_GOTO ("bit-rot-stub", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, unwind);

        ret = br_stub_get_inode_ctx (this, fd->inode, &ctx_addr);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        BRS_MSG_GET_INODE_CONTEXT_FAILED, "failed to get the "
                        "inode context for the file %s (gfid: %s)", loc->path,
                        uuid_utoa (fd->inode->gfid));
                goto unwind;
        }

        ctx = (br_stub_inode_ctx_t *)(long)ctx_addr;

        ret = br_stub_check_bad_object (this, fd->inode, &op_ret, &op_errno);
        if (ret)
                goto unwind;

        if (frame->root->pid == GF_CLIENT_PID_SCRUB)
                goto wind;

        if (flags == O_RDONLY)
                goto wind;

        ret = br_stub_add_fd_to_inode (this, fd, ctx);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        BRS_MSG_ADD_FD_TO_LIST_FAILED,
                        "failed add fd to the list (gfid: %s)",
                        uuid_utoa (fd->inode->gfid));
                goto unwind;
        }

wind:
        STACK_WIND (frame, default_open_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->open, loc, flags, fd, xdata);
        return 0;
unwind:
        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, NULL, NULL);
        return 0;
}

/** }}} */


/** {{{ */

/* creat() */

/**
 * This routine registers a release callback for the given fd and adds the
 * fd to the inode context fd tracking list.
 */
int32_t
br_stub_add_fd_to_inode (xlator_t *this, fd_t *fd, br_stub_inode_ctx_t *ctx)
{
        int32_t       ret        = -1;
        br_stub_fd_t *br_stub_fd = NULL;

        ret = br_stub_require_release_call (this, fd, &br_stub_fd);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        BRS_MSG_SET_FD_CONTEXT_FAILED, "failed to set the fd "
                        "context for the file (gfid: %s)",
                        uuid_utoa (fd->inode->gfid));
                goto out;
        }

        LOCK (&fd->inode->lock);
        {
                list_add_tail (&ctx->fd_list, &br_stub_fd->list);
        }
        UNLOCK (&fd->inode->lock);

        ret = 0;

out:
        return ret;
}

int
br_stub_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno, fd_t *fd, inode_t *inode,
                    struct iatt *stbuf, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
{
        int32_t              ret      = 0;
        uint64_t             ctx_addr = 0;
        br_stub_inode_ctx_t *ctx      = NULL;
        unsigned long        version  = BITROT_DEFAULT_CURRENT_VERSION;

        if (op_ret < 0)
                goto unwind;

        ret = br_stub_get_inode_ctx (this, fd->inode, &ctx_addr);
        if (ret < 0) {
                ret = br_stub_init_inode_versions (this, fd, inode, version,
                                                   _gf_true, _gf_false);
                if (ret) {
                        op_ret = -1;
                        op_errno = EINVAL;
                }
        } else {
                ctx = (br_stub_inode_ctx_t *)(long)ctx_addr;
                ret = br_stub_add_fd_to_inode (this, fd, ctx);
        }

unwind:
        STACK_UNWIND_STRICT (create, frame, op_ret, op_errno,
                             fd, inode, stbuf, preparent, postparent, xdata);
        return 0;
}

int
br_stub_create (call_frame_t *frame,
                xlator_t *this, loc_t *loc, int32_t flags,
                mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        GF_VALIDATE_OR_GOTO ("bit-rot-stub", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, unwind);

        STACK_WIND (frame, br_stub_create_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->create,
                    loc, flags, mode, umask, fd, xdata);
        return 0;
unwind:
        STACK_UNWIND_STRICT (create, frame, -1, EINVAL, NULL, NULL, NULL, NULL,
                             NULL, NULL);
        return 0;
}

int
br_stub_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int op_ret, int op_errno, inode_t *inode,
                    struct iatt *stbuf, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
{
        int32_t              ret      = -1;
        unsigned long        version  = BITROT_DEFAULT_CURRENT_VERSION;

        if (op_ret < 0)
                goto unwind;

        ret = br_stub_init_inode_versions (this, NULL, inode, version,
                                           _gf_true, _gf_false);
        /**
         * Like lookup, if init_inode_versions fail, return EINVAL
         */
        if (ret) {
                op_ret = -1;
                op_errno = EINVAL;
        }

unwind:
        STACK_UNWIND_STRICT (mknod, frame, op_ret, op_errno,
                             inode, stbuf, preparent, postparent, xdata);
        return 0;
}

int
br_stub_mknod (call_frame_t *frame, xlator_t *this,
                loc_t *loc, mode_t mode, dev_t dev, mode_t umask, dict_t *xdata)
{
        GF_VALIDATE_OR_GOTO ("bit-rot-stub", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, unwind);

        STACK_WIND (frame, br_stub_mknod_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->mknod,
                    loc, mode, dev, umask, xdata);
        return 0;
unwind:
        STACK_UNWIND_STRICT (mknod, frame, -1, EINVAL, NULL, NULL, NULL,
                             NULL, NULL);
        return 0;
}

/** }}} */

/**
 * As of now, only lookup searches for bad object xattr and marks the
 * object as bad in its inode context if the xattr is present. But there
 * is a possibility that, at the time of the lookup the object was not
 * marked bad (i.e. bad object xattr was not set), and later its marked
 * as bad. In this case, object is not bad, so when a fop such as open or
 * readv or writev comes on the object, the fop will be sent downward instead
 * of sending as error upwards.
 * The solution for this is to do a getxattr for the below list of fops.
 * lookup, readdirp, open, readv, writev.
 * But doing getxattr for each of the above fops might be costly.
 * So another method followed is to catch the bad file marking by the scrubber
 * and set that info within the object's inode context. In this way getxattr
 * calls can be avoided and bad objects can be caught instantly. Fetching the
 * xattr is needed only in lookups when there is a brick restart or inode
 * forget.
 *
 * If the dict (@xattr) is NULL, then how should that be handled? Fail the
 * lookup operation? Or let it continue with version being initialized to
 * BITROT_DEFAULT_CURRENT_VERSION. But what if the version was different
 * on disk (and also a right signature was there), but posix failed to
 * successfully allocate the dict? Posix does not treat call back xdata
 * creattion failure as the lookup failure.
 */
static int32_t
br_stub_lookup_version (xlator_t *this,
                        uuid_t gfid, inode_t *inode, dict_t *xattr)
{
        unsigned long       version = 0;
        br_version_t       *obuf    = NULL;
        br_signature_t     *sbuf    = NULL;
        br_vxattr_status_t  status;
        gf_boolean_t        bad_object = _gf_false;

        /**
         * versioning xattrs were requested from POSIX. if available, figure
         * out the correct version to use in the inode context (start with
         * the default version if unavailable). As of now versions are not
         * persisted on-disk. The inode is marked dirty, so that the first
         * operation (such as write(), etc..) triggers synchronization to
         * disk.
         */
        status = br_version_xattr_state (xattr, &obuf, &sbuf, &bad_object);
        version = ((status == BR_VXATTR_STATUS_FULL)
                   || (status == BR_VXATTR_STATUS_UNSIGNED))
                        ? obuf->ongoingversion : BITROT_DEFAULT_CURRENT_VERSION;

        /**
         * If signature is there, but version is not therem then that status is
         * is treated as INVALID. So in that case, we should not initialize the
         * inode context with wrong version names etc.
         */
        if (status == BR_VXATTR_STATUS_INVALID)
                return -1;

        return br_stub_init_inode_versions (this, NULL, inode, version,
                                            _gf_true, bad_object);
}


/** {{{ */

int32_t
br_stub_opendir (call_frame_t *frame, xlator_t *this,
               loc_t *loc, fd_t *fd, dict_t *xdata)
{
        br_stub_private_t    *priv = NULL;
        br_stub_fd_t         *fd_ctx = NULL;
        int32_t               op_ret = -1;
        int32_t               op_errno = EINVAL;

        priv = this->private;
        if (gf_uuid_compare (fd->inode->gfid, priv->bad_object_dir_gfid))
                goto normal;

        fd_ctx = br_stub_fd_new ();
        if (!fd_ctx) {
                op_errno = ENOMEM;
                goto unwind;
        }

        fd_ctx->bad_object.dir_eof = -1;
        fd_ctx->bad_object.dir = sys_opendir (priv->stub_basepath);
        if (!fd_ctx->bad_object.dir) {
                op_errno = errno;
                goto err_freectx;
        }

        op_ret = br_stub_fd_ctx_set (this, fd, fd_ctx);
        if (!op_ret)
                goto unwind;

        sys_closedir (fd_ctx->bad_object.dir);

err_freectx:
        GF_FREE (fd_ctx);
unwind:
        STACK_UNWIND_STRICT (opendir, frame, op_ret, op_errno, fd, NULL);
        return 0;

normal:
        STACK_WIND (frame, default_opendir_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->opendir, loc, fd, xdata);
        return 0;
}

int32_t
br_stub_readdir (call_frame_t *frame, xlator_t *this,
               fd_t *fd, size_t size, off_t off, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        br_stub_private_t    *priv = NULL;

        priv = this->private;
        if (gf_uuid_compare (fd->inode->gfid, priv->bad_object_dir_gfid))
                goto out;
        stub = fop_readdir_stub (frame, br_stub_readdir_wrapper, fd, size, off,
                                 xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (readdir, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }
        br_stub_worker_enqueue (this, stub);
        return 0;
out:
        STACK_WIND (frame, default_readdir_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdir, fd, size, off, xdata);
        return 0;
}

int
br_stub_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, gf_dirent_t *entries,
                      dict_t *dict)
{
        int32_t      ret     = 0;
        uint64_t     ctxaddr = 0;
        gf_dirent_t *entry   = NULL;

        if (op_ret < 0)
                goto unwind;

        list_for_each_entry (entry, &entries->list, list) {
                if ((strcmp (entry->d_name, ".") == 0)
                    || (strcmp (entry->d_name, "..") == 0))
                        continue;

                if (!IA_ISREG (entry->d_stat.ia_type))
                        continue;

                ret = br_stub_get_inode_ctx (this, entry->inode, &ctxaddr);
                if (ret < 0)
                        ctxaddr = 0;
                if (ctxaddr) { /* already has the context */
                        br_stub_remove_vxattrs (entry->dict);
                        continue;
                }

                ret = br_stub_lookup_version
                        (this, entry->inode->gfid, entry->inode, entry->dict);
                br_stub_remove_vxattrs (entry->dict);
                if (ret) {
                        /**
                         * there's no per-file granularity support in case of
                         * failure. let's fail the entire request for now..
                         */
                        break;
                }
        }

        if (ret) {
                op_ret   = -1;
                op_errno = EINVAL;
        }

 unwind:
        STACK_UNWIND_STRICT (readdirp, frame, op_ret, op_errno, entries, dict);

        return 0;
}

int
br_stub_readdirp (call_frame_t *frame, xlator_t *this,
                  fd_t *fd, size_t size, off_t offset, dict_t *dict)
{
        int32_t ret = -1;
        int op_errno = 0;
        gf_boolean_t xref = _gf_false;

        op_errno = ENOMEM;
        if (!dict) {
                dict = dict_new ();
                if (!dict)
                        goto unwind;
        } else {
                dict = dict_ref (dict);
        }

        xref = _gf_true;

        op_errno = EINVAL;
        ret = dict_set_uint32 (dict, BITROT_CURRENT_VERSION_KEY, 0);
        if (ret)
                goto unwind;
        ret = dict_set_uint32 (dict, BITROT_SIGNING_VERSION_KEY, 0);
        if (ret)
                goto unwind;
        ret = dict_set_uint32 (dict, BITROT_OBJECT_BAD_KEY, 0);
        if (ret)
                goto unwind;

        STACK_WIND (frame, br_stub_readdirp_cbk, FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->readdirp, fd, size,
                    offset, dict);
        goto unref_dict;

 unwind:
        STACK_UNWIND_STRICT (readdirp, frame, -1, op_errno, NULL, NULL);
        return 0;

 unref_dict:
        if (xref)
                dict_unref (dict);
        return 0;
}

/** }}} */


/** {{{ */

/* lookup() */

/**
 * This function mainly handles the ENOENT error for the bad objects. Though
 * br_stub_forget () handles removal of the link for the bad object from the
 * quarantine directory, its better to handle it in lookup as well, where
 * a failed lookup on a bad object with ENOENT, will trigger deletion of the
 * link for the bad object from quarantine directory. So whoever comes first
 * either forget () or lookup () will take care of removing the link.
 */
void
br_stub_handle_lookup_error (xlator_t *this, inode_t *inode, int32_t op_errno)
{
        int32_t     ret = -1;
        uint64_t   ctx_addr = 0;
        br_stub_inode_ctx_t *ctx = NULL;

        if (op_errno != ENOENT)
                goto out;

        if (!inode_is_linked (inode))
                goto out;

        ret = br_stub_get_inode_ctx (this, inode, &ctx_addr);
        if (ret)
                goto out;

        ctx = (br_stub_inode_ctx_t *)(long)ctx_addr;

        LOCK (&inode->lock);
        {
                if (__br_stub_is_bad_object (ctx))
                        (void) br_stub_del (this, inode->gfid);
        }
        UNLOCK (&inode->lock);

        if (__br_stub_is_bad_object (ctx)) {
                /* File is not present, might be deleted for recovery,
                 * del the bitrot inode context
                 */
                ctx_addr = 0;
                inode_ctx_del (inode, this, &ctx_addr);
                if (ctx_addr) {
                        ctx = (br_stub_inode_ctx_t *)(long)ctx_addr;
                        GF_FREE (ctx);
                }
        }

out:
        return;
}

int
br_stub_lookup_cbk (call_frame_t *frame, void *cookie,
                    xlator_t *this, int op_ret, int op_errno, inode_t *inode,
                    struct iatt *stbuf, dict_t *xattr, struct iatt *postparent)
{
        int32_t ret = 0;

        if (op_ret < 0) {
                (void) br_stub_handle_lookup_error (this, inode, op_errno);
                goto unwind;
        }

        if (!IA_ISREG (stbuf->ia_type))
                goto unwind;

        /**
         * If the object is bad, then "bad inode" marker has to be sent back
         * in resoinse, for revalidated lookups as well. Some xlators such as
         * quick-read might cache the data in revalidated lookup as fresh
         * lookup would anyway have sent "bad inode" marker.
         * In general send bad inode marker for every lookup operation on the
         * bad object.
         */
        if (cookie != (void *) BR_STUB_REQUEST_COOKIE) {
                ret =  br_stub_mark_xdata_bad_object (this, inode, xattr);
                if (ret) {
                        op_ret = -1;
                        op_errno = EIO;
                        goto unwind;
                }

                goto delkey;
        }

        ret = br_stub_lookup_version (this, stbuf->ia_gfid, inode, xattr);
        if (ret < 0) {
                op_ret   = -1;
                op_errno = EINVAL;
                goto delkey;
        }

        /**
         * If the object is bad, send "bad inode" marker back in response
         * for xlator(s) to act accordingly (such as quick-read, etc..)
         */
        ret = br_stub_mark_xdata_bad_object (this, inode, xattr);
        if (ret) {
                /**
                 * aaha! bad object, but sorry we would not
                 * satisfy the request on allocation failures.
                 */
                op_ret = -1;
                op_errno = EIO;
                goto unwind;
        }

delkey:
        br_stub_remove_vxattrs (xattr);
unwind:
        STACK_UNWIND_STRICT (lookup, frame,
                             op_ret, op_errno, inode, stbuf, xattr, postparent);

        return 0;
}

int
br_stub_lookup (call_frame_t *frame,
                xlator_t *this, loc_t *loc, dict_t *xdata)
{
        int32_t ret = 0;
        int op_errno = 0;
        void *cookie = NULL;
        uint64_t ctx_addr = 0;
        gf_boolean_t xref = _gf_false;
        br_stub_private_t *priv = NULL;
        call_stub_t       *stub = NULL;

        GF_VALIDATE_OR_GOTO ("bit-rot-stub", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, unwind);

        priv = this->private;

        if (!gf_uuid_compare (loc->gfid, priv->bad_object_dir_gfid) ||
            !gf_uuid_compare (loc->pargfid, priv->bad_object_dir_gfid)) {

                stub = fop_lookup_stub (frame, br_stub_lookup_wrapper, loc,
                                        xdata);
                if (!stub) {
                        op_errno = ENOMEM;
                        goto unwind;
                }
                br_stub_worker_enqueue (this, stub);
                return 0;
        }

        ret = br_stub_get_inode_ctx (this, loc->inode, &ctx_addr);
        if (ret < 0)
                ctx_addr = 0;
        if (ctx_addr != 0)
                goto wind;

        /**
         * fresh lookup: request version keys from POSIX
         */
        op_errno = ENOMEM;
        if (!xdata) {
                xdata = dict_new ();
                if (!xdata)
                        goto unwind;
        } else {
                xdata = dict_ref (xdata);
        }

        xref = _gf_true;

        /**
         * Requesting both xattrs provides a way of sanity checking the
         * object. Anomaly checking is done in cbk by examining absence
         * of either or both xattrs.
         */
        op_errno = EINVAL;
        ret = dict_set_uint32 (xdata, BITROT_CURRENT_VERSION_KEY, 0);
        if (ret)
                goto unwind;
        ret = dict_set_uint32 (xdata, BITROT_SIGNING_VERSION_KEY, 0);
        if (ret)
                goto unwind;
        ret = dict_set_uint32 (xdata, BITROT_OBJECT_BAD_KEY, 0);
        if (ret)
                goto unwind;
        cookie = (void *) BR_STUB_REQUEST_COOKIE;

 wind:
        STACK_WIND_COOKIE (frame, br_stub_lookup_cbk, cookie,
                           FIRST_CHILD(this), FIRST_CHILD(this)->fops->lookup,
                           loc, xdata);
        goto dealloc_dict;

 unwind:
        STACK_UNWIND_STRICT (lookup, frame,
                             -1, op_errno, NULL, NULL, NULL, NULL);
 dealloc_dict:
        if (xref)
                dict_unref (xdata);
        return 0;
}

/** }}} */

/** {{{ */

/* stat */
int
br_stub_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        int32_t ret      = 0;
        int32_t op_ret   = -1;
        int32_t op_errno = EINVAL;

        if (!IA_ISREG (loc->inode->ia_type))
                goto wind;

        ret = br_stub_check_bad_object (this, loc->inode, &op_ret, &op_errno);
        if (ret)
                goto unwind;

 wind:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->stat, loc, xdata);
        return 0;

unwind:
        STACK_UNWIND_STRICT (stat, frame, op_ret, op_errno, NULL, NULL);
        return 0;
}

/* fstat */
int
br_stub_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        int32_t ret      = 0;
        int32_t op_ret   = -1;
        int32_t op_errno = EINVAL;

        if (!IA_ISREG (fd->inode->ia_type))
                goto wind;

        ret = br_stub_check_bad_object (this, fd->inode, &op_ret, &op_errno);
        if (ret)
                goto unwind;

 wind:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->fstat, fd, xdata);
        return 0;

unwind:
        STACK_UNWIND_STRICT (fstat, frame, op_ret, op_errno, NULL, NULL);
        return 0;
}

/** }}} */

/** {{{ */

/* unlink() */

int
br_stub_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        br_stub_local_t     *local    = NULL;
        inode_t             *inode    = NULL;
        uint64_t             ctx_addr = 0;
        br_stub_inode_ctx_t *ctx      = NULL;
        int32_t              ret      = -1;

        local = frame->local;
        frame->local = NULL;

        if (op_ret < 0)
                goto unwind;

        inode = local->u.context.inode;
        if (!IA_ISREG (inode->ia_type))
           goto unwind;

        ret = br_stub_get_inode_ctx (this, inode, &ctx_addr);
        if (ret) {
                /**
                 * If the inode is bad AND context is not there, then there
                 * is a possibility of the gfid of the object being listed
                 * in the quarantine directory and will be shown in the
                 * bad objects list. So continuing with the fop with a
                 * warning log. The entry from the quarantine directory
                 * has to be removed manually. Its not a good idea to fail
                 * the fop, as the object has already been deleted.
                 */
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        BRS_MSG_GET_INODE_CONTEXT_FAILED,
                        "failed to get the context for the inode %s",
                        uuid_utoa (inode->gfid));
                goto unwind;
        }

        ctx = (br_stub_inode_ctx_t *)(long)ctx_addr;

        LOCK (&inode->lock);
        {
                /**
                 * Ignoring the return value of br_stub_del ().
                 * There is not much that can be done if unlinking
                 * of the entry in the quarantine directory fails.
                 * The failure is logged.
                 */
                if (__br_stub_is_bad_object (ctx))
                        (void) br_stub_del (this, inode->gfid);
        }
        UNLOCK (&inode->lock);

unwind:
        STACK_UNWIND_STRICT (unlink, frame, op_ret, op_errno, preparent,
                             postparent, xdata);
        br_stub_cleanup_local (local);
        br_stub_dealloc_local (local);
        return 0;
}

int
br_stub_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int flag,
                dict_t *xdata)
{
        br_stub_local_t *local = NULL;
        int32_t          op_ret = -1;
        int32_t          op_errno = 0;

        local = br_stub_alloc_local (this);
        if (!local) {
                op_ret = -1;
                op_errno = ENOMEM;
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM, BRS_MSG_NO_MEMORY,
                        "failed to allocate memory for local (path: %s, gfid: %s)",
                        loc->path, uuid_utoa (loc->inode->gfid));
                goto unwind;
        }

        br_stub_fill_local (local, NULL, NULL, loc->inode,
                            loc->inode->gfid,
                            BR_STUB_NO_VERSIONING, 0);

        frame->local = local;

        STACK_WIND (frame, br_stub_unlink_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->unlink, loc, flag, xdata);
        return 0;

unwind:
        STACK_UNWIND_STRICT (unlink, frame, op_ret, op_errno, NULL, NULL, NULL);
        return 0;
}


/** }}} */

/** {{{ */

/* forget() */

int
br_stub_forget (xlator_t *this, inode_t *inode)
{
        uint64_t ctx_addr = 0;
        br_stub_inode_ctx_t *ctx = NULL;

        inode_ctx_del (inode, this, &ctx_addr);
        if (!ctx_addr)
                return 0;

        ctx = (br_stub_inode_ctx_t *) (long) ctx_addr;

        GF_FREE (ctx);

        return 0;
}

/** }}} */

/** {{{ */

int32_t
br_stub_noop (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        STACK_DESTROY (frame->root);
        return 0;
}

static void
br_stub_send_ipc_fop (xlator_t *this, fd_t *fd, unsigned long releaseversion,
                      int sign_info)
{
        int32_t op = 0;
        int32_t ret = 0;
        dict_t *xdata = NULL;
        call_frame_t *frame = NULL;
        changelog_event_t ev = {0,};

        ev.ev_type = CHANGELOG_OP_TYPE_BR_RELEASE;
        ev.u.releasebr.version = releaseversion;
        ev.u.releasebr.sign_info = sign_info;
        gf_uuid_copy (ev.u.releasebr.gfid, fd->inode->gfid);

        xdata = dict_new ();
        if (!xdata) {
                gf_msg (this->name, GF_LOG_WARNING, ENOMEM, BRS_MSG_NO_MEMORY,
                        "dict allocation failed: cannot send IPC FOP "
                        "to changelog");
                goto out;
        }

        ret = dict_set_static_bin (xdata,
                                   "RELEASE-EVENT", &ev, CHANGELOG_EV_SIZE);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, BRS_MSG_SET_EVENT_FAILED,
                        "cannot set release event in dict");
                goto dealloc_dict;
        }

        frame = create_frame (this, this->ctx->pool);
        if (!frame) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        BRS_MSG_CREATE_FRAME_FAILED,
                        "create_frame() failure");
                goto dealloc_dict;
        }

        op = GF_IPC_TARGET_CHANGELOG;
        STACK_WIND (frame, br_stub_noop, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->ipc, op, xdata);

 dealloc_dict:
        dict_unref (xdata);
 out:
        return;
}

/**
 * This is how the state machine of sign info works:
 * 3 states:
 * 1) BR_SIGN_NORMAL => The default State of the inode
 * 2) BR_SIGN_REOPEN_WAIT => A release has been sent and is waiting for reopen
 * 3) BR_SIGN_QUICK => reopen has happened and this release should trigger sign
 * 2 events:
 * 1) GF_FOP_RELEASE
 * 2) GF_FOP_WRITE (actually a dummy write fro BitD)
 *
 * This is how states are changed based on events:
 * EVENT: GF_FOP_RELEASE:
 * if (state == BR_SIGN_NORMAL) ; then
 *     set state = BR_SIGN_REOPEN_WAIT;
 * if (state == BR_SIGN_QUICK); then
 *     set state = BR_SIGN_NORMAL;
 * EVENT: GF_FOP_WRITE:
 *  if (state == BR_SIGN_REOPEN_WAIT); then
 *     set state = BR_SIGN_QUICK;
 */
br_sign_state_t
__br_stub_inode_sign_state (br_stub_inode_ctx_t *ctx,
                            glusterfs_fop_t fop, fd_t *fd)
{
        br_sign_state_t sign_info = BR_SIGN_INVALID;

        switch (fop) {

        case GF_FOP_FSETXATTR:
                sign_info = ctx->info_sign = BR_SIGN_QUICK;
                break;

        case GF_FOP_RELEASE:
                GF_ASSERT (ctx->info_sign != BR_SIGN_REOPEN_WAIT);

                if (ctx->info_sign == BR_SIGN_NORMAL) {
                        sign_info = ctx->info_sign = BR_SIGN_REOPEN_WAIT;
                } else {
                        sign_info = ctx->info_sign;
                        ctx->info_sign = BR_SIGN_NORMAL;
                }

                break;
        default:
                break;
        }

        return sign_info;
}

int32_t
br_stub_release (xlator_t *this, fd_t *fd)
{
        int32_t              ret            = 0;
        int32_t              flags          = 0;
        inode_t             *inode          = NULL;
        unsigned long        releaseversion = 0;
        br_stub_inode_ctx_t *ctx            = NULL;
        uint64_t             tmp            = 0;
        br_stub_fd_t        *br_stub_fd     = NULL;
        int32_t              signinfo       = 0;

        inode = fd->inode;

        LOCK (&inode->lock);
        {
                ctx = __br_stub_get_ongoing_version_ctx (this, inode, NULL);
                if (ctx == NULL)
                        goto unblock;
                br_stub_fd = br_stub_fd_ctx_get (this, fd);
                if (br_stub_fd) {
                        list_del_init (&br_stub_fd->list);
                }

                ret = __br_stub_can_trigger_release
                                    (inode, ctx, &releaseversion);
                if (!ret)
                        goto unblock;

                signinfo = __br_stub_inode_sign_state (ctx, GF_FOP_RELEASE, fd);
                signinfo = htonl (signinfo);

                /* inode back to initital state: mark dirty */
                if (ctx->info_sign == BR_SIGN_NORMAL) {
                        __br_stub_mark_inode_dirty (ctx);
                        __br_stub_unset_inode_modified (ctx);
                }
        }
 unblock:
        UNLOCK (&inode->lock);

        if (ret) {
                gf_msg_debug (this->name, 0, "releaseversion: %lu | flags: %d "
                              "| signinfo: %d",
                              (unsigned long) ntohl (releaseversion), flags,
                              ntohl(signinfo));
                br_stub_send_ipc_fop (this, fd, releaseversion, signinfo);
        }

        ret = fd_ctx_del (fd, this, &tmp);
        br_stub_fd = (br_stub_fd_t *)(long)tmp;

        GF_FREE (br_stub_fd);

        return 0;
}

int32_t
br_stub_releasedir (xlator_t *this, fd_t *fd)
{
        br_stub_fd_t   *fctx = NULL;
        uint64_t        ctx  = 0;
        int             ret  = 0;

        ret = fd_ctx_del (fd, this, &ctx);
        if (ret < 0)
                goto out;

        fctx = (br_stub_fd_t *) (long) ctx;
        if (fctx->bad_object.dir) {
                ret = sys_closedir (fctx->bad_object.dir);
                if (ret)
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                BRS_MSG_BAD_OBJ_DIR_CLOSE_FAIL,
                                "closedir error: %s", strerror (errno));
        }

        GF_FREE (fctx);
out:
        return 0;
}

/** }}} */

/** {{{ */

/* ictxmerge */

void
br_stub_ictxmerge (xlator_t *this, fd_t *fd,
                   inode_t *inode, inode_t *linked_inode)
{
        int32_t              ret        = 0;
        uint64_t             ctxaddr    = 0;
        uint64_t             lctxaddr   = 0;
        br_stub_inode_ctx_t *ctx        = NULL;
        br_stub_inode_ctx_t *lctx       = NULL;
        br_stub_fd_t        *br_stub_fd = NULL;

        ret = br_stub_get_inode_ctx (this, inode, &ctxaddr);
        if (ret < 0)
                goto done;
        ctx = (br_stub_inode_ctx_t *) ctxaddr;

        LOCK (&linked_inode->lock);
        {
                ret = __br_stub_get_inode_ctx (this, linked_inode, &lctxaddr);
                if (ret < 0)
                        goto unblock;
                lctx = (br_stub_inode_ctx_t *) lctxaddr;

                GF_ASSERT (list_is_singular (&ctx->fd_list));
                br_stub_fd = list_first_entry (&ctx->fd_list, br_stub_fd_t,
                                               list);
                if (br_stub_fd) {
                        GF_ASSERT (br_stub_fd->fd == fd);
                        list_move_tail (&br_stub_fd->list, &lctx->fd_list);
                }
        }
unblock:
        UNLOCK (&linked_inode->lock);

 done:
        return;
}

/** }}} */


struct xlator_fops fops = {
        .lookup    = br_stub_lookup,
        .stat      = br_stub_stat,
        .fstat     = br_stub_fstat,
        .open      = br_stub_open,
        .create    = br_stub_create,
        .readdirp  = br_stub_readdirp,
        .getxattr  = br_stub_getxattr,
        .fgetxattr = br_stub_fgetxattr,
        .fsetxattr = br_stub_fsetxattr,
        .writev    = br_stub_writev,
        .truncate  = br_stub_truncate,
        .ftruncate = br_stub_ftruncate,
        .mknod     = br_stub_mknod,
        .readv     = br_stub_readv,
        .removexattr = br_stub_removexattr,
        .fremovexattr = br_stub_fremovexattr,
        .setxattr  = br_stub_setxattr,
        .opendir   = br_stub_opendir,
        .readdir   = br_stub_readdir,
        .unlink    = br_stub_unlink,
};

struct xlator_cbks cbks = {
        .forget    = br_stub_forget,
        .release   = br_stub_release,
        .ictxmerge = br_stub_ictxmerge,
};

struct volume_options options[] = {
        { .key = {"bitrot"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "enable/disable bitrot stub"
        },
        { .key = {"export"},
          .type = GF_OPTION_TYPE_PATH,
          .description = "brick path for versioning"
        },
	{ .key  = {NULL} },
};
