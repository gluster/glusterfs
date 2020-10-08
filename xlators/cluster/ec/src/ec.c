/*
  Copyright (c) 2012-2015 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <glusterfs/defaults.h>
#include <glusterfs/statedump.h>
#include <glusterfs/compat-errno.h>
#include <glusterfs/upcall-utils.h>

#include "ec.h"
#include "ec-messages.h"
#include "ec-mem-types.h"
#include "ec-types.h"
#include "ec-helpers.h"
#include "ec-common.h"
#include "ec-fops.h"
#include "ec-method.h"
#include "ec-code.h"
#include "ec-heald.h"
#include <glusterfs/events.h>

static char *ec_read_policies[EC_READ_POLICY_MAX + 1] = {
    [EC_ROUND_ROBIN] = "round-robin",
    [EC_GFID_HASH] = "gfid-hash",
    [EC_READ_POLICY_MAX] = NULL};

#define EC_INTERNAL_XATTR_OR_GOTO(name, xattr, op_errno, label)                \
    do {                                                                       \
        if (ec_is_internal_xattr(NULL, (char *)name, NULL, NULL)) {            \
            op_errno = EPERM;                                                  \
            goto label;                                                        \
        }                                                                      \
        if (name && (strlen(name) == 0) && xattr) {                            \
            /* Bulk [f]removexattr/[f]setxattr */                              \
            GF_IF_INTERNAL_XATTR_GOTO(EC_XATTR_PREFIX "*", xattr, op_errno,    \
                                      label);                                  \
        }                                                                      \
    } while (0)

int32_t
ec_parse_options(xlator_t *this)
{
    ec_t *ec = this->private;
    int32_t error = EINVAL;
    uintptr_t mask;

    GF_OPTION_INIT("redundancy", ec->redundancy, int32, out);
    ec->fragments = ec->nodes - ec->redundancy;
    if ((ec->redundancy < 1) || (ec->redundancy >= ec->fragments) ||
        (ec->fragments > EC_MAX_FRAGMENTS)) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, EC_MSG_INVALID_REDUNDANCY,
               "Invalid redundancy (must be between "
               "1 and %d)",
               (ec->nodes - 1) / 2);

        goto out;
    }

    ec->bits_for_nodes = 1;
    mask = 2;
    while (ec->nodes > mask) {
        ec->bits_for_nodes++;
        mask <<= 1;
    }
    ec->node_mask = (1ULL << ec->nodes) - 1ULL;
    ec->fragment_size = EC_METHOD_CHUNK_SIZE;
    ec->stripe_size = ec->fragment_size * ec->fragments;

    gf_msg_debug("ec", 0,
                 "Initialized with: nodes=%u, fragments=%u, "
                 "stripe_size=%u, node_mask=%" PRIxFAST32,
                 ec->nodes, ec->fragments, ec->stripe_size, ec->node_mask);

    error = 0;

out:
    return error;
}

int32_t
ec_prepare_childs(xlator_t *this)
{
    ec_t *ec = this->private;
    xlator_list_t *child = NULL;
    int32_t count = 0;

    for (child = this->children; child != NULL; child = child->next) {
        count++;
    }
    if (count > EC_MAX_NODES) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, EC_MSG_TOO_MANY_SUBVOLS,
               "Too many subvolumes");

        return EINVAL;
    }
    ec->nodes = count;

    ec->xl_list = GF_CALLOC(count, sizeof(ec->xl_list[0]), ec_mt_xlator_t);
    if (ec->xl_list == NULL) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, EC_MSG_NO_MEMORY,
               "Allocation of xlator list failed");

        return ENOMEM;
    }
    ec->xl_up = 0;
    ec->xl_up_count = 0;

    count = 0;
    for (child = this->children; child != NULL; child = child->next) {
        ec->xl_list[count++] = child->xlator;
    }

    return 0;
}

/* This function transforms the subvol to subvol-id*/
static int
_subvol_to_subvolid(dict_t *this, char *key, data_t *value, void *data)
{
    ec_t *ec = data;
    xlator_t *subvol = NULL;
    int i = 0;
    int ret = -1;

    subvol = data_to_ptr(value);
    for (i = 0; i < ec->nodes; i++) {
        if (ec->xl_list[i] == subvol) {
            ret = dict_set_int32(this, key, i);
            /* -1 stops dict_foreach and returns -1*/
            if (ret < 0)
                ret = -1;
            goto out;
        }
    }
out:
    return ret;
}

int
ec_subvol_to_subvol_id_transform(ec_t *ec, dict_t *leaf_to_subvolid)
{
    return dict_foreach(leaf_to_subvolid, _subvol_to_subvolid, ec);
}

void
__ec_destroy_private(xlator_t *this)
{
    ec_t *ec = this->private;

    if (ec != NULL) {
        LOCK(&ec->lock);

        if (ec->timer != NULL) {
            gf_timer_call_cancel(this->ctx, ec->timer);
            ec->timer = NULL;
        }

        UNLOCK(&ec->lock);

        /* There is a race with timer because there is no way to know if
         * timer callback has really been cancelled or it has been scheduled
         * for execution. If it has been scheduled, it will crash if we
         * destroy ec too fast.
         *
         * Not sure how this can be solved without using global variables or
         * having support from gf_timer_call_cancel()
         */
        sleep(2);

        this->private = NULL;
        if (ec->xl_list != NULL) {
            GF_FREE(ec->xl_list);
            ec->xl_list = NULL;
        }

        if (ec->fop_pool != NULL) {
            mem_pool_destroy(ec->fop_pool);
        }

        if (ec->cbk_pool != NULL) {
            mem_pool_destroy(ec->cbk_pool);
        }

        if (ec->lock_pool != NULL) {
            mem_pool_destroy(ec->lock_pool);
        }

        LOCK_DESTROY(&ec->lock);

        if (ec->leaf_to_subvolid)
            dict_unref(ec->leaf_to_subvolid);

        ec_method_fini(&ec->matrix);

        GF_FREE(ec);
    }
}

int32_t
mem_acct_init(xlator_t *this)
{
    if (xlator_mem_acct_init(this, ec_mt_end + 1) != 0) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, EC_MSG_NO_MEMORY,
               "Memory accounting initialization "
               "failed.");

        return -1;
    }

    return 0;
}

void
ec_configure_background_heal_opts(ec_t *ec, int background_heals,
                                  int heal_wait_qlen)
{
    if (background_heals == 0) {
        ec->heal_wait_qlen = 0;
    } else {
        ec->heal_wait_qlen = heal_wait_qlen;
    }
    ec->background_heals = background_heals;
}

int
ec_assign_read_policy(ec_t *ec, char *read_policy)
{
    int read_policy_idx = -1;

    read_policy_idx = gf_get_index_by_elem(ec_read_policies, read_policy);
    if (read_policy_idx < 0 || read_policy_idx >= EC_READ_POLICY_MAX)
        return -1;

    ec->read_policy = read_policy_idx;
    return 0;
}

int32_t
reconfigure(xlator_t *this, dict_t *options)
{
    ec_t *ec = this->private;
    char *read_policy = NULL;
    char *extensions = NULL;
    uint32_t heal_wait_qlen = 0;
    uint32_t background_heals = 0;
    int32_t ret = -1;
    int32_t err;

    GF_OPTION_RECONF("cpu-extensions", extensions, options, str, failed);

    GF_OPTION_RECONF("self-heal-daemon", ec->shd.enabled, options, bool,
                     failed);
    GF_OPTION_RECONF("iam-self-heal-daemon", ec->shd.iamshd, options, bool,
                     failed);
    GF_OPTION_RECONF("eager-lock", ec->eager_lock, options, bool, failed);
    GF_OPTION_RECONF("other-eager-lock", ec->other_eager_lock, options, bool,
                     failed);
    GF_OPTION_RECONF("eager-lock-timeout", ec->eager_lock_timeout, options,
                     uint32, failed);
    GF_OPTION_RECONF("other-eager-lock-timeout", ec->other_eager_lock_timeout,
                     options, uint32, failed);
    GF_OPTION_RECONF("background-heals", background_heals, options, uint32,
                     failed);
    GF_OPTION_RECONF("heal-wait-qlength", heal_wait_qlen, options, uint32,
                     failed);
    GF_OPTION_RECONF("self-heal-window-size", ec->self_heal_window_size,
                     options, uint32, failed);
    GF_OPTION_RECONF("heal-timeout", ec->shd.timeout, options, int32, failed);
    ec_configure_background_heal_opts(ec, background_heals, heal_wait_qlen);
    GF_OPTION_RECONF("shd-max-threads", ec->shd.max_threads, options, uint32,
                     failed);
    GF_OPTION_RECONF("shd-wait-qlength", ec->shd.wait_qlength, options, uint32,
                     failed);

    GF_OPTION_RECONF("read-policy", read_policy, options, str, failed);

    GF_OPTION_RECONF("optimistic-change-log", ec->optimistic_changelog, options,
                     bool, failed);
    GF_OPTION_RECONF("parallel-writes", ec->parallel_writes, options, bool,
                     failed);
    GF_OPTION_RECONF("stripe-cache", ec->stripe_cache, options, uint32, failed);
    GF_OPTION_RECONF("quorum-count", ec->quorum_count, options, uint32, failed);
    ret = 0;
    if (ec_assign_read_policy(ec, read_policy)) {
        ret = -1;
    }

    err = ec_method_update(this, &ec->matrix, extensions);
    if (err != 0) {
        ret = -1;
    }

failed:
    return ret;
}

glusterfs_event_t
ec_get_event_from_state(ec_t *ec)
{
    int down_count = 0;

    if (ec->xl_up_count >= ec->fragments) {
        /* If ec is up but some subvolumes are yet to notify, give
         * grace time for other subvols to notify to prevent start of
         * I/O which may result in self-heals */
        if (ec->xl_notify_count < ec->nodes)
            return GF_EVENT_MAXVAL;

        return GF_EVENT_CHILD_UP;
    } else {
        down_count = ec->xl_notify_count - ec->xl_up_count;
        if (down_count > ec->redundancy)
            return GF_EVENT_CHILD_DOWN;
    }

    return GF_EVENT_MAXVAL;
}

void
ec_up(xlator_t *this, ec_t *ec)
{
    char str1[32], str2[32];

    if (ec->timer != NULL) {
        gf_timer_call_cancel(this->ctx, ec->timer);
        ec->timer = NULL;
    }

    ec->up = 1;
    gf_msg(this->name, GF_LOG_INFO, 0, EC_MSG_EC_UP,
           "Going UP : Child UP = %s Child Notify = %s",
           ec_bin(str1, sizeof(str1), ec->xl_up, ec->nodes),
           ec_bin(str2, sizeof(str2), ec->xl_notify, ec->nodes));

    gf_event(EVENT_EC_MIN_BRICKS_UP, "subvol=%s", this->name);
}

void
ec_down(xlator_t *this, ec_t *ec)
{
    char str1[32], str2[32];

    if (ec->timer != NULL) {
        gf_timer_call_cancel(this->ctx, ec->timer);
        ec->timer = NULL;
    }

    ec->up = 0;
    gf_msg(this->name, GF_LOG_INFO, 0, EC_MSG_EC_DOWN,
           "Going DOWN : Child UP = %s Child Notify = %s",
           ec_bin(str1, sizeof(str1), ec->xl_up, ec->nodes),
           ec_bin(str2, sizeof(str2), ec->xl_notify, ec->nodes));

    gf_event(EVENT_EC_MIN_BRICKS_NOT_UP, "subvol=%s", this->name);
}

void
ec_notify_cbk(void *data)
{
    ec_t *ec = data;
    glusterfs_event_t event = GF_EVENT_MAXVAL;
    gf_boolean_t propagate = _gf_false;
    gf_boolean_t launch_heal = _gf_false;

    LOCK(&ec->lock);
    {
        if (!ec->timer) {
            /*
             * Either child_up/child_down is already sent to parent
             * This is a spurious wake up.
             */
            goto unlock;
        }

        gf_timer_call_cancel(ec->xl->ctx, ec->timer);
        ec->timer = NULL;

        /* The timeout has expired, so any subvolume that has not
         * already reported its state, will be considered to be down.
         * We mark as if all bricks had reported. */
        ec->xl_notify = (1ULL << ec->nodes) - 1ULL;
        ec->xl_notify_count = ec->nodes;

        /* Since we have marked all subvolumes as notified, it's
         * guaranteed that ec_get_event_from_state() will return
         * CHILD_UP or CHILD_DOWN, but not MAXVAL. */
        event = ec_get_event_from_state(ec);
        if (event == GF_EVENT_CHILD_UP) {
            /* We are ready to bring the volume up. If there are
             * still bricks DOWN, they will be healed when they
             * come up. */
            ec_up(ec->xl, ec);

            if (ec->shd.iamshd && !ec->shutdown) {
                launch_heal = _gf_true;
                GF_ATOMIC_INC(ec->async_fop_count);
            }
        }

        propagate = _gf_true;
    }
unlock:
    UNLOCK(&ec->lock);

    if (launch_heal) {
        /* We have just brought the volume UP, so we trigger
         * a self-heal check on the root directory. */
        ec_launch_replace_heal(ec);
    }
    if (propagate) {
        default_notify(ec->xl, event, NULL);
    }
}

void
ec_launch_notify_timer(xlator_t *this, ec_t *ec)
{
    struct timespec delay = {
        0,
    };

    gf_msg_debug(this->name, 0, "Initiating child-down timer");
    delay.tv_sec = 10;
    delay.tv_nsec = 0;
    ec->timer = gf_timer_call_after(this->ctx, delay, ec_notify_cbk, ec);
    if (ec->timer == NULL) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, EC_MSG_TIMER_CREATE_FAIL,
               "Cannot create timer "
               "for delayed initialization");
    }
}

gf_boolean_t
ec_disable_delays(ec_t *ec)
{
    ec->shutdown = _gf_true;

    return __ec_is_last_fop(ec);
}

void
ec_cleanup_healer_object(ec_t *ec)
{
    struct subvol_healer *healer = NULL;
    ec_self_heald_t *shd = NULL;
    void *res = NULL;
    int i = 0;
    gf_boolean_t is_join = _gf_false;

    shd = &ec->shd;
    if (!shd->iamshd)
        return;

    for (i = 0; i < ec->nodes; i++) {
        healer = &shd->index_healers[i];
        pthread_mutex_lock(&healer->mutex);
        {
            healer->rerun = 1;
            if (healer->running) {
                pthread_cond_signal(&healer->cond);
                is_join = _gf_true;
            }
        }
        pthread_mutex_unlock(&healer->mutex);
        if (is_join) {
            pthread_join(healer->thread, &res);
            is_join = _gf_false;
        }

        healer = &shd->full_healers[i];
        pthread_mutex_lock(&healer->mutex);
        {
            healer->rerun = 1;
            if (healer->running) {
                pthread_cond_signal(&healer->cond);
                is_join = _gf_true;
            }
        }
        pthread_mutex_unlock(&healer->mutex);
        if (is_join) {
            pthread_join(healer->thread, &res);
            is_join = _gf_false;
        }
    }
}
void
ec_pending_fops_completed(ec_t *ec)
{
    if (ec->shutdown) {
        default_notify(ec->xl, GF_EVENT_PARENT_DOWN, NULL);
    }
}

static gf_boolean_t
ec_set_up_state(ec_t *ec, uintptr_t index_mask, uintptr_t new_state)
{
    uintptr_t current_state = 0;

    if (xlator_is_cleanup_starting(ec->xl))
        return _gf_false;

    if ((ec->xl_notify & index_mask) == 0) {
        ec->xl_notify |= index_mask;
        ec->xl_notify_count++;
    }
    current_state = ec->xl_up & index_mask;
    if (current_state != new_state) {
        ec->xl_up ^= index_mask;
        ec->xl_up_count += (current_state ? -1 : 1);

        return _gf_true;
    }

    return _gf_false;
}

static gf_boolean_t
ec_upcall(ec_t *ec, struct gf_upcall *upcall)
{
    struct gf_upcall_cache_invalidation *ci = NULL;
    struct gf_upcall_inodelk_contention *lc = NULL;
    inode_t *inode;
    inode_table_t *table;

    switch (upcall->event_type) {
        case GF_UPCALL_CACHE_INVALIDATION:
            ci = upcall->data;
            ci->flags |= UP_INVAL_ATTR;
            return _gf_true;

        case GF_UPCALL_INODELK_CONTENTION:
            lc = upcall->data;
            if (strcmp(lc->domain, ec->xl->name) != 0) {
                /* The lock is not owned by EC, ignore it. */
                return _gf_true;
            }
            table = ((xlator_t *)ec->xl->graph->top)->itable;
            if (table == NULL) {
                /* Self-heal daemon doesn't have an inode table on the top
                 * xlator because it doesn't need it. In this case we should
                 * use the inode table managed by EC itself where all inodes
                 * being healed should be present. However self-heal doesn't
                 * use eager-locking and inodelk's are already released as
                 * soon as possible. In this case we can safely ignore these
                 * notifications. */
                return _gf_false;
            }
            inode = inode_find(table, upcall->gfid);
            /* If inode is not found, it means that it's already released,
             * so we can ignore it. Probably it has been released and
             * destroyed while the contention notification was being sent.
             */
            if (inode != NULL) {
                ec_lock_release(ec, inode);
                inode_unref(inode);
            }

            return _gf_false;

        default:
            return _gf_true;
    }
}

int32_t
ec_notify(xlator_t *this, int32_t event, void *data, void *data2)
{
    ec_t *ec = this->private;
    int32_t idx = 0;
    int32_t error = 0;
    glusterfs_event_t old_event = GF_EVENT_MAXVAL;
    dict_t *input = NULL;
    dict_t *output = NULL;
    gf_boolean_t propagate = _gf_true;
    gf_boolean_t needs_shd_check = _gf_false;
    int32_t orig_event = event;
    uintptr_t mask = 0;

    gf_msg_trace(this->name, 0, "NOTIFY(%d): %p, %p", event, data, data2);

    if (event == GF_EVENT_UPCALL) {
        propagate = ec_upcall(ec, data);
        goto done;
    }

    if (event == GF_EVENT_TRANSLATOR_OP) {
        if (!ec->up) {
            error = -1;
        } else {
            input = data;
            output = data2;
            error = ec_xl_op(this, input, output);
        }
        goto out;
    }

    for (idx = 0; idx < ec->nodes; idx++) {
        if (ec->xl_list[idx] == data) {
            break;
        }
    }

    LOCK(&ec->lock);

    if (event == GF_EVENT_PARENT_UP) {
        /*
         * Start a timer which sends appropriate event to parent
         * xlator to prevent the 'mount' syscall from hanging.
         */
        ec_launch_notify_timer(this, ec);
        goto unlock;
    } else if (event == GF_EVENT_PARENT_DOWN) {
        /* If there aren't pending fops running after we have waken up
         * them, we immediately propagate the notification. */
        propagate = ec_disable_delays(ec);
        ec_cleanup_healer_object(ec);
        goto unlock;
    }

    if (idx < ec->nodes) { /* CHILD_* events */
        old_event = ec_get_event_from_state(ec);

        mask = 1ULL << idx;
        if (event == GF_EVENT_CHILD_UP) {
            /* We need to trigger a selfheal if a brick changes
             * to UP state. */
            if (ec_set_up_state(ec, mask, mask) && ec->shd.iamshd &&
                !ec->shutdown) {
                needs_shd_check = _gf_true;
            }
        } else if (event == GF_EVENT_CHILD_DOWN) {
            ec_set_up_state(ec, mask, 0);
        }

        event = ec_get_event_from_state(ec);

        if (event == GF_EVENT_CHILD_UP) {
            if (!ec->up) {
                ec_up(this, ec);
            }
        } else {
            /* If the volume is not UP, it's irrelevant if one
             * brick has come up. We cannot heal anything. */
            needs_shd_check = _gf_false;

            if ((event == GF_EVENT_CHILD_DOWN) && ec->up) {
                ec_down(this, ec);
            }
        }

        if (event != GF_EVENT_MAXVAL) {
            if (event == old_event) {
                if (orig_event == GF_EVENT_CHILD_UP)
                    event = GF_EVENT_SOME_DESCENDENT_UP;
                else /* orig_event has to be GF_EVENT_CHILD_DOWN */
                    event = GF_EVENT_SOME_DESCENDENT_DOWN;
            }
        } else {
            propagate = _gf_false;
            needs_shd_check = _gf_false;
        }

        if (needs_shd_check) {
            GF_ATOMIC_INC(ec->async_fop_count);
        }
    }
unlock:
    UNLOCK(&ec->lock);

done:
    if (needs_shd_check) {
        ec_launch_replace_heal(ec);
    }
    if (propagate) {
        error = default_notify(this, event, data);
    }

out:
    return error;
}

int32_t
notify(xlator_t *this, int32_t event, void *data, ...)
{
    int ret = -1;
    va_list ap;
    void *data2 = NULL;

    va_start(ap, data);
    data2 = va_arg(ap, dict_t *);
    va_end(ap);
    ret = ec_notify(this, event, data, data2);

    return ret;
}

static void
ec_statistics_init(ec_t *ec)
{
    GF_ATOMIC_INIT(ec->stats.stripe_cache.hits, 0);
    GF_ATOMIC_INIT(ec->stats.stripe_cache.misses, 0);
    GF_ATOMIC_INIT(ec->stats.stripe_cache.updates, 0);
    GF_ATOMIC_INIT(ec->stats.stripe_cache.invals, 0);
    GF_ATOMIC_INIT(ec->stats.stripe_cache.evicts, 0);
    GF_ATOMIC_INIT(ec->stats.stripe_cache.allocs, 0);
    GF_ATOMIC_INIT(ec->stats.stripe_cache.errors, 0);
    GF_ATOMIC_INIT(ec->stats.shd.attempted, 0);
    GF_ATOMIC_INIT(ec->stats.shd.completed, 0);
}

static int
ec_assign_read_mask(ec_t *ec, char *read_mask_str)
{
    char *mask = NULL;
    char *maskptr = NULL;
    char *saveptr = NULL;
    char *id_str = NULL;
    int id = 0;
    int ret = 0;
    uintptr_t read_mask = 0;

    if (!read_mask_str) {
        ec->read_mask = 0;
        ret = 0;
        goto out;
    }

    mask = gf_strdup(read_mask_str);
    if (!mask) {
        ret = -1;
        goto out;
    }
    maskptr = mask;

    for (;;) {
        id_str = strtok_r(maskptr, ":", &saveptr);
        if (id_str == NULL)
            break;
        if (gf_string2int(id_str, &id)) {
            gf_msg(ec->xl->name, GF_LOG_ERROR, 0, EC_MSG_XLATOR_INIT_FAIL,
                   "In read-mask \"%s\" id %s is not a valid integer",
                   read_mask_str, id_str);
            ret = -1;
            goto out;
        }

        if ((id < 0) || (id >= ec->nodes)) {
            gf_msg(ec->xl->name, GF_LOG_ERROR, 0, EC_MSG_XLATOR_INIT_FAIL,
                   "In read-mask \"%s\" id %d is not in range [0 - %d]",
                   read_mask_str, id, ec->nodes - 1);
            ret = -1;
            goto out;
        }
        read_mask |= (1UL << id);
        maskptr = NULL;
    }

    if (gf_bits_count(read_mask) < ec->fragments) {
        gf_msg(ec->xl->name, GF_LOG_ERROR, 0, EC_MSG_XLATOR_INIT_FAIL,
               "read-mask \"%s\" should contain at least %d ids", read_mask_str,
               ec->fragments);
        ret = -1;
        goto out;
    }
    ec->read_mask = read_mask;
    ret = 0;
out:
    GF_FREE(mask);
    return ret;
}

int32_t
init(xlator_t *this)
{
    ec_t *ec = NULL;
    char *read_policy = NULL;
    char *extensions = NULL;
    int32_t err;
    char *read_mask_str = NULL;

    if (this->parents == NULL) {
        gf_msg(this->name, GF_LOG_WARNING, 0, EC_MSG_NO_PARENTS,
               "Volume does not have parents.");
    }

    ec = GF_MALLOC(sizeof(*ec), ec_mt_ec_t);
    if (ec == NULL) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, EC_MSG_NO_MEMORY,
               "Failed to allocate private memory.");

        return -1;
    }
    memset(ec, 0, sizeof(*ec));

    this->private = ec;

    ec->xl = this;
    LOCK_INIT(&ec->lock);

    GF_ATOMIC_INIT(ec->async_fop_count, 0);
    INIT_LIST_HEAD(&ec->pending_fops);
    INIT_LIST_HEAD(&ec->heal_waiting);
    INIT_LIST_HEAD(&ec->healing);

    ec->fop_pool = mem_pool_new(ec_fop_data_t, 1024);
    ec->cbk_pool = mem_pool_new(ec_cbk_data_t, 4096);
    ec->lock_pool = mem_pool_new(ec_lock_t, 1024);
    if ((ec->fop_pool == NULL) || (ec->cbk_pool == NULL) ||
        (ec->lock_pool == NULL)) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, EC_MSG_NO_MEMORY,
               "Failed to create memory pools.");

        goto failed;
    }

    if (ec_prepare_childs(this) != 0) {
        gf_msg(this->name, GF_LOG_ERROR, 0, EC_MSG_XLATOR_INIT_FAIL,
               "Failed to initialize xlator");

        goto failed;
    }

    if (ec_parse_options(this) != 0) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, EC_MSG_XLATOR_PARSE_OPT_FAIL,
               "Failed to parse xlator options");

        goto failed;
    }

    GF_OPTION_INIT("cpu-extensions", extensions, str, failed);

    err = ec_method_init(this, &ec->matrix, ec->fragments, ec->nodes,
                         ec->nodes * 2, extensions);
    if (err != 0) {
        gf_msg(this->name, GF_LOG_ERROR, -err, EC_MSG_MATRIX_FAILED,
               "Failed to initialize matrix management");

        goto failed;
    }

    GF_OPTION_INIT("self-heal-daemon", ec->shd.enabled, bool, failed);
    GF_OPTION_INIT("iam-self-heal-daemon", ec->shd.iamshd, bool, failed);
    GF_OPTION_INIT("eager-lock", ec->eager_lock, bool, failed);
    GF_OPTION_INIT("other-eager-lock", ec->other_eager_lock, bool, failed);
    GF_OPTION_INIT("eager-lock-timeout", ec->eager_lock_timeout, uint32,
                   failed);
    GF_OPTION_INIT("other-eager-lock-timeout", ec->other_eager_lock_timeout,
                   uint32, failed);
    GF_OPTION_INIT("background-heals", ec->background_heals, uint32, failed);
    GF_OPTION_INIT("heal-wait-qlength", ec->heal_wait_qlen, uint32, failed);
    GF_OPTION_INIT("self-heal-window-size", ec->self_heal_window_size, uint32,
                   failed);
    ec_configure_background_heal_opts(ec, ec->background_heals,
                                      ec->heal_wait_qlen);
    GF_OPTION_INIT("read-policy", read_policy, str, failed);
    if (ec_assign_read_policy(ec, read_policy))
        goto failed;

    GF_OPTION_INIT("heal-timeout", ec->shd.timeout, int32, failed);
    GF_OPTION_INIT("shd-max-threads", ec->shd.max_threads, uint32, failed);
    GF_OPTION_INIT("shd-wait-qlength", ec->shd.wait_qlength, uint32, failed);
    GF_OPTION_INIT("optimistic-change-log", ec->optimistic_changelog, bool,
                   failed);
    GF_OPTION_INIT("parallel-writes", ec->parallel_writes, bool, failed);
    GF_OPTION_INIT("stripe-cache", ec->stripe_cache, uint32, failed);
    GF_OPTION_INIT("quorum-count", ec->quorum_count, uint32, failed);
    GF_OPTION_INIT("ec-read-mask", read_mask_str, str, failed);

    if (ec_assign_read_mask(ec, read_mask_str))
        goto failed;

    this->itable = inode_table_new(EC_SHD_INODE_LRU_LIMIT, this, 0, 0);
    if (!this->itable)
        goto failed;

    if (ec->shd.iamshd)
        ec_selfheal_daemon_init(this);
    gf_msg_debug(this->name, 0, "Disperse translator initialized.");

    ec->leaf_to_subvolid = dict_new();
    if (!ec->leaf_to_subvolid)
        goto failed;
    if (glusterfs_reachable_leaves(this, ec->leaf_to_subvolid)) {
        gf_msg(this->name, GF_LOG_ERROR, 0, EC_MSG_SUBVOL_BUILD_FAIL,
               "Failed to build subvol "
               "dictionary");
        goto failed;
    }

    if (ec_subvol_to_subvol_id_transform(ec, ec->leaf_to_subvolid) < 0) {
        gf_msg(this->name, GF_LOG_ERROR, 0, EC_MSG_SUBVOL_ID_DICT_SET_FAIL,
               "Failed to build subvol-id "
               "dictionary");
        goto failed;
    }

    ec_statistics_init(ec);

    return 0;

failed:
    __ec_destroy_private(this);

    return -1;
}

void
fini(xlator_t *this)
{
    ec_selfheal_daemon_fini(this);
    __ec_destroy_private(this);
}

int32_t
ec_gf_access(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask,
             dict_t *xdata)
{
    ec_access(frame, this, -1, EC_MINIMUM_ONE, default_access_cbk, NULL, loc,
              mask, xdata);

    return 0;
}

int32_t
ec_gf_create(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
             mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
    ec_create(frame, this, -1, EC_MINIMUM_MIN, default_create_cbk, NULL, loc,
              flags, mode, umask, fd, xdata);

    return 0;
}

int32_t
ec_gf_discard(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
              size_t len, dict_t *xdata)
{
    ec_discard(frame, this, -1, EC_MINIMUM_MIN, default_discard_cbk, NULL, fd,
               offset, len, xdata);

    return 0;
}

int32_t
ec_gf_entrylk(call_frame_t *frame, xlator_t *this, const char *volume,
              loc_t *loc, const char *basename, entrylk_cmd cmd,
              entrylk_type type, dict_t *xdata)
{
    uint32_t fop_flags = EC_MINIMUM_ALL;

    if (cmd == ENTRYLK_UNLOCK)
        fop_flags = EC_MINIMUM_ONE;
    ec_entrylk(frame, this, -1, fop_flags, default_entrylk_cbk, NULL, volume,
               loc, basename, cmd, type, xdata);

    return 0;
}

int32_t
ec_gf_fentrylk(call_frame_t *frame, xlator_t *this, const char *volume,
               fd_t *fd, const char *basename, entrylk_cmd cmd,
               entrylk_type type, dict_t *xdata)
{
    uint32_t fop_flags = EC_MINIMUM_ALL;

    if (cmd == ENTRYLK_UNLOCK)
        fop_flags = EC_MINIMUM_ONE;
    ec_fentrylk(frame, this, -1, fop_flags, default_fentrylk_cbk, NULL, volume,
                fd, basename, cmd, type, xdata);

    return 0;
}

int32_t
ec_gf_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t mode,
                off_t offset, size_t len, dict_t *xdata)
{
    ec_fallocate(frame, this, -1, EC_MINIMUM_MIN, default_fallocate_cbk, NULL,
                 fd, mode, offset, len, xdata);

    return 0;
}

int32_t
ec_gf_flush(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
    ec_flush(frame, this, -1, EC_MINIMUM_MIN, default_flush_cbk, NULL, fd,
             xdata);

    return 0;
}

int32_t
ec_gf_fsync(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t datasync,
            dict_t *xdata)
{
    ec_fsync(frame, this, -1, EC_MINIMUM_MIN, default_fsync_cbk, NULL, fd,
             datasync, xdata);

    return 0;
}

int32_t
ec_gf_fsyncdir(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t datasync,
               dict_t *xdata)
{
    ec_fsyncdir(frame, this, -1, EC_MINIMUM_MIN, default_fsyncdir_cbk, NULL, fd,
                datasync, xdata);

    return 0;
}

int
ec_marker_populate_args(call_frame_t *frame, int type, int *gauge,
                        xlator_t **subvols)
{
    xlator_t *this = frame->this;
    ec_t *ec = this->private;

    memcpy(subvols, ec->xl_list, sizeof(*subvols) * ec->nodes);

    if (type == MARKER_XTIME_TYPE) {
        /*Don't error out on ENOENT/ENOTCONN */
        gauge[MCNT_NOTFOUND] = 0;
        gauge[MCNT_ENOTCONN] = 0;
    }

    return ec->nodes;
}

int32_t
ec_handle_heal_commands(call_frame_t *frame, xlator_t *this, loc_t *loc,
                        const char *name, dict_t *xdata)
{
    dict_t *dict_rsp = NULL;
    int op_ret = -1;
    int op_errno = ENOMEM;

    if (!name || strcmp(name, GF_HEAL_INFO))
        return -1;

    op_errno = -ec_get_heal_info(this, loc, &dict_rsp);
    if (op_errno <= 0) {
        op_errno = op_ret = 0;
    }

    STACK_UNWIND_STRICT(getxattr, frame, op_ret, op_errno, dict_rsp, NULL);
    if (dict_rsp)
        dict_unref(dict_rsp);
    return 0;
}

int32_t
ec_gf_getxattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
               const char *name, dict_t *xdata)
{
    int error = 0;
    ec_t *ec = this->private;
    int32_t fop_flags = EC_MINIMUM_ONE;

    if (name && strcmp(name, EC_XATTR_HEAL) != 0) {
        EC_INTERNAL_XATTR_OR_GOTO(name, NULL, error, out);
    }

    if (ec_handle_heal_commands(frame, this, loc, name, xdata) == 0)
        return 0;

    if (cluster_handle_marker_getxattr(frame, loc, name, ec->vol_uuid, NULL,
                                       ec_marker_populate_args) == 0)
        return 0;

    if (name && ((fnmatch(GF_XATTR_STIME_PATTERN, name, 0) == 0) ||
                 XATTR_IS_NODE_UUID(name) || XATTR_IS_NODE_UUID_LIST(name))) {
        fop_flags = EC_MINIMUM_ALL;
    }

    ec_getxattr(frame, this, -1, fop_flags, default_getxattr_cbk, NULL, loc,
                name, xdata);

    return 0;
out:
    error = ENODATA;
    STACK_UNWIND_STRICT(getxattr, frame, -1, error, NULL, NULL);
    return 0;
}

int32_t
ec_gf_fgetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, const char *name,
                dict_t *xdata)
{
    int error = 0;

    EC_INTERNAL_XATTR_OR_GOTO(name, NULL, error, out);

    ec_fgetxattr(frame, this, -1, EC_MINIMUM_ONE, default_fgetxattr_cbk, NULL,
                 fd, name, xdata);
    return 0;
out:
    error = ENODATA;
    STACK_UNWIND_STRICT(fgetxattr, frame, -1, error, NULL, NULL);
    return 0;
}

int32_t
ec_gf_inodelk(call_frame_t *frame, xlator_t *this, const char *volume,
              loc_t *loc, int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
    int32_t fop_flags = EC_MINIMUM_ALL;

    if (flock->l_type == F_UNLCK)
        fop_flags = EC_MINIMUM_ONE;

    ec_inodelk(frame, this, &frame->root->lk_owner, -1, fop_flags,
               default_inodelk_cbk, NULL, volume, loc, cmd, flock, xdata);

    return 0;
}

int32_t
ec_gf_finodelk(call_frame_t *frame, xlator_t *this, const char *volume,
               fd_t *fd, int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
    int32_t fop_flags = EC_MINIMUM_ALL;

    if (flock->l_type == F_UNLCK)
        fop_flags = EC_MINIMUM_ONE;
    ec_finodelk(frame, this, &frame->root->lk_owner, -1, fop_flags,
                default_finodelk_cbk, NULL, volume, fd, cmd, flock, xdata);

    return 0;
}

int32_t
ec_gf_link(call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
           dict_t *xdata)
{
    ec_link(frame, this, -1, EC_MINIMUM_MIN, default_link_cbk, NULL, oldloc,
            newloc, xdata);

    return 0;
}

int32_t
ec_gf_lk(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
         struct gf_flock *flock, dict_t *xdata)
{
    int32_t fop_flags = EC_MINIMUM_ALL;

    if (flock->l_type == F_UNLCK)
        fop_flags = EC_MINIMUM_ONE;
    ec_lk(frame, this, -1, fop_flags, default_lk_cbk, NULL, fd, cmd, flock,
          xdata);

    return 0;
}

int32_t
ec_gf_lookup(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    ec_lookup(frame, this, -1, EC_MINIMUM_MIN, default_lookup_cbk, NULL, loc,
              xdata);

    return 0;
}

int32_t
ec_gf_mkdir(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
            mode_t umask, dict_t *xdata)
{
    ec_mkdir(frame, this, -1, EC_MINIMUM_MIN, default_mkdir_cbk, NULL, loc,
             mode, umask, xdata);

    return 0;
}

int32_t
ec_gf_mknod(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
            dev_t rdev, mode_t umask, dict_t *xdata)
{
    ec_mknod(frame, this, -1, EC_MINIMUM_MIN, default_mknod_cbk, NULL, loc,
             mode, rdev, umask, xdata);

    return 0;
}

int32_t
ec_gf_open(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
           fd_t *fd, dict_t *xdata)
{
    ec_open(frame, this, -1, EC_MINIMUM_MIN, default_open_cbk, NULL, loc, flags,
            fd, xdata);

    return 0;
}

int32_t
ec_gf_opendir(call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
              dict_t *xdata)
{
    ec_opendir(frame, this, -1, EC_MINIMUM_MIN, default_opendir_cbk, NULL, loc,
               fd, xdata);

    return 0;
}

int32_t
ec_gf_readdir(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
              off_t offset, dict_t *xdata)
{
    ec_readdir(frame, this, -1, EC_MINIMUM_ONE, default_readdir_cbk, NULL, fd,
               size, offset, xdata);

    return 0;
}

int32_t
ec_gf_readdirp(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
               off_t offset, dict_t *xdata)
{
    ec_readdirp(frame, this, -1, EC_MINIMUM_ONE, default_readdirp_cbk, NULL, fd,
                size, offset, xdata);

    return 0;
}

int32_t
ec_gf_readlink(call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size,
               dict_t *xdata)
{
    ec_readlink(frame, this, -1, EC_MINIMUM_ONE, default_readlink_cbk, NULL,
                loc, size, xdata);

    return 0;
}

int32_t
ec_gf_readv(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
            off_t offset, uint32_t flags, dict_t *xdata)
{
    ec_readv(frame, this, -1, EC_MINIMUM_MIN, default_readv_cbk, NULL, fd, size,
             offset, flags, xdata);

    return 0;
}

int32_t
ec_gf_removexattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
                  const char *name, dict_t *xdata)
{
    int error = 0;

    EC_INTERNAL_XATTR_OR_GOTO(name, xdata, error, out);

    ec_removexattr(frame, this, -1, EC_MINIMUM_MIN, default_removexattr_cbk,
                   NULL, loc, name, xdata);

    return 0;
out:
    STACK_UNWIND_STRICT(removexattr, frame, -1, error, NULL);
    return 0;
}

int32_t
ec_gf_fremovexattr(call_frame_t *frame, xlator_t *this, fd_t *fd,
                   const char *name, dict_t *xdata)
{
    int error = 0;

    EC_INTERNAL_XATTR_OR_GOTO(name, xdata, error, out);

    ec_fremovexattr(frame, this, -1, EC_MINIMUM_MIN, default_fremovexattr_cbk,
                    NULL, fd, name, xdata);

    return 0;
out:
    STACK_UNWIND_STRICT(fremovexattr, frame, -1, error, NULL);
    return 0;
}

int32_t
ec_gf_rename(call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
             dict_t *xdata)
{
    ec_rename(frame, this, -1, EC_MINIMUM_MIN, default_rename_cbk, NULL, oldloc,
              newloc, xdata);

    return 0;
}

int32_t
ec_gf_rmdir(call_frame_t *frame, xlator_t *this, loc_t *loc, int xflags,
            dict_t *xdata)
{
    ec_rmdir(frame, this, -1, EC_MINIMUM_MIN, default_rmdir_cbk, NULL, loc,
             xflags, xdata);

    return 0;
}

int32_t
ec_gf_setattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
              struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
    ec_setattr(frame, this, -1, EC_MINIMUM_MIN, default_setattr_cbk, NULL, loc,
               stbuf, valid, xdata);

    return 0;
}

int32_t
ec_gf_fsetattr(call_frame_t *frame, xlator_t *this, fd_t *fd,
               struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
    ec_fsetattr(frame, this, -1, EC_MINIMUM_MIN, default_fsetattr_cbk, NULL, fd,
                stbuf, valid, xdata);

    return 0;
}

int32_t
ec_gf_setxattr(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
               int32_t flags, dict_t *xdata)
{
    int error = 0;

    EC_INTERNAL_XATTR_OR_GOTO("", dict, error, out);

    ec_setxattr(frame, this, -1, EC_MINIMUM_MIN, default_setxattr_cbk, NULL,
                loc, dict, flags, xdata);

    return 0;
out:
    STACK_UNWIND_STRICT(setxattr, frame, -1, error, NULL);
    return 0;
}

int32_t
ec_gf_fsetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
                int32_t flags, dict_t *xdata)
{
    int error = 0;

    EC_INTERNAL_XATTR_OR_GOTO("", dict, error, out);

    ec_fsetxattr(frame, this, -1, EC_MINIMUM_MIN, default_fsetxattr_cbk, NULL,
                 fd, dict, flags, xdata);

    return 0;
out:
    STACK_UNWIND_STRICT(fsetxattr, frame, -1, error, NULL);
    return 0;
}

int32_t
ec_gf_stat(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    ec_stat(frame, this, -1, EC_MINIMUM_MIN, default_stat_cbk, NULL, loc,
            xdata);

    return 0;
}

int32_t
ec_gf_fstat(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
    ec_fstat(frame, this, -1, EC_MINIMUM_MIN, default_fstat_cbk, NULL, fd,
             xdata);

    return 0;
}

int32_t
ec_gf_statfs(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    ec_statfs(frame, this, -1, EC_MINIMUM_MIN, default_statfs_cbk, NULL, loc,
              xdata);

    return 0;
}

int32_t
ec_gf_symlink(call_frame_t *frame, xlator_t *this, const char *linkname,
              loc_t *loc, mode_t umask, dict_t *xdata)
{
    ec_symlink(frame, this, -1, EC_MINIMUM_MIN, default_symlink_cbk, NULL,
               linkname, loc, umask, xdata);

    return 0;
}

int32_t
ec_gf_truncate(call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
               dict_t *xdata)
{
    ec_truncate(frame, this, -1, EC_MINIMUM_MIN, default_truncate_cbk, NULL,
                loc, offset, xdata);

    return 0;
}

int32_t
ec_gf_ftruncate(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                dict_t *xdata)
{
    ec_ftruncate(frame, this, -1, EC_MINIMUM_MIN, default_ftruncate_cbk, NULL,
                 fd, offset, xdata);

    return 0;
}

int32_t
ec_gf_unlink(call_frame_t *frame, xlator_t *this, loc_t *loc, int xflags,
             dict_t *xdata)
{
    ec_unlink(frame, this, -1, EC_MINIMUM_MIN, default_unlink_cbk, NULL, loc,
              xflags, xdata);

    return 0;
}

int32_t
ec_gf_writev(call_frame_t *frame, xlator_t *this, fd_t *fd,
             struct iovec *vector, int32_t count, off_t offset, uint32_t flags,
             struct iobref *iobref, dict_t *xdata)
{
    ec_writev(frame, this, -1, EC_MINIMUM_MIN, default_writev_cbk, NULL, fd,
              vector, count, offset, flags, iobref, xdata);

    return 0;
}

int32_t
ec_gf_xattrop(call_frame_t *frame, xlator_t *this, loc_t *loc,
              gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
    ec_xattrop(frame, this, -1, EC_MINIMUM_MIN, default_xattrop_cbk, NULL, loc,
               optype, xattr, xdata);

    return 0;
}

int32_t
ec_gf_fxattrop(call_frame_t *frame, xlator_t *this, fd_t *fd,
               gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
    ec_fxattrop(frame, this, -1, EC_MINIMUM_MIN, default_fxattrop_cbk, NULL, fd,
                optype, xattr, xdata);

    return 0;
}

int32_t
ec_gf_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
               off_t len, dict_t *xdata)
{
    default_zerofill_failure_cbk(frame, ENOTSUP);

    return 0;
}

int32_t
ec_gf_seek(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
           gf_seek_what_t what, dict_t *xdata)
{
    ec_seek(frame, this, -1, EC_MINIMUM_ONE, default_seek_cbk, NULL, fd, offset,
            what, xdata);

    return 0;
}

int32_t
ec_gf_ipc(call_frame_t *frame, xlator_t *this, int32_t op, dict_t *xdata)
{
    ec_ipc(frame, this, -1, EC_MINIMUM_MIN, default_ipc_cbk, NULL, op, xdata);
    return 0;
}

int32_t
ec_gf_forget(xlator_t *this, inode_t *inode)
{
    uint64_t value = 0;
    ec_inode_t *ctx = NULL;

    if ((inode_ctx_del(inode, this, &value) == 0) && (value != 0)) {
        ctx = (ec_inode_t *)(uintptr_t)value;
        /* We can only forget an inode if it has been unlocked, so the stripe
         * cache should also be empty. */
        GF_ASSERT(list_empty(&ctx->stripe_cache.lru));
        GF_FREE(ctx);
    }

    return 0;
}

void
ec_gf_release_fd(xlator_t *this, fd_t *fd)
{
    uint64_t value = 0;
    ec_fd_t *ctx = NULL;

    if ((fd_ctx_del(fd, this, &value) == 0) && (value != 0)) {
        ctx = (ec_fd_t *)(uintptr_t)value;
        loc_wipe(&ctx->loc);
        GF_FREE(ctx);
    }
}

int32_t
ec_gf_release(xlator_t *this, fd_t *fd)
{
    ec_gf_release_fd(this, fd);

    return 0;
}

int32_t
ec_gf_releasedir(xlator_t *this, fd_t *fd)
{
    ec_gf_release_fd(this, fd);

    return 0;
}

int32_t
ec_dump_private(xlator_t *this)
{
    ec_t *ec = NULL;
    char key_prefix[GF_DUMP_MAX_BUF_LEN];
    char tmp[65];

    GF_ASSERT(this);

    ec = this->private;
    GF_ASSERT(ec);

    snprintf(key_prefix, GF_DUMP_MAX_BUF_LEN, "%s.%s", this->type, this->name);
    gf_proc_dump_add_section("%s", key_prefix);
    gf_proc_dump_write("up", "%u", ec->up);
    gf_proc_dump_write("nodes", "%u", ec->nodes);
    gf_proc_dump_write("redundancy", "%u", ec->redundancy);
    gf_proc_dump_write("fragment_size", "%u", ec->fragment_size);
    gf_proc_dump_write("stripe_size", "%u", ec->stripe_size);
    gf_proc_dump_write("childs_up", "%u", ec->xl_up_count);
    gf_proc_dump_write("childs_up_mask", "%s",
                       ec_bin(tmp, sizeof(tmp), ec->xl_up, ec->nodes));
    if (ec->read_mask) {
        gf_proc_dump_write("read-mask", "%s",
                           ec_bin(tmp, sizeof(tmp), ec->read_mask, ec->nodes));
    }
    gf_proc_dump_write("background-heals", "%d", ec->background_heals);
    gf_proc_dump_write("heal-wait-qlength", "%d", ec->heal_wait_qlen);
    gf_proc_dump_write("self-heal-window-size", "%" PRIu32,
                       ec->self_heal_window_size);
    gf_proc_dump_write("healers", "%d", ec->healers);
    gf_proc_dump_write("heal-waiters", "%d", ec->heal_waiters);
    gf_proc_dump_write("read-policy", "%s", ec_read_policies[ec->read_policy]);
    gf_proc_dump_write("parallel-writes", "%d", ec->parallel_writes);
    gf_proc_dump_write("quorum-count", "%u", ec->quorum_count);

    snprintf(key_prefix, GF_DUMP_MAX_BUF_LEN, "%s.%s.stats.stripe_cache",
             this->type, this->name);
    gf_proc_dump_add_section("%s", key_prefix);

    gf_proc_dump_write("hits", "%" GF_PRI_ATOMIC,
                       GF_ATOMIC_GET(ec->stats.stripe_cache.hits));
    gf_proc_dump_write("misses", "%" GF_PRI_ATOMIC,
                       GF_ATOMIC_GET(ec->stats.stripe_cache.misses));
    gf_proc_dump_write("updates", "%" GF_PRI_ATOMIC,
                       GF_ATOMIC_GET(ec->stats.stripe_cache.updates));
    gf_proc_dump_write("invalidations", "%" GF_PRI_ATOMIC,
                       GF_ATOMIC_GET(ec->stats.stripe_cache.invals));
    gf_proc_dump_write("evicts", "%" GF_PRI_ATOMIC,
                       GF_ATOMIC_GET(ec->stats.stripe_cache.evicts));
    gf_proc_dump_write("allocations", "%" GF_PRI_ATOMIC,
                       GF_ATOMIC_GET(ec->stats.stripe_cache.allocs));
    gf_proc_dump_write("errors", "%" GF_PRI_ATOMIC,
                       GF_ATOMIC_GET(ec->stats.stripe_cache.errors));
    gf_proc_dump_write("heals-attempted", "%" GF_PRI_ATOMIC,
                       GF_ATOMIC_GET(ec->stats.shd.attempted));
    gf_proc_dump_write("heals-completed", "%" GF_PRI_ATOMIC,
                       GF_ATOMIC_GET(ec->stats.shd.completed));

    return 0;
}

struct xlator_fops fops = {.lookup = ec_gf_lookup,
                           .stat = ec_gf_stat,
                           .fstat = ec_gf_fstat,
                           .truncate = ec_gf_truncate,
                           .ftruncate = ec_gf_ftruncate,
                           .access = ec_gf_access,
                           .readlink = ec_gf_readlink,
                           .mknod = ec_gf_mknod,
                           .mkdir = ec_gf_mkdir,
                           .unlink = ec_gf_unlink,
                           .rmdir = ec_gf_rmdir,
                           .symlink = ec_gf_symlink,
                           .rename = ec_gf_rename,
                           .link = ec_gf_link,
                           .create = ec_gf_create,
                           .open = ec_gf_open,
                           .readv = ec_gf_readv,
                           .writev = ec_gf_writev,
                           .flush = ec_gf_flush,
                           .fsync = ec_gf_fsync,
                           .opendir = ec_gf_opendir,
                           .readdir = ec_gf_readdir,
                           .readdirp = ec_gf_readdirp,
                           .fsyncdir = ec_gf_fsyncdir,
                           .statfs = ec_gf_statfs,
                           .setxattr = ec_gf_setxattr,
                           .getxattr = ec_gf_getxattr,
                           .fsetxattr = ec_gf_fsetxattr,
                           .fgetxattr = ec_gf_fgetxattr,
                           .removexattr = ec_gf_removexattr,
                           .fremovexattr = ec_gf_fremovexattr,
                           .lk = ec_gf_lk,
                           .inodelk = ec_gf_inodelk,
                           .finodelk = ec_gf_finodelk,
                           .entrylk = ec_gf_entrylk,
                           .fentrylk = ec_gf_fentrylk,
                           .xattrop = ec_gf_xattrop,
                           .fxattrop = ec_gf_fxattrop,
                           .setattr = ec_gf_setattr,
                           .fsetattr = ec_gf_fsetattr,
                           .fallocate = ec_gf_fallocate,
                           .discard = ec_gf_discard,
                           .zerofill = ec_gf_zerofill,
                           .seek = ec_gf_seek,
                           .ipc = ec_gf_ipc};

struct xlator_cbks cbks = {.forget = ec_gf_forget,
                           .release = ec_gf_release,
                           .releasedir = ec_gf_releasedir};

struct xlator_dumpops dumpops = {.priv = ec_dump_private};

struct volume_options options[] = {
    {.key = {"redundancy"},
     .type = GF_OPTION_TYPE_INT,
     .default_value = "{{ volume.redundancy }}",
     .description = "Maximum number of bricks that can fail "
                    "simultaneously without losing data."},
    {
        .key = {"self-heal-daemon"},
        .type = GF_OPTION_TYPE_BOOL,
        .description = "self-heal daemon enable/disable",
        .default_value = "enable",
        .op_version = {GD_OP_VERSION_3_7_0},
        .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
        .tags = {"disperse"},
    },
    {.key = {"iam-self-heal-daemon"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "off",
     .description = "This option differentiates if the disperse "
                    "translator is running as part of self-heal-daemon "
                    "or not."},
    {.key = {"eager-lock"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "on",
     .op_version = {GD_OP_VERSION_3_7_10},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
     .tags = {"disperse"},
     .description = "Enable/Disable eager lock for regular files on a "
                    "disperse volume. If a fop takes a lock and completes "
                    "its operation, it waits for next 1 second before "
                    "releasing the lock, to see if the lock can be reused "
                    "for next fop from the same client. If ec finds any lock "
                    "contention within 1 second it releases the lock "
                    "immediately before time expires. This improves the "
                    "performance of file operations. However, as it takes "
                    "lock on first brick, for few operations like read, "
                    "discovery of lock contention might take long time and "
                    "can actually degrade the performance. If eager lock is "
                    "disabled, lock will be released as soon as fop "
                    "completes."},
    {.key = {"other-eager-lock"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "on",
     .op_version = {GD_OP_VERSION_3_13_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
     .tags = {"disperse"},
     .description = "It's equivalent to the eager-lock option but for non "
                    "regular files."},
    {.key = {"eager-lock-timeout"},
     .type = GF_OPTION_TYPE_INT,
     .min = 1,
     .max = 60,
     .default_value = "1",
     .op_version = {GD_OP_VERSION_4_0_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
     .tags = {"disperse", "locks", "timeout"},
     .description = "Maximum time (in seconds) that a lock on an inode is "
                    "kept held if no new operations on the inode are "
                    "received."},
    {.key = {"other-eager-lock-timeout"},
     .type = GF_OPTION_TYPE_INT,
     .min = 1,
     .max = 60,
     .default_value = "1",
     .op_version = {GD_OP_VERSION_4_0_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
     .tags = {"disperse", "locks", "timeout"},
     .description = "It's equivalent to eager-lock-timeout option but for "
                    "non regular files."},
    {
        .key = {"background-heals"},
        .type = GF_OPTION_TYPE_INT,
        .min = 0, /*Disabling background heals*/
        .max = 256,
        .default_value = "8",
        .op_version = {GD_OP_VERSION_3_7_3},
        .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
        .tags = {"disperse"},
        .description = "This option can be used to control number of parallel"
                       " heals",
    },
    {
        .key = {"heal-wait-qlength"},
        .type = GF_OPTION_TYPE_INT,
        .min = 0,
        .max =
            65536, /*Around 100MB as of now with sizeof(ec_fop_data_t) at 1800*/
        .default_value = "128",
        .op_version = {GD_OP_VERSION_3_7_3},
        .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
        .tags = {"disperse"},
        .description = "This option can be used to control number of heals"
                       " that can wait",
    },
    {.key = {"heal-timeout"},
     .type = GF_OPTION_TYPE_INT,
     .min = 60,
     .max = INT_MAX,
     .default_value = "600",
     .op_version = {GD_OP_VERSION_3_7_3},
     .flags = OPT_FLAG_SETTABLE,
     .tags = {"disperse"},
     .description = "time interval for checking the need to self-heal "
                    "in self-heal-daemon"},
    {
        .key = {"read-policy"},
        .type = GF_OPTION_TYPE_STR,
        .value = {"round-robin", "gfid-hash"},
        .default_value = "gfid-hash",
        .op_version = {GD_OP_VERSION_3_7_6},
        .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
        .tags = {"disperse"},
        .description =
            "inode-read fops happen only on 'k' number of bricks in"
            " n=k+m disperse subvolume. 'round-robin' selects the read"
            " subvolume using round-robin algo. 'gfid-hash' selects read"
            " subvolume based on hash of the gfid of that file/directory.",
    },
    {.key = {"shd-max-threads"},
     .type = GF_OPTION_TYPE_INT,
     .min = 1,
     .max = 64,
     .default_value = "1",
     .op_version = {GD_OP_VERSION_3_9_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"disperse"},
     .description = "Maximum number of parallel heals SHD can do per local "
                    "brick.  This can substantially lower heal times, "
                    "but can also crush your bricks if you don't have "
                    "the storage hardware to support this."},
    {.key = {"shd-wait-qlength"},
     .type = GF_OPTION_TYPE_INT,
     .min = 1,
     .max = 65536,
     .default_value = "1024",
     .op_version = {GD_OP_VERSION_3_9_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"disperse"},
     .description = "This option can be used to control number of heals"
                    " that can wait in SHD per subvolume"},
    {.key = {"cpu-extensions"},
     .type = GF_OPTION_TYPE_STR,
     .value = {"none", "auto", "x64", "sse", "avx"},
     .default_value = "auto",
     .op_version = {GD_OP_VERSION_3_9_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
     .tags = {"disperse"},
     .description = "force the cpu extensions to be used to accelerate the "
                    "galois field computations."},
    {.key = {"self-heal-window-size"},
     .type = GF_OPTION_TYPE_INT,
     .min = 1,
     .max = 1024,
     .default_value = "1",
     .op_version = {GD_OP_VERSION_3_11_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
     .tags = {"disperse"},
     .description = "Maximum number blocks(128KB) per file for which "
                    "self-heal process would be applied simultaneously."},
    {.key = {"optimistic-change-log"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "on",
     .op_version = {GD_OP_VERSION_3_10_1},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT,
     .tags = {"disperse"},
     .description = "Set/Unset dirty flag for every update fop at the start"
                    "of the fop. If OFF, this option impacts performance of"
                    "entry  operations or metadata operations as it will"
                    "set dirty flag at the start and unset it at the end of"
                    "ALL update fop. If ON and all the bricks are good,"
                    "dirty flag will be set at the start only for file fops"
                    "For metadata and entry fops dirty flag will not be set"
                    "at the start, if all the bricks are good. This does"
                    "not impact performance for metadata operations and"
                    "entry operation but has a very small window to miss"
                    "marking entry as dirty in case it is required to be"
                    "healed"},
    {.key = {"parallel-writes"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "on",
     .description = "This controls if writes can be wound in parallel as long"
                    "as it doesn't modify same stripes"},
    {.key = {"stripe-cache"},
     .type = GF_OPTION_TYPE_INT,
     .min = 0, /*Disabling stripe_cache*/
     .max = EC_STRIPE_CACHE_MAX_SIZE,
     .default_value = "4",
     .description = "This option will keep the last stripe of write fop"
                    "in memory. If next write falls in this stripe, we need"
                    "not to read it again from backend and we can save READ"
                    "fop going over the network. This will improve performance,"
                    "specially for sequential writes. However, this will also"
                    "lead to extra memory consumption, maximum "
                    "(cache size * stripe size) Bytes per open file."},
    {
        .key = {"quorum-count"},
        .type = GF_OPTION_TYPE_INT,
        .default_value = "0",
        .description =
            "This option can be used to define how many successes on"
            "the bricks constitute a success to the application. This"
            " count should be in the range"
            "[disperse-data-count,  disperse-count] (inclusive)",
    },
    {
        .key = {"ec-read-mask"},
        .type = GF_OPTION_TYPE_STR,
        .default_value = NULL,
        .description = "This option can be used to choose which bricks can be"
                       " used for reading data/metadata of a file/directory",
    },
    {
        .key = {NULL},
    },
};

xlator_api_t xlator_api = {
    .init = init,
    .fini = fini,
    .notify = notify,
    .reconfigure = reconfigure,
    .mem_acct_init = mem_acct_init,
    .op_version = {1},
    .dumpops = &dumpops,
    .fops = &fops,
    .cbks = &cbks,
    .options = options,
    .identifier = "disperse",
    .category = GF_MAINTAINED,
};
