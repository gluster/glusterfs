/*
  Copyright (c) 2006-2011 Gluster, Inc. <http://www.gluster.com>
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

/*
  This file defines MACROS and static inlines used to emulate a function
  call over asynchronous communication with remote server
*/

#ifndef _STACK_H
#define _STACK_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

struct _call_stack_t;
typedef struct _call_stack_t call_stack_t;
struct _call_frame_t;
typedef struct _call_frame_t call_frame_t;
struct _call_pool_t;
typedef struct _call_pool_t call_pool_t;

#include <sys/time.h>

#include "xlator.h"
#include "dict.h"
#include "list.h"
#include "common-utils.h"
#include "globals.h"

#define NFS_PID 1
#define LOW_PRIO_PROC_PID -1
typedef int32_t (*ret_fn_t) (call_frame_t *frame,
                             call_frame_t *prev_frame,
                             xlator_t *this,
                             int32_t op_ret,
                             int32_t op_errno,
                             ...);

struct _call_pool_t {
        union {
                struct list_head   all_frames;
                struct {
                        call_stack_t *next_call;
                        call_stack_t *prev_call;
                } all_stacks;
        };
        int64_t                     cnt;
        gf_lock_t                   lock;
        struct mem_pool             *frame_mem_pool;
        struct mem_pool             *stack_mem_pool;
};

struct _call_frame_t {
        call_stack_t *root;        /* stack root */
        call_frame_t *parent;      /* previous BP */
        call_frame_t *next;
        call_frame_t *prev;        /* maintenance list */
        void         *local;       /* local variables */
        xlator_t     *this;        /* implicit object */
        ret_fn_t      ret;         /* op_return address */
        int32_t       ref_count;
        gf_lock_t     lock;
        void         *cookie;      /* unique cookie */
        gf_boolean_t  complete;

        glusterfs_fop_t op;
        struct timeval begin;      /* when this frame was created */
        struct timeval end;        /* when this frame completed */
        const char      *wind_from;
        const char      *wind_to;
        const char      *unwind_from;
        const char      *unwind_to;
};

struct _call_stack_t {
        union {
                struct list_head      all_frames;
                struct {
                        call_stack_t *next_call;
                        call_stack_t *prev_call;
                };
        };
        call_pool_t                  *pool;
        void                         *trans;
        uint64_t                      unique;
        void                         *state;  /* pointer to request state */
        uid_t                         uid;
        gid_t                         gid;
        pid_t                         pid;
        uint32_t                      ngrps;
        uint32_t                      groups[GF_REQUEST_MAXGROUPS];
        uint64_t                      lk_owner;

        call_frame_t                  frames;

        int32_t                       op;
        int8_t                        type;
};


#define frame_set_uid_gid(frm, u, g)            \
        do {                                    \
                if (frm) {                      \
                        (frm)->root->uid = u;   \
                        (frm)->root->gid = g;   \
                        (frm)->root->ngrps = 0; \
                }                               \
        } while (0);                            \


struct xlator_fops;


void
gf_latency_begin (call_frame_t *frame, void *fn);

void
gf_latency_end (call_frame_t *frame);


static inline void
FRAME_DESTROY (call_frame_t *frame)
{
        void *local = NULL;
        if (frame->next)
                frame->next->prev = frame->prev;
        if (frame->prev)
                frame->prev->next = frame->next;
        if (frame->local) {
                local = frame->local;
                frame->local = NULL;

        }

        LOCK_DESTROY (&frame->lock);
        mem_put (frame);

        if (local)
                GF_FREE (local);
}


static inline void
STACK_DESTROY (call_stack_t *stack)
{
        void *local = NULL;

        LOCK (&stack->pool->lock);
        {
                list_del_init (&stack->all_frames);
                stack->pool->cnt--;
        }
        UNLOCK (&stack->pool->lock);

        if (stack->frames.local) {
                local = stack->frames.local;
                stack->frames.local = NULL;
        }

        LOCK_DESTROY (&stack->frames.lock);

        while (stack->frames.next) {
                FRAME_DESTROY (stack->frames.next);
        }
        mem_put (stack);

        if (local)
                GF_FREE (local);
}


#define cbk(x) cbk_##x

#define FRAME_SU_DO(frm, local_type)                                   \
        do {                                                           \
                local_type *__local = (frm)->local;                 \
                __local->uid = frm->root->uid;                         \
                __local->gid = frm->root->gid;                         \
                frm->root->uid = 0;                                    \
                frm->root->gid = 0;                                    \
        } while (0);                                                   \

#define FRAME_SU_UNDO(frm, local_type)                                 \
        do {                                                           \
                local_type *__local = (frm)->local;                 \
                frm->root->uid = __local->uid;                         \
                frm->root->gid = __local->gid;                         \
        } while (0);                                                   \


/* make a call */
#define STACK_WIND(frame, rfn, obj, fn, params ...)                     \
        do {                                                            \
                call_frame_t *_new = NULL;                              \
                xlator_t     *old_THIS = NULL;                          \
                                                                        \
                _new = mem_get0 (frame->root->pool->frame_mem_pool);    \
                if (!_new) {                                            \
                        gf_log ("stack", GF_LOG_ERROR, "alloc failed"); \
                        break;                                          \
                }                                                       \
                typeof(fn##_cbk) tmp_cbk = rfn;                         \
                _new->root = frame->root;                               \
                _new->next = frame->root->frames.next;                  \
                _new->prev = &frame->root->frames;                      \
                if (frame->root->frames.next)                           \
                        frame->root->frames.next->prev = _new;          \
                frame->root->frames.next = _new;                        \
                _new->this = obj;                                       \
                _new->ret = (ret_fn_t) tmp_cbk;                         \
                _new->parent = frame;                                   \
                _new->cookie = _new;                                    \
                LOCK_INIT (&_new->lock);                                \
                _new->wind_from = __FUNCTION__;                         \
                _new->wind_to = #fn;                                    \
                _new->unwind_to = #rfn;                                 \
                frame->ref_count++;                                     \
                old_THIS = THIS;                                        \
                THIS = obj;                                             \
                if (frame->this->ctx->measure_latency)                  \
                        gf_latency_begin (_new, fn);                    \
                fn (_new, obj, params);                                 \
                THIS = old_THIS;                                        \
        } while (0)


/* make a call with a cookie */
#define STACK_WIND_COOKIE(frame, rfn, cky, obj, fn, params ...)         \
        do {                                                            \
                call_frame_t *_new = NULL;                              \
                xlator_t     *old_THIS = NULL;                          \
                                                                        \
                _new = mem_get0 (frame->root->pool->frame_mem_pool);    \
                if (!_new) {                                            \
                        gf_log ("stack", GF_LOG_ERROR, "alloc failed"); \
                        break;                                          \
                }                                                       \
                typeof(fn##_cbk) tmp_cbk = rfn;                         \
                _new->root = frame->root;                               \
                _new->next = frame->root->frames.next;                  \
                _new->prev = &frame->root->frames;                      \
                if (frame->root->frames.next)                           \
                        frame->root->frames.next->prev = _new;          \
                frame->root->frames.next = _new;                        \
                _new->this = obj;                                       \
                _new->ret = (ret_fn_t) tmp_cbk;                         \
                _new->parent = frame;                                   \
                _new->cookie = cky;                                     \
                LOCK_INIT (&_new->lock);                                \
                _new->wind_from = __FUNCTION__;                         \
                _new->wind_to = #fn;                                    \
                _new->unwind_to = #rfn;                                 \
                frame->ref_count++;                                     \
                fn##_cbk = rfn;                                         \
                old_THIS = THIS;                                        \
                THIS = obj;                                             \
                if (obj->ctx->measure_latency)                          \
                        gf_latency_begin (_new, fn);                    \
                fn (_new, obj, params);                                 \
                THIS = old_THIS;                                        \
        } while (0)


/* return from function */
#define STACK_UNWIND(frame, params ...)                                 \
        do {                                                            \
                ret_fn_t      fn = NULL;                                \
                call_frame_t *_parent = NULL;                           \
                xlator_t     *old_THIS = NULL;                          \
                if (!frame) {                                           \
                        gf_log ("stack", GF_LOG_CRITICAL, "!frame");    \
                        break;                                          \
                }                                                       \
                fn = frame->ret;                                        \
                _parent = frame->parent;                                \
                _parent->ref_count--;                                   \
                old_THIS = THIS;                                        \
                THIS = _parent->this;                                   \
                frame->complete = _gf_true;                             \
                frame->unwind_from = __FUNCTION__;                      \
                if (frame->this->ctx->measure_latency)                  \
                        gf_latency_end (frame);                         \
                fn (_parent, frame->cookie, _parent->this, params);     \
                THIS = old_THIS;                                        \
        } while (0)


/* return from function in type-safe way */
#define STACK_UNWIND_STRICT(op, frame, params ...)                      \
        do {                                                            \
                fop_##op##_cbk_t      fn = NULL;                        \
                call_frame_t *_parent = NULL;                           \
                xlator_t     *old_THIS = NULL;                          \
                                                                        \
                if (!frame) {                                           \
                        gf_log ("stack", GF_LOG_CRITICAL, "!frame");    \
                        break;                                          \
                }                                                       \
                fn = (fop_##op##_cbk_t )frame->ret;                     \
                _parent = frame->parent;                                \
                _parent->ref_count--;                                   \
                old_THIS = THIS;                                        \
                THIS = _parent->this;                                   \
                frame->complete = _gf_true;                             \
                frame->unwind_from = __FUNCTION__;                      \
                if (frame->this->ctx->measure_latency)                  \
                        gf_latency_end (frame);                         \
                fn (_parent, frame->cookie, _parent->this, params);     \
                THIS = old_THIS;                                        \
        } while (0)


static inline call_frame_t *
copy_frame (call_frame_t *frame)
{
        call_stack_t *newstack = NULL;
        call_stack_t *oldstack = NULL;

        if (!frame) {
                return NULL;
        }

        newstack = mem_get0 (frame->root->pool->stack_mem_pool);
        if (newstack == NULL) {
                return NULL;
        }

        oldstack = frame->root;

        newstack->uid = oldstack->uid;
        newstack->gid = oldstack->gid;
        newstack->pid = oldstack->pid;
        newstack->ngrps = oldstack->ngrps;
        newstack->op  = oldstack->op;
        newstack->type = oldstack->type;
        memcpy (newstack->groups, oldstack->groups,
                sizeof (uint32_t) * GF_REQUEST_MAXGROUPS);
        newstack->unique = oldstack->unique;

        newstack->frames.this = frame->this;
        newstack->frames.root = newstack;
        newstack->pool = oldstack->pool;
        newstack->lk_owner = oldstack->lk_owner;

        LOCK_INIT (&newstack->frames.lock);

        LOCK (&oldstack->pool->lock);
        {
                list_add (&newstack->all_frames, &oldstack->all_frames);
                newstack->pool->cnt++;
        }
        UNLOCK (&oldstack->pool->lock);

        return &newstack->frames;
}


static inline call_frame_t *
create_frame (xlator_t *xl, call_pool_t *pool)
{
        call_stack_t    *stack = NULL;

        if (!xl || !pool) {
                return NULL;
        }

        stack = mem_get0 (pool->stack_mem_pool);
        if (!stack)
                return NULL;

        stack->pool = pool;
        stack->frames.root = stack;
        stack->frames.this = xl;

        LOCK (&pool->lock);
        {
                list_add (&stack->all_frames, &pool->all_frames);
                pool->cnt++;
        }
        UNLOCK (&pool->lock);

        LOCK_INIT (&stack->frames.lock);

        return &stack->frames;
}

void gf_proc_dump_pending_frames(call_pool_t *call_pool);

gf_boolean_t __is_fuse_call (call_frame_t *frame);
#endif /* _STACK_H */
