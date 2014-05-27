/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __BARRIER_H__
#define __BARRIER_H__

#include "barrier-mem-types.h"
#include "xlator.h"
#include "timer.h"
#include "call-stub.h"

#define BARRIER_FOP_CBK(fop_name, label, frame, this, params ...)       \
        do {                                                            \
                barrier_priv_t         *_priv           = NULL;         \
                call_stub_t            *_stub           = NULL;         \
                gf_boolean_t            _barrier_enabled= _gf_false;    \
                struct list_head        queue           = {0, };        \
                                                                        \
                INIT_LIST_HEAD (&queue);                                \
                                                                        \
                _priv = this->private;                                  \
                GF_ASSERT (_priv);                                      \
                                                                        \
                LOCK (&_priv->lock);                                    \
                {                                                       \
                        if (_priv->barrier_enabled) {                   \
                                _barrier_enabled = _priv->barrier_enabled;\
                                                                        \
                                _stub = fop_##fop_name##_cbk_stub       \
                                        (frame,                         \
                                         barrier_##fop_name##_cbk_resume,\
                                         params);                       \
                                if (!_stub) {                           \
                                        __barrier_disable (this, &queue);\
                                        goto unlock;                    \
                                }                                       \
                                                                        \
                                __barrier_enqueue (this, _stub);        \
                        }                                               \
                }                                                       \
unlock:                                                                 \
                UNLOCK (&_priv->lock);                                  \
                                                                        \
                if (_stub)                                              \
                        goto label;                                     \
                                                                        \
                if (_barrier_enabled && !_stub) {                       \
                        gf_log (this->name, GF_LOG_CRITICAL,            \
                                "Failed to barrier FOPs, disabling "    \
                                "barrier. FOP: %s, ERROR: %s",          \
                                #fop_name, strerror (ENOMEM));          \
                        barrier_dequeue_all (this, &queue);             \
                }                                                       \
                barrier_local_free_gfid (frame);                        \
                STACK_UNWIND_STRICT (fop_name, frame, params);          \
                goto label;                                             \
        } while (0)

typedef struct {
        gf_timer_t       *timer;
        gf_boolean_t      barrier_enabled;
        gf_lock_t         lock;
        struct list_head  queue;
        struct timespec   timeout;
        uint32_t          queue_size;
} barrier_priv_t;

int __barrier_enable (xlator_t *this, barrier_priv_t *priv);
void __barrier_enqueue (xlator_t *this, call_stub_t *stub);
void __barrier_disable (xlator_t *this, struct list_head *queue);
void barrier_timeout (void *data);
void barrier_dequeue_all (xlator_t *this, struct list_head *queue);
call_stub_t *__barrier_dequeue (xlator_t *this, struct list_head *queue);

#endif
