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

#include "bit-rot-stub.h"
#include "bit-rot-stub-mem-types.h"

#include "bit-rot-common.h"

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
br_stub_prepare_default_request (xlator_t *this, dict_t *dict,
                                 br_version_t *obuf, br_signature_t *sbuf)
{
        int32_t ret = 0;
        size_t size = 0;
        br_stub_private_t *priv = NULL;

        priv = this->private;

        /** Prepare ongoing version */
        br_set_default_ongoingversion (obuf, priv->boot);
        ret = dict_set_static_bin (dict, BITROT_CURRENT_VERSION_KEY,
                                   (void *)obuf, sizeof (br_version_t));
        if (ret)
                return -1;

        /** Prepare signature version */
        br_set_default_signature (sbuf, &size);
        return dict_set_static_bin (dict, BITROT_SIGNING_VERSION_KEY,
                                    (void *)sbuf, size);
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
                                 unsigned long currentversion,
                                 br_isignature_t *sign, size_t signaturelen)
{
        size_t size = 0;

        br_set_signature (sbuf, sign, currentversion, signaturelen, &size);

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
br_stub_init_inode_versions (xlator_t *this, inode_t *inode,
                             unsigned long version, gf_boolean_t markdirty)
{
        int32_t ret = 0;
        br_stub_inode_ctx_t *ctx = NULL;

        ctx = GF_CALLOC (1, sizeof (br_stub_inode_ctx_t),
                         gf_br_stub_mt_inode_ctx_t);
        if (!ctx)
                goto error_return;

        (markdirty) ? __br_stub_mark_inode_dirty (ctx)
                : __br_stub_mark_inode_synced (ctx);
        ctx->currentversion = version;
        ctx->transientversion = version;

        ret = br_stub_set_inode_ctx (this, inode, ctx);
        if (ret)
                goto free_ctx;
        return 0;

 free_ctx:
        GF_FREE (ctx);
 error_return:
        return -1;
}

static inline int
br_stub_get_ongoing_version (xlator_t *this,
                             inode_t *inode, unsigned long *version)
{
        int32_t ret = 0;
        uint64_t ctx_addr = 0;
        br_stub_inode_ctx_t *ctx = 0;

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_get (inode, this, &ctx_addr);
                if (ret < 0)
                        goto unblock;
                ctx = (br_stub_inode_ctx_t *) (long) ctx_addr;
                *version = ctx->currentversion;
        }
 unblock:
        UNLOCK (&inode->lock);

        return ret;
}

/**
 * modify the ongoing version of an inode.
 */
static inline int
br_stub_mod_inode_versions (xlator_t *this,
                            inode_t *inode, unsigned long version)
{
        int32_t ret = -1;
        uint64_t ctx_addr = 0;
        br_stub_inode_ctx_t *ctx = 0;

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_get (inode, this, &ctx_addr);
                if (ret < 0)
                        goto unblock;

                ctx = (br_stub_inode_ctx_t *) (long) ctx_addr;
                if (version > ctx->currentversion) {
                        ctx->currentversion = version;
                        __br_stub_mark_inode_synced (ctx);
                }
        }
 unblock:
        UNLOCK (&inode->lock);

        return ret;
}

static inline void
br_stub_fill_local (br_stub_local_t *local,
                    call_stub_t *stub, inode_t *inode, uuid_t gfid,
                    int versioningtype, unsigned long memversion, int durable)
{
        local->fopstub = stub;
        local->versioningtype = versioningtype;
        local->u.context.version = memversion;
        local->u.context.inode = inode_ref (inode);
        uuid_copy (local->u.context.gfid, gfid);

        /* mark inode dirty/fresh according to durability */
        local->u.context.markdirty = (durable) ? _gf_false : _gf_true;
}

static inline void
br_stub_cleanup_local (br_stub_local_t *local)
{
        local->fopstub = NULL;
        local->versioningtype = 0;
        local->u.context.version = 0;
        inode_unref (local->u.context.inode);
        local->u.context.markdirty = _gf_true;
        memset (local->u.context.gfid, '\0', sizeof (uuid_t));
}

/**
 * callback for inode/fd full versioning
 */
int
br_stub_inode_fullversioning_cbk (call_frame_t *frame,
                                  void *cookie, xlator_t *this,
                                  int op_ret, int op_errno, dict_t *xdata)
{
        inode_t         *inode   = NULL;
        unsigned long    version = 0;
        gf_boolean_t     dirty   = _gf_true;
        br_stub_local_t *local   = NULL;

        local = (br_stub_local_t *)frame->local;
        if (op_ret < 0)
                goto done;
        inode   = local->u.context.inode;
        version = local->u.context.version;
        dirty   = local->u.context.markdirty;

        op_ret = br_stub_init_inode_versions (this, inode, version, dirty);
        if (op_ret < 0)
                op_errno = EINVAL;

 done:
        frame->local = NULL;
        if (op_ret < 0)
                call_unwind_error (local->fopstub, op_ret, op_errno);
        else
                call_resume (local->fopstub);
        br_stub_cleanup_local (local);
        br_stub_dealloc_local (local);

        return 0;
}

int
br_stub_fd_incversioning_cbk (call_frame_t *frame,
                              void *cookie, xlator_t *this,
                              int op_ret, int op_errno, dict_t *xdata)
{
        inode_t         *inode   = NULL;
        unsigned long    version = 0;
        br_stub_local_t *local   = NULL;

        local = (br_stub_local_t *)frame->local;
        if (op_ret < 0)
                goto done;
        inode = local->u.context.inode;
        version = local->u.context.version;

        op_ret = br_stub_mod_inode_versions (this, inode, version);
        if (op_ret < 0)
                op_errno = EINVAL;

 done:
        frame->local = NULL;
        if (op_ret < 0)
                call_unwind_error (local->fopstub, -1, op_errno);
        else
                call_resume (local->fopstub);
        br_stub_cleanup_local (local);
        br_stub_dealloc_local (local);

        return 0;
}

/**
 * Initial object versioning
 *
 * Version persists two (2) extended attributes as explained below:
 *   1. Current (ongoing) version: This is incremented on an open()
 *      or creat() and is the running version for an object.
 *   2. Signing version: This is the version against which an object
 *      was signed (checksummed). Alongside this, the _current_
 *      ongoing version is persisted.
 *
 * During initial versioning, both ongoing and signing versions are
 * set of one and zero respectively. An open()/creat() call increments
 * the ongoing version as an indication of modification to the object.
 * Additionally this needs to be persisted on disk and needs to be
 * durable: fsync().. :-/
 * As an optimization only the first open()/creat() synchronizes the
 * ongoing version to disk, subsequent calls just increment the in-
 * memory ongoing version.
 *
 * c.f. br_stub_open_cbk() / br_stub_create_cbk()
 */

/**
 * perform full or incremental versioning on an inode pointd by an
 * fd. incremental versioning is done when an inode is dirty and a
 * writeback is trigerred.
 */

int
br_stub_fd_versioning (xlator_t *this, call_frame_t *frame, call_stub_t *stub,
                       dict_t *dict, fd_t *fd, br_stub_version_cbk *callback,
                       unsigned long memversion, int versioningtype, int durable)
{
        int32_t  ret   = -1;
        dict_t  *xdata = NULL;
        br_stub_local_t *local = NULL;

        if (durable) {
                xdata = dict_new ();
                if (!xdata)
                        goto done;
                ret = dict_set_int32 (xdata, GLUSTERFS_DURABLE_OP, 0);
                if (ret)
                        goto dealloc_xdata;
        }

        local = br_stub_alloc_local (this);
        if (!local) {
                ret = -1;
                goto dealloc_xdata;
        }

        br_stub_fill_local (local, stub, fd->inode, fd->inode->gfid,
                            versioningtype, memversion, durable);

        frame->local = local;
        STACK_WIND (frame,
                    callback, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsetxattr, fd, dict, 0, xdata);

        ret = 0;

 dealloc_xdata:
        if (durable)
                dict_unref (xdata);
 done:
        return ret;
}

static inline int
br_stub_perform_fullversioning (xlator_t *this, call_frame_t *frame,
                                call_stub_t *stub, fd_t *fd)
{
        int32_t         ret      = -1;
        dict_t         *dict     = NULL;
        br_version_t   *obuf     = NULL;
        br_signature_t *sbuf     = NULL;
        int             op_errno = 0;

        op_errno = ENOMEM;
        dict = dict_new ();
        if (!dict)
                goto done;
        ret = br_stub_alloc_versions (&obuf, &sbuf, 0);
        if (ret)
                goto dealloc_dict;

        op_errno = EINVAL;
        ret = br_stub_prepare_default_request (this, dict, obuf, sbuf);
        if (ret)
                goto dealloc_versions;

        /**
         * Version extended attributes need not be durable at this point of
         * time. If the objects (inode) data gets persisted on disk but the
         * version extended attributes are lost due to a crash/power failure,
         * a subsequent lookup marks the objects signature as stale.This way,
         * dentry operation times do not shoot up.
         */
        ret = br_stub_fd_versioning (this, frame, stub, dict, fd,
                                     br_stub_inode_fullversioning_cbk,
                                     BITROT_DEFAULT_CURRENT_VERSION,
                                     BR_STUB_FULL_VERSIONING, !WRITEBACK_DURABLE);

 dealloc_versions:
        br_stub_dealloc_versions (obuf);
 dealloc_dict:
        dict_unref (dict);
 done:
        if (ret)
                call_unwind_error (stub, -1, op_errno);
        return ret;
}

static inline int
br_stub_perform_incversioning (xlator_t *this,
                               call_frame_t *frame, call_stub_t *stub,
                               fd_t *fd, br_stub_inode_ctx_t *ctx)
{
        int32_t        ret               = -1;
        dict_t        *dict              = NULL;
        inode_t       *inode             = NULL;
        br_version_t  *obuf              = NULL;
        gf_boolean_t   writeback         = _gf_false;
        unsigned long  writeback_version = 0;
        int            op_errno          = 0;

        inode = fd->inode;

        LOCK (&inode->lock);
        {
                ctx->transientversion++;

                if (!__br_stub_is_inode_dirty (ctx)) {
                        ctx->currentversion = ctx->transientversion;
                } else {
                        writeback = _gf_true;
                        writeback_version = ctx->transientversion;
                }
        }
        UNLOCK (&inode->lock);

        if (!writeback) {
                ret = 0;
                goto done;
        }

        /* inode requires writeback to disk */
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
                 BR_STUB_INCREMENTAL_VERSIONING, WRITEBACK_DURABLE);

 dealloc_versions:
        br_stub_dealloc_versions (obuf);
 dealloc_dict:
        dict_unref (dict);
 done:
        if (!ret && !writeback)
                call_resume (stub);
        if (ret)
                call_unwind_error (stub, -1, op_errno);
        return ret;
}

/** {{{ */

/* fsetxattr() */

static inline int
br_stub_prepare_signature (xlator_t *this, dict_t *dict,
                           inode_t *inode, br_isignature_t *sign)
{
        int32_t ret = 0;
        size_t signaturelen = 0;
        br_signature_t *sbuf = NULL;
        unsigned long version = 0;

        if (!br_is_signature_type_valid (sign->signaturetype))
                goto error_return;

        signaturelen = strlen (sign->signature);
        ret = br_stub_alloc_versions (NULL, &sbuf, signaturelen);
        if (ret)
                goto error_return;
        ret = br_stub_get_ongoing_version (this, inode, &version);
        if (ret)
                goto dealloc_versions;
        ret = br_stub_prepare_signing_request (dict, sbuf,
                                               version, sign, signaturelen);
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
br_stub_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, dict_t *xattr, dict_t *xdata)
{
        int32_t              ret          = 0;
        ssize_t              totallen     = 0;
        ssize_t              signaturelen = 0;
        br_version_t        *obuf         = NULL;
        br_signature_t      *sbuf         = NULL;
        unsigned long        version      = 0;
        gf_boolean_t         xrequested   = _gf_false;
        br_isignature_out_t *sign         = NULL;

        if (frame->local) {
                frame->local = NULL;
                xrequested = _gf_true;
        }
        if (op_ret < 0)
                goto unwind;
        if (!xrequested)
                goto unwind;

        op_ret   = -1;
        op_errno = EINVAL;

        ret = dict_get_bin (xattr, BITROT_CURRENT_VERSION_KEY, (void **)&obuf);
        ret |= dict_get_bin (xattr, BITROT_SIGNING_VERSION_KEY, (void **)&sbuf);
        if (ret)
                goto unwind;
        signaturelen = strlen (sbuf->signature);
        totallen = signaturelen + sizeof (br_isignature_out_t);

        op_errno = ENOMEM;
        sign = GF_CALLOC (1, totallen, gf_br_stub_mt_signature_t);
        if (!sign)
                goto unwind;

        version = br_stub_get_current_version (obuf, sbuf);

        sign->time[0] = obuf->timebuf[0];
        sign->time[1] = obuf->timebuf[1];

        /* Object's dirty state */
        sign->stale = (version != sbuf->signedversion) ? 1 : 0;

        /* Object's signature */
        sign->signaturetype = sbuf->signaturetype;
        (void) memcpy (sign->signature, sbuf->signature, signaturelen);

        op_errno = EINVAL;
        ret = dict_set_bin (xattr, GLUSTERFS_GET_OBJECT_SIGNATURE,
                            (void *)sign, totallen);
        if (ret < 0)
                goto unwind;
        op_errno = 0;
        op_ret = signaturelen;

 unwind:
        dict_del (xattr, BITROT_CURRENT_VERSION_KEY);
        dict_del (xattr, BITROT_SIGNING_VERSION_KEY);

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
        uuid_t rootgfid = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};

        if (br_stub_is_internal_xattr (name))
                goto wind;

        /**
         * this special extended attribute is allowed only on root
         */
        if ( name
             && (strncmp (name, GLUSTERFS_GET_BR_STUB_INIT_TIME,
                          strlen (GLUSTERFS_GET_BR_STUB_INIT_TIME)) == 0)
             && ( (uuid_compare (loc->gfid, rootgfid) == 0)
                  || (uuid_compare (loc->inode->gfid, rootgfid) == 0) ) ) {
                br_stub_send_stub_init_time (frame, this);
                return 0;
        }

        if (!IA_ISREG (loc->inode->ia_type))
                goto wind;

        if (name && (strncmp (name, GLUSTERFS_GET_OBJECT_SIGNATURE,
                              strlen (GLUSTERFS_GET_OBJECT_SIGNATURE)) == 0)) {
                frame->local = (void *) 0x1;
        }

 wind:
        STACK_WIND (frame, br_stub_getxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->getxattr, loc, name, xdata);
        return 0;
}

int
br_stub_fgetxattr (call_frame_t *frame, xlator_t *this,
                   fd_t *fd, const char *name, dict_t *xdata)
{
        uuid_t rootgfid = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};

        if (br_stub_is_internal_xattr (name))
                goto wind;

        /**
         * this special extended attribute is allowed only on root
         */
        if ( name
             && (strncmp (name, GLUSTERFS_GET_BR_STUB_INIT_TIME,
                          strlen (GLUSTERFS_GET_BR_STUB_INIT_TIME)) == 0)
             && (uuid_compare (fd->inode->gfid, rootgfid) == 0) ) {
                br_stub_send_stub_init_time (frame, this);
                return 0;
        }

        if (!IA_ISREG (fd->inode->ia_type))
                goto wind;

        if (name && (strncmp (name, GLUSTERFS_GET_OBJECT_SIGNATURE,
                              strlen (GLUSTERFS_GET_OBJECT_SIGNATURE)) == 0)) {
                frame->local = (void *) 0x1;
        }

 wind:
        STACK_WIND (frame, br_stub_getxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fgetxattr, fd, name, xdata);
        return 0;
}

/** }}} */


/** {{{ */

/* open() */

int
br_stub_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int op_ret, int op_errno, fd_t *fd, dict_t *xdata)
{
        int32_t              ret      = 0;
        uint64_t             ctx_addr = 0;
        gf_boolean_t         vercheck = _gf_false;
        br_stub_inode_ctx_t *ctx      = NULL;
        call_stub_t         *stub     = NULL;

        if (frame->local) {
                frame->local = NULL;
                vercheck = _gf_true;
        }
        if (op_ret < 0)
                goto unwind;
        if (!vercheck)
                goto unwind;

        ret = br_stub_get_inode_ctx (this, fd->inode, &ctx_addr);
        if (ret < 0)
                goto unwind;

        stub = fop_open_cbk_stub (frame, NULL, op_ret, op_errno, fd, xdata);
        if (!stub) {
                op_ret = -1;
                op_errno = EINVAL;
                goto unwind;
        }

        /**
         * Ongoing version needs to be incremented. If the inode is not dirty,
         * things are simple: increment the ongoing version safely and be done.
         * If inode is dirty, a writeback to disk is required. This is tricky in
         * case of multiple open()'s as ongoing version needs to be incremented
         * on a successful writeback. It's probably safe to remember the ongoing
         * version before writeback and *assigning* it in the callback, but that
         * may lead to a trustable checksum to be treated as stale by scrubber
         * (the case where the in-memory ongoing version is lesser than the
         * on-disk version). To counter this, the ongoing version is modified
         * only if the assigned version is greater than the current ongoing
         * version. As far as the on-disk version is concerned, it's harmless
         * as what's important is the inequality of ongoing and signing version
         * (at the time of scrub or after crash recovery). On the other hand,
         * in-memory ongoing version should reflect the maximum version amongst
         * the instances as _this_ is eventually synced to disk.
         */
        ctx = (br_stub_inode_ctx_t *) (long) ctx_addr;
        return br_stub_perform_incversioning (this, frame, stub, fd, ctx);

 unwind:
        STACK_UNWIND_STRICT (open, frame,
                             op_ret, op_errno, fd, xdata);
        return 0;
}

int
br_stub_open (call_frame_t *frame, xlator_t *this,
              loc_t *loc, int32_t flags, fd_t *fd, dict_t *xdata)
{
        if (!flags)
                goto wind;
        frame->local = (void *)0x1; /* _do not_ dereference in ->cbk */

 wind:
        STACK_WIND (frame, br_stub_open_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->open, loc, flags, fd, xdata);
        return 0;
}

/** }}} */


/** {{{ */

/* creat() */

int
br_stub_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno, fd_t *fd, inode_t *inode,
                    struct iatt *stbuf, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
{
        int32_t ret = 0;
        uint64_t ctx_addr = 0;
        call_stub_t *stub = NULL;
        br_stub_inode_ctx_t *ctx = NULL;

        if (op_ret < 0)
                goto unwind;

        stub = fop_create_cbk_stub (frame, NULL, op_ret, op_errno, fd, inode,
                                    stbuf, preparent, postparent, xdata);
        if (!stub) {
                op_ret = -1;
                op_errno = EINVAL;
                goto unwind;
        }

        ret = br_stub_get_inode_ctx (this, fd->inode, &ctx_addr);
        if (ret < 0)
                ctx_addr = 0;
        ctx = (br_stub_inode_ctx_t *) (long) ctx_addr;

        /* see comment in br_stub_open_cbk().. */
        return (ctx)
                ? br_stub_perform_incversioning (this, frame, stub, fd, ctx)
                : br_stub_perform_fullversioning (this, frame, stub, fd);

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
        STACK_WIND (frame, br_stub_create_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->create,
                    loc, flags, mode, umask, fd, xdata);
        return 0;
}

/** }}} */


/** {{{ */

/* lookup() */

int
br_stub_inode_fullversioning (xlator_t *this, call_frame_t *frame,
                              call_stub_t *stub, inode_t *inode, uuid_t gfid)

{
        int32_t          ret      = -1;
        loc_t            loc      = {0,};
        dict_t          *dict     = NULL;
        br_version_t    *obuf     = NULL;
        br_signature_t  *sbuf     = NULL;
        br_stub_local_t *local    = NULL;
        int              op_errno = 0;

        op_errno = ENOMEM;
        dict = dict_new ();
        if (!dict)
                goto done;

        op_errno = EINVAL;
        ret = br_stub_alloc_versions (&obuf, &sbuf, 0);
        if (ret)
                goto dealloc_dict;
        ret = br_stub_prepare_default_request (this, dict, obuf, sbuf);
        if (ret)
                goto dealloc_versions;

        op_errno = ENOMEM;
        local = br_stub_alloc_local (this);
        if (!local)
                goto dealloc_versions;

        br_stub_fill_local (local,
                            stub, inode, gfid, BR_STUB_FULL_VERSIONING,
                            BITROT_DEFAULT_CURRENT_VERSION, !WRITEBACK_DURABLE);
        frame->local = local;
        uuid_copy (loc.gfid, gfid);

        STACK_WIND (frame,
                    br_stub_inode_fullversioning_cbk, FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->setxattr, &loc, dict, 0, NULL);
        ret = 0;

 dealloc_versions:
        br_stub_dealloc_versions (obuf);
 dealloc_dict:
        dict_unref (dict);
 done:
        if (ret)
                call_unwind_error (stub, -1, op_errno);
        return ret;
}

int
br_stub_lookup_cbk (call_frame_t *frame, void *cookie,
                    xlator_t *this, int op_ret, int op_errno, inode_t *inode,
                    struct iatt *stbuf, dict_t *xattr, struct iatt *postparent)
{
        int32_t         ret        = 0;
        call_stub_t    *stub       = NULL;
        br_version_t   *obuf       = NULL;
        br_signature_t *sbuf       = NULL;
        unsigned long   version    = 0;
        gf_boolean_t    xrequested = _gf_false;

        if (frame->local) {
                frame->local = NULL;
                xrequested = _gf_true;
        }
        if (op_ret < 0)
                goto unwind;
        if (!IA_ISREG (stbuf->ia_type))
                goto unwind;
        if (!xrequested)
                goto unwind;

        /**
         * versioning xattrs were requested from POSIX. if available, figure
         * out the correct version to use in the inode context. On the other
         * hand, try to perform versioning. At this point, versioning xattrs
         * need not be durable on disk.. anyhow, scrubbing would skip this
         * object on version mismatches or non-existance of xattrs. The
         * protocol as of now performs non-durable versioning on lookup().
         * This can be optimized by initializing default versions in-memory
         * when on-disk xattrs are missing (or are not fully consistent).
         */
        ret = dict_get_bin (xattr, BITROT_CURRENT_VERSION_KEY, (void **)&obuf);
        ret |= dict_get_bin (xattr, BITROT_SIGNING_VERSION_KEY, (void **)&sbuf);

        if (ret == 0) {
                version = br_stub_get_current_version (obuf, sbuf);
                op_ret = br_stub_init_inode_versions (this, inode,
                                                      version, _gf_true);
                if (op_ret < 0)
                        op_errno = EINVAL;
        } else {
                stub = fop_lookup_cbk_stub (frame, NULL, op_ret, op_errno,
                                            inode, stbuf, xattr, postparent);
                if (!stub) {
                        op_ret = -1;
                        op_errno = EINVAL;
                        goto unwind;
                }

                return br_stub_inode_fullversioning (this, frame, stub,
                                                     inode, stbuf->ia_gfid);
        }

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
        op_errno = -ENOMEM;
        if (!xdata) {
                xdata = dict_new ();
                if (!xdata)
                        goto unwind;
        } else {
                xdata = dict_ref (xdata);
        }

        xref = _gf_true;

        op_errno = -EINVAL;
        ret = dict_set_uint32 (xdata, BITROT_CURRENT_VERSION_KEY, 0);
        if (ret)
                goto unwind;
        ret = dict_set_uint32 (xdata, BITROT_SIGNING_VERSION_KEY, 0);
        if (ret)
                goto unwind;
        frame->local = (void *) 0x1; /* _do not_ dereference in ->cbk */

 wind:
        STACK_WIND (frame,
                    br_stub_lookup_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup, loc, xdata);
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

int32_t
br_stub_flush_cbk (call_frame_t *frame, void *cookie,
                   xlator_t *this, int op_ret, int op_errno, dict_t *xdata)
{
        int32_t              ret            = -1;
        fd_t                *fd             = NULL;
        uint64_t             ctx_addr       = 0;
        inode_t             *inode          = NULL;
        br_stub_inode_ctx_t *ctx            = NULL;
        unsigned long        releaseversion = 0;

        fd = (fd_t *)frame->local;
        frame->local = NULL;

        if (op_ret < 0)
                goto unwind;
        inode = fd->inode;

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_get (inode, this, &ctx_addr);
                if (ret < 0)
                        goto unblock;
                ctx = (br_stub_inode_ctx_t *) (long) ctx_addr;
                releaseversion = htonl (ctx->currentversion);
        }
 unblock:
        UNLOCK (&inode->lock);

        if (!ctx)
                goto unwind;
        if (!xdata) {
                xdata = dict_new ();
                if (!xdata) {
                        ret = -1;
                        goto unwind;
                }
        } else {
                xdata = dict_ref (xdata);
        }

        /**
         * TODO: refresh versions if the ongoingversion was modified since the
         * last time a verison refresh was sent.
         */
        ret = dict_set_uint64
                (xdata, GLUSTERFS_VERSION_XCHG_KEY, (uint64_t) releaseversion);

 unwind:
        if (ret)
                gf_log (this->name, GF_LOG_WARNING,
                        "Could not fetch current version from inode context. "
                        "Signature would most probably be invalid [Object: %s]",
                        uuid_utoa (inode->gfid));

        STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno, xdata);

        if (xdata)
                dict_unref (xdata);

        fd_unref (fd); /* NOTE: this would not trigger a release() */

        return 0;
}

/**
 * Why is flush needed?
 *
 * Object's are candidates for signing upon release(). Object's signature
 * is _tied_ to a particular version. This allows us to validate an object's
 * signature by comparing the signed version with the current (ongoing)
 * version. This exposes a hairy race between release() trying to fetch
 * the version against which an object is deemed to be signed and open()
 * trying to increment the ongoing version. If open() races ahead, release()
 * would pick the version that currently has an open file desciptor (and
 * possbily ongoing IO operations), which the bitrot daemon would happily
 * sign. The signature would most probably be incorrect but _trustable_
 * as the signed version matches ongoing version.
 *
 * Therefore, the version to be signed against is maintained separately,
 * which is _refreshed_ on every flush. Since a flush() is guaranteed
 * before a release, this version closesly reflects the ongoing version.
 * Another key point is that an object is signed after all file descriptors
 * open()'d on it is close()'ed. This along with maintaining a side-copy of
 * the ongoing version (release version) takes care of correct _attachment_
 * of a signature against it's version.
 *
 * Furthermore, the version needs to be _handed over_ to changelog translator,
 * for client notification. As of now, this is done by using xdata (which is
 * kind of ugly as that forces bit-rot stub to be places after changelog in
 * the graph). This should probably be cleaned up (or even taken away if
 * the object signer is made a part of the server graph itself).
 */
int32_t
br_stub_flush (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        frame->local = fd_ref (fd);

        STACK_WIND (frame, br_stub_flush_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->flush, fd, xdata);
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


struct xlator_fops fops = {
        .lookup    = br_stub_lookup,
        .open      = br_stub_open,
        .flush     = br_stub_flush,
        .create    = br_stub_create,
        .getxattr  = br_stub_getxattr,
        .fgetxattr = br_stub_fgetxattr,
        .fsetxattr = br_stub_fsetxattr,
};

struct xlator_cbks cbks = {
        .forget = br_stub_forget,
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
