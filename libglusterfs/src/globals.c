/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif /* !_CONFIG_H */

#include <pthread.h>

#include "glusterfs.h"
#include "globals.h"
#include "xlator.h"
#include "mem-pool.h"
#include "syncop.h"

const char *gf_fop_list[GF_FOP_MAXVALUE] = {
        [GF_FOP_NULL]        = "NULL",
        [GF_FOP_STAT]        = "STAT",
        [GF_FOP_READLINK]    = "READLINK",
        [GF_FOP_MKNOD]       = "MKNOD",
        [GF_FOP_MKDIR]       = "MKDIR",
        [GF_FOP_UNLINK]      = "UNLINK",
        [GF_FOP_RMDIR]       = "RMDIR",
        [GF_FOP_SYMLINK]     = "SYMLINK",
        [GF_FOP_RENAME]      = "RENAME",
        [GF_FOP_LINK]        = "LINK",
        [GF_FOP_TRUNCATE]    = "TRUNCATE",
        [GF_FOP_OPEN]        = "OPEN",
        [GF_FOP_READ]        = "READ",
        [GF_FOP_WRITE]       = "WRITE",
        [GF_FOP_STATFS]      = "STATFS",
        [GF_FOP_FLUSH]       = "FLUSH",
        [GF_FOP_FSYNC]       = "FSYNC",
        [GF_FOP_SETXATTR]    = "SETXATTR",
        [GF_FOP_GETXATTR]    = "GETXATTR",
        [GF_FOP_REMOVEXATTR] = "REMOVEXATTR",
        [GF_FOP_OPENDIR]     = "OPENDIR",
        [GF_FOP_FSYNCDIR]    = "FSYNCDIR",
        [GF_FOP_ACCESS]      = "ACCESS",
        [GF_FOP_CREATE]      = "CREATE",
        [GF_FOP_FTRUNCATE]   = "FTRUNCATE",
        [GF_FOP_FSTAT]       = "FSTAT",
        [GF_FOP_LK]          = "LK",
        [GF_FOP_LOOKUP]      = "LOOKUP",
        [GF_FOP_READDIR]     = "READDIR",
        [GF_FOP_INODELK]     = "INODELK",
        [GF_FOP_FINODELK]    = "FINODELK",
        [GF_FOP_ENTRYLK]     = "ENTRYLK",
        [GF_FOP_FENTRYLK]    = "FENTRYLK",
        [GF_FOP_XATTROP]     = "XATTROP",
        [GF_FOP_FXATTROP]    = "FXATTROP",
        [GF_FOP_FSETXATTR]   = "FSETXATTR",
        [GF_FOP_FGETXATTR]   = "FGETXATTR",
        [GF_FOP_RCHECKSUM]   = "RCHECKSUM",
        [GF_FOP_SETATTR]     = "SETATTR",
        [GF_FOP_FSETATTR]    = "FSETATTR",
        [GF_FOP_READDIRP]    = "READDIRP",
        [GF_FOP_GETSPEC]     = "GETSPEC",
        [GF_FOP_FORGET]      = "FORGET",
        [GF_FOP_RELEASE]     = "RELEASE",
        [GF_FOP_RELEASEDIR]  = "RELEASEDIR",
        [GF_FOP_FREMOVEXATTR]= "FREMOVEXATTR",
	[GF_FOP_FALLOCATE]   = "FALLOCATE",
	[GF_FOP_DISCARD]     = "DISCARD",
        [GF_FOP_ZEROFILL]     = "ZEROFILL",
};
/* THIS */

xlator_t global_xlator;
static pthread_key_t this_xlator_key;
static pthread_key_t synctask_key;
static pthread_key_t uuid_buf_key;
static char          global_uuid_buf[GF_UUID_BUF_SIZE];
static pthread_key_t lkowner_buf_key;
static char          global_lkowner_buf[GF_LKOWNER_BUF_SIZE];


void
glusterfs_this_destroy (void *ptr)
{
        FREE (ptr);
}


int
glusterfs_this_init ()
{
        int  ret = 0;

        ret = pthread_key_create (&this_xlator_key, glusterfs_this_destroy);
        if (ret != 0) {
                gf_log ("", GF_LOG_WARNING, "failed to create the pthread key");
                return ret;
        }

        global_xlator.name = "glusterfs";
        global_xlator.type = "global";

        INIT_LIST_HEAD (&global_xlator.volume_options);

        return ret;
}


xlator_t **
__glusterfs_this_location ()
{
        xlator_t **this_location = NULL;
        int        ret = 0;

        this_location = pthread_getspecific (this_xlator_key);

        if (!this_location) {
                this_location = CALLOC (1, sizeof (*this_location));
                if (!this_location)
                        goto out;

                ret = pthread_setspecific (this_xlator_key, this_location);
                if (ret != 0) {
                        FREE (this_location);
                        this_location = NULL;
                        goto out;
                }
        }
out:
        if (this_location) {
                if (!*this_location)
                        *this_location = &global_xlator;
        }
        return this_location;
}


xlator_t *
glusterfs_this_get ()
{
        xlator_t **this_location = NULL;

        this_location = __glusterfs_this_location ();
        if (!this_location)
                return &global_xlator;

        return *this_location;
}


int
glusterfs_this_set (xlator_t *this)
{
        xlator_t **this_location = NULL;

        this_location = __glusterfs_this_location ();
        if (!this_location)
                return -ENOMEM;

        *this_location = this;

        return 0;
}

/* SYNCOPCTX */
static pthread_key_t syncopctx_key;

static void
syncopctx_key_destroy (void *ptr)
{
	struct syncopctx *opctx = ptr;

	if (opctx) {
		if (opctx->groups)
			GF_FREE (opctx->groups);

		GF_FREE (opctx);
	}

	return;
}

void *
syncopctx_getctx ()
{
	void *opctx = NULL;

	opctx = pthread_getspecific (syncopctx_key);

	return opctx;
}

int
syncopctx_setctx (void *ctx)
{
	int ret = 0;

	ret = pthread_setspecific (syncopctx_key, ctx);

	return ret;
}

static int
syncopctx_init (void)
{
	int ret;

	ret = pthread_key_create (&syncopctx_key, syncopctx_key_destroy);

	return ret;
}

/* SYNCTASK */

int
synctask_init ()
{
        int  ret = 0;

        ret = pthread_key_create (&synctask_key, NULL);

        return ret;
}

void *
synctask_get ()
{
        void   *synctask = NULL;

        synctask = pthread_getspecific (synctask_key);

        return synctask;
}


int
synctask_set (void *synctask)
{
        int     ret = 0;

        pthread_setspecific (synctask_key, synctask);

        return ret;
}

//UUID_BUFFER

void
glusterfs_uuid_buf_destroy (void *ptr)
{
        FREE (ptr);
}

int
glusterfs_uuid_buf_init ()
{
        int ret = 0;

        ret = pthread_key_create (&uuid_buf_key,
                                  glusterfs_uuid_buf_destroy);
        return ret;
}

char *
glusterfs_uuid_buf_get ()
{
        char *buf;
        int ret = 0;

        buf = pthread_getspecific (uuid_buf_key);
        if(!buf) {
                buf = MALLOC (GF_UUID_BUF_SIZE);
                ret = pthread_setspecific (uuid_buf_key, (void *) buf);
                if (ret)
                        buf = global_uuid_buf;
        }
        return buf;
}

/* LKOWNER_BUFFER */

void
glusterfs_lkowner_buf_destroy (void *ptr)
{
        FREE (ptr);
}

int
glusterfs_lkowner_buf_init ()
{
        int ret = 0;

        ret = pthread_key_create (&lkowner_buf_key,
                                  glusterfs_lkowner_buf_destroy);
        return ret;
}

char *
glusterfs_lkowner_buf_get ()
{
        char *buf;
        int ret = 0;

        buf = pthread_getspecific (lkowner_buf_key);
        if(!buf) {
                buf = MALLOC (GF_LKOWNER_BUF_SIZE);
                ret = pthread_setspecific (lkowner_buf_key, (void *) buf);
                if (ret)
                        buf = global_lkowner_buf;
        }
        return buf;
}

int
glusterfs_globals_init (glusterfs_ctx_t *ctx)
{
        int ret = 0;

        gf_log_globals_init (ctx);

        ret = glusterfs_this_init ();
        if (ret) {
                gf_log ("", GF_LOG_CRITICAL,
                        "ERROR: glusterfs-translator init failed");
                goto out;
        }

        ret = glusterfs_uuid_buf_init ();
        if(ret) {
                gf_log ("", GF_LOG_CRITICAL,
                        "ERROR: glusterfs uuid buffer init failed");
                goto out;
        }

        ret = glusterfs_lkowner_buf_init ();
        if(ret) {
                gf_log ("", GF_LOG_CRITICAL,
                        "ERROR: glusterfs lkowner buffer init failed");
                goto out;
        }

        ret = synctask_init ();
        if (ret) {
                gf_log ("", GF_LOG_CRITICAL,
                        "ERROR: glusterfs synctask init failed");
                goto out;
        }

        ret = syncopctx_init ();
        if (ret) {
                gf_log ("", GF_LOG_CRITICAL,
                        "ERROR: glusterfs syncopctx init failed");
                goto out;
        }
out:
        return ret;
}
