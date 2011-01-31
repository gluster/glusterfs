/*
   Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU Affero General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Affero General Public License for more details.

   You should have received a copy of the GNU Affero General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
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


/* gf_*_list[] */

char *gf_fop_list[GF_FOP_MAXVALUE];
char *gf_mgmt_list[GF_MGMT_MAXVALUE];


void
gf_op_list_init()
{
	gf_fop_list[GF_FOP_NULL]        = "NULL";
	gf_fop_list[GF_FOP_STAT]        = "STAT";
	gf_fop_list[GF_FOP_READLINK]    = "READLINK";
	gf_fop_list[GF_FOP_MKNOD]       = "MKNOD";
	gf_fop_list[GF_FOP_MKDIR]       = "MKDIR";
	gf_fop_list[GF_FOP_UNLINK]      = "UNLINK";
	gf_fop_list[GF_FOP_RMDIR]       = "RMDIR";
	gf_fop_list[GF_FOP_SYMLINK]     = "SYMLINK";
	gf_fop_list[GF_FOP_RENAME]      = "RENAME";
	gf_fop_list[GF_FOP_LINK]        = "LINK";
	gf_fop_list[GF_FOP_TRUNCATE]    = "TRUNCATE";
	gf_fop_list[GF_FOP_OPEN]        = "OPEN";
	gf_fop_list[GF_FOP_READ]        = "READ";
	gf_fop_list[GF_FOP_WRITE]       = "WRITE";
	gf_fop_list[GF_FOP_STATFS]      = "STATFS";
	gf_fop_list[GF_FOP_FLUSH]       = "FLUSH";
	gf_fop_list[GF_FOP_FSYNC]       = "FSYNC";
	gf_fop_list[GF_FOP_SETXATTR]    = "SETXATTR";
	gf_fop_list[GF_FOP_GETXATTR]    = "GETXATTR";
	gf_fop_list[GF_FOP_REMOVEXATTR] = "REMOVEXATTR";
	gf_fop_list[GF_FOP_OPENDIR]     = "OPENDIR";
	gf_fop_list[GF_FOP_FSYNCDIR]    = "FSYNCDIR";
	gf_fop_list[GF_FOP_ACCESS]      = "ACCESS";
	gf_fop_list[GF_FOP_CREATE]      = "CREATE";
	gf_fop_list[GF_FOP_FTRUNCATE]   = "FTRUNCATE";
	gf_fop_list[GF_FOP_FSTAT]       = "FSTAT";
	gf_fop_list[GF_FOP_LK]          = "LK";
	gf_fop_list[GF_FOP_LOOKUP]      = "LOOKUP";
	gf_fop_list[GF_FOP_READDIR]     = "READDIR";
	gf_fop_list[GF_FOP_INODELK]     = "INODELK";
	gf_fop_list[GF_FOP_FINODELK]    = "FINODELK";
	gf_fop_list[GF_FOP_ENTRYLK]     = "ENTRYLK";
	gf_fop_list[GF_FOP_FENTRYLK]    = "FENTRYLK";
	gf_fop_list[GF_FOP_XATTROP]     = "XATTROP";
	gf_fop_list[GF_FOP_FXATTROP]    = "FXATTROP";
	gf_fop_list[GF_FOP_FSETXATTR]   = "FSETXATTR";
	gf_fop_list[GF_FOP_FGETXATTR]   = "FGETXATTR";
        gf_fop_list[GF_FOP_RCHECKSUM]   = "RCHECKSUM";
        gf_fop_list[GF_FOP_SETATTR]     = "SETATTR";
        gf_fop_list[GF_FOP_FSETATTR]    = "FSETATTR";
	gf_fop_list[GF_FOP_READDIRP]    = "READDIRP";
	gf_fop_list[GF_FOP_GETSPEC]     = "GETSPEC";
	gf_fop_list[GF_FOP_FORGET]      = "FORGET";
	gf_fop_list[GF_FOP_RELEASE]     = "RELEASE";
	gf_fop_list[GF_FOP_RELEASEDIR]  = "RELEASEDIR";

	gf_fop_list[GF_MGMT_NULL]  = "NULL";
	return;
}


/* CTX */
static glusterfs_ctx_t *glusterfs_ctx;


int
glusterfs_ctx_init ()
{
        int  ret = 0;

        if (glusterfs_ctx)
                goto out;

        glusterfs_ctx = CALLOC (1, sizeof (*glusterfs_ctx));
        if (!glusterfs_ctx) {
                ret = -1;
                goto out;
        }

        INIT_LIST_HEAD (&glusterfs_ctx->graphs);
        ret = pthread_mutex_init (&glusterfs_ctx->lock, NULL);

out:
        return ret;
}


glusterfs_ctx_t *
glusterfs_ctx_get ()
{
        return glusterfs_ctx;

}


/* THIS */

xlator_t global_xlator;
static pthread_key_t this_xlator_key;

void
glusterfs_this_destroy (void *ptr)
{
        if (ptr)
                FREE (ptr);
}


int
glusterfs_this_init ()
{
        int  ret = 0;

        ret = pthread_key_create (&this_xlator_key, glusterfs_this_destroy);
        if (ret != 0) {
                return ret;
        }

        global_xlator.name = "glusterfs";
        global_xlator.type = "global";
        global_xlator.ctx  = glusterfs_ctx;

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


/* IS_CENTRAL_LOG */

static pthread_key_t central_log_flag_key;

void
glusterfs_central_log_flag_destroy (void *ptr)
{
        if (ptr)
                FREE (ptr);
}


int
glusterfs_central_log_flag_init ()
{
        int ret = 0;

        ret = pthread_key_create (&central_log_flag_key,
                                  glusterfs_central_log_flag_destroy);

        if (ret != 0) {
                return ret;
        }

        pthread_setspecific (central_log_flag_key, (void *) 0);

        return ret;
}


void
glusterfs_central_log_flag_set ()
{
        pthread_setspecific (central_log_flag_key, (void *) 1);
}


long
glusterfs_central_log_flag_get ()
{
        long flag = 0;

        flag = (long) pthread_getspecific (central_log_flag_key);

        return flag;
}


void
glusterfs_central_log_flag_unset ()
{
        pthread_setspecific (central_log_flag_key, (void *) 0);
}



/* SYNCTASK */

static pthread_key_t synctask_key;


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

static pthread_key_t uuid_buf_key;
static char global_uuid_buf[GF_UUID_BUF_SIZE];
void
glusterfs_uuid_buf_destroy (void *ptr)
{
        if (ptr)
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
                if(ret)
                        buf = global_uuid_buf;
        }
        return buf;
}

int
glusterfs_globals_init ()
{
        int ret = 0;

        gf_op_list_init ();

        ret = glusterfs_ctx_init ();
        if (ret)
                goto out;

        ret = glusterfs_this_init ();
        if (ret)
                goto out;

        ret = glusterfs_central_log_flag_init ();
        if (ret)
                goto out;

        ret = glusterfs_uuid_buf_init ();
        if(ret)
                goto out;

        gf_mem_acct_enable_set ();

        ret = synctask_init ();
        if (ret)
                goto out;

out:
        return ret;
}


char eventstring[GF_EVENT_MAXVAL][64] = {
        "Invalid event",
        "Parent Up",
        "Poll In",
        "Poll Out",
        "Poll Err",
        "Child Up",
        "Child Down",
        "Child Connecting",
        "Transport Cleanup",
        "Transport Connected",
        "Volfile Modified",
};

/* Copy the string ptr contents if needed for yourself */
char *
glusterfs_strevent (glusterfs_event_t ev)
{
        return eventstring[ev];
}

