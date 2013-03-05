/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __COROUTINE_SYNCTASK_H
#define __COROUTINE_SYNCTASK_H

#include "syncop.h"
#include "block/coroutine_int.h"
#include "qemu-common.h"
#include "block/coroutine_int.h"

/*
  Three entities:

  synctask - glusterfs implementation of xlator friendly lightweight threads
  Coroutine - qemu coroutine API for its block drivers
  CoroutineSynctask - implementation of Coroutine using synctasks

  Coroutine is an "embedded" structure inside CoroutineSynctask, called "base".

  E.g:

  Coroutine *co;
  CoroutineSynctask *cs;
  struct synctask *synctask;

  cs == synctask->opaque;
  co == &(cs->base);
  cs = DO_UPCAST(CoroutineSynctask, base, co);
  synctask == cs->synctask;

*/

typedef struct {
	Coroutine base;
	struct synctask *synctask;
	CoroutineAction action;
	bool run;
	bool die;
} CoroutineSynctask;



#endif /* !__COROUTINE_SYNCTASK_H */
