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
#include "coroutine-synctask.h"

void
qemu_coroutine_delete (Coroutine *co_)
{
	struct synctask *synctask = NULL;
	CoroutineSynctask *cs = NULL;

	cs = DO_UPCAST(CoroutineSynctask, base, co_);
	synctask = cs->synctask;

	cs->die = 1;
	synctask_wake (synctask);

	/* Do not free either @cs or @synctask here.
	   @synctask is naturally destroyed when
	   cs_proc() returns (after "break"ing out of
	   the loop because of setting cs->die=1 above.

	   We free @cs too just before returning from
	   cs_proc()
	*/
	return;
}


CoroutineAction
qemu_coroutine_switch (Coroutine *from_, Coroutine *to_, CoroutineAction action)
{
	struct synctask *to = NULL;
	struct synctask *from = NULL;
	CoroutineSynctask *csto = NULL;
	CoroutineSynctask *csfrom = NULL;

	csto = DO_UPCAST(CoroutineSynctask, base, to_);
	csfrom = DO_UPCAST(CoroutineSynctask, base, from_);
	to = csto->synctask;
	from = csfrom->synctask;

	/* TODO: need mutex/cond guarding when making syncenv
	   multithreaded
	*/
	csfrom->run = false;
	csto->run = true;

	/* the next three lines must be in this specific order only */
	csfrom->action = action;

	synctask_wake (to);

	synctask_yield (from);

	/* the yielder set @action value in @csfrom, but for the
	   resumer it is @csto
	*/
	return csto->action;
}


int
cs_fin (int ret, call_frame_t *frame, void *opaque)
{
	/* nop */
	return 0;
}


static int
cs_proc (void *opaque)
{
	CoroutineSynctask *cs = opaque;
	struct synctask *synctask = NULL;

	synctask = synctask_get (); /* == cs->synctask */

	for (;;) {
		while (!cs->run && !cs->die)
			/* entry function (i.e cs->base.entry) will
			   not be set just yet first time. Wait for
			   caller to set it and call switch()
			*/
			synctask_yield (synctask);

		if (cs->die)
			break;

		cs->base.entry (cs->base.entry_arg);
		qemu_coroutine_switch (&cs->base, cs->base.caller,
				       COROUTINE_TERMINATE);
	}

	GF_FREE (cs);

	return 0;
}


Coroutine *
qemu_coroutine_new()
{
	qb_conf_t *conf = NULL;
	CoroutineSynctask *cs = NULL;
	struct synctask *task = NULL;

	conf = THIS->private;

	cs = GF_CALLOC (1, sizeof (*cs), gf_qb_mt_coroutinesynctask_t);
	if (!cs)
		return NULL;

	task = synctask_get ();
	/* Inherit the frame from the parent synctask, as this will
	   carry forward things like uid, gid, pid, lkowner etc. of the
	   caller properly.
	*/
	cs->synctask = synctask_create (conf->env, cs_proc, cs_fin,
					task ? task->frame : NULL, cs);
	if (!cs->synctask)
		return NULL;

	return &cs->base;
}


Coroutine *
qemu_coroutine_self()
{
	struct synctask *synctask = NULL;
	CoroutineSynctask *cs = NULL;

	synctask = synctask_get();

	cs = synctask->opaque;

	return &cs->base;
}


bool
qemu_in_coroutine ()
{
	Coroutine *co = NULL;

	co = qemu_coroutine_self ();

	return co && co->caller;
}


/* These are calls for the "top" xlator to invoke/submit
   coroutines
*/

static int
synctask_nop_cbk (int ret, call_frame_t *frame, void *opaque)
{
	return 0;
}


int
qb_synctask_wrap (void *opaque)
{
	struct synctask *task = NULL;
	CoroutineSynctask *cs = NULL;
	qb_local_t *qb_local = NULL;

	task = synctask_get ();
	cs = opaque;
	cs->synctask = task;
	qb_local = DO_UPCAST (qb_local_t, cs, cs);

	return qb_local->synctask_fn (opaque);
}


int
qb_coroutine (call_frame_t *frame, synctask_fn_t fn)
{
	qb_local_t *qb_local = NULL;
	qb_conf_t *qb_conf = NULL;

	qb_local = frame->local;
	qb_local->synctask_fn = fn;
	qb_conf = frame->this->private;

	return synctask_new (qb_conf->env, qb_synctask_wrap, synctask_nop_cbk,
			     frame, &qb_local->cs);
}
