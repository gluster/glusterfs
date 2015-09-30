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

#include "xlator.h"
#include "defaults.h"

enum gf_pdht_mem_types_ {
        gf_pdht_mt_coord_t = gf_common_mt_end + 1,
        gf_pdht_mt_end
};

typedef struct {
        pthread_mutex_t         lock;
        uint16_t                refs;
        int32_t                 op_ret;
        int32_t                 op_errno;
        dict_t                  *xdata;
} pdht_coord_t;

static char PROTECT_KEY[] = "trusted.glusterfs.protect";

void
pdht_unref_and_unlock (call_frame_t *frame, xlator_t *this,
                       pdht_coord_t *coord)
{
        gf_boolean_t    should_unwind;

        should_unwind = (--(coord->refs) == 0);
        pthread_mutex_unlock(&coord->lock);

        if (should_unwind) {
                STACK_UNWIND_STRICT (setxattr, frame, 
                                     coord->op_ret, coord->op_errno,
                                     coord->xdata);
                if (coord->xdata) {
                        dict_unref(coord->xdata);
                }
                GF_FREE(coord);
        }
}

int32_t
pdht_recurse_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        pdht_coord_t    *coord = cookie;

        pthread_mutex_lock(&coord->lock);
        if (op_ret) {
                coord->op_ret = op_ret;
                coord->op_errno = op_errno;
        }
        if (xdata) {
                if (coord->xdata) {
                        dict_unref(coord->xdata);
                }
                coord->xdata = dict_ref(xdata);
        }
        pdht_unref_and_unlock(frame,this,coord);
                
        return 0;
}

void
pdht_recurse (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
              int32_t flags, dict_t *xdata, xlator_t *xl, pdht_coord_t *coord)
{
        xlator_list_t   *iter;

        if (!strcmp(xl->type,"features/prot_client")) {
                pthread_mutex_lock(&coord->lock);
                ++(coord->refs);
                pthread_mutex_unlock(&coord->lock);
                STACK_WIND_COOKIE (frame, pdht_recurse_cbk, coord, xl,
                                   xl->fops->setxattr, loc, dict, flags, xdata);
        }

        else for (iter = xl->children; iter; iter = iter->next) {
                pdht_recurse (frame, this, loc, dict, flags, xdata,
                              iter->xlator, coord);
        }
}

int32_t
pdht_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
               int32_t flags, dict_t *xdata)
{
        pdht_coord_t    *coord;

        if (!dict_get(dict,PROTECT_KEY)) {
                goto simple_wind;
        }

        if (dict->count > 1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "attempted to mix %s with other keys", PROTECT_KEY);
                goto simple_wind;
        }

        coord = GF_CALLOC(1,sizeof(*coord),gf_pdht_mt_coord_t);
        if (!coord) {
                gf_log (this->name, GF_LOG_WARNING, "allocation failed");
                goto simple_wind;
        }

        pthread_mutex_init(&coord->lock,NULL);
        coord->refs = 1;
        coord->op_ret = 0;
        coord->xdata = NULL;

        pdht_recurse(frame,this,loc,dict,flags,xdata,this,coord);
        pthread_mutex_lock(&coord->lock);
        pdht_unref_and_unlock(frame,this,coord);

        return 0;

simple_wind:
        STACK_WIND_TAIL (frame,
                         FIRST_CHILD(this), FIRST_CHILD(this)->fops->setxattr,
                         loc, dict, flags, xdata);
        return 0;
}

int32_t
init (xlator_t *this)
{
	if (!this->children || this->children->next) {
		gf_log (this->name, GF_LOG_ERROR,
			"translator not configured with exactly one child");
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
        .setxattr = pdht_setxattr,
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
	{ .key = {NULL} },
};
