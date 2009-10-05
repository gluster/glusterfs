/*
   Copyright (c) 2009 Gluster, Inc. <http://www.gluster.com>
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
#endif /* !_CONFIG_H */

#include <pthread.h>

#include "globals.h"
#include "glusterfs.h"
#include "xlator.h"


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


int
glusterfs_globals_init ()
{
        int ret = 0;

        ret = glusterfs_ctx_init ();
        if (ret)
                goto out;

        ret = glusterfs_this_init ();
        if (ret)
                goto out;

        ret = glusterfs_central_log_flag_init ();
        if (ret)
                goto out;
out:
        return ret;
}
