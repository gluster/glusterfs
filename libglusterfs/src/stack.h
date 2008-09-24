/*
  Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

struct _call_ctx_t;
typedef struct _call_ctx_t call_ctx_t;
struct _call_frame_t;
typedef struct _call_frame_t call_frame_t;
struct _call_pool_t;
typedef struct _call_pool_t call_pool_t;

#include "xlator.h"
#include "dict.h"
#include "list.h"
#include "common-utils.h"


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
			call_ctx_t *next_call;
			call_ctx_t *prev_call;
		};
	};
	int64_t                     cnt;
	gf_lock_t                   lock;
};

struct _call_frame_t {
	call_ctx_t   *root;        /* stack root */
	call_frame_t *parent;      /* previous BP */
	call_frame_t *next;
	call_frame_t *prev;        /* maintainence list */
	void         *local;       /* local variables */
	xlator_t     *this;        /* implicit object */
	ret_fn_t      ret;         /* op_return address */
	int32_t       ref_count;
	gf_lock_t     lock;
	void         *cookie;      /* unique cookie */
	int32_t       op;          /* function signature */
	int8_t        type;
};

struct _call_ctx_t {
	union {
		struct list_head    all_frames;
		struct {
			call_ctx_t *next_call;
			call_ctx_t *prev_call;
		};
	};
	call_pool_t                *pool;
	void                       *trans;
	uint64_t                    unique;
	void                       *state;  /* pointer to request state */
	uid_t                       uid;
	gid_t                       gid;
	pid_t                       pid;
	call_frame_t                frames;
	dict_t                     *req_refs;
	dict_t                     *rsp_refs;
};


static inline void
FRAME_DESTROY (call_frame_t *frame)
{
	if (frame->next)
		frame->next->prev = frame->prev;
	if (frame->prev)
		frame->prev->next = frame->next;
	if (frame->local)
		FREE (frame->local);
	LOCK_DESTROY (&frame->lock);
	FREE (frame);
}


static inline void
STACK_DESTROY (call_ctx_t *cctx)
{
	LOCK (&cctx->pool->lock);
	{
		list_del_init (&cctx->all_frames);
		cctx->pool->cnt--;
	}
	UNLOCK (&cctx->pool->lock);

	if (cctx->frames.local)
		FREE (cctx->frames.local);

	LOCK_DESTROY (&cctx->frames.lock);

	while (cctx->frames.next) {
		FRAME_DESTROY (cctx->frames.next);
	}
	FREE (cctx);
}


#define cbk(x) cbk_##x


/* make a call */
#define STACK_WIND(frame, rfn, obj, fn, params ...)			\
	do {								\
		call_frame_t *_new = NULL;				\
		                                                        \
                _new = calloc (1, sizeof (call_frame_t));	        \
		ERR_ABORT (_new);					\
		typeof(fn##_cbk) tmp_cbk = rfn;				\
		_new->root = frame->root;				\
		_new->next = frame->root->frames.next;			\
		_new->prev = &frame->root->frames;			\
		if (frame->root->frames.next)				\
			frame->root->frames.next->prev = _new;		\
		frame->root->frames.next = _new;			\
		_new->this = obj;					\
		_new->ret = (ret_fn_t) tmp_cbk;				\
		_new->parent = frame;					\
		_new->cookie = _new;					\
		LOCK_INIT (&_new->lock);				\
		frame->ref_count++;					\
									\
		fn (_new, obj, params);					\
	} while (0)


/* make a call with a cookie */
#define STACK_WIND_COOKIE(frame, rfn, cky, obj, fn, params ...)		\
	do {								\
		call_frame_t *_new = calloc (1,				\
					     sizeof (call_frame_t));	\
		ERR_ABORT (_new);					\
		typeof(fn##_cbk) tmp_cbk = rfn;				\
		_new->root = frame->root;				\
		_new->next = frame->root->frames.next;			\
		_new->prev = &frame->root->frames;			\
		if (frame->root->frames.next)				\
			frame->root->frames.next->prev = _new;		\
		frame->root->frames.next = _new;			\
		_new->this = obj;					\
		_new->ret = (ret_fn_t) tmp_cbk;				\
		_new->parent = frame;					\
		_new->cookie = cky;					\
		LOCK_INIT (&_new->lock);				\
		frame->ref_count++;					\
		fn##_cbk = rfn;						\
									\
		fn (_new, obj, params);					\
	} while (0)


/* return from function */
#define STACK_UNWIND(frame, params ...)					\
	do {								\
		ret_fn_t fn = frame->ret;				\
		call_frame_t *_parent = frame->parent;			\
		_parent->ref_count--;					\
		fn (_parent, frame->cookie, _parent->this, params);	\
	} while (0)


static inline call_frame_t *
copy_frame (call_frame_t *frame)
{
	call_ctx_t *newctx = NULL;
	call_ctx_t *oldctx = NULL;

	if (!frame) {
		return NULL;
	}

	newctx = (void *) calloc (1, sizeof (*newctx));
	oldctx = frame->root;

	newctx->uid = oldctx->uid;
	newctx->gid = oldctx->gid;
	newctx->pid = oldctx->pid;
	newctx->unique = oldctx->unique;

	newctx->frames.this = frame->this;
	newctx->frames.root = newctx;

	LOCK (&oldctx->pool->lock);
	{
		list_add (&newctx->all_frames, &oldctx->all_frames);
		newctx->pool->cnt++;
		
	}
	UNLOCK (&oldctx->pool->lock);

	newctx->pool = oldctx->pool;

	LOCK_INIT (&newctx->frames.lock);

	return &newctx->frames;
}

static inline call_frame_t *
create_frame (xlator_t *xl, call_pool_t *pool)
{
	call_ctx_t *cctx = NULL;

	if (!xl || !pool) {
		return NULL;
	}

	cctx = calloc (1, sizeof (*cctx));
	if (!cctx)
		return NULL;

	cctx->pool = pool;
	cctx->frames.root = cctx;
	cctx->frames.this = xl;

	LOCK (&pool->lock);
	{
		list_add (&cctx->all_frames, &pool->all_frames);
		pool->cnt++;
	}
	UNLOCK (&pool->lock);

	LOCK_INIT (&cctx->frames.lock);

	return &cctx->frames;
}


#endif /* _STACK_H */
