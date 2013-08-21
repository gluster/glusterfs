/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
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
struct call_pool;
typedef struct call_pool call_pool_t;

#include <sys/time.h>

#include "xlator.h"
#include "dict.h"
#include "list.h"
#include "common-utils.h"
#include "globals.h"
#include "lkowner.h"
#include "client_t.h"

#define NFS_PID 1
#define LOW_PRIO_PROC_PID -1
typedef int32_t (*ret_fn_t) (call_frame_t *frame,
                             call_frame_t *prev_frame,
                             xlator_t *this,
                             int32_t op_ret,
                             int32_t op_errno,
                             ...);

struct call_pool {
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

#define SMALL_GROUP_COUNT 128

struct _call_stack_t {
        union {
                struct list_head      all_frames;
                struct {
                        call_stack_t *next_call;
                        call_stack_t *prev_call;
                };
        };
        call_pool_t                  *pool;
        gf_lock_t                     stack_lock;
        client_t                     *client;
        uint64_t                      unique;
        void                         *state;  /* pointer to request state */
        uid_t                         uid;
        gid_t                         gid;
        pid_t                         pid;
        uint16_t                      ngrps;
        uint32_t                      groups_small[SMALL_GROUP_COUNT];
	uint32_t                     *groups_large;
	uint32_t                     *groups;
        gf_lkowner_t                  lk_owner;
        glusterfs_ctx_t              *ctx;

        call_frame_t                  frames;

        int32_t                       op;
        int8_t                        type;
        struct timeval                tv;
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
                mem_put (local);
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
        LOCK_DESTROY (&stack->stack_lock);

        while (stack->frames.next) {
                FRAME_DESTROY (stack->frames.next);
        }

	GF_FREE (stack->groups_large);

        mem_put (stack);

        if (local)
                mem_put (local);
}

static inline void
STACK_RESET (call_stack_t *stack)
{
        void *local = NULL;

        if (stack->frames.local) {
                local = stack->frames.local;
                stack->frames.local = NULL;
        }

        while (stack->frames.next) {
                FRAME_DESTROY (stack->frames.next);
        }

        if (local)
                mem_put (local);
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
                _new->this = obj;                                       \
                _new->ret = (ret_fn_t) tmp_cbk;                         \
                _new->parent = frame;                                   \
                _new->cookie = _new;                                    \
                _new->wind_from = __FUNCTION__;                         \
                _new->wind_to = #fn;                                    \
                _new->unwind_to = #rfn;                                 \
                                                                        \
                LOCK_INIT (&_new->lock);                                \
                LOCK(&frame->root->stack_lock);                         \
                {                                                       \
                        _new->next = frame->root->frames.next;          \
                        _new->prev = &frame->root->frames;              \
                        if (frame->root->frames.next)                   \
                                frame->root->frames.next->prev = _new;  \
                        frame->root->frames.next = _new;                \
                        frame->ref_count++;                             \
                }                                                       \
                UNLOCK(&frame->root->stack_lock);                       \
                old_THIS = THIS;                                        \
                THIS = obj;                                             \
                if (frame->this->ctx->measure_latency)                  \
                        gf_latency_begin (_new, fn);                    \
                fn (_new, obj, params);                                 \
                THIS = old_THIS;                                        \
        } while (0)


/* make a call without switching frames */
#define STACK_WIND_TAIL(frame, obj, fn, params ...)                     \
        do {                                                            \
                xlator_t     *old_THIS = NULL;                          \
                                                                        \
                frame->this = obj;                                      \
                frame->wind_to = #fn;                                   \
                old_THIS = THIS;                                        \
                THIS = obj;                                             \
                fn (frame, obj, params);                                \
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
                _new->this = obj;                                       \
                _new->ret = (ret_fn_t) tmp_cbk;                         \
                _new->parent = frame;                                   \
                _new->cookie = cky;                                     \
                _new->wind_from = __FUNCTION__;                         \
                _new->wind_to = #fn;                                    \
                _new->unwind_to = #rfn;                                 \
                LOCK_INIT (&_new->lock);                                \
                LOCK(&frame->root->stack_lock);                         \
                {                                                       \
                        frame->ref_count++;                             \
                        _new->next = frame->root->frames.next;          \
                        _new->prev = &frame->root->frames;              \
                        if (frame->root->frames.next)                   \
                                frame->root->frames.next->prev = _new;  \
                        frame->root->frames.next = _new;                \
                }                                                       \
                UNLOCK(&frame->root->stack_lock);                       \
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
                LOCK(&frame->root->stack_lock);                         \
                {                                                       \
                        _parent->ref_count--;                           \
                }                                                       \
                UNLOCK(&frame->root->stack_lock);                       \
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
                LOCK(&frame->root->stack_lock);                         \
                {                                                       \
                        _parent->ref_count--;                           \
                }                                                       \
                UNLOCK(&frame->root->stack_lock);                       \
                old_THIS = THIS;                                        \
                THIS = _parent->this;                                   \
                frame->complete = _gf_true;                             \
                frame->unwind_from = __FUNCTION__;                      \
                if (frame->this->ctx->measure_latency)                  \
                        gf_latency_end (frame);                         \
                fn (_parent, frame->cookie, _parent->this, params);     \
                THIS = old_THIS;                                        \
        } while (0)


static inline int
call_stack_alloc_groups (call_stack_t *stack, int ngrps)
{
	if (ngrps <= SMALL_GROUP_COUNT) {
		stack->groups = stack->groups_small;
	} else {
		stack->groups_large = GF_CALLOC (sizeof (gid_t), ngrps,
						 gf_common_mt_groups_t);
		if (!stack->groups_large)
			return -1;
		stack->groups = stack->groups_large;
	}

	stack->ngrps = ngrps;

	return 0;
}

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
	if (call_stack_alloc_groups (newstack, oldstack->ngrps) != 0) {
		mem_put (newstack);
		return NULL;
	}
        memcpy (newstack->groups, oldstack->groups,
                sizeof (gid_t) * oldstack->ngrps);
        newstack->unique = oldstack->unique;

        newstack->frames.this = frame->this;
        newstack->frames.root = newstack;
        newstack->pool = oldstack->pool;
        newstack->lk_owner = oldstack->lk_owner;
        newstack->ctx = oldstack->ctx;

        if (newstack->ctx->measure_latency) {
                if (gettimeofday (&newstack->tv, NULL) == -1)
                        gf_log ("stack", GF_LOG_ERROR, "gettimeofday () failed."
                                " (%s)", strerror (errno));
                memcpy (&newstack->frames.begin, &newstack->tv,
                        sizeof (newstack->tv));
        }

        LOCK_INIT (&newstack->frames.lock);
        LOCK_INIT (&newstack->stack_lock);

        LOCK (&oldstack->pool->lock);
        {
                list_add (&newstack->all_frames, &oldstack->all_frames);
                newstack->pool->cnt++;
        }
        UNLOCK (&oldstack->pool->lock);

        return &newstack->frames;
}

void gf_proc_dump_pending_frames(call_pool_t *call_pool);
void gf_proc_dump_pending_frames_to_dict (call_pool_t *call_pool,
                                          dict_t *dict);
call_frame_t *create_frame (xlator_t *xl, call_pool_t *pool);
gf_boolean_t __is_fuse_call (call_frame_t *frame);
#endif /* _STACK_H */
