#ifndef __LIBGLUSTERFS_CLIENT_INTERNALS_H
#define __LIBGLUSTERFS_CLIENT_INTERNALS_H

#include <glusterfs.h>
#include <logging.h>
#include <inode.h>
#include <pthread.h>
#include <stack.h>
#include <list.h>
#include <signal.h>
#include <call-stub.h>
#include <sys/time.h>
#include <sys/resource.h>

typedef void (*sighandler_t) (int);
typedef struct list_head list_head_t;

typedef struct libglusterfs_client_ctx {
  glusterfs_ctx_t gf_ctx;
  inode_table_t *itable;
  pthread_t reply_thread;
  call_pool_t pool;
  uint32_t counter;
  time_t lookup_timeout;
  time_t stat_timeout;
}libglusterfs_client_ctx_t;

typedef struct signal_handler {
  int signo;
  sighandler_t handler;
  list_head_t next;
}libgf_client_signal_handler_t ;

typedef struct {
  pthread_mutex_t lock;
  pthread_cond_t reply_cond;
  call_stub_t *reply_stub;
  char complete;
  union {
    struct {
      char is_revalidate;
      loc_t *loc;
      int32_t size;
    } lookup;
  }fop;
}libgf_client_local_t;

/* typedef struct libglusterfs_client_ctx * libglusterfs_handle_t; */
/*
void debug_fun (void *ptr);

#define debug_fun_macro(param, params ...) \
do { \
     debug_fun (param); \
}while (0)
*/

#define LIBGF_STACK_WIND_AND_WAIT(frame, rfn, obj, fn, params ...)         \
do {                                                                       \
      STACK_WIND (frame, rfn, obj, fn, params);                            \
      pthread_mutex_lock (&local->lock);                                   \
      {                                                                    \
         while (!local->complete) {                                        \
	   pthread_cond_wait (&local->reply_cond, &local->lock);           \
         }                                                                 \
      }                                                                    \
      pthread_mutex_unlock (&local->lock);                                 \
} while (0)



#define LIBGF_CLIENT_SIGNAL(signal_handler_list, signo, handler)                     \
do {                                                                                 \
  libgf_client_signal_handler_t *libgf_handler = calloc (1, sizeof (*libgf_handler));\
  ERR_ABORT (libgf_handler);						             \
  libgf_handler->signo = signo;                                                      \
  libgf_handler->handler = signal (signo, handler);                                  \
  list_add (&libgf_handler->next, signal_handler_list);                              \
} while (0)                                                           

#define LIBGF_INSTALL_SIGNAL_HANDLERS(signal_handlers)                      \
do {                                                                        \
  INIT_LIST_HEAD (&signal_handlers);                                        \
  /* Handle SIGABORT and SIGSEGV */                                         \
  LIBGF_CLIENT_SIGNAL (&signal_handlers, SIGSEGV, gf_print_trace);          \
  LIBGF_CLIENT_SIGNAL (&signal_handlers, SIGABRT, gf_print_trace);          \
  LIBGF_CLIENT_SIGNAL (&signal_handlers, SIGHUP, gf_log_logrotate);         \
  /* LIBGF_CLIENT_SIGNAL (SIGTERM, glusterfs_cleanup_and_exit); */          \
} while (0)

#define LIBGF_RESTORE_SIGNAL_HANDLERS(local)                               \
do {                                                                       \
  libgf_client_signal_handler_t *ptr = NULL, *tmp = NULL;                  \
  list_for_each_entry_safe (ptr, tmp, &local->signal_handlers, next) {     \
    signal (ptr->signo, ptr->handler);                                     \
    FREE (ptr);                                                           \
  }                                                                        \
} while (0)                                       

#define LIBGF_CLIENT_FOP_ASYNC(ctx, local, ret_fn, op, args ...)	\
do {									   \
  call_frame_t *frame = get_call_frame_for_req (ctx, 1);                   \
  xlator_t *xl = frame->this->children ?                                   \
                        frame->this->children->xlator : NULL;              \
  dict_t *refs = frame->root->req_refs;                                    \
  frame->root->state = ctx;                                                \
  frame->local = local;                                                  \
  STACK_WIND (frame, ret_fn, xl, xl->fops->op, args);                      \
  dict_unref (refs);                                                       \
} while (0)

#define LIBGF_CLIENT_FOP(ctx, stub, op, local, args ...)                   \
do {                                                                       \
  call_frame_t *frame = get_call_frame_for_req (ctx, 1);                   \
  xlator_t *xl = frame->this->children ?                                   \
                        frame->this->children->xlator : NULL;              \
  dict_t *refs = frame->root->req_refs;                                    \
  if (!local) {                                                            \
    frame->local = calloc (1, sizeof (*local));                            \
  } else {                                                                 \
    frame->local = local;                                                  \
  }                                                                        \
  ERR_ABORT (local);                                                       \
  frame->root->state = ctx;                                                \
  frame->local = local;                                                    \
  pthread_cond_init (&local->reply_cond, NULL);                            \
  pthread_mutex_init (&local->lock, NULL);                                 \
  LIBGF_STACK_WIND_AND_WAIT (frame, libgf_client_##op##_cbk, xl, xl->fops->op, args); \
  dict_unref (refs);                                                       \
  stub = local->reply_stub;                                                \
  FREE (frame->local);                                                    \
  frame->local = NULL;                                                     \
  STACK_DESTROY (frame->root);                                             \
} while (0)

#endif
