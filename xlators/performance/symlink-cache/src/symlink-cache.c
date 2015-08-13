/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "list.h"
#include "compat.h"
#include "compat-errno.h"
#include "common-utils.h"
#include "symlink-cache-messages.h"

struct symlink_cache {
	time_t ctime;
	char   *readlink;
};


static int
symlink_inode_ctx_get (inode_t *inode, xlator_t *this, void **ctx)
{
	int ret = 0;
	uint64_t tmp_ctx = 0;
	ret = inode_ctx_get (inode, this, &tmp_ctx);
	if (-1 == ret)
		gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        SYMLINK_CACHE_MSG_DICT_GET_FAILED, "dict get failed");
	else
		*ctx = (void *)(long)tmp_ctx;

	return 0;
}


static int
symlink_inode_ctx_set (inode_t *inode, xlator_t *this, void *ctx)
{
	int ret = 0;
	ret = inode_ctx_put (inode, this, (uint64_t)(long) ctx);
	if (-1 == ret)
		gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        SYMLINK_CACHE_MSG_DICT_SET_FAILED, "dict set failed");

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
		gf_msg_debug (this->name, 0,
                              "updating cache: %s", link);

		sc->readlink = strdup (link);
	} else
                gf_msg_debug (this->name, 0,
                              "not updating existing cache: %s with %s",
                              sc->readlink, link);


	return 0;
}


int
sc_cache_set (xlator_t *this, inode_t *inode, struct iatt *buf,
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
			gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
				SYMLINK_CACHE_MSG_NO_MEMORY,
                                "out of memory :(");
			goto err;
		}
	}

	if (sc->readlink) {
		gf_msg_debug (this->name, 0,
                              "replacing old cache: %s with new cache: %s",
                              sc->readlink, link);
		FREE (sc->readlink);
		sc->readlink = NULL;
	}

	if (link) {
		sc->readlink = strdup (link);
		if (!sc->readlink) {
			gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
				SYMLINK_CACHE_MSG_NO_MEMORY,
                                "out of memory :(");
			goto err;
		}
	}

	sc->ctime = buf->ia_ctime;

	gf_msg_debug (this->name, 0,
                      "setting symlink cache: %s", link);

	if (need_set) {
		ret = symlink_inode_ctx_set (inode, this, sc);

		if (ret < 0) {
			gf_msg (this->name, GF_LOG_ERROR,
                                -ret, SYMLINK_CACHE_MSG_NO_MEMORY,
                                "could not set inode context ");
			goto err;
		}
	}

	return 0;
err:

	if (sc) {
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
		gf_msg_debug (this->name, 0,
                              "flushing cache: %s", sc->readlink);

		FREE (sc->readlink);
		sc->readlink = NULL;
	}

	FREE (sc);

	return 0;
}


int
sc_cache_validate (xlator_t *this, inode_t *inode, struct iatt *buf)
{
	struct symlink_cache *sc = NULL;
	uint64_t tmp_sc = 0;

	if (!IA_ISLNK (buf->ia_type)) {
		sc_cache_flush (this, inode);
		return 0;
	}

	symlink_inode_ctx_get (inode, this, VOID(&sc));

	if (!sc) {
		sc_cache_set (this, inode, buf, NULL);
		inode_ctx_get (inode, this, &tmp_sc);

		if (!tmp_sc) {
			gf_msg (this->name, GF_LOG_ERROR, 0,
                                SYMLINK_CACHE_MSG_NO_MEMORY,
				"out of memory :(");
			return 0;
		}
		sc = (struct symlink_cache *)(long)tmp_sc;
	}

	if (sc->ctime == buf->ia_ctime)
		return 0;

	/* STALE */
	if (sc->readlink) {
		gf_msg_debug (this->name, 0,
                              "flushing cache: %s", sc->readlink);

		FREE (sc->readlink);
		sc->readlink = NULL;
	}

	sc->ctime = buf->ia_ctime;

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
		 const char *link, struct iatt *sbuf, dict_t *xdata)
{
	if (op_ret > 0)
		sc_cache_update (this, frame->local, link);

	inode_unref (frame->local);
	frame->local = NULL;

        STACK_UNWIND_STRICT (readlink, frame, op_ret, op_errno, link, sbuf,
                             xdata);
        return 0;
}


int
sc_readlink (call_frame_t *frame, xlator_t *this,
	     loc_t *loc, size_t size, dict_t *xdata)
{
	char *link = NULL;
        struct iatt buf = {0, };

	sc_cache_get (this, loc->inode, &link);

	if (link) {
		/* cache hit */
		gf_msg_debug (this->name, 0,
                              "cache hit %s -> %s",
                              loc->path, link);

                /*
                  libglusterfsclient, nfs or any other translators
                  using buf in readlink_cbk should be aware that @buf
                  is 0 filled
                */
		STACK_UNWIND_STRICT (readlink, frame, strlen (link), 0, link,
                                     &buf, NULL);
		FREE (link);
		return 0;
	}

	frame->local = inode_ref (loc->inode);

        STACK_WIND (frame, sc_readlink_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readlink,
                    loc, size, xdata);

	return 0;
}


int
sc_symlink_cbk (call_frame_t *frame, void *cookie,
		xlator_t *this, int op_ret, int op_errno,
                inode_t *inode, struct iatt *buf, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
	if (op_ret == 0) {
		if (frame->local) {
			sc_cache_set (this, inode, buf, frame->local);
		}
	}

        STACK_UNWIND_STRICT (symlink, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}


int
sc_symlink (call_frame_t *frame, xlator_t *this,
	    const char *dst, loc_t *src, mode_t umask, dict_t *xdata)
{
	frame->local = strdup (dst);

        STACK_WIND (frame, sc_symlink_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->symlink,
                    dst, src, umask, xdata);

	return 0;
}


int
sc_lookup_cbk (call_frame_t *frame, void *cookie,
	       xlator_t *this, int op_ret, int op_errno,
	       inode_t *inode, struct iatt *buf, dict_t *xdata,
               struct iatt *postparent)
{
	if (op_ret == 0)
		sc_cache_validate (this, inode, buf);
	else
		sc_cache_flush (this, inode);

        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, inode, buf,
                             xdata, postparent);
        return 0;
}


int
sc_lookup (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, dict_t *xdata)
{
        STACK_WIND (frame, sc_lookup_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup,
                    loc, xdata);

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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        SYMLINK_CACHE_MSG_XLATOR_CHILD_MISCONFIGURED,
                        "FATAL: volume (%s) not configured with exactly one "
			"child", this->name);
                return -1;
        }

	if (!this->parents) {
		gf_msg (this->name, GF_LOG_WARNING, 0,
			SYMLINK_CACHE_MSG_VOL_MISCONFIGURED,
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


struct xlator_cbks cbks = {
        .forget  = sc_forget,
};

struct volume_options options[] = {
	{ .key = {NULL} },
};
