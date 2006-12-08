
#ifndef _STACK_H
#define _STACK_H

struct _call_ctx_t;
typedef struct _call_ctx_t call_ctx_t;
struct _call_frame_t;
typedef struct _call_frame_t call_frame_t;

#include "xlator.h"

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
};
	     
struct _call_ctx_t {
  uint64_t unique;
  void *state;           /* pointer to request state */
  uid_t uid;
  gid_t gid;
  pid_t pid;
  call_frame_t frames;
  dict_t *reply;
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
  frame->ref_count++;                                  \
                                                       \
  fn (_new, obj, params);                              \
} while (0)


#define STACK_UNWIND(frame, params ...)     \
do {                                        \
  ret_fn_t fn = frame->ret;                 \
  call_frame_t *_parent = frame->parent;    \
  fn (_parent, frame, _parent->this, params);      \
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
