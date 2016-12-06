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
#include "libglusterfs-messages.h"

#define NFS_PID 1
#define LOW_PRIO_PROC_PID -1

#define STACK_ERR_XL_NAME(stack) (stack->err_xl?stack->err_xl->name:"-")
#define STACK_CLIENT_NAME(stack) (stack->client?stack->client->client_uid:"-")

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
        struct list_head frames;
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
        char                          identifier[UNIX_PATH_MAX];
        uint16_t                      ngrps;
        uint32_t                      groups_small[SMALL_GROUP_COUNT];
	uint32_t                     *groups_large;
	uint32_t                     *groups;
        gf_lkowner_t                  lk_owner;
        glusterfs_ctx_t              *ctx;

        struct list_head              myframes; /* List of call_frame_t that go
                                                   to make the call stack */

        int32_t                       op;
        int8_t                        type;
        struct timeval                tv;
        xlator_t                     *err_xl;
        int32_t                       error;
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

        list_del_init (&frame->frames);
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
        call_frame_t *frame = NULL;
        call_frame_t *tmp = NULL;

        LOCK (&stack->pool->lock);
        {
                list_del_init (&stack->all_frames);
                stack->pool->cnt--;
        }
        UNLOCK (&stack->pool->lock);

        LOCK_DESTROY (&stack->stack_lock);

        list_for_each_entry_safe (frame, tmp, &stack->myframes, frames) {
                FRAME_DESTROY (frame);
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
        call_frame_t *frame = NULL;
        call_frame_t *tmp = NULL;
        call_frame_t *last = NULL;
        struct list_head toreset = {0};

        INIT_LIST_HEAD (&toreset);

        /* We acquire call_pool->lock only to remove the frames from this stack
         * to preserve atomicity. This synchronizes across concurrent requests
         * like statedump, STACK_DESTROY etc. */

        LOCK (&stack->pool->lock);
        {
                last = list_last_entry (&stack->myframes, call_frame_t, frames);
                list_del_init (&last->frames);
                list_splice_init (&stack->myframes, &toreset);
                list_add (&last->frames, &stack->myframes);
        }
        UNLOCK (&stack->pool->lock);

        list_for_each_entry_safe (frame, tmp, &toreset, frames) {
                FRAME_DESTROY (frame);
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
                        list_add (&_new->frames, &frame->root->myframes);\
                        frame->ref_count++;                             \
                }                                                       \
                UNLOCK(&frame->root->stack_lock);                       \
                old_THIS = THIS;                                        \
                THIS = obj;                                             \
                gf_msg_trace ("stack-trace", 0,                         \
                              "stack-address: %p, "                     \
                              "winding from %s to %s",                  \
                              frame->root, old_THIS->name,              \
                              THIS->name);                              \
                if (frame->this->ctx->measure_latency)                  \
                        gf_latency_begin (_new, fn);                    \
                fn (_new, obj, params);                                 \
                THIS = old_THIS;                                        \
        } while (0)


/* make a call without switching frames */
#define STACK_WIND_TAIL(frame, obj, fn, params ...)                     \
        do {                                                            \
                xlator_t     *old_THIS = NULL;                          \
                xlator_t     *next_xl = obj;                            \
                typeof(fn)    next_xl_fn = fn;                          \
                                                                        \
                frame->this = next_xl;                                  \
                frame->wind_to = #fn;                                   \
                old_THIS = THIS;                                        \
                THIS = next_xl;                                         \
                gf_msg_trace ("stack-trace", 0,                         \
                              "stack-address: %p, "                     \
                              "winding from %s to %s",                  \
                              frame->root, old_THIS->name,              \
                              THIS->name);                              \
                next_xl_fn (frame, next_xl, params);                    \
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
                        list_add (&_new->frames, &frame->root->myframes);\
                        frame->ref_count++;                             \
                }                                                       \
                UNLOCK(&frame->root->stack_lock);                       \
                fn##_cbk = rfn;                                         \
                old_THIS = THIS;                                        \
                THIS = obj;                                             \
                gf_msg_trace ("stack-trace", 0,                         \
                              "stack-address: %p, "                     \
                              "winding from %s to %s",                  \
                              frame->root, old_THIS->name,              \
                              THIS->name);                              \
                if (obj->ctx->measure_latency)                  \
                        gf_latency_begin (_new, fn);                    \
                fn (_new, obj, params);                                 \
                THIS = old_THIS;                                        \
        } while (0)


/* return from function */
#define STACK_UNWIND(frame, op_ret, op_errno, params ...)               \
        do {                                                            \
                ret_fn_t      fn = NULL;                                \
                call_frame_t *_parent = NULL;                           \
                xlator_t     *old_THIS = NULL;                          \
                if (!frame) {                                           \
                        gf_msg ("stack", GF_LOG_CRITICAL, 0,            \
                                LG_MSG_FRAME_ERROR, "!frame");          \
                        break;                                          \
                }                                                       \
                if (op_ret < 0) {                                       \
                        gf_msg_debug ("stack-trace", op_errno,          \
                                      "stack-address: %p, "             \
                                      "%s returned %d error: %s",       \
                                      frame->root, THIS->name,          \
                                      (int32_t)op_ret,                  \
                                      strerror(op_errno));              \
                } else {                                                \
                        gf_msg_trace ("stack-trace", 0,                 \
                                      "stack-address: %p, "             \
                                      "%s returned %d",                 \
                                      frame->root, THIS->name,          \
                                      (int32_t)op_ret);                 \
                }                                                       \
                fn = frame->ret;                                        \
                _parent = frame->parent;                                \
                LOCK(&frame->root->stack_lock);                         \
                {                                                       \
                        _parent->ref_count--;                           \
                        if (op_ret < 0 &&                               \
                            op_errno != frame->root->error) {           \
                                frame->root->err_xl = frame->this;      \
                                frame->root->error = op_errno;          \
                        } else if (op_ret == 0) {                       \
                                frame->root->err_xl = NULL;             \
                                frame->root->error = 0;                 \
                        }                                               \
                }                                                       \
                UNLOCK(&frame->root->stack_lock);                       \
                old_THIS = THIS;                                        \
                THIS = _parent->this;                                   \
                frame->complete = _gf_true;                             \
                frame->unwind_from = __FUNCTION__;                      \
                if (frame->this->ctx->measure_latency)                  \
                        gf_latency_end (frame);                         \
                fn (_parent, frame->cookie, _parent->this, op_ret,      \
                    op_errno, params);                                  \
                THIS = old_THIS;                                        \
        } while (0)


/* return from function in type-safe way */
#define STACK_UNWIND_STRICT(op, frame, op_ret, op_errno, params ...)    \
        do {                                                            \
                fop_##op##_cbk_t      fn = NULL;                        \
                call_frame_t *_parent = NULL;                           \
                xlator_t     *old_THIS = NULL;                          \
                                                                        \
                if (!frame) {                                           \
                        gf_msg ("stack", GF_LOG_CRITICAL, 0,            \
                                LG_MSG_FRAME_ERROR, "!frame");          \
                        break;                                          \
                }                                                       \
                if (op_ret < 0) {                                       \
                        gf_msg_debug ("stack-trace", op_errno,          \
                                      "stack-address: %p, "             \
                                      "%s returned %d error: %s",       \
                                      frame->root, THIS->name,          \
                                      (int32_t)op_ret,                  \
                                      strerror(op_errno));              \
                } else {                                                \
                        gf_msg_trace ("stack-trace", 0,                 \
                                      "stack-address: %p, "             \
                                      "%s returned %d",                 \
                                      frame->root, THIS->name,          \
                                      (int32_t)op_ret);                 \
                }                                                       \
                fn = (fop_##op##_cbk_t )frame->ret;                     \
                _parent = frame->parent;                                \
                LOCK(&frame->root->stack_lock);                         \
                {                                                       \
                        _parent->ref_count--;                           \
                        if (op_ret < 0 &&                               \
                            op_errno != frame->root->error) {           \
                                frame->root->err_xl = frame->this;      \
                                frame->root->error = op_errno;          \
                        } else if (op_ret == 0) {                       \
                                frame->root->err_xl = NULL;             \
                                frame->root->error = 0;                 \
                        }                                               \
                }                                                       \
                UNLOCK(&frame->root->stack_lock);                       \
                old_THIS = THIS;                                        \
                THIS = _parent->this;                                   \
                frame->complete = _gf_true;                             \
                frame->unwind_from = __FUNCTION__;                      \
                if (frame->this->ctx->measure_latency)                  \
                        gf_latency_end (frame);                         \
                fn (_parent, frame->cookie, _parent->this, op_ret,      \
                    op_errno, params);                                  \
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

static inline
int call_frames_count (call_stack_t *call_stack)
{
        call_frame_t *pos;
        int32_t count = 0;

        if (!call_stack)
                return count;

        list_for_each_entry (pos, &call_stack->myframes, frames)
                count++;

        return count;
}

static inline call_frame_t *
copy_frame (call_frame_t *frame)
{
        call_stack_t *newstack = NULL;
        call_stack_t *oldstack = NULL;
        call_frame_t *newframe = NULL;

        if (!frame) {
                return NULL;
        }

        newstack = mem_get0 (frame->root->pool->stack_mem_pool);
        if (newstack == NULL) {
                return NULL;
        }

        INIT_LIST_HEAD (&newstack->myframes);

        newframe = mem_get0 (frame->root->pool->frame_mem_pool);
        if (!newframe) {
                mem_put (newstack);
                return NULL;
        }

        newframe->this = frame->this;
        newframe->root = newstack;
        INIT_LIST_HEAD (&newframe->frames);
        list_add (&newframe->frames, &newstack->myframes);

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
        newstack->pool = oldstack->pool;
        newstack->lk_owner = oldstack->lk_owner;
        newstack->ctx = oldstack->ctx;

        if (newstack->ctx->measure_latency) {
                if (gettimeofday (&newstack->tv, NULL) == -1)
                        gf_msg ("stack", GF_LOG_ERROR, errno,
                                LG_MSG_GETTIMEOFDAY_FAILED,
                                "gettimeofday () failed.");
                memcpy (&newframe->begin, &newstack->tv,
                        sizeof (newstack->tv));
        }

        LOCK_INIT (&newframe->lock);
        LOCK_INIT (&newstack->stack_lock);

        LOCK (&oldstack->pool->lock);
        {
                list_add (&newstack->all_frames, &oldstack->all_frames);
                newstack->pool->cnt++;
        }
        UNLOCK (&oldstack->pool->lock);

        return newframe;
}

void gf_proc_dump_pending_frames(call_pool_t *call_pool);
void gf_proc_dump_pending_frames_to_dict (call_pool_t *call_pool,
                                          dict_t *dict);
call_frame_t *create_frame (xlator_t *xl, call_pool_t *pool);
gf_boolean_t __is_fuse_call (call_frame_t *frame);
#endif /* _STACK_H */
