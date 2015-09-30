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


qb_inode_t *
__qb_inode_ctx_get (xlator_t *this, inode_t *inode)
{
        uint64_t    value    = 0;
        qb_inode_t *qb_inode = NULL;

        __inode_ctx_get (inode, this, &value);
        qb_inode = (qb_inode_t *)(unsigned long) value;

        return qb_inode;
}


qb_inode_t *
qb_inode_ctx_get (xlator_t *this, inode_t *inode)
{
        qb_inode_t *qb_inode = NULL;

        LOCK (&inode->lock);
        {
                qb_inode = __qb_inode_ctx_get (this, inode);
        }
        UNLOCK (&inode->lock);

        return qb_inode;
}


qb_inode_t *
qb_inode_ctx_del (xlator_t *this, inode_t *inode)
{
        uint64_t    value    = 0;
        qb_inode_t *qb_inode = NULL;

        inode_ctx_del (inode, this, &value);
        qb_inode = (qb_inode_t *)(unsigned long) value;

        return qb_inode;
}


int
qb_inode_cleanup (xlator_t *this, inode_t *inode, int warn)
{
	qb_inode_t *qb_inode = NULL;

	qb_inode = qb_inode_ctx_del (this, inode);

	if (!qb_inode)
		return 0;

	if (warn)
		gf_log (this->name, GF_LOG_WARNING,
			"inode %s no longer block formatted",
			uuid_utoa (inode->gfid));

	/* free (qb_inode->bs); */

	GF_FREE (qb_inode);

	return 0;
}


int
qb_iatt_fixup (xlator_t *this, inode_t *inode, struct iatt *iatt)
{
	qb_inode_t *qb_inode = NULL;

	qb_inode = qb_inode_ctx_get (this, inode);
	if (!qb_inode)
		return 0;

	iatt->ia_size = qb_inode->size;

	return 0;
}


int
qb_format_extract (xlator_t *this, char *format, inode_t *inode)
{
	char       *s, *save;
	uint64_t    size = 0;
	char        fmt[QB_XATTR_VAL_MAX+1] = {0, };
	qb_inode_t *qb_inode = NULL;
	char *formatstr = NULL;
	uuid_t gfid = {0,};
	char gfid_str[64] = {0,};
	int ret;

	strncpy(fmt, format, QB_XATTR_VAL_MAX);

	s = strtok_r(fmt, ":", &save);
	if (!s)
		goto invalid;
	formatstr = gf_strdup(s);

	s = strtok_r(NULL, ":", &save);
	if (!s)
		goto invalid;
	if (gf_string2bytesize (s, &size))
		goto invalid;
	if (!size)
		goto invalid;

	s = strtok_r(NULL, "\0", &save);
	if (s && !strncmp(s, "<gfid:", strlen("<gfid:"))) {
		/*
		 * Check for valid gfid backing image specifier.
		 */
		if (strlen(s) + 1 > sizeof(gfid_str))
			goto invalid;
		ret = sscanf(s, "<gfid:%[^>]s", gfid_str);
		if (ret == 1) {
			ret = uuid_parse(gfid_str, gfid);
			if (ret < 0)
				goto invalid;
		}
	}

	qb_inode = qb_inode_ctx_get (this, inode);
	if (!qb_inode)
		qb_inode = GF_CALLOC (1, sizeof (*qb_inode),
				      gf_qb_mt_qb_inode_t);
	if (!qb_inode) {
		GF_FREE(formatstr);
		return ENOMEM;
	}

	strncpy(qb_inode->fmt, formatstr, QB_XATTR_VAL_MAX);
	qb_inode->size = size;

	/*
	 * If a backing gfid was not specified, interpret any remaining bytes
	 * associated with a backing image as a filename local to the parent
	 * directory. The format processing will validate further.
	 */
	if (!uuid_is_null(gfid))
		uuid_copy(qb_inode->backing_gfid, gfid);
	else if (s)
		qb_inode->backing_fname = gf_strdup(s);

	inode_ctx_set (inode, this, (void *)&qb_inode);

	GF_FREE(formatstr);

	return 0;

invalid:
	GF_FREE(formatstr);

	gf_log (this->name, GF_LOG_WARNING,
		"invalid format '%s' in inode %s", format,
		uuid_utoa (inode->gfid));
	return EINVAL;
}


void
qb_local_free (xlator_t *this, qb_local_t *local)
{
	if (local->inode)
		inode_unref (local->inode);
	if (local->fd)
		fd_unref (local->fd);
	GF_FREE (local);
}


int
qb_local_init (call_frame_t *frame)
{
	qb_local_t *qb_local = NULL;

	qb_local = GF_CALLOC (1, sizeof (*qb_local), gf_qb_mt_qb_local_t);
	if (!qb_local)
		return -1;
	INIT_LIST_HEAD(&qb_local->list);

	qb_local->frame = frame;
	frame->local = qb_local;

	return 0;
}


int
qb_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	       int op_ret, int op_errno, inode_t *inode, struct iatt *buf,
	       dict_t *xdata, struct iatt *postparent)
{
	char *format = NULL;
	qb_conf_t *conf = NULL;

	conf = this->private;

	if (op_ret == -1)
		goto out;

	/*
	 * Cache the root inode for dealing with backing images. The format
	 * coroutine and the gluster qemu backend driver both use the root inode
	 * table to verify and/or redirect I/O to the backing image via
	 * anonymous fd's.
	 */
	if (!conf->root_inode && __is_root_gfid(inode->gfid))
		conf->root_inode = inode_ref(inode);

	if (!xdata)
		goto out;

	if (dict_get_str (xdata, conf->qb_xattr_key, &format))
		goto out;

	if (!format) {
		qb_inode_cleanup (this, inode, 1);
		goto out;
	}

	op_errno = qb_format_extract (this, format, inode);
	if (op_errno)
		op_ret = -1;

	qb_iatt_fixup (this, inode, buf);
out:
	QB_STACK_UNWIND (lookup, frame, op_ret, op_errno, inode, buf,
			 xdata, postparent);
	return 0;
}


int
qb_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
	qb_conf_t *conf = NULL;

	conf = this->private;

	xdata = xdata ? dict_ref (xdata) : dict_new ();

	if (!xdata)
		goto enomem;

	if (dict_set_int32 (xdata, conf->qb_xattr_key, 0))
		goto enomem;

	STACK_WIND (frame, qb_lookup_cbk, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->lookup, loc, xdata);
	dict_unref (xdata);
	return 0;
enomem:
	QB_STACK_UNWIND (lookup, frame, -1, ENOMEM, 0, 0, 0, 0);
	if (xdata)
		dict_unref (xdata);
	return 0;
}


int
qb_setxattr_format (call_frame_t *frame, xlator_t *this, call_stub_t *stub,
		    dict_t *xattr, inode_t *inode)
{
	char *format = NULL;
	int op_errno = 0;
	qb_local_t *qb_local = NULL;
	data_t *data = NULL;
	qb_inode_t *qb_inode;

	if (!(data = dict_get (xattr, "trusted.glusterfs.block-format"))) {
		QB_STUB_RESUME (stub);
		return 0;
	}

	format = alloca (data->len + 1);
	memcpy (format, data->data, data->len);
	format[data->len] = 0;

	op_errno = qb_format_extract (this, format, inode);
	if (op_errno) {
		QB_STUB_UNWIND (stub, -1, op_errno);
		return 0;
	}
	qb_inode = qb_inode_ctx_get(this, inode);

	qb_local = frame->local;

	qb_local->stub = stub;
	qb_local->inode = inode_ref (inode);

	snprintf(qb_local->fmt, QB_XATTR_VAL_MAX, "%s:%" PRId64, qb_inode->fmt,
		 qb_inode->size);

	qb_coroutine (frame, qb_format_and_resume);

	return 0;
}


int
qb_setxattr_snapshot_create (call_frame_t *frame, xlator_t *this,
			     call_stub_t *stub, dict_t *xattr, inode_t *inode)
{
	qb_local_t *qb_local = NULL;
	char *name = NULL;
	data_t *data = NULL;

	if (!(data = dict_get (xattr, "trusted.glusterfs.block-snapshot-create"))) {
		QB_STUB_RESUME (stub);
		return 0;
	}

	name = alloca (data->len + 1);
	memcpy (name, data->data, data->len);
	name[data->len] = 0;

	qb_local = frame->local;

	qb_local->stub = stub;
	qb_local->inode = inode_ref (inode);
	strncpy (qb_local->name, name, 128);

	qb_coroutine (frame, qb_snapshot_create);

	return 0;
}


int
qb_setxattr_snapshot_delete (call_frame_t *frame, xlator_t *this,
			     call_stub_t *stub, dict_t *xattr, inode_t *inode)
{
	qb_local_t *qb_local = NULL;
	char *name = NULL;
	data_t *data = NULL;

	if (!(data = dict_get (xattr, "trusted.glusterfs.block-snapshot-delete"))) {
		QB_STUB_RESUME (stub);
		return 0;
	}

	name = alloca (data->len + 1);
	memcpy (name, data->data, data->len);
	name[data->len] = 0;

	qb_local = frame->local;

	qb_local->stub = stub;
	qb_local->inode = inode_ref (inode);
	strncpy (qb_local->name, name, 128);

	qb_coroutine (frame, qb_snapshot_delete);

	return 0;
}

int
qb_setxattr_snapshot_goto (call_frame_t *frame, xlator_t *this,
			   call_stub_t *stub, dict_t *xattr, inode_t *inode)
{
	qb_local_t *qb_local = NULL;
	char *name = NULL;
	data_t *data = NULL;

	if (!(data = dict_get (xattr, "trusted.glusterfs.block-snapshot-goto"))) {
		QB_STUB_RESUME (stub);
		return 0;
	}

	name = alloca (data->len + 1);
	memcpy (name, data->data, data->len);
	name[data->len] = 0;

	qb_local = frame->local;

	qb_local->stub = stub;
	qb_local->inode = inode_ref (inode);
	strncpy (qb_local->name, name, 128);

	qb_coroutine (frame, qb_snapshot_goto);

	return 0;
}


int
qb_setxattr_common (call_frame_t *frame, xlator_t *this, call_stub_t *stub,
		    dict_t *xattr, inode_t *inode)
{
	data_t *data = NULL;

	if ((data = dict_get (xattr, "trusted.glusterfs.block-format"))) {
		qb_setxattr_format (frame, this, stub, xattr, inode);
		return 0;
	}

	if ((data = dict_get (xattr, "trusted.glusterfs.block-snapshot-create"))) {
		qb_setxattr_snapshot_create (frame, this, stub, xattr, inode);
		return 0;
	}

	if ((data = dict_get (xattr, "trusted.glusterfs.block-snapshot-delete"))) {
		qb_setxattr_snapshot_delete (frame, this, stub, xattr, inode);
		return 0;
	}

	if ((data = dict_get (xattr, "trusted.glusterfs.block-snapshot-goto"))) {
		qb_setxattr_snapshot_goto (frame, this, stub, xattr, inode);
		return 0;
	}

	QB_STUB_RESUME (stub);

	return 0;
}


int
qb_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xattr,
	     int flags, dict_t *xdata)
{
	call_stub_t *stub = NULL;

	if (qb_local_init (frame) != 0)
		goto enomem;

	stub = fop_setxattr_stub (frame, default_setxattr_resume, loc, xattr,
				  flags, xdata);
	if (!stub)
		goto enomem;

	qb_setxattr_common (frame, this, stub, xattr, loc->inode);

	return 0;
enomem:
	QB_STACK_UNWIND (setxattr, frame, -1, ENOMEM, 0);
	return 0;
}


int
qb_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xattr,
	      int flags, dict_t *xdata)
{
	call_stub_t *stub = NULL;

	if (qb_local_init (frame) != 0)
		goto enomem;

	stub = fop_fsetxattr_stub (frame, default_fsetxattr_resume, fd, xattr,
				   flags, xdata);
	if (!stub)
		goto enomem;

	qb_setxattr_common (frame, this, stub, xattr, fd->inode);

	return 0;
enomem:
	QB_STACK_UNWIND (fsetxattr, frame, -1, ENOMEM, 0);
	return 0;
}


int
qb_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	     int op_ret, int op_errno, fd_t *fd, dict_t *xdata)
{
	call_stub_t *stub = NULL;
	qb_local_t *qb_local = NULL;

	qb_local = frame->local;

	if (op_ret < 0)
		goto unwind;

	if (!qb_inode_ctx_get (this, qb_local->inode))
		goto unwind;

	stub = fop_open_cbk_stub (frame, NULL, op_ret, op_errno, fd, xdata);
	if (!stub) {
		op_ret = -1;
		op_errno = ENOMEM;
		goto unwind;
	}

	qb_local->stub = stub;

	qb_coroutine (frame, qb_co_open);

	return 0;
unwind:
	QB_STACK_UNWIND (open, frame, op_ret, op_errno, fd, xdata);
	return 0;
}


int
qb_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
	 fd_t *fd, dict_t *xdata)
{
	qb_local_t *qb_local = NULL;
	qb_inode_t *qb_inode = NULL;

	qb_inode = qb_inode_ctx_get (this, loc->inode);
	if (!qb_inode) {
		STACK_WIND (frame, default_open_cbk, FIRST_CHILD(this),
			    FIRST_CHILD(this)->fops->open, loc, flags, fd,
			    xdata);
		return 0;
	}

	if (qb_local_init (frame) != 0)
		goto enomem;

	qb_local = frame->local;

	qb_local->inode = inode_ref (loc->inode);
	qb_local->fd = fd_ref (fd);

	STACK_WIND (frame, qb_open_cbk, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->open, loc, flags, fd, xdata);
	return 0;
enomem:
	QB_STACK_UNWIND (open, frame, -1, ENOMEM, 0, 0);
	return 0;
}


int
qb_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
	   int count, off_t offset, uint32_t flags, struct iobref *iobref,
	   dict_t *xdata)
{
	qb_local_t *qb_local = NULL;
	qb_inode_t *qb_inode = NULL;

	qb_inode = qb_inode_ctx_get (this, fd->inode);
	if (!qb_inode) {
		STACK_WIND (frame, default_writev_cbk, FIRST_CHILD(this),
			    FIRST_CHILD(this)->fops->writev, fd, vector, count,
			    offset, flags, iobref, xdata);
		return 0;
	}

	if (qb_local_init (frame) != 0)
		goto enomem;

	qb_local = frame->local;

	qb_local->inode = inode_ref (fd->inode);
	qb_local->fd = fd_ref (fd);

	qb_local->stub = fop_writev_stub (frame, NULL, fd, vector, count,
					  offset, flags, iobref, xdata);
	if (!qb_local->stub)
		goto enomem;

	qb_coroutine (frame, qb_co_writev);

	return 0;
enomem:
	QB_STACK_UNWIND (writev, frame, -1, ENOMEM, 0, 0, 0);
	return 0;
}


int
qb_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
	  off_t offset, uint32_t flags, dict_t *xdata)
{
	qb_local_t *qb_local = NULL;
	qb_inode_t *qb_inode = NULL;

	qb_inode = qb_inode_ctx_get (this, fd->inode);
	if (!qb_inode) {
		STACK_WIND (frame, default_readv_cbk, FIRST_CHILD(this),
			    FIRST_CHILD(this)->fops->readv, fd, size, offset,
			    flags, xdata);
		return 0;
	}

	if (qb_local_init (frame) != 0)
		goto enomem;

	qb_local = frame->local;

	qb_local->inode = inode_ref (fd->inode);
	qb_local->fd = fd_ref (fd);

	qb_local->stub = fop_readv_stub (frame, NULL, fd, size, offset,
					  flags, xdata);
	if (!qb_local->stub)
		goto enomem;

	qb_coroutine (frame, qb_co_readv);

	return 0;
enomem:
	QB_STACK_UNWIND (readv, frame, -1, ENOMEM, 0, 0, 0, 0, 0);
	return 0;
}


int
qb_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int dsync,
	  dict_t *xdata)
{
	qb_local_t *qb_local = NULL;
	qb_inode_t *qb_inode = NULL;

	qb_inode = qb_inode_ctx_get (this, fd->inode);
	if (!qb_inode) {
		STACK_WIND (frame, default_fsync_cbk, FIRST_CHILD(this),
			    FIRST_CHILD(this)->fops->fsync, fd, dsync, xdata);
		return 0;
	}

	if (qb_local_init (frame) != 0)
		goto enomem;

	qb_local = frame->local;

	qb_local->inode = inode_ref (fd->inode);
	qb_local->fd = fd_ref (fd);

	qb_local->stub = fop_fsync_stub (frame, NULL, fd, dsync, xdata);

	if (!qb_local->stub)
		goto enomem;

	qb_coroutine (frame, qb_co_fsync);

	return 0;
enomem:
	QB_STACK_UNWIND (fsync, frame, -1, ENOMEM, 0, 0, 0);
	return 0;
}


int
qb_flush (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
	qb_local_t *qb_local = NULL;
	qb_inode_t *qb_inode = NULL;

	qb_inode = qb_inode_ctx_get (this, fd->inode);
	if (!qb_inode) {
		STACK_WIND (frame, default_flush_cbk, FIRST_CHILD(this),
			    FIRST_CHILD(this)->fops->flush, fd, xdata);
		return 0;
	}

	if (qb_local_init (frame) != 0)
		goto enomem;

	qb_local = frame->local;

	qb_local->inode = inode_ref (fd->inode);
	qb_local->fd = fd_ref (fd);

	qb_local->stub = fop_flush_stub (frame, NULL, fd, xdata);

	if (!qb_local->stub)
		goto enomem;

	qb_coroutine (frame, qb_co_fsync);

	return 0;
enomem:
	QB_STACK_UNWIND (flush, frame, -1, ENOMEM, 0);
	return 0;
}

static int32_t
qb_readdirp_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
		int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
		dict_t *xdata)
{
	qb_conf_t *conf = this->private;
	gf_dirent_t *entry;
	char *format;

	list_for_each_entry(entry, &entries->list, list) {
		if (!entry->inode || !entry->dict)
			continue;

		format = NULL;
		if (dict_get_str(entry->dict, conf->qb_xattr_key, &format))
			continue;

		if (!format) {
			qb_inode_cleanup(this, entry->inode, 1);
			continue;
		}

		if (qb_format_extract(this, format, entry->inode))
			continue;

		qb_iatt_fixup(this, entry->inode, &entry->d_stat);
	}

	STACK_UNWIND_STRICT(readdirp, frame, op_ret, op_errno, entries, xdata);
	return 0;
}

static int32_t
qb_readdirp(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
	    off_t off, dict_t *xdata)
{
	qb_conf_t *conf = this->private;

	xdata = xdata ? dict_ref(xdata) : dict_new();
	if (!xdata)
		goto enomem;

	if (dict_set_int32 (xdata, conf->qb_xattr_key, 0))
		goto enomem;

	STACK_WIND(frame, qb_readdirp_cbk, FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->readdirp, fd, size, off, xdata);

	dict_unref(xdata);
	return 0;

enomem:
	QB_STACK_UNWIND(readdirp, frame, -1, ENOMEM, NULL, NULL);
	if (xdata)
		dict_unref(xdata);
	return 0;
}

int
qb_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
	     dict_t *xdata)
{
	qb_local_t *qb_local = NULL;
	qb_inode_t *qb_inode = NULL;

	qb_inode = qb_inode_ctx_get (this, loc->inode);
	if (!qb_inode) {
		STACK_WIND (frame, default_truncate_cbk, FIRST_CHILD(this),
			    FIRST_CHILD(this)->fops->truncate, loc, offset,
			    xdata);
		return 0;
	}

	if (qb_local_init (frame) != 0)
		goto enomem;

	qb_local = frame->local;

	qb_local->inode = inode_ref (loc->inode);
	qb_local->fd = fd_anonymous (loc->inode);

	qb_local->stub = fop_truncate_stub (frame, NULL, loc, offset, xdata);

	if (!qb_local->stub)
		goto enomem;

	qb_coroutine (frame, qb_co_truncate);

	return 0;
enomem:
	QB_STACK_UNWIND (truncate, frame, -1, ENOMEM, 0, 0, 0);
	return 0;
}


int
qb_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
	     dict_t *xdata)
{
	qb_local_t *qb_local = NULL;
	qb_inode_t *qb_inode = NULL;

	qb_inode = qb_inode_ctx_get (this, fd->inode);
	if (!qb_inode) {
		STACK_WIND (frame, default_ftruncate_cbk, FIRST_CHILD(this),
			    FIRST_CHILD(this)->fops->ftruncate, fd, offset,
			    xdata);
		return 0;
	}

	if (qb_local_init (frame) != 0)
		goto enomem;

	qb_local = frame->local;

	qb_local->inode = inode_ref (fd->inode);
	qb_local->fd = fd_ref (fd);

	qb_local->stub = fop_ftruncate_stub (frame, NULL, fd, offset, xdata);

	if (!qb_local->stub)
		goto enomem;

	qb_coroutine (frame, qb_co_truncate);

	return 0;
enomem:
	QB_STACK_UNWIND (ftruncate, frame, -1, ENOMEM, 0, 0, 0);
	return 0;
}


int
qb_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	     int op_ret, int op_errno, struct iatt *iatt, dict_t *xdata)
{
	inode_t *inode = NULL;

	inode = frame->local;
	frame->local = NULL;

	if (inode) {
		qb_iatt_fixup (this, inode, iatt);
		inode_unref (inode);
	}

	QB_STACK_UNWIND (stat, frame, op_ret, op_errno, iatt, xdata);

	return 0;
}

int
qb_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
	if (qb_inode_ctx_get (this, loc->inode))
		frame->local = inode_ref (loc->inode);

	STACK_WIND (frame, qb_stat_cbk, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->stat, loc, xdata);
	return 0;
}


int
qb_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	      int op_ret, int op_errno, struct iatt *iatt, dict_t *xdata)
{
	inode_t *inode = NULL;

	inode = frame->local;
	frame->local = NULL;

	if (inode) {
		qb_iatt_fixup (this, inode, iatt);
		inode_unref (inode);
	}

	QB_STACK_UNWIND (fstat, frame, op_ret, op_errno, iatt, xdata);

	return 0;
}


int
qb_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
	if (qb_inode_ctx_get (this, fd->inode))
		frame->local = inode_ref (fd->inode);

	STACK_WIND (frame, qb_fstat_cbk, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fstat, fd, xdata);
	return 0;
}


int
qb_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		int op_ret, int op_errno, struct iatt *pre, struct iatt *post,
		dict_t *xdata)
{
	inode_t *inode = NULL;

	inode = frame->local;
	frame->local = NULL;

	if (inode) {
		qb_iatt_fixup (this, inode, pre);
		qb_iatt_fixup (this, inode, post);
		inode_unref (inode);
	}

	QB_STACK_UNWIND (setattr, frame, op_ret, op_errno, pre, post, xdata);

	return 0;
}


int
qb_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc, struct iatt *buf,
	    int valid, dict_t *xdata)
{
	if (qb_inode_ctx_get (this, loc->inode))
		frame->local = inode_ref (loc->inode);

	STACK_WIND (frame, qb_setattr_cbk, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->setattr, loc, buf, valid, xdata);
	return 0;
}


int
qb_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		 int op_ret, int op_errno, struct iatt *pre, struct iatt *post,
		 dict_t *xdata)
{
	inode_t *inode = NULL;

	inode = frame->local;
	frame->local = NULL;

	if (inode) {
		qb_iatt_fixup (this, inode, pre);
		qb_iatt_fixup (this, inode, post);
		inode_unref (inode);
	}

	QB_STACK_UNWIND (fsetattr, frame, op_ret, op_errno, pre, post, xdata);

	return 0;
}


int
qb_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iatt *buf,
	     int valid, dict_t *xdata)
{
	if (qb_inode_ctx_get (this, fd->inode))
		frame->local = inode_ref (fd->inode);

	STACK_WIND (frame, qb_setattr_cbk, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fsetattr, fd, buf, valid, xdata);
	return 0;
}


int
qb_forget (xlator_t *this, inode_t *inode)
{
	return qb_inode_cleanup (this, inode, 0);
}


int
qb_release (xlator_t *this, fd_t *fd)
{
	call_frame_t *frame = NULL;

	frame = create_frame (this, this->ctx->pool);
	if (!frame) {
		gf_log (this->name, GF_LOG_ERROR,
			"Could not allocate frame. "
			"Leaking QEMU BlockDriverState");
		return -1;
	}

	if (qb_local_init (frame) != 0) {
		gf_log (this->name, GF_LOG_ERROR,
			"Could not allocate local. "
			"Leaking QEMU BlockDriverState");
		STACK_DESTROY (frame->root);
		return -1;
	}

	if (qb_coroutine (frame, qb_co_close) != 0) {
		gf_log (this->name, GF_LOG_ERROR,
			"Could not allocate coroutine. "
			"Leaking QEMU BlockDriverState");
		qb_local_free (this, frame->local);
		frame->local = NULL;
		STACK_DESTROY (frame->root);
	}

	return 0;
}

int
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        ret = xlator_mem_acct_init (this, gf_qb_mt_end + 1);

        if (ret)
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init "
                        "failed");
        return ret;
}


int
reconfigure (xlator_t *this, dict_t *options)
{
	return 0;
}


int
init (xlator_t *this)
{
        qb_conf_t *conf    = NULL;
        int32_t    ret     = -1;
	static int bdrv_inited = 0;

        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "FATAL: qemu-block (%s) not configured with exactly "
                        "one child", this->name);
                goto out;
        }

        conf = GF_CALLOC (1, sizeof (*conf), gf_qb_mt_qb_conf_t);
        if (!conf)
                goto out;

        /* configure 'option window-size <size>' */
        GF_OPTION_INIT ("default-password", conf->default_password, str, out);

	/* qemu coroutines use "co_mutex" for synchronizing among themselves.
	   However "co_mutex" itself is not threadsafe if the coroutine framework
	   is multithreaded (which usually is not). However synctasks are
	   fundamentally multithreaded, so for now create a syncenv which has
	   scaling limits set to max 1 thread so that the qemu coroutines can
	   execute "safely".

	   Future work: provide an implementation of "co_mutex" which is
	   threadsafe and use the global multithreaded ctx->env syncenv.
	*/
	conf->env = syncenv_new (0, 1, 1);

        this->private = conf;

        ret = 0;

	snprintf (conf->qb_xattr_key, QB_XATTR_KEY_MAX, QB_XATTR_KEY_FMT,
		  this->name);

	cur_mon = (void *) 1;

	if (!bdrv_inited) {
		bdrv_init ();
		bdrv_inited = 1;
	}

out:
        if (ret)
                GF_FREE (conf);

        return ret;
}


void
fini (xlator_t *this)
{
        qb_conf_t *conf = NULL;

        conf = this->private;

        this->private = NULL;

	if (conf->root_inode)
		inode_unref(conf->root_inode);
        GF_FREE (conf);

	return;
}


struct xlator_fops fops = {
	.lookup      = qb_lookup,
	.fsetxattr   = qb_fsetxattr,
	.setxattr    = qb_setxattr,
	.open        = qb_open,
        .writev      = qb_writev,
        .readv       = qb_readv,
        .fsync       = qb_fsync,
        .truncate    = qb_truncate,
        .ftruncate   = qb_ftruncate,
        .stat        = qb_stat,
        .fstat       = qb_fstat,
	.setattr     = qb_setattr,
	.fsetattr    = qb_fsetattr,
        .flush       = qb_flush,
/*
	.getxattr    = qb_getxattr,
	.fgetxattr   = qb_fgetxattr
*/
	.readdirp    = qb_readdirp,
};


struct xlator_cbks cbks = {
        .forget   = qb_forget,
	.release  = qb_release,
};


struct xlator_dumpops dumpops = {
};


struct volume_options options[] = {
        { .key  = {"default-password"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = "",
          .description = "Default password for the AES encrypted block images."
        },
        { .key = {NULL} },
};
