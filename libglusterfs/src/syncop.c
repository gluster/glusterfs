/*
  Copyright (c) 2008-2013 Red Hat, Inc. <http://www.redhat.com>
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

#include "syncop.h"

int
syncopctx_setfsuid (void *uid)
{
	struct syncopctx *opctx = NULL;
	int               ret = 0;

	/* In args check */
	if (!uid) {
		ret = -1;
		errno = EINVAL;
		goto out;
	}

	opctx = syncopctx_getctx ();

	/* alloc for this thread the first time */
	if (!opctx) {
		opctx = GF_CALLOC (1, sizeof (*opctx), gf_common_mt_syncopctx);
		if (!opctx) {
			ret = -1;
			goto out;
		}

		ret = syncopctx_setctx (opctx);
		if (ret != 0) {
			GF_FREE (opctx);
			opctx = NULL;
			goto out;
		}
	}

out:
	if (opctx && uid) {
		opctx->uid = *(uid_t *)uid;
		opctx->valid |= SYNCOPCTX_UID;
	}

	return ret;
}

int
syncopctx_setfsgid (void *gid)
{
	struct syncopctx *opctx = NULL;
	int               ret = 0;

	/* In args check */
	if (!gid) {
		ret = -1;
		errno = EINVAL;
		goto out;
	}

	opctx = syncopctx_getctx ();

	/* alloc for this thread the first time */
	if (!opctx) {
		opctx = GF_CALLOC (1, sizeof (*opctx), gf_common_mt_syncopctx);
		if (!opctx) {
			ret = -1;
			goto out;
		}

		ret = syncopctx_setctx (opctx);
		if (ret != 0) {
			GF_FREE (opctx);
			opctx = NULL;
			goto out;
		}
	}

out:
	if (opctx && gid) {
		opctx->gid = *(gid_t *)gid;
		opctx->valid |= SYNCOPCTX_GID;
	}

	return ret;
}

int
syncopctx_setfsgroups (int count, const void *groups)
{
	struct syncopctx *opctx = NULL;
	gid_t            *tmpgroups = NULL;
	int               ret = 0;

	/* In args check */
	if (count != 0 && !groups) {
		ret = -1;
		errno = EINVAL;
		goto out;
	}

	opctx = syncopctx_getctx ();

	/* alloc for this thread the first time */
	if (!opctx) {
		opctx = GF_CALLOC (1, sizeof (*opctx), gf_common_mt_syncopctx);
		if (!opctx) {
			ret = -1;
			goto out;
		}

		ret = syncopctx_setctx (opctx);
		if (ret != 0) {
			GF_FREE (opctx);
			opctx = NULL;
			goto out;
		}
	}

	/* resize internal groups as required */
	if (count && opctx->grpsize < count) {
		if (opctx->groups) {
			tmpgroups = GF_REALLOC (opctx->groups,
						(sizeof (gid_t) * count));
			/* NOTE: Not really required to zero the reallocation,
			 * as ngrps controls the validity of data,
			 * making a note irrespective */
			if (tmpgroups == NULL) {
				opctx->grpsize = 0;
				GF_FREE (opctx->groups);
				opctx->groups = NULL;
				ret = -1;
				goto out;
			}
		}
		else {
			tmpgroups = GF_CALLOC (count, sizeof (gid_t),
					       gf_common_mt_syncopctx);
			if (tmpgroups == NULL) {
				opctx->grpsize = 0;
				ret = -1;
				goto out;
			}
		}

		opctx->groups = tmpgroups;
		opctx->grpsize = count;
	}

	/* copy out the groups passed */
	if (count)
		memcpy (opctx->groups, groups, (sizeof (gid_t) * count));

	/* set/reset the ngrps, this is where reset of groups is handled */
	opctx->ngrps = count;
	opctx->valid |= SYNCOPCTX_GROUPS;

out:
	return ret;
}

static void
__run (struct synctask *task)
{
        struct syncenv *env = NULL;

        env = task->env;

        list_del_init (&task->all_tasks);
        switch (task->state) {
        case SYNCTASK_INIT:
        case SYNCTASK_SUSPEND:
                break;
        case SYNCTASK_RUN:
                gf_log (task->xl->name, GF_LOG_DEBUG,
                        "re-running already running task");
                env->runcount--;
                break;
        case SYNCTASK_WAIT:
                env->waitcount--;
                break;
        case SYNCTASK_DONE:
                gf_log (task->xl->name, GF_LOG_WARNING,
                        "running completed task");
		return;
	case SYNCTASK_ZOMBIE:
		gf_log (task->xl->name, GF_LOG_WARNING,
			"attempted to wake up zombie!!");
		return;
        }

        list_add_tail (&task->all_tasks, &env->runq);
        env->runcount++;
        task->state = SYNCTASK_RUN;
}


static void
__wait (struct synctask *task)
{
        struct syncenv *env = NULL;

        env = task->env;

        list_del_init (&task->all_tasks);
        switch (task->state) {
        case SYNCTASK_INIT:
        case SYNCTASK_SUSPEND:
                break;
        case SYNCTASK_RUN:
                env->runcount--;
                break;
        case SYNCTASK_WAIT:
                gf_log (task->xl->name, GF_LOG_WARNING,
                        "re-waiting already waiting task");
                env->waitcount--;
                break;
        case SYNCTASK_DONE:
                gf_log (task->xl->name, GF_LOG_WARNING,
                        "running completed task");
                return;
	case SYNCTASK_ZOMBIE:
		gf_log (task->xl->name, GF_LOG_WARNING,
			"attempted to sleep a zombie!!");
		return;
        }

        list_add_tail (&task->all_tasks, &env->waitq);
        env->waitcount++;
        task->state = SYNCTASK_WAIT;
}


void
synctask_yield (struct synctask *task)
{
        xlator_t *oldTHIS = THIS;

#if defined(__NetBSD__) && defined(_UC_TLSBASE)
        /* Preserve pthread private pointer through swapcontex() */
        task->proc->sched.uc_flags &= ~_UC_TLSBASE;
#endif

        if (task->state != SYNCTASK_DONE)
                task->state = SYNCTASK_SUSPEND;
        if (swapcontext (&task->ctx, &task->proc->sched) < 0) {
                gf_log ("syncop", GF_LOG_ERROR,
                        "swapcontext failed (%s)", strerror (errno));
        }

        THIS = oldTHIS;
}


void
synctask_wake (struct synctask *task)
{
        struct syncenv *env = NULL;

        env = task->env;

        pthread_mutex_lock (&env->mutex);
        {
                task->woken = 1;

                if (task->slept)
                        __run (task);

		pthread_cond_broadcast (&env->cond);
        }
        pthread_mutex_unlock (&env->mutex);
}

void
synctask_wrap (struct synctask *old_task)
{
        struct synctask *task = NULL;

        /* Do not trust the pointer received. It may be
           wrong and can lead to crashes. */

        task = synctask_get ();
        task->ret = task->syncfn (task->opaque);
        if (task->synccbk)
                task->synccbk (task->ret, task->frame, task->opaque);

        task->state = SYNCTASK_DONE;

        synctask_yield (task);
}


void
synctask_destroy (struct synctask *task)
{
        if (!task)
                return;

        FREE (task->stack);

        if (task->opframe)
                STACK_DESTROY (task->opframe->root);

        if (task->synccbk == NULL) {
               pthread_mutex_destroy (&task->mutex);
               pthread_cond_destroy (&task->cond);
        }

        FREE (task);
}


void
synctask_done (struct synctask *task)
{
        if (task->synccbk) {
                synctask_destroy (task);
                return;
        }

        pthread_mutex_lock (&task->mutex);
        {
		task->state = SYNCTASK_ZOMBIE;
                task->done = 1;
                pthread_cond_broadcast (&task->cond);
        }
        pthread_mutex_unlock (&task->mutex);
}


int
synctask_setid (struct synctask *task, uid_t uid, gid_t gid)
{
        if (!task)
                return -1;

        if (uid != -1)
                task->uid = uid;

        if (gid != -1)
                task->gid = gid;

        return 0;
}


struct synctask *
synctask_create (struct syncenv *env, synctask_fn_t fn, synctask_cbk_t cbk,
		 call_frame_t *frame, void *opaque)
{
        struct synctask *newtask = NULL;
        xlator_t        *this    = THIS;

        VALIDATE_OR_GOTO (env, err);
        VALIDATE_OR_GOTO (fn, err);

        newtask = CALLOC (1, sizeof (*newtask));
        if (!newtask)
                return NULL;

        newtask->frame      = frame;
        if (!frame) {
                newtask->opframe = create_frame (this, this->ctx->pool);
        } else {
                newtask->opframe = copy_frame (frame);
        }
        if (!newtask->opframe)
                goto err;
        newtask->env        = env;
        newtask->xl         = this;
        newtask->syncfn     = fn;
        newtask->synccbk    = cbk;
        newtask->opaque     = opaque;

        /* default to the uid/gid of the passed frame */
        newtask->uid = newtask->opframe->root->uid;
        newtask->gid = newtask->opframe->root->gid;

        INIT_LIST_HEAD (&newtask->all_tasks);
        INIT_LIST_HEAD (&newtask->waitq);

        if (getcontext (&newtask->ctx) < 0) {
                gf_log ("syncop", GF_LOG_ERROR,
                        "getcontext failed (%s)",
                        strerror (errno));
                goto err;
        }

        newtask->stack = CALLOC (1, env->stacksize);
        if (!newtask->stack) {
                gf_log ("syncop", GF_LOG_ERROR,
                        "out of memory for stack");
                goto err;
        }

        newtask->ctx.uc_stack.ss_sp   = newtask->stack;
        newtask->ctx.uc_stack.ss_size = env->stacksize;

        makecontext (&newtask->ctx, (void (*)(void)) synctask_wrap, 2, newtask);

        newtask->state = SYNCTASK_INIT;

        newtask->slept = 1;

        if (!cbk) {
                pthread_mutex_init (&newtask->mutex, NULL);
                pthread_cond_init (&newtask->cond, NULL);
                newtask->done = 0;
        }

        synctask_wake (newtask);
        /*
         * Make sure someone's there to execute anything we just put on the
         * run queue.
         */
        syncenv_scale(env);

	return newtask;
err:
        if (newtask) {
                FREE (newtask->stack);
                if (newtask->opframe)
                        STACK_DESTROY (newtask->opframe->root);
                FREE (newtask);
        }

        return NULL;
}


int
synctask_join (struct synctask *task)
{
	int ret = 0;

	pthread_mutex_lock (&task->mutex);
	{
		while (!task->done)
			pthread_cond_wait (&task->cond, &task->mutex);
	}
	pthread_mutex_unlock (&task->mutex);

	ret = task->ret;

	synctask_destroy (task);

	return ret;
}


int
synctask_new (struct syncenv *env, synctask_fn_t fn, synctask_cbk_t cbk,
              call_frame_t *frame, void *opaque)
{
	struct synctask *newtask = NULL;
	int              ret = 0;

	newtask = synctask_create (env, fn, cbk, frame, opaque);
	if (!newtask)
		return -1;

        if (!cbk)
		ret = synctask_join (newtask);

        return ret;
}


struct synctask *
syncenv_task (struct syncproc *proc)
{
        struct syncenv   *env = NULL;
        struct synctask  *task = NULL;
        struct timespec   sleep_till = {0, };
        int               ret = 0;

        env = proc->env;

        pthread_mutex_lock (&env->mutex);
        {
                while (list_empty (&env->runq)) {
                        sleep_till.tv_sec = time (NULL) + SYNCPROC_IDLE_TIME;
                        ret = pthread_cond_timedwait (&env->cond, &env->mutex,
                                                      &sleep_till);
                        if (!list_empty (&env->runq))
                                break;
                        if ((ret == ETIMEDOUT) &&
                            (env->procs > env->procmin)) {
                                task = NULL;
                                env->procs--;
                                memset (proc, 0, sizeof (*proc));
                                goto unlock;
                        }
                }

                task = list_entry (env->runq.next, struct synctask, all_tasks);

                list_del_init (&task->all_tasks);
                env->runcount--;

                task->woken = 0;
                task->slept = 0;

                task->proc = proc;
        }
unlock:
        pthread_mutex_unlock (&env->mutex);

        return task;
}


void
synctask_switchto (struct synctask *task)
{
        struct syncenv *env = NULL;

        env = task->env;

        synctask_set (task);
        THIS = task->xl;

#if defined(__NetBSD__) && defined(_UC_TLSBASE)
        /* Preserve pthread private pointer through swapcontex() */
        task->ctx.uc_flags &= ~_UC_TLSBASE;
#endif

        if (swapcontext (&task->proc->sched, &task->ctx) < 0) {
                gf_log ("syncop", GF_LOG_ERROR,
                        "swapcontext failed (%s)", strerror (errno));
        }

        if (task->state == SYNCTASK_DONE) {
                synctask_done (task);
                return;
        }

        pthread_mutex_lock (&env->mutex);
        {
                if (task->woken) {
                        __run (task);
                } else {
                        task->slept = 1;
                        __wait (task);
                }
        }
        pthread_mutex_unlock (&env->mutex);
}

void *
syncenv_processor (void *thdata)
{
        struct syncenv  *env = NULL;
        struct syncproc *proc = NULL;
        struct synctask *task = NULL;

        proc = thdata;
        env = proc->env;

        for (;;) {
                task = syncenv_task (proc);
                if (!task)
                        break;

                synctask_switchto (task);

                syncenv_scale (env);
        }

        return NULL;
}


void
syncenv_scale (struct syncenv *env)
{
        int  diff = 0;
        int  scale = 0;
        int  i = 0;
        int  ret = 0;

        pthread_mutex_lock (&env->mutex);
        {
                if (env->procs > env->runcount)
                        goto unlock;

                scale = env->runcount;
                if (scale > env->procmax)
                        scale = env->procmax;
                if (scale > env->procs)
                        diff = scale - env->procs;
                while (diff) {
                        diff--;
                        for (; (i < env->procmax); i++) {
                                if (env->proc[i].processor == 0)
                                        break;
                        }

                        env->proc[i].env = env;
                        ret = gf_thread_create (&env->proc[i].processor, NULL,
						syncenv_processor, &env->proc[i]);
                        if (ret)
                                break;
                        env->procs++;
                        i++;
                }
        }
unlock:
        pthread_mutex_unlock (&env->mutex);
}


void
syncenv_destroy (struct syncenv *env)
{

}


struct syncenv *
syncenv_new (size_t stacksize, int procmin, int procmax)
{
        struct syncenv *newenv = NULL;
        int             ret = 0;
        int             i = 0;

	if (!procmin || procmin < 0)
		procmin = SYNCENV_PROC_MIN;
	if (!procmax || procmax > SYNCENV_PROC_MAX)
		procmax = SYNCENV_PROC_MAX;

	if (procmin > procmax)
		return NULL;

        newenv = CALLOC (1, sizeof (*newenv));

        if (!newenv)
                return NULL;

        pthread_mutex_init (&newenv->mutex, NULL);
        pthread_cond_init (&newenv->cond, NULL);

        INIT_LIST_HEAD (&newenv->runq);
        INIT_LIST_HEAD (&newenv->waitq);

        newenv->stacksize    = SYNCENV_DEFAULT_STACKSIZE;
        if (stacksize)
                newenv->stacksize = stacksize;
	newenv->procmin = procmin;
	newenv->procmax = procmax;

        for (i = 0; i < newenv->procmin; i++) {
                newenv->proc[i].env = newenv;
                ret = gf_thread_create (&newenv->proc[i].processor, NULL,
					syncenv_processor, &newenv->proc[i]);
                if (ret)
                        break;
                newenv->procs++;
        }

        if (ret != 0)
                syncenv_destroy (newenv);

        return newenv;
}


int
synclock_init (synclock_t *lock)
{
	if (!lock)
		return -1;

	pthread_cond_init (&lock->cond, 0);
	lock->lock = 0;
	INIT_LIST_HEAD (&lock->waitq);

	return pthread_mutex_init (&lock->guard, 0);
}


int
synclock_destroy (synclock_t *lock)
{
	if (!lock)
		return -1;

	pthread_cond_destroy (&lock->cond);
	return pthread_mutex_destroy (&lock->guard);
}


static int
__synclock_lock (struct synclock *lock)
{
	struct synctask *task = NULL;

	if (!lock)
		return -1;

	task = synctask_get ();

	while (lock->lock) {
		if (task) {
			/* called within a synctask */
			list_add_tail (&task->waitq, &lock->waitq);
                        pthread_mutex_unlock (&lock->guard);
                        synctask_yield (task);
                        /* task is removed from waitq in unlock,
                         * under lock->guard.*/
                        pthread_mutex_lock (&lock->guard);
		} else {
			/* called by a non-synctask */
			pthread_cond_wait (&lock->cond, &lock->guard);
		}
	}

	lock->lock = _gf_true;
	lock->owner = task;

	return 0;
}


int
synclock_lock (synclock_t *lock)
{
	int ret = 0;

	pthread_mutex_lock (&lock->guard);
	{
		ret = __synclock_lock (lock);
	}
	pthread_mutex_unlock (&lock->guard);

	return ret;
}


int
synclock_trylock (synclock_t *lock)
{
	int ret = 0;

	errno = 0;

	pthread_mutex_lock (&lock->guard);
	{
		if (lock->lock) {
			errno = EBUSY;
			ret = -1;
			goto unlock;
		}

		ret = __synclock_lock (lock);
	}
unlock:
	pthread_mutex_unlock (&lock->guard);

	return ret;
}


static int
__synclock_unlock (synclock_t *lock)
{
	struct synctask *task = NULL;
	struct synctask *curr = NULL;

	if (!lock)
		return -1;

	curr = synctask_get ();

	if (lock->owner != curr) {
		/* warn ? */
	}

	lock->lock = _gf_false;

	/* There could be both synctasks and non synctasks
	   waiting (or none, or either). As a mid-approach
	   between maintaining too many waiting counters
	   at one extreme and a thundering herd on unlock
	   at the other, call a cond_signal (which wakes
	   one waiter) and first synctask waiter. So at
	   most we have two threads waking up to grab the
	   just released lock.
	*/
	pthread_cond_signal (&lock->cond);
	if (!list_empty (&lock->waitq)) {
		task = list_entry (lock->waitq.next, struct synctask, waitq);
                list_del_init (&task->waitq);
		synctask_wake (task);
	}

	return 0;
}


int
synclock_unlock (synclock_t *lock)
{
	int ret = 0;

	pthread_mutex_lock (&lock->guard);
	{
		ret = __synclock_unlock (lock);
	}
	pthread_mutex_unlock (&lock->guard);

	return ret;
}

/* Barriers */

int
syncbarrier_init (struct syncbarrier *barrier)
{
	if (!barrier) {
		errno = EINVAL;
		return -1;
	}

	pthread_cond_init (&barrier->cond, 0);
	barrier->count = 0;
	INIT_LIST_HEAD (&barrier->waitq);

	return pthread_mutex_init (&barrier->guard, 0);
}


int
syncbarrier_destroy (struct syncbarrier *barrier)
{
	if (!barrier) {
		errno = EINVAL;
		return -1;
	}

	pthread_cond_destroy (&barrier->cond);
	return pthread_mutex_destroy (&barrier->guard);
}


static int
__syncbarrier_wait (struct syncbarrier *barrier, int waitfor)
{
	struct synctask *task = NULL;

	if (!barrier) {
		errno = EINVAL;
		return -1;
	}

	task = synctask_get ();

	while (barrier->count < waitfor) {
		if (task) {
			/* called within a synctask */
			list_add_tail (&task->waitq, &barrier->waitq);
                        pthread_mutex_unlock (&barrier->guard);
                        synctask_yield (task);
                        pthread_mutex_lock (&barrier->guard);
		} else {
			/* called by a non-synctask */
			pthread_cond_wait (&barrier->cond, &barrier->guard);
		}
	}

	barrier->count = 0;

	return 0;
}


int
syncbarrier_wait (struct syncbarrier *barrier, int waitfor)
{
	int ret = 0;

	pthread_mutex_lock (&barrier->guard);
	{
		ret = __syncbarrier_wait (barrier, waitfor);
	}
	pthread_mutex_unlock (&barrier->guard);

	return ret;
}


static int
__syncbarrier_wake (struct syncbarrier *barrier)
{
	struct synctask *task = NULL;

	if (!barrier) {
		errno = EINVAL;
		return -1;
	}

	barrier->count++;

	pthread_cond_signal (&barrier->cond);
	if (!list_empty (&barrier->waitq)) {
		task = list_entry (barrier->waitq.next, struct synctask, waitq);
                list_del_init (&task->waitq);
		synctask_wake (task);
	}

	return 0;
}


int
syncbarrier_wake (struct syncbarrier *barrier)
{
	int ret = 0;

	pthread_mutex_lock (&barrier->guard);
	{
		ret = __syncbarrier_wake (barrier);
	}
	pthread_mutex_unlock (&barrier->guard);

	return ret;
}


/* FOPS */


int
syncop_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int op_ret, int op_errno, inode_t *inode,
                   struct iatt *iatt, dict_t *xdata, struct iatt *parent)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        if (op_ret == 0) {
                args->iatt1  = *iatt;
                args->iatt2  = *parent;
                if (xdata)
                        args->xdata  = dict_ref (xdata);
        }

        __wake (args);

        return 0;
}


int
syncop_lookup (xlator_t *subvol, loc_t *loc, dict_t *xdata_req,
               struct iatt *iatt, dict_t **xdata_rsp, struct iatt *parent)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_lookup_cbk, subvol->fops->lookup,
                loc, xdata_req);

        if (iatt)
                *iatt = args.iatt1;
        if (parent)
                *parent = args.iatt2;
        if (xdata_rsp)
                *xdata_rsp = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}

static gf_dirent_t *
entry_copy (gf_dirent_t *source)
{
        gf_dirent_t *sink = NULL;

        sink = gf_dirent_for_name (source->d_name);

        sink->d_off = source->d_off;
        sink->d_ino = source->d_ino;
        sink->d_type = source->d_type;
        sink->d_stat = source->d_stat;

	if (source->inode)
		sink->inode = inode_ref (source->inode);
        return sink;
}

int32_t
syncop_readdirp_cbk (call_frame_t *frame,
                     void *cookie,
                     xlator_t *this,
                     int32_t op_ret,
                     int32_t op_errno,
                     gf_dirent_t *entries, dict_t *xdata)
{
        struct syncargs *args = NULL;
        gf_dirent_t *entry = NULL;
        gf_dirent_t  *tmp = NULL;

        int count = 0;

        args = cookie;

        INIT_LIST_HEAD (&args->entries.list);

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        if (op_ret >= 0) {
                list_for_each_entry (entry, &entries->list, list) {
                        tmp = entry_copy (entry);
                        gf_log (this->name, GF_LOG_TRACE,
                                "adding entry=%s, count=%d",
                                tmp->d_name, count);
                        list_add_tail (&tmp->list, &(args->entries.list));
                        count++;
                }
        }

        __wake (args);

        return 0;

}

int
syncop_readdirp (xlator_t *subvol,
                 fd_t *fd,
                 size_t size,
                 off_t off,
                 dict_t *dict,
                 gf_dirent_t *entries)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_readdirp_cbk, subvol->fops->readdirp,
                fd, size, off, dict);

        if (entries)
                list_splice_init (&args.entries.list, &entries->list);
        /* TODO: need to free all the 'args.entries' in 'else' case */

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;

}

int32_t
syncop_readdir_cbk (call_frame_t *frame,
                    void *cookie,
                    xlator_t *this,
                    int32_t op_ret,
                    int32_t op_errno,
                    gf_dirent_t *entries, dict_t *xdata)
{
        struct syncargs *args = NULL;
        gf_dirent_t *entry = NULL;
        gf_dirent_t  *tmp = NULL;

        int count = 0;

        args = cookie;

        INIT_LIST_HEAD (&args->entries.list);

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        if (op_ret >= 0) {
                list_for_each_entry (entry, &entries->list, list) {
                        tmp = entry_copy (entry);
                        gf_log (this->name, GF_LOG_TRACE,
                                "adding entry=%s, count=%d",
                                tmp->d_name, count);
                        list_add_tail (&tmp->list, &(args->entries.list));
                        count++;
                }
        }

        __wake (args);

        return 0;

}

int
syncop_readdir (xlator_t *subvol,
                fd_t *fd,
                size_t size,
                off_t off,
                gf_dirent_t *entries)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_readdir_cbk, subvol->fops->readdir,
                fd, size, off, NULL);

        if (entries)
                list_splice_init (&args.entries.list, &entries->list);
        /* TODO: need to free all the 'args.entries' in 'else' case */

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;

}

int32_t
syncop_opendir_cbk (call_frame_t *frame,
                    void *cookie,
                    xlator_t *this,
                    int32_t op_ret,
                    int32_t op_errno,
                    fd_t *fd, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        __wake (args);

        return 0;
}

int
syncop_opendir (xlator_t *subvol,
                loc_t *loc,
                fd_t *fd)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_opendir_cbk, subvol->fops->opendir,
                loc, fd, NULL);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;

}

int
syncop_fsyncdir_cbk (call_frame_t *frame, void* cookie, xlator_t *this,
                     int op_ret, int op_errno, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        __wake (args);

        return 0;
}

int
syncop_fsyncdir (xlator_t *subvol, fd_t *fd, int datasync)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_fsyncdir_cbk, subvol->fops->fsyncdir,
                fd, datasync, NULL);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}

int
syncop_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        __wake (args);

        return 0;
}

int
syncop_removexattr (xlator_t *subvol, loc_t *loc, const char *name)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_removexattr_cbk, subvol->fops->removexattr,
                loc, name, NULL);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}

int
syncop_fremovexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int op_ret, int op_errno, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        __wake (args);

        return 0;
}

int
syncop_fremovexattr (xlator_t *subvol, fd_t *fd, const char *name)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_fremovexattr_cbk,
                subvol->fops->fremovexattr, fd, name, NULL);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}

int
syncop_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        __wake (args);

        return 0;
}


int
syncop_setxattr (xlator_t *subvol, loc_t *loc, dict_t *dict, int32_t flags)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_setxattr_cbk, subvol->fops->setxattr,
                loc, dict, flags, NULL);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}

int
syncop_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        __wake (args);

        return 0;
}


int
syncop_fsetxattr (xlator_t *subvol, fd_t *fd, dict_t *dict, int32_t flags)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_fsetxattr_cbk, subvol->fops->fsetxattr,
                fd, dict, flags, NULL);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}

int
syncop_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno, dict_t *dict, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;
        if (op_ret >= 0)
                args->xattr = dict_ref (dict);

        __wake (args);

        return 0;
}

int
syncop_listxattr (xlator_t *subvol, loc_t *loc, dict_t **dict)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_getxattr_cbk, subvol->fops->getxattr,
                loc, NULL, NULL);

        if (dict)
                *dict = args.xattr;
        else if (args.xattr)
                dict_unref (args.xattr);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}

int
syncop_getxattr (xlator_t *subvol, loc_t *loc, dict_t **dict, const char *key)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_getxattr_cbk, subvol->fops->getxattr,
                loc, key, NULL);

        if (dict)
                *dict = args.xattr;
        else if (args.xattr)
                dict_unref (args.xattr);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}

int
syncop_fgetxattr (xlator_t *subvol, fd_t *fd, dict_t **dict, const char *key)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_getxattr_cbk, subvol->fops->fgetxattr,
                fd, key, NULL);

        if (dict)
                *dict = args.xattr;
        else if (args.xattr)
                dict_unref (args.xattr);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}

int
syncop_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno,
                   struct statvfs *buf, dict_t *xdata)

{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        if (op_ret == 0) {
                args->statvfs_buf  = *buf;
        }

        __wake (args);

        return 0;
}


int
syncop_statfs (xlator_t *subvol, loc_t *loc, struct statvfs *buf)

{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_statfs_cbk, subvol->fops->statfs,
                loc, NULL);

        if (buf)
                *buf = args.statvfs_buf;

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}

int
syncop_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno,
                    struct iatt *preop, struct iatt *postop, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        if (op_ret == 0) {
                args->iatt1  = *preop;
                args->iatt2  = *postop;
        }

        __wake (args);

        return 0;
}


int
syncop_setattr (xlator_t *subvol, loc_t *loc, struct iatt *iatt, int valid,
                struct iatt *preop, struct iatt *postop)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_setattr_cbk, subvol->fops->setattr,
                loc, iatt, valid, NULL);

        if (preop)
                *preop = args.iatt1;
        if (postop)
                *postop = args.iatt2;

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}


int
syncop_fsetattr (xlator_t *subvol, fd_t *fd, struct iatt *iatt, int valid,
                 struct iatt *preop, struct iatt *postop)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_setattr_cbk, subvol->fops->fsetattr,
                fd, iatt, valid, NULL);

        if (preop)
                *preop = args.iatt1;
        if (postop)
                *postop = args.iatt2;

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}


int32_t
syncop_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        __wake (args);

        return 0;
}

int
syncop_open (xlator_t *subvol, loc_t *loc, int32_t flags, fd_t *fd)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_open_cbk, subvol->fops->open,
                loc, flags, fd, NULL);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;

}


int32_t
syncop_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iovec *vector,
                  int32_t count, struct iatt *stbuf, struct iobref *iobref,
                  dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        INIT_LIST_HEAD (&args->entries.list);

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        if (args->op_ret >= 0) {
                if (iobref)
                        args->iobref = iobref_ref (iobref);
                args->vector = iov_dup (vector, count);
                args->count  = count;
        }

        __wake (args);

        return 0;

}

int
syncop_readv (xlator_t *subvol, fd_t *fd, size_t size, off_t off,
              uint32_t flags, struct iovec **vector, int *count,
              struct iobref **iobref)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_readv_cbk, subvol->fops->readv,
                fd, size, off, flags, NULL);

        if (args.op_ret < 0)
                goto out;

        if (vector)
                *vector = args.vector;
        else
                GF_FREE (args.vector);

        if (count)
                *count = args.count;

        /* Do we need a 'ref' here? */
        if (iobref)
                *iobref = args.iobref;
        else if (args.iobref)
                iobref_unref (args.iobref);

out:
        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;

}

int
syncop_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int op_ret, int op_errno, struct iatt *prebuf,
                   struct iatt *postbuf, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        __wake (args);

        return 0;
}

int
syncop_writev (xlator_t *subvol, fd_t *fd, const struct iovec *vector,
               int32_t count, off_t offset, struct iobref *iobref,
               uint32_t flags)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_writev_cbk, subvol->fops->writev,
                fd, (struct iovec *) vector, count, offset, flags, iobref,
                NULL);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}

int syncop_write (xlator_t *subvol, fd_t *fd, const char *buf, int size,
                  off_t offset, struct iobref *iobref, uint32_t flags)
{
        struct syncargs args = {0,};
        struct iovec    vec  = {0,};

        vec.iov_len = size;
        vec.iov_base = (void *)buf;

        SYNCOP (subvol, (&args), syncop_writev_cbk, subvol->fops->writev,
                fd, &vec, 1, offset, flags, iobref, NULL);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}


int
syncop_close (fd_t *fd)
{
        if (fd)
                fd_unref (fd);
        return 0;
}

int32_t
syncop_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
                   struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

	if (buf)
		args->iatt1 = *buf;

        __wake (args);

        return 0;
}

int
syncop_create (xlator_t *subvol, loc_t *loc, int32_t flags, mode_t mode,
               fd_t *fd, dict_t *xdata, struct iatt *iatt)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_create_cbk, subvol->fops->create,
                loc, flags, mode, 0, fd, xdata);

	if (iatt)
		*iatt = args.iatt1;

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;

}

int
syncop_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int op_ret, int op_errno, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        __wake (args);

        return 0;
}

int
syncop_unlink (xlator_t *subvol, loc_t *loc)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_unlink_cbk, subvol->fops->unlink, loc,
                0, NULL);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}

int
syncop_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int op_ret, int op_errno, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        __wake (args);

        return 0;
}

int
syncop_rmdir (xlator_t *subvol, loc_t *loc, int flags)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_rmdir_cbk, subvol->fops->rmdir, loc,
                flags, NULL);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}


int
syncop_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, inode_t *inode,
                 struct iatt *buf, struct iatt *preparent,
                 struct iatt *postparent, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret = op_ret;
        args->op_errno = op_errno;

        __wake (args);

        return 0;
}


int
syncop_link (xlator_t *subvol, loc_t *oldloc, loc_t *newloc)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_link_cbk, subvol->fops->link,
                oldloc, newloc, NULL);

        if (args.op_ret < 0)
                return -args.op_errno;

        return args.op_ret;
}


int
syncop_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *buf,
                   struct iatt *preoldparent, struct iatt *postoldparent,
                   struct iatt *prenewparent, struct iatt *postnewparent,
                   dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret = op_ret;
        args->op_errno = op_errno;

        __wake (args);

        return 0;
}


int
syncop_rename (xlator_t *subvol, loc_t *oldloc, loc_t *newloc)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_rename_cbk, subvol->fops->rename,
                oldloc, newloc, NULL);

        if (args.op_ret < 0)
                return -args.op_errno;

        return args.op_ret;
}


int
syncop_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, struct iatt *prebuf,
                      struct iatt *postbuf, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        __wake (args);

        return 0;
}

int
syncop_ftruncate (xlator_t *subvol, fd_t *fd, off_t offset)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_ftruncate_cbk, subvol->fops->ftruncate,
                fd, offset, NULL);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}

int
syncop_truncate (xlator_t *subvol, loc_t *loc, off_t offset)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_ftruncate_cbk, subvol->fops->truncate,
                loc, offset, NULL);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}

int
syncop_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        __wake (args);

        return 0;

}

int
syncop_fsync (xlator_t *subvol, fd_t *fd, int dataonly)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_fsync_cbk, subvol->fops->fsync,
                fd, dataonly, NULL);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;

}


int
syncop_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        __wake (args);

        return 0;

}

int
syncop_flush (xlator_t *subvol, fd_t *fd)
{
        struct syncargs args = {0};

        SYNCOP (subvol, (&args), syncop_flush_cbk, subvol->fops->flush,
                fd, NULL);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;

}

int
syncop_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *stbuf, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;
        if (op_ret == 0)
                args->iatt1 = *stbuf;

        __wake (args);

        return 0;

}

int
syncop_fstat (xlator_t *subvol, fd_t *fd, struct iatt *stbuf)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_fstat_cbk, subvol->fops->fstat,
                fd, NULL);

        if (stbuf)
                *stbuf = args.iatt1;

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;

}

int
syncop_stat (xlator_t *subvol, loc_t *loc, struct iatt *stbuf)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_fstat_cbk, subvol->fops->stat,
                loc, NULL);

        if (stbuf)
                *stbuf = args.iatt1;

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;

}

int32_t
syncop_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;
	if (buf)
		args->iatt1 = *buf;

        __wake (args);

        return 0;
}

int
syncop_symlink (xlator_t *subvol, loc_t *loc, const char *newpath, dict_t *dict,
		struct iatt *iatt)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_symlink_cbk, subvol->fops->symlink,
                newpath, loc, 0, dict);

	if (iatt)
		*iatt = args.iatt1;

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;

}

int
syncop_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno, const char *path,
                     struct iatt *stbuf, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        if ((op_ret != -1) && path)
                args->buffer = gf_strdup (path);

        __wake (args);

        return 0;
}

int
syncop_readlink (xlator_t *subvol, loc_t *loc, char **buffer, size_t size)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_readlink_cbk, subvol->fops->readlink,
                loc, size, NULL);

        if (buffer)
                *buffer = args.buffer;
        else GF_FREE (args.buffer);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}

int
syncop_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *buf, struct iatt *preparent,
                  struct iatt *postparent, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

	if (buf)
		args->iatt1 = *buf;

        __wake (args);

        return 0;
}

int
syncop_mknod (xlator_t *subvol, loc_t *loc, mode_t mode, dev_t rdev,
              dict_t *dict, struct iatt *iatt)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_mknod_cbk, subvol->fops->mknod,
                loc, mode, rdev, 0, dict);

	if (iatt)
		*iatt = args.iatt1;

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;

}


int
syncop_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *buf, struct iatt *preparent,
                  struct iatt *postparent, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;
	if (buf)
		args->iatt1 = *buf;

        __wake (args);

        return 0;
}


int
syncop_mkdir (xlator_t *subvol, loc_t *loc, mode_t mode, dict_t *dict,
	      struct iatt *iatt)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_mkdir_cbk, subvol->fops->mkdir,
                loc, mode, 0, dict);

	if (iatt)
		*iatt = args.iatt1;

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;

}

int
syncop_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;
        __wake (args);

        return 0;
}

int
syncop_access (xlator_t *subvol, loc_t *loc, int32_t mask)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_access_cbk, subvol->fops->access,
                loc, mask, NULL);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}


int
syncop_fallocate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, struct iatt *prebuf,
                      struct iatt *postbuf, dict_t *xdata)
{
	struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        __wake (args);

	return 0;
}

int
syncop_fallocate(xlator_t *subvol, fd_t *fd, int32_t keep_size, off_t offset,
		 size_t len)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_fallocate_cbk, subvol->fops->fallocate,
                fd, keep_size, offset, len, NULL);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}


int
syncop_discard_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno, struct iatt *prebuf,
                    struct iatt *postbuf, dict_t *xdata)
{
	struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        __wake (args);

	return 0;
}

int
syncop_discard(xlator_t *subvol, fd_t *fd, off_t offset, size_t len)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_discard_cbk, subvol->fops->discard,
                fd, offset, len, NULL);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}

int
syncop_zerofill_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno, struct iatt *prebuf,
                    struct iatt *postbuf, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        __wake (args);

        return 0;
}

int
syncop_zerofill(xlator_t *subvol, fd_t *fd, off_t offset, off_t len)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_zerofill_cbk, subvol->fops->zerofill,
                fd, offset, len, NULL);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}


int
syncop_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	       int op_ret, int op_errno, struct gf_flock *flock,
	       dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;
	if (flock)
		args->flock = *flock;
        __wake (args);

        return 0;
}


int
syncop_lk (xlator_t *subvol, fd_t *fd, int cmd, struct gf_flock *flock)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_lk_cbk, subvol->fops->lk,
                fd, cmd, flock, NULL);

	*flock = args.flock;

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}
