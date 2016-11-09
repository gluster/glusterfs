/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "open-behind-mem-types.h"
#include "xlator.h"
#include "statedump.h"
#include "call-stub.h"
#include "defaults.h"
#include "open-behind-messages.h"

typedef struct ob_conf {
	gf_boolean_t  use_anonymous_fd; /* use anonymous FDs wherever safe
					   e.g - fstat() readv()

					   whereas for fops like writev(), lk(),
					   the fd is important for side effects
					   like mandatory locks
					*/
	gf_boolean_t  lazy_open; /* delay backend open as much as possible */
        gf_boolean_t  read_after_open; /* instead of sending readvs on
                                               anonymous fds, open the file
                                               first and then send readv i.e
                                               similar to what writev does
                                            */
} ob_conf_t;


typedef struct ob_fd {
	call_frame_t     *open_frame;
	loc_t             loc;
	dict_t           *xdata;
	int               flags;
	int               op_errno;
	struct list_head  list;
} ob_fd_t;


ob_fd_t *
__ob_fd_ctx_get (xlator_t *this, fd_t *fd)
{
	uint64_t    value = 0;
	int         ret = -1;
	ob_fd_t    *ob_fd = NULL;

	ret = __fd_ctx_get (fd, this, &value);
	if (ret)
		return NULL;

	ob_fd = (void *) ((long) value);

	return ob_fd;
}


ob_fd_t *
ob_fd_ctx_get (xlator_t *this, fd_t *fd)
{
	ob_fd_t  *ob_fd = NULL;

	LOCK (&fd->lock);
	{
		ob_fd = __ob_fd_ctx_get (this, fd);
	}
	UNLOCK (&fd->lock);

	return ob_fd;
}


int
__ob_fd_ctx_set (xlator_t *this, fd_t *fd, ob_fd_t *ob_fd)
{
	uint64_t    value = 0;
	int         ret = -1;

	value = (long) ((void *) ob_fd);

	ret = __fd_ctx_set (fd, this, value);

	return ret;
}


int
ob_fd_ctx_set (xlator_t *this, fd_t *fd, ob_fd_t *ob_fd)
{
	int ret = -1;

	LOCK (&fd->lock);
	{
		ret = __ob_fd_ctx_set (this, fd, ob_fd);
	}
	UNLOCK (&fd->lock);

	return ret;
}


ob_fd_t *
ob_fd_new (void)
{
	ob_fd_t  *ob_fd = NULL;

	ob_fd = GF_CALLOC (1, sizeof (*ob_fd), gf_ob_mt_fd_t);

	INIT_LIST_HEAD (&ob_fd->list);

	return ob_fd;
}


void
ob_fd_free (ob_fd_t *ob_fd)
{
	loc_wipe (&ob_fd->loc);

	if (ob_fd->xdata)
		dict_unref (ob_fd->xdata);

	if (ob_fd->open_frame)
		STACK_DESTROY (ob_fd->open_frame->root);

	GF_FREE (ob_fd);
}


int
ob_wake_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	     int op_ret, int op_errno, fd_t *fd_ret, dict_t *xdata)
{
	fd_t              *fd = NULL;
	struct list_head   list;
	ob_fd_t           *ob_fd = NULL;
	call_stub_t       *stub = NULL, *tmp = NULL;

	fd = frame->local;
	frame->local = NULL;

	INIT_LIST_HEAD (&list);

	LOCK (&fd->lock);
	{
		ob_fd = __ob_fd_ctx_get (this, fd);

		list_splice_init (&ob_fd->list, &list);

		if (op_ret < 0) {
			/* mark fd BAD for ever */
			ob_fd->op_errno = op_errno;
                        ob_fd = NULL; /*shouldn't be freed*/
		} else {
			__fd_ctx_del (fd, this, NULL);
		}
	}
	UNLOCK (&fd->lock);

        if (ob_fd)
                ob_fd_free (ob_fd);

	list_for_each_entry_safe (stub, tmp, &list, list) {
		list_del_init (&stub->list);

		if (op_ret < 0)
			call_unwind_error (stub, -1, op_errno);
		else
			call_resume (stub);
	}

	fd_unref (fd);

	STACK_DESTROY (frame->root);

	return 0;
}


int
ob_fd_wake (xlator_t *this, fd_t *fd)
{
	call_frame_t *frame = NULL;
	ob_fd_t      *ob_fd = NULL;

	LOCK (&fd->lock);
	{
		ob_fd = __ob_fd_ctx_get (this, fd);
		if (!ob_fd)
			goto unlock;

		frame = ob_fd->open_frame;
		ob_fd->open_frame = NULL;
	}
unlock:
	UNLOCK (&fd->lock);

	if (frame) {
		frame->local = fd_ref (fd);

		STACK_WIND (frame, ob_wake_cbk, FIRST_CHILD (this),
			    FIRST_CHILD (this)->fops->open,
			    &ob_fd->loc, ob_fd->flags, fd, ob_fd->xdata);
	}

	return 0;
}


int
open_and_resume (xlator_t *this, fd_t *fd, call_stub_t *stub)
{
	ob_fd_t  *ob_fd = NULL;
	int       op_errno = 0;

	if (!fd)
		goto nofd;

	LOCK (&fd->lock);
	{
		ob_fd = __ob_fd_ctx_get (this, fd);
		if (!ob_fd)
			goto unlock;

		if (ob_fd->op_errno) {
			op_errno = ob_fd->op_errno;
			goto unlock;
		}

		list_add_tail (&stub->list, &ob_fd->list);
	}
unlock:
	UNLOCK (&fd->lock);

nofd:
	if (op_errno)
		call_unwind_error (stub, -1, op_errno);
	else if (ob_fd)
		ob_fd_wake (this, fd);
	else
		call_resume (stub);

	return 0;
}


int
ob_open_behind (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
		fd_t *fd, dict_t *xdata)
{
	ob_fd_t    *ob_fd = NULL;
	int         ret = -1;
	ob_conf_t  *conf = NULL;


	conf = this->private;

	if (flags & O_TRUNC) {
		STACK_WIND (frame, default_open_cbk,
			    FIRST_CHILD (this), FIRST_CHILD (this)->fops->open,
			    loc, flags, fd, xdata);
		return 0;
	}

	ob_fd = ob_fd_new ();
	if (!ob_fd)
		goto enomem;

	ob_fd->open_frame = copy_frame (frame);
	if (!ob_fd->open_frame)
		goto enomem;
	ret = loc_copy (&ob_fd->loc, loc);
	if (ret)
		goto enomem;

	ob_fd->flags = flags;
	if (xdata)
		ob_fd->xdata = dict_ref (xdata);

	ret = ob_fd_ctx_set (this, fd, ob_fd);
	if (ret)
		goto enomem;

	fd_ref (fd);

	STACK_UNWIND_STRICT (open, frame, 0, 0, fd, xdata);

	if (!conf->lazy_open)
		ob_fd_wake (this, fd);

	fd_unref (fd);

	return 0;
enomem:
	if (ob_fd) {
		if (ob_fd->open_frame)
			STACK_DESTROY (ob_fd->open_frame->root);
		loc_wipe (&ob_fd->loc);
		if (ob_fd->xdata)
			dict_unref (ob_fd->xdata);
		GF_FREE (ob_fd);
	}

	return -1;
}


int
ob_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
	 fd_t *fd, dict_t *xdata)
{
	fd_t         *old_fd = NULL;
	int           ret = -1;
	int           op_errno = 0;
	call_stub_t  *stub = NULL;

	old_fd = fd_lookup (fd->inode, 0);
	if (old_fd) {
		/* open-behind only when this is the first FD */
		stub = fop_open_stub (frame, default_open_resume,
				      loc, flags, fd, xdata);
		if (!stub) {
			op_errno = ENOMEM;
			fd_unref (old_fd);
			goto err;
		}

		open_and_resume (this, old_fd, stub);

		fd_unref (old_fd);

		return 0;
	}

	ret = ob_open_behind (frame, this, loc, flags, fd, xdata);
	if (ret) {
		op_errno = ENOMEM;
		goto err;
	}

	return 0;
err:
	gf_msg (this->name, GF_LOG_ERROR, op_errno, OPEN_BEHIND_MSG_NO_MEMORY,
                "%s", loc->path);

	STACK_UNWIND_STRICT (open, frame, -1, op_errno, 0, 0);

	return 0;
}


fd_t *
ob_get_wind_fd (xlator_t *this, fd_t *fd, uint32_t *flag)
{
        fd_t       *wind_fd = NULL;
	ob_fd_t    *ob_fd   = NULL;
	ob_conf_t  *conf    = NULL;

	conf = this->private;

	ob_fd = ob_fd_ctx_get (this, fd);

	if (ob_fd && conf->use_anonymous_fd) {
                wind_fd = fd_anonymous (fd->inode);
                if ((ob_fd->flags & O_DIRECT) && (flag))
                        *flag = *flag | O_DIRECT;
        } else {
                wind_fd = fd_ref (fd);
        }

	return wind_fd;
}


int
ob_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
	  off_t offset, uint32_t flags, dict_t *xdata)
{
	call_stub_t  *stub = NULL;
	fd_t         *wind_fd = NULL;
        ob_conf_t    *conf = NULL;

        conf = this->private;

        if (!conf->read_after_open)
                wind_fd = ob_get_wind_fd (this, fd, &flags);
        else
                wind_fd = fd_ref (fd);

	stub = fop_readv_stub (frame, default_readv_resume, wind_fd,
			       size, offset, flags, xdata);
	fd_unref (wind_fd);

	if (!stub)
		goto err;

	open_and_resume (this, wind_fd, stub);

	return 0;
err:
	STACK_UNWIND_STRICT (readv, frame, -1, ENOMEM, 0, 0, 0, 0, 0);

	return 0;
}


int
ob_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *iov,
	   int count, off_t offset, uint32_t flags, struct iobref *iobref,
	   dict_t *xdata)
{
	call_stub_t  *stub = NULL;

	stub = fop_writev_stub (frame, default_writev_resume, fd, iov, count,
				offset, flags, iobref, xdata);
	if (!stub)
		goto err;

	open_and_resume (this, fd, stub);

	return 0;
err:
	STACK_UNWIND_STRICT (writev, frame, -1, ENOMEM, 0, 0, 0);

	return 0;
}


int
ob_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
	call_stub_t  *stub = NULL;
	fd_t         *wind_fd = NULL;

	wind_fd = ob_get_wind_fd (this, fd, NULL);

	stub = fop_fstat_stub (frame, default_fstat_resume, wind_fd, xdata);

	fd_unref (wind_fd);

	if (!stub)
		goto err;

	open_and_resume (this, wind_fd, stub);

	return 0;
err:
	STACK_UNWIND_STRICT (fstat, frame, -1, ENOMEM, 0, 0);

	return 0;
}


int
ob_flush (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
	call_stub_t   *stub = NULL;
	ob_fd_t       *ob_fd = NULL;
	gf_boolean_t   unwind = _gf_false;

	LOCK (&fd->lock);
	{
		ob_fd = __ob_fd_ctx_get (this, fd);
		if (ob_fd && ob_fd->open_frame)
			/* if open() was never wound to backend,
			   no need to wind flush() either.
			*/
			unwind = _gf_true;
	}
	UNLOCK (&fd->lock);

	if (unwind)
		goto unwind;

	stub = fop_flush_stub (frame, default_flush_resume, fd, xdata);
	if (!stub)
		goto err;

	open_and_resume (this, fd, stub);

	return 0;
err:
	STACK_UNWIND_STRICT (flush, frame, -1, ENOMEM, 0);

	return 0;

unwind:
	STACK_UNWIND_STRICT (flush, frame, 0, 0, 0);

	return 0;
}


int
ob_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int flag,
	  dict_t *xdata)
{
	call_stub_t  *stub = NULL;

	stub = fop_fsync_stub (frame, default_fsync_resume, fd, flag, xdata);
	if (!stub)
		goto err;

	open_and_resume (this, fd, stub);

	return 0;
err:
	STACK_UNWIND_STRICT (fsync, frame, -1, ENOMEM, 0, 0, 0);

	return 0;
}


int
ob_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int cmd,
       struct gf_flock *flock, dict_t *xdata)
{
	call_stub_t  *stub = NULL;

	stub = fop_lk_stub (frame, default_lk_resume, fd, cmd, flock, xdata);
	if (!stub)
		goto err;

	open_and_resume (this, fd, stub);

	return 0;
err:
	STACK_UNWIND_STRICT (lk, frame, -1, ENOMEM, 0, 0);

	return 0;
}

int
ob_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
	      dict_t *xdata)
{
	call_stub_t  *stub = NULL;

	stub = fop_ftruncate_stub (frame, default_ftruncate_resume, fd, offset,
				   xdata);
	if (!stub)
		goto err;

	open_and_resume (this, fd, stub);

	return 0;
err:
	STACK_UNWIND_STRICT (ftruncate, frame, -1, ENOMEM, 0, 0, 0);

	return 0;
}


int
ob_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xattr,
	      int flags, dict_t *xdata)
{
	call_stub_t  *stub = NULL;

	stub = fop_fsetxattr_stub (frame, default_fsetxattr_resume, fd, xattr,
				   flags, xdata);
	if (!stub)
		goto err;

	open_and_resume (this, fd, stub);

	return 0;
err:
	STACK_UNWIND_STRICT (fsetxattr, frame, -1, ENOMEM, 0);

	return 0;
}


int
ob_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, const char *name,
	      dict_t *xdata)
{
	call_stub_t  *stub = NULL;

	stub = fop_fgetxattr_stub (frame, default_fgetxattr_resume, fd, name,
				   xdata);
	if (!stub)
		goto err;

	open_and_resume (this, fd, stub);

	return 0;
err:
	STACK_UNWIND_STRICT (fgetxattr, frame, -1, ENOMEM, 0, 0);

	return 0;
}


int
ob_fremovexattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
		 const char *name, dict_t *xdata)
{
	call_stub_t  *stub = NULL;

	stub = fop_fremovexattr_stub (frame, default_fremovexattr_resume, fd,
				      name, xdata);
	if (!stub)
		goto err;

	open_and_resume (this, fd, stub);

	return 0;
err:
	STACK_UNWIND_STRICT (fremovexattr, frame, -1, ENOMEM, 0);

	return 0;
}


int
ob_finodelk (call_frame_t *frame, xlator_t *this, const char *volume, fd_t *fd,
	     int cmd, struct gf_flock *flock, dict_t *xdata)
{
	call_stub_t  *stub = NULL;

	stub = fop_finodelk_stub (frame, default_finodelk_resume, volume, fd,
				  cmd, flock, xdata);
	if (!stub)
		goto err;

	open_and_resume (this, fd, stub);

	return 0;
err:
	STACK_UNWIND_STRICT (finodelk, frame, -1, ENOMEM, 0);

	return 0;
}


int
ob_fentrylk (call_frame_t *frame, xlator_t *this, const char *volume, fd_t *fd,
	     const char *basename, entrylk_cmd cmd, entrylk_type type,
	     dict_t *xdata)
{
	call_stub_t  *stub = NULL;

	stub = fop_fentrylk_stub (frame, default_fentrylk_resume, volume, fd,
				  basename, cmd, type, xdata);
	if (!stub)
		goto err;

	open_and_resume (this, fd, stub);

	return 0;
err:
	STACK_UNWIND_STRICT (fentrylk, frame, -1, ENOMEM, 0);

	return 0;
}


int
ob_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
	     gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
	call_stub_t  *stub = NULL;

	stub = fop_fxattrop_stub (frame, default_fxattrop_resume, fd, optype,
				  xattr, xdata);
	if (!stub)
		goto err;

	open_and_resume (this, fd, stub);

	return 0;
err:
	STACK_UNWIND_STRICT (fxattrop, frame, -1, ENOMEM, 0, 0);

	return 0;
}


int
ob_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
	     struct iatt *iatt, int valid, dict_t *xdata)
{
	call_stub_t  *stub = NULL;

	stub = fop_fsetattr_stub (frame, default_fsetattr_resume, fd,
				  iatt, valid, xdata);
	if (!stub)
		goto err;

	open_and_resume (this, fd, stub);

	return 0;
err:
	STACK_UNWIND_STRICT (fsetattr, frame, -1, ENOMEM, 0, 0, 0);

	return 0;
}

int
ob_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t mode,
	     off_t offset, size_t len, dict_t *xdata)
{
	call_stub_t *stub;

	stub = fop_fallocate_stub(frame, default_fallocate_resume, fd, mode,
				  offset, len, xdata);
	if (!stub)
		goto err;

	open_and_resume(this, fd, stub);

	return 0;
err:
	STACK_UNWIND_STRICT(fallocate, frame, -1, ENOMEM, NULL, NULL, NULL);
	return 0;
}

int
ob_discard(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
	   size_t len, dict_t *xdata)
{
	call_stub_t *stub;

	stub = fop_discard_stub(frame, default_discard_resume, fd, offset, len,
				xdata);
	if (!stub)
		goto err;

	open_and_resume(this, fd, stub);

	return 0;
err:
	STACK_UNWIND_STRICT(discard, frame, -1, ENOMEM, NULL, NULL, NULL);
	return 0;
}

int
ob_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
           off_t len, dict_t *xdata)
{
        call_stub_t *stub;

        stub = fop_zerofill_stub(frame, default_zerofill_resume, fd,
                                 offset, len, xdata);
        if (!stub)
                goto err;

        open_and_resume(this, fd, stub);

        return 0;
err:
        STACK_UNWIND_STRICT(zerofill, frame, -1, ENOMEM, NULL, NULL, NULL);
        return 0;
}


int
ob_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflags,
	   dict_t *xdata)
{
	fd_t         *fd = NULL;
	call_stub_t  *stub = NULL;

	stub = fop_unlink_stub (frame, default_unlink_resume, loc,
				xflags, xdata);
	if (!stub)
		goto err;

	fd = fd_lookup (loc->inode, 0);

	open_and_resume (this, fd, stub);
        if (fd)
                fd_unref (fd);

	return 0;
err:
	STACK_UNWIND_STRICT (unlink, frame, -1, ENOMEM, 0, 0, 0);

	return 0;
}


int
ob_rename (call_frame_t *frame, xlator_t *this, loc_t *src, loc_t *dst,
	   dict_t *xdata)
{
	fd_t         *fd = NULL;
	call_stub_t  *stub = NULL;

	stub = fop_rename_stub (frame, default_rename_resume, src, dst, xdata);
	if (!stub)
		goto err;

	if (dst->inode)
		fd = fd_lookup (dst->inode, 0);

	open_and_resume (this, fd, stub);
        if (fd)
                fd_unref (fd);

	return 0;
err:
	STACK_UNWIND_STRICT (rename, frame, -1, ENOMEM, 0, 0, 0, 0, 0, 0);

	return 0;
}


int
ob_release (xlator_t *this, fd_t *fd)
{
	ob_fd_t *ob_fd = NULL;

	ob_fd = ob_fd_ctx_get (this, fd);

	ob_fd_free (ob_fd);

	return 0;
}


int
ob_priv_dump (xlator_t *this)
{
        ob_conf_t        *conf       = NULL;
        char              key_prefix[GF_DUMP_MAX_BUF_LEN];

        conf = this->private;

        if (!conf)
                return -1;

        gf_proc_dump_build_key (key_prefix, "xlator.performance.open-behind",
                                "priv");

        gf_proc_dump_add_section (key_prefix);

        gf_proc_dump_write ("use_anonymous_fd", "%d", conf->use_anonymous_fd);

        gf_proc_dump_write ("lazy_open", "%d", conf->lazy_open);

        return 0;
}


int
ob_fdctx_dump (xlator_t *this, fd_t *fd)
{
	ob_fd_t   *ob_fd = NULL;
        char       key_prefix[GF_DUMP_MAX_BUF_LEN] = {0, };
	int        ret = 0;

	ret = TRY_LOCK (&fd->lock);
	if (ret)
		return 0;

	ob_fd = __ob_fd_ctx_get (this, fd);
	if (!ob_fd) {
		UNLOCK (&fd->lock);
		return 0;
	}

        gf_proc_dump_build_key (key_prefix, "xlator.performance.open-behind",
                                "file");
        gf_proc_dump_add_section (key_prefix);

        gf_proc_dump_write ("fd", "%p", fd);

        gf_proc_dump_write ("open_frame", "%p", ob_fd->open_frame);

        if (ob_fd->open_frame)
                gf_proc_dump_write ("open_frame.root.unique", "%p",
                                    ob_fd->open_frame->root->unique);

	gf_proc_dump_write ("loc.path", "%s", ob_fd->loc.path);

	gf_proc_dump_write ("loc.ino", "%s", uuid_utoa (ob_fd->loc.gfid));

        gf_proc_dump_write ("flags", "%d", ob_fd->flags);

	UNLOCK (&fd->lock);

	return 0;
}


int
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        ret = xlator_mem_acct_init (this, gf_ob_mt_end + 1);

        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        OPEN_BEHIND_MSG_NO_MEMORY,
                        "Memory accounting failed");

        return ret;
}


int
reconfigure (xlator_t *this, dict_t *options)
{
        ob_conf_t  *conf = NULL;
	int         ret = -1;

        conf = this->private;

        GF_OPTION_RECONF ("use-anonymous-fd", conf->use_anonymous_fd, options,
			  bool, out);

        GF_OPTION_RECONF ("lazy-open", conf->lazy_open, options, bool, out);
        GF_OPTION_RECONF ("read-after-open", conf->read_after_open, options,
                          bool, out);

        ret = 0;
out:
        return ret;
}


int
init (xlator_t *this)
{
        ob_conf_t    *conf = NULL;

        if (!this->children || this->children->next) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        OPEN_BEHIND_MSG_XLATOR_CHILD_MISCONFIGURED,
                        "FATAL: volume (%s) not configured with exactly one "
                        "child", this->name);
                return -1;
        }

        if (!this->parents)
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        OPEN_BEHIND_MSG_VOL_MISCONFIGURED,
                        "dangling volume. check volfile ");

        conf = GF_CALLOC (1, sizeof (*conf), gf_ob_mt_conf_t);
        if (!conf)
                goto err;

        GF_OPTION_INIT ("use-anonymous-fd", conf->use_anonymous_fd, bool, err);

        GF_OPTION_INIT ("lazy-open", conf->lazy_open, bool, err);
        GF_OPTION_INIT ("read-after-open", conf->read_after_open, bool, err);
        this->private = conf;

	return 0;
err:
	if (conf)
                GF_FREE (conf);

        return -1;
}


void
fini (xlator_t *this)
{
        ob_conf_t *conf = NULL;

        conf = this->private;

        GF_FREE (conf);

	return;
}


struct xlator_fops fops = {
        .open        = ob_open,
        .readv       = ob_readv,
        .writev      = ob_writev,
	.flush       = ob_flush,
	.fsync       = ob_fsync,
	.fstat       = ob_fstat,
	.ftruncate   = ob_ftruncate,
	.fsetxattr   = ob_fsetxattr,
	.fgetxattr   = ob_fgetxattr,
	.fremovexattr = ob_fremovexattr,
	.finodelk    = ob_finodelk,
	.fentrylk    = ob_fentrylk,
	.fxattrop    = ob_fxattrop,
	.fsetattr    = ob_fsetattr,
	.fallocate   = ob_fallocate,
	.discard     = ob_discard,
        .zerofill    = ob_zerofill,
	.unlink      = ob_unlink,
	.rename      = ob_rename,
	.lk          = ob_lk,
};

struct xlator_cbks cbks = {
        .release  = ob_release,
};

struct xlator_dumpops dumpops = {
	.priv    = ob_priv_dump,
	.fdctx   = ob_fdctx_dump,
};


struct volume_options options[] = {
        { .key  = {"use-anonymous-fd"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "yes",
          .description = "For read operations, use anonymous FD when "
          "original FD is open-behind and not yet opened in the backend.",
        },
        { .key  = {"lazy-open"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "yes",
          .description = "Perform open in the backend only when a necessary "
          "FOP arrives (e.g writev on the FD, unlink of the file). When option "
          "is disabled, perform backend open right after unwinding open().",
        },
        { .key  = {"read-after-open"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "no",
          .description = "read is sent only after actual open happens and real "
          "fd is obtained, instead of doing on anonymous fd (similar to write)",
        },
        { .key  = {NULL} }

};
