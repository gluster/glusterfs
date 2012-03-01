/*
  Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "md-cache-mem-types.h"
#include <assert.h>
#include <sys/time.h>


/* TODO:
   - cache symlink() link names and nuke symlink-cache
   - send proper postbuf in setattr_cbk even when op_ret = -1
*/


struct mdc_conf {
	int  timeout;
};


struct mdc_key {
	const char *name;
	int         load;
	int         check;
} mdc_keys[] = {
	{
		.name = "system.posix_acl_access",
		.load = 1,
		.check = 1,
	},
	{
		.name = "system.posix_acl_default",
		.load = 1,
		.check = 1,
	},
	{
		.name = "security.selinux",
		.load = 1,
		.check = 1,
	},
	{
		.name = "security.capability",
		.load = 1,
		.check = 1,
	},
	{
		.name = "gfid-req",
		.load = 0,
		.check = 1,
	},
	{},
};


static uint64_t
gfid_to_ino (uuid_t gfid)
{
	uint64_t  ino = 0;
	int       i = 0, j = 0;

        for (i = 15; i > (15 - 8); i--) {
                ino += (uint64_t)(gfid[i]) << j;
                j += 8;
        }

	return ino;
}


struct mdc_local;
typedef struct mdc_local mdc_local_t;

#define MDC_STACK_UNWIND(fop, frame, params ...) do {           \
                mdc_local_t *__local = NULL;                    \
                xlator_t    *__xl    = NULL;                    \
                if (frame) {                                    \
                        __xl         = frame->this;             \
                        __local      = frame->local;            \
                        frame->local = NULL;                    \
                }                                               \
                STACK_UNWIND_STRICT (fop, frame, params);       \
                mdc_local_wipe (__xl, __local);                 \
        } while (0)


struct md_cache {
        ia_prot_t     md_prot;
        uint32_t      md_nlink;
        uint32_t      md_uid;
        uint32_t      md_gid;
        uint32_t      md_atime;
        uint32_t      md_atime_nsec;
        uint32_t      md_mtime;
        uint32_t      md_mtime_nsec;
        uint32_t      md_ctime;
        uint32_t      md_ctime_nsec;
        uint64_t      md_rdev;
        uint64_t      md_size;
        uint64_t      md_blocks;
        dict_t       *xattr;
        char         *linkname;
	time_t        ia_time;
	time_t        xa_time;
        gf_lock_t     lock;
};


struct mdc_local {
        loc_t   loc;
        loc_t   loc2;
        fd_t   *fd;
        char   *linkname;
        dict_t *xattr;
};


int
__mdc_inode_ctx_get (xlator_t *this, inode_t *inode, struct md_cache **mdc_p)
{
        int              ret = 0;
        struct md_cache *mdc = NULL;
        uint64_t         mdc_int = 0;

	ret = __inode_ctx_get (inode, this, &mdc_int);
	mdc = (void *) (long) (mdc_int);
	if (ret == 0 && mdc_p)
		*mdc_p = mdc;

	return ret;
}


int
mdc_inode_ctx_get (xlator_t *this, inode_t *inode, struct md_cache **mdc_p)
{
	int   ret;

	LOCK(&inode->lock);
	{
		ret = __mdc_inode_ctx_get (this, inode, mdc_p);
	}
	UNLOCK(&inode->lock);

	return ret;
}


int
__mdc_inode_ctx_set (xlator_t *this, inode_t *inode, struct md_cache *mdc)
{
        int              ret = 0;
        uint64_t         mdc_int = 0;

	mdc_int = (long) mdc;
	ret = __inode_ctx_set2 (inode, this, &mdc_int, 0);

	return ret;
}


int
mdc_inode_ctx_set (xlator_t *this, inode_t *inode, struct md_cache *mdc)
{
	int   ret;

	LOCK(&inode->lock);
	{
		ret = __mdc_inode_ctx_set (this, inode, mdc);
	}
	UNLOCK(&inode->lock);

	return ret;
}


mdc_local_t *
mdc_local_get (call_frame_t *frame)
{
        mdc_local_t *local = NULL;

        local = frame->local;
        if (local)
                goto out;

        local = GF_CALLOC (sizeof (*local), 1, gf_mdc_mt_mdc_local_t);
        if (!local)
                goto out;

        frame->local = local;
out:
        return local;
}


void
mdc_local_wipe (xlator_t *this, mdc_local_t *local)
{
        if (!local)
                return;

        loc_wipe (&local->loc);

        loc_wipe (&local->loc2);

        if (local->fd)
                fd_unref (local->fd);

        if (local->linkname)
                GF_FREE (local->linkname);

        if (local->xattr)
                dict_unref (local->xattr);

        GF_FREE (local);
        return;
}


int
mdc_inode_wipe (xlator_t *this, inode_t *inode)
{
        int              ret = 0;
        uint64_t         mdc_int = 0;
        struct md_cache *mdc = NULL;

        ret = inode_ctx_del (inode, this, &mdc_int);
        if (ret != 0)
                goto out;

        mdc = (void *) (long) mdc_int;

        if (mdc->xattr)
                dict_unref (mdc->xattr);

        if (mdc->linkname)
                GF_FREE (mdc->linkname);

        GF_FREE (mdc);

        ret = 0;
out:
        return ret;
}


struct md_cache *
mdc_inode_prep (xlator_t *this, inode_t *inode)
{
        int              ret = 0;
        struct md_cache *mdc = NULL;

        LOCK (&inode->lock);
        {
		ret = __mdc_inode_ctx_get (this, inode, &mdc);
                if (ret == 0)
                        goto unlock;

                mdc = GF_CALLOC (sizeof (*mdc), 1, gf_mdc_mt_md_cache_t);
                if (!mdc) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "out of memory :(");
                        goto unlock;
                }

                LOCK_INIT (&mdc->lock);

                ret = __mdc_inode_ctx_set (this, inode, mdc);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "out of memory :(");
                        GF_FREE (mdc);
                        mdc = NULL;
                }
        }
unlock:
        UNLOCK (&inode->lock);

        return mdc;
}


static gf_boolean_t
is_md_cache_iatt_valid (xlator_t *this, struct md_cache *mdc)
{
	struct mdc_conf *conf = NULL;
	time_t           now = 0;
        gf_boolean_t     ret = _gf_true;
	conf = this->private;

	time (&now);

        LOCK (&mdc->lock);
        {
                if (now > (mdc->ia_time + conf->timeout))
                        ret = _gf_false;
        }
        UNLOCK (&mdc->lock);

	return ret;
}


static gf_boolean_t
is_md_cache_xatt_valid (xlator_t *this, struct md_cache *mdc)
{
	struct mdc_conf *conf = NULL;
	time_t           now = 0;
        gf_boolean_t     ret = _gf_true;

	conf = this->private;

	time (&now);

        LOCK (&mdc->lock);
        {
                if (now > (mdc->xa_time + conf->timeout))
                        ret = _gf_false;
        }
        UNLOCK (&mdc->lock);

	return ret;
}


void
mdc_from_iatt (struct md_cache *mdc, struct iatt *iatt)
{
        mdc->md_prot       = iatt->ia_prot;
        mdc->md_nlink      = iatt->ia_nlink;
        mdc->md_uid        = iatt->ia_uid;
        mdc->md_gid        = iatt->ia_gid;
        mdc->md_atime      = iatt->ia_atime;
        mdc->md_atime_nsec = iatt->ia_atime_nsec;
        mdc->md_mtime      = iatt->ia_mtime;
        mdc->md_mtime_nsec = iatt->ia_mtime_nsec;
        mdc->md_ctime      = iatt->ia_ctime;
        mdc->md_ctime_nsec = iatt->ia_ctime_nsec;
        mdc->md_rdev       = iatt->ia_rdev;
        mdc->md_size       = iatt->ia_size;
        mdc->md_blocks     = iatt->ia_blocks;
}


void
mdc_to_iatt (struct md_cache *mdc, struct iatt *iatt)
{
        iatt->ia_prot       = mdc->md_prot;
        iatt->ia_nlink      = mdc->md_nlink;
        iatt->ia_uid        = mdc->md_uid;
        iatt->ia_gid        = mdc->md_gid;
        iatt->ia_atime      = mdc->md_atime;
        iatt->ia_atime_nsec = mdc->md_atime_nsec;
        iatt->ia_mtime      = mdc->md_mtime;
        iatt->ia_mtime_nsec = mdc->md_mtime_nsec;
        iatt->ia_ctime      = mdc->md_ctime;
        iatt->ia_ctime_nsec = mdc->md_ctime_nsec;
        iatt->ia_rdev       = mdc->md_rdev;
        iatt->ia_size       = mdc->md_size;
        iatt->ia_blocks     = mdc->md_blocks;
}


int
mdc_inode_iatt_set (xlator_t *this, inode_t *inode, struct iatt *iatt)
{
        int              ret = -1;
        struct md_cache *mdc = NULL;

        mdc = mdc_inode_prep (this, inode);
        if (!mdc)
                goto out;

        LOCK (&mdc->lock);
        {
                if (!iatt || !iatt->ia_ctime) {
                        mdc->ia_time = 0;
                        goto unlock;
                }

                mdc_from_iatt (mdc, iatt);

                time (&mdc->ia_time);
        }
unlock:
        UNLOCK (&mdc->lock);
        ret = 0;
out:
        return ret;
}


int
mdc_inode_iatt_get (xlator_t *this, inode_t *inode, struct iatt *iatt)
{
        int              ret = -1;
        struct md_cache *mdc = NULL;

        if (mdc_inode_ctx_get (this, inode, &mdc) != 0)
                goto out;

	if (!is_md_cache_iatt_valid (this, mdc))
		goto out;

        LOCK (&mdc->lock);
        {
                mdc_to_iatt (mdc, iatt);
        }
        UNLOCK (&mdc->lock);

        uuid_copy (iatt->ia_gfid, inode->gfid);
        iatt->ia_ino    = gfid_to_ino (inode->gfid);
        iatt->ia_dev    = 42;
        iatt->ia_type   = inode->ia_type;

        ret = 0;
out:
        return ret;
}


int
mdc_inode_xatt_set (xlator_t *this, inode_t *inode, dict_t *dict)
{
        int              ret = -1;
        struct md_cache *mdc = NULL;

        mdc = mdc_inode_prep (this, inode);
        if (!mdc)
                goto out;

        if (!dict)
                goto out;

        LOCK (&mdc->lock);
        {
                if (mdc->xattr)
                        dict_unref (mdc->xattr);

                mdc->xattr = dict_ref (dict);

                time (&mdc->xa_time);
        }
        UNLOCK (&mdc->lock);
        ret = 0;
out:
        return ret;
}


int
mdc_inode_xatt_update (xlator_t *this, inode_t *inode, dict_t *dict)
{
        int              ret = -1;
        struct md_cache *mdc = NULL;

        mdc = mdc_inode_prep (this, inode);
        if (!mdc)
                goto out;

        if (!dict)
                goto out;

        LOCK (&mdc->lock);
        {
                if (!mdc->xattr)
                        mdc->xattr = dict_ref (dict);
                else
                        dict_copy (dict, mdc->xattr);

                mdc->xattr = dict_ref (dict);

                time (&mdc->xa_time);
        }
        UNLOCK (&mdc->lock);

        ret = 0;
out:
        return ret;
}


int
mdc_inode_xatt_get (xlator_t *this, inode_t *inode, dict_t **dict)
{
        int              ret = -1;
        struct md_cache *mdc = NULL;

        if (mdc_inode_ctx_get (this, inode, &mdc) != 0)
                goto out;

	if (!is_md_cache_xatt_valid (this, mdc))
		goto out;

        LOCK (&mdc->lock);
        {
                if (!mdc->xattr)
                        goto unlock;

                if (dict)
                        *dict = dict_ref (mdc->xattr);

                ret = 0;
        }
unlock:
        UNLOCK (&mdc->lock);

out:
        return ret;
}


void
mdc_load_reqs (xlator_t *this, dict_t *dict)
{
	const char *mdc_key = NULL;
	int  i = 0;
	int  ret = 0;

	for (mdc_key = mdc_keys[i].name; (mdc_key = mdc_keys[i].name); i++) {
		if (!mdc_keys[i].load)
			continue;
		ret = dict_set_int8 (dict, (char *)mdc_key, 0);
		if (ret)
			return;
	}
}


struct checkpair {
	int  ret;
	dict_t *rsp;
};


static int
is_mdc_key_satisfied (const char *key)
{
	const char *mdc_key = NULL;
	int  i = 0;

	if (!key)
		return 0;

	for (mdc_key = mdc_keys[i].name; (mdc_key = mdc_keys[i].name); i++) {
		if (!mdc_keys[i].check)
			continue;
		if (strcmp (mdc_key, key) == 0)
			return 1;
	}

	return 0;
}


static void
checkfn (dict_t *this, char *key, data_t *value, void *data)
{
        struct checkpair *pair = data;

	if (!is_mdc_key_satisfied (key))
		pair->ret = 0;
}


int
mdc_xattr_satisfied (xlator_t *this, dict_t *req, dict_t *rsp)
{
        struct checkpair pair = {
                .ret = 1,
                .rsp = rsp,
        };

        dict_foreach (req, checkfn, &pair);

        return pair.ret;
}


int
mdc_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret,	int32_t op_errno, inode_t *inode,
                struct iatt *stbuf, dict_t *dict, struct iatt *postparent)
{
        mdc_local_t *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        if (local->loc.parent) {
                mdc_inode_iatt_set (this, local->loc.parent, postparent);
        }

        if (local->loc.inode) {
                mdc_inode_iatt_set (this, local->loc.inode, stbuf);
                mdc_inode_xatt_set (this, local->loc.inode, dict);
        }
out:
        MDC_STACK_UNWIND (lookup, frame, op_ret, op_errno, inode, stbuf,
                          dict, postparent);
        return 0;
}


int
mdc_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc,
            dict_t *xattr_req)
{
        int          ret = 0;
        struct iatt  stbuf = {0, };
        struct iatt  postparent = {0, };
        dict_t      *xattr_rsp = NULL;
        mdc_local_t *local = NULL;


        local = mdc_local_get (frame);
        if (!local)
                goto uncached;

        loc_copy (&local->loc, loc);

        ret = mdc_inode_iatt_get (this, loc->inode, &stbuf);
        if (ret != 0)
                goto uncached;

        if (xattr_req) {
                ret = mdc_inode_xatt_get (this, loc->inode, &xattr_rsp);
                if (ret != 0)
                        goto uncached;

                if (!mdc_xattr_satisfied (this, xattr_req, xattr_rsp))
                        goto uncached;
        }

        MDC_STACK_UNWIND (lookup, frame, 0, 0, loc->inode, &stbuf,
                          xattr_rsp, &postparent);

        if (xattr_rsp)
                dict_unref (xattr_rsp);

        return 0;

uncached:
	if (xattr_req)
		mdc_load_reqs (this, xattr_req);

        STACK_WIND (frame, mdc_lookup_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->lookup, loc, xattr_req);

        if (xattr_rsp)
                dict_unref (xattr_rsp);

        return 0;
}


int
mdc_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        mdc_local_t  *local = NULL;

        if (op_ret != 0)
                goto out;

        local = frame->local;
        if (!local)
                goto out;

        mdc_inode_iatt_set (this, local->loc.inode, buf);

out:
        MDC_STACK_UNWIND (stat, frame, op_ret, op_errno, buf);

        return 0;
}


int
mdc_stat (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        int           ret;
        struct iatt   stbuf;
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);
        if (!local)
                goto uncached;

        loc_copy (&local->loc, loc);

        ret = mdc_inode_iatt_get (this, loc->inode, &stbuf);
        if (ret != 0)
                goto uncached;

        MDC_STACK_UNWIND (stat, frame, 0, 0, &stbuf);

        return 0;

uncached:
        STACK_WIND (frame, mdc_stat_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->stat,
                    loc);
        return 0;
}


int
mdc_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        mdc_local_t  *local = NULL;

        if (op_ret != 0)
                goto out;

        local = frame->local;
        if (!local)
                goto out;

        mdc_inode_iatt_set (this, local->fd->inode, buf);

out:
        MDC_STACK_UNWIND (fstat, frame, op_ret, op_errno, buf);

        return 0;
}


int
mdc_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        int           ret;
        struct iatt   stbuf;
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);
        if (!local)
                goto uncached;

        local->fd = fd_ref (fd);

        ret = mdc_inode_iatt_get (this, fd->inode, &stbuf);
        if (ret != 0)
                goto uncached;

        MDC_STACK_UNWIND (fstat, frame, 0, 0, &stbuf);

        return 0;

uncached:
        STACK_WIND (frame, mdc_fstat_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fstat,
                    fd);
        return 0;
}


int
mdc_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  struct iatt *prebuf, struct iatt *postbuf)
{
        mdc_local_t  *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        mdc_inode_iatt_set (this, local->loc.inode, postbuf);

out:
        MDC_STACK_UNWIND (truncate, frame, op_ret, op_errno, prebuf, postbuf);

        return 0;
}


int
mdc_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc,
              off_t offset)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        local->loc.inode = inode_ref (loc->inode);

        STACK_WIND (frame, mdc_truncate_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->truncate,
                    loc, offset);
        return 0;
}


int
mdc_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno,
                   struct iatt *prebuf, struct iatt *postbuf)
{
        mdc_local_t  *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        mdc_inode_iatt_set (this, local->fd->inode, postbuf);

out:
        MDC_STACK_UNWIND (ftruncate, frame, op_ret, op_errno, prebuf, postbuf);

        return 0;
}


int
mdc_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd,
               off_t offset)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        local->fd = fd_ref (fd);

        STACK_WIND (frame, mdc_ftruncate_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->ftruncate,
                    fd, offset);
        return 0;
}


int
mdc_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode,
               struct iatt *buf, struct iatt *preparent, struct iatt *postparent)
{
        mdc_local_t *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        if (local->loc.parent) {
                mdc_inode_iatt_set (this, local->loc.parent, postparent);
        }

        if (local->loc.inode) {
                mdc_inode_iatt_set (this, local->loc.inode, buf);
                mdc_inode_xatt_set (this, local->loc.inode, local->xattr);
        }
out:
        MDC_STACK_UNWIND (mknod, frame, op_ret, op_errno, inode, buf,
                          preparent, postparent);
        return 0;
}


int
mdc_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc,
           mode_t mode, dev_t rdev, dict_t *params)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        loc_copy (&local->loc, loc);
        local->xattr = dict_ref (params);

        STACK_WIND (frame, mdc_mknod_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->mknod,
                    loc, mode, rdev, params);
        return 0;
}


int
mdc_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode,
               struct iatt *buf, struct iatt *preparent, struct iatt *postparent)
{
        mdc_local_t *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        if (local->loc.parent) {
                mdc_inode_iatt_set (this, local->loc.parent, postparent);
        }

        if (local->loc.inode) {
                mdc_inode_iatt_set (this, local->loc.inode, buf);
                mdc_inode_xatt_set (this, local->loc.inode, local->xattr);
        }
out:
        MDC_STACK_UNWIND (mkdir, frame, op_ret, op_errno, inode, buf,
                          preparent, postparent);
        return 0;
}


int
mdc_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc,
           mode_t mode, dict_t *params)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        loc_copy (&local->loc, loc);
        local->xattr = dict_ref (params);

        STACK_WIND (frame, mdc_mkdir_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->mkdir,
                    loc, mode, params);
        return 0;
}


int
mdc_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno,
                struct iatt *preparent, struct iatt *postparent)
{
        mdc_local_t *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        if (local->loc.parent) {
                mdc_inode_iatt_set (this, local->loc.parent, postparent);
        }

        if (local->loc.inode) {
                mdc_inode_iatt_set (this, local->loc.inode, NULL);
        }

out:
        MDC_STACK_UNWIND (unlink, frame, op_ret, op_errno,
                          preparent, postparent);
        return 0;
}


int
mdc_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        loc_copy (&local->loc, loc);

        STACK_WIND (frame, mdc_unlink_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->unlink,
                    loc);
        return 0;
}


int
mdc_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno,
                struct iatt *preparent, struct iatt *postparent)
{
        mdc_local_t *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        if (local->loc.parent) {
                mdc_inode_iatt_set (this, local->loc.parent, postparent);
        }

out:
        MDC_STACK_UNWIND (rmdir, frame, op_ret, op_errno,
                          preparent, postparent);
        return 0;
}


int
mdc_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flag)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        loc_copy (&local->loc, loc);

        STACK_WIND (frame, mdc_rmdir_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->rmdir,
                    loc, flag);
        return 0;
}


int
mdc_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, inode_t *inode,
                 struct iatt *buf, struct iatt *preparent, struct iatt *postparent)
{
        mdc_local_t *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        if (local->loc.parent) {
                mdc_inode_iatt_set (this, local->loc.parent, postparent);
        }

        if (local->loc.inode) {
                mdc_inode_iatt_set (this, local->loc.inode, buf);
        }
out:
        MDC_STACK_UNWIND (symlink, frame, op_ret, op_errno, inode, buf,
                          preparent, postparent);
        return 0;
}


int
mdc_symlink (call_frame_t *frame, xlator_t *this, const char *linkname,
             loc_t *loc, dict_t *params)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        loc_copy (&local->loc, loc);

        local->linkname = gf_strdup (linkname);

        STACK_WIND (frame, mdc_symlink_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->symlink,
                    linkname, loc, params);
        return 0;
}


int
mdc_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *buf,
                struct iatt *preoldparent, struct iatt *postoldparent,
                struct iatt *prenewparent, struct iatt *postnewparent)
{
        mdc_local_t *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        if (local->loc.parent) {
                mdc_inode_iatt_set (this, local->loc.parent, postoldparent);
        }

        if (local->loc.inode) {
		/* TODO: fix dht_rename() not to return linkfile
		   attributes before setting attributes here
		*/

		mdc_inode_iatt_set (this, local->loc.inode, NULL);
        }

        if (local->loc2.parent) {
                mdc_inode_iatt_set (this, local->loc2.parent, postnewparent);
        }
out:
        MDC_STACK_UNWIND (rename, frame, op_ret, op_errno, buf,
                          preoldparent, postoldparent, prenewparent, postnewparent);
        return 0;
}


int
mdc_rename (call_frame_t *frame, xlator_t *this,
            loc_t *oldloc, loc_t *newloc)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        loc_copy (&local->loc, oldloc);
        loc_copy (&local->loc2, newloc);

        STACK_WIND (frame, mdc_rename_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->rename,
                    oldloc, newloc);
        return 0;
}


int
mdc_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, inode_t *inode, struct iatt *buf,
              struct iatt *preparent, struct iatt *postparent)
{
        mdc_local_t *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        if (local->loc.inode) {
                mdc_inode_iatt_set (this, local->loc.inode, buf);
        }

        if (local->loc2.parent) {
                mdc_inode_iatt_set (this, local->loc2.parent, postparent);
        }
out:
        MDC_STACK_UNWIND (link, frame, op_ret, op_errno, inode, buf,
                          preparent, postparent);
        return 0;
}


int
mdc_link (call_frame_t *frame, xlator_t *this,
          loc_t *oldloc, loc_t *newloc)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        loc_copy (&local->loc, oldloc);
        loc_copy (&local->loc2, newloc);

        STACK_WIND (frame, mdc_link_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->link,
                    oldloc, newloc);
        return 0;
}


int
mdc_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
                struct iatt *buf, struct iatt *preparent, struct iatt *postparent)
{
        mdc_local_t *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        if (local->loc.parent) {
                mdc_inode_iatt_set (this, local->loc.parent, postparent);
        }

        if (local->loc.inode) {
                mdc_inode_iatt_set (this, inode, buf);
                mdc_inode_xatt_set (this, local->loc.inode, local->xattr);
        }
out:
        MDC_STACK_UNWIND (create, frame, op_ret, op_errno, fd, inode, buf,
                          preparent, postparent);
        return 0;
}


int
mdc_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
            mode_t mode, fd_t *fd, dict_t *params)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        loc_copy (&local->loc, loc);
        local->xattr = dict_ref (params);

        STACK_WIND (frame, mdc_create_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->create,
                    loc, flags, mode, fd, params);
        return 0;
}


int
mdc_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno,
               struct iovec *vector, int32_t count,
               struct iatt *stbuf, struct iobref *iobref)
{
        mdc_local_t  *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        mdc_inode_iatt_set (this, local->fd->inode, stbuf);

out:
        MDC_STACK_UNWIND (readv, frame, op_ret, op_errno, vector, count,
                          stbuf, iobref);

        return 0;
}


int
mdc_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
           off_t offset, uint32_t flags)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        local->fd = fd_ref (fd);

        STACK_WIND (frame, mdc_readv_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->readv,
                    fd, size, offset, flags);
        return 0;
}


int
mdc_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno,
               struct iatt *prebuf, struct iatt *postbuf)
{
        mdc_local_t  *local = NULL;

        local = frame->local;

        if (op_ret == -1)
                goto out;

        if (!local)
                goto out;

        mdc_inode_iatt_set (this, local->fd->inode, postbuf);

out:
        MDC_STACK_UNWIND (writev, frame, op_ret, op_errno, prebuf, postbuf);

        return 0;
}


int
mdc_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
            int count, off_t offset, uint32_t flags, struct iobref *iobref)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        local->fd = fd_ref (fd);

        STACK_WIND (frame, mdc_writev_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->writev,
                    fd, vector, count, offset, flags, iobref);
        return 0;
}


int
mdc_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 struct iatt *prebuf, struct iatt *postbuf)
{
        mdc_local_t  *local = NULL;

        local = frame->local;

        if (op_ret != 0) {
		mdc_inode_iatt_set (this, local->loc.inode, NULL);
                goto out;
	}

        if (!local)
                goto out;

        mdc_inode_iatt_set (this, local->loc.inode, postbuf);

out:
        MDC_STACK_UNWIND (setattr, frame, op_ret, op_errno, prebuf, postbuf);

        return 0;
}


int
mdc_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
             struct iatt *stbuf, int valid)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        loc_copy (&local->loc, loc);

        STACK_WIND (frame, mdc_setattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->setattr,
                    loc, stbuf, valid);
        return 0;
}


int
mdc_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  struct iatt *prebuf, struct iatt *postbuf)
{
        mdc_local_t  *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        mdc_inode_iatt_set (this, local->fd->inode, postbuf);

out:
        MDC_STACK_UNWIND (fsetattr, frame, op_ret, op_errno, prebuf, postbuf);

        return 0;
}


int
mdc_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
              struct iatt *stbuf, int valid)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        local->fd = fd_ref (fd);

        STACK_WIND (frame, mdc_setattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fsetattr,
                    fd, stbuf, valid);
        return 0;
}


int
mdc_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno,
               struct iatt *prebuf, struct iatt *postbuf)
{
        mdc_local_t  *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        mdc_inode_iatt_set (this, local->fd->inode, postbuf);

out:
        MDC_STACK_UNWIND (fsync, frame, op_ret, op_errno, prebuf, postbuf);

        return 0;
}


int
mdc_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int datasync)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        local->fd = fd_ref (fd);

        STACK_WIND (frame, mdc_fsync_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fsync,
                    fd, datasync);
        return 0;
}


int
mdc_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno)
{
        mdc_local_t  *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        mdc_inode_xatt_update (this, local->loc.inode, local->xattr);

out:
        MDC_STACK_UNWIND (setxattr, frame, op_ret, op_errno);

        return 0;
}


int
mdc_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
              dict_t *xattr, int flags)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        loc_copy (&local->loc, loc);
        local->xattr = dict_ref (xattr);

        STACK_WIND (frame, mdc_setxattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->setxattr,
                    loc, xattr, flags);
        return 0;
}


int
mdc_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		   int32_t op_ret, int32_t op_errno)
{
        mdc_local_t  *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        mdc_inode_xatt_update (this, local->fd->inode, local->xattr);

out:
        MDC_STACK_UNWIND (fsetxattr, frame, op_ret, op_errno);

        return 0;
}


int
mdc_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
	       dict_t *xattr, int flags)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

	local->fd = fd_ref (fd);
        local->xattr = dict_ref (xattr);

        STACK_WIND (frame, mdc_fsetxattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fsetxattr,
                    fd, xattr, flags);
        return 0;
}

int
mdc_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int32_t op_ret, int32_t op_errno, dict_t *xattr)
{
        mdc_local_t  *local = NULL;

        if (op_ret != 0)
                goto out;

        local = frame->local;
        if (!local)
                goto out;

        mdc_inode_xatt_update (this, local->loc.inode, xattr);

out:
        MDC_STACK_UNWIND (getxattr, frame, op_ret, op_errno, xattr);

        return 0;
}


int
mdc_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, const char *key)
{
        int           ret;
        mdc_local_t  *local = NULL;
	dict_t       *xattr = NULL;

        local = mdc_local_get (frame);
        if (!local)
                goto uncached;

        loc_copy (&local->loc, loc);

	if (!is_mdc_key_satisfied (key))
		goto uncached;

	ret = mdc_inode_xatt_get (this, loc->inode, &xattr);
	if (ret != 0)
		goto uncached;

	if (!dict_get (xattr, (char *)key))
		goto uncached;

        MDC_STACK_UNWIND (getxattr, frame, 0, 0, xattr);

        return 0;

uncached:
        STACK_WIND (frame, mdc_getxattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->getxattr,
                    loc, key);
        return 0;
}


int
mdc_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		   int32_t op_ret, int32_t op_errno, dict_t *xattr)
{
        mdc_local_t  *local = NULL;

        if (op_ret != 0)
                goto out;

        local = frame->local;
        if (!local)
                goto out;

        mdc_inode_xatt_update (this, local->fd->inode, xattr);

out:
        MDC_STACK_UNWIND (fgetxattr, frame, op_ret, op_errno, xattr);

        return 0;
}


int
mdc_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, const char *key)
{
        int           ret;
        mdc_local_t  *local = NULL;
	dict_t       *xattr = NULL;

        local = mdc_local_get (frame);
        if (!local)
                goto uncached;

        local->fd = fd_ref (fd);

	if (!is_mdc_key_satisfied (key))
		goto uncached;

	ret = mdc_inode_xatt_get (this, fd->inode, &xattr);
	if (ret != 0)
		goto uncached;

	if (!dict_get (xattr, (char *)key))
		goto uncached;

        MDC_STACK_UNWIND (fgetxattr, frame, 0, 0, xattr);

        return 0;

uncached:
        STACK_WIND (frame, mdc_fgetxattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fgetxattr,
                    fd, key);
        return 0;
}


int
mdc_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int op_ret, int op_errno, gf_dirent_t *entries)
{
        gf_dirent_t *entry      = NULL;

	if (op_ret <= 0)
		goto unwind;

        list_for_each_entry (entry, &entries->list, list) {
                if (!entry->inode)
			continue;
                mdc_inode_iatt_set (this, entry->inode, &entry->d_stat);
                mdc_inode_xatt_set (this, entry->inode, entry->dict);
        }

unwind:
	STACK_UNWIND_STRICT (readdirp, frame, op_ret, op_errno, entries);
	return 0;
}


int
mdc_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd,
	      size_t size, off_t offset, dict_t *xattr_req)
{
	STACK_WIND (frame, mdc_readdirp_cbk,
		    FIRST_CHILD (this), FIRST_CHILD (this)->fops->readdirp,
		    fd, size, offset, xattr_req);
	return 0;
}


int
mdc_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd,
	     size_t size, off_t offset)
{
	dict_t *xattr_req = NULL;

	xattr_req = dict_new ();

	if (xattr_req) {
		mdc_load_reqs (this, xattr_req);
	}

	STACK_WIND (frame, mdc_readdirp_cbk,
		    FIRST_CHILD (this), FIRST_CHILD (this)->fops->readdirp,
		    fd, size, offset, xattr_req);

        dict_unref (xattr_req);
	return 0;
}


int
mdc_forget (xlator_t *this, inode_t *inode)
{
        mdc_inode_wipe (this, inode);

        return 0;
}


int
reconfigure (xlator_t *this, dict_t *options)
{
	struct mdc_conf *conf = NULL;

	conf = this->private;

	GF_OPTION_RECONF ("timeout", conf->timeout, options, int32, out);
out:
	return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        ret = xlator_mem_acct_init (this, gf_mdc_mt_end + 1);
        return ret;
}

int
init (xlator_t *this)
{
	struct mdc_conf *conf = NULL;

	conf = GF_CALLOC (sizeof (*conf), 1, gf_mdc_mt_mdc_conf_t);
	if (!conf) {
		gf_log (this->name, GF_LOG_ERROR,
			"out of memory");
		return -1;
	}

        GF_OPTION_INIT ("timeout", conf->timeout, int32, out);

out:
	this->private = conf;

        return 0;
}


void
fini (xlator_t *this)
{
        return;
}


struct xlator_fops fops = {
        .lookup      = mdc_lookup,
        .stat        = mdc_stat,
        .fstat       = mdc_fstat,
        .truncate    = mdc_truncate,
        .ftruncate   = mdc_ftruncate,
        .mknod       = mdc_mknod,
        .mkdir       = mdc_mkdir,
        .unlink      = mdc_unlink,
        .rmdir       = mdc_rmdir,
        .symlink     = mdc_symlink,
        .rename      = mdc_rename,
        .link        = mdc_link,
        .create      = mdc_create,
        .readv       = mdc_readv,
        .writev      = mdc_writev,
        .setattr     = mdc_setattr,
        .fsetattr    = mdc_fsetattr,
        .fsync       = mdc_fsync,
        .setxattr    = mdc_setxattr,
        .fsetxattr   = mdc_fsetxattr,
        .getxattr    = mdc_getxattr,
        .fgetxattr   = mdc_fgetxattr,
	.readdirp    = mdc_readdirp,
	.readdir     = mdc_readdir
};


struct xlator_cbks cbks = {
        .forget      = mdc_forget,
};

struct volume_options options[] = {
        { .key = {"timeout"},
          .type = GF_OPTION_TYPE_INT,
          .min = 0,
          .max = 60,
          .default_value = "1",
          .description = "Time period after which cache has to be refreshed",
        },
};
