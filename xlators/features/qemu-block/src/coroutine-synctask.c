/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "syncop.h"
#include "qemu-block-memory-types.h"

#include "qemu-block.h"

/*
 * This code serves as the bridge from the main glusterfs context to the qemu
 * coroutine context via synctask. We create a single threaded syncenv with a
 * single synctask responsible for processing a queue of coroutines. The qemu
 * code invoked from within the synctask function handlers uses the ucontext
 * coroutine implementation and scheduling logic internal to qemu. This
 * effectively donates a thread of execution to qemu and its internal coroutine
 * management.
 *
 * NOTE: The existence of concurrent synctasks has proven quite racy with regard
 * to qemu coroutine management, particularly related to the lifecycle
 * differences with top-level synctasks and internally created coroutines and
 * interactions with qemu-internal queues (and locks, in turn). We explicitly
 * disallow this scenario, via the queue, until it is more well supported.
 */

static struct {
	struct list_head	queue;
	gf_lock_t		lock;
	struct synctask		*task;
} qb_co;

static void
init_qbco()
{
	INIT_LIST_HEAD(&qb_co.queue);
	LOCK_INIT(&qb_co.lock);
}

static int
synctask_nop_cbk (int ret, call_frame_t *frame, void *opaque)
{
	return 0;
}

static int
qb_synctask_wrap (void *opaque)
{
	qb_local_t *qb_local, *tmp;

	LOCK(&qb_co.lock);

	while (!list_empty(&qb_co.queue)) {
		list_for_each_entry_safe(qb_local, tmp, &qb_co.queue, list) {
			list_del_init(&qb_local->list);
			break;
		}

		UNLOCK(&qb_co.lock);

		qb_local->synctask_fn(qb_local);
		/* qb_local is now unwound and gone! */

		LOCK(&qb_co.lock);
	}

	qb_co.task = NULL;

	UNLOCK(&qb_co.lock);

	return 0;
}

int
qb_coroutine (call_frame_t *frame, synctask_fn_t fn)
{
	qb_local_t *qb_local = NULL;
	qb_conf_t *qb_conf = NULL;
	static int init = 0;

	qb_local = frame->local;
	qb_local->synctask_fn = fn;
	qb_conf = frame->this->private;

	if (!init) {
		init = 1;
		init_qbco();
	}

	LOCK(&qb_co.lock);

	if (!qb_co.task)
		qb_co.task = synctask_create(qb_conf->env, qb_synctask_wrap,
					     synctask_nop_cbk, frame, NULL);

	list_add_tail(&qb_local->list, &qb_co.queue);

	UNLOCK(&qb_co.lock);

	return 0;
}
