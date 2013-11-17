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

#ifndef __NetBSD__
#include <execinfo.h>
#endif

#define NUM_FRAMES 20

static char PROTECT_KEY[] = "trusted.glusterfs.protect";

enum {
        PROT_ACT_NONE = 0,
        PROT_ACT_LOG,
        PROT_ACT_REJECT,
};

void
pcli_print_trace (char *name, call_frame_t *frame)
{
        void    *frames[NUM_FRAMES];
        char    **symbols;
        int     size;
        int     i;

        gf_log (name, GF_LOG_INFO, "Translator stack:");
        while (frame) {
                gf_log (name, GF_LOG_INFO, "%s (%s)",
                        frame->wind_from, frame->this->name);
                frame = frame->next;
        }

        size = backtrace(frames,NUM_FRAMES);
        if (size <= 0) {
                return;
        }
        symbols = backtrace_symbols(frames,size);
        if (!symbols) {
                return;
        }

        gf_log(name, GF_LOG_INFO, "Processor stack:");
        for (i = 0; i < size; ++i) {
                gf_log (name, GF_LOG_INFO, "%s", symbols[i]);
        }
        free(symbols);
}

int32_t
pcli_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
                loc_t *newloc, dict_t *xdata)
{
        uint64_t        value;

        if (newloc->parent == oldloc->parent) {
                gf_log (this->name, GF_LOG_DEBUG, "rename in same directory");
                goto simple_unwind;
        }
        if (!oldloc->parent) {
                goto simple_unwind;
        }
        if (inode_ctx_get(oldloc->parent,this,&value) != 0) {
                goto simple_unwind;
        }

        if (value != PROT_ACT_NONE) {
                gf_log (this->name, GF_LOG_WARNING,
                        "got rename for protected %s", oldloc->path);
                pcli_print_trace(this->name,frame->next);
                if (value == PROT_ACT_REJECT) {
                        STACK_UNWIND_STRICT (rename, frame, -1, EPERM,
                                             NULL, NULL, NULL, NULL, NULL,
                                             xdata);
                        return 0;
                }
        }

simple_unwind:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->rename, oldloc, newloc,
                         xdata);
        return 0;
}

int32_t
pcli_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
               int32_t flags, dict_t *xdata)
{
        data_t          *data;
        uint64_t        value;

        /*
         * We can't use dict_get_str and strcmp here, because the value comes
         * directly from the user and might not be NUL-terminated (it would
         * be if we had set it ourselves.
         */

        data = dict_get(dict,PROTECT_KEY);
        if (!data) {
                goto simple_wind;
        }

        if (dict->count > 1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "attempted to mix %s with other keys", PROTECT_KEY);
                goto simple_wind;
        }

        gf_log (this->name, GF_LOG_DEBUG, "got %s request", PROTECT_KEY);
        if (!strncmp(data->data,"log",data->len)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "logging removals on %s", loc->path);
                value = PROT_ACT_LOG;
        }
        else if (!strncmp(data->data,"reject",data->len)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "rejecting removals on %s", loc->path);
                value = PROT_ACT_REJECT;
        }
        else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "removing protection on %s", loc->path);
                value = PROT_ACT_NONE;
        }
        /* Right now the value doesn't matter - just the presence. */
        if (inode_ctx_set(loc->inode,this,&value) != 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to set protection status for %s", loc->path);
        }
        STACK_UNWIND_STRICT (setxattr, frame, 0, 0, NULL);
        return 0;

simple_wind:
        STACK_WIND_TAIL (frame,
                         FIRST_CHILD(this), FIRST_CHILD(this)->fops->setxattr,
                         loc, dict, flags, xdata);
        return 0;
}

int32_t
pcli_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
             dict_t *xdata)
{
        uint64_t        value;

        if (!loc->parent || (inode_ctx_get(loc->parent,this,&value) != 0)) {
                goto simple_unwind;
        }

        if (value != PROT_ACT_NONE) {
                gf_log (this->name, GF_LOG_WARNING,
                        "got unlink for protected %s", loc->path);
                pcli_print_trace(this->name,frame->next);
                if (value == PROT_ACT_REJECT) {
                        STACK_UNWIND_STRICT (unlink, frame, -1, EPERM,
                                             NULL, NULL, NULL);
                        return 0;
                }
        }

simple_unwind:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->unlink, loc, xflag, xdata);
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
        .rename         = pcli_rename,
        .setxattr       = pcli_setxattr,
        .unlink         = pcli_unlink,
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
	{ .key = {NULL} },
};
