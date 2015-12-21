/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "quick-read.h"
#include "statedump.h"
#include "quick-read-messages.h"

qr_inode_t *qr_inode_ctx_get (xlator_t *this, inode_t *inode);
void __qr_inode_prune (qr_inode_table_t *table, qr_inode_t *qr_inode);


int
__qr_inode_ctx_set (xlator_t *this, inode_t *inode, qr_inode_t *qr_inode)
{
	uint64_t    value = 0;
	int         ret = -1;

	value = (long) qr_inode;

	ret = __inode_ctx_set (inode, this, &value);

	return ret;
}


qr_inode_t *
__qr_inode_ctx_get (xlator_t *this, inode_t *inode)
{
	qr_inode_t *qr_inode = NULL;
	uint64_t    value = 0;
	int         ret = -1;

	ret = __inode_ctx_get (inode, this, &value);
	if (ret)
		return NULL;

	qr_inode = (void *) ((long) value);

	return qr_inode;
}


qr_inode_t *
qr_inode_ctx_get (xlator_t *this, inode_t *inode)
{
	qr_inode_t *qr_inode = NULL;

	LOCK (&inode->lock);
	{
		qr_inode = __qr_inode_ctx_get (this, inode);
	}
	UNLOCK (&inode->lock);

	return qr_inode;
}


qr_inode_t *
qr_inode_new (xlator_t *this, inode_t *inode)
{
        qr_inode_t    *qr_inode = NULL;

        qr_inode = GF_CALLOC (1, sizeof (*qr_inode), gf_qr_mt_qr_inode_t);
        if (!qr_inode)
                return NULL;

        INIT_LIST_HEAD (&qr_inode->lru);

        qr_inode->priority = 0; /* initial priority */

        return qr_inode;
}


qr_inode_t *
qr_inode_ctx_get_or_new (xlator_t *this, inode_t *inode)
{
	qr_inode_t   *qr_inode = NULL;
	int           ret = -1;
	qr_private_t *priv = NULL;

	priv = this->private;

	LOCK (&inode->lock);
	{
		qr_inode = __qr_inode_ctx_get (this, inode);
		if (qr_inode)
			goto unlock;

		qr_inode = qr_inode_new (this, inode);
		if (!qr_inode)
			goto unlock;

		ret = __qr_inode_ctx_set (this, inode, qr_inode);
		if (ret) {
			__qr_inode_prune (&priv->table, qr_inode);
			GF_FREE (qr_inode);
                        qr_inode = NULL;
		}
	}
unlock:
	UNLOCK (&inode->lock);

	return qr_inode;
}


uint32_t
qr_get_priority (qr_conf_t *conf, const char *path)
{
        uint32_t            priority = 0;
        struct qr_priority *curr     = NULL;

        list_for_each_entry (curr, &conf->priority_list, list) {
                if (fnmatch (curr->pattern, path, FNM_NOESCAPE) == 0)
                        priority = curr->priority;
        }

        return priority;
}


void
__qr_inode_register (qr_inode_table_t *table, qr_inode_t *qr_inode)
{
	if (!qr_inode->data)
		return;

	if (list_empty (&qr_inode->lru))
		/* first time addition of this qr_inode into table */
		table->cache_used += qr_inode->size;
	else
		list_del_init (&qr_inode->lru);

	list_add_tail (&qr_inode->lru, &table->lru[qr_inode->priority]);
}


void
qr_inode_set_priority (xlator_t *this, inode_t *inode, const char *path)
{
	uint32_t          priority = 0;
	qr_inode_table_t *table = NULL;
	qr_inode_t       *qr_inode = NULL;
        qr_private_t     *priv = NULL;
	qr_conf_t        *conf = NULL;

	qr_inode = qr_inode_ctx_get (this, inode);
	if (!qr_inode)
		return;

	priv = this->private;
	table = &priv->table;
	conf = &priv->conf;

	if (path)
		priority = qr_get_priority (conf, path);
	else
		/* retain existing priority, just bump LRU */
		priority = qr_inode->priority;

	LOCK (&table->lock);
	{
		qr_inode->priority = priority;

		__qr_inode_register (table, qr_inode);
	}
	UNLOCK (&table->lock);
}


/* To be called with priv->table.lock held */
void
__qr_inode_prune (qr_inode_table_t *table, qr_inode_t *qr_inode)
{
	GF_FREE (qr_inode->data);
	qr_inode->data = NULL;

	if (!list_empty (&qr_inode->lru)) {
		table->cache_used -= qr_inode->size;
		qr_inode->size = 0;

		list_del_init (&qr_inode->lru);
	}

	memset (&qr_inode->buf, 0, sizeof (qr_inode->buf));
}


void
qr_inode_prune (xlator_t *this, inode_t *inode)
{
        qr_private_t     *priv          = NULL;
        qr_inode_table_t *table         = NULL;
	qr_inode_t       *qr_inode      = NULL;

	qr_inode = qr_inode_ctx_get (this, inode);
	if (!qr_inode)
		return;

	priv = this->private;
	table = &priv->table;

	LOCK (&table->lock);
	{
		__qr_inode_prune (table, qr_inode);
	}
	UNLOCK (&table->lock);
}


/* To be called with priv->table.lock held */
void
__qr_cache_prune (qr_inode_table_t *table, qr_conf_t *conf)
{
        qr_inode_t        *curr = NULL;
	qr_inode_t        *next = NULL;
        int                index = 0;
	size_t             size_pruned = 0;

        for (index = 0; index < conf->max_pri; index++) {
                list_for_each_entry_safe (curr, next, &table->lru[index], lru) {

                        size_pruned += curr->size;

                        __qr_inode_prune (table, curr);

                        if (table->cache_used < conf->cache_size)
				return;
                }
        }

        return;
}


void
qr_cache_prune (xlator_t *this)
{
        qr_private_t      *priv = NULL;
        qr_conf_t         *conf = NULL;
        qr_inode_table_t  *table = NULL;

        priv = this->private;
        table = &priv->table;
        conf = &priv->conf;

	LOCK (&table->lock);
	{
		if (table->cache_used > conf->cache_size)
			__qr_cache_prune (table, conf);
	}
	UNLOCK (&table->lock);
}


void *
qr_content_extract (dict_t *xdata)
{
	data_t  *data = NULL;
	void    *content = NULL;

	data = dict_get (xdata, GF_CONTENT_KEY);
	if (!data)
		return NULL;

	content = GF_CALLOC (1, data->len, gf_qr_mt_content_t);
	if (!content)
		return NULL;

	memcpy (content, data->data, data->len);

	return content;
}


void
qr_content_update (xlator_t *this, qr_inode_t *qr_inode, void *data,
		   struct iatt *buf)
{
        qr_private_t      *priv = NULL;
        qr_inode_table_t  *table = NULL;

        priv = this->private;
        table = &priv->table;

	LOCK (&table->lock);
	{
		__qr_inode_prune (table, qr_inode);

		qr_inode->data = data;
		qr_inode->size = buf->ia_size;

		qr_inode->ia_mtime = buf->ia_mtime;
		qr_inode->ia_mtime_nsec = buf->ia_mtime_nsec;

		qr_inode->buf = *buf;

		gettimeofday (&qr_inode->last_refresh, NULL);

		__qr_inode_register (table, qr_inode);
	}
	UNLOCK (&table->lock);

	qr_cache_prune (this);
}


gf_boolean_t
qr_size_fits (qr_conf_t *conf, struct iatt *buf)
{
	return (buf->ia_size <= conf->max_file_size);
}


gf_boolean_t
qr_mtime_equal (qr_inode_t *qr_inode, struct iatt *buf)
{
	return (qr_inode->ia_mtime == buf->ia_mtime &&
		qr_inode->ia_mtime_nsec == buf->ia_mtime_nsec);
}


void
__qr_content_refresh (xlator_t *this, qr_inode_t *qr_inode, struct iatt *buf)
{
        qr_private_t      *priv = NULL;
        qr_inode_table_t  *table = NULL;
	qr_conf_t         *conf = NULL;

        priv = this->private;
        table = &priv->table;
	conf = &priv->conf;

	if (qr_size_fits (conf, buf) && qr_mtime_equal (qr_inode, buf)) {
		qr_inode->buf = *buf;

		gettimeofday (&qr_inode->last_refresh, NULL);

		__qr_inode_register (table, qr_inode);
	} else {
		__qr_inode_prune (table, qr_inode);
	}

	return;
}


void
qr_content_refresh (xlator_t *this, qr_inode_t *qr_inode, struct iatt *buf)
{
        qr_private_t      *priv = NULL;
        qr_inode_table_t  *table = NULL;

        priv = this->private;
        table = &priv->table;

	LOCK (&table->lock);
	{
		__qr_content_refresh (this, qr_inode, buf);
	}
	UNLOCK (&table->lock);
}


gf_boolean_t
__qr_cache_is_fresh (xlator_t *this, qr_inode_t *qr_inode)
{
	qr_conf_t        *conf = NULL;
	qr_private_t     *priv = NULL;
	struct timeval    now;
	struct timeval    diff;

	priv = this->private;
	conf = &priv->conf;

	gettimeofday (&now, NULL);

	timersub (&now, &qr_inode->last_refresh, &diff);

	if (diff.tv_sec >= conf->cache_timeout)
		return _gf_false;

	return _gf_true;
}


int
qr_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode_ret,
               struct iatt *buf, dict_t *xdata, struct iatt *postparent)
{
        void             *content  = NULL;
        qr_inode_t       *qr_inode = NULL;
	inode_t          *inode    = NULL;

	inode = frame->local;
	frame->local = NULL;

        if (op_ret == -1) {
		qr_inode_prune (this, inode);
                goto out;
	}

        if (dict_get (xdata, GLUSTERFS_BAD_INODE)) {
                qr_inode_prune (this, inode);
                goto out;
        }

	if (dict_get (xdata, "sh-failed")) {
		qr_inode_prune (this, inode);
		goto out;
	}

	content = qr_content_extract (xdata);

	if (content) {
		/* new content came along, always replace old content */
		qr_inode = qr_inode_ctx_get_or_new (this, inode);
		if (!qr_inode) {
			/* no harm done */
			GF_FREE (content);
			goto out;
		}
		qr_content_update (this, qr_inode, content, buf);
	} else {
		/* purge old content if necessary */
		qr_inode = qr_inode_ctx_get (this, inode);
		if (!qr_inode)
			/* usual path for large files */
			goto out;

		qr_content_refresh (this, qr_inode, buf);
	}
out:
	if (inode)
		inode_unref (inode);

        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, inode_ret,
			     buf, xdata, postparent);
        return 0;
}


int
qr_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        qr_private_t     *priv           = NULL;
        qr_conf_t        *conf           = NULL;
        qr_inode_t       *qr_inode       = NULL;
	int               ret            = -1;
	dict_t           *new_xdata      = NULL;

        priv = this->private;
        conf = &priv->conf;

	qr_inode = qr_inode_ctx_get (this, loc->inode);
	if (qr_inode && qr_inode->data)
		/* cached. only validate in qr_lookup_cbk */
		goto wind;

	if (!xdata)
		xdata = new_xdata = dict_new ();

	if (!xdata)
		goto wind;

	ret = 0;
	if (conf->max_file_size)
		ret = dict_set (xdata, GF_CONTENT_KEY,
				data_from_uint64 (conf->max_file_size));
	if (ret)
		gf_msg (this->name, GF_LOG_WARNING, 0,
			QUICK_READ_MSG_DICT_SET_FAILED,
                        "cannot set key in request dict (%s)",
			loc->path);
wind:
	frame->local = inode_ref (loc->inode);

        STACK_WIND (frame, qr_lookup_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup, loc, xdata);

	if (new_xdata)
		dict_unref (new_xdata);

        return 0;
}


int
qr_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		 int op_ret, int op_errno, gf_dirent_t *entries, dict_t *xdata)
{
        gf_dirent_t *entry      = NULL;
	qr_inode_t  *qr_inode   = NULL;

	if (op_ret <= 0)
		goto unwind;

        list_for_each_entry (entry, &entries->list, list) {
                if (!entry->inode)
			continue;

		qr_inode = qr_inode_ctx_get (this, entry->inode);
		if (!qr_inode)
			/* no harm */
			continue;

		qr_content_refresh (this, qr_inode, &entry->d_stat);
        }

unwind:
	STACK_UNWIND_STRICT (readdirp, frame, op_ret, op_errno, entries, xdata);
	return 0;
}


int
qr_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd,
	     size_t size, off_t offset, dict_t *xdata)
{
	STACK_WIND (frame, qr_readdirp_cbk,
		    FIRST_CHILD (this), FIRST_CHILD (this)->fops->readdirp,
		    fd, size, offset, xdata);
	return 0;
}


int
qr_readv_cached (call_frame_t *frame, qr_inode_t *qr_inode, size_t size,
		 off_t offset, uint32_t flags, dict_t *xdata)
{
	xlator_t         *this = NULL;
	qr_private_t     *priv = NULL;
	qr_inode_table_t *table = NULL;
	int               op_ret = -1;
	struct iobuf     *iobuf = NULL;
	struct iobref    *iobref = NULL;
	struct iovec      iov = {0, };
	struct iatt       buf = {0, };

	this = frame->this;
	priv = this->private;
	table = &priv->table;

	LOCK (&table->lock);
	{
		if (!qr_inode->data)
			goto unlock;

		if (offset >= qr_inode->size)
			goto unlock;

		if (!__qr_cache_is_fresh (this, qr_inode))
			goto unlock;

		op_ret = min (size, (qr_inode->size - offset));

		iobuf = iobuf_get2 (this->ctx->iobuf_pool, op_ret);
		if (!iobuf) {
			op_ret = -1;
			goto unlock;
		}

		iobref = iobref_new ();
		if (!iobref) {
			op_ret = -1;
			goto unlock;
		}

		iobref_add (iobref, iobuf);

		memcpy (iobuf->ptr, qr_inode->data + offset, op_ret);

		buf = qr_inode->buf;

		/* bump LRU */
		__qr_inode_register (table, qr_inode);
	}
unlock:
	UNLOCK (&table->lock);

	if (op_ret >= 0) {
		iov.iov_base = iobuf->ptr;
		iov.iov_len = op_ret;

		STACK_UNWIND_STRICT (readv, frame, op_ret, 0, &iov, 1,
				     &buf, iobref, xdata);
	}
        if (iobuf)
                iobuf_unref (iobuf);

        if (iobref)
	        iobref_unref (iobref);

	return op_ret;
}


int
qr_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
          off_t offset, uint32_t flags, dict_t *xdata)
{
	qr_inode_t *qr_inode = NULL;

	qr_inode = qr_inode_ctx_get (this, fd->inode);
	if (!qr_inode)
		goto wind;

	if (qr_readv_cached (frame, qr_inode, size, offset, flags, xdata) < 0)
		goto wind;

	return 0;
wind:
	STACK_WIND (frame, default_readv_cbk,
		    FIRST_CHILD (this), FIRST_CHILD (this)->fops->readv,
		    fd, size, offset, flags, xdata);
	return 0;
}


int
qr_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *iov,
	   int count, off_t offset, uint32_t flags, struct iobref *iobref,
	   dict_t *xdata)
{
	qr_inode_prune (this, fd->inode);

	STACK_WIND (frame, default_writev_cbk,
		    FIRST_CHILD (this), FIRST_CHILD (this)->fops->writev,
		    fd, iov, count, offset, flags, iobref, xdata);
	return 0;
}


int
qr_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
	     dict_t *xdata)
{
	qr_inode_prune (this, loc->inode);

	STACK_WIND (frame, default_truncate_cbk,
		    FIRST_CHILD (this), FIRST_CHILD (this)->fops->truncate,
		    loc, offset, xdata);
	return 0;
}


int
qr_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
	      dict_t *xdata)
{
	qr_inode_prune (this, fd->inode);

	STACK_WIND (frame, default_ftruncate_cbk,
		    FIRST_CHILD (this), FIRST_CHILD (this)->fops->ftruncate,
		    fd, offset, xdata);
	return 0;
}


int
qr_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
	 fd_t *fd, dict_t *xdata)
{
	qr_inode_set_priority (this, fd->inode, loc->path);

	STACK_WIND (frame, default_open_cbk,
		    FIRST_CHILD (this), FIRST_CHILD (this)->fops->open,
		    loc, flags, fd, xdata);
	return 0;
}

int
qr_forget (xlator_t *this, inode_t *inode)
{
        qr_inode_t   *qr_inode = NULL;

	qr_inode = qr_inode_ctx_get (this, inode);

	if (!qr_inode)
		return 0;

	qr_inode_prune (this, inode);

	GF_FREE (qr_inode);

	return 0;
}


int32_t
qr_inodectx_dump (xlator_t *this, inode_t *inode)
{
        qr_inode_t *qr_inode = NULL;
        int32_t     ret      = -1;
        char        key_prefix[GF_DUMP_MAX_BUF_LEN] = {0, };
        char        buf[256]                        = {0, };

        qr_inode = qr_inode_ctx_get (this, inode);
        if (!qr_inode)
                goto out;

        gf_proc_dump_build_key (key_prefix, "xlator.performance.quick-read",
                                "inodectx");
        gf_proc_dump_add_section (key_prefix);

        gf_proc_dump_write ("entire-file-cached", "%s", qr_inode->data ? "yes" : "no");

        if (qr_inode->last_refresh.tv_sec) {
                gf_time_fmt (buf, sizeof buf, qr_inode->last_refresh.tv_sec,
                             gf_timefmt_FT);
                snprintf (buf + strlen (buf), sizeof buf - strlen (buf),
                          ".%"GF_PRI_SUSECONDS, qr_inode->last_refresh.tv_usec);

                gf_proc_dump_write ("last-cache-validation-time", "%s", buf);
        }

        ret = 0;
out:
        return ret;
}


int
qr_priv_dump (xlator_t *this)
{
        qr_conf_t        *conf       = NULL;
        qr_private_t     *priv       = NULL;
        qr_inode_table_t *table      = NULL;
        uint32_t          file_count = 0;
        uint32_t          i          = 0;
        qr_inode_t       *curr       = NULL;
        uint64_t          total_size = 0;
        char              key_prefix[GF_DUMP_MAX_BUF_LEN];

        if (!this) {
                return -1;
        }

        priv = this->private;
        conf = &priv->conf;

        if (!conf)
                return -1;

        table = &priv->table;

        gf_proc_dump_build_key (key_prefix, "xlator.performance.quick-read",
                                "priv");

        gf_proc_dump_add_section (key_prefix);

        gf_proc_dump_write ("max_file_size", "%d", conf->max_file_size);
        gf_proc_dump_write ("cache_timeout", "%d", conf->cache_timeout);

        if (!table) {
                goto out;
        } else {
                for (i = 0; i < conf->max_pri; i++) {
                        list_for_each_entry (curr, &table->lru[i], lru) {
                                file_count++;
                                total_size += curr->size;
                        }
                }
        }

        gf_proc_dump_write ("total_files_cached", "%d", file_count);
        gf_proc_dump_write ("total_cache_used", "%d", total_size);

out:
        return 0;
}


int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_qr_mt_end + 1);

        if (ret != 0) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        QUICK_READ_MSG_NO_MEMORY,
                        "Memory accounting init failed");
                return ret;
        }

        return ret;
}


static gf_boolean_t
check_cache_size_ok (xlator_t *this, int64_t cache_size)
{
        int                     ret = _gf_true;
        uint64_t                total_mem = 0;
        uint64_t                max_cache_size = 0;
        volume_option_t         *opt = NULL;

        GF_ASSERT (this);
        opt = xlator_volume_option_get (this, "cache-size");
        if (!opt) {
                ret = _gf_false;
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        QUICK_READ_MSG_INVALID_ARGUMENT,
                        "could not get cache-size option");
                goto out;
        }

        total_mem = get_mem_size ();
        if (-1 == total_mem)
                max_cache_size = opt->max;
        else
                max_cache_size = total_mem;

        gf_msg_debug (this->name, 0, "Max cache size is %"PRIu64,
                      max_cache_size);
        if (cache_size > max_cache_size) {
                ret = _gf_false;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        QUICK_READ_MSG_INVALID_ARGUMENT, "Cache size %"PRIu64
                        " is greater than the max size of %"PRIu64,
                        cache_size, max_cache_size);
                goto out;
        }
out:
        return ret;
}

int
reconfigure (xlator_t *this, dict_t *options)
{
        int32_t       ret            = -1;
        qr_private_t *priv           = NULL;
        qr_conf_t    *conf           = NULL;
        uint64_t       cache_size_new = 0;

        GF_VALIDATE_OR_GOTO ("quick-read", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);
        GF_VALIDATE_OR_GOTO (this->name, options, out);

        priv = this->private;

        conf = &priv->conf;
        if (!conf) {
                goto out;
        }

        GF_OPTION_RECONF ("cache-timeout", conf->cache_timeout, options, int32,
                          out);

        GF_OPTION_RECONF ("cache-size", cache_size_new, options, size_uint64, out);
        if (!check_cache_size_ok (this, cache_size_new)) {
                ret = -1;
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        QUICK_READ_MSG_INVALID_CONFIG,
                        "Not reconfiguring cache-size");
                goto out;
        }
        conf->cache_size = cache_size_new;

        ret = 0;
out:
        return ret;
}


int32_t
qr_get_priority_list (const char *opt_str, struct list_head *first)
{
        int32_t              max_pri      = 1;
        char                *tmp_str      = NULL;
        char                *tmp_str1     = NULL;
        char                *tmp_str2     = NULL;
        char                *dup_str      = NULL;
        char                *priority_str = NULL;
        char                *pattern      = NULL;
        char                *priority     = NULL;
        char                *string       = NULL;
        struct qr_priority  *curr         = NULL, *tmp = NULL;

        GF_VALIDATE_OR_GOTO ("quick-read", opt_str, out);
        GF_VALIDATE_OR_GOTO ("quick-read", first, out);

        string = gf_strdup (opt_str);
        if (string == NULL) {
                max_pri = -1;
                goto out;
        }

        /* Get the pattern for cache priority.
         * "option priority *.jpg:1,abc*:2" etc
         */
        /* TODO: inode_lru in table is statically hard-coded to 5,
         * should be changed to run-time configuration
         */
        priority_str = strtok_r (string, ",", &tmp_str);
        while (priority_str) {
                curr = GF_CALLOC (1, sizeof (*curr), gf_qr_mt_qr_priority_t);
                if (curr == NULL) {
                        max_pri = -1;
                        goto out;
                }

                list_add_tail (&curr->list, first);

                dup_str = gf_strdup (priority_str);
                if (dup_str == NULL) {
                        max_pri = -1;
                        goto out;
                }

                pattern = strtok_r (dup_str, ":", &tmp_str1);
                if (!pattern) {
                        max_pri = -1;
                        goto out;
                }

                priority = strtok_r (NULL, ":", &tmp_str1);
                if (!priority) {
                        max_pri = -1;
                        goto out;
                }

                gf_msg_trace ("quick-read", 0,
                              "quick-read priority : pattern %s : priority %s",
                              pattern, priority);

                curr->pattern = gf_strdup (pattern);
                if (curr->pattern == NULL) {
                        max_pri = -1;
                        goto out;
                }

                curr->priority = strtol (priority, &tmp_str2, 0);
                if (tmp_str2 && (*tmp_str2)) {
                        max_pri = -1;
                        goto out;
                } else {
                        max_pri = max (max_pri, curr->priority);
                }

                GF_FREE (dup_str);
                dup_str = NULL;

                priority_str = strtok_r (NULL, ",", &tmp_str);
        }
out:
        GF_FREE (string);

        GF_FREE (dup_str);

        if (max_pri == -1) {
                list_for_each_entry_safe (curr, tmp, first, list) {
                        list_del_init (&curr->list);
                        GF_FREE (curr->pattern);
                        GF_FREE (curr);
                }
        }

        return max_pri;
}


int32_t
init (xlator_t *this)
{
        int32_t       ret  = -1, i = 0;
        qr_private_t *priv = NULL;
        qr_conf_t    *conf = NULL;

        if (!this->children || this->children->next) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        QUICK_READ_MSG_XLATOR_CHILD_MISCONFIGURED,
                        "FATAL: volume (%s) not configured with exactly one "
                        "child", this->name);
                return -1;
        }

        if (!this->parents) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        QUICK_READ_MSG_VOL_MISCONFIGURED,
                        "dangling volume. check volfile ");
        }

        priv = GF_CALLOC (1, sizeof (*priv), gf_qr_mt_qr_private_t);
        if (priv == NULL) {
                ret = -1;
                goto out;
        }

        LOCK_INIT (&priv->table.lock);
        conf = &priv->conf;

        GF_OPTION_INIT ("max-file-size", conf->max_file_size, size_uint64, out);

        GF_OPTION_INIT ("cache-timeout", conf->cache_timeout, int32, out);

        GF_OPTION_INIT ("cache-size", conf->cache_size, size_uint64, out);
        if (!check_cache_size_ok (this, conf->cache_size)) {
                ret = -1;
                goto out;
        }

        INIT_LIST_HEAD (&conf->priority_list);
        conf->max_pri = 1;
        if (dict_get (this->options, "priority")) {
                char *option_list = data_to_str (dict_get (this->options,
                                                           "priority"));
                gf_msg_trace (this->name, 0,
                              "option path %s", option_list);
                /* parse the list of pattern:priority */
                conf->max_pri = qr_get_priority_list (option_list,
                                                      &conf->priority_list);

                if (conf->max_pri == -1) {
                        goto out;
                }
                conf->max_pri ++;
        }

        priv->table.lru = GF_CALLOC (conf->max_pri, sizeof (*priv->table.lru),
                                     gf_common_mt_list_head);
        if (priv->table.lru == NULL) {
                ret = -1;
                goto out;
        }

        for (i = 0; i < conf->max_pri; i++) {
                INIT_LIST_HEAD (&priv->table.lru[i]);
        }

        ret = 0;

        this->private = priv;
out:
        if ((ret == -1) && priv) {
                GF_FREE (priv);
        }

        return ret;
}


void
qr_inode_table_destroy (qr_private_t *priv)
{
        int        i    = 0;
        qr_conf_t *conf = NULL;

        conf = &priv->conf;

        for (i = 0; i < conf->max_pri; i++) {
                /* There is a known leak of inodes, hence until
                 * that is fixed, log the assert as warning.
                GF_ASSERT (list_empty (&priv->table.lru[i]));*/
                if (!list_empty (&priv->table.lru[i])) {
                        gf_msg ("quick-read", GF_LOG_INFO, 0,
                                QUICK_READ_MSG_LRU_NOT_EMPTY,
                                "quick read inode table lru not empty");
                }
        }

        LOCK_DESTROY (&priv->table.lock);

        return;
}


void
qr_conf_destroy (qr_conf_t *conf)
{
        struct qr_priority *curr = NULL, *tmp = NULL;

        list_for_each_entry_safe (curr, tmp, &conf->priority_list, list) {
                list_del (&curr->list);
                GF_FREE (curr->pattern);
                GF_FREE (curr);
        }

        return;
}


void
fini (xlator_t *this)
{
        qr_private_t *priv = NULL;

        if (this == NULL) {
                goto out;
        }

        priv = this->private;
        if (priv == NULL) {
                goto out;
        }

        qr_inode_table_destroy (priv);
        qr_conf_destroy (&priv->conf);

        this->private = NULL;

        GF_FREE (priv);
out:
        return;
}

struct xlator_fops fops = {
        .lookup      = qr_lookup,
	.readdirp    = qr_readdirp,
        .open        = qr_open,
        .readv       = qr_readv,
	.writev      = qr_writev,
	.truncate    = qr_truncate,
	.ftruncate   = qr_ftruncate
};

struct xlator_cbks cbks = {
        .forget  = qr_forget,
};

struct xlator_dumpops dumpops = {
        .priv      =  qr_priv_dump,
        .inodectx  =  qr_inodectx_dump,
};

struct volume_options options[] = {
        { .key  = {"priority"},
          .type = GF_OPTION_TYPE_ANY
        },
        { .key  = {"cache-size"},
          .type = GF_OPTION_TYPE_SIZET,
          .min  = 0,
          .max  = 32 * GF_UNIT_GB,
          .default_value = "128MB",
          .description = "Size of the read cache."
        },
        { .key  = {"cache-timeout"},
          .type = GF_OPTION_TYPE_INT,
          .min = 1,
          .max = 60,
          .default_value = "1",
        },
        { .key  = {"max-file-size"},
          .type = GF_OPTION_TYPE_SIZET,
          .min  = 0,
          .max  = 1 * GF_UNIT_KB * 1000,
          .default_value = "64KB",
        },
        { .key  = {NULL} }
};
