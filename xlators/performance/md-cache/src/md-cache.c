/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "glusterfs.h"
#include "defaults.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "syncop.h"
#include "md-cache-mem-types.h"
#include "compat-errno.h"
#include "glusterfs-acl.h"
#include "defaults.h"
#include "upcall-utils.h"
#include <assert.h>
#include <sys/time.h>
#include "md-cache-messages.h"
#include "statedump.h"

/* TODO:
   - cache symlink() link names and nuke symlink-cache
   - send proper postbuf in setattr_cbk even when op_ret = -1
*/

struct mdc_statistics {
        uint64_t stat_hit; /* No. of times lookup/stat was served from mdc */
        uint64_t stat_miss; /* No. of times valid stat wasn't present in mdc */
        uint64_t xattr_hit; /* No. of times getxattr was served from mdc, Note:
                             this doesn't count the xattr served from lookup */
        uint64_t xattr_miss; /* No. of times xattr req was WIND from mdc */
        uint64_t negative_lookup; /* No. of negative lookups */
        uint64_t nameless_lookup; /* No. of negative lookups that were sent
                                     sent to bricks */
        uint64_t stat_invals; /* No. of invalidates recieved from upcall*/
        uint64_t xattr_invals; /* No. of invalidates recieved from upcall*/
        uint64_t need_lookup; /* No. of lookups issued, because other xlators
                               * requested for explicit lookup */
        gf_lock_t lock;
};

struct mdc_conf {
	int  timeout;
	gf_boolean_t cache_posix_acl;
	gf_boolean_t cache_selinux;
        gf_boolean_t cache_capability;
        gf_boolean_t cache_ima;
	gf_boolean_t force_readdirp;
        gf_boolean_t cache_swift_metadata;
        gf_boolean_t cache_samba_metadata;
        gf_boolean_t mdc_invalidation;
        time_t last_child_down;
        gf_lock_t lock;
        struct mdc_statistics mdc_counter;
};


static struct mdc_key {
	const char *name;
	int         load;
	int         check;
} mdc_keys[] = {
	{
		.name = POSIX_ACL_ACCESS_XATTR,
		.load = 0,
		.check = 1,
	},
	{
		.name = POSIX_ACL_DEFAULT_XATTR,
		.load = 0,
		.check = 1,
	},
	{
		.name = GF_POSIX_ACL_ACCESS,
		.load = 0,
		.check = 1,
	},
	{
		.name = GF_POSIX_ACL_DEFAULT,
		.load = 0,
		.check = 1,
	},
	{
		.name = GF_SELINUX_XATTR_KEY,
		.load = 0,
		.check = 1,
	},
        {
                .name = "user.swift.metadata",
                .load = 0,
                .check = 1,
        },
        {
                .name = "user.DOSATTRIB",
                .load = 0,
                .check = 1,
        },
        {
                .name = "security.NTACL",
                .load = 0,
                .check = 1,
        },
	{
		.name = "security.capability",
		.load = 0,
		.check = 1,
	},
	{
		.name = "gfid-req",
		.load = 0,
		.check = 1,
	},
        {
                .name = "security.ima",
                .load = 0,
                .check = 1,
        },
        {
                .name = NULL,
                .load = 0,
                .check = 0,
        }
};

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
        gf_boolean_t  need_lookup;
        gf_lock_t     lock;
};


struct mdc_local {
        loc_t   loc;
        loc_t   loc2;
        fd_t   *fd;
        char   *linkname;
	char   *key;
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
	ret = __inode_ctx_set (inode, this, &mdc_int);

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

        GF_FREE (local->linkname);

        GF_FREE (local->key);

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
                        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                                MD_CACHE_MSG_NO_MEMORY, "out of memory");
                        goto unlock;
                }

                LOCK_INIT (&mdc->lock);

                ret = __mdc_inode_ctx_set (this, inode, mdc);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                                MD_CACHE_MSG_NO_MEMORY, "out of memory");
                        GF_FREE (mdc);
                        mdc = NULL;
                }
        }
unlock:
        UNLOCK (&inode->lock);

        return mdc;
}


/* Cache is valid if:
 * - It is not cached before any brick was down. Brick down case is handled by
 *   invalidating all the cache when any brick went down.
 * - The cache time is not expired
 */
static gf_boolean_t
__is_cache_valid (xlator_t *this, time_t mdc_time)
{
        time_t           now             = 0;
        gf_boolean_t     ret             = _gf_true;
        struct mdc_conf *conf            = NULL;
        int              timeout         = 0;
        time_t           last_child_down = 0;

        conf = this->private;

        /* conf->lock here is not taken deliberately, so that the multi
         * threaded IO doesn't contend on a global lock. While updating
         * the variable, the lock is taken, so that atleast the writes are
         * intact. The read of last_child_down may return junk, but that
         * is for a very short period of time.
         */
        last_child_down = conf->last_child_down;
        timeout = conf->timeout;

        time (&now);

        if ((mdc_time == 0) ||
            ((last_child_down != 0) && (mdc_time < last_child_down))) {
                ret = _gf_false;
                goto out;
        }

        if (now >= (mdc_time + timeout)) {
                ret = _gf_false;
        }

out:
        return ret;
}


static gf_boolean_t
is_md_cache_iatt_valid (xlator_t *this, struct md_cache *mdc)
{
        gf_boolean_t     ret = _gf_true;

        LOCK (&mdc->lock);
        {
                ret = __is_cache_valid (this, mdc->ia_time);
                if (ret == _gf_false)
                        mdc->ia_time = 0;
        }
        UNLOCK (&mdc->lock);

	return ret;
}


static gf_boolean_t
is_md_cache_xatt_valid (xlator_t *this, struct md_cache *mdc)
{
        gf_boolean_t     ret = _gf_true;

        LOCK (&mdc->lock);
        {
                ret = __is_cache_valid (this, mdc->xa_time);
                if (ret == _gf_false)
                        mdc->xa_time = 0;
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
mdc_inode_iatt_set_validate(xlator_t *this, inode_t *inode, struct iatt *prebuf,
			    struct iatt *iatt, gf_boolean_t update_time)
{
        int              ret = 0;
        struct md_cache *mdc = NULL;

        mdc = mdc_inode_prep (this, inode);
        if (!mdc) {
                ret = -1;
                goto out;
        }

        LOCK (&mdc->lock);
        {
                if (!iatt || !iatt->ia_ctime) {
                        gf_msg_trace ("md-cache", 0, "invalidating iatt(NULL)"
                                      "(%s)", uuid_utoa (inode->gfid));
                        mdc->ia_time = 0;
                        goto unlock;
                }

                /* There could be a race in invalidation, where the
                 * invalidations in order A, B reaches md-cache in the order
                 * B, A. Hence, make sure the invalidation A is discarded if
                 * it comes after B. ctime of a file is always in ascending
                 * order unlike atime and mtime(which can be changed by user
                 * to any date), also ctime gets updates when atime/mtime
                 * changes, hence check for ctime only.
                 */
                if (mdc->md_ctime > iatt->ia_ctime) {
                        gf_msg_callingfn (this->name, GF_LOG_DEBUG, EINVAL,
                                          MD_CACHE_MSG_DISCARD_UPDATE,
                                          "discarding the iatt validate "
                                          "request");
                        ret = -1;
                        goto unlock;

                }
                if ((mdc->md_ctime == iatt->ia_ctime) &&
                    (mdc->md_ctime_nsec > iatt->ia_ctime_nsec)) {
                        gf_msg_callingfn (this->name, GF_LOG_DEBUG, EINVAL,
                                          MD_CACHE_MSG_DISCARD_UPDATE,
                                          "discarding the iatt validate "
                                          "request(ctime_nsec)");
                        ret = -1;
                        goto unlock;
                }

		/*
		 * Invalidate the inode if the mtime or ctime has changed
		 * and the prebuf doesn't match the value we have cached.
		 * TODO: writev returns with a NULL iatt due to
		 * performance/write-behind, causing invalidation on writes.
		 */
		if (IA_ISREG(inode->ia_type) &&
		    ((iatt->ia_mtime != mdc->md_mtime) ||
		    (iatt->ia_mtime_nsec != mdc->md_mtime_nsec) ||
		    (iatt->ia_ctime != mdc->md_ctime) ||
		    (iatt->ia_ctime_nsec != mdc->md_ctime_nsec)))
			if (!prebuf || (prebuf->ia_ctime != mdc->md_ctime) ||
			    (prebuf->ia_ctime_nsec != mdc->md_ctime_nsec) ||
			    (prebuf->ia_mtime != mdc->md_mtime) ||
			    (prebuf->ia_mtime_nsec != mdc->md_mtime_nsec)) {
                                gf_msg_trace ("md-cache", 0, "prebuf doesn't "
                                              "match the value we have cached,"
                                              " invalidate the inode(%s)",
                                              uuid_utoa (inode->gfid));

				inode_invalidate(inode);
                        }

                mdc_from_iatt (mdc, iatt);

                if (update_time)
                        time (&mdc->ia_time);

                gf_msg_callingfn ("md-cache", GF_LOG_TRACE, 0,
                                  MD_CACHE_MSG_CACHE_UPDATE, "Updated iatt(%s)"
                                  " time:%lld ", uuid_utoa (inode->gfid),
                                  (long long)mdc->ia_time);
        }
unlock:
        UNLOCK (&mdc->lock);

out:
        return ret;
}

int mdc_inode_iatt_set(xlator_t *this, inode_t *inode, struct iatt *iatt)
{
	return mdc_inode_iatt_set_validate(this, inode, NULL, iatt, _gf_true);
}

int
mdc_inode_iatt_get (xlator_t *this, inode_t *inode, struct iatt *iatt)
{
        int              ret = -1;
        struct md_cache *mdc = NULL;

        if (mdc_inode_ctx_get (this, inode, &mdc) != 0) {
                gf_msg_trace ("md-cache", 0, "mdc_inode_ctx_get failed (%s)",
                              uuid_utoa (inode->gfid));
                goto out;
        }

	if (!is_md_cache_iatt_valid (this, mdc)) {
                gf_msg_trace ("md-cache", 0, "iatt cache not valid for (%s)",
                              uuid_utoa (inode->gfid));
		goto out;
        }

        LOCK (&mdc->lock);
        {
                mdc_to_iatt (mdc, iatt);
        }
        UNLOCK (&mdc->lock);

        gf_uuid_copy (iatt->ia_gfid, inode->gfid);
        iatt->ia_ino    = gfid_to_ino (inode->gfid);
        iatt->ia_dev    = 42;
        iatt->ia_type   = inode->ia_type;

        ret = 0;
out:
        return ret;
}

struct updatedict {
	dict_t *dict;
	int ret;
};

static int
updatefn(dict_t *dict, char *key, data_t *value, void *data)
{
	struct updatedict *u = data;
	const char *mdc_key;
	int i = 0;

	for (mdc_key = mdc_keys[i].name; (mdc_key = mdc_keys[i].name); i++) {
		if (!mdc_keys[i].check)
			continue;
		if (strcmp(mdc_key, key))
			continue;

		if (!u->dict) {
			u->dict = dict_new();
			if (!u->dict) {
				u->ret = -1;
				return -1;
			}
		}

                /* posix xlator as part of listxattr will send both names
                 * and values of the xattrs in the dict. But as per man page
                 * listxattr is mainly supposed to send names of the all the
                 * xattrs. gfapi, as of now will put all the keys it obtained
                 * in the dict (sent by posix) into a buffer provided by the
                 * caller (thus the values of those xattrs are lost). If some
                 * xlator makes gfapi based calls (ex: snapview-server), then
                 * it has to unwind the calls by putting those names it got
                 * in the buffer again into the dict. But now it would not be
                 * having the values for those xattrs. So it might just put
                 * a 0 byte value ("") into the dict for each xattr and unwind
                 * the call. So the xlators which cache the xattrs (as of now
                 * md-cache caches the acl and selinux related xattrs), should
                 * not update their cache if the value of a xattr is a 0 byte
                 * data (i.e. "").
                 */
                if (!strcmp (value->data, ""))
                        continue;

		if (dict_set(u->dict, key, value) < 0) {
			u->ret = -1;
			return -1;
		}

		break;
	}
        return 0;
}

static int
mdc_dict_update(dict_t **tgt, dict_t *src)
{
	struct updatedict u = {
		.dict = *tgt,
		.ret = 0,
	};

	dict_foreach(src, updatefn, &u);

	if (*tgt)
		return u.ret;

	if ((u.ret < 0) && u.dict) {
		dict_unref(u.dict);
		return u.ret;
	}

	*tgt = u.dict;

	return u.ret;
}

int
mdc_inode_xatt_set (xlator_t *this, inode_t *inode, dict_t *dict)
{
        int              ret = -1;
        struct md_cache *mdc = NULL;
	dict_t		*newdict = NULL;

        mdc = mdc_inode_prep (this, inode);
        if (!mdc)
                goto out;

        if (!dict) {
                gf_msg_trace ("md-cache", 0, "mdc_inode_xatt_set failed (%s) "
                              "dict NULL", uuid_utoa (inode->gfid));
                goto out;
        }

        LOCK (&mdc->lock);
        {
                if (mdc->xattr) {
                        gf_msg_trace ("md-cache", 0, "deleteing the old xattr "
                              "cache (%s)", uuid_utoa (inode->gfid));
                        dict_unref (mdc->xattr);
			mdc->xattr = NULL;
		}

		ret = mdc_dict_update(&newdict, dict);
		if (ret < 0) {
			UNLOCK(&mdc->lock);
			goto out;
		}

		if (newdict)
			mdc->xattr = newdict;

                time (&mdc->xa_time);
                gf_msg_trace ("md-cache", 0, "xatt cache set for (%s) time:%lld",
                              uuid_utoa (inode->gfid), (long long)mdc->xa_time);

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
		ret = mdc_dict_update(&mdc->xattr, dict);
		if (ret < 0) {
			UNLOCK(&mdc->lock);
			goto out;
		}
        }
        UNLOCK (&mdc->lock);

        ret = 0;
out:
        return ret;
}


int
mdc_inode_xatt_unset (xlator_t *this, inode_t *inode, char *name)
{
        int              ret = -1;
        struct md_cache *mdc = NULL;

        mdc = mdc_inode_prep (this, inode);
        if (!mdc)
                goto out;

        if (!name || !mdc->xattr)
                goto out;

        LOCK (&mdc->lock);
        {
		dict_del (mdc->xattr, name);
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

        if (mdc_inode_ctx_get (this, inode, &mdc) != 0) {
                gf_msg_trace ("md-cache", 0, "mdc_inode_ctx_get failed (%s)",
                              uuid_utoa (inode->gfid));
                goto out;
        }

	if (!is_md_cache_xatt_valid (this, mdc)) {
                gf_msg_trace ("md-cache", 0, "xattr cache not valid for (%s)",
                              uuid_utoa (inode->gfid));
		goto out;
        }

        LOCK (&mdc->lock);
        {
                ret = 0;
		/* Missing xattr only means no keys were there, i.e
		   a negative cache for the "loaded" keys
		*/
                if (!mdc->xattr) {
                        gf_msg_trace ("md-cache", 0, "xattr not present (%s)",
                                      uuid_utoa (inode->gfid));
                        goto unlock;
                }

                if (dict)
                        *dict = dict_ref (mdc->xattr);
        }
unlock:
        UNLOCK (&mdc->lock);

out:
        return ret;
}

gf_boolean_t
mdc_inode_reset_need_lookup (xlator_t *this, inode_t *inode)
{
        struct md_cache *mdc  = NULL;
        gf_boolean_t     need = _gf_false;

        if (mdc_inode_ctx_get (this, inode, &mdc) != 0)
                goto out;

        LOCK (&mdc->lock);
        {
                need = mdc->need_lookup;
                mdc->need_lookup = _gf_false;
        }
        UNLOCK (&mdc->lock);

out:
        return need;
}

void
mdc_inode_set_need_lookup (xlator_t *this, inode_t *inode, gf_boolean_t need)
{
        struct md_cache *mdc = NULL;

        if (mdc_inode_ctx_get (this, inode, &mdc) != 0)
                goto out;

        LOCK (&mdc->lock);
        {
                mdc->need_lookup = need;
        }
        UNLOCK (&mdc->lock);

out:
        return;
}

void
mdc_inode_iatt_invalidate (xlator_t *this, inode_t *inode)
{
        struct md_cache *mdc = NULL;

        if (mdc_inode_ctx_get (this, inode, &mdc) != 0)
                goto out;

        LOCK (&mdc->lock);
        {
		mdc->ia_time = 0;
        }
        UNLOCK (&mdc->lock);

out:
        return;
}


int
mdc_inode_xatt_invalidate (xlator_t *this, inode_t *inode)
{
        int              ret = -1;
        struct md_cache *mdc = NULL;

        if (mdc_inode_ctx_get (this, inode, &mdc) != 0)
                goto out;

        LOCK (&mdc->lock);
        {
		mdc->xa_time = 0;
        }
        UNLOCK (&mdc->lock);

out:
        return ret;
}


static int
mdc_update_gfid_stat (xlator_t *this, struct iatt *iatt)
{
        int              ret        = 0;
        inode_table_t   *itable     = NULL;
        inode_t         *inode      = NULL;

        itable = ((xlator_t *)this->graph->top)->itable;
        inode = inode_find (itable, iatt->ia_gfid);
        if (!inode) {
                ret = -1;
                goto out;
        }
        ret = mdc_inode_iatt_set_validate (this, inode, NULL,
                                           iatt, _gf_true);
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


static char*
mdc_serialize_loaded_key_names (xlator_t *this)
{
        int             max_len = 0;
        int             len = 0;
        int             i = 0;
        char           *mdc_key_names = NULL;
        const char     *mdc_key = NULL;
        gf_boolean_t    at_least_one_key_loaded = _gf_false;

        for (mdc_key = mdc_keys[i].name; (mdc_key = mdc_keys[i].name); i++) {
                max_len += (strlen(mdc_keys[i].name) + 1);
                if (mdc_keys[i].load)
                        at_least_one_key_loaded = _gf_true;
        }

        if (!at_least_one_key_loaded)
                goto out;

        mdc_key_names = GF_CALLOC (1, max_len + 1, gf_common_mt_char);
        if (!mdc_key_names)
                goto out;

        i = 0;
        for (mdc_key = mdc_keys[i].name; (mdc_key = mdc_keys[i].name); i++) {
                if (!mdc_keys[i].load)
                        continue;
                strcat (mdc_key_names, mdc_keys[i].name);
                strcat (mdc_key_names, " ");
        }

        len = strlen (mdc_key_names);
        if (len > 0) {
                mdc_key_names[len - 1] = '\0';
        } else {
                GF_FREE (mdc_key_names);
                mdc_key_names = NULL;
        }

out:
        return mdc_key_names;
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
		if (!mdc_keys[i].load)
			continue;
		if (strcmp (mdc_key, key) == 0)
			return 1;
	}

        gf_msg_trace ("md-cache", 0, "xattr key %s doesn't satisfy "
                      "caching requirements", key);
	return 0;
}


static int
checkfn (dict_t *this, char *key, data_t *value, void *data)
{
        struct checkpair *pair = data;

	if (!is_mdc_key_satisfied (key))
		pair->ret = 0;

        return 0;
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
        struct mdc_conf *conf = this->private;

        local = frame->local;

        if (op_ret != 0) {
                if (op_errno == ENOENT)
                        INCREMENT_ATOMIC (conf->mdc_counter.lock,
                                          conf->mdc_counter.negative_lookup);
                goto out;
        }

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
            dict_t *xdata)
{
        int          ret = 0;
        struct iatt  stbuf = {0, };
        struct iatt  postparent = {0, };
        dict_t      *xattr_rsp = NULL;
        dict_t      *xattr_alloc = NULL;
        mdc_local_t *local = NULL;
        struct mdc_conf *conf = this->private;

        local = mdc_local_get (frame);
        if (!local) {
                INCREMENT_ATOMIC (conf->mdc_counter.lock, conf->mdc_counter.stat_miss);
                goto uncached;
        }

        loc_copy (&local->loc, loc);

	if (!loc->name) {
                INCREMENT_ATOMIC (conf->mdc_counter.lock,
                                  conf->mdc_counter.nameless_lookup);

                gf_msg_trace ("md-cache", 0, "Nameless lookup(%s) sent to the "
                              "brick", uuid_utoa (loc->inode->gfid));
		/* A nameless discovery is dangerous to serve from cache. We
		   perform nameless lookup with the intention of
		   re-establishing an inode "properly"
		*/
		goto uncached;
        }

        if (mdc_inode_reset_need_lookup (this, loc->inode)) {
                INCREMENT_ATOMIC (conf->mdc_counter.lock,
                                  conf->mdc_counter.need_lookup);
                goto uncached;
        }

        ret = mdc_inode_iatt_get (this, loc->inode, &stbuf);
        if (ret != 0) {
                INCREMENT_ATOMIC (conf->mdc_counter.lock,
                                  conf->mdc_counter.stat_miss);
                goto uncached;
        }

        if (xdata) {
                ret = mdc_inode_xatt_get (this, loc->inode, &xattr_rsp);
                if (ret != 0) {
                        INCREMENT_ATOMIC (conf->mdc_counter.lock,
                                          conf->mdc_counter.xattr_miss);
                        goto uncached;
                }

                if (!mdc_xattr_satisfied (this, xdata, xattr_rsp)) {
                        INCREMENT_ATOMIC (conf->mdc_counter.lock,
                                          conf->mdc_counter.xattr_miss);
                        goto uncached;
                }
        }

        INCREMENT_ATOMIC (conf->mdc_counter.lock, conf->mdc_counter.stat_hit);
        MDC_STACK_UNWIND (lookup, frame, 0, 0, loc->inode, &stbuf,
                          xattr_rsp, &postparent);

        if (xattr_rsp)
                dict_unref (xattr_rsp);

        return 0;

uncached:
	if (!xdata)
		xdata = xattr_alloc = dict_new ();
	if (xdata)
		mdc_load_reqs (this, xdata);

        STACK_WIND (frame, mdc_lookup_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->lookup, loc, xdata);

        if (xattr_rsp)
                dict_unref (xattr_rsp);
	if (xattr_alloc)
		dict_unref (xattr_alloc);
        return 0;
}


int
mdc_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, struct iatt *buf, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        if (op_ret != 0)
                goto out;

        local = frame->local;
        if (!local)
                goto out;

        mdc_inode_iatt_set (this, local->loc.inode, buf);

out:
        MDC_STACK_UNWIND (stat, frame, op_ret, op_errno, buf, xdata);

        return 0;
}


int
mdc_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        int           ret;
        struct iatt   stbuf;
        mdc_local_t  *local = NULL;
        struct mdc_conf *conf = this->private;

        local = mdc_local_get (frame);
        if (!local)
                goto uncached;

        loc_copy (&local->loc, loc);

        ret = mdc_inode_iatt_get (this, loc->inode, &stbuf);
        if (ret != 0)
                goto uncached;

        INCREMENT_ATOMIC (conf->mdc_counter.lock, conf->mdc_counter.stat_hit);
        MDC_STACK_UNWIND (stat, frame, 0, 0, &stbuf, xdata);

        return 0;

uncached:
        INCREMENT_ATOMIC (conf->mdc_counter.lock, conf->mdc_counter.stat_miss);
        STACK_WIND (frame, mdc_stat_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->stat,
                    loc, xdata);
        return 0;
}


int
mdc_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *buf,
               dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        if (op_ret != 0)
                goto out;

        local = frame->local;
        if (!local)
                goto out;

        mdc_inode_iatt_set (this, local->fd->inode, buf);

out:
        MDC_STACK_UNWIND (fstat, frame, op_ret, op_errno, buf, xdata);

        return 0;
}


int
mdc_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        int           ret;
        struct iatt   stbuf;
        mdc_local_t  *local = NULL;
        struct mdc_conf *conf = this->private;

        local = mdc_local_get (frame);
        if (!local)
                goto uncached;

        local->fd = fd_ref (fd);

        ret = mdc_inode_iatt_get (this, fd->inode, &stbuf);
        if (ret != 0)
                goto uncached;

        INCREMENT_ATOMIC (conf->mdc_counter.lock, conf->mdc_counter.stat_hit);
        MDC_STACK_UNWIND (fstat, frame, 0, 0, &stbuf, xdata);

        return 0;

uncached:
        INCREMENT_ATOMIC (conf->mdc_counter.lock, conf->mdc_counter.stat_miss);
        STACK_WIND (frame, mdc_fstat_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fstat,
                    fd, xdata);
        return 0;
}


int
mdc_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        mdc_inode_iatt_set_validate(this, local->loc.inode, prebuf, postbuf,
                                    _gf_true);

out:
        MDC_STACK_UNWIND (truncate, frame, op_ret, op_errno, prebuf, postbuf,
                          xdata);

        return 0;
}


int
mdc_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc,
              off_t offset, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        local->loc.inode = inode_ref (loc->inode);

        STACK_WIND (frame, mdc_truncate_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->truncate,
                    loc, offset, xdata);
        return 0;
}


int
mdc_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno,
                   struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        mdc_inode_iatt_set_validate(this, local->fd->inode, prebuf, postbuf,
                                    _gf_true);

out:
        MDC_STACK_UNWIND (ftruncate, frame, op_ret, op_errno, prebuf, postbuf,
                          xdata);

        return 0;
}


int
mdc_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd,
               off_t offset, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        local->fd = fd_ref (fd);

        STACK_WIND (frame, mdc_ftruncate_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->ftruncate,
                    fd, offset, xdata);
        return 0;
}


int
mdc_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode,
               struct iatt *buf, struct iatt *preparent,
               struct iatt *postparent, dict_t *xdata)
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
                          preparent, postparent, xdata);
        return 0;
}


int
mdc_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc,
           mode_t mode, dev_t rdev, mode_t umask, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        loc_copy (&local->loc, loc);
        local->xattr = dict_ref (xdata);

        STACK_WIND (frame, mdc_mknod_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->mknod,
                    loc, mode, rdev, umask, xdata);
        return 0;
}


int
mdc_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode,
               struct iatt *buf, struct iatt *preparent,
               struct iatt *postparent, dict_t *xdata)
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
                          preparent, postparent, xdata);
        return 0;
}


int
mdc_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc,
           mode_t mode, mode_t umask, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        loc_copy (&local->loc, loc);
        local->xattr = dict_ref (xdata);

        STACK_WIND (frame, mdc_mkdir_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->mkdir,
                    loc, mode, umask, xdata);
        return 0;
}


int
mdc_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno,
                struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
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
                          preparent, postparent, xdata);
        return 0;
}


int
mdc_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t xflag,
            dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        loc_copy (&local->loc, loc);

        STACK_WIND (frame, mdc_unlink_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->unlink,
                    loc, xflag, xdata);
        return 0;
}


int
mdc_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno,
                struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
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
                          preparent, postparent, xdata);
        return 0;
}


int
mdc_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flag,
           dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        loc_copy (&local->loc, loc);

        STACK_WIND (frame, mdc_rmdir_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->rmdir,
                    loc, flag, xdata);
        return 0;
}


int
mdc_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, inode_t *inode,
                 struct iatt *buf, struct iatt *preparent,
                 struct iatt *postparent, dict_t *xdata)
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
                          preparent, postparent, xdata);
        return 0;
}


int
mdc_symlink (call_frame_t *frame, xlator_t *this, const char *linkname,
             loc_t *loc, mode_t umask, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        loc_copy (&local->loc, loc);

        local->linkname = gf_strdup (linkname);

        STACK_WIND (frame, mdc_symlink_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->symlink,
                    linkname, loc, umask, xdata);
        return 0;
}


int
mdc_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *buf,
                struct iatt *preoldparent, struct iatt *postoldparent,
                struct iatt *prenewparent, struct iatt *postnewparent,
                dict_t *xdata)
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
                          preoldparent, postoldparent, prenewparent,
                          postnewparent, xdata);
        return 0;
}


int
mdc_rename (call_frame_t *frame, xlator_t *this,
            loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        loc_copy (&local->loc, oldloc);
        loc_copy (&local->loc2, newloc);

        STACK_WIND (frame, mdc_rename_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->rename,
                    oldloc, newloc, xdata);
        return 0;
}


int
mdc_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, inode_t *inode, struct iatt *buf,
              struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
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
                          preparent, postparent, xdata);
        return 0;
}


int
mdc_link (call_frame_t *frame, xlator_t *this,
          loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        loc_copy (&local->loc, oldloc);
        loc_copy (&local->loc2, newloc);

        STACK_WIND (frame, mdc_link_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->link,
                    oldloc, newloc, xdata);
        return 0;
}


int
mdc_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
                struct iatt *buf, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
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
                          preparent, postparent, xdata);
        return 0;
}


int
mdc_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
            mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        loc_copy (&local->loc, loc);
        local->xattr = dict_ref (xdata);

        STACK_WIND (frame, mdc_create_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->create,
                    loc, flags, mode, umask, fd, xdata);
        return 0;
}


static int
mdc_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, fd_t *fd,
                dict_t *xdata)
{
        mdc_local_t *local = NULL;

        local = frame->local;

        if (op_ret || !local)
                goto out;

        if (local->fd->flags & O_TRUNC) {
                /* O_TRUNC modifies file size. Hence invalidate the
                 * cache entry to fetch latest attributes. */
                mdc_inode_iatt_invalidate (this, local->fd->inode);
        }

out:
        MDC_STACK_UNWIND (open, frame, op_ret, op_errno, fd, xdata);
        return 0;
}


static int
mdc_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
          fd_t *fd, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        if (!fd || !IA_ISREG(fd->inode->ia_type) ||
            !(fd->flags & O_TRUNC)) {
                goto out;
        }

        local = mdc_local_get (frame);

        local->fd = fd_ref (fd);

out:
        STACK_WIND (frame, mdc_open_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->open,
                    loc, flags, fd, xdata);
        return 0;
}


int
mdc_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno,
               struct iovec *vector, int32_t count,
               struct iatt *stbuf, struct iobref *iobref, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = frame->local;

        if (op_ret < 0)
                goto out;

        if (!local)
                goto out;

        mdc_inode_iatt_set (this, local->fd->inode, stbuf);

out:
        MDC_STACK_UNWIND (readv, frame, op_ret, op_errno, vector, count,
                          stbuf, iobref, xdata);

        return 0;
}


int
mdc_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
           off_t offset, uint32_t flags, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        local->fd = fd_ref (fd);

        STACK_WIND (frame, mdc_readv_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->readv,
                    fd, size, offset, flags, xdata);
        return 0;
}


int
mdc_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno,
               struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = frame->local;

        if (op_ret == -1)
                goto out;

        if (!local)
                goto out;

        mdc_inode_iatt_set_validate(this, local->fd->inode, prebuf, postbuf,
                                    _gf_true);

out:
        MDC_STACK_UNWIND (writev, frame, op_ret, op_errno, prebuf, postbuf,
                          xdata);

        return 0;
}


int
mdc_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
            int count, off_t offset, uint32_t flags, struct iobref *iobref,
            dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        local->fd = fd_ref (fd);

        STACK_WIND (frame, mdc_writev_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->writev,
                    fd, vector, count, offset, flags, iobref, xdata);
        return 0;
}


int
mdc_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = frame->local;

        if (op_ret != 0) {
		mdc_inode_iatt_set (this, local->loc.inode, NULL);
                goto out;
	}

        if (!local)
                goto out;

	mdc_inode_iatt_set_validate(this, local->loc.inode, prebuf, postbuf,
                                    _gf_true);

out:
        MDC_STACK_UNWIND (setattr, frame, op_ret, op_errno, prebuf, postbuf,
                          xdata);

        return 0;
}


int
mdc_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
             struct iatt *stbuf, int valid, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        loc_copy (&local->loc, loc);

        STACK_WIND (frame, mdc_setattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->setattr,
                    loc, stbuf, valid, xdata);
        return 0;
}


int
mdc_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        mdc_inode_iatt_set_validate(this, local->fd->inode, prebuf, postbuf,
                                    _gf_true);

out:
        MDC_STACK_UNWIND (fsetattr, frame, op_ret, op_errno, prebuf, postbuf,
                          xdata);

        return 0;
}


int
mdc_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
              struct iatt *stbuf, int valid, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        local->fd = fd_ref (fd);

        STACK_WIND (frame, mdc_fsetattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fsetattr,
                    fd, stbuf, valid, xdata);
        return 0;
}


int
mdc_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno,
               struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        mdc_inode_iatt_set_validate(this, local->fd->inode, prebuf, postbuf,
                                     _gf_true);

out:
        MDC_STACK_UNWIND (fsync, frame, op_ret, op_errno, prebuf, postbuf,
                          xdata);

        return 0;
}


int
mdc_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int datasync,
           dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        local->fd = fd_ref (fd);

        STACK_WIND (frame, mdc_fsync_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fsync,
                    fd, datasync, xdata);
        return 0;
}


int
mdc_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        mdc_inode_xatt_update (this, local->loc.inode, local->xattr);

	mdc_inode_iatt_invalidate (this, local->loc.inode);

out:
        MDC_STACK_UNWIND (setxattr, frame, op_ret, op_errno, xdata);

        return 0;
}


int
mdc_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
              dict_t *xattr, int flags, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

        loc_copy (&local->loc, loc);
        local->xattr = dict_ref (xattr);

        STACK_WIND (frame, mdc_setxattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->setxattr,
                    loc, xattr, flags, xdata);
        return 0;
}


int
mdc_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		   int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        mdc_inode_xatt_update (this, local->fd->inode, local->xattr);

	mdc_inode_iatt_invalidate (this, local->fd->inode);
out:
        MDC_STACK_UNWIND (fsetxattr, frame, op_ret, op_errno, xdata);

        return 0;
}


int
mdc_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
	       dict_t *xattr, int flags, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = mdc_local_get (frame);

	local->fd = fd_ref (fd);
        local->xattr = dict_ref (xattr);

        STACK_WIND (frame, mdc_fsetxattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fsetxattr,
                    fd, xattr, flags, xdata);
        return 0;
}

int
mdc_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int32_t op_ret, int32_t op_errno, dict_t *xattr,
                  dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        if (op_ret < 0)
                goto out;

        local = frame->local;
        if (!local)
                goto out;

        mdc_inode_xatt_update (this, local->loc.inode, xattr);

out:
        MDC_STACK_UNWIND (getxattr, frame, op_ret, op_errno, xattr, xdata);

        return 0;
}


int
mdc_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, const char *key,
              dict_t *xdata)
{
        int           ret;
	int           op_errno = ENODATA;
        mdc_local_t  *local = NULL;
	dict_t       *xattr = NULL;
        struct mdc_conf *conf = this->private;

        local = mdc_local_get (frame);
        if (!local)
                goto uncached;

        loc_copy (&local->loc, loc);

	if (!is_mdc_key_satisfied (key))
		goto uncached;

	ret = mdc_inode_xatt_get (this, loc->inode, &xattr);
	if (ret != 0)
		goto uncached;

	if (!xattr || !dict_get (xattr, (char *)key)) {
		ret = -1;
		op_errno = ENODATA;
	}

        INCREMENT_ATOMIC (conf->mdc_counter.lock, conf->mdc_counter.xattr_hit);
        MDC_STACK_UNWIND (getxattr, frame, ret, op_errno, xattr, xdata);

        return 0;

uncached:
        INCREMENT_ATOMIC (conf->mdc_counter.lock, conf->mdc_counter.xattr_miss);
        STACK_WIND (frame, mdc_getxattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->getxattr,
                    loc, key, xdata);
        return 0;
}


int
mdc_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		   int32_t op_ret, int32_t op_errno, dict_t *xattr,
                   dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        if (op_ret < 0)
                goto out;

        local = frame->local;
        if (!local)
                goto out;

        mdc_inode_xatt_update (this, local->fd->inode, xattr);

out:
        MDC_STACK_UNWIND (fgetxattr, frame, op_ret, op_errno, xattr, xdata);

        return 0;
}


int
mdc_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, const char *key,
               dict_t *xdata)
{
        int           ret;
        mdc_local_t  *local = NULL;
	dict_t       *xattr = NULL;
	int           op_errno = ENODATA;
        struct mdc_conf *conf = this->private;

        local = mdc_local_get (frame);
        if (!local)
                goto uncached;

        local->fd = fd_ref (fd);

	if (!is_mdc_key_satisfied (key))
		goto uncached;

	ret = mdc_inode_xatt_get (this, fd->inode, &xattr);
	if (ret != 0)
		goto uncached;

	if (!xattr || !dict_get (xattr, (char *)key)) {
		ret = -1;
		op_errno = ENODATA;
	}

        INCREMENT_ATOMIC (conf->mdc_counter.lock, conf->mdc_counter.xattr_hit);
        MDC_STACK_UNWIND (fgetxattr, frame, ret, op_errno, xattr, xdata);

        return 0;

uncached:
        INCREMENT_ATOMIC (conf->mdc_counter.lock, conf->mdc_counter.xattr_miss);
        STACK_WIND (frame, mdc_fgetxattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fgetxattr,
                    fd, key, xdata);
        return 0;
}

int
mdc_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

	if (local->key)
		mdc_inode_xatt_unset (this, local->loc.inode, local->key);
	else
		mdc_inode_xatt_invalidate (this, local->loc.inode);

	mdc_inode_iatt_invalidate (this, local->loc.inode);
out:
        MDC_STACK_UNWIND (removexattr, frame, op_ret, op_errno, xdata);

        return 0;
}


int
mdc_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
		 const char *name, dict_t *xdata)
{
        mdc_local_t  *local = NULL;
        int           op_errno = ENODATA;
        int           ret = 0;
        dict_t       *xattr = NULL;
        struct mdc_conf *conf = this->private;

        local = mdc_local_get (frame);

	loc_copy (&local->loc, loc);

	local->key = gf_strdup (name);

        if (!is_mdc_key_satisfied (name))
                goto uncached;

        ret = mdc_inode_xatt_get (this, loc->inode, &xattr);
        if (ret != 0)
                goto uncached;

        if (!xattr || !dict_get (xattr, (char *)name)) {
                ret = -1;
                op_errno = ENODATA;
        }

        INCREMENT_ATOMIC (conf->mdc_counter.lock, conf->mdc_counter.xattr_hit);
        MDC_STACK_UNWIND (removexattr, frame, ret, op_errno, xdata);

        return 0;

uncached:
        INCREMENT_ATOMIC (conf->mdc_counter.lock, conf->mdc_counter.xattr_miss);
        STACK_WIND (frame, mdc_removexattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->removexattr,
                    loc, name, xdata);
        return 0;
}


int
mdc_fremovexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

	if (local->key)
		mdc_inode_xatt_unset (this, local->fd->inode, local->key);
	else
		mdc_inode_xatt_invalidate (this, local->fd->inode);

	mdc_inode_iatt_invalidate (this, local->fd->inode);
out:
        MDC_STACK_UNWIND (fremovexattr, frame, op_ret, op_errno, xdata);

        return 0;
}


int
mdc_fremovexattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
		  const char *name, dict_t *xdata)
{
        mdc_local_t  *local = NULL;
        int           op_errno = ENODATA;
        int           ret = 0;
        dict_t       *xattr = NULL;
        struct mdc_conf *conf = this->private;

        local = mdc_local_get (frame);

	local->fd = fd_ref (fd);

	local->key = gf_strdup (name);

        if (!is_mdc_key_satisfied (name))
                goto uncached;

        ret = mdc_inode_xatt_get (this, fd->inode, &xattr);
        if (ret != 0)
                goto uncached;

        if (!xattr || !dict_get (xattr, (char *)name)) {
                ret = -1;
                op_errno = ENODATA;
        }

        INCREMENT_ATOMIC (conf->mdc_counter.lock, conf->mdc_counter.xattr_hit);
        MDC_STACK_UNWIND (fremovexattr, frame, ret, op_errno, xdata);
        return 0;

uncached:
        INCREMENT_ATOMIC (conf->mdc_counter.lock, conf->mdc_counter.xattr_miss);
        STACK_WIND (frame, mdc_fremovexattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fremovexattr,
                    fd, name, xdata);
        return 0;
}


int
mdc_opendir(call_frame_t *frame, xlator_t *this, loc_t *loc,
            fd_t *fd, dict_t *xdata)
{
        int          ret = -1;
        char        *mdc_key_names = NULL;
        dict_t      *xattr_alloc = NULL;

        if (!xdata)
                xdata = xattr_alloc = dict_new ();

        if (xdata) {
                /* Tell readdir-ahead to include these keys in xdata when it
                 * internally issues readdirp() in it's opendir_cbk */
                mdc_key_names = mdc_serialize_loaded_key_names(this);
                if (!mdc_key_names)
                        goto wind;
                ret = dict_set_dynstr (xdata, GF_MDC_LOADED_KEY_NAMES,
                                       mdc_key_names);
                if (ret)
                        goto wind;
        }

wind:
        STACK_WIND (frame, default_opendir_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->opendir, loc, fd, xdata);

        if (xattr_alloc)
                dict_unref (xattr_alloc);

        return 0;
}


int
mdc_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int op_ret, int op_errno, gf_dirent_t *entries, dict_t *xdata)
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
	STACK_UNWIND_STRICT (readdirp, frame, op_ret, op_errno, entries, xdata);
	return 0;
}


int
mdc_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd,
	      size_t size, off_t offset, dict_t *xdata)
{
	dict_t *xattr_alloc = NULL;

	if (!xdata)
		xdata = xattr_alloc = dict_new ();
	if (xdata)
		mdc_load_reqs (this, xdata);

	STACK_WIND (frame, mdc_readdirp_cbk,
		    FIRST_CHILD (this), FIRST_CHILD (this)->fops->readdirp,
		    fd, size, offset, xdata);
	if (xattr_alloc)
		dict_unref (xattr_alloc);
	return 0;
}

int
mdc_readdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
		int op_errno, gf_dirent_t *entries, dict_t *xdata)
{
	STACK_UNWIND_STRICT (readdir, frame, op_ret, op_errno, entries, xdata);
	return 0;
}

int
mdc_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd,
	     size_t size, off_t offset, dict_t *xdata)
{
        int need_unref = 0;
	struct mdc_conf *conf = this->private;

	if (!conf->force_readdirp) {
		STACK_WIND(frame, mdc_readdir_cbk, FIRST_CHILD(this),
			   FIRST_CHILD(this)->fops->readdir, fd, size, offset,
			   xdata);
		return 0;
	}

	if (!xdata) {
                xdata = dict_new ();
                need_unref = 1;
        }

        if (xdata)
		mdc_load_reqs (this, xdata);

	STACK_WIND(frame, mdc_readdirp_cbk, FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->readdirp, fd, size, offset,
		   xdata);

        if (need_unref && xdata)
                dict_unref (xdata);

	return 0;
}

int
mdc_fallocate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        mdc_inode_iatt_set_validate(this, local->fd->inode, prebuf, postbuf,
                                    _gf_true);

out:
        MDC_STACK_UNWIND (fallocate, frame, op_ret, op_errno, prebuf, postbuf,
                          xdata);

        return 0;
}

int mdc_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t mode,
		  off_t offset, size_t len, dict_t *xdata)
{
	mdc_local_t *local;

	local = mdc_local_get(frame);
	local->fd = fd_ref(fd);

	STACK_WIND(frame, mdc_fallocate_cbk, FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->fallocate, fd, mode, offset, len,
		   xdata);

	return 0;
}

int
mdc_discard_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno,
                struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        mdc_inode_iatt_set_validate(this, local->fd->inode, prebuf, postbuf,
                                    _gf_true);

out:
        MDC_STACK_UNWIND(discard, frame, op_ret, op_errno, prebuf, postbuf,
                         xdata);

        return 0;
}

int mdc_discard(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
		size_t len, dict_t *xdata)
{
	mdc_local_t *local;

	local = mdc_local_get(frame);
	local->fd = fd_ref(fd);

	STACK_WIND(frame, mdc_discard_cbk, FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->discard, fd, offset, len,
		   xdata);

	return 0;
}

int
mdc_zerofill_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno,
                struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        mdc_local_t  *local = NULL;

        local = frame->local;

        if (op_ret != 0)
                goto out;

        if (!local)
                goto out;

        mdc_inode_iatt_set_validate(this, local->fd->inode, prebuf, postbuf,
                                    _gf_true);

out:
        MDC_STACK_UNWIND(zerofill, frame, op_ret, op_errno, prebuf, postbuf,
                         xdata);

        return 0;
}

int mdc_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                off_t len, dict_t *xdata)
{
        mdc_local_t *local;

        local = mdc_local_get(frame);
        local->fd = fd_ref(fd);

        STACK_WIND(frame, mdc_zerofill_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->zerofill, fd, offset, len,
                   xdata);

        return 0;
}


int
mdc_priv_dump (xlator_t *this)
{
        struct mdc_conf *conf = NULL;
        char  key_prefix[GF_DUMP_MAX_BUF_LEN];

        conf = this->private;

        snprintf(key_prefix, GF_DUMP_MAX_BUF_LEN, "%s.%s", this->type, this->name);
        gf_proc_dump_add_section(key_prefix);

        gf_proc_dump_write("stat_hit_count", "%"PRId64,
                           conf->mdc_counter.stat_hit);
        gf_proc_dump_write("stat_miss_count", "%"PRId64,
                           conf->mdc_counter.stat_miss);
        gf_proc_dump_write("xattr_hit_count", "%"PRId64,
                           conf->mdc_counter.xattr_hit);
        gf_proc_dump_write("xattr_miss_count", "%"PRId64,
                           conf->mdc_counter.xattr_miss);
        gf_proc_dump_write("nameless_lookup_count", "%"PRId64,
                           conf->mdc_counter.nameless_lookup);
        gf_proc_dump_write("negative_lookup_count", "%"PRId64,
                           conf->mdc_counter.negative_lookup);
        gf_proc_dump_write("stat_invalidations_recieved", "%"PRId64,
                           conf->mdc_counter.stat_invals);
        gf_proc_dump_write("xattr_invalidations_recieved", "%"PRId64,
                           conf->mdc_counter.xattr_invals);

        return 0;
}


int
mdc_forget (xlator_t *this, inode_t *inode)
{
        mdc_inode_wipe (this, inode);

        return 0;
}


int
is_strpfx (const char *str1, const char *str2)
{
	/* is one of the string a prefix of the other? */
	int i;

	for (i = 0; str1[i] == str2[i]; i++) {
		if (!str1[i] || !str2[i])
			break;
	}

	return !(str1[i] && str2[i]);
}


static int
mdc_key_unload_all (struct mdc_key *keys)
{
        struct mdc_key *key = NULL;

        for (key = keys; key->name; key++) {
                key->load = 0;
        }

        return 0;
}

int
mdc_key_load_set (struct mdc_key *keys, char *pattern, gf_boolean_t val)
{
	struct mdc_key *key = NULL;

	for (key = keys; key->name; key++) {
		if (is_strpfx (key->name, pattern))
			key->load = val;
	}

	return 0;
}

struct set {
       inode_t *inode;
       xlator_t *this;
};

static int
mdc_inval_xatt (dict_t *d, char *k, data_t *v, void *tmp)
{
        struct set *tmp1 = NULL;
        int         ret  = 0;

        tmp1 = (struct set *)tmp;
        ret = mdc_inode_xatt_unset (tmp1->this, tmp1->inode, k);
        return ret;
}

static int
mdc_invalidate (xlator_t *this, void *data)
{
        struct gf_upcall                    *up_data    = NULL;
        struct gf_upcall_cache_invalidation *up_ci      = NULL;
        inode_t                             *inode      = NULL;
        int                                  ret        = 0;
        struct set                           tmp        = {0, };
        inode_table_t                       *itable     = NULL;
        struct mdc_conf                     *conf       = this->private;

        up_data = (struct gf_upcall *)data;

        if (up_data->event_type != GF_UPCALL_CACHE_INVALIDATION)
                goto out;

        up_ci = (struct gf_upcall_cache_invalidation *)up_data->data;

        itable = ((xlator_t *)this->graph->top)->itable;
        inode = inode_find (itable, up_data->gfid);
        if (!inode) {
                ret = -1;
                goto out;
        }

        if (up_ci->flags & UP_PARENT_DENTRY_FLAGS) {
                mdc_update_gfid_stat (this, &up_ci->p_stat);
                        if (up_ci->flags & UP_RENAME_FLAGS)
                                mdc_update_gfid_stat (this, &up_ci->oldp_stat);
        }

        if (up_ci->flags & UP_EXPLICIT_LOOKUP) {
                mdc_inode_set_need_lookup (this, inode, _gf_true);
                goto out;
        }

        if (up_ci->flags &
            (UP_NLINK | UP_RENAME_FLAGS | UP_FORGET | UP_INVAL_ATTR)) {
                mdc_inode_iatt_invalidate (this, inode);
                mdc_inode_xatt_invalidate (this, inode);
                INCREMENT_ATOMIC (conf->mdc_counter.lock,
                                  conf->mdc_counter.stat_invals);
                goto out;
        }

        if (up_ci->flags & IATT_UPDATE_FLAGS) {
                ret = mdc_inode_iatt_set_validate (this, inode, NULL,
                                                   &up_ci->stat, _gf_false);
                /* one of the scenarios where ret < 0 is when this invalidate
                 * is older than the current stat, in that case do not
                 * update the xattrs as well
                 */
                if (ret < 0)
                        goto out;
                INCREMENT_ATOMIC (conf->mdc_counter.lock,
                                  conf->mdc_counter.stat_invals);
        }

        if (up_ci->flags & UP_XATTR) {
                if (up_ci->dict)
                        ret = mdc_inode_xatt_update (this, inode, up_ci->dict);
                else
                        ret = mdc_inode_xatt_invalidate (this, inode);

                INCREMENT_ATOMIC (conf->mdc_counter.lock,
                                  conf->mdc_counter.xattr_invals);
        } else if (up_ci->flags & UP_XATTR_RM) {
                tmp.inode = inode;
                tmp.this = this;
                ret = dict_foreach (up_ci->dict, mdc_inval_xatt, &tmp);

                INCREMENT_ATOMIC (conf->mdc_counter.lock,
                                  conf->mdc_counter.xattr_invals);
        }

out:
        if (inode)
                inode_unref (inode);

        return ret;
}

struct mdc_ipc {
       xlator_t *this;
       dict_t   *xattr;
};

static int
mdc_send_xattrs_cbk (int ret, call_frame_t *frame, void *data)
{
        struct mdc_ipc *tmp = data;

        if (ret < 0) {
                mdc_key_unload_all (mdc_keys);
                gf_msg ("md-cache", GF_LOG_INFO, 0, MD_CACHE_MSG_NO_XATTR_CACHE,
                        "Disabled cache for all xattrs, as registering for "
                        "xattr cache invalidation failed");
        }
        STACK_DESTROY (frame->root);
        dict_unref (tmp->xattr);
        GF_FREE (tmp);

        return 0;
}

static int
mdc_send_xattrs (void *data)
{
        int             ret = 0;
        struct mdc_ipc *tmp = data;

        ret = syncop_ipc (FIRST_CHILD (tmp->this), GF_IPC_TARGET_UPCALL,
                          tmp->xattr, NULL);
        DECODE_SYNCOP_ERR (ret);
        if (ret < 0) {
                gf_msg (tmp->this->name, GF_LOG_WARNING, errno,
                        MD_CACHE_MSG_IPC_UPCALL_FAILED, "Registering the list "
                        "of xattrs that needs invalidaton, with upcall, failed");
        }

        return ret;
}


static int
mdc_register_xattr_inval (xlator_t *this)
{
        dict_t          *xattr = NULL;
        int              ret   = 0;
        struct mdc_conf *conf  = NULL;
        call_frame_t    *frame = NULL;
        struct mdc_ipc  *data  = NULL;

        conf = this->private;

        LOCK (&conf->lock);
        {
                if (!conf->mdc_invalidation) {
                        UNLOCK (&conf->lock);
                        goto out;
                }
        }
        UNLOCK (&conf->lock);

        xattr = dict_new ();
        if (!xattr) {
                gf_msg (this->name, GF_LOG_WARNING, ENOMEM,
                        MD_CACHE_MSG_NO_MEMORY, "dict_new failed");
                ret = -1;
                goto out;
        }

        mdc_load_reqs (this, xattr);

        frame = create_frame (this, this->ctx->pool);
        if (!frame) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        MD_CACHE_MSG_NO_MEMORY,
                        "failed to create the frame");
                ret = -1;
                goto out;
        }

        data = GF_CALLOC (1, sizeof (struct mdc_ipc), gf_mdc_mt_mdc_ipc);
        if (!data) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        MD_CACHE_MSG_NO_MEMORY,
                        "failed to allocate memory");
                ret = -1;
                goto out;
        }

        data->this = this;
        data->xattr = xattr;
        ret = synctask_new (this->ctx->env, mdc_send_xattrs, mdc_send_xattrs_cbk,
                            frame, data);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        MD_CACHE_MSG_IPC_UPCALL_FAILED, "Registering the list "
                        "of xattrs that needs invalidaton, with upcall, failed");
        }

out:
        if (ret < 0) {
                mdc_key_unload_all (mdc_keys);
                if (xattr)
                        dict_unref (xattr);
                if (frame)
                        STACK_DESTROY (frame->root);
                GF_FREE (data);
                gf_msg (this->name, GF_LOG_INFO, 0, MD_CACHE_MSG_NO_XATTR_CACHE,
                        "Disabled cache for all xattrs, as registering for "
                        "xattr cache invalidation failed");
        }

        return ret;
}


int
reconfigure (xlator_t *this, dict_t *options)
{
	struct mdc_conf *conf = NULL;
        int    timeout = 0;

	conf = this->private;

	GF_OPTION_RECONF ("md-cache-timeout", timeout, options, int32, out);

	GF_OPTION_RECONF ("cache-selinux", conf->cache_selinux, options, bool, out);
	mdc_key_load_set (mdc_keys, "security.selinux", conf->cache_selinux);

        GF_OPTION_RECONF ("cache-capability-xattrs", conf->cache_capability,
                          options, bool, out);
        mdc_key_load_set (mdc_keys, "security.capability",
                          conf->cache_capability);

        GF_OPTION_RECONF ("cache-ima-xattrs", conf->cache_ima, options, bool,
                          out);
        mdc_key_load_set (mdc_keys, "security.ima", conf->cache_ima);

	GF_OPTION_RECONF ("cache-posix-acl", conf->cache_posix_acl, options, bool, out);
	mdc_key_load_set (mdc_keys, "system.posix_acl_", conf->cache_posix_acl);
	mdc_key_load_set (mdc_keys, "glusterfs.posix_acl.", conf->cache_posix_acl);

        GF_OPTION_RECONF ("cache-swift-metadata", conf->cache_swift_metadata,
                          options, bool, out);
        mdc_key_load_set (mdc_keys, "user.swift.metadata",
                          conf->cache_swift_metadata);

        GF_OPTION_RECONF ("cache-samba-metadata", conf->cache_samba_metadata,
                          options, bool, out);
        mdc_key_load_set (mdc_keys, "user.DOSATTRIB",
                          conf->cache_samba_metadata);
        mdc_key_load_set (mdc_keys, "security.NTACL",
                          conf->cache_samba_metadata);

	GF_OPTION_RECONF("force-readdirp", conf->force_readdirp, options, bool, out);
        GF_OPTION_RECONF("cache-invalidation", conf->mdc_invalidation, options,
                         bool, out);

        /* If timeout is greater than 60s (default before the patch that added
         * cache invalidation support was added) then, cache invalidation
         * feature for md-cache needs to be enabled, if not set timeout to the
         * previous max which is 60s
         */
        if ((timeout > 60) && (!conf->mdc_invalidation)) {
                        conf->timeout = 60;
                        goto out;
        }
        conf->timeout = timeout;

        (void) mdc_register_xattr_inval (this);
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
        int    timeout = 0;

	conf = GF_CALLOC (sizeof (*conf), 1, gf_mdc_mt_mdc_conf_t);
	if (!conf) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        MD_CACHE_MSG_NO_MEMORY, "out of memory");
		return -1;
	}

        GF_OPTION_INIT ("md-cache-timeout", timeout, int32, out);

	GF_OPTION_INIT ("cache-selinux", conf->cache_selinux, bool, out);
	mdc_key_load_set (mdc_keys, "security.selinux", conf->cache_selinux);

        GF_OPTION_INIT ("cache-capability-xattrs", conf->cache_capability,
                        bool, out);
        mdc_key_load_set (mdc_keys, "security.capability",
                          conf->cache_capability);

        GF_OPTION_INIT ("cache-ima-xattrs", conf->cache_ima, bool, out);
        mdc_key_load_set (mdc_keys, "security.ima", conf->cache_ima);

	GF_OPTION_INIT ("cache-posix-acl", conf->cache_posix_acl, bool, out);
	mdc_key_load_set (mdc_keys, "system.posix_acl_", conf->cache_posix_acl);
	mdc_key_load_set (mdc_keys, "glusterfs.posix_acl.", conf->cache_posix_acl);

        GF_OPTION_INIT ("cache-swift-metadata",
                        conf->cache_swift_metadata, bool, out);
        mdc_key_load_set (mdc_keys, "user.swift.metadata",
                          conf->cache_swift_metadata);

        GF_OPTION_INIT ("cache-samba-metadata", conf->cache_samba_metadata,
                        bool, out);
        mdc_key_load_set (mdc_keys, "user.DOSATTRIB",
                          conf->cache_samba_metadata);
        mdc_key_load_set (mdc_keys, "security.NTACL",
                          conf->cache_samba_metadata);

	GF_OPTION_INIT("force-readdirp", conf->force_readdirp, bool, out);
        GF_OPTION_INIT("cache-invalidation", conf->mdc_invalidation, bool, out);

        LOCK_INIT (&conf->lock);
        LOCK_INIT (&conf->mdc_counter.lock);
        time (&conf->last_child_down);

        /* If timeout is greater than 60s (default before the patch that added
         * cache invalidation support was added) then, cache invalidation
         * feature for md-cache needs to be enabled, if not set timeout to the
         * previous max which is 60s
         */
        if ((timeout > 60) && (!conf->mdc_invalidation)) {
                        conf->timeout = 60;
                        goto out;
        }
        conf->timeout = timeout;

out:
	this->private = conf;

        return 0;
}


void
mdc_update_child_down_time (xlator_t *this, time_t *now)
{
        struct mdc_conf *conf = NULL;

        conf = this->private;

        LOCK (&conf->lock);
        {
                conf->last_child_down = *now;
        }
        UNLOCK (&conf->lock);
}


int
notify (xlator_t *this, int event, void *data, ...)
{
        int ret = 0;
        struct mdc_conf *conf = NULL;
        time_t           now = 0;

        conf = this->private;
        switch (event) {
        case GF_EVENT_CHILD_DOWN:
        case GF_EVENT_SOME_DESCENDENT_DOWN:
                time (&now);
                mdc_update_child_down_time (this, &now);
                break;
        case GF_EVENT_UPCALL:
                if (conf->mdc_invalidation)
                        ret = mdc_invalidate (this, data);
                break;
        case GF_EVENT_CHILD_UP:
        case GF_EVENT_SOME_DESCENDENT_UP:
                ret = mdc_register_xattr_inval (this);
                break;
        default:
                break;
        }

        if (default_notify (this, event, data) != 0)
                ret = -1;

        return ret;
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
        .open        = mdc_open,
        .readv       = mdc_readv,
        .writev      = mdc_writev,
        .setattr     = mdc_setattr,
        .fsetattr    = mdc_fsetattr,
        .fsync       = mdc_fsync,
        .setxattr    = mdc_setxattr,
        .fsetxattr   = mdc_fsetxattr,
        .getxattr    = mdc_getxattr,
        .fgetxattr   = mdc_fgetxattr,
	.removexattr = mdc_removexattr,
	.fremovexattr= mdc_fremovexattr,
        .opendir     = mdc_opendir,
	.readdirp    = mdc_readdirp,
	.readdir     = mdc_readdir,
	.fallocate   = mdc_fallocate,
	.discard     = mdc_discard,
        .zerofill    = mdc_zerofill,
};


struct xlator_cbks cbks = {
        .forget      = mdc_forget,
};


struct xlator_dumpops dumpops = {
        .priv       = mdc_priv_dump,
};


struct volume_options options[] = {
	{ .key = {"cache-selinux"},
	  .type = GF_OPTION_TYPE_BOOL,
	  .default_value = "false",
	},
        { .key = {"cache-capability-xattrs"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "true",
        },
        { .key = {"cache-ima-xattrs"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "true",
        },
        { .key = {"cache-swift-metadata"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "true",
          .description = "Cache swift metadata (user.swift.metadata xattr)",
        },
        { .key = {"cache-samba-metadata"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "false",
          .description = "Cache samba metadata (user.DOSATTRIB, security.NTACL"
                         " xattrs)",
        },
	{ .key = {"cache-posix-acl"},
	  .type = GF_OPTION_TYPE_BOOL,
	  .default_value = "false",
	},
        { .key = {"md-cache-timeout"},
          .type = GF_OPTION_TYPE_INT,
          .min = 0,
          .max = 600,
          .default_value = "1",
          .description = "Time period after which cache has to be refreshed",
        },
	{ .key = {"force-readdirp"},
	  .type = GF_OPTION_TYPE_BOOL,
	  .default_value = "true",
	  .description = "Convert all readdir requests to readdirplus to "
			 "collect stat info on each entry.",
	},
        { .key = {"cache-invalidation"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "false",
          .description = "When \"on\", invalidates/updates the metadata cache,"
                         " on receiving the cache-invalidation notifications",
        },
    { .key = {NULL} },
};
