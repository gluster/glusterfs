/*
  (C) 2006,2007 Z RESEARCH Inc. <http://www.zresearch.com>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

/*
  This file defines MACROS and static inlines used to emulate a function
  call over asynchronous communication with remote server
*/

#ifndef _STACK_H
#define _STACK_H

struct _call_ctx_t;
typedef struct _call_ctx_t call_ctx_t;
struct _call_frame_t;
typedef struct _call_frame_t call_frame_t;

#include "xlator.h"
#include "dict.h"

typedef int32_t (*ret_fn_t) (call_frame_t *frame,
			     call_frame_t *prev_frame,
			     xlator_t *this,
			     int32_t op_ret,
			     int32_t op_errno,
			     ...);

struct _call_frame_t {
  call_ctx_t *root;      /* stack root */
  call_frame_t *parent;  /* previous BP */
  call_frame_t *next;    /* */
  call_frame_t *prev;    /* maintainence list */
  void *local;           /* local variables */
  xlator_t *this;        /* implicit object */
  ret_fn_t ret;          /* op_return address */
  int32_t ref_count;
  pthread_mutex_t mutex;
  void *cookie;           /* unique cookie */
};
	     
struct _call_ctx_t {
  uint64_t unique;
  void *state;           /* pointer to request state */
  uid_t uid;
  gid_t gid;
  pid_t pid;
  call_frame_t frames;
  dict_t *req_refs;
  dict_t *rsp_refs;
};

static inline void
FRAME_DESTROY (call_frame_t *frame)
{
  if (frame->next)
    frame->next->prev = frame->prev;
  if (frame->prev)
    frame->prev->next = frame->next;
  if (frame->local)
    free (frame->local);
  if (frame->parent)
    frame->parent->ref_count--;
  free (frame);
}

static inline void
STACK_DESTROY (call_ctx_t *cctx)
{                                   
  if (cctx->frames.local)
    free (cctx->frames.local);
  while (cctx->frames.next) {
    FRAME_DESTROY (cctx->frames.next);
  }
  free (cctx);
}

/* make a call */
#define STACK_WIND(frame, rfn, obj, fn, params ...)    \
do {                                                   \
  call_frame_t *_new = calloc (1,                      \
			       sizeof (call_frame_t)); \
  _new->root = frame->root;                            \
  _new->next = frame->root->frames.next;               \
  _new->prev = &frame->root->frames;                   \
  if (frame->root->frames.next)                        \
    frame->root->frames.next->prev = _new;             \
  frame->root->frames.next = _new;                     \
  _new->this = obj;                                    \
  _new->ret = (ret_fn_t) rfn;                          \
  _new->parent = frame;                                \
  _new->cookie = _new;                                 \
  frame->ref_count++;                                  \
                                                       \
  fn (_new, obj, params);                              \
} while (0)

/* make a call with a cookie */
#define _STACK_WIND(frame, rfn, cky, obj, fn, params ...)   \
do {                                                        \
  call_frame_t *_new = calloc (1,                           \
			       sizeof (call_frame_t));      \
  _new->root = frame->root;                                 \
  _new->next = frame->root->frames.next;                    \
  _new->prev = &frame->root->frames;                        \
  if (frame->root->frames.next)                             \
    frame->root->frames.next->prev = _new;                  \
  frame->root->frames.next = _new;                          \
  _new->this = obj;                                         \
  _new->ret = (ret_fn_t) rfn;                               \
  _new->parent = frame;                                     \
  _new->cookie = cky;                                       \
  frame->ref_count++;                                       \
                                                            \
  fn (_new, obj, params);                                   \
} while (0)


/* return from function */
#define STACK_UNWIND(frame, params ...)               \
do {                                                  \
  ret_fn_t fn = frame->ret;                           \
  call_frame_t *_parent = frame->parent;              \
  fn (_parent, frame->cookie, _parent->this, params); \
} while (0)

static inline call_frame_t *
copy_frame (call_frame_t *frame)
{
  call_ctx_t *newctx = (void *) calloc (1, sizeof (*newctx));
  call_ctx_t *oldctx = frame->root;

  newctx->uid = oldctx->uid;
  newctx->gid = oldctx->gid;
  newctx->pid = oldctx->pid;
  newctx->unique = oldctx->unique;

  newctx->frames.this = frame->this;
  newctx->frames.root = newctx;
  return &newctx->frames;
}
#endif /* _STACK_H */
