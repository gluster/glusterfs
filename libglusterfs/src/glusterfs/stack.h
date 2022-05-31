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

struct _call_stack;
typedef struct _call_stack call_stack_t;
struct _call_frame;
typedef struct _call_frame call_frame_t;
struct call_pool;
typedef struct call_pool call_pool_t;

#include <sys/time.h>

#include "glusterfs/xlator.h"
#include "glusterfs/dict.h"
#include "glusterfs/list.h"
#include "glusterfs/client_t.h"
#include "glusterfs/libglusterfs-messages.h"
#include "glusterfs/timespec.h"

#define NFS_PID 1
#define LOW_PRIO_PROC_PID -1

#define STACK_ERR_XL_NAME(stack) (stack->err_xl ? stack->err_xl->name : "-")
#define STACK_CLIENT_NAME(stack)                                               \
    (stack->client ? stack->client->client_uid : "-")

typedef int32_t (*ret_fn_t)(call_frame_t *frame, call_frame_t *prev_frame,
                            xlator_t *this, int32_t op_ret, int32_t op_errno,
                            ...);

void
gf_frame_latency_update(call_frame_t *frame);

struct call_pool {
    struct list_head all_frames;
    int64_t cnt;
    gf_atomic_t total_count;
    gf_lock_t lock;
    struct mem_pool *frame_mem_pool;
    struct mem_pool *stack_mem_pool;
};

struct _call_frame {
    call_stack_t *root;   /* stack root */
    call_frame_t *parent; /* previous BP */
    struct list_head frames;
    struct timespec begin; /* when this frame was created */
    struct timespec end;   /* when this frame completed */
    void *local;           /* local variables */
    gf_lock_t lock;
    void *cookie;   /* unique cookie */
    xlator_t *this; /* implicit object */
    ret_fn_t ret;   /* op_return address */

    glusterfs_fop_t op;
    int32_t complete;
    const char *wind_from;
    const char *wind_to;
    const char *unwind_from;
    const char *unwind_to;
};

struct _ns_info {
    uint32_t hash;      /* Hash of the namespace from SuperFastHash */
    gf_boolean_t found; /* Set to true if we found a namespace */
};

typedef struct _ns_info ns_info_t;

#define SMALL_GROUP_COUNT 128

struct _call_stack {
    struct list_head all_frames;
    call_pool_t *pool;
    gf_lock_t stack_lock;
    client_t *client;
    uint64_t unique;
    void *state; /* pointer to request state */
    uid_t uid;
    gid_t gid;
    pid_t pid;
    char identifier[UNIX_PATH_MAX];
    uint16_t ngrps;
    int8_t type;
    uint32_t groups_small[SMALL_GROUP_COUNT];
    uint32_t *groups_large;
    uint32_t *groups;
    glusterfs_ctx_t *ctx;

    struct list_head myframes; /* List of call_frame_t that go
                                  to make the call stack */

    int32_t op;
    struct timespec tv;
    xlator_t *err_xl;
    int32_t error;

    uint32_t flags;        /* use it wisely, think of it as a mechanism to
                              send information over the wire too */
    struct timespec ctime; /* timestamp, most probably set at
                              creation of stack. */

    ns_info_t ns_info;
    gf_lkowner_t lk_owner;
};

/* call_stack flags field users */
#define MDATA_CTIME (1 << 0)
#define MDATA_MTIME (1 << 1)
#define MDATA_ATIME (1 << 2)
#define MDATA_PAR_CTIME (1 << 3)
#define MDATA_PAR_MTIME (1 << 4)
#define MDATA_PAR_ATIME (1 << 5)

static inline void
FRAME_DESTROY(call_frame_t *frame, const gf_boolean_t measure_latency)
{
    void *local = NULL;

    if (measure_latency)
        gf_frame_latency_update(frame);

    list_del_init(&frame->frames);
    if (frame->local) {
        local = frame->local;
        frame->local = NULL;
    }

    LOCK_DESTROY(&frame->lock);
    mem_put(frame);

    if (local)
        mem_put(local);
}

static inline void
STACK_DESTROY(call_stack_t *stack)
{
    call_frame_t *frame = NULL;
    call_frame_t *tmp = NULL;
    gf_boolean_t measure_latency;

    LOCK(&stack->pool->lock);
    {
        list_del_init(&stack->all_frames);
        stack->pool->cnt--;
    }
    UNLOCK(&stack->pool->lock);

    LOCK_DESTROY(&stack->stack_lock);

    measure_latency = stack->ctx->measure_latency;
    list_for_each_entry_safe(frame, tmp, &stack->myframes, frames)
    {
        FRAME_DESTROY(frame, measure_latency);
    }

    GF_FREE(stack->groups_large);

    mem_put(stack);
}

static inline void
STACK_RESET(call_stack_t *stack)
{
    call_frame_t *frame = NULL;
    call_frame_t *tmp = NULL;
    call_frame_t *last = NULL;
    struct list_head toreset;
    gf_boolean_t measure_latency;

    INIT_LIST_HEAD(&toreset);

    /* We acquire call_pool->lock only to remove the frames from this stack
     * to preserve atomicity. This synchronizes across concurrent requests
     * like statedump, STACK_DESTROY etc. */

    LOCK(&stack->pool->lock);
    {
        last = list_last_entry(&stack->myframes, call_frame_t, frames);
        list_del_init(&last->frames);
        list_splice_init(&stack->myframes, &toreset);
        list_add(&last->frames, &stack->myframes);
    }
    UNLOCK(&stack->pool->lock);

    measure_latency = stack->ctx->measure_latency;
    list_for_each_entry_safe(frame, tmp, &toreset, frames)
    {
        FRAME_DESTROY(frame, measure_latency);
    }
}

#define FRAME_SU_DO(frm, local_type)                                           \
    do {                                                                       \
        local_type *__local = (frm)->local;                                    \
        __local->uid = frm->root->uid;                                         \
        __local->gid = frm->root->gid;                                         \
        __local->pid = frm->root->pid;                                         \
        frm->root->uid = 0;                                                    \
        frm->root->gid = 0;                                                    \
        frm->root->pid = GF_CLIENT_PID_NO_ROOT_SQUASH;                         \
    } while (0);

#define FRAME_SU_UNDO(frm, local_type)                                         \
    do {                                                                       \
        local_type *__local = (frm)->local;                                    \
        frm->root->uid = __local->uid;                                         \
        frm->root->gid = __local->gid;                                         \
        frm->root->pid = __local->pid;                                         \
    } while (0);

/* NOTE: make sure to keep this as an macro, mainly because, we need 'fn'
   field here to be the proper fn ptr, so its address is valid entry in
   'xlator_fops' struct.
   To understand this, check the `xlator.h:struct xlator_fops`, and then
   see a STACK_WIND call, which generally calls `subvol->fops->fop`, so
   the address offset should give the index */

/* +1 is required as 0 means NULL fop, and we don't have a variable for it */
#define get_fop_index_from_fn(xl, fn)                                          \
    (1 + (((long)&(fn) - (long)&((xl)->fops->stat)) / sizeof(void *)))

/* NOTE: the above reason holds good here too. But notice that we are getting
 the base address of the 'stat' fop, which is the first entry in the fop
 structure. All we need to do is move as much as 'idx' fields, and get the
 actual pointer from that field. */

static inline void *
get_the_pt_fop(void *base_fop, int fop_idx)
{
    void *target_addr = (base_fop + ((fop_idx - 1) * sizeof(void *)));
    /* all below type casting is for not getting warning. */
    return (void *)*(unsigned long *)target_addr;
}

/* make a call without switching frames */
#define STACK_WIND_TAIL(frame, obj, fn, params...)                             \
    do {                                                                       \
        xlator_t *old_THIS = NULL;                                             \
        xlator_t *next_xl = obj;                                               \
        typeof(fn) next_xl_fn = fn;                                            \
        int opn = get_fop_index_from_fn((next_xl), (fn));                      \
                                                                               \
        frame->this = next_xl;                                                 \
        frame->wind_to = #fn;                                                  \
        old_THIS = THIS;                                                       \
        THIS = next_xl;                                                        \
        gf_msg_trace("stack-trace", 0,                                         \
                     "stack-address: %p, "                                     \
                     "winding from %s to %s",                                  \
                     frame->root, old_THIS->name, next_xl->name);              \
        /* Need to capture counts at leaf node */                              \
        if (!next_xl->pass_through && !next_xl->children) {                    \
            GF_ATOMIC_INC(next_xl->stats[opn].total_fop);                      \
            GF_ATOMIC_INC(next_xl->stats[opn].interval_fop);                   \
        }                                                                      \
                                                                               \
        if (next_xl->pass_through) {                                           \
            next_xl_fn = get_the_pt_fop(&next_xl->pass_through_fops->stat,     \
                                        opn);                                  \
        }                                                                      \
        next_xl_fn(frame, next_xl, params);                                    \
        THIS = old_THIS;                                                       \
    } while (0)

/* make a call */
#define STACK_WIND(frame, rfn, obj, fn, params...)                             \
    STACK_WIND_COMMON(frame, rfn, 0, NULL, obj, fn, params)

/* make a call with a cookie */
#define STACK_WIND_COOKIE(frame, rfn, cky, obj, fn, params...)                 \
    STACK_WIND_COMMON(frame, rfn, 1, cky, obj, fn, params)

/* Cookie passed as the argument can be NULL (ptr) or 0 (int). Hence we
   have to have a mechanism to separate out the two STACK_WIND formats.
   Needed a common macro, as other than for cookie, all the other code
   is common across.
 */
#define STACK_WIND_COMMON(frame, rfn, has_cookie, cky, obj, fn, params...)     \
    do {                                                                       \
        call_frame_t *_new = NULL;                                             \
        xlator_t *old_THIS = NULL;                                             \
        typeof(fn) next_xl_fn = fn;                                            \
                                                                               \
        _new = mem_get0(frame->root->pool->frame_mem_pool);                    \
        if (caa_unlikely(!_new)) {                                             \
            break;                                                             \
        }                                                                      \
        typeof(fn##_cbk) tmp_cbk = rfn;                                        \
        _new->root = frame->root;                                              \
        _new->parent = frame;                                                  \
        LOCK_INIT(&_new->lock);                                                \
        /* (void *) is required for avoiding gcc warning */                    \
        _new->cookie = ((has_cookie == 1) ? (void *)(cky) : (void *)_new);     \
        _new->this = obj;                                                      \
        _new->ret = (ret_fn_t)tmp_cbk;                                         \
        _new->wind_from = __FUNCTION__;                                        \
        _new->wind_to = #fn;                                                   \
        _new->unwind_to = #rfn;                                                \
        LOCK(&frame->root->stack_lock);                                        \
        {                                                                      \
            list_add(&_new->frames, &frame->root->myframes);                   \
        }                                                                      \
        UNLOCK(&frame->root->stack_lock);                                      \
        fn##_cbk = rfn;                                                        \
        old_THIS = THIS;                                                       \
        THIS = obj;                                                            \
        gf_msg_trace("stack-trace", 0,                                         \
                     "stack-address: %p, "                                     \
                     "winding from %s to %s",                                  \
                     frame->root, old_THIS->name, obj->name);                  \
        if (obj->ctx->measure_latency)                                         \
            timespec_now(&_new->begin);                                        \
        _new->op = get_fop_index_from_fn((_new->this), (fn));                  \
        if (!obj->pass_through) {                                              \
            GF_ATOMIC_INC(obj->stats[_new->op].total_fop);                     \
            GF_ATOMIC_INC(obj->stats[_new->op].interval_fop);                  \
        } else {                                                               \
            /* we want to get to the actual fop to call */                     \
            next_xl_fn = get_the_pt_fop(&obj->pass_through_fops->stat,         \
                                        _new->op);                             \
        }                                                                      \
        next_xl_fn(_new, obj, params);                                         \
        THIS = old_THIS;                                                       \
    } while (0)

#define STACK_UNWIND STACK_UNWIND_STRICT

/* return from function in type-safe way */
#define STACK_UNWIND_STRICT(fop, frame, op_ret, op_errno, params...)           \
    do {                                                                       \
        fop_##fop##_cbk_t fn = NULL;                                           \
        call_frame_t *_parent = NULL;                                          \
        xlator_t *old_THIS = NULL;                                             \
                                                                               \
        if (caa_unlikely(!frame)) {                                            \
            gf_msg("stack", GF_LOG_CRITICAL, 0, LG_MSG_FRAME_ERROR, "!frame"); \
            break;                                                             \
        }                                                                      \
        old_THIS = THIS;                                                       \
        if ((op_ret) < 0) {                                                    \
            gf_msg_debug("stack-trace", op_errno,                              \
                         "stack-address: %p, "                                 \
                         "%s returned %d",                                     \
                         frame->root, old_THIS->name, (int32_t)(op_ret));      \
        } else {                                                               \
            gf_msg_trace("stack-trace", 0,                                     \
                         "stack-address: %p, "                                 \
                         "%s returned %d",                                     \
                         frame->root, old_THIS->name, (int32_t)(op_ret));      \
        }                                                                      \
        fn = (fop_##fop##_cbk_t)frame->ret;                                    \
        _parent = frame->parent;                                               \
        LOCK(&frame->root->stack_lock);                                        \
        {                                                                      \
            if ((op_ret) < 0 && (op_errno) != frame->root->error) {            \
                frame->root->err_xl = frame->this;                             \
                frame->root->error = (op_errno);                               \
            } else if ((op_ret) == 0) {                                        \
                frame->root->err_xl = NULL;                                    \
                frame->root->error = 0;                                        \
            }                                                                  \
        }                                                                      \
        UNLOCK(&frame->root->stack_lock);                                      \
        THIS = _parent->this;                                                  \
        frame->complete = _gf_true;                                            \
        frame->unwind_from = __FUNCTION__;                                     \
        if (frame->this->ctx->measure_latency) {                               \
            timespec_now(&frame->end);                                         \
            /* required for top most xlator */                                 \
            if (_parent->ret == NULL)                                          \
                memcpy(&_parent->end, &frame->end, sizeof(struct timespec));   \
        }                                                                      \
        if (op_ret < 0) {                                                      \
            GF_ATOMIC_INC(_parent->this->stats[frame->op].total_fop_cbk);      \
            GF_ATOMIC_INC(_parent->this->stats[frame->op].interval_fop_cbk);   \
        }                                                                      \
        fn(_parent, frame->cookie, _parent->this, op_ret, op_errno, params);   \
        THIS = old_THIS;                                                       \
    } while (0)

static inline int
call_stack_alloc_groups(call_stack_t *stack, int ngrps)
{
    if (ngrps <= SMALL_GROUP_COUNT) {
        stack->groups = stack->groups_small;
    } else {
        GF_FREE(stack->groups_large);
        stack->groups_large = GF_CALLOC(ngrps, sizeof(gid_t),
                                        gf_common_mt_groups_t);
        if (!stack->groups_large)
            return -1;
        stack->groups = stack->groups_large;
    }

    stack->ngrps = ngrps;

    return 0;
}

static inline int
call_stack_groups_capacity(call_stack_t *stack)
{
    return max(stack->ngrps, SMALL_GROUP_COUNT);
}

static inline int
call_frames_count(call_stack_t *call_stack)
{
    call_frame_t *pos;
    int32_t count = 0;

    if (call_stack) {
        list_for_each_entry(pos, &call_stack->myframes, frames) count++;
    }

    return count;
}

static inline call_frame_t *
copy_frame(call_frame_t *frame)
{
    call_stack_t *newstack = NULL;
    call_stack_t *oldstack = NULL;
    call_frame_t *newframe = NULL;

    if (caa_unlikely(!frame)) {
        return NULL;
    }

    newstack = mem_get0(frame->root->pool->stack_mem_pool);
    if (caa_unlikely(newstack == NULL)) {
        return NULL;
    }

    INIT_LIST_HEAD(&newstack->myframes);

    newframe = mem_get0(frame->root->pool->frame_mem_pool);
    if (caa_unlikely(!newframe)) {
        mem_put(newstack);
        return NULL;
    }

    newframe->this = frame->this;
    newframe->root = newstack;
    INIT_LIST_HEAD(&newframe->frames);
    list_add(&newframe->frames, &newstack->myframes);

    oldstack = frame->root;

    newstack->uid = oldstack->uid;
    newstack->gid = oldstack->gid;
    newstack->pid = oldstack->pid;
    newstack->op = oldstack->op;
    newstack->type = oldstack->type;
    newstack->ctime = oldstack->ctime;
    newstack->flags = oldstack->flags;
    if (call_stack_alloc_groups(newstack, oldstack->ngrps) != 0) {
        mem_put(newstack);
        return NULL;
    }
    if (!oldstack->groups) {
        gf_msg_debug("stack", EINVAL, "groups is null (ngrps: %d)",
                     oldstack->ngrps);
        /* Considering 'groups' is NULL, set ngrps to 0 */
        oldstack->ngrps = 0;

        if (oldstack->groups_large)
            oldstack->groups = oldstack->groups_large;
        else
            oldstack->groups = oldstack->groups_small;
    }
    newstack->ngrps = oldstack->ngrps;
    memcpy(newstack->groups, oldstack->groups, sizeof(gid_t) * oldstack->ngrps);
    newstack->unique = oldstack->unique;
    newstack->pool = oldstack->pool;
    lk_owner_copy(&newstack->lk_owner, &oldstack->lk_owner);
    newstack->ctx = oldstack->ctx;

    if (newstack->ctx->measure_latency) {
        timespec_now(&newstack->tv);
        memcpy(&newframe->begin, &newstack->tv, sizeof(newstack->tv));
    }

    LOCK_INIT(&newframe->lock);
    LOCK_INIT(&newstack->stack_lock);

    LOCK(&oldstack->pool->lock);
    {
        list_add(&newstack->all_frames, &oldstack->all_frames);
        newstack->pool->cnt++;
    }
    UNLOCK(&oldstack->pool->lock);
    GF_ATOMIC_INC(newstack->pool->total_count);

    return newframe;
}

void
call_stack_set_groups(call_stack_t *stack, int ngrps, gid_t **groupbuf_p);
void
gf_proc_dump_pending_frames(call_pool_t *call_pool);
void
gf_proc_dump_pending_frames_to_dict(call_pool_t *call_pool, dict_t *dict);
call_frame_t *
create_frame(xlator_t *xl, call_pool_t *pool);
gf_boolean_t
__is_fuse_call(call_frame_t *frame);
#endif /* _STACK_H */
