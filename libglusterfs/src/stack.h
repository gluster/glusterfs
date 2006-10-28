
#ifndef _STACK_H
#define _STACK_H

struct _call_ctx_t;
typedef struct _call_ctx_t call_ctx_t;
struct _call_frame_t;
typedef struct _call_frame_t call_frame_t;

typedef int32_t (*ret_fn_t) (call_frame_t *frame,
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
};


#define FRAME_DESTROY(frame)         \
do {                                 \
  if (frame->next)                   \
    frame->next->prev = frame->prev; \
  if (frame->prev)                   \
    frame->prev->next = frame->next; \
  if (frame->local)                  \
    dict_destroy (frame->local);     \
  if (frame->parent)                 \
    frame->parent->ref_count--;      \
  free (frame);                      \
} while (0)


#define STACK_DESTROY(cctx)            \
do {                                   \
  if (cctx->frames.local)              \
    dict_destroy (cctx->frames.local); \
  while (cctx->frames.next) {          \
    FRAME_DESTROY (cctx->frames.next); \
  }                                    \
  free (cctx);                         \
} while (0)

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
  fn (_parent, _parent->this, params);      \
} while (0)


#endif /* _STACK_H */
