/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "xlator.h"
#include "defaults.h"

#include "meta-mem-types.h"
#include "meta.h"
#include "strfd.h"
#include "globals.h"
#include "lkowner.h"

static int
frames_file_fill (xlator_t *this, inode_t *file, strfd_t *strfd)
{
        struct call_pool *pool = NULL;
        call_stack_t *stack = NULL;
        call_frame_t *frame = NULL;
        int i = 0;
        int j = 1;

        if (!this || !file || !strfd)
                return -1;

        pool = this->ctx->pool;

        LOCK (&pool->lock);
        {
                strprintf (strfd, "{ \n\t\"Stack\": [\n");
                list_for_each_entry (stack, &pool->all_frames, all_frames) {
                        strprintf (strfd, "\t   {\n");
                        strprintf (strfd, "\t\t\"Number\": %d,\n", ++i);
                        strprintf (strfd, "\t\t\"Frame\": [\n");
                        j = 1;
                        list_for_each_entry (frame, &stack->myframes, frames) {
                                strprintf (strfd, "\t\t   {\n");
                                strprintf (strfd, "\t\t\t\"Number\": %d,\n",
                                                j++);
                                strprintf (strfd,
                                                "\t\t\t\"Xlator\": \"%s\",\n",
                                                frame->this->name);
                                if (frame->begin.tv_sec)
                                        strprintf (strfd,
                                                        "\t\t\t\"Creation_time\": %d.%d,\n",
                                                        (int)frame->begin.tv_sec,
                                                        (int)frame->begin.tv_usec);
                                strprintf (strfd, " \t\t\t\"Refcount\": %d,\n",
                                                frame->ref_count);
                                if (frame->parent)
                                        strprintf (strfd, "\t\t\t\"Parent\": \"%s\",\n",
                                                        frame->parent->this->name);
                                if (frame->wind_from)
                                        strprintf (strfd, "\t\t\t\"Wind_from\": \"%s\",\n",
                                                        frame->wind_from);
                                if (frame->wind_to)
                                        strprintf (strfd, "\t\t\t\"Wind_to\": \"%s\",\n",
                                                        frame->wind_to);
                                if (frame->unwind_from)
                                        strprintf (strfd, "\t\t\t\"Unwind_from\": \"%s\",\n",
                                                        frame->unwind_from);
                                if (frame->unwind_to)
                                        strprintf (strfd, "\t\t\t\"Unwind_to\": \"%s\",\n",
                                                        frame->unwind_to);
                                strprintf (strfd, "\t\t\t\"Complete\": %d\n",
                                                frame->complete);
                                if (list_is_last (&frame->frames,
                                                  &stack->myframes))
                                        strprintf (strfd, "\t\t   }\n");
                                else
                                        strprintf (strfd, "\t\t   },\n");
                        }
                        strprintf (strfd, "\t\t],\n");
                        strprintf (strfd, "\t\t\"Unique\": %"PRId64",\n",
                                        stack->unique);
                        strprintf (strfd, "\t\t\"Type\": \"%s\",\n",
                                        gf_fop_list[stack->op]);
                        strprintf (strfd, "\t\t\"UID\": %d,\n",
                                        stack->uid);
                        strprintf (strfd, "\t\t\"GID\": %d,\n",
                                        stack->gid);
                        strprintf (strfd, "\t\t\"LK_owner\": \"%s\"\n",
                                        lkowner_utoa (&stack->lk_owner));
                        if (i == (int)pool->cnt)
                                strprintf (strfd, "\t   }\n");
                        else
                                strprintf (strfd, "\t   },\n");
                }
                strprintf (strfd, "\t],\n");
                strprintf (strfd, "\t\"Call_Count\": %d\n",
                                (int)pool->cnt);
                strprintf (strfd, "}");
        }
        UNLOCK (&pool->lock);

        return strfd->size;
}


static struct meta_ops frames_file_ops = {
        .file_fill = frames_file_fill,
};


int
meta_frames_file_hook (call_frame_t *frame, xlator_t *this, loc_t *loc,
                       dict_t *xdata)
{
        meta_ops_set (loc->inode, this, &frames_file_ops);
        return 0;
}
