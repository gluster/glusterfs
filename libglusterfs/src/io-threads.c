/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/
#include "mem-pool.h"
#include "glusterfs.h"
#include "globals.h"
#include "io-threads.h"

/*
 * We use a decay model to keep track and make sure we're not spawning new
 * threads too often.  Each increment adds a large value to a counter, and that
 * counter keeps ticking back down to zero over a fairly long period.  For
 * example, let's use ONE_WEEK=604800 seconds, and we want to detect when we
 * have N=3 increments during that time.  Thus, our threshold is
 * (N-1)*ONE_WEEK.  To see how it works, look at three examples.

 *   (a) Two events close together, then one more almost a week later.  The
 *   first two events push our counter to 2*ONE_WEEK plus a bit.  At the third
 *   event, we decay down to ONE_WEEK plus a bit and then add ONE_WEEK for the
 *   new event, exceeding our threshold.

 *   (b) One event, then two more almost a week later.  At the time of the
 *   second and third events, the counter is already non-zero, so when we add
 *   2*ONE_WEEK we exceed again.

 *   (c) Three events, spaced three days apart.  At the time of the second
 *   event, we decay down to approxitely ONE_WEEK*4/7 and then add another
 *   ONE_WEEK.  At the third event, we decay again down to ONE_WEEK*8/7 and add
 *   another ONE_WEEK, so boom.

 * Note that in all three cases if that last event came a day later our counter
 * would have decayed a bit more and we would *not* exceed our threshold.  It's
 * not exactly the same as a precise "three in one week" limit, but it's very
 * close and it allows the same kind of tweaking while requiring only constant
 * space - no arrays of variable length N to allocate or maintain.  All we need
 * (for each queue) is the value plus the time of the last update.
 */

typedef struct {
    uint32_t value;
    time_t update_time;
} threshold_t;
/*
 * Variables so that I can hack these for testing.
 * TBD: make these tunable?
 */
static uint32_t THRESH_SECONDS = 604800;
static uint32_t THRESH_EVENTS = 3;
static uint32_t THRESH_LIMIT = 1209600; /* SECONDS * (EVENTS-1) */

void
gf_iot_cleanup(void *data);

call_stub_t *
__iot_dequeue(gf_iot_t *iot, int *pri)
{
    call_stub_t *stub = NULL;
    int i = 0;
    iot_client_ctx_t *ctx;

    *pri = -1;
    for (i = 0; i < GF_FOP_PRI_MAX; i++) {
        if (iot->ac_iot_count[i] >= iot->ac_iot_limit[i]) {
            continue;
        }

        if (list_empty(&iot->clients[i])) {
            continue;
        }

        /* Get the first per-client queue for this priority. */
        ctx = list_first_entry(&iot->clients[i], iot_client_ctx_t, clients);
        if (!ctx) {
            continue;
        }

        if (list_empty(&ctx->reqs)) {
            continue;
        }

        /* Get the first request on that queue. */
        stub = list_first_entry(&ctx->reqs, call_stub_t, list);
        list_del_init(&stub->list);
        if (list_empty(&ctx->reqs)) {
            list_del_init(&ctx->clients);
        } else {
            list_rotate_left(&iot->clients[i]);
        }

        iot->ac_iot_count[i]++;
        iot->queue_marked[i] = _gf_false;
        *pri = i;
        break;
    }

    if (!stub)
        return NULL;

    iot->queue_size--;
    iot->queue_sizes[*pri]--;

    return stub;
}

void *
iot_worker(void *data)
{
    gf_iot_t *iot = NULL;
    call_stub_t *stub = NULL;
    struct timespec sleep_till = {
        0,
    };
    int ret = 0;
    int pri = -1;
    gf_boolean_t bye = _gf_false;

    iot = data;

    for (;;) {
        pthread_mutex_lock(&iot->mutex);
        {
            if (pri != -1) {
                iot->ac_iot_count[pri]--;
                pri = -1;
            }
            while (iot->queue_size == 0) {
                if (iot->state & IOT_STOPPED) {
                    bye = _gf_true; /*Avoid sleep*/
                    break;
                }

                clock_gettime(CLOCK_REALTIME_COARSE, &sleep_till);
                sleep_till.tv_sec += iot->idle_time;

                iot->sleep_count++;
                ret = pthread_cond_timedwait(&iot->cond, &iot->mutex,
                                             &sleep_till);
                iot->sleep_count--;

                if ((iot->state & IOT_STOPPED) || ret == ETIMEDOUT) {
                    bye = _gf_true;
                    break;
                }
            }

            if (bye) {
                if ((iot->state & IOT_STOPPED) ||
                    iot->curr_count > IOT_MIN_THREADS) {
                    iot->curr_count--;
                    if (iot->curr_count == 0)
                        pthread_cond_broadcast(&iot->cond);
                    gf_msg_debug("io-threads", 0,
                                 "terminated. "
                                 "iot->curr_count=%d",
                                 iot->curr_count);
                } else {
                    bye = _gf_false;
                }
            }

            if (!bye)
                stub = __iot_dequeue(iot, &pri);
        }
        pthread_mutex_unlock(&iot->mutex);

        if (stub) { /* guard against spurious wakeups */
            if (stub->poison) {
                gf_log("io-threads", GF_LOG_INFO,
                       "Dropping poisoned request %p.", stub);
                call_stub_destroy(stub);
            } else {
                call_resume(stub);
            }
        }
        stub = NULL;

        if (bye)
            break;
    }
    return NULL;
}

int
__iot_workers_scale(gf_iot_t *iot)
{
    int scale = 0;
    int diff = 0;
    pthread_t thread;
    int ret = 0;
    int i = 0;
    char thread_name[GF_THREAD_NAMEMAX] = {
        0,
    };

    for (i = 0; i < GF_FOP_PRI_MAX; i++)
        scale += min(iot->queue_sizes[i], iot->ac_iot_limit[i]);

    if (scale < IOT_MIN_THREADS)
        scale = IOT_MIN_THREADS;

    if (scale > iot->max_count)
        scale = iot->max_count;

    if (iot->curr_count < scale) {
        diff = scale - iot->curr_count;
    }

    while (diff) {
        diff--;

        snprintf(thread_name, sizeof(thread_name), "iotwr%03hx",
                 (iot->curr_count & 0x3ff));
        ret = gf_thread_create(&thread, &iot->w_attr, iot_worker, iot,
                               thread_name);
        if (ret == 0) {
            iot->curr_count++;
            gf_msg_debug("io-threads", 0,
                         "scaled threads to %d (queue_size=%d/%d)",
                         iot->curr_count, iot->queue_size, scale);
        } else {
            break;
        }
    }
    return diff;
}

int
iot_workers_scale(gf_iot_t *iot)
{
    int ret = -1;

    if (iot == NULL) {
        ret = -EINVAL;
        goto out;
    }

    pthread_mutex_lock(&iot->mutex);
    {
        ret = __iot_workers_scale(iot);
    }
    pthread_mutex_unlock(&iot->mutex);

out:
    return ret;
}

int
set_stack_size(gf_iot_t *iot)
{
    int err = 0;
    size_t stacksize = IOT_THREAD_STACK_SIZE;

    err = pthread_attr_init(&iot->w_attr);
    if (err != 0) {
        gf_msg("io-threads", GF_LOG_ERROR, err, LG_MSG_IO_THREADS_INIT_FAILED,
               "Thread attribute initialization failed");
        return err;
    }

    err = pthread_attr_setstacksize(&iot->w_attr, stacksize);
    if (err == EINVAL) {
        err = pthread_attr_getstacksize(&iot->w_attr, &stacksize);
        if (!err) {
            gf_msg("io-threads", GF_LOG_WARNING, 0,
                   LG_MSG_IO_THREADS_SIZE_NOT_SET,
                   "Using default thread stack size %zd", stacksize);
        } else {
            gf_msg("io-threads", GF_LOG_WARNING, 0,
                   LG_MSG_IO_THREADS_SIZE_NOT_SET,
                   "Using default thread stack size");
            err = 0;
        }
    }

    iot->stack_size = stacksize;
    return err;
}

gf_iot_t *
gf_iot_defaults_init(glusterfs_ctx_t *ctx)
{
    gf_iot_t *iot = NULL;
    int ret = 0;
    int i = 0;

    iot = (void *)GF_CALLOC(1, sizeof(*iot), gf_common_mt_iot_t);

    if (iot == NULL) {
        gf_msg("io-threads", GF_LOG_ERROR, ENOMEM, LG_MSG_NO_MEMORY,
               "out of memory");
        goto out;
    }

    ret = pthread_cond_init(&iot->cond, NULL);
    if (ret != 0) {
        gf_msg("io-threads", GF_LOG_ERROR, 0, LG_MSG_IO_THREADS_INIT_FAILED,
               "pthread_cond_init failed (%d)", ret);
        goto out;
    }
    iot->cond_inited = _gf_true;

    ret = pthread_mutex_init(&iot->mutex, NULL);
    if (ret != 0) {
        gf_msg("io-threads", GF_LOG_ERROR, 0, LG_MSG_IO_THREADS_INIT_FAILED,
               "pthread_mutex_init failed (%d)", ret);
        goto out;
    }
    iot->mutex_inited = _gf_true;

    GF_REF_INIT(iot, gf_iot_cleanup);

    iot->state = IOT_INITED;

    ret = set_stack_size(iot);
    if (ret != 0)
        goto out;

    for (i = 0; i < GF_FOP_PRI_MAX; i++) {
        INIT_LIST_HEAD(&iot->clients[i]);
        INIT_LIST_HEAD(&iot->no_client[i].clients);
        INIT_LIST_HEAD(&iot->no_client[i].reqs);
    }
out:
    return iot;
}

void
gf_iot_free(gf_iot_t *iot)
{
    if (!iot)
        goto out;

    if (!(iot->state & IOT_STOPPED)) {
        gf_msg("io-threads", GF_LOG_ERROR, 0, LG_MSG_IO_THREADS_FREE_FAILED,
               "cannot free iot as the io-threads are still "
               "running due to a ref");
        goto out;
    }

    if (iot->cond_inited) {
        pthread_cond_destroy(&iot->cond);
    }
    if (iot->mutex_inited) {
        pthread_mutex_destroy(&iot->mutex);
    }

    GF_FREE(iot);

out:
    return;
}

int
gf_iot_client_destroy(xlator_t *this, client_t *client)
{
    void *tmp = NULL;

    if (client_ctx_del(client, this, &tmp) == 0) {
        GF_FREE(tmp);
    }

    return 0;
}

int
gf_iot_disconnect_cbk(xlator_t *this, client_t *client)
{
    int i;
    call_stub_t *curr;
    call_stub_t *next;
    gf_iot_t *iot = this->ctx->iot;
    iot_client_ctx_t *ctx;

    if (!iot || !iot->cleanup_disconnected_reqs) {
        goto out;
    }

    pthread_mutex_lock(&iot->mutex);
    for (i = 0; i < GF_FOP_PRI_MAX; i++) {
        ctx = &iot->no_client[i];
        list_for_each_entry_safe(curr, next, &ctx->reqs, list)
        {
            if (curr->frame->root->client != client) {
                continue;
            }
            gf_log(this->name, GF_LOG_INFO,
                   "poisoning %s fop at %p for client %s",
                   gf_fop_list[curr->fop], curr, client->client_uid);
            curr->poison = _gf_true;
        }
    }
    pthread_mutex_unlock(&iot->mutex);

out:
    return 0;
}

iot_client_ctx_t *
iot_get_ctx(xlator_t *this, client_t *client)
{
    iot_client_ctx_t *ctx = NULL;
    iot_client_ctx_t *setted_ctx = NULL;
    int i;

    if (client_ctx_get(client, this, (void **)&ctx) != 0) {
        ctx = GF_CALLOC(GF_FOP_PRI_MAX, sizeof(*ctx),
                        gf_common_mt_iot_client_ctx_t);
        if (ctx) {
            for (i = 0; i < GF_FOP_PRI_MAX; ++i) {
                INIT_LIST_HEAD(&ctx[i].clients);
                INIT_LIST_HEAD(&ctx[i].reqs);
            }
            setted_ctx = client_ctx_set(client, this, ctx);
            if (ctx != setted_ctx) {
                GF_FREE(ctx);
                ctx = setted_ctx;
            }
        }
    }

    return ctx;
}

void
__iot_enqueue(gf_iot_t *iot, call_stub_t *stub, int pri)
{
    client_t *client = stub->frame->root->client;
    iot_client_ctx_t *ctx;

    if (pri < 0 || pri >= GF_FOP_PRI_MAX)
        pri = GF_FOP_PRI_MAX - 1;

    if (client) {
        ctx = iot_get_ctx(THIS, client);
        if (ctx) {
            ctx = &ctx[pri];
        }
    } else {
        ctx = NULL;
    }
    if (!ctx) {
        ctx = &iot->no_client[pri];
    }

    if (list_empty(&ctx->reqs)) {
        list_add_tail(&ctx->clients, &iot->clients[pri]);
    }
    list_add_tail(&stub->list, &ctx->reqs);

    iot->queue_size++;
    iot->queue_sizes[pri]++;
}

int
do_iot_schedule(gf_iot_t *iot, call_stub_t *stub, int pri)
{
    int ret = 0;

    pthread_mutex_lock(&iot->mutex);
    {
        __iot_enqueue(iot, stub, pri);

        pthread_cond_signal(&iot->cond);

        ret = __iot_workers_scale(iot);
    }
    pthread_mutex_unlock(&iot->mutex);

    return ret;
}

char *
iot_get_pri_meaning(gf_fop_pri_t pri)
{
    char *name = NULL;

    switch (pri) {
        case GF_FOP_PRI_HI:
            name = "fast";
            break;
        case GF_FOP_PRI_NORMAL:
            name = "normal";
            break;
        case GF_FOP_PRI_LO:
            name = "slow";
            break;
        case GF_FOP_PRI_LEAST:
            name = "least priority";
            break;
        case GF_FOP_PRI_MAX:
            name = "invalid";
            break;
        case GF_FOP_PRI_UNSPEC:
            name = "unspecified";
            break;
    }
    return name;
}

int
gf_iot_schedule(call_frame_t *frame, xlator_t *this, call_stub_t *stub)
{
    int ret = -1;
    gf_fop_pri_t pri = GF_FOP_PRI_MAX - 1;
    gf_iot_t *iot = this->ctx->iot;

    if ((frame->root->pid < GF_CLIENT_PID_MAX) && iot->least_priority) {
        pri = GF_FOP_PRI_LEAST;
        goto out;
    }

    switch (stub->fop) {
        case GF_FOP_OPEN:
        case GF_FOP_STAT:
        case GF_FOP_FSTAT:
        case GF_FOP_LOOKUP:
        case GF_FOP_ACCESS:
        case GF_FOP_READLINK:
        case GF_FOP_OPENDIR:
        case GF_FOP_STATFS:
        case GF_FOP_READDIR:
        case GF_FOP_READDIRP:
        case GF_FOP_GETACTIVELK:
        case GF_FOP_SETACTIVELK:
        case GF_FOP_ICREATE:
        case GF_FOP_NAMELINK:
            pri = GF_FOP_PRI_HI;
            break;

        case GF_FOP_CREATE:
        case GF_FOP_FLUSH:
        case GF_FOP_LK:
        case GF_FOP_INODELK:
        case GF_FOP_FINODELK:
        case GF_FOP_ENTRYLK:
        case GF_FOP_FENTRYLK:
        case GF_FOP_LEASE:
        case GF_FOP_UNLINK:
        case GF_FOP_SETATTR:
        case GF_FOP_FSETATTR:
        case GF_FOP_MKNOD:
        case GF_FOP_MKDIR:
        case GF_FOP_RMDIR:
        case GF_FOP_SYMLINK:
        case GF_FOP_RENAME:
        case GF_FOP_LINK:
        case GF_FOP_SETXATTR:
        case GF_FOP_GETXATTR:
        case GF_FOP_FGETXATTR:
        case GF_FOP_FSETXATTR:
        case GF_FOP_REMOVEXATTR:
        case GF_FOP_FREMOVEXATTR:
        case GF_FOP_PUT:
            pri = GF_FOP_PRI_NORMAL;
            break;

        case GF_FOP_READ:
        case GF_FOP_WRITE:
        case GF_FOP_FSYNC:
        case GF_FOP_TRUNCATE:
        case GF_FOP_FTRUNCATE:
        case GF_FOP_FSYNCDIR:
        case GF_FOP_XATTROP:
        case GF_FOP_FXATTROP:
        case GF_FOP_RCHECKSUM:
        case GF_FOP_FALLOCATE:
        case GF_FOP_DISCARD:
        case GF_FOP_ZEROFILL:
        case GF_FOP_SEEK:
            pri = GF_FOP_PRI_LO;
            break;

        case GF_FOP_FORGET:
        case GF_FOP_RELEASE:
        case GF_FOP_RELEASEDIR:
        case GF_FOP_GETSPEC:
            break;
        case GF_FOP_IPC:
        default:
            return -EINVAL;
    }
out:
    gf_msg_debug(this->name, 0, "%s scheduled as %s fop",
                 gf_fop_list[stub->fop], iot_get_pri_meaning(pri));

    ret = do_iot_schedule(iot, stub, pri);

    return ret;
}

static void
iot_apply_event(xlator_t *this, threshold_t *thresh)
{
    struct timespec now;
    time_t delta;

    /* Refresh for manual testing/debugging.  It's cheap. */
    THRESH_LIMIT = THRESH_SECONDS * (THRESH_EVENTS - 1);

    timespec_now(&now);

    if (thresh->value && thresh->update_time) {
        delta = now.tv_sec - thresh->update_time;
        /* Be careful about underflow. */
        if (thresh->value <= delta) {
            thresh->value = 0;
        } else {
            thresh->value -= delta;
        }
    }

    thresh->value += THRESH_SECONDS;
    if (thresh->value >= THRESH_LIMIT) {
        gf_log(this->name, GF_LOG_EMERG, "watchdog firing too often");
        /*
         * The default action for SIGTRAP is to dump core, but the fact
         * that it's distinct from other signals we use means that
         * there are other possibilities as well (e.g. drop into gdb or
         * invoke a special handler).
         */
        kill(getpid(), SIGTRAP);
    }

    thresh->update_time = now.tv_sec;
}

static void *
iot_watchdog(void *arg)
{
    xlator_t *this = arg;
    gf_iot_t *priv = this->ctx->iot;
    int i;
    int bad_times[GF_FOP_PRI_MAX] = {
        0,
    };
    threshold_t thresholds[GF_FOP_PRI_MAX] = {{
        0,
    }};

    for (;;) {
        sleep(max(priv->watchdog_secs / 5, 1));
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        pthread_mutex_lock(&priv->mutex);
        for (i = 0; i < GF_FOP_PRI_MAX; ++i) {
            if (priv->queue_marked[i]) {
                if (++bad_times[i] >= 5) {
                    gf_log(this->name, GF_LOG_WARNING, "queue %d stalled", i);
                    iot_apply_event(this, &thresholds[i]);
                    /*
                     * We might not get here if the event
                     * put us over our threshold.
                     */
                    ++(priv->ac_iot_limit[i]);
                    bad_times[i] = 0;
                }
            } else {
                bad_times[i] = 0;
            }
            priv->queue_marked[i] = (priv->queue_sizes[i] > 0);
        }
        pthread_mutex_unlock(&priv->mutex);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    }

    /* NOTREACHED */
    return NULL;
}

static void
__start_iot_watchdog(xlator_t *this, gf_iot_t *iot)
{
    int ret;

    if (iot->watchdog_running) {
        return;
    }

    ret = pthread_create(&iot->watchdog_thread, NULL, iot_watchdog, this);
    if (ret == 0) {
        iot->watchdog_running = _gf_true;
    } else {
        gf_log(this->name, GF_LOG_WARNING,
               "pthread_create(iot_watchdog) failed");
    }
}

static void
__stop_iot_watchdog(xlator_t *this, gf_iot_t *iot)
{
    if (!iot->watchdog_running) {
        return;
    }

    if (pthread_cancel(iot->watchdog_thread) != 0) {
        gf_log(this->name, GF_LOG_WARNING,
               "pthread_cancel(iot_watchdog) failed");
    }

    if (pthread_join(iot->watchdog_thread, NULL) != 0) {
        gf_log(this->name, GF_LOG_WARNING, "pthread_join(iot_watchdog) failed");
    }

    /* Failure probably means it's already dead. */
    iot->watchdog_running = _gf_false;
}

int
__gf_iot_start(xlator_t *this, gf_iot_t *iot)
{
    int ret = 0;

    ret = __iot_workers_scale(iot);
    if (ret == -1) {
        gf_msg(this->name, GF_LOG_ERROR, 0, LG_MSG_IO_THREADS_INIT_FAILED,
               "cannot initialize worker threads, exiting init");
        goto out;
    }
    if (iot->watchdog_secs > 0) {
        __start_iot_watchdog(this, iot);
    } else {
        __stop_iot_watchdog(this, iot);
    }
out:
    return ret;
}

int
gf_iot_get(xlator_t *this)
{
    gf_iot_t *iot = (gf_iot_t *)this->ctx->iot;
    int ret = 0;

    pthread_mutex_lock(&iot->mutex);
    {
        if (!(iot->state & IOT_STARTED))
            ret = __gf_iot_start(this, iot);
    }
    pthread_mutex_unlock(&iot->mutex);

    GF_REF_GET(iot);

    return ret;
}

void
gf_iot_put(xlator_t *this)
{
    gf_iot_t *iot = (gf_iot_t *)this->ctx->iot;

    GF_REF_PUT(iot);

    return;
}

int
gf_iot_reconf(xlator_t *this)
{
    int ret = 0;
    gf_iot_t *iot = (gf_iot_t *)this->ctx->iot;

    pthread_mutex_lock(&iot->mutex);
    {
        if (!(iot->state & IOT_STARTED))
            ret = __gf_iot_start(this, iot);
    }
    pthread_mutex_unlock(&iot->mutex);

    return ret;
}

static void
iot_exit_threads(gf_iot_t *iot)
{
    pthread_mutex_lock(&iot->mutex);
    {
        iot->state = IOT_STOPPED;
        /*Let all the threads know that xl is going down*/
        pthread_cond_broadcast(&iot->cond);
        while (iot->curr_count) /*Wait for threads to exit*/
            pthread_cond_wait(&iot->cond, &iot->mutex);

        __stop_iot_watchdog(THIS, iot);
    }
    pthread_mutex_unlock(&iot->mutex);
}

void
gf_iot_cleanup(void *data)
{
    gf_iot_t *iot = data;

    if (!iot)
        return;

    if (iot->state & IOT_STARTED)
        iot_exit_threads(iot);

    return;
}

int
gf_dump_iot_info(glusterfs_ctx_t *ctx)
{
    gf_iot_t *iot = NULL;
    char key_prefix[GF_DUMP_MAX_BUF_LEN];

    iot = (gf_iot_t *)ctx->iot;
    if (!iot)
        return 0;

    snprintf(key_prefix, GF_DUMP_MAX_BUF_LEN, "%s", "io-threads");

    gf_proc_dump_add_section(key_prefix);

    gf_proc_dump_write("maximum_threads_count", "%d", iot->max_count);
    gf_proc_dump_write("current_threads_count", "%d", iot->curr_count);
    gf_proc_dump_write("sleep_count", "%d", iot->sleep_count);
    gf_proc_dump_write("idle_time", "%d", iot->idle_time);
    gf_proc_dump_write("stack_size", "%zd", iot->stack_size);
    gf_proc_dump_write("high_priority_threads", "%d",
                       iot->ac_iot_limit[GF_FOP_PRI_HI]);
    gf_proc_dump_write("normal_priority_threads", "%d",
                       iot->ac_iot_limit[GF_FOP_PRI_NORMAL]);
    gf_proc_dump_write("low_priority_threads", "%d",
                       iot->ac_iot_limit[GF_FOP_PRI_LO]);
    gf_proc_dump_write("least_priority_threads", "%d",
                       iot->ac_iot_limit[GF_FOP_PRI_LEAST]);

    return 0;
}
