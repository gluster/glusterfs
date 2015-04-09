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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "changelog.h"
#include "compat-errno.h"

#include "bit-rot-stub.h"
#include "bit-rot-stub-mem-types.h"

#include "bit-rot-common.h"

#define BR_STUB_REQUEST_COOKIE  0x1

int32_t
mem_acct_init (xlator_t *this)
{
        int32_t ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_br_stub_mt_end + 1);

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
        char *tmp = NULL;
        struct timeval tv = {0,};
	br_stub_private_t *priv = NULL;

	if (!this->children) {
		gf_log (this->name, GF_LOG_ERROR, "FATAL: no children");
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

        (void) gettimeofday (&tv, NULL);

        /* boot time is in network endian format */
        priv->boot[0] = htonl (tv.tv_sec);
        priv->boot[1] = htonl (tv.tv_usec);

        gf_log (this->name, GF_LOG_DEBUG, "bit-rot stub loaded");
	this->private = priv;
        return 0;

 free_mempool:
        mem_pool_destroy (priv->local_pool);
 free_priv:
        GF_FREE (priv);
 error_return:
        return -1;
}

void
fini (xlator_t *this)
{
	br_stub_private_t *priv = this->private;

        if (!priv)
                return;
        this->private = NULL;
	GF_FREE (priv);

	return;
}

static inline int
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

static inline void
br_stub_dealloc_versions (void *mem)
{
        GF_FREE (mem);
}

static inline br_stub_local_t *
br_stub_alloc_local (xlator_t *this)
{
        br_stub_private_t *priv = this->private;

        return mem_get0 (priv->local_pool);
}

static inline void
br_stub_dealloc_local (br_stub_local_t *ptr)
{
        mem_put (ptr);
}

static inline int
br_stub_prepare_version_request (xlator_t *this, dict_t *dict,
                                br_version_t *obuf, unsigned long oversion)
{
        br_stub_private_t *priv = NULL;

        priv = this->private;
        br_set_ongoingversion (obuf, oversion, priv->boot);

        return dict_set_static_bin (dict, BITROT_CURRENT_VERSION_KEY,
                                    (void *)obuf, sizeof (br_version_t));
}

static inline int
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
static inline int
br_stub_init_inode_versions (xlator_t *this, fd_t *fd, inode_t *inode,
                             unsigned long version, gf_boolean_t markdirty)
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
static inline int
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

static inline void
br_stub_fill_local (br_stub_local_t *local,
                    call_stub_t *stub, fd_t *fd, inode_t *inode, uuid_t gfid,
                    int versioningtype, unsigned long memversion)
{
        local->fopstub = stub;
        local->versioningtype = versioningtype;
        local->u.context.version = memversion;
        if (fd && !local->u.context.fd)
                local->u.context.fd = fd_ref (fd);
        if (inode)
                local->u.context.inode = inode_ref (inode);
        gf_uuid_copy (local->u.context.gfid, gfid);
}

static inline void
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

        frame->local = local;
        STACK_WIND (frame, callback,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->fsetxattr,
                    fd, dict, flags, xdata);

        ret = 0;

 dealloc_xdata:
        dict_unref (xdata);
 done:
        return ret;
}

static inline int
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

        /* inode requires writeback to disk */
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

static inline int
br_stub_compare_sign_version (xlator_t *this, inode_t *inode,
                              br_signature_t *sbuf, dict_t *dict)
{
        int32_t ret = -1;
        br_stub_inode_ctx_t *ctx = NULL;
        uint64_t tmp_ctx = 0;

        GF_VALIDATE_OR_GOTO ("bit-rot-stub", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);
        GF_VALIDATE_OR_GOTO (this->name, sbuf, out);
        GF_VALIDATE_OR_GOTO (this->name, dict, out);

        ret = br_stub_get_inode_ctx (this, inode, &tmp_ctx);
        if (ret) {
                dict_del (dict, BITROT_SIGNING_VERSION_KEY);
                goto out;
        }

        ret = -1;
        ctx = (br_stub_inode_ctx_t *)(long)tmp_ctx;

        LOCK (&inode->lock);
        {
                if (ctx->currentversion == sbuf->signedversion)
                        ret = 0;
                else
                        gf_log (this->name, GF_LOG_WARNING, "current version "
                                "%lu and version of the signature %lu are not "
                                "same", ctx->currentversion,
                                sbuf->signedversion);
        }
        UNLOCK (&inode->lock);

out:
        return ret;
}

static inline int
br_stub_prepare_signature (xlator_t *this, dict_t *dict,
                           inode_t *inode, br_isignature_t *sign)
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

        ret = br_stub_compare_sign_version (this, inode, sbuf, dict);
        if (ret)
                goto dealloc_versions;

        return 0;

 dealloc_versions:
        br_stub_dealloc_versions (sbuf);
 error_return:
        return -1;
}

int
br_stub_fsetxattr (call_frame_t *frame, xlator_t *this,
                   fd_t *fd, dict_t *dict, int flags, dict_t *xdata)
{
        int32_t          ret  = 0;
        br_isignature_t *sign = NULL;
        gf_boolean_t     xref = _gf_false;

        if (!IA_ISREG (fd->inode->ia_type))
                goto wind;
        ret = dict_get_bin (dict, GLUSTERFS_SET_OBJECT_SIGNATURE,
                            (void **) &sign);
        if (ret < 0)
                goto wind;
        if (frame->root->pid != GF_CLIENT_PID_BITD)
                goto unwind;

        ret = br_stub_prepare_signature (this, dict, fd->inode, sign);
        if (ret)
                goto unwind;
        dict_del (dict, GLUSTERFS_SET_OBJECT_SIGNATURE);

        if (!xdata) {
                xdata = dict_new ();
                if (!xdata)
                        goto unwind;
        } else {
                dict_ref (xdata);
        }

        xref = _gf_true;
        ret = dict_set_int32 (xdata, GLUSTERFS_DURABLE_OP, 0);
        if (ret)
                goto unwind;

        gf_log (this->name, GF_LOG_DEBUG, "SIGNED VERSION: %lu",
                sign->signedversion);
 wind:
        STACK_WIND (frame, default_setxattr_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->fsetxattr, fd,
                    dict, flags, xdata);
        goto done;

 unwind:
        STACK_UNWIND_STRICT (setxattr, frame, -1, EINVAL, NULL);
 done:
        if (xref)
                dict_unref (xdata);
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

        if (op_ret < 0)
                goto unwind;
        if (cookie != (void *) BR_STUB_REQUEST_COOKIE)
                goto unwind;

        op_ret   = -1;
        status = br_version_xattr_state (xattr, &obuf, &sbuf);

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
        sign->stale = (obuf->ongoingversion != sbuf->signedversion) ? 1 : 0;

        /* Object's signature */
        sign->signaturelen  = signaturelen;
        sign->signaturetype = sbuf->signaturetype;
        (void) memcpy (sign->signature, sbuf->signature, signaturelen);

        op_errno = EINVAL;
        ret = dict_set_bin (xattr, GLUSTERFS_GET_OBJECT_SIGNATURE,
                            (void *)sign, totallen);
        if (ret < 0)
                goto delkeys;
        op_errno = 0;
        op_ret = totallen;

 delkeys:
        br_stub_remove_vxattrs (xattr);

 unwind:
        STACK_UNWIND (frame, op_ret, op_errno, xattr, xdata);
        return 0;
}

static inline void
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
        void *cookie = NULL;
        uuid_t rootgfid = {0, };
        fop_getxattr_cbk_t cbk = br_stub_getxattr_cbk;

        rootgfid[15] = 1;

        if (!name) {
                cbk = br_stub_listxattr_cbk;
                goto wind;
        }

        if (br_stub_is_internal_xattr (name)) {
                STACK_UNWIND (frame, -1, EINVAL, NULL, NULL);
                return 0;
        }

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
        }

 wind:
        STACK_WIND_COOKIE
                      (frame, cbk, cookie, FIRST_CHILD (this),
                       FIRST_CHILD (this)->fops->getxattr, loc, name, xdata);
        return 0;
}

int
br_stub_fgetxattr (call_frame_t *frame, xlator_t *this,
                   fd_t *fd, const char *name, dict_t *xdata)
{
        void *cookie = NULL;
        uuid_t rootgfid = {0, };
        fop_fgetxattr_cbk_t cbk = br_stub_getxattr_cbk;

        rootgfid[15] = 1;

        if (!name) {
                cbk = br_stub_listxattr_cbk;
                goto wind;
        }

        if (br_stub_is_internal_xattr (name)) {
                STACK_UNWIND (frame, -1, EINVAL, NULL, NULL);
                return 0;
        }

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
        }

 wind:
        STACK_WIND_COOKIE
                      (frame, cbk, cookie, FIRST_CHILD (this),
                       FIRST_CHILD (this)->fops->fgetxattr, fd, name, xdata);
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
        int32_t              ret         = 0;
        uint64_t             ctx_addr    = 0;
        br_stub_inode_ctx_t *ctx         = NULL;
        br_stub_local_t     *local       = NULL;

        if (frame->local) {
                local = frame->local;
                frame->local = NULL;
        }

        if (op_ret < 0)
                goto unwind;

        ret = br_stub_get_inode_ctx (this, local->u.context.fd->inode,
                                     &ctx_addr);
        if (ret < 0)
                goto unwind;

        ctx = (br_stub_inode_ctx_t *) (long) ctx_addr;

        /* Mark the flag to indicate the inode has been modified */
        LOCK (&local->u.context.fd->inode->lock);
        {
                if (!__br_stub_is_inode_modified (ctx))
                        __br_stub_set_inode_modified (ctx);
        }
        UNLOCK (&local->u.context.fd->inode->lock);


unwind:
        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf,
                             xdata);
        br_stub_cleanup_local (local);
        br_stub_dealloc_local (local);
        return 0;
}

/**
 * Ongoing version is increased only for the first modify operation.
 * First modify version means the first write or truncate call coming on the
 * first fd in the list of inodes.
 * For anonymous fds open would not have come, so check if its the first write
 * by doing both inode dirty check and ensuring list of fds is empty
 */
static inline gf_boolean_t
br_stub_inc_version (xlator_t *this, fd_t *fd, br_stub_inode_ctx_t *ctx)
{
        gf_boolean_t inc_version = _gf_false;

        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, ctx, out);

        LOCK (&fd->inode->lock);
        {
                if (__br_stub_is_inode_dirty (ctx))
                        inc_version = _gf_true;
        }
        UNLOCK (&fd->inode->lock);

out:
        return inc_version;
}

/**
 * Since NFS does not do open, writes from NFS are sent over an anonymous
 * fd. It means each write fop might come on a different anonymous fd and
 * will lead to very large number of notifications being sent. It might
 * affect the perfromance as, there will too many sign requests.
 * To avoid that whenever the last fd released from an inode (logical release)
 * is an anonymous fd the release notification is sent with a flag being set
 * __br_stub_anon_release (ctx);
 * BitD checks for the flag and if set, it will send a dummy write request
 * (again on an anonymous fd) instead of triggering sign.
 * Bit-rot-stub should identify such dummy writes and should send success to
 * them instead of winding them downwards.
 */
gf_boolean_t
br_stub_dummy_write (call_frame_t *frame)
{
        return (frame->root->pid == GF_CLIENT_PID_BITD)
                        ? _gf_true : _gf_false;
}

int32_t
br_stub_anon_fd_ctx (xlator_t *this, fd_t *fd, br_stub_inode_ctx_t *ctx)
{
        int32_t  ret = -1;
        br_stub_fd_t *br_stub_fd = NULL;

        br_stub_fd = br_stub_fd_ctx_get (this, fd);
        if (!br_stub_fd) {
                ret = br_stub_add_fd_to_inode (this, fd, ctx);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to "
                                "add fd to the inode (gfid: %s)",
                                uuid_utoa (fd->inode->gfid));
                        goto out;
                }
        }

        ret = 0;

out:
        return ret;
}

int32_t
br_stub_writev_resume (call_frame_t *frame, xlator_t *this, fd_t *fd,
                       struct iovec *vector, int32_t count, off_t offset,
                       uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
        if (frame->root->pid == GF_CLIENT_PID_BITD)
                br_stub_writev_cbk (frame, NULL, this, vector->iov_len, 0,
                                    NULL, NULL, NULL);
        else
                STACK_WIND (frame, br_stub_writev_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->writev, fd, vector, count,
                            offset, flags, iobref, xdata);
        return 0;
}

/**
   TODO: If possible add pictorial represention of below comment.

   Before sending writev on the ANONYMOUS FD, increase the ongoing
   version first. This brings anonymous fd write closer to the regular
   fd write by having the ongoing version increased before doing the
   write (In regular fd, after open the ongoing version is incremented).
   Do following steps to handle writes on anonymous fds:
   1) Increase the on-disk ongoing version
   2) Once versioning is successfully done send write operation. If versioning
      fails, then fail the write fop.
   3) In writev_cbk do below things:
      a) Increase in-memory version
      b) set the fd context (so that br_stub_release is invoked)
      c) add the fd to the list of fds maintained in the inode context of
         bitrot-stub.
      d) Mark inode as non dirty
      e) Mard inode as modified (in the inode context)
**/
int32_t
br_stub_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
                struct iovec *vector, int32_t count, off_t offset,
                uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
        br_stub_local_t     *local       = NULL;
        call_stub_t         *stub        = NULL;
        int32_t              op_ret      = -1;
        int32_t              op_errno    = EINVAL;
        gf_boolean_t         inc_version = _gf_false;
        br_stub_inode_ctx_t *ctx         = NULL;
        uint64_t             ctx_addr    = 0;
        int32_t              ret         = -1;

        GF_VALIDATE_OR_GOTO ("bit-rot-stub", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, frame, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd, unwind);

        local = br_stub_alloc_local (this);
        if (!local) {
                gf_log (this->name, GF_LOG_ERROR, "local allocation failed "
                        "(gfid: %s)", uuid_utoa (fd->inode->gfid));
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        local->u.context.fd = fd_ref (fd);
        frame->local = local;

        ret = br_stub_get_inode_ctx (this, fd->inode, &ctx_addr);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the inode "
                        "context for the inode %s",
                        uuid_utoa (fd->inode->gfid));
                goto unwind;
        }

        ctx = (br_stub_inode_ctx_t *) (long) ctx_addr;
        if (fd_is_anonymous (fd)) {
                ret = br_stub_anon_fd_ctx (this, fd, ctx);
                if (ret)
                        goto unwind;
        }

        /* TODO: Better to do a dummy fsetxattr instead of write. Keep write
           simple */
        if (br_stub_dummy_write (frame)) {
                LOCK (&fd->inode->lock);
                {
                        (void) __br_stub_inode_sign_state
                                             (ctx, GF_FOP_WRITE, fd);
                }
                UNLOCK (&fd->inode->lock);

                if (xdata && dict_get (xdata, "br-fd-reopen")) {
                        op_ret = vector->iov_len;
                        op_errno = 0;
                        goto unwind;
                }
        }

        /**
         * Check whether this is the first write on this inode since the last
         * sign notification has been sent. If so, do versioning. Otherwise
         * go ahead with the fop.
         */
        inc_version = br_stub_inc_version (this, fd, ctx);
        if (!inc_version)
                goto wind;

        /* Create the stub for the write fop */
        stub = fop_writev_stub (frame, br_stub_writev_resume, fd, vector, count,
                                offset, flags, iobref, xdata);

        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "failed to allocate stub for "
                        "write fop (gfid: %s), unwinding",
                        uuid_utoa (fd->inode->gfid));
                goto unwind;
        }

        /* Perform Versioning */
        return br_stub_perform_incversioning (this, frame, stub, fd, ctx);

wind:
        STACK_WIND (frame, br_stub_writev_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev, fd, vector, count, offset,
                    flags, iobref, xdata);
        return 0;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, NULL, NULL,
                             NULL);
        br_stub_cleanup_local (local);
        br_stub_dealloc_local (local);
        return 0;
}

int32_t
br_stub_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                       struct iatt *postbuf, dict_t *xdata)
{
        int32_t              ret         = 0;
        uint64_t             ctx_addr    = 0;
        br_stub_inode_ctx_t *ctx         = NULL;
        br_stub_local_t     *local       = NULL;

        if (frame->local) {
                local = frame->local;
                frame->local = NULL;
        }

        if (op_ret < 0)
                goto unwind;

        ret = br_stub_get_inode_ctx (this, local->u.context.fd->inode,
                                     &ctx_addr);
        if (ret < 0)
                goto unwind;

        ctx = (br_stub_inode_ctx_t *) (long) ctx_addr;

        /* Mark the flag to indicate the inode has been modified */
        LOCK (&local->u.context.fd->inode->lock);
        {
                if (!__br_stub_is_inode_modified (ctx))
                        __br_stub_set_inode_modified (ctx);
        }
        UNLOCK (&local->u.context.fd->inode->lock);


unwind:
        STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno, prebuf, postbuf,
                             xdata);
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

int32_t
br_stub_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd,
                   off_t offset, dict_t *xdata)
{
        br_stub_local_t     *local       = NULL;
        call_stub_t         *stub        = NULL;
        int32_t              op_ret      = -1;
        int32_t              op_errno    = EINVAL;
        gf_boolean_t         inc_version = _gf_false;
        br_stub_inode_ctx_t *ctx         = NULL;
        uint64_t             ctx_addr    = 0;
        int32_t              ret         = -1;

        GF_VALIDATE_OR_GOTO ("bit-rot-stub", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, frame, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd, unwind);

        local = br_stub_alloc_local (this);
        if (!local) {
                gf_log (this->name, GF_LOG_ERROR, "local allocation failed "
                        "(gfid: %s)", uuid_utoa (fd->inode->gfid));
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        local->u.context.fd = fd_ref (fd);
        frame->local = local;

        ret = br_stub_get_inode_ctx (this, fd->inode, &ctx_addr);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the inode "
                        "context for the inode %s",
                        uuid_utoa (fd->inode->gfid));
                goto unwind;
        }

        ctx = (br_stub_inode_ctx_t *) (long) ctx_addr;
        if (fd_is_anonymous (fd)) {
                ret = br_stub_anon_fd_ctx (this, fd, ctx);
                if (ret)
                        goto unwind;
        }

        /**
         * c.f. br_stub_writev()
         */
        inc_version = br_stub_inc_version (this, fd, ctx);
        if (!inc_version)
                goto wind;

        /* Create the stub for the ftruncate fop */
        stub = fop_ftruncate_stub (frame, br_stub_ftruncate_resume, fd, offset,
                                   xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "failed to allocate stub for "
                        "ftruncate fop (gfid: %s), unwinding",
                        uuid_utoa (fd->inode->gfid));
                goto unwind;
        }

        /* Perform Versioning */
        return br_stub_perform_incversioning (this, frame, stub, fd, ctx);

wind:
        STACK_WIND (frame, br_stub_ftruncate_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->ftruncate, fd, offset, xdata);
        return 0;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno, NULL, NULL,
                             NULL);
        br_stub_cleanup_local (local);
        br_stub_dealloc_local (local);
        return 0;
}

int32_t
br_stub_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                      struct iatt *postbuf, dict_t *xdata)
{
        int32_t              ret         = 0;
        uint64_t             ctx_addr    = 0;
        br_stub_inode_ctx_t *ctx         = NULL;
        br_stub_local_t     *local       = NULL;

        if (frame->local) {
                local = frame->local;
                frame->local = NULL;
        }

        if (op_ret < 0)
                goto unwind;

        ret = br_stub_get_inode_ctx (this, local->u.context.fd->inode,
                                     &ctx_addr);
        if (ret < 0)
                goto unwind;

        ctx = (br_stub_inode_ctx_t *) (long) ctx_addr;

        /* Mark the flag to indicate the inode has been modified */
        LOCK (&local->u.context.fd->inode->lock);
        {
                if (!__br_stub_is_inode_modified (ctx))
                        __br_stub_set_inode_modified (ctx);
        }
        UNLOCK (&local->u.context.fd->inode->lock);


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
        br_stub_inode_ctx_t *ctx         = NULL;
        uint64_t             ctx_addr    = 0;
        int32_t              ret         = -1;
        fd_t                *fd          = NULL;

        GF_VALIDATE_OR_GOTO ("bit-rot-stub", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, frame, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, unwind);

        fd = fd_anonymous (loc->inode);
        if (!fd) {
                gf_log (this->name, GF_LOG_ERROR, "failed to create anonymous "
                        "fd for the inode %s", uuid_utoa (loc->inode->gfid));
                goto unwind;
        }

        local = br_stub_alloc_local (this);
        if (!local) {
                gf_log (this->name, GF_LOG_ERROR, "local allocation failed "
                        "(gfid: %s)", uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        local->u.context.fd = fd;
        frame->local = local;

        ret = br_stub_get_inode_ctx (this, loc->inode, &ctx_addr);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the inode "
                        "context for the inode %s",
                        uuid_utoa (fd->inode->gfid));
                goto unwind;
        }

        ctx = (br_stub_inode_ctx_t *) (long) ctx_addr;
        ret = br_stub_anon_fd_ctx (this, local->u.context.fd, ctx);
        if (ret)
                goto unwind;

        /**
         * c.f. br_stub_writev()
         */
        inc_version = br_stub_inc_version (this, fd, ctx);
        if (!inc_version)
                goto wind;

        /* Create the stub for the truncate fop */
        stub = fop_truncate_stub (frame, br_stub_truncate_resume, loc, offset,
                                  xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "failed to allocate stub for "
                        "truncate fop (gfid: %s), unwinding",
                        uuid_utoa (fd->inode->gfid));
                goto unwind;
        }

        /* Perform Versioning */
        return br_stub_perform_incversioning (this, frame, stub,
                                              local->u.context.fd, ctx);

wind:
        STACK_WIND (frame, br_stub_truncate_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->truncate, loc, offset, xdata);
        return 0;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno, NULL, NULL,
                             NULL);
        br_stub_cleanup_local (local);
        br_stub_dealloc_local (local);
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

        GF_VALIDATE_OR_GOTO ("bit-rot-stub", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, unwind);

        if (frame->root->pid == GF_CLIENT_PID_SCRUB)
                goto wind;

        ret = br_stub_get_inode_ctx (this, fd->inode, &ctx_addr);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the inode "
                        "context for the file %s (gfid: %s)", loc->path,
                        uuid_utoa (fd->inode->gfid));
                goto unwind;
        }

        ctx = (br_stub_inode_ctx_t *)(long)ctx_addr;
        if (flags == O_RDONLY)
                goto wind;

        ret = br_stub_add_fd_to_inode (this, fd, ctx);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed add fd to the list "
                        "(gfid: %s)", uuid_utoa (fd->inode->gfid));
                goto unwind;
        }

wind:
        STACK_WIND (frame, default_open_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->open, loc, flags, fd, xdata);
        return 0;
unwind:
        STACK_UNWIND_STRICT (open, frame, -1, EINVAL, NULL, NULL);
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
                gf_log (this->name, GF_LOG_ERROR, "failed to set the fd "
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
                                                   _gf_true);
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

/** }}} */

static inline int32_t
br_stub_lookup_version (xlator_t *this,
                        uuid_t gfid, inode_t *inode, dict_t *xattr)
{
        unsigned long       version = 0;
        br_version_t       *obuf    = NULL;
        br_signature_t     *sbuf    = NULL;
        br_vxattr_status_t  status;

        /**
         * versioning xattrs were requested from POSIX. if available, figure
         * out the correct version to use in the inode context (start with
         * the default version if unavailable). As of now versions are not
         * persisted on-disk. The inode is marked dirty, so that the first
         * operation (such as write(), etc..) triggers synchronization to
         * disk.
         */
        status = br_version_xattr_state (xattr, &obuf, &sbuf);

        version = ((status == BR_VXATTR_STATUS_FULL)
                   || (status == BR_VXATTR_STATUS_UNSIGNED))
                        ? obuf->ongoingversion : BITROT_DEFAULT_CURRENT_VERSION;
        return br_stub_init_inode_versions (this, NULL,
                                            inode, version, _gf_true);
}


/** {{{ */

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

int
br_stub_lookup_cbk (call_frame_t *frame, void *cookie,
                    xlator_t *this, int op_ret, int op_errno, inode_t *inode,
                    struct iatt *stbuf, dict_t *xattr, struct iatt *postparent)
{
        int32_t ret = 0;

        if (op_ret < 0)
                goto unwind;
        if (!IA_ISREG (stbuf->ia_type))
                goto unwind;
        if (cookie != (void *) BR_STUB_REQUEST_COOKIE)
                goto delkey;

        ret = br_stub_lookup_version (this, stbuf->ia_gfid, inode, xattr);
        if (ret < 0) {
                op_ret   = -1;
                op_errno = EINVAL;
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

static inline void
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
                gf_log (this->name, GF_LOG_WARNING,
                        "dict allocation failed: cannot send IPC FOP "
                        "to changelog");
                goto out;
        }

        ret = dict_set_static_bin (xdata,
                                   "RELEASE-EVENT", &ev, CHANGELOG_EV_SIZE);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot set release event in dict");
                goto dealloc_dict;
        }

        frame = create_frame (this, this->ctx->pool);
        if (!frame) {
                gf_log (this->name, GF_LOG_WARNING, "create_frame() failure");
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

        case GF_FOP_WRITE:
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
                gf_log (this->name, GF_LOG_DEBUG,
                        "releaseversion: %lu | flags: %d | signinfo: %d",
                        (unsigned long) ntohl (releaseversion),
                        flags, ntohl(signinfo));
                br_stub_send_ipc_fop (this, fd, releaseversion, signinfo);
        }

        ret = fd_ctx_del (fd, this, &tmp);
        br_stub_fd = (br_stub_fd_t *)(long)tmp;

        GF_FREE (br_stub_fd);

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
        .open      = br_stub_open,
        .create    = br_stub_create,
        .readdirp  = br_stub_readdirp,
        .getxattr  = br_stub_getxattr,
        .fgetxattr = br_stub_fgetxattr,
        .fsetxattr = br_stub_fsetxattr,
        .writev    = br_stub_writev,
        .truncate  = br_stub_truncate,
        .ftruncate = br_stub_ftruncate,
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
