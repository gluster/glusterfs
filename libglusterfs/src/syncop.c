/*
  Copyright (c) 2008-2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "syncop.h"
#include "libglusterfs-messages.h"

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

int
syncopctx_setfspid (void *pid)
{
	struct syncopctx *opctx = NULL;
	int               ret = 0;

	/* In args check */
	if (!pid) {
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
	if (opctx && pid) {
		opctx->pid = *(pid_t *)pid;
		opctx->valid |= SYNCOPCTX_PID;
	}

	return ret;
}

int
syncopctx_setfslkowner (gf_lkowner_t *lk_owner)
{
        struct syncopctx *opctx = NULL;
        int               ret = 0;

        /* In args check */
        if (!lk_owner) {
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
        if (opctx && lk_owner) {
                opctx->lk_owner = *lk_owner;
                opctx->valid |= SYNCOPCTX_LKOWNER;
        }

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
                gf_msg_debug (task->xl->name, 0, "re-running already running"
                              " task");
                env->runcount--;
                break;
        case SYNCTASK_WAIT:
                env->waitcount--;
                break;
        case SYNCTASK_DONE:
                gf_msg (task->xl->name, GF_LOG_WARNING, 0,
                        LG_MSG_COMPLETED_TASK, "running completed task");
		return;
	case SYNCTASK_ZOMBIE:
		gf_msg (task->xl->name, GF_LOG_WARNING, 0,
                        LG_MSG_WAKE_UP_ZOMBIE, "attempted to wake up "
                        "zombie!!");
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
                gf_msg (task->xl->name, GF_LOG_WARNING, 0,
                        LG_MSG_REWAITING_TASK, "re-waiting already waiting "
                        "task");
                env->waitcount--;
                break;
        case SYNCTASK_DONE:
                gf_msg (task->xl->name, GF_LOG_WARNING, 0,
                        LG_MSG_COMPLETED_TASK,
                        "running completed task");
                return;
	case SYNCTASK_ZOMBIE:
		gf_msg (task->xl->name, GF_LOG_WARNING, 0,
                        LG_MSG_SLEEP_ZOMBIE,
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

        if (task->state != SYNCTASK_DONE) {
                task->state = SYNCTASK_SUSPEND;
                (void) gf_backtrace_save (task->btbuf);
        }
        if (swapcontext (&task->ctx, &task->proc->sched) < 0) {
                gf_msg ("syncop", GF_LOG_ERROR, errno,
                        LG_MSG_SWAPCONTEXT_FAILED, "swapcontext failed");
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

        GF_FREE (task->stack);

        if (task->opframe)
                STACK_DESTROY (task->opframe->root);

        if (task->synccbk == NULL) {
               pthread_mutex_destroy (&task->mutex);
               pthread_cond_destroy (&task->cond);
        }

        GF_FREE (task);
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
synctask_create (struct syncenv *env, size_t stacksize, synctask_fn_t fn,
                 synctask_cbk_t cbk, call_frame_t *frame, void *opaque)
{
        struct synctask *newtask = NULL;
        xlator_t        *this    = THIS;
        int             destroymode = 0;

        VALIDATE_OR_GOTO (env, err);
        VALIDATE_OR_GOTO (fn, err);

        /* Check if the syncenv is in destroymode i.e. destroy is SET.
         * If YES, then don't allow any new synctasks on it. Return NULL.
         */
        pthread_mutex_lock (&env->mutex);
        {
                destroymode = env->destroy;
        }
        pthread_mutex_unlock (&env->mutex);

        /* syncenv is in DESTROY mode, return from here */
        if (destroymode)
                return NULL;

        newtask = GF_CALLOC (1, sizeof (*newtask), gf_common_mt_synctask);
        if (!newtask)
                return NULL;

        newtask->frame      = frame;
        if (!frame) {
                newtask->opframe = create_frame (this, this->ctx->pool);
                set_lk_owner_from_ptr (&newtask->opframe->root->lk_owner,
                                       newtask->opframe->root);
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
                gf_msg ("syncop", GF_LOG_ERROR, errno,
                        LG_MSG_GETCONTEXT_FAILED, "getcontext failed");
                goto err;
        }

        if (stacksize <= 0) {
                newtask->stack = GF_CALLOC (1, env->stacksize,
                                            gf_common_mt_syncstack);
                newtask->ctx.uc_stack.ss_size = env->stacksize;
        } else {
                newtask->stack = GF_CALLOC (1, stacksize,
                                            gf_common_mt_syncstack);
                newtask->ctx.uc_stack.ss_size = stacksize;
        }

        if (!newtask->stack) {
                goto err;
        }

        newtask->ctx.uc_stack.ss_sp   = newtask->stack;

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
                GF_FREE (newtask->stack);
                if (newtask->opframe)
                        STACK_DESTROY (newtask->opframe->root);
                GF_FREE (newtask);
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
synctask_new1 (struct syncenv *env, size_t stacksize, synctask_fn_t fn,
                synctask_cbk_t cbk, call_frame_t *frame, void *opaque)
{
	struct synctask *newtask = NULL;
	int              ret = 0;

	newtask = synctask_create (env, stacksize, fn, cbk, frame, opaque);
	if (!newtask)
		return -1;

        if (!cbk)
		ret = synctask_join (newtask);

        return ret;
}


int
synctask_new (struct syncenv *env, synctask_fn_t fn, synctask_cbk_t cbk,
              call_frame_t *frame, void *opaque)
{
        return synctask_new1 (env, 0, fn, cbk, frame, opaque);
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
                        /* If either of the conditions are met then exit
                         * the current thread:
                         * 1. syncenv has to scale down(procs > procmin)
                         * 2. syncenv is in destroy mode and no tasks in
                         *    either waitq or runq.
                         *
                         * At any point in time, a task can be either in runq,
                         * or in executing state or in the waitq. Once the
                         * destroy mode is set, no new synctask creates will
                         * be allowed, but whatever in waitq or runq should be
                         * allowed to finish before exiting any of the syncenv
                         * processor threads.
                         */
                        if (((ret == ETIMEDOUT) && (env->procs > env->procmin))
                            || (env->destroy && list_empty (&env->waitq))) {
                                task = NULL;
                                env->procs--;
                                memset (proc, 0, sizeof (*proc));
                                pthread_cond_broadcast (&env->cond);
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
        gf_msg ("syncop", GF_LOG_ERROR, errno, LG_MSG_SWAPCONTEXT_FAILED,
                "swapcontext failed");
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

/* The syncenv threads are cleaned up in this routine.
 */
void
syncenv_destroy (struct syncenv *env)
{

        if (env == NULL)
                return;

        /* SET the 'destroy' in syncenv structure to prohibit any
         * further synctask(s) on this syncenv which is in destroy mode.
         *
         * If syncenv threads are in pthread cond wait with no tasks in
         * their run or wait queue, then the threads are woken up by
         * broadcasting the cond variable and if destroy field is set,
         * the infinite loop in syncenv_processor is broken and the
         * threads return.
         *
         * If syncenv threads have tasks in runq or waitq, the tasks are
         * completed and only then the thread returns.
         */
        pthread_mutex_lock (&env->mutex);
        {
                env->destroy = 1;
                /* This broadcast will wake threads in pthread_cond_wait
                 * in syncenv_task
                 */
                pthread_cond_broadcast (&env->cond);

                /* when the syncenv_task() thread is exiting, it broadcasts to
                 * wake the below wait.
                 */
                while (env->procs != 0) {
                        pthread_cond_wait (&env->cond, &env->mutex);
                }
        }
        pthread_mutex_unlock (&env->mutex);

        pthread_mutex_destroy (&env->mutex);
        pthread_cond_destroy (&env->cond);

        GF_FREE (env);

        return;
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

        newenv = GF_CALLOC (1, sizeof (*newenv), gf_common_mt_syncenv);

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
synclock_init (synclock_t *lock, lock_attr_t attr)
{
        if (!lock)
               return -1;

        pthread_cond_init (&lock->cond, 0);
        lock->type = LOCK_NULL;
        lock->owner = NULL;
        lock->owner_tid = 0;
        lock->lock = 0;
        lock->attr = attr;
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

        if (lock->lock && (lock->attr == SYNC_LOCK_RECURSIVE)) {
                /*Recursive lock (if same owner requested for lock again then
                 *increment lock count and return success).
                 *Note:same number of unlocks required.
                 */
                switch (lock->type) {
                case LOCK_TASK:
                        if (task == lock->owner) {
                                lock->lock++;
                                gf_msg_trace ("", 0, "Recursive lock called by"
                                              " sync task.owner= %p,lock=%d",
                                              lock->owner, lock->lock);
                                return 0;
                        }
                        break;
                case LOCK_THREAD:
                        if (pthread_equal(pthread_self (), lock->owner_tid)) {
                                lock->lock++;
                                gf_msg_trace ("", 0, "Recursive lock called by"
                                              " thread ,owner=%u lock=%d",
                                              (unsigned int) lock->owner_tid,
                                              lock->lock);
                                return 0;
                        }
                        break;
                default:
                        gf_msg ("", GF_LOG_CRITICAL, 0,
                                LG_MSG_UNKNOWN_LOCK_TYPE, "unknown lock type");
                        break;
                }
        }


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

        if (task) {
                lock->type = LOCK_TASK;
                lock->owner = task;    /* for synctask*/

        } else {
                lock->type = LOCK_THREAD;
                lock->owner_tid = pthread_self (); /* for non-synctask */

        }
        lock->lock = 1;

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

        if (lock->lock == 0) {
                gf_msg ("", GF_LOG_CRITICAL, 0, LG_MSG_UNLOCK_BEFORE_LOCK,
                        "Unlock called  before lock ");
                return -1;
        }
        curr = synctask_get ();
        /*unlock should be called by lock owner
         *i.e this will not allow the lock in nonsync task and unlock
         * in sync task and vice-versa
         */
        switch (lock->type) {
        case LOCK_TASK:
                if (curr == lock->owner) {
                        lock->lock--;
                        gf_msg_trace ("", 0, "Unlock success %p, remaining"
                                      " locks=%d", lock->owner, lock->lock);
                } else {
                        gf_msg ("", GF_LOG_WARNING, 0, LG_MSG_LOCK_OWNER_ERROR,
                                "Unlock called by %p, but lock held by %p",
                                curr, lock->owner);
                }

                break;
        case LOCK_THREAD:
                if (pthread_equal(pthread_self (), lock->owner_tid)) {
                        lock->lock--;
                        gf_msg_trace ("", 0, "Unlock success %u, remaining "
                                      "locks=%d",
                                      (unsigned int)lock->owner_tid,
                                      lock->lock);
                } else {
                        gf_msg ("", GF_LOG_WARNING, 0, LG_MSG_LOCK_OWNER_ERROR,
                                "Unlock called by %u, but lock held by %u",
                                (unsigned int) pthread_self(),
                                (unsigned int) lock->owner_tid);
                }

                break;
        default:
                break;
        }

        if (lock->lock > 0) {
                 return 0;
        }
        lock->type = LOCK_NULL;
        lock->owner = NULL;
        lock->owner_tid = 0;
        lock->lock = 0;
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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        if (op_ret == 0) {
                args->iatt1  = *iatt;
                args->iatt2  = *parent;
        }

        __wake (args);

        return 0;
}


int
syncop_lookup (xlator_t *subvol, loc_t *loc, struct iatt *iatt,
               struct iatt *parent, dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_lookup_cbk, subvol->fops->lookup,
                loc, xdata_in);

        if (iatt)
                *iatt = args.iatt1;
        if (parent)
                *parent = args.iatt2;
        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
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
        if (xdata)
                args->xdata  = dict_ref (xdata);


        if (op_ret >= 0) {
                list_for_each_entry (entry, &entries->list, list) {
                        tmp = entry_copy (entry);
                        if (!tmp) {
                                args->op_ret = -1;
                                args->op_errno = ENOMEM;
                                gf_dirent_free (&(args->entries));
                                break;
                        }
                        gf_msg_trace (this->name, 0, "adding entry=%s, "
                                      "count=%d", tmp->d_name, count);
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
                 gf_dirent_t *entries,
                 dict_t *xdata_in,
                 dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_readdirp_cbk, subvol->fops->readdirp,
                fd, size, off, xdata_in);

        if (entries)
                list_splice_init (&args.entries.list, &entries->list);
        else
                gf_dirent_free (&args.entries);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        if (op_ret >= 0) {
                list_for_each_entry (entry, &entries->list, list) {
                        tmp = entry_copy (entry);
                        if (!tmp) {
                                args->op_ret = -1;
                                args->op_errno = ENOMEM;
                                gf_dirent_free (&(args->entries));
                                break;
                        }
                        gf_msg_trace (this->name, 0, "adding "
                                      "entry=%s, count=%d", tmp->d_name,
                                      count);
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
                gf_dirent_t *entries,
		dict_t *xdata_in,
                dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_readdir_cbk, subvol->fops->readdir,
                fd, size, off, xdata_in);

        if (entries)
                list_splice_init (&args.entries.list, &entries->list);
        else
                gf_dirent_free (&args.entries);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        __wake (args);

        return 0;
}

int
syncop_opendir (xlator_t *subvol,
                loc_t *loc,
                fd_t *fd,
		dict_t *xdata_in,
                dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_opendir_cbk, subvol->fops->opendir,
                loc, fd, xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        __wake (args);

        return 0;
}

int
syncop_fsyncdir (xlator_t *subvol, fd_t *fd, int datasync, dict_t *xdata_in,
                 dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_fsyncdir_cbk, subvol->fops->fsyncdir,
                fd, datasync, xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        __wake (args);

        return 0;
}

int
syncop_removexattr (xlator_t *subvol, loc_t *loc, const char *name,
                    dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_removexattr_cbk,
                subvol->fops->removexattr, loc, name, xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        __wake (args);

        return 0;
}

int
syncop_fremovexattr (xlator_t *subvol, fd_t *fd, const char *name,
                     dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_fremovexattr_cbk,
                subvol->fops->fremovexattr, fd, name, xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        __wake (args);

        return 0;
}


int
syncop_setxattr (xlator_t *subvol, loc_t *loc, dict_t *dict, int32_t flags,
		 dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_setxattr_cbk, subvol->fops->setxattr,
                loc, dict, flags, xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        __wake (args);

        return 0;
}


int
syncop_fsetxattr (xlator_t *subvol, fd_t *fd, dict_t *dict, int32_t flags,
		  dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_fsetxattr_cbk, subvol->fops->fsetxattr,
                fd, dict, flags, xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        if (op_ret >= 0)
                args->xattr = dict_ref (dict);

        __wake (args);

        return 0;
}

int
syncop_listxattr (xlator_t *subvol, loc_t *loc, dict_t **dict, dict_t *xdata_in,
                  dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_getxattr_cbk, subvol->fops->getxattr,
                loc, NULL, xdata_in);

        if (dict)
                *dict = args.xattr;
        else if (args.xattr)
                dict_unref (args.xattr);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}

int
syncop_getxattr (xlator_t *subvol, loc_t *loc, dict_t **dict, const char *key,
                 dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_getxattr_cbk, subvol->fops->getxattr,
                loc, key, xdata_in);

        if (dict)
                *dict = args.xattr;
        else if (args.xattr)
                dict_unref (args.xattr);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}

int
syncop_fgetxattr (xlator_t *subvol, fd_t *fd, dict_t **dict, const char *key,
                  dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_getxattr_cbk, subvol->fops->fgetxattr,
                fd, key, xdata_in);

        if (dict)
                *dict = args.xattr;
        else if (args.xattr)
                dict_unref (args.xattr);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        if (op_ret == 0) {
                args->statvfs_buf  = *buf;
        }

        __wake (args);

        return 0;
}


int
syncop_statfs (xlator_t *subvol, loc_t *loc, struct statvfs *buf,
               dict_t *xdata_in, dict_t **xdata_out)

{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_statfs_cbk, subvol->fops->statfs,
                loc, xdata_in);

        if (buf)
                *buf = args.statvfs_buf;
        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        if (op_ret == 0) {
                args->iatt1  = *preop;
                args->iatt2  = *postop;
        }

        __wake (args);

        return 0;
}


int
syncop_setattr (xlator_t *subvol, loc_t *loc, struct iatt *iatt, int valid,
                struct iatt *preop, struct iatt *postop, dict_t *xdata_in,
                dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_setattr_cbk, subvol->fops->setattr,
                loc, iatt, valid, xdata_in);

        if (preop)
                *preop = args.iatt1;
        if (postop)
                *postop = args.iatt2;

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}


int
syncop_fsetattr (xlator_t *subvol, fd_t *fd, struct iatt *iatt, int valid,
                 struct iatt *preop, struct iatt *postop, dict_t *xdata_in,
                 dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_setattr_cbk, subvol->fops->fsetattr,
                fd, iatt, valid, xdata_in);

        if (preop)
                *preop = args.iatt1;
        if (postop)
                *postop = args.iatt2;

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        __wake (args);

        return 0;
}

int
syncop_open (xlator_t *subvol, loc_t *loc, int32_t flags, fd_t *fd,
	     dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_open_cbk, subvol->fops->open,
                loc, flags, fd, xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

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
              struct iobref **iobref, dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_readv_cbk, subvol->fops->readv,
                fd, size, off, flags, xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        __wake (args);

        return 0;
}

int
syncop_writev (xlator_t *subvol, fd_t *fd, const struct iovec *vector,
               int32_t count, off_t offset, struct iobref *iobref,
               uint32_t flags, dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_writev_cbk, subvol->fops->writev,
                fd, (struct iovec *) vector, count, offset, flags, iobref,
                xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}

int syncop_write (xlator_t *subvol, fd_t *fd, const char *buf, int size,
                  off_t offset, struct iobref *iobref, uint32_t flags,
                  dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0,};
        struct iovec    vec  = {0,};

        vec.iov_len = size;
        vec.iov_base = (void *)buf;

        SYNCOP (subvol, (&args), syncop_writev_cbk, subvol->fops->writev,
                fd, &vec, 1, offset, flags, iobref, xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

	if (buf)
		args->iatt1 = *buf;

        __wake (args);

        return 0;
}

int
syncop_create (xlator_t *subvol, loc_t *loc, int32_t flags, mode_t mode,
               fd_t *fd, struct iatt *iatt,
               dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_create_cbk, subvol->fops->create,
                loc, flags, mode, 0, fd, xdata_in);

	if (iatt)
		*iatt = args.iatt1;

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        __wake (args);

        return 0;
}

int
syncop_unlink (xlator_t *subvol, loc_t *loc, dict_t *xdata_in,
               dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_unlink_cbk, subvol->fops->unlink, loc,
                0, xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        __wake (args);

        return 0;
}

int
syncop_rmdir (xlator_t *subvol, loc_t *loc, int flags, dict_t *xdata_in,
              dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_rmdir_cbk, subvol->fops->rmdir, loc,
                flags, xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        if (buf)
                args->iatt1 = *buf;

        __wake (args);

        return 0;
}


int
syncop_link (xlator_t *subvol, loc_t *oldloc, loc_t *newloc, struct iatt *iatt,
             dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_link_cbk, subvol->fops->link,
                oldloc, newloc, xdata_in);

        if (iatt)
                *iatt = args.iatt1;

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        __wake (args);

        return 0;
}


int
syncop_rename (xlator_t *subvol, loc_t *oldloc, loc_t *newloc,
               dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_rename_cbk, subvol->fops->rename,
                oldloc, newloc, xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        __wake (args);

        return 0;
}

int
syncop_ftruncate (xlator_t *subvol, fd_t *fd, off_t offset, dict_t *xdata_in,
                  dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_ftruncate_cbk, subvol->fops->ftruncate,
                fd, offset, xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}

int
syncop_truncate (xlator_t *subvol, loc_t *loc, off_t offset, dict_t *xdata_in,
                 dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_ftruncate_cbk, subvol->fops->truncate,
                loc, offset, xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        __wake (args);

        return 0;

}

int
syncop_fsync (xlator_t *subvol, fd_t *fd, int dataonly, dict_t *xdata_in,
              dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_fsync_cbk, subvol->fops->fsync,
                fd, dataonly, xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        __wake (args);

        return 0;

}

int
syncop_flush (xlator_t *subvol, fd_t *fd, dict_t *xdata_in,
              dict_t **xdata_out)
{
        struct syncargs args = {0};

        SYNCOP (subvol, (&args), syncop_flush_cbk, subvol->fops->flush,
                fd, xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;

}

int
syncop_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *stbuf,
                  dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;
        if (xdata)
                args->xdata  = dict_ref (xdata);

        if (op_ret == 0)
                args->iatt1 = *stbuf;

        __wake (args);

        return 0;

}

int
syncop_fstat (xlator_t *subvol, fd_t *fd, struct iatt *stbuf, dict_t *xdata_in,
              dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_fstat_cbk, subvol->fops->fstat,
                fd, xdata_in);

        if (stbuf)
                *stbuf = args.iatt1;

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;

}

int
syncop_stat (xlator_t *subvol, loc_t *loc, struct iatt *stbuf, dict_t *xdata_in,
             dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_fstat_cbk, subvol->fops->stat,
                loc, xdata_in);

        if (stbuf)
                *stbuf = args.iatt1;

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

	if (buf)
		args->iatt1 = *buf;

        __wake (args);

        return 0;
}

int
syncop_symlink (xlator_t *subvol, loc_t *loc, const char *newpath,
                struct iatt *iatt, dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_symlink_cbk, subvol->fops->symlink,
                newpath, loc, 0, xdata_in);

	if (iatt)
		*iatt = args.iatt1;

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        if ((op_ret != -1) && path)
                args->buffer = gf_strdup (path);

        __wake (args);

        return 0;
}

int
syncop_readlink (xlator_t *subvol, loc_t *loc, char **buffer, size_t size,
		 dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_readlink_cbk, subvol->fops->readlink,
                loc, size, xdata_in);

        if (buffer)
                *buffer = args.buffer;
        else GF_FREE (args.buffer);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

	if (buf)
		args->iatt1 = *buf;

        __wake (args);

        return 0;
}

int
syncop_mknod (xlator_t *subvol, loc_t *loc, mode_t mode, dev_t rdev,
              struct iatt *iatt, dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_mknod_cbk, subvol->fops->mknod,
                loc, mode, rdev, 0, xdata_in);

	if (iatt)
		*iatt = args.iatt1;

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

	if (buf)
		args->iatt1 = *buf;

        __wake (args);

        return 0;
}


int
syncop_mkdir (xlator_t *subvol, loc_t *loc, mode_t mode, struct iatt *iatt,
              dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_mkdir_cbk, subvol->fops->mkdir,
                loc, mode, 0, xdata_in);

	if (iatt)
		*iatt = args.iatt1;

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        __wake (args);

        return 0;
}

/* posix_acl xlator will respond in different ways for access calls from
   fuse and access calls from nfs. For fuse, checking op_ret is sufficient
   to check whether the access call is successful or not. But for nfs the
   mode of the access that is permitted is put into op_errno before unwind.
   With syncop, the caller of syncop_access will not be able to get the
   mode of the access despite call being successul (since syncop_access
   returns only the op_ret collected in args).
   Now, if access call is failed, then args.op_ret is returned to recognise
   the failure. But if op_ret is zero, then the mode of access which is
   set in args.op_errno is returned. Thus the caller of syncop_access
   has to check whether the return value is less than zero or not. If the
   return value it got is less than zero, then access call is failed.
   If it is not, then the access call is successful and the value the caller
   got is the mode of the access.
*/
int
syncop_access (xlator_t *subvol, loc_t *loc, int32_t mask, dict_t *xdata_in,
               dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_access_cbk, subvol->fops->access,
                loc, mask, xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_errno;
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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        __wake (args);

	return 0;
}

int
syncop_fallocate(xlator_t *subvol, fd_t *fd, int32_t keep_size, off_t offset,
		 size_t len, dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_fallocate_cbk, subvol->fops->fallocate,
                fd, keep_size, offset, len, xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        __wake (args);

	return 0;
}

int
syncop_discard(xlator_t *subvol, fd_t *fd, off_t offset, size_t len,
	       dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_discard_cbk, subvol->fops->discard,
                fd, offset, len, xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

        __wake (args);

        return 0;
}

int
syncop_zerofill(xlator_t *subvol, fd_t *fd, off_t offset, off_t len,
		dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_zerofill_cbk, subvol->fops->zerofill,
                fd, offset, len, xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}


int
syncop_ipc_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;
        if (xdata)
                args->xdata  = dict_ref (xdata);

        __wake (args);

        return 0;
}

int
syncop_ipc (xlator_t *subvol, int32_t op, dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_ipc_cbk, subvol->fops->ipc,
                op, xdata_in);

        if (args.xdata) {
                if (xdata_out) {
                        /*
                         * We're passing this reference to the caller, along
                         * with the pointer itself.  That means they're
                         * responsible for calling dict_unref at some point.
                         */
                        *xdata_out = args.xdata;
                } else {
                        dict_unref(args.xdata);
                }
        }

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}

int
syncop_seek_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int op_ret, int op_errno, off_t offset, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;
        args->offset   = offset;
        if (xdata)
                args->xdata  = dict_ref (xdata);

        __wake (args);

        return 0;
}

int
syncop_seek (xlator_t *subvol, fd_t *fd, off_t offset, gf_seek_what_t what,
             dict_t *xdata_in, off_t *off)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_seek_cbk, subvol->fops->seek, fd,
                offset, what, xdata_in);

        if (*off)
                *off = args.offset;

        if (args.op_ret == -1)
                return -args.op_errno;
        return args.op_ret;
}

int
syncop_lease_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                  int op_errno, struct gf_lease *lease, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;
        if (xdata)
                args->xdata  = dict_ref (xdata);
        if (lease)
                args->lease = *lease;

        __wake (args);

        return 0;
}

int
syncop_lease (xlator_t *subvol, loc_t *loc, struct gf_lease *lease,
              dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_lease_cbk, subvol->fops->lease,
                loc, lease, xdata_in);

        *lease = args.lease;

        if (args.xdata) {
                if (xdata_out) {
                        /*
                         * We're passing this reference to the caller, along
                         * with the pointer itself.  That means they're
                         * responsible for calling dict_unref at some point.
                         */
                        *xdata_out = args.xdata;
                } else {
                        dict_unref(args.xdata);
                }
        }

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
        if (xdata)
                args->xdata  = dict_ref (xdata);

	if (flock)
		args->flock = *flock;
        __wake (args);

        return 0;
}

int
syncop_lk (xlator_t *subvol, fd_t *fd, int cmd, struct gf_flock *flock,
	   dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_lk_cbk, subvol->fops->lk,
                fd, cmd, flock, xdata_in);

	*flock = args.flock;

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

        if (args.op_ret < 0)
                return -args.op_errno;
        return args.op_ret;
}

int32_t
syncop_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;
        if (xdata)
                args->xdata  = dict_ref (xdata);

        __wake (args);

        return 0;

}

int
syncop_inodelk (xlator_t *subvol, const char *volume, loc_t *loc, int32_t cmd,
                struct gf_flock *lock, dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_inodelk_cbk, subvol->fops->inodelk,
                volume, loc, cmd, lock, xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

        if (args.op_ret < 0)
                return -args.op_errno;

        return args.op_ret;
}

int32_t
syncop_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *dict,
                    dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;
        if (xdata)
                args->xdata  = dict_ref (xdata);

        __wake (args);

        return 0;

}

int
syncop_xattrop (xlator_t *subvol, loc_t *loc, gf_xattrop_flags_t flags,
                dict_t *dict, dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_xattrop_cbk, subvol->fops->xattrop,
                loc, flags, dict, xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

        if (args.op_ret < 0)
                return -args.op_errno;

        return args.op_ret;
}

int
syncop_fxattrop (xlator_t *subvol, fd_t *fd, gf_xattrop_flags_t flags,
                 dict_t *dict, dict_t *xdata_in, dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_xattrop_cbk, subvol->fops->fxattrop,
                fd, flags, dict, xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

        if (args.op_ret < 0)
                return -args.op_errno;

        return args.op_ret;
}

int32_t
syncop_getactivelk_cbk (call_frame_t *frame,
                         void *cookie,
                         xlator_t *this,
                         int32_t op_ret,
                         int32_t op_errno,
                         lock_migration_info_t *locklist, dict_t *xdata)
{
        struct syncargs *args = NULL;
        lock_migration_info_t   *tmp = NULL;
        lock_migration_info_t   *entry = NULL;

        args = cookie;

        INIT_LIST_HEAD (&args->locklist.list);

        args->op_ret   = op_ret;
        args->op_errno = op_errno;
        if (xdata)
                args->xdata  = dict_ref (xdata);

        if (op_ret > 0) {
                list_for_each_entry (tmp, &locklist->list, list) {
                        entry = GF_CALLOC (1, sizeof (lock_migration_info_t),
                                          gf_common_mt_char);

                        if (!entry) {
                                gf_msg (THIS->name, GF_LOG_ERROR, 0, 0,
                                        "lock mem allocation  failed");
                                gf_free_mig_locks (&args->locklist);

                                break;
                        }

                        INIT_LIST_HEAD (&entry->list);

                        entry->flock = tmp->flock;

                        entry->lk_flags = tmp->lk_flags;

                        entry->client_uid = gf_strdup (tmp->client_uid);

                        list_add_tail (&entry->list, &args->locklist.list);

                }
        }

        __wake (args);

        return 0;

}

int
syncop_getactivelk (xlator_t *subvol, loc_t *loc,
                    lock_migration_info_t *locklist,  dict_t *xdata_in,
                    dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_getactivelk_cbk,
                subvol->fops->getactivelk,
                loc, xdata_in);

        if (locklist)
                list_splice_init (&args.locklist.list, &locklist->list);
        else
                gf_free_mig_locks (&args.locklist) ;

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

        if (args.op_ret < 0)
                return -args.op_errno;

        return args.op_ret;

}

int
syncop_setactivelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        if (xdata)
                args->xdata  = dict_ref (xdata);


        __wake (args);

        return 0;

}

int
syncop_setactivelk (xlator_t *subvol, loc_t *loc,
                     lock_migration_info_t *locklist,  dict_t *xdata_in,
                     dict_t **xdata_out)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_setactivelk_cbk,
                subvol->fops->setactivelk,
                loc, locklist, xdata_in);

        if (xdata_out)
                *xdata_out = args.xdata;
        else if (args.xdata)
                dict_unref (args.xdata);

        if (args.op_ret < 0)
                return -args.op_errno;

        return args.op_ret;

}
