/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
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

#include "call-stub.h"
#include "defaults.h"
#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "io-threads.h"
#include <signal.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include "locking.h"
#include "timespec.h"
#include "hashfn.h"

void *iot_worker (void *arg);
int iot_workers_scale (iot_conf_t *conf);
int __iot_workers_scale (iot_conf_t *conf);
int32_t iot_reinit_clock (iot_conf_t *conf, int i, uint32_t total_weight);
int32_t __iot_reinit_clock (iot_conf_t *conf, int i, uint32_t total_weight);
struct volume_options options[];

#define IOT_FOP(name, frame, this, args ...)                                   \
        do {                                                                   \
                call_stub_t     *__stub     = NULL;                            \
                int              __ret      = -1;                              \
                                                                               \
                __stub = fop_##name##_stub(frame, default_##name##_resume, args);  \
                if (!__stub) {                                                 \
                        __ret = -ENOMEM;                                       \
                        goto out;                                              \
                }                                                              \
                                                                               \
                __ret = iot_schedule (frame, this, __stub);                    \
                                                                               \
        out:                                                                   \
                if (__ret < 0) {                                               \
                        default_##name##_failure_cbk (frame, -__ret);          \
                        if (__stub != NULL) {                                  \
                                call_stub_destroy (__stub);                    \
                        }                                                      \
                }                                                              \
        } while (0)

gf_boolean_t
__iot_ns_queue_empty (iot_ns_queue_t *queue)
{
        return (queue->size == 0);
}

/* Fetch a namespace queue for the given ns_info struct. If the hash was not
 * found or the clock is not enabled, then return NULL which will be substituted
 * by the unknown queue. */
iot_ns_queue_t *
__iot_get_ns_queue (iot_conf_t *conf, ns_info_t *info)
{
        char            ns_key[DICT_UINT32_KEY_SIZE];
        int32_t         ret   = -1;
        iot_ns_queue_t *queue = NULL;

        if (!conf->ns_weighted_queueing || !info->found) {
                gf_log (GF_IO_THREADS, GF_LOG_TRACE,
                        "Namespace not found for %u", info->hash);
                return NULL;
        }

        dict_uint32_to_key (info->hash, ns_key);
        ret = dict_get_ptr (conf->ns_queues, ns_key, (void **)&queue);

        if (ret) {
                gf_log (GF_IO_THREADS, GF_LOG_TRACE,
                        "Namespace not found for %u", info->hash);
        }

        /* If successful, return `queue`. */
        return (!ret && queue) ? queue : NULL;
}

/* When we parse a new namespace conf file, we create a whole new set of queue
 * structs; however, old requests may be sitting on old queues. This function
 * drains the old requests lists into the new queue, or alternatively appends
 * it onto the unknown queue if there is no corresponding new queue.
 * (i.e. if it was removed from the conf file) */
int
__iot_drain_ns_queue_foreach (dict_t *this, char *key, data_t *value, void *data)
{
        int             ret       = 0;
        iot_conf_t     *conf      = data;
        dict_t         *new_dict  = conf->ns_queues;
        iot_ns_queue_t *old_queue = data_to_ptr (value);
        iot_ns_queue_t *new_queue = NULL;
        int             i;

        ret = dict_get_ptr (new_dict, key, (void **)&new_queue);

        /* Don't drain the unknown queue. */
        if (old_queue == conf->ns_unknown_queue) {
                return 0;
        }

        for (i = 0; i < IOT_PRI_MAX; i++) {
                /* If we didn't find a "new queue" corresponding to the old,
                 * then drain into the unknown queue of that priority level. */
                if (!ret && new_queue) {
                        list_append_init (&old_queue[i].reqs,
                                          &new_queue[i].reqs);
                        new_queue[i].size += old_queue[i].size;
                } else {
                        list_append_init (&old_queue[i].reqs,
                                          &conf->ns_unknown_queue[i].reqs);
                        conf->ns_unknown_queue[i].size += old_queue[i].size;
                }
        }

        return 0;
}

/* Drain the namespace queues in old_dict, if there are any. Then free the dict
 * and clear the clock structs. */
void
__iot_drain_and_clear_clock (iot_conf_t *conf, dict_t *old_dict)
{
        int i;

        if (old_dict) {
                dict_foreach (old_dict, __iot_drain_ns_queue_foreach, conf);
                dict_destroy (old_dict);
        }

        for (i = 0; i < IOT_PRI_MAX; i++) {
                GF_FREE (conf->ns_clocks[i].slots);
                conf->ns_clocks[i].slots = NULL;
                conf->ns_clocks[i].idx = 0;
                conf->ns_clocks[i].size = 0;
        }
}

/* Parse a single namespace conf line. This constructs a new queue and puts it
 * into the namespace dictionary as well, skipping duplicated namespaces with
 * a warning. */
int32_t
__iot_ns_parse_conf_line (iot_conf_t *conf, char *file_line, uint32_t *total_weight)
{
        char            ns_key[DICT_UINT32_KEY_SIZE];
        char           *ns_name      = NULL;
        iot_ns_queue_t *queue        = NULL;
        int32_t         queue_weight = 1;
        uint32_t        ns_hash      = 0;
        int             i, ret = -1, scanned = -1;

        ns_name = GF_CALLOC (strlen (file_line), sizeof (char), 0);
        if (!ns_name) {
                gf_log (GF_IO_THREADS, GF_LOG_WARNING,
                        "Memory allocation error!");
                ret = ENOMEM;
                goto out;
        }

        /* Scan the line, skipping the second column which corresponds to a
         * throttling rate, which we don't particularly care about. */
        scanned = sscanf (file_line, "%s %*d %d", ns_name, &queue_weight);
        if (scanned < 1 || strlen (ns_name) < 1) {
                gf_log (GF_IO_THREADS, GF_LOG_WARNING,
                        "Empty or malformatted line \"%s\" while parsing", file_line);
                goto out;
        }

        /* Hash the namespace name, convert it to a key, then search the dict
         * for an entry matching this key. */
        ns_hash = SuperFastHash (ns_name, strlen (ns_name));

        gf_log (GF_IO_THREADS, GF_LOG_INFO,
                "Parsed namespace \'%s\' (%u)", ns_name, ns_hash);

        dict_uint32_to_key (ns_hash, ns_key);
        ret = dict_get_ptr (conf->ns_queues, ns_key, (void **)&queue);
        if (!ret && queue) {
                gf_log (GF_IO_THREADS, GF_LOG_WARNING,
                        "Duplicate-hashed queue found for namespace %s", ns_name);
                /* Since ret == 0, we won't free the queue inadvertently. */
                goto out;
        }

        queue = GF_CALLOC (IOT_PRI_MAX, sizeof (iot_ns_queue_t), 0);
        if (!queue) {
                gf_log (GF_IO_THREADS, GF_LOG_WARNING,
                        "Memory allocation error!");
                ret = -(ENOMEM);
                goto out;
        }

        /* Init queues. */
        for (i = 0; i < IOT_PRI_MAX; i++) {
                INIT_LIST_HEAD (&queue[i].reqs);
                queue[i].hash = ns_hash;
                queue[i].weight = conf->ns_default_weight;
                queue[i].size = 0;
        }

        ret = dict_set_ptr (conf->ns_queues, ns_key, queue);
        if (ret) {
                goto out;
        }

        ret = dict_set_dynstr_with_alloc (conf->hash_to_ns, ns_key, ns_name);
        if (ret) {
                goto out;
        }

        if (scanned < 2 || queue_weight < 1) {
                gf_log (GF_IO_THREADS, GF_LOG_WARNING,
                        "No weight (or too low) found in config line for namespace %s, "
                        "defaulting to weight of %u.", ns_name, conf->ns_default_weight);
        } else {
                gf_log (GF_IO_THREADS, GF_LOG_INFO,
                        "Parsed weight \'%s\' = %u", ns_name, queue_weight);
                for (i = 0; i < IOT_PRI_MAX; i++) {
                        queue[i].weight = (uint32_t) queue_weight;
                }
        }

        *total_weight += queue->weight;

out:
        if (ns_name) {
                GF_FREE (ns_name);
        }

        if (ret && queue) {
                GF_FREE (queue);
        }

        return ret;
}

/* This function (re)initializes the clock that is used by the en/de-queue
 * operations. */
void
__iot_reinit_ns_conf (iot_conf_t *conf)
{
        char             ns_unknown_key[DICT_UINT32_KEY_SIZE];
        dict_t          *old_dict, *new_dict;
        FILE            *fp = NULL;
        char            *line = NULL;
        size_t           len = 0;
        uint32_t         total_weight;
        int              i, ret = 0;

        if (!conf) {
                return;
        }

        if (conf->ns_weighted_queueing) {
                gf_log (GF_IO_THREADS, GF_LOG_INFO,
                        "Loading %s from disk.",
                        _IOT_NAMESPACE_CONF);

                fp = fopen (_IOT_NAMESPACE_CONF, "r");
                if (!fp) {
                        gf_log (GF_IO_THREADS, GF_LOG_INFO,
                                "Cannot open file for reading.");
                        ret = ENOENT;
                        goto out;
                }

                /* Save the old queues; we will need to drain old requests out
                 * of it once we make new queues. */
                old_dict = conf->ns_queues;
                conf->ns_queues = new_dict = get_new_dict ();

                /* Include the unknown queue weight, which isn't a parsed line. */
                total_weight = conf->ns_default_weight;

                /* Parse the new config file line-by-line, making a new queue
                 * for each namespace that is parsed. */
                while (getline (&line, &len, fp) != -1) {
                        __iot_ns_parse_conf_line (conf, line, &total_weight);
                }
                free (line);

                /* Drain old queues into new queues, or into unknown queue. */
                __iot_drain_and_clear_clock (conf, old_dict);

                /* We will add the unknown queue manually into the dictionaries. */
                dict_uint32_to_key (0, ns_unknown_key);
                ret = dict_set_dynstr_with_alloc (conf->hash_to_ns,
                                                  ns_unknown_key, "unknown");
                if (ret) {
                        goto out;
                }

                /* Set the ns_unknown_queue as static pointer so it's not freed
                 * in the queue drain step next time the automation. */
                ret = dict_set_static_ptr (conf->ns_queues, ns_unknown_key,
                                           conf->ns_unknown_queue);
                if (ret) {
                        goto out;
                }

                for (i = 0; i < IOT_PRI_MAX; i++) {
                        ret = __iot_reinit_clock (conf, i, total_weight);
                        if (ret) {
                                goto out;
                        }
                }

                /* Finally, keep our conf struct updated so we don't spuriously
                 * reconfigure the clock. */
                get_file_mtime (_IOT_NAMESPACE_CONF, &conf->ns_conf_mtime);
        }

        ret = 0;
out:
        if (fp) {
                fclose (fp);
        }

        if (ret) {
                gf_log (GF_IO_THREADS, GF_LOG_INFO,
                        "There was an error loading the namespaces conf file, "
                        "disabling clock.");
                conf->ns_weighted_queueing = _gf_false;
        }

        /* If our clock isn't enabled (or it was disabled because of an error)
         * then drain the queues if there are any. */
        if (!conf->ns_weighted_queueing) {
                old_dict = conf->ns_queues;
                /* This NULL signals to the drain that we're not populating any
                 * new queues... */
                conf->ns_queues = NULL;
                __iot_drain_and_clear_clock (conf, old_dict);
        }
}

/* This is a simple iterative algorithm which tries to allocate the lowest
 * amount of slots that maintains the same proportional amount of work given
 * to each namespace.
 *
 * Each namespace has a weight, and the proportion of "weight / total weight"
 * is something we'll call the "ideal" work ratio. If we have no latency and
 * infinite queues, this ratio is the amount of requests we serve (out of total)
 * for the given namespace.
 *
 * For a given namespace idx i, let the namespace's weight be W_i, and the total
 * weight be TW. The "ideal" percentage is thus given by W_i/TW. Now if we
 * choose some arbitrary total number of slots TS, the number of slots we
 * should give to the namespace is given by S_i = TS*(W_i/TW). Since this is
 * probably not an integer, we'll actually floor the number, meaning that the
 * sum of S_i for each namespace most likely doesn't equal the TS we chose
 * earlier. Let this "real" total sum of slots be RS.
 *
 * Now we have the concept of an "realized" percentage given by the ratio of
 * _allocated slots_ instead of just ideal weights, given by S_i/RS. We consider
 * this set of slot allocations to be ideal if these two ratios (slots and
 * weights) are "close enough", given by our ns-weight-tolerance option.
 *
 * We then use a loop to distribute the shares, using some modulo magic to
 * get a good, semi-even distribution in the slots array. The main concern here
 * is  trying to make sure that no single share is bunched up.
 * If we have namespaces A and B with 2 and 8 slots respectively, we should
 * shoot for a distribution like [A B B B B A B B B] unstead of like
 * [A A B B B B B B B B]. This loop tries its best to do that. We don't want
 * to shuffle randomly either, since there is still a risk of having a bad
 * bunching if our RNG is bad or we're unlucky... */
int32_t
__iot_reinit_clock (iot_conf_t *conf, int i, uint32_t total_weight)
{
        int32_t          ret = -1;
        uint32_t         try_slots, total_slots, slots_idx, rep;
        data_pair_t     *curr       = NULL;
        iot_ns_queue_t  *curr_queue = NULL;
        iot_ns_queue_t **slots      = NULL;
        char            *ns_name    = NULL;
        gf_boolean_t     fail;
        double           real_percentage;

        /* Initialize the "ideal" percentage each queue should have. */
        dict_foreach_inline (conf->ns_queues, curr) {
                curr_queue = data_to_ptr (curr->value);
                curr_queue = &curr_queue[i];

                curr_queue->percentage = ((double) curr_queue->weight)
                                            / total_weight;
        }

        /* We try to satisfy each percentage within some margin, first trying
         * 1 slot, until we get up to the total sum of all weights, which will
         * obviously satisfy each percentage but in many cases is far too large
         * for a slots matrix. */
        for (try_slots = 1; try_slots <= total_weight; try_slots++) {
                fail = _gf_false;
                total_slots = 0;

                /* Calculate how many slots each namespace much get. Since we're
                 * rounding integers, we keep track of the actual total number
                 * of slots in `total_slots`. */
                dict_foreach_inline (conf->ns_queues, curr) {
                        curr_queue = data_to_ptr (curr->value);
                        curr_queue = &curr_queue[i];

                        curr_queue->slots = (int) (curr_queue->percentage * try_slots);
                        total_slots += curr_queue->slots;

                        /* If we've allocated less than 1 slot for the queue,
                         * we should find a larger size. */
                        if (curr_queue->slots < 1) {
                                fail = _gf_true;
                                break;
                        }
                }

                if (fail) {
                        continue;
                }

                dict_foreach_inline (conf->ns_queues, curr) {
                        curr_queue = data_to_ptr (curr->value);
                        curr_queue = &curr_queue[i];

                        real_percentage = ((double) curr_queue->slots) / total_slots;
                        /* If the realized percentage is more than ns_weight_tolerance
                         * percent away from the ideal percentage, then let's try
                         * another number. */
                        if (abs (real_percentage - curr_queue->percentage) * 100.0
                              > conf->ns_weight_tolerance) {
                                fail = _gf_true;
                                break;
                        }
                }

                if (!fail) {
                        break;
                }
        }

        /* Report the fits that we have found. */
        dict_foreach_inline (conf->ns_queues, curr) {
                curr_queue = data_to_ptr (curr->value);
                curr_queue = &curr_queue[i];

                real_percentage = ((double) curr_queue->slots) / total_slots;
                ret = dict_get_str (conf->hash_to_ns, curr->key, &ns_name);

                if (ret || !ns_name) {
                        continue;
                }

                gf_log (GF_IO_THREADS, GF_LOG_INFO,
                        "Initializing namespace \'%s\' (weight: %d) with %d slots. "
                        "Ideal percentage: %0.2f%%, real percentage: %0.2f%%.",
                        ns_name, curr_queue->weight, curr_queue->slots,
                        curr_queue->percentage * 100.0, real_percentage * 100.0);
        }

        /* At this point, we've either found a good fit, or gotten all the way to
         * total_weight. In either case, we can start allocating slots. */
        slots = GF_CALLOC (total_slots, sizeof (iot_ns_queue_t *), 0);
        slots_idx = 0;
        rep = 0;

        if (!slots) {
                ret = -(ENOMEM);
                goto out;
        }

        /* Allocate slots, with some fun modulo-math to make sure that they're
         * well distributed. */
        while (total_slots != slots_idx) {
                dict_foreach_inline (conf->ns_queues, curr) {
                        curr_queue = data_to_ptr (curr->value);
                        curr_queue = &curr_queue[i];

                        if (curr_queue->slots == 0) {
                                continue;
                        }

                        if (rep % (total_slots / curr_queue->slots) == 0) {
                                slots[slots_idx++] = curr_queue;
                                curr_queue->slots--;
                        }
                }

                rep++;
        }

        /* Set the slots into the queue, and we're ready to go! */
        conf->ns_clocks[i].slots = slots;
        conf->ns_clocks[i].size = total_slots;
        conf->ns_clocks[i].idx = 0;

        ret = 0;
out:
        if (ret && slots) {
                GF_FREE (slots);
        }

        return ret;
}


void *
iot_reinit_ns_conf_thread (void *arg)
{
        xlator_t        *this       = arg;
        iot_conf_t      *conf       = this->private;
        time_t           curr_mtime = {0, };

        while (_gf_true) {
                sleep (conf->ns_conf_reinit_secs);

                get_file_mtime (_IOT_NAMESPACE_CONF, &curr_mtime);
                if (conf->ns_weighted_queueing && curr_mtime != conf->ns_conf_mtime) {
                        pthread_setcancelstate (PTHREAD_CANCEL_DISABLE, NULL);
                        pthread_mutex_lock (&conf->mutex);
                        {
                                __iot_reinit_ns_conf (conf);
                        }
                        pthread_mutex_unlock (&conf->mutex);
                        pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);
                } else {
                        gf_log (GF_IO_THREADS, GF_LOG_DEBUG,
                                "Config file %s not modified, skipping.",
                                _IOT_NAMESPACE_CONF);
                }
        }
}

static void
start_iot_reinit_ns_conf_thread (xlator_t *this)
{
        iot_conf_t      *priv = this->private;
        int              ret;

        if (!priv) {
                return;
        }

        if (priv->reinit_ns_conf_thread_running) {
                gf_log (GF_IO_THREADS, GF_LOG_INFO, "reinit_ns_conf_thread already started.");
                return;
        }

        gf_log (GF_IO_THREADS, GF_LOG_INFO, "Starting reinit_ns_conf_thread.");

        ret = pthread_create (&priv->reinit_ns_conf_thread, NULL, iot_reinit_ns_conf_thread, this);
        if (ret == 0) {
                priv->reinit_ns_conf_thread_running = _gf_true;
        } else {
                gf_log (this->name, GF_LOG_WARNING,
                        "pthread_create(iot_reinit_ns_conf_thread) failed");
        }
}

static void
stop_iot_reinit_ns_conf_thread (xlator_t *this)
{
        iot_conf_t      *priv   = this->private;

        if (!priv) {
                return;
        }

        if (!priv->reinit_ns_conf_thread_running) {
                gf_log (GF_IO_THREADS, GF_LOG_INFO, "reinit_ns_conf_thread already stopped.");
                return;
        }

        gf_log (GF_IO_THREADS, GF_LOG_INFO, "Stopping reinit_ns_conf_thread.");

        if (pthread_cancel (priv->reinit_ns_conf_thread) != 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "pthread_cancel(iot_reinit_ns_conf_thread) failed");
        }

        if (pthread_join (priv->reinit_ns_conf_thread, NULL) != 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "pthread_join(iot_reinit_ns_conf_thread) failed");
        }

        /* Failure probably means it's already dead. */
        priv->reinit_ns_conf_thread_running = _gf_false;
}

call_stub_t *
__iot_dequeue (iot_conf_t *conf, int *pri)
{
        call_stub_t    *stub = NULL;
        iot_ns_clock_t *curr_clock;
        iot_ns_queue_t *queue = NULL;
        int             i, start_idx;

        *pri = -1;

        for (i = 0; i < IOT_PRI_MAX; i++) {
                if (conf->ac_iot_count[i] >= conf->ac_iot_limit[i] ||
                    conf->queue_sizes[i] == 0) {
                        /* If we have too many workers currently serving this
                         * priority level, or no reqs at this level, skip. */
                        continue;
                }

                if (conf->ns_weighted_queueing) {
                        /* Get the clock for this priority level, and keep track
                         * of where the search started, so we know if we've
                         * searched through the whole clock. */
                        curr_clock = &conf->ns_clocks[i];
                        start_idx = curr_clock->idx;

                        do {
                                /* Get the queue for the current index (modulo
                                 * size), and increment the clock forward. */
                                queue = curr_clock->slots[curr_clock->idx];
                                curr_clock->idx = (curr_clock->idx + 1) % curr_clock->size;

                                /* If we have a request to serve, then we're done. */
                                if (!__iot_ns_queue_empty (queue)) {
                                        break;
                                }

                                /* Otherwise, keep searching until we've looped
                                 * back to the start. */
                                queue = NULL;
                        } while (curr_clock->idx != start_idx);

                        /* If we have no queue, we must've not found a req to
                         * serve. Let's try the next priority. */
                        if (!queue) {
                                continue;
                        }
                } else {
                        /* If our unknown queue is empty, we have no other
                         * queues to serve. */
                        if (__iot_ns_queue_empty (&conf->ns_unknown_queue[i])) {
                                continue;
                        }

                        /* Select the unknown queue as the next queue to
                         * serve a request from. */
                        queue = &conf->ns_unknown_queue[i];
                }

                /* Otherwise take a request off of the queue. */
                stub = list_first_entry (&queue->reqs, call_stub_t, list);
                list_del_init (&stub->list);

                /* Increment the number of workers serving this priority,
                 * and record which priority we are serving. Update queue
                 * sizes and set `pri` variable for the caller. */
                conf->ac_iot_count[i]++;
                conf->queue_marked[i] = _gf_false;

                conf->queue_size--;
                conf->queue_sizes[i]--;
                queue->size--;

                *pri = i;
                break;
        }

        return stub;
}


void
__iot_enqueue (iot_conf_t *conf, call_stub_t *stub, int pri)
{
        ns_info_t    *info = &stub->frame->root->ns_info;
        iot_ns_queue_t *queue = 0;

        /* If we have an invalid priority, set it to LEAST. */
        if (pri < 0 || pri >= IOT_PRI_MAX) {
                pri = IOT_PRI_MAX - 1;
        }

        /* Get the queue for this namespace. If we don't have one,
         * use the unknown queue that always exists in the conf struct. */
        queue = __iot_get_ns_queue (conf, info);
        if (!queue) {
                queue = conf->ns_unknown_queue;
        }

        /* Get the (pri)'th level queue, and add the request to the queue. */
        queue = &queue[pri];
        list_add_tail (&stub->list, &queue->reqs);

        /* Update size records. */
        conf->queue_size++;
        conf->queue_sizes[pri]++;
        queue->size++;
}


void *
iot_worker (void *data)
{
        iot_conf_t       *conf = NULL;
        xlator_t         *this = NULL;
        call_stub_t      *stub = NULL;
        struct timespec   sleep_till = {0, };
        int               ret = 0;
        int               pri = -1;
        char              timeout = 0;
        char              bye = 0;

        conf = data;
        this = conf->this;
        THIS = this;
        gf_log (GF_IO_THREADS, GF_LOG_DEBUG, "IOT worker spawned.");

        while (_gf_true) {
                pthread_mutex_lock (&conf->mutex);
                {
                        if (pri != -1) {
                                conf->ac_iot_count[pri]--;
                                pri = -1;
                        }

                        while (conf->queue_size == 0) {
                                clock_gettime (CLOCK_REALTIME_COARSE, &sleep_till);
                                sleep_till.tv_sec += conf->idle_time;

                                conf->sleep_count++;
                                ret = pthread_cond_timedwait (&conf->cond,
                                                              &conf->mutex,
                                                              &sleep_till);
                                conf->sleep_count--;

                                if (ret == ETIMEDOUT) {
                                        timeout = 1;
                                        break;
                                }
                        }

                        if (timeout) {
                                if (conf->curr_count > IOT_MIN_THREADS) {
                                        conf->curr_count--;
                                        bye = 1;
                                        gf_log (conf->this->name, GF_LOG_DEBUG,
                                                "timeout, terminated. conf->curr_count=%d",
                                                conf->curr_count);
                                } else {
                                        timeout = 0;
                                }
                        }

                        stub = __iot_dequeue (conf, &pri);
                }
                pthread_mutex_unlock (&conf->mutex);

                if (stub) { /* guard against spurious wakeups */
                        if (stub->poison) {
                                gf_log (this->name, GF_LOG_INFO,
                                        "dropping poisoned request %p", stub);
                                call_stub_destroy (stub);
                        } else {
                                call_resume (stub);
                        }
                }

                if (bye) {
                        break;
                }
        }

        if (pri != -1) {
                pthread_mutex_lock (&conf->mutex);
                {
                        conf->ac_iot_count[pri]--;
                }
                pthread_mutex_unlock (&conf->mutex);
        }

        gf_log (GF_IO_THREADS, GF_LOG_DEBUG, "IOT worker died.");
        return NULL;
}


int
do_iot_schedule (iot_conf_t *conf, call_stub_t *stub, int pri)
{
        int   ret = 0;
        int   active_count = 0;

        pthread_mutex_lock (&conf->mutex);
        {
                __iot_enqueue (conf, stub, pri);

                /* If we have an ample supply of threads alive already
                 * it's massively more efficient to keep the ones you have
                 * busy vs making new ones and signaling everyone
                 */
                active_count = conf->curr_count - conf->sleep_count;
                if (conf->fops_per_thread_ratio == 0 || active_count == 0 ||
                    (conf->queue_size/active_count >
                     conf->fops_per_thread_ratio &&
                     active_count < conf->max_count)) {
                        pthread_cond_signal (&conf->cond);

                        ret = __iot_workers_scale (conf);
                }
        }
        pthread_mutex_unlock (&conf->mutex);

        return ret;
}

char*
iot_get_pri_meaning (iot_pri_t pri)
{
        char    *name = NULL;
        switch (pri) {
        case IOT_PRI_UNSPEC:
                name = "unspecified";
                break;
        case IOT_PRI_HI:
                name = "fast";
                break;
        case IOT_PRI_NORMAL:
                name = "normal";
                break;
        case IOT_PRI_LO:
                name = "slow";
                break;
        case IOT_PRI_LEAST:
                name = "least priority";
                break;
        case IOT_PRI_MAX:
                name = "invalid";
                break;
        }
        return name;
}


int
iot_schedule (call_frame_t *frame, xlator_t *this, call_stub_t *stub)
{
        int             ret = -1;
        iot_pri_t       pri = IOT_PRI_MAX - 1;
        iot_conf_t      *conf = this->private;

        if ((frame->root->pid < GF_CLIENT_PID_MAX) && conf->least_priority) {
                pri = IOT_PRI_LEAST;
                goto out;
        }

        if (frame->pri != IOT_PRI_UNSPEC) {
                pri = frame->pri;
                goto out;
        }

        // Retrieve the fop priority
        pri = iot_fop_to_pri (stub->fop);

out:
        gf_log (this->name, GF_LOG_DEBUG, "%s scheduled as %s fop",
                gf_fop_list[stub->fop], iot_get_pri_meaning (pri));
        ret = do_iot_schedule (this->private, stub, pri);
        return ret;
}

int
iot_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        IOT_FOP (lookup, frame, this, loc, xdata);
        return 0;
}


int
iot_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
             struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        IOT_FOP (setattr, frame, this, loc, stbuf, valid, xdata);
        return 0;
}


int
iot_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
              struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        IOT_FOP (fsetattr, frame, this, fd, stbuf, valid, xdata);
        return 0;
}


int
iot_access (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask,
            dict_t *xdata)
{
        IOT_FOP (access, frame, this, loc, mask, xdata);
        return 0;
}


int
iot_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size, dict_t *xdata)
{
        IOT_FOP (readlink, frame, this, loc, size, xdata);
        return 0;
}


int
iot_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
           dev_t rdev, mode_t umask, dict_t *xdata)
{
        IOT_FOP (mknod, frame, this, loc, mode, rdev, umask, xdata);
        return 0;
}


int
iot_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
           mode_t umask, dict_t *xdata)
{
        IOT_FOP (mkdir, frame, this, loc, mode, umask, xdata);
        return 0;
}


int
iot_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags, dict_t *xdata)
{
        IOT_FOP (rmdir, frame, this, loc, flags, xdata);
        return 0;
}


int
iot_symlink (call_frame_t *frame, xlator_t *this, const char *linkname,
             loc_t *loc, mode_t umask, dict_t *xdata)
{
        IOT_FOP (symlink, frame, this, linkname, loc, umask, xdata);
        return 0;
}


int
iot_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
            dict_t *xdata)
{
        IOT_FOP (rename, frame, this, oldloc, newloc, xdata);
        return 0;
}


int
iot_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
          fd_t *fd, dict_t *xdata)
{
        IOT_FOP (open, frame, this, loc, flags, fd, xdata);
        return 0;
}


int
iot_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
            mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        IOT_FOP (create, frame, this, loc, flags, mode, umask, fd, xdata);
        return 0;
}


int
iot_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
           off_t offset, uint32_t flags, dict_t *xdata)
{
        IOT_FOP (readv, frame, this, fd, size, offset, flags, xdata);
        return 0;
}


int
iot_flush (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        IOT_FOP (flush, frame, this, fd, xdata);
        return 0;
}


int
iot_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t datasync,
           dict_t *xdata)
{
        IOT_FOP (fsync, frame, this, fd, datasync, xdata);
        return 0;
}


int
iot_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
            struct iovec *vector, int32_t count, off_t offset,
            uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
        IOT_FOP (writev, frame, this, fd, vector, count, offset,
                 flags, iobref, xdata);
        return 0;
}


int
iot_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
	struct gf_flock *flock, dict_t *xdata)
{
        IOT_FOP (lk, frame, this, fd, cmd, flock, xdata);
        return 0;
}


int
iot_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        IOT_FOP (stat, frame, this, loc, xdata);
        return 0;
}


int
iot_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        IOT_FOP (fstat, frame, this, fd, xdata);
        return 0;
}


int
iot_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
              dict_t *xdata)
{
        IOT_FOP (truncate, frame, this, loc, offset, xdata);
        return 0;
}


int
iot_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
               dict_t *xdata)
{
        IOT_FOP (ftruncate, frame, this, fd, offset, xdata);
        return 0;
}



int
iot_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t xflag,
            dict_t *xdata)
{
        IOT_FOP (unlink, frame, this, loc, xflag, xdata);
        return 0;
}


int
iot_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
          dict_t *xdata)
{
        IOT_FOP (link, frame, this, oldloc, newloc, xdata);
        return 0;
}


int
iot_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
             dict_t *xdata)
{
        IOT_FOP (opendir, frame, this, loc, fd, xdata);
        return 0;
}


int
iot_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd, int datasync,
              dict_t *xdata)
{
        IOT_FOP (fsyncdir, frame, this, fd, datasync, xdata);
        return 0;
}


int
iot_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        IOT_FOP (statfs, frame, this, loc, xdata);
        return 0;
}


int
iot_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
              int32_t flags, dict_t *xdata)
{
        IOT_FOP (setxattr, frame, this, loc, dict, flags, xdata);
        return 0;
}


/* Populate all queue size keys for a specific priority level. */
int
__iot_populate_ns_queue_sizes (iot_conf_t *conf, dict_t *depths, int pri)
{
        int             ret        = 0;
        data_pair_t    *curr       = NULL;
        iot_ns_queue_t *curr_queue = NULL;
        char           *temp_key   = NULL;
        char           *ns_name    = NULL;
        const char     *pri_str    = fop_pri_to_string (pri);;

        /* For each namespace at this priority level, we try to grab the n
         * namespace name and record a key like `namespaces.NAME.PRI_LEVEL`. */
        dict_foreach_inline (conf->ns_queues, curr) {
                curr_queue = data_to_ptr (curr->value);
                curr_queue = &curr_queue[pri];

                /* Try to retrieve the namespace's un-hashed real name. */
                ret = dict_get_str (conf->hash_to_ns, curr->key, &ns_name);
                if (ret || !ns_name) {
                        continue;
                }

                /* Give the root namespace a readable name. */
                if (strncmp (ns_name, "/", 1) == 0) {
                        ns_name = "root";
                }

                /* Print a new temporary key for the namespace and priority level. */
                ret = gf_asprintf (&temp_key, "namespaces.%s.%s", ns_name, pri_str);
                if (ret == -1 || !temp_key) {
                        ret = -(ENOMEM);
                        goto out;
                }

                /* Insert the key and queue-size. */
                ret = dict_set_int32 (depths, temp_key, curr_queue->size);
		GF_FREE (temp_key);
		temp_key = NULL;

                if (ret) {
                        goto out;
                }
        }

out:
        return ret;
}

/* Populate global queue counts (per-priority) and if namespace queueing is
 * enabled, then also add those keys to the dictionary as well. */
int
__iot_populate_queue_sizes (iot_conf_t *conf, dict_t **depths)
{
        int         ret = 0, pri = 0;
        const char *pri_str = NULL;

        /* We explicitly do not want a reference count for this dict
         * in this translator, since it will get freed in io_stats later. */
        *depths = get_new_dict ();
        if (!*depths) {
                return -(ENOMEM);
        }

        for (pri = 0; pri < IOT_PRI_MAX; pri++) {
                pri_str = fop_pri_to_string (pri);

                /* First, let's add an entry for the number of requests
                 * per prority (globally). */
                ret = dict_set_int32 (*depths, (char *)pri_str,
                                      conf->queue_sizes[pri]);
                if (ret) {
                        goto out;
                }

                /* If namespace queueing is enabled, then try to populate
                 * per-namespace queue keys as well. */
                if (conf->ns_weighted_queueing) {
                        ret = __iot_populate_ns_queue_sizes (conf, *depths, pri);
                        if (ret) {
                                goto out;
                        }
                }
        }

out:
        if (ret) {
                dict_destroy (*depths);
                *depths = NULL;
        }

        return ret;
}

int
iot_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
              const char *name, dict_t *xdata)
{
        iot_conf_t     *conf       = this->private;
        dict_t         *depths     = NULL;

        if (name && strcmp (name, IO_THREADS_QUEUE_SIZE_KEY) == 0) {
                /* Populate depths dict, or it'll stay NULL if an error occurred. */
                pthread_mutex_lock (&conf->mutex);
                {
                        __iot_populate_queue_sizes (conf, &depths);
                }
                pthread_mutex_unlock (&conf->mutex);

                STACK_UNWIND_STRICT (getxattr, frame, 0, 0, depths, xdata);
                return 0;
        }

        IOT_FOP (getxattr, frame, this, loc, name, xdata);
        return 0;
}


int
iot_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
               const char *name, dict_t *xdata)
{
        IOT_FOP (fgetxattr, frame, this, fd, name, xdata);
        return 0;
}


int
iot_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
               int32_t flags, dict_t *xdata)
{
        IOT_FOP (fsetxattr, frame, this, fd, dict, flags, xdata);
        return 0;
}


int
iot_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                 const char *name, dict_t *xdata)
{
        IOT_FOP (removexattr, frame, this, loc, name, xdata);
        return 0;
}

int
iot_fremovexattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 const char *name, dict_t *xdata)
{
        IOT_FOP (fremovexattr, frame, this, fd, name, xdata);
        return 0;
}


int
iot_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
              off_t offset, dict_t *xdata)
{
        IOT_FOP (readdirp, frame, this, fd, size, offset, xdata);
        return 0;
}


int
iot_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t offset, dict_t *xdata)
{
        IOT_FOP (readdir, frame, this, fd, size, offset, xdata);
        return 0;
}

int
iot_inodelk (call_frame_t *frame, xlator_t *this,
             const char *volume, loc_t *loc, int32_t cmd, struct gf_flock *lock,
             dict_t *xdata)
{
        IOT_FOP (inodelk, frame, this, volume, loc, cmd, lock, xdata);
        return 0;
}

int
iot_finodelk (call_frame_t *frame, xlator_t *this,
              const char *volume, fd_t *fd, int32_t cmd, struct gf_flock *lock,
              dict_t *xdata)
{
        IOT_FOP (finodelk, frame, this, volume, fd, cmd, lock, xdata);
        return 0;
}

int
iot_entrylk (call_frame_t *frame, xlator_t *this,
             const char *volume, loc_t *loc, const char *basename,
             entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
        IOT_FOP (entrylk, frame, this, volume, loc, basename, cmd, type, xdata);
        return 0;
}

int
iot_fentrylk (call_frame_t *frame, xlator_t *this,
              const char *volume, fd_t *fd, const char *basename,
              entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
        IOT_FOP (fentrylk, frame, this, volume, fd, basename, cmd, type, xdata);
        return 0;
}


int
iot_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
             gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
        IOT_FOP (xattrop, frame, this, loc, optype, xattr, xdata);
        return 0;
}


int
iot_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
              gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
        IOT_FOP (fxattrop, frame, this, fd, optype, xattr, xdata);
        return 0;
}


int32_t
iot_rchecksum (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
               int32_t len, dict_t *xdata)
{
        IOT_FOP (rchecksum, frame, this, fd, offset, len, xdata);
        return 0;
}

int
iot_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t mode,
	      off_t offset, size_t len, dict_t *xdata)
{
        IOT_FOP (fallocate, frame, this, fd, mode, offset, len, xdata);
        return 0;
}

int
iot_discard(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
	    size_t len, dict_t *xdata)
{
        IOT_FOP (discard, frame, this, fd, offset, len, xdata);
        return 0;
}

int
iot_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
            off_t len, dict_t *xdata)
{
        IOT_FOP (zerofill, frame, this, fd, offset, len, xdata);
        return 0;
}


int
__iot_workers_scale (iot_conf_t *conf)
{
        int       scale = 0;
        int       diff = 0;
        pthread_t thread;
        int       ret = 0;
        int       i = 0;

        for (i = 0; i < IOT_PRI_MAX; i++)
                scale += min (conf->queue_sizes[i], conf->ac_iot_limit[i]);

        if (scale < IOT_MIN_THREADS)
                scale = IOT_MIN_THREADS;

        if (scale > conf->max_count)
                scale = conf->max_count;

        if (conf->curr_count < scale) {
                diff = scale - conf->curr_count;
        }

        while (diff) {
                diff --;

                ret = gf_thread_create (&thread, &conf->w_attr, iot_worker, conf);
                if (ret == 0) {
                        conf->curr_count++;
                        gf_log (conf->this->name, GF_LOG_DEBUG,
                                "scaled threads to %d (queue_size=%d/%d)",
                                conf->curr_count, conf->queue_size, scale);
                } else {
                        break;
                }
        }

        return diff;
}


int
iot_workers_scale (iot_conf_t *conf)
{
        int     ret = -1;

        if (conf == NULL) {
                ret = -EINVAL;
                goto out;
        }

        pthread_mutex_lock (&conf->mutex);
        {
                ret = __iot_workers_scale (conf);
        }
        pthread_mutex_unlock (&conf->mutex);

out:
        return ret;
}


void
set_stack_size (iot_conf_t *conf)
{
        int     err = 0;
        size_t  stacksize = IOT_THREAD_STACK_SIZE;
        xlator_t *this = NULL;

        this = THIS;

        pthread_attr_init (&conf->w_attr);
        err = pthread_attr_setstacksize (&conf->w_attr, stacksize);
        if (err == EINVAL) {
                err = pthread_attr_getstacksize (&conf->w_attr, &stacksize);
                if (!err)
                        gf_log (this->name, GF_LOG_WARNING,
                                "Using default thread stack size %zd",
                                stacksize);
                else
                        gf_log (this->name, GF_LOG_WARNING,
                                "Using default thread stack size");
        }

        conf->stack_size = stacksize;
}


int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_iot_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
                                "failed");
                return ret;
        }

        return ret;
}

int
iot_priv_dump (xlator_t *this)
{
        iot_conf_t     *conf   =   NULL;
        char           key_prefix[GF_DUMP_MAX_BUF_LEN];

        if (!this)
                return 0;

        conf = this->private;
        if (!conf)
                return 0;

        snprintf (key_prefix, GF_DUMP_MAX_BUF_LEN, "%s.%s", this->type,
                  this->name);

        gf_proc_dump_add_section(key_prefix);

        gf_proc_dump_write("maximum_threads_count", "%d", conf->max_count);
        gf_proc_dump_write("current_threads_count", "%d", conf->curr_count);
        gf_proc_dump_write("sleep_count", "%d", conf->sleep_count);
        gf_proc_dump_write("idle_time", "%d", conf->idle_time);
        gf_proc_dump_write("stack_size", "%zd", conf->stack_size);
        gf_proc_dump_write("high_priority_threads", "%d",
                           conf->ac_iot_limit[IOT_PRI_HI]);
        gf_proc_dump_write("normal_priority_threads", "%d",
                           conf->ac_iot_limit[IOT_PRI_NORMAL]);
        gf_proc_dump_write("low_priority_threads", "%d",
                           conf->ac_iot_limit[IOT_PRI_LO]);
        gf_proc_dump_write("least_priority_threads", "%d",
                           conf->ac_iot_limit[IOT_PRI_LEAST]);

        return 0;
}

/*
 * We use a decay model to keep track and make sure we're not spawning new
 * threads too often.  Each increment adds a large value to a counter, and that
 * counter keeps ticking back down to zero over a fairly long period.  For
 * example, let's use ONE_WEEK=604800 seconds, and we want to detect when we
 * have N=3 increments during that time.  Thus, our threshold is
 * (N-1)*ONE_WEEK.  To see how it works, look at three examples.
 *
 *   (a) Two events close together, then one more almost a week later.  The
 *   first two events push our counter to 2*ONE_WEEK plus a bit.  At the third
 *   event, we decay down to ONE_WEEK plus a bit and then add ONE_WEEK for the
 *   new event, exceeding our threshold.
 *
 *   (b) One event, then two more almost a week later.  At the time of the
 *   second and third events, the counter is already non-zero, so when we add
 *   2*ONE_WEEK we exceed again.
 *
 *   (c) Three events, spaced three days apart.  At the time of the second
 *   event, we decay down to approxitely ONE_WEEK*4/7 and then add another
 *   ONE_WEEK.  At the third event, we decay again down to ONE_WEEK*8/7 and add
 *   another ONE_WEEK, so boom.
 *
 * Note that in all three cases if that last event came a day later our counter
 * would have decayed a bit more and we would *not* exceed our threshold.  It's
 * not exactly the same as a precise "three in one week" limit, but it's very
 * close and it allows the same kind of tweaking while requiring only constant
 * space - no arrays of variable length N to allocate or maintain.  All we need
 * (for each queue) is the value plus the time of the last update.
 */

typedef struct {
        uint32_t        value;
        time_t          update_time;
} threshold_t;
/*
 * Variables so that I can hack these for testing.
 * TBD: make these tunable?
 */
static uint32_t        THRESH_SECONDS  = 604800;
static uint32_t        THRESH_EVENTS   = 3;
static uint32_t        THRESH_LIMIT    = 1209600;       /* SECONDS * (EVENTS-1) */

static void
iot_apply_event (xlator_t *this, threshold_t *thresh)
{
        struct timespec now;
        time_t          delta;

        /* Refresh for manual testing/debugging.  It's cheap. */
        THRESH_LIMIT = THRESH_SECONDS * (THRESH_EVENTS - 1);

        timespec_now (&now);

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
                gf_log (this->name, GF_LOG_EMERG, "watchdog firing too often");
                /*
                 * The default action for SIGTRAP is to dump core, but the fact
                 * that it's distinct from other signals we use means that
                 * there are other possibilities as well (e.g. drop into gdb or
                 * invoke a special handler).
                 */
                kill (getpid (), SIGTRAP);
        }

        thresh->update_time = now.tv_sec;
}

static void *
iot_watchdog (void *arg)
{
        xlator_t        *this   = arg;
        iot_conf_t      *priv   = this->private;
        int             i;
        int             bad_times[IOT_PRI_MAX] = { 0, };
        threshold_t     thresholds[IOT_PRI_MAX] = { { 0, } };

        for (;;) {
                sleep (max (priv->watchdog_secs/5, 1));
                pthread_setcancelstate (PTHREAD_CANCEL_DISABLE, NULL);
                pthread_mutex_lock (&priv->mutex);
                for (i = 0; i < IOT_PRI_MAX; ++i) {
                        if (priv->queue_marked[i]) {
                                if (++bad_times[i] >= 5) {
                                        gf_log (this->name, GF_LOG_WARNING,
                                                "queue %d stalled", i);
                                        iot_apply_event (this, &thresholds[i]);
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
                pthread_mutex_unlock (&priv->mutex);
                pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);
        }

        /* NOTREACHED */
        return NULL;
}

static void
start_iot_watchdog (xlator_t *this)
{
        iot_conf_t      *priv   = this->private;
        int             ret;

        if (priv->watchdog_running) {
                return;
        }

        ret = pthread_create (&priv->watchdog_thread, NULL, iot_watchdog, this);
        if (ret == 0) {
                priv->watchdog_running = _gf_true;
        } else {
                gf_log (this->name, GF_LOG_WARNING,
                        "pthread_create(iot_watchdog) failed");
        }
}

static void
stop_iot_watchdog (xlator_t *this)
{
        iot_conf_t      *priv   = this->private;

        if (!priv->watchdog_running) {
                return;
        }

        if (pthread_cancel (priv->watchdog_thread) != 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "pthread_cancel(iot_watchdog) failed");
        }

        if (pthread_join (priv->watchdog_thread, NULL) != 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "pthread_join(iot_watchdog) failed");
        }

        /* Failure probably means it's already dead. */
        priv->watchdog_running = _gf_false;
}

int
reconfigure (xlator_t *this, dict_t *options)
{
	iot_conf_t      *conf         = NULL;
	int		 i, ret = -1;
        gf_boolean_t     ns_weighted_queueing = _gf_false;

        conf = this->private;
        if (!conf)
                goto out;

        GF_OPTION_RECONF ("iam-brick-daemon", conf->iambrickd, options, bool, out);

        GF_OPTION_RECONF ("thread-count", conf->max_count, options, int32, out);

        GF_OPTION_RECONF ("fops-per-thread-ratio", conf->fops_per_thread_ratio,
                          options, int32, out);

        GF_OPTION_RECONF ("high-prio-threads",
                          conf->ac_iot_limit[IOT_PRI_HI], options, int32, out);

        GF_OPTION_RECONF ("normal-prio-threads",
                          conf->ac_iot_limit[IOT_PRI_NORMAL], options, int32,
                          out);

        GF_OPTION_RECONF ("low-prio-threads",
                          conf->ac_iot_limit[IOT_PRI_LO], options, int32, out);

        GF_OPTION_RECONF ("least-prio-threads",
                          conf->ac_iot_limit[IOT_PRI_LEAST], options, int32,
                          out);

        GF_OPTION_RECONF ("enable-least-priority", conf->least_priority,
                          options, bool, out);

        GF_OPTION_RECONF ("cleanup-disconnected-reqs",
                          conf->cleanup_disconnected_reqs, options, bool, out);

        GF_OPTION_RECONF ("watchdog-secs", conf->watchdog_secs, options,
                          int32, out);
        if (conf->watchdog_secs > 0) {
                start_iot_watchdog (this);
        } else {
                stop_iot_watchdog (this);
        }

        GF_OPTION_RECONF ("ns-weighted-queueing", ns_weighted_queueing, options, bool, out);
        GF_OPTION_RECONF ("ns-default-weight", conf->ns_default_weight, options, uint32, out);
        GF_OPTION_RECONF ("ns-weight-tolerance", conf->ns_weight_tolerance, options, double, out);
        GF_OPTION_RECONF ("ns-conf-reinit-secs", conf->ns_conf_reinit_secs, options,
                          uint32, out);

        if (!conf->iambrickd) {
                ns_weighted_queueing = _gf_false;
        }

        /* Reinit the default weight, which is the weight of the unknown queue. */
        for (i = 0; i < IOT_PRI_MAX; i++) {
                conf->ns_unknown_queue[i].weight = conf->ns_default_weight;
        }

        if (ns_weighted_queueing != conf->ns_weighted_queueing) {
                pthread_mutex_lock (&conf->mutex);
                {
                        conf->ns_weighted_queueing = ns_weighted_queueing;
                        __iot_reinit_ns_conf (conf);
                }
                pthread_mutex_unlock (&conf->mutex);
        }

        if (conf->ns_weighted_queueing) {
                start_iot_reinit_ns_conf_thread (this);
        } else {
                stop_iot_reinit_ns_conf_thread (this);
        }

        ret = 0;
out:
	return ret;
}


int
init (xlator_t *this)
{
        iot_conf_t *conf = NULL;
        int         i, ret = -1;

	if (!this->children || this->children->next) {
		gf_log ("io-threads", GF_LOG_ERROR,
			"FATAL: iot not configured with exactly one child");
                goto out;
	}

	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"dangling volume. check volfile ");
	}

	conf = (void *) GF_CALLOC (1, sizeof (*conf),
                                   gf_iot_mt_iot_conf_t);
        if (conf == NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory");
                goto out;
        }

        if ((ret = pthread_cond_init (&conf->cond, NULL)) != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "pthread_cond_init failed (%d)", ret);
                goto out;
        }

        if ((ret = pthread_mutex_init (&conf->mutex, NULL)) != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "pthread_mutex_init failed (%d)", ret);
                goto out;
        }

        set_stack_size (conf);

        GF_OPTION_INIT ("iam-brick-daemon", conf->iambrickd, bool, out);

        GF_OPTION_INIT ("thread-count", conf->max_count, int32, out);

        GF_OPTION_INIT ("fops-per-thread-ratio", conf->fops_per_thread_ratio,
                        int32, out);

        GF_OPTION_INIT ("high-prio-threads",
                        conf->ac_iot_limit[IOT_PRI_HI], int32, out);

        GF_OPTION_INIT ("normal-prio-threads",
                        conf->ac_iot_limit[IOT_PRI_NORMAL], int32, out);

        GF_OPTION_INIT ("low-prio-threads",
                        conf->ac_iot_limit[IOT_PRI_LO], int32, out);

        GF_OPTION_INIT ("least-prio-threads",
                        conf->ac_iot_limit[IOT_PRI_LEAST], int32, out);

        GF_OPTION_INIT ("idle-time", conf->idle_time, int32, out);

        GF_OPTION_INIT ("enable-least-priority", conf->least_priority,
                        bool, out);

        GF_OPTION_INIT ("cleanup-disconnected-reqs",
                        conf->cleanup_disconnected_reqs, bool, out);

        conf->this = this;

        GF_OPTION_INIT ("ns-weighted-queueing", conf->ns_weighted_queueing, bool, out);
        GF_OPTION_INIT ("ns-default-weight", conf->ns_default_weight, uint32, out);
        GF_OPTION_INIT ("ns-weight-tolerance", conf->ns_weight_tolerance, double, out);
        GF_OPTION_INIT ("ns-conf-reinit-secs", conf->ns_conf_reinit_secs, uint32, out);

        for (i = 0; i < IOT_PRI_MAX; i++) {
                INIT_LIST_HEAD (&conf->ns_unknown_queue[i].reqs);
                conf->ns_unknown_queue[i].hash = 0;
                conf->ns_unknown_queue[i].weight = conf->ns_default_weight;
                conf->ns_unknown_queue[i].size = 0;
        }

        if (!conf->iambrickd) {
                conf->ns_weighted_queueing = _gf_false;
        }

        conf->hash_to_ns = dict_new ();

        pthread_mutex_lock (&conf->mutex);
        {
                __iot_reinit_ns_conf (conf);
        }
        pthread_mutex_unlock (&conf->mutex);

	ret = iot_workers_scale (conf);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "cannot initialize worker threads, exiting init");
                goto out;
        }

	this->private = conf;

        if (conf->ns_weighted_queueing) {
                start_iot_reinit_ns_conf_thread (this);
        }

        conf->watchdog_secs = 0;
        GF_OPTION_INIT ("watchdog-secs", conf->watchdog_secs, int32, out);
        if (conf->watchdog_secs > 0) {
                start_iot_watchdog (this);
        }

        ret = 0;
out:
        if (ret) {
                GF_FREE (conf);
        }

	return ret;
}


void
fini (xlator_t *this)
{
	iot_conf_t *conf = this->private;

        stop_iot_watchdog (this);
        stop_iot_reinit_ns_conf_thread (this);

        dict_unref (conf->hash_to_ns);
        conf->hash_to_ns = NULL;

	GF_FREE (conf);

	this->private = NULL;
	return;
}

/* Clears a queue of requests from the given client (which is assumed to
 * have disconnected or otherwise stopped needing these requests...)  */
void
clear_reqs_from_queue (xlator_t *this, client_t *client, struct list_head *queue)
{
        call_stub_t     *curr;
        call_stub_t     *next;

        list_for_each_entry_safe (curr, next, queue, list) {
                if (curr->frame->root->client != client) {
                        continue;
                }
                gf_log (this->name, GF_LOG_INFO,
                        "poisoning %s fop at %p for client %s",
                        gf_fop_list[curr->fop], curr, client->client_uid);
                curr->poison = _gf_true;
        }
}

static int
iot_disconnect_cbk (xlator_t *this, client_t *client)
{
        iot_conf_t     *conf       = this->private;
        data_pair_t    *curr       = NULL;
        iot_ns_queue_t *curr_queue = NULL;
        int             i;

        if (!conf || !conf->cleanup_disconnected_reqs) {
                goto out;
        }

        pthread_mutex_lock (&conf->mutex);
        for (i = 0; i < IOT_PRI_MAX; i++) {
                /* Clear client reqs from the unknown queue. */
                clear_reqs_from_queue (this, client, &conf->ns_unknown_queue[i].reqs);
                /* Clear client reqs from each of the namespace queues. */
                if (conf->ns_weighted_queueing && conf->ns_queues) {
                        dict_foreach_inline (conf->ns_queues, curr) {
                                curr_queue = data_to_ptr (curr->value);
                                curr_queue = &curr_queue[i];
                                clear_reqs_from_queue (this, client, &curr_queue->reqs);
                        }
                }
        }
        pthread_mutex_unlock (&conf->mutex);

out:
        return 0;
}

struct xlator_dumpops dumpops = {
        .priv    = iot_priv_dump,
};

struct xlator_fops fops = {
	.open        = iot_open,
	.create      = iot_create,
	.readv       = iot_readv,
	.writev      = iot_writev,
	.flush       = iot_flush,
	.fsync       = iot_fsync,
	.lk          = iot_lk,
	.stat        = iot_stat,
	.fstat       = iot_fstat,
	.truncate    = iot_truncate,
	.ftruncate   = iot_ftruncate,
	.unlink      = iot_unlink,
        .lookup      = iot_lookup,
        .setattr     = iot_setattr,
        .fsetattr    = iot_fsetattr,
        .access      = iot_access,
        .readlink    = iot_readlink,
        .mknod       = iot_mknod,
        .mkdir       = iot_mkdir,
        .rmdir       = iot_rmdir,
        .symlink     = iot_symlink,
        .rename      = iot_rename,
        .link        = iot_link,
        .opendir     = iot_opendir,
        .fsyncdir    = iot_fsyncdir,
        .statfs      = iot_statfs,
        .setxattr    = iot_setxattr,
        .getxattr    = iot_getxattr,
        .fgetxattr   = iot_fgetxattr,
        .fsetxattr   = iot_fsetxattr,
        .removexattr = iot_removexattr,
        .fremovexattr = iot_fremovexattr,
        .readdir     = iot_readdir,
        .readdirp    = iot_readdirp,
        .inodelk     = iot_inodelk,
        .finodelk    = iot_finodelk,
        .entrylk     = iot_entrylk,
        .fentrylk    = iot_fentrylk,
        .xattrop     = iot_xattrop,
	.fxattrop    = iot_fxattrop,
        .rchecksum   = iot_rchecksum,
	.fallocate   = iot_fallocate,
	.discard     = iot_discard,
        .zerofill    = iot_zerofill,
};

struct xlator_cbks cbks = {
        .client_disconnect = iot_disconnect_cbk,
};

struct volume_options options[] = {
	{ .key  = {"thread-count"},
	  .type = GF_OPTION_TYPE_INT,
	  .min  = IOT_MIN_THREADS,
	  .max  = IOT_MAX_THREADS,
          .default_value = "32",
          .description = "Number of threads in IO threads translator which "
                         "perform concurrent IO operations"

	},
        { .key  = {"fops-per-thread-ratio"},
          .type = GF_OPTION_TYPE_INT,
          .min  = IOT_MIN_FOP_PER_THREAD,
          .max  = IOT_MAX_FOP_PER_THREAD,
          .default_value = "20",
          .description = "The optimal ratio of threads to FOPs in the queue "
                         "we wish to achieve before creating a new thread. "
                         "The idea here is it's far cheaper to keep our "
                         "currently running threads busy than spin up "
                         "new threads or cause a stampeding herd of threads "
                         "to service a singlular FOP when you have a thread "
                         "which will momentarily become available to do the "
                         "work."
        },
	{ .key  = {"high-prio-threads"},
	  .type = GF_OPTION_TYPE_INT,
	  .min  = IOT_MIN_THREADS,
	  .max  = IOT_MAX_THREADS,
          .default_value = "32",
          .description = "Max number of threads in IO threads translator which "
                         "perform high priority IO operations at a given time"

	},
	{ .key  = {"normal-prio-threads"},
	  .type = GF_OPTION_TYPE_INT,
	  .min  = IOT_MIN_THREADS,
	  .max  = IOT_MAX_THREADS,
          .default_value = "32",
          .description = "Max number of threads in IO threads translator which "
                         "perform normal priority IO operations at a given time"

	},
	{ .key  = {"low-prio-threads"},
	  .type = GF_OPTION_TYPE_INT,
	  .min  = IOT_MIN_THREADS,
	  .max  = IOT_MAX_THREADS,
          .default_value = "32",
          .description = "Max number of threads in IO threads translator which "
                         "perform low priority IO operations at a given time"

	},
	{ .key  = {"least-prio-threads"},
	  .type = GF_OPTION_TYPE_INT,
	  .min  = IOT_MIN_THREADS,
	  .max  = IOT_MAX_THREADS,
          .default_value = "1",
          .description = "Max number of threads in IO threads translator which "
                         "perform least priority IO operations at a given time"
	},
        { .key  = {"enable-least-priority"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "Enable/Disable least priority"
        },
        { .key   = {"idle-time"},
          .type  = GF_OPTION_TYPE_INT,
          .min   = 1,
          .max   = 0x7fffffff,
          .default_value = "120",
        },
        { .key   = {"watchdog-secs"},
          .type  = GF_OPTION_TYPE_INT,
          .min   = 0,
          .default_value = 0,
          .description = "Number of seconds a queue must be stalled before "
                         "starting an 'emergency' thread."
        },
        { .key  = {"cleanup-disconnected-reqs"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "Enable/Disable least priority"
        },
        { .key  = {"ns-weighted-queueing"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "Enables the namespace queues clock."
        },
        { .key   = {"ns-default-weight"},
          .type  = GF_OPTION_TYPE_INT,
          .min   = 1,
          .max   = 0x7fffffff,
          .default_value = "100",
          .description = "The default weight of a queue which doesn't have a "
                         "weight specified in the namespace conf. This is also "
                         "the weight of the unknown (default) queue."
        },
        { .key   = {"ns-weight-tolerance"},
          .type  = GF_OPTION_TYPE_DOUBLE,
          .default_value = "0.5",
          .description = "The tolerance in percentage (out of 100) that the "
                         "slot-allocation algorithm will tolerate for weight/total "
                         "and slots/total percentages. This corresponds to "
                         "ideal and realized workload percentages allocated "
                         "to the namespace."
        },
        { .key   = {"ns-conf-reinit-secs"},
          .type  = GF_OPTION_TYPE_INT,
          .min   = 1,
          .max   = 0x7fffffff,
          .default_value = "5",
          .description = "Number of seconds that the ns conf reinit thread "
                         "sleeps before trying to detect changes in the "
                         "namespace config file."
        },
        {
            .key = {"iam-brick-daemon"},
            .type = GF_OPTION_TYPE_BOOL,
            .default_value = "off",
            .description = "This option differentiates if the io-stats "
                           "translator is running as part of brick daemon "
                           "or not."
        },
	{ .key  = {NULL},
        },
};
