/*
  Copyright (c) 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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
#include "list.h"
#include "compat.h"
#include "compat-errno.h"
#include "common-utils.h"

struct symlink_cache {
	time_t ctime;
	char   *readlink;
};


static int
symlink_inode_ctx_get (inode_t *inode, xlator_t *this, void **ctx)
{
	int ret = 0;
	ret = inode_ctx_get (inode, this, (uint64_t *)ctx);
	if (-1 == ret)
		gf_log (this->name, GF_LOG_ERROR, "dict get failed");

	return 0;
}


static int
symlink_inode_ctx_set (inode_t *inode, xlator_t *this, void *ctx)
{
	int ret = 0;
	ret = inode_ctx_put (inode, this, (uint64_t)(long) ctx);
	if (-1 == ret)
		gf_log (this->name, GF_LOG_ERROR, "dict set failed");

	return 0;
}


int
sc_cache_update (xlator_t *this, inode_t *inode, const char *link)
{
	struct symlink_cache *sc = NULL;

	symlink_inode_ctx_get (inode, this, VOID(&sc));
	if (!sc)
		return 0;

	if (!sc->readlink) {
		gf_log (this->name, GF_LOG_DEBUG,
			"updating cache: %s", link);

		sc->readlink = strdup (link);
	} else {
		gf_log (this->name, GF_LOG_DEBUG,
			"not updating existing cache: %s with %s",
			sc->readlink, link);
	}

	return 0;
}


int
sc_cache_set (xlator_t *this, inode_t *inode, struct stat *buf,
	      const char *link)
{
	struct symlink_cache *sc = NULL;
	int                   ret = -1;
	int                   need_set = 0;


	symlink_inode_ctx_get (inode, this, VOID(&sc));
	if (!sc) {
		need_set = 1;
		sc = CALLOC (1, sizeof (*sc));
		if (!sc) {
			gf_log (this->name, GF_LOG_ERROR,
				"out of memory :(");
			goto err;
		}
	}

	if (sc->readlink) {
		gf_log (this->name, GF_LOG_DEBUG,
			"replacing old cache: %s with new cache: %s",
			sc->readlink, link);
		FREE (sc->readlink);
		sc->readlink = NULL;
	}

	if (link) {
		sc->readlink = strdup (link);
		if (!sc->readlink) {
			gf_log (this->name, GF_LOG_ERROR,
				"out of memory :(");
			goto err;
		}
	}

	sc->ctime = buf->st_ctime;

	gf_log (this->name, GF_LOG_DEBUG,
		"setting symlink cache: %s", link);

	if (need_set) {
		ret = symlink_inode_ctx_set (inode, this, sc);

		if (ret < 0) {
			gf_log (this->name, GF_LOG_ERROR,
				"could not set inode context (%s)",
				strerror (-ret));
			goto err;
		}
	}

	return 0;
err:

	if (sc) {
		if (sc->readlink)
			FREE (sc->readlink);
		sc->readlink = NULL;
		FREE (sc);
	}

	return -1;
}


int
sc_cache_flush (xlator_t *this, inode_t *inode)
{
	struct symlink_cache *sc = NULL;

	symlink_inode_ctx_get (inode, this, VOID(&sc));
	if (!sc)
		return 0;

	if (sc->readlink) {
		gf_log (this->name, GF_LOG_DEBUG,
			"flushing cache: %s", sc->readlink);

		FREE (sc->readlink);
		sc->readlink = NULL;
	}

	FREE (sc);

	return 0;
}


int
sc_cache_validate (xlator_t *this, inode_t *inode, struct stat *buf)
{
	struct symlink_cache *sc = NULL;


	if (!S_ISLNK (buf->st_mode)) {
		sc_cache_flush (this, inode);
		return 0;
	}

	symlink_inode_ctx_get (inode, this, VOID(&sc));

	if (!sc) {
		sc_cache_set (this, inode, buf, NULL);
		inode_ctx_get (inode, this, (uint64_t *)(&sc));

		if (!sc) {
			gf_log (this->name, GF_LOG_ERROR,
				"out of memory :(");
			return 0;
		}
	}

	if (sc->ctime == buf->st_ctime)
		return 0;

	/* STALE */
	if (sc->readlink) {
		gf_log (this->name, GF_LOG_DEBUG,
			"flushing cache: %s", sc->readlink);

		FREE (sc->readlink);
		sc->readlink = NULL;
	}

	sc->ctime = buf->st_ctime;

	return 0;
}



int
sc_cache_get (xlator_t *this, inode_t *inode, char **link)
{
	struct symlink_cache *sc = NULL;

	symlink_inode_ctx_get (inode, this, VOID(&sc));

	if (!sc)
		return 0;

	if (link && sc->readlink)
		*link = strdup (sc->readlink);
	return 0;
}


int
sc_readlink_cbk (call_frame_t *frame, void *cookie,
		 xlator_t *this, int op_ret, int op_errno,
		 const char *link)
{
	if (op_ret > 0)
		sc_cache_update (this, frame->local, link);

	inode_unref (frame->local);
	frame->local = NULL;

        STACK_UNWIND (frame, op_ret, op_errno, link);
        return 0;
}


int
sc_readlink (call_frame_t *frame, xlator_t *this,
	     loc_t *loc, size_t size)
{
	char *link = NULL;

	sc_cache_get (this, loc->inode, &link);

	if (link) {
		/* cache hit */
		gf_log (this->name, GF_LOG_DEBUG,
			"cache hit %s -> %s",
			loc->path, link);
		STACK_UNWIND (frame, strlen (link) + 1, 0, link);
		FREE (link);
		return 0;
	}

	frame->local = inode_ref (loc->inode);

        STACK_WIND (frame, sc_readlink_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readlink,
                    loc, size);

	return 0;
}


int
sc_symlink_cbk (call_frame_t *frame, void *cookie,
		xlator_t *this, int op_ret, int op_errno,
		inode_t *inode, struct stat *buf)
{
	if (op_ret == 0) {
		if (frame->local) {
			sc_cache_set (this, inode, buf, frame->local);
		}
	}

        STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
        return 0;
}


int
sc_symlink (call_frame_t *frame, xlator_t *this,
	    const char *dst, loc_t *src)
{
	frame->local = strdup (dst);

        STACK_WIND (frame, sc_symlink_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->symlink,
                    dst, src);

	return 0;
}


int
sc_lookup_cbk (call_frame_t *frame, void *cookie,
	       xlator_t *this, int op_ret, int op_errno,
	       inode_t *inode, struct stat *buf, dict_t *xattr)
{
	if (op_ret == 0)
		sc_cache_validate (this, inode, buf);
	else
		sc_cache_flush (this, inode);

        STACK_UNWIND (frame, op_ret, op_errno, inode, buf, xattr);
        return 0;
}


int
sc_lookup (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, int need_xattr)
{
        STACK_WIND (frame, sc_lookup_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup,
                    loc, need_xattr);

        return 0;
}


int
sc_forget (xlator_t *this,
	   inode_t *inode)
{
	sc_cache_flush (this, inode);

        return 0;
}


int32_t 
init (xlator_t *this)
{
	
        if (!this->children || this->children->next)
        {
                gf_log (this->name, GF_LOG_ERROR,
                        "FATAL: volume (%s) not configured with exactly one "
			"child", this->name);
                return -1;
        }

	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"dangling volume. check volfile ");
	}

        return 0;
}


void
fini (xlator_t *this)
{
        return;
}


struct xlator_fops fops = {
	.lookup      = sc_lookup,
	.symlink     = sc_symlink,
	.readlink    = sc_readlink,
};

struct xlator_mops mops = {
};

struct xlator_cbks cbks = {
        .forget  = sc_forget,
};

struct volume_options options[] = {
	{ .key = {NULL} },
};
