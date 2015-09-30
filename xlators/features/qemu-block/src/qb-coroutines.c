/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
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

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "inode.h"
#include "call-stub.h"
#include "defaults.h"
#include "qemu-block-memory-types.h"
#include "qemu-block.h"
#include "qb-coroutines.h"


int
qb_format_and_resume (void *opaque)
{
	qb_local_t *local = NULL;
	call_frame_t *frame = NULL;
	call_stub_t *stub = NULL;
	inode_t *inode = NULL;
	char filename[64];
	char base_filename[128];
	int use_base = 0;
	qb_inode_t *qb_inode = NULL;
	Error *local_err = NULL;
	fd_t *fd = NULL;
	dict_t *xattr = NULL;
	qb_conf_t *qb_conf = NULL;
	int ret = -1;

	local = opaque;
	frame = local->frame;
	stub = local->stub;
	inode = local->inode;
	qb_conf = frame->this->private;

	qb_inode_to_filename (inode, filename, 64);

	qb_inode = qb_inode_ctx_get (frame->this, inode);

	/*
	 * See if the caller specified a backing image.
	 */
	if (!uuid_is_null(qb_inode->backing_gfid) || qb_inode->backing_fname) {
		loc_t loc = {0,};
		char gfid_str[64];
		struct iatt buf;

		if (!uuid_is_null(qb_inode->backing_gfid)) {
			loc.inode = inode_find(qb_conf->root_inode->table,
					qb_inode->backing_gfid);
			if (!loc.inode) {
				loc.inode = inode_new(qb_conf->root_inode->table);
				uuid_copy(loc.inode->gfid,
					qb_inode->backing_gfid);
			}
			uuid_copy(loc.gfid, loc.inode->gfid);
		} else if (qb_inode->backing_fname) {
			loc.inode = inode_new(qb_conf->root_inode->table);
			loc.name = qb_inode->backing_fname;
			loc.parent = inode_parent(inode, NULL, NULL);
			loc_path(&loc, loc.name);
		}

		/*
		 * Lookup the backing image. Verify existence and/or get the
		 * gfid if we don't already have it.
		 */
		ret = syncop_lookup(FIRST_CHILD(frame->this), &loc, NULL, &buf,
				    NULL, NULL);
		GF_FREE(qb_inode->backing_fname);
		if (ret) {
			loc_wipe(&loc);
			ret = -ret;
			goto err;
		}

		uuid_copy(qb_inode->backing_gfid, buf.ia_gfid);
		loc_wipe(&loc);

		/*
		 * We pass the filename of the backing image into the qemu block
		 * subsystem as the associated gfid. This is embedded into the
		 * clone image and passed along to the gluster bdrv backend when
		 * the block subsystem needs to operate on the backing image on
		 * behalf of the clone.
		 */
		uuid_unparse(qb_inode->backing_gfid, gfid_str);
		snprintf(base_filename, sizeof(base_filename),
			 "gluster://gfid:%s", gfid_str);
		use_base = 1;
	}

	bdrv_img_create (filename, qb_inode->fmt,
			 use_base ? base_filename : NULL, 0, 0, qb_inode->size,
			 0, &local_err, true);

	if (error_is_set (&local_err)) {
		gf_log (frame->this->name, GF_LOG_ERROR, "%s",
			error_get_pretty (local_err));
		error_free (local_err);
		QB_STUB_UNWIND (stub, -1, EIO);
		return 0;
	}

	fd = fd_anonymous (inode);
	if (!fd) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"could not create anonymous fd for %s",
			uuid_utoa (inode->gfid));
		QB_STUB_UNWIND (stub, -1, ENOMEM);
		return 0;
	}

	xattr = dict_new ();
	if (!xattr) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"could not allocate xattr dict for %s",
			uuid_utoa (inode->gfid));
		QB_STUB_UNWIND (stub, -1, ENOMEM);
		fd_unref (fd);
		return 0;
	}

	ret = dict_set_str (xattr, qb_conf->qb_xattr_key, local->fmt);
	if (ret) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"could not dict_set for %s",
			uuid_utoa (inode->gfid));
		QB_STUB_UNWIND (stub, -1, ENOMEM);
		fd_unref (fd);
		dict_unref (xattr);
		return 0;
	}

	ret = syncop_fsetxattr (FIRST_CHILD(THIS), fd, xattr, 0);
	if (ret) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"failed to setxattr for %s",
			uuid_utoa (inode->gfid));
		QB_STUB_UNWIND (stub, -1, -ret);
		fd_unref (fd);
		dict_unref (xattr);
		return 0;
	}

	fd_unref (fd);
	dict_unref (xattr);

	QB_STUB_UNWIND (stub, 0, 0);

	return 0;

err:
	QB_STUB_UNWIND(stub, -1, ret);
	return 0;
}


static BlockDriverState *
qb_bs_create (inode_t *inode, const char *fmt)
{
	char filename[64];
	BlockDriverState *bs = NULL;
	BlockDriver *drv = NULL;
	int op_errno = 0;
	int ret = 0;

	bs = bdrv_new (uuid_utoa (inode->gfid));
	if (!bs) {
		op_errno = ENOMEM;
		gf_log (THIS->name, GF_LOG_ERROR,
			"could not allocate @bdrv for gfid:%s",
			uuid_utoa (inode->gfid));
		goto err;
	}

	drv = bdrv_find_format (fmt);
	if (!drv) {
		op_errno = EINVAL;
		gf_log (THIS->name, GF_LOG_ERROR,
			"Unknown file format: %s for gfid:%s",
			fmt, uuid_utoa (inode->gfid));
		goto err;
	}

	qb_inode_to_filename (inode, filename, 64);

	ret = bdrv_open (bs, filename, NULL, BDRV_O_RDWR, drv);
	if (ret < 0) {
		op_errno = -ret;
		gf_log (THIS->name, GF_LOG_ERROR,
			"Unable to bdrv_open() gfid:%s (%s)",
			uuid_utoa (inode->gfid), strerror (op_errno));
		goto err;
	}

	return bs;
err:
	errno = op_errno;
	return NULL;
}


int
qb_co_open (void *opaque)
{
	qb_local_t *local = NULL;
	call_frame_t *frame = NULL;
	call_stub_t *stub = NULL;
	inode_t *inode = NULL;
	qb_inode_t *qb_inode = NULL;

	local = opaque;
	frame = local->frame;
	stub = local->stub;
	inode = local->inode;

	qb_inode = qb_inode_ctx_get (frame->this, inode);
	if (!qb_inode->bs) {
		/* FIXME: we need locks around this when
		   enabling multithreaded syncop/coroutine
		   for qemu-block
		*/

		qb_inode->bs = qb_bs_create (inode, qb_inode->fmt);
		if (!qb_inode->bs) {
			QB_STUB_UNWIND (stub, -1, errno);
			return 0;
		}
	}
	qb_inode->refcnt++;

	QB_STUB_RESUME (stub);

	return 0;
}


int
qb_co_writev (void *opaque)
{
	qb_local_t *local = NULL;
	call_frame_t *frame = NULL;
	call_stub_t *stub = NULL;
	inode_t *inode = NULL;
	qb_inode_t *qb_inode = NULL;
	QEMUIOVector qiov = {0, };
	int ret = 0;

	local = opaque;
	frame = local->frame;
	stub = local->stub;
	inode = local->inode;

	qb_inode = qb_inode_ctx_get (frame->this, inode);
	if (!qb_inode->bs) {
		/* FIXME: we need locks around this when
		   enabling multithreaded syncop/coroutine
		   for qemu-block
		*/

		qb_inode->bs = qb_bs_create (inode, qb_inode->fmt);
		if (!qb_inode->bs) {
			QB_STUB_UNWIND (stub, -1, errno);
			return 0;
		}
	}

	qemu_iovec_init_external (&qiov, stub->args.vector, stub->args.count);

	ret = bdrv_pwritev (qb_inode->bs, stub->args.offset, &qiov);

	if (ret < 0) {
		QB_STUB_UNWIND (stub, -1, -ret);
	} else {
		QB_STUB_UNWIND (stub, ret, 0);
	}

	return 0;
}


int
qb_co_readv (void *opaque)
{
	qb_local_t *local = NULL;
	call_frame_t *frame = NULL;
	call_stub_t *stub = NULL;
	inode_t *inode = NULL;
	qb_inode_t *qb_inode = NULL;
	struct iobuf *iobuf = NULL;
	struct iobref *iobref = NULL;
	struct iovec iov = {0, };
	int ret = 0;

	local = opaque;
	frame = local->frame;
	stub = local->stub;
	inode = local->inode;

	qb_inode = qb_inode_ctx_get (frame->this, inode);
	if (!qb_inode->bs) {
		/* FIXME: we need locks around this when
		   enabling multithreaded syncop/coroutine
		   for qemu-block
		*/

		qb_inode->bs = qb_bs_create (inode, qb_inode->fmt);
		if (!qb_inode->bs) {
			QB_STUB_UNWIND (stub, -1, errno);
			return 0;
		}
	}

	if (stub->args.offset >= qb_inode->size) {
		QB_STUB_UNWIND (stub, 0, 0);
		return 0;
	}

	iobuf = iobuf_get2 (frame->this->ctx->iobuf_pool, stub->args.size);
	if (!iobuf) {
		QB_STUB_UNWIND (stub, -1, ENOMEM);
		return 0;
	}

	iobref = iobref_new ();
	if (!iobref) {
		QB_STUB_UNWIND (stub, -1, ENOMEM);
		iobuf_unref (iobuf);
		return 0;
	}

	if (iobref_add (iobref, iobuf) < 0) {
		iobuf_unref (iobuf);
		iobref_unref (iobref);
		QB_STUB_UNWIND (stub, -1, ENOMEM);
		return 0;
	}

	ret = bdrv_pread (qb_inode->bs, stub->args.offset, iobuf_ptr (iobuf),
			  stub->args.size);

	if (ret < 0) {
		QB_STUB_UNWIND (stub, -1, -ret);
		iobref_unref (iobref);
		return 0;
	}

	iov.iov_base = iobuf_ptr (iobuf);
	iov.iov_len = ret;

	stub->args_cbk.vector = iov_dup (&iov, 1);
	stub->args_cbk.count = 1;
	stub->args_cbk.iobref = iobref;

	QB_STUB_UNWIND (stub, ret, 0);

	return 0;
}


int
qb_co_fsync (void *opaque)
{
	qb_local_t *local = NULL;
	call_frame_t *frame = NULL;
	call_stub_t *stub = NULL;
	inode_t *inode = NULL;
	qb_inode_t *qb_inode = NULL;
	int ret = 0;

	local = opaque;
	frame = local->frame;
	stub = local->stub;
	inode = local->inode;

	qb_inode = qb_inode_ctx_get (frame->this, inode);
	if (!qb_inode->bs) {
		/* FIXME: we need locks around this when
		   enabling multithreaded syncop/coroutine
		   for qemu-block
		*/

		qb_inode->bs = qb_bs_create (inode, qb_inode->fmt);
		if (!qb_inode->bs) {
			QB_STUB_UNWIND (stub, -1, errno);
			return 0;
		}
	}

	ret = bdrv_flush (qb_inode->bs);

	if (ret < 0) {
		QB_STUB_UNWIND (stub, -1, -ret);
	} else {
		QB_STUB_UNWIND (stub, ret, 0);
	}

	return 0;
}


static void
qb_update_size_xattr (xlator_t *this, fd_t *fd, const char *fmt, off_t offset)
{
	char val[QB_XATTR_VAL_MAX];
	qb_conf_t *qb_conf = NULL;
	dict_t *xattr = NULL;

	qb_conf = this->private;

	snprintf (val, QB_XATTR_VAL_MAX, "%s:%llu",
		  fmt, (long long unsigned) offset);

	xattr = dict_new ();
	if (!xattr)
		return;

	if (dict_set_str (xattr, qb_conf->qb_xattr_key, val) != 0) {
		dict_unref (xattr);
		return;
	}

	syncop_fsetxattr (FIRST_CHILD(this), fd, xattr, 0);
	dict_unref (xattr);
}


int
qb_co_truncate (void *opaque)
{
	qb_local_t *local = NULL;
	call_frame_t *frame = NULL;
	call_stub_t *stub = NULL;
	inode_t *inode = NULL;
	qb_inode_t *qb_inode = NULL;
	int ret = 0;
	off_t offset = 0;
	xlator_t *this = NULL;

	this = THIS;

	local = opaque;
	frame = local->frame;
	stub = local->stub;
	inode = local->inode;

	qb_inode = qb_inode_ctx_get (frame->this, inode);
	if (!qb_inode->bs) {
		/* FIXME: we need locks around this when
		   enabling multithreaded syncop/coroutine
		   for qemu-block
		*/

		qb_inode->bs = qb_bs_create (inode, qb_inode->fmt);
		if (!qb_inode->bs) {
			QB_STUB_UNWIND (stub, -1, errno);
			return 0;
		}
	}

	ret = syncop_fstat (FIRST_CHILD(this), local->fd,
                            &stub->args_cbk.prestat);
        if (ret < 0)
                goto out;
	stub->args_cbk.prestat.ia_size = qb_inode->size;

	ret = bdrv_truncate (qb_inode->bs, stub->args.offset);
	if (ret < 0)
		goto out;

	offset = bdrv_getlength (qb_inode->bs);

	qb_inode->size = offset;

	ret = syncop_fstat (FIRST_CHILD(this), local->fd,
                            &stub->args_cbk.poststat);
        if (ret < 0)
                goto out;
	stub->args_cbk.poststat.ia_size = qb_inode->size;

	qb_update_size_xattr (this, local->fd, qb_inode->fmt, qb_inode->size);

out:
	if (ret < 0) {
		QB_STUB_UNWIND (stub, -1, -ret);
	} else {
		QB_STUB_UNWIND (stub, ret, 0);
	}

	return 0;
}


int
qb_co_close (void *opaque)
{
	qb_local_t *local = NULL;
	call_frame_t *frame = NULL;
	inode_t *inode = NULL;
	qb_inode_t *qb_inode = NULL;
	BlockDriverState *bs = NULL;

	local = opaque;
	inode = local->inode;

	qb_inode = qb_inode_ctx_get (THIS, inode);

	if (!--qb_inode->refcnt) {
		bs = qb_inode->bs;
		qb_inode->bs = NULL;
		bdrv_delete (bs);
	}

	frame = local->frame;
	frame->local = NULL;
	qb_local_free (THIS, local);
	STACK_DESTROY (frame->root);

	return 0;
}


int
qb_snapshot_create (void *opaque)
{
	qb_local_t *local = NULL;
	call_frame_t *frame = NULL;
	call_stub_t *stub = NULL;
	inode_t *inode = NULL;
	qb_inode_t *qb_inode = NULL;
	QEMUSnapshotInfo sn;
	struct timeval tv = {0, };
	int ret = 0;

	local = opaque;
	frame = local->frame;
	stub = local->stub;
	inode = local->inode;

	qb_inode = qb_inode_ctx_get (frame->this, inode);
	if (!qb_inode->bs) {
		/* FIXME: we need locks around this when
		   enabling multithreaded syncop/coroutine
		   for qemu-block
		*/

		qb_inode->bs = qb_bs_create (inode, qb_inode->fmt);
		if (!qb_inode->bs) {
			QB_STUB_UNWIND (stub, -1, errno);
			return 0;
		}
	}

	memset (&sn, 0, sizeof (sn));
        pstrcpy (sn.name, sizeof(sn.name), local->name);
        gettimeofday (&tv, NULL);
        sn.date_sec = tv.tv_sec;
        sn.date_nsec = tv.tv_usec * 1000;

        ret = bdrv_snapshot_create (qb_inode->bs, &sn);
	if (ret < 0) {
		QB_STUB_UNWIND (stub, -1, -ret);
	} else {
		QB_STUB_UNWIND (stub, ret, 0);
	}

	return 0;
}


int
qb_snapshot_delete (void *opaque)
{
	qb_local_t *local = NULL;
	call_frame_t *frame = NULL;
	call_stub_t *stub = NULL;
	inode_t *inode = NULL;
	qb_inode_t *qb_inode = NULL;
	int ret = 0;

	local = opaque;
	frame = local->frame;
	stub = local->stub;
	inode = local->inode;

	qb_inode = qb_inode_ctx_get (frame->this, inode);
	if (!qb_inode->bs) {
		/* FIXME: we need locks around this when
		   enabling multithreaded syncop/coroutine
		   for qemu-block
		*/

		qb_inode->bs = qb_bs_create (inode, qb_inode->fmt);
		if (!qb_inode->bs) {
			QB_STUB_UNWIND (stub, -1, errno);
			return 0;
		}
	}

	ret = bdrv_snapshot_delete (qb_inode->bs, local->name);

	if (ret < 0) {
		QB_STUB_UNWIND (stub, -1, -ret);
	} else {
		QB_STUB_UNWIND (stub, ret, 0);
	}

	return 0;
}


int
qb_snapshot_goto (void *opaque)
{
	qb_local_t *local = NULL;
	call_frame_t *frame = NULL;
	call_stub_t *stub = NULL;
	inode_t *inode = NULL;
	qb_inode_t *qb_inode = NULL;
	int ret = 0;

	local = opaque;
	frame = local->frame;
	stub = local->stub;
	inode = local->inode;

	qb_inode = qb_inode_ctx_get (frame->this, inode);
	if (!qb_inode->bs) {
		/* FIXME: we need locks around this when
		   enabling multithreaded syncop/coroutine
		   for qemu-block
		*/

		qb_inode->bs = qb_bs_create (inode, qb_inode->fmt);
		if (!qb_inode->bs) {
			QB_STUB_UNWIND (stub, -1, errno);
			return 0;
		}
	}

	ret = bdrv_snapshot_goto (qb_inode->bs, local->name);

	if (ret < 0) {
		QB_STUB_UNWIND (stub, -1, -ret);
	} else {
		QB_STUB_UNWIND (stub, ret, 0);
	}

	return 0;
}
