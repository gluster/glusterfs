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

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "hashfn.h"
#include "logging.h"
#include "lock.h"

static lock_inner_t locks_granted; /* list of held locks.
				      ->path is the path the lock held on.
				      ->who is the transport_t via which
				        the lock was held.
				   */
static lock_inner_t locks_request; /* list of pending lock requests.
				      ->path is the path of lock request.
				      ->who is the call_frame_t which has
				      requested. the call_frame_t must
				      be preserved to STACK_UNWIND a
				      success message to the client
				   */

static lock_inner_t *request_tail = &locks_request;

static pthread_mutex_t locks_mutex;
static char locks_mutex_init;

static void
hold_lock()
{
  if (!locks_mutex_init) {
    locks_mutex_init = 1;
    pthread_mutex_init (&locks_mutex, NULL);
  }

  pthread_mutex_lock (&locks_mutex);
}

static void
give_lock()
{
  pthread_mutex_unlock (&locks_mutex);
}


int32_t
gf_listlocks (void)
{
  return 0;
}

static lock_inner_t *
place_lock_after (lock_inner_t *granted,
		  const char *path)
{
  int32_t ret = -1;
  int32_t len2 = 0;
  
  if (granted == NULL || path == NULL)
    {
      gf_log ("lock", GF_LOG_ERROR, "invalid argument");
      return NULL;
    }

  len2 = strlen (path);
  while (granted->next) {
    int32_t len1 = 0;
    int32_t len = 0;

    if (granted->next->path)
      len1 = strlen (granted->next->path);
    else 
      break;

    len = len1 < len2 ? len1 : len2;

    ret = strncmp (granted->next->path, path, len);
    /* held locks are in ascending order of path */
    if (ret >= 0)
      break;

    granted = granted->next;
  }

  if (!ret)
    return NULL;

  return granted;
}

static call_frame_t *
__mop_lock_impl (call_frame_t *frame,
		 xlator_t *this_xl,
		 const char *path)
{
  lock_inner_t *granted = NULL;
  lock_inner_t *this = NULL;
  lock_inner_t *hold_place = NULL;
  call_frame_t *unwind = NULL;

  if (frame == NULL || this_xl == NULL || path == NULL)
    {
      gf_log ("lock", GF_LOG_ERROR, "invalid argument");
      return NULL;
    }
  
  granted = &locks_granted;
  
  this = calloc (1, sizeof (lock_inner_t));
  ERR_ABORT (this);

  /* workaround for holding locks on not only on directories but also on files */
  asprintf ((char **)&this->path, "%s/", path);

  /*  this->path = strdup (path); */

  hold_place = place_lock_after (granted, this->path);
  
  if (!hold_place) {
    /* append to queue, lock when possible */
    this->who = frame;            /* store with call_frame_t
				     to STACK_UNWIND when lock is granted */
    request_tail->next = this;
    this->prev = request_tail;
    request_tail = this;

    gf_log ("lock", GF_LOG_DEBUG, "Lock request to %s queued", path);
    unwind = NULL;
  } else {
    /* got lock */
    this->who = frame->root->trans; /* store with transport_t
				       to force unlock when this
				       tranport_t is closed */
    this->next = hold_place->next;
    this->prev = hold_place;
    if (hold_place->next)
      hold_place->next->prev = this;
    hold_place->next = this;
    /*
      gf_log ("lock",
	    GF_LOG_DEBUG,
	    "Lock request to %s granted",
	    path);
    */
    unwind = frame;
  }

  return unwind;
}

int32_t
mop_lock_impl (call_frame_t *frame,
	       xlator_t *this,
	       const char *path)
{
  call_frame_t *unwind = NULL;

  if (frame == NULL || this == NULL || path == NULL)
    {
      gf_log ("lock", GF_LOG_ERROR, "invalid argument");
      return -1;
    }
  
  hold_lock ();
  {
    unwind = __mop_lock_impl (frame, this, path);
  }
  give_lock ();

  if (unwind)
    STACK_UNWIND (unwind, 0, 0);

  return 0;
}

static int32_t 
__mop_unlock_impl (call_frame_t *frame,
		   xlator_t *this,
		   const char *path)
{
  char *tmp_path = NULL;
  lock_inner_t *granted = NULL;
  lock_inner_t *request = NULL;
  int32_t ret = 0;
  
  if (frame == NULL || this == NULL || path == NULL)
    {
      gf_log ("lock", GF_LOG_ERROR, "invalid argument");
      return -1;
    }
  
  granted = &locks_granted;
  request = &locks_request;
  
  /* path is set for regular lock requests.
     path is NULL when called from cleanup function
     to force unlock all held locks and requests
     where lock_inner_t->who == frame->root->state
  */

  if (path) {
    asprintf (&tmp_path, "%s/", path);
    granted = granted->next;
    while (granted) {
      if (granted->path && !strcmp (granted->path, tmp_path)) {
	break;
      }
      granted = granted->next;
    }
    if (granted) {
      granted->prev->next = granted->next;
      
      if (granted->next)
	granted->next->prev = granted->prev;

      FREE (granted->path);
      FREE (granted);

      /*      gf_log ("lock",
	      GF_LOG_DEBUG,
	      "Unlocked %s",
	      path);
      */
      ret = 0;
    } else {
      gf_log ("lock", GF_LOG_WARNING,
	      "Unlock request to '%s' found no entry", path);
      ret = -1;
    }
    FREE (tmp_path);
  } else {
    /* clear held locks from this transport_t */
    granted = granted->next;
    while (granted) {
      lock_inner_t *next = granted->next;

      if (granted->who == frame->root->trans) {
	gf_log ("lock", GF_LOG_DEBUG, 
		"Forced unlock on '%s' due to transport_t death", path);

	granted->prev->next = granted->next;
	if (granted->next)
	  granted->next->prev = granted->prev;

	FREE (granted->path);
	FREE (granted);
      }
      granted = next;
    }

    /* clear pending requests from this transport_t */
    request = request->next;
    while (request) {
      lock_inner_t *next = request->next;

      if (((call_frame_t *)request->who)->root->trans == 
	  frame->root->trans) {
	call_frame_t *_frame = request->who;
	/* gf_log ("lock",
		GF_LOG_DEBUG,
		"Granted lock to '%s' by unlocking '%s'",
		request->path,
		((char *) path ? (char *)path :  "(nil)"));
	*/
	request->prev->next = request->next;
	if (request->next)
	  request->next->prev = request->prev;

	FREE (request->path);
	FREE (request);

	/* no point preserving the call context of this request */
	STACK_DESTROY(_frame->root);
      }
      request = next;
    }
    ret = 0;
  }

  return ret;
}

static call_frame_t *
__mop_lock_next ()
{
  /* process pending queue to progress as many
     requests as possible. one unlock may permit
     many requests to get a lock.
  */
  call_frame_t *unwind = NULL;
  lock_inner_t *granted = &locks_granted;
  lock_inner_t *request = &locks_request;

  request = request->next;

  while (request) {
    lock_inner_t *next = request->next;
    lock_inner_t *after = place_lock_after (granted, request->path);

    if (after) {
      call_frame_t *_frame = request->who;

      /* unlink from request queue */
      if (request->prev)
	request->prev->next = request->next;
      if (request->next)
	request->next->prev = request->prev;

      /* place in held locks at ascending order position */
      request->next = after->next;
      if (after->next)
	after->next->prev = request;
      request->prev = after;
      after->next = request;

      /* update 'who' to the transport object since
	 call_frame_t will no more be valid after
	 STACK_UNWIND'ing the success message to client
      */
      request->who = _frame->root->trans;

      /* good new delivery */
      unwind = _frame;
      break;
    }
    request = next;
  }

  if (unwind) {
    /* reset tail pointer since request queue has been modified */
    request_tail = &locks_request;
    while (request_tail->next)
      request_tail = request_tail->next;
  }

  return unwind;
}

int32_t
mop_unlock_impl (call_frame_t *frame,
		 xlator_t *this,
		 const char *path)
{
  int32_t ret;
  
  if (frame == NULL || this == NULL || path == NULL)
    {
      gf_log ("lock", GF_LOG_ERROR, "invalid argument");
      return -1;
    }
  
  hold_lock ();
  {
    ret = __mop_unlock_impl (frame, this, path);
  }
  give_lock ();

  STACK_UNWIND (frame, ret, ENOENT);

  while (1) {
    call_frame_t *unwind = NULL;

    hold_lock ();
    {
      unwind = __mop_lock_next ();
    }
    give_lock ();

    if (!unwind)
      break;

    STACK_UNWIND (unwind, 0, 0);
  }

  return 0;
}
