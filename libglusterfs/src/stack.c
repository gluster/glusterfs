/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "statedump.h"
#include "stack.h"

static inline
int call_frames_count (call_frame_t *call_frame)
{
        call_frame_t *pos;
        int32_t count = 0;

        if (!call_frame)
                return count;

        for (pos = call_frame; pos != NULL; pos = pos->next)
                count++;

        return count;
}

void
gf_proc_dump_call_frame (call_frame_t *call_frame, const char *key_buf,...)
{

        char prefix[GF_DUMP_MAX_BUF_LEN];
        va_list ap;
        call_frame_t my_frame;
        int  ret = -1;

        if (!call_frame)
                return;

        GF_ASSERT (key_buf);

        memset(prefix, 0, sizeof(prefix));
        memset(&my_frame, 0, sizeof(my_frame));
        va_start(ap, key_buf);
        vsnprintf(prefix, GF_DUMP_MAX_BUF_LEN, key_buf, ap);
        va_end(ap);

        ret = TRY_LOCK(&call_frame->lock);
        if (ret) {
                gf_log("", GF_LOG_WARNING, "Unable to dump call frame"
                       " errno: %s", strerror (errno));
                return;
        }

        memcpy(&my_frame, call_frame, sizeof(my_frame));
        UNLOCK(&call_frame->lock);

        gf_proc_dump_write("ref_count", "%d", my_frame.ref_count);
        gf_proc_dump_write("translator", "%s", my_frame.this->name);
        gf_proc_dump_write("complete", "%d", my_frame.complete);
        if (my_frame.parent)
                gf_proc_dump_write("parent", "%s", my_frame.parent->this->name);

        if (my_frame.wind_from)
                gf_proc_dump_write("wind_from", "%s", my_frame.wind_from);

        if (my_frame.wind_to)
                gf_proc_dump_write("wind_to", "%s", my_frame.wind_to);

        if (my_frame.unwind_from)
                gf_proc_dump_write("unwind_from", "%s", my_frame.unwind_from);

        if (my_frame.unwind_to)
                gf_proc_dump_write("unwind_to", "%s", my_frame.unwind_to);
}


void
gf_proc_dump_call_stack (call_stack_t *call_stack, const char *key_buf,...)
{
        char prefix[GF_DUMP_MAX_BUF_LEN];
        va_list ap;
        call_frame_t *trav;
        int32_t cnt, i;

        if (!call_stack)
                return;

        GF_ASSERT (key_buf);

        cnt = call_frames_count(&call_stack->frames);

        memset(prefix, 0, sizeof(prefix));
        va_start(ap, key_buf);
        vsnprintf(prefix, GF_DUMP_MAX_BUF_LEN, key_buf, ap);
        va_end(ap);

        gf_proc_dump_write("uid", "%d", call_stack->uid);
        gf_proc_dump_write("gid", "%d", call_stack->gid);
        gf_proc_dump_write("pid", "%d", call_stack->pid);
        gf_proc_dump_write("unique", "%Ld", call_stack->unique);
        gf_proc_dump_write("lk-owner", "%s", lkowner_utoa (&call_stack->lk_owner));

        if (call_stack->type == GF_OP_TYPE_FOP)
                gf_proc_dump_write("op", "%s",
                                   (char *)gf_fop_list[call_stack->op]);
        else
                gf_proc_dump_write("op", "stack");

        gf_proc_dump_write("type", "%d", call_stack->type);
        gf_proc_dump_write("cnt", "%d", cnt);

        trav = &call_stack->frames;

        for (i = 1; i <= cnt; i++) {
                if (trav) {
                        gf_proc_dump_add_section("%s.frame.%d", prefix, i);
                        gf_proc_dump_call_frame(trav, "%s.frame.%d", prefix, i);
                        trav = trav->next;
                }
        }
}

void
gf_proc_dump_pending_frames (call_pool_t *call_pool)
{

        call_stack_t     *trav = NULL;
        int              i = 1;
        int              ret = -1;

        if (!call_pool)
                return;

        ret = TRY_LOCK (&(call_pool->lock));
        if (ret) {
                gf_log("", GF_LOG_WARNING, "Unable to dump call pool"
                       " errno: %d", errno);
                return;
        }


        gf_proc_dump_add_section("global.callpool");
        gf_proc_dump_write("callpool_address","%p", call_pool);
        gf_proc_dump_write("callpool.cnt","%d", call_pool->cnt);


        list_for_each_entry (trav, &call_pool->all_frames, all_frames) {
                gf_proc_dump_add_section("global.callpool.stack.%d",i);
                gf_proc_dump_call_stack(trav, "global.callpool.stack.%d", i);
                i++;
        }
        UNLOCK (&(call_pool->lock));
}

void
gf_proc_dump_call_frame_to_dict (call_frame_t *call_frame,
                                 char *prefix, dict_t *dict)
{
        int             ret = -1;
        char            key[GF_DUMP_MAX_BUF_LEN] = {0,};
        call_frame_t    tmp_frame = {0,};

        if (!call_frame || !dict)
                return;

        ret = TRY_LOCK (&call_frame->lock);
        if (ret)
                return;
        memcpy (&tmp_frame, call_frame, sizeof (tmp_frame));
        UNLOCK (&call_frame->lock);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.refcount", prefix);
        ret = dict_set_int32 (dict, key, tmp_frame.ref_count);
        if (ret)
                return;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.translator", prefix);
        ret = dict_set_dynstr (dict, key, gf_strdup (tmp_frame.this->name));
        if (ret)
                return;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.complete", prefix);
        ret = dict_set_int32 (dict, key, tmp_frame.complete);
        if (ret)
                return;

        if (tmp_frame.parent) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.parent", prefix);
                ret = dict_set_dynstr (dict, key,
                                    gf_strdup (tmp_frame.parent->this->name));
                if (ret)
                        return;
        }

        if (tmp_frame.wind_from) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.windfrom", prefix);
                ret = dict_set_dynstr (dict, key,
                                       gf_strdup (tmp_frame.wind_from));
                if (ret)
                        return;
        }

        if (tmp_frame.wind_to) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.windto", prefix);
                ret = dict_set_dynstr (dict, key,
                                       gf_strdup (tmp_frame.wind_to));
                if (ret)
                        return;
        }

        if (tmp_frame.unwind_from) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.unwindfrom", prefix);
                ret = dict_set_dynstr (dict, key,
                                       gf_strdup (tmp_frame.unwind_from));
                if (ret)
                        return;
        }

        if (tmp_frame.unwind_to) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.unwind_to", prefix);
                ret = dict_set_dynstr (dict, key,
                                       gf_strdup (tmp_frame.unwind_to));
        }

        return;
}

void
gf_proc_dump_call_stack_to_dict (call_stack_t *call_stack,
                                 char *prefix, dict_t *dict)
{
        int             ret = -1;
        char            key[GF_DUMP_MAX_BUF_LEN] = {0,};
        call_frame_t    *trav = NULL;
        int             count = 0;
        int             i = 0;

        if (!call_stack || !dict)
                return;

        count = call_frames_count (&call_stack->frames);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.uid", prefix);
        ret = dict_set_int32 (dict, key, call_stack->uid);
        if (ret)
                return;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.gid", prefix);
        ret = dict_set_int32 (dict, key, call_stack->gid);
        if (ret)
                return;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.pid", prefix);
        ret = dict_set_int32 (dict, key, call_stack->pid);
        if (ret)
                return;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.unique", prefix);
        ret = dict_set_uint64 (dict, key, call_stack->unique);
        if (ret)
                return;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.op", prefix);
        if (call_stack->type == GF_OP_TYPE_FOP)
                ret = dict_set_str (dict, key,
                                    (char *)gf_fop_list[call_stack->op]);
        else
                ret = dict_set_str (dict, key, "other");

        if (ret)
                return;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.type", prefix);
        ret = dict_set_int32 (dict, key, call_stack->type);
        if (ret)
                return;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.count", prefix);
        ret = dict_set_int32 (dict, key, count);
        if (ret)
                return;

        trav = &call_stack->frames;
        for (i = 0; i < count; i++) {
                if (trav) {
                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "%s.frame%d",
                                  prefix, i);
                        gf_proc_dump_call_frame_to_dict (trav, key, dict);
                        trav = trav->next;
                }
        }

        return;
}

void
gf_proc_dump_pending_frames_to_dict (call_pool_t *call_pool, dict_t *dict)
{
        int             ret = -1;
        call_stack_t    *trav = NULL;
        char            key[GF_DUMP_MAX_BUF_LEN] = {0,};
        int             i = 0;

        if (!call_pool || !dict)
                return;

        ret = TRY_LOCK (&call_pool->lock);
        if (ret) {
                gf_log (THIS->name, GF_LOG_WARNING, "Unable to dump call pool"
                        " to dict. errno: %d", errno);
                return;
        }

        ret = dict_set_int32 (dict, "callpool.count", call_pool->cnt);
        if (ret)
                goto out;

        list_for_each_entry (trav, &call_pool->all_frames, all_frames) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "callpool.stack%d", i);
                gf_proc_dump_call_stack_to_dict (trav, key, dict);
                i++;
        }

out:
        UNLOCK (&call_pool->lock);

        return;
}

gf_boolean_t
__is_fuse_call (call_frame_t *frame)
{
        gf_boolean_t    is_fuse_call = _gf_false;
        GF_ASSERT (frame);
        GF_ASSERT (frame->root);

        if (NFS_PID != frame->root->pid)
                is_fuse_call = _gf_true;
        return is_fuse_call;
}
