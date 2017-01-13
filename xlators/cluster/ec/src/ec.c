/*
  Copyright (c) 2012-2015 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "defaults.h"
#include "statedump.h"
#include "compat-errno.h"
#include "upcall-utils.h"

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
#include "events.h"

static char *ec_read_policies[EC_READ_POLICY_MAX + 1] = {
        [EC_ROUND_ROBIN] = "round-robin",
        [EC_GFID_HASH] = "gfid-hash",
        [EC_READ_POLICY_MAX] = NULL
};

#define EC_MAX_FRAGMENTS EC_METHOD_MAX_FRAGMENTS
/* The maximum number of nodes is derived from the maximum allowed fragments
 * using the rule that redundancy cannot be equal or greater than the number
 * of fragments.
 */
#define EC_MAX_NODES min(EC_MAX_FRAGMENTS * 2 - 1, EC_METHOD_MAX_NODES)

#define EC_INTERNAL_XATTR_OR_GOTO(name, xattr, op_errno, label)                \
        do {                                                                   \
                if (ec_is_internal_xattr (NULL, (char *)name, NULL, NULL)) {   \
                        op_errno = EPERM;                                      \
                        goto label;                                            \
                }                                                              \
                if (name && (strlen (name) == 0) && xattr) {                   \
                        /* Bulk [f]removexattr/[f]setxattr */                  \
                        GF_IF_INTERNAL_XATTR_GOTO (EC_XATTR_PREFIX"*", xattr,  \
                                                   op_errno, label);           \
                }                                                              \
        } while (0)

int32_t ec_parse_options(xlator_t * this)
{
    ec_t * ec = this->private;
    int32_t error = EINVAL;
    uintptr_t mask;

    GF_OPTION_INIT("redundancy", ec->redundancy, int32, out);
    ec->fragments = ec->nodes - ec->redundancy;
    if ((ec->redundancy < 1) || (ec->redundancy >= ec->fragments) ||
        (ec->fragments > EC_MAX_FRAGMENTS))
    {
        gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                EC_MSG_INVALID_REDUNDANCY,
                "Invalid redundancy (must be between "
                "1 and %d)", (ec->nodes - 1) / 2);

        goto out;
    }

    ec->bits_for_nodes = 1;
    mask = 2;
    while (ec->nodes > mask)
    {
        ec->bits_for_nodes++;
        mask <<= 1;
    }
    ec->node_mask = (1ULL << ec->nodes) - 1ULL;
    ec->fragment_size = EC_METHOD_CHUNK_SIZE;
    ec->stripe_size = ec->fragment_size * ec->fragments;

    gf_msg_debug ("ec", 0, "Initialized with: nodes=%u, fragments=%u, "
                               "stripe_size=%u, node_mask=%lX",
           ec->nodes, ec->fragments, ec->stripe_size, ec->node_mask);

    error = 0;

out:
    return error;
}

int32_t ec_prepare_childs(xlator_t * this)
{
    ec_t * ec = this->private;
    xlator_list_t * child = NULL;
    int32_t count = 0;

    for (child = this->children; child != NULL; child = child->next)
    {
        count++;
    }
    if (count > EC_MAX_NODES)
    {
        gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                EC_MSG_TOO_MANY_SUBVOLS, "Too many subvolumes");

        return EINVAL;
    }
    ec->nodes = count;

    ec->xl_list = GF_CALLOC(count, sizeof(ec->xl_list[0]), ec_mt_xlator_t);
    if (ec->xl_list == NULL)
    {
        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                EC_MSG_NO_MEMORY, "Allocation of xlator list failed");

        return ENOMEM;
    }
    ec->xl_up = 0;
    ec->xl_up_count = 0;

    count = 0;
    for (child = this->children; child != NULL; child = child->next)
    {
        ec->xl_list[count++] = child->xlator;
    }

    return 0;
}

/* This function transforms the subvol to subvol-id*/
static int
_subvol_to_subvolid (dict_t *this, char *key, data_t *value, void *data)
{
        ec_t *ec = data;
        xlator_t *subvol = NULL;
        int      i = 0;
        int     ret = -1;

        subvol = data_to_ptr (value);
        for (i = 0; i < ec->nodes; i++) {
                if (ec->xl_list[i] == subvol) {
                        ret = dict_set_int32 (this, key, i);
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
ec_subvol_to_subvol_id_transform (ec_t *ec, dict_t *leaf_to_subvolid)
{
        return dict_foreach (leaf_to_subvolid, _subvol_to_subvolid, ec);
}

void __ec_destroy_private(xlator_t * this)
{
    ec_t * ec = this->private;

    if (ec != NULL)
    {
        LOCK(&ec->lock);

        if (ec->timer != NULL)
        {
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
        if (ec->xl_list != NULL)
        {
            GF_FREE(ec->xl_list);
            ec->xl_list = NULL;
        }

        if (ec->fop_pool != NULL)
        {
            mem_pool_destroy(ec->fop_pool);
        }

        if (ec->cbk_pool != NULL)
        {
            mem_pool_destroy(ec->cbk_pool);
        }

        if (ec->lock_pool != NULL)
        {
            mem_pool_destroy(ec->lock_pool);
        }

        LOCK_DESTROY(&ec->lock);

        if (ec->leaf_to_subvolid)
                dict_unref (ec->leaf_to_subvolid);

        ec_method_fini(&ec->matrix);

        GF_FREE(ec);
    }
}

int32_t mem_acct_init(xlator_t * this)
{
    if (xlator_mem_acct_init(this, ec_mt_end + 1) != 0)
    {
        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                EC_MSG_NO_MEMORY, "Memory accounting initialization "
                                         "failed.");

        return -1;
    }

    return 0;
}

void
ec_configure_background_heal_opts (ec_t *ec, int background_heals,
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
ec_assign_read_policy (ec_t *ec, char *read_policy)
{
        int read_policy_idx = -1;

        read_policy_idx = gf_get_index_by_elem (ec_read_policies, read_policy);
        if (read_policy_idx < 0 || read_policy_idx >= EC_READ_POLICY_MAX)
                return -1;

        ec->read_policy = read_policy_idx;
        return 0;
}

int32_t
reconfigure (xlator_t *this, dict_t *options)
{
        ec_t     *ec              = this->private;
        char     *read_policy     = NULL;
        char     *extensions      = NULL;
        uint32_t heal_wait_qlen   = 0;
        uint32_t background_heals = 0;
        int32_t  ret              = -1;
        int32_t  err;

        GF_OPTION_RECONF ("cpu-extensions", extensions, options, str, failed);

        GF_OPTION_RECONF ("self-heal-daemon", ec->shd.enabled, options, bool,
                          failed);
        GF_OPTION_RECONF ("iam-self-heal-daemon", ec->shd.iamshd, options,
                          bool, failed);
        GF_OPTION_RECONF ("eager-lock", ec->eager_lock, options,
                          bool, failed);
        GF_OPTION_RECONF ("background-heals", background_heals, options,
                          uint32, failed);
        GF_OPTION_RECONF ("heal-wait-qlength", heal_wait_qlen, options,
                          uint32, failed);
        GF_OPTION_RECONF ("heal-timeout", ec->shd.timeout, options,
                          int32, failed);
        ec_configure_background_heal_opts (ec, background_heals,
                                           heal_wait_qlen);
        GF_OPTION_RECONF ("shd-max-threads", ec->shd.max_threads,
                          options, uint32, failed);
        GF_OPTION_RECONF ("shd-wait-qlength", ec->shd.wait_qlength,
                          options, uint32, failed);

        GF_OPTION_RECONF ("read-policy", read_policy, options, str, failed);

        ret = 0;
        if (ec_assign_read_policy (ec, read_policy)) {
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
ec_get_event_from_state (ec_t *ec)
{
        int               down_count = 0;

        if (ec->xl_up_count >= ec->fragments) {
                /* If ec is up but some subvolumes are yet to notify, give
                 * grace time for other subvols to notify to prevent start of
                 * I/O which may result in self-heals */
                if (ec->timer && ec->xl_notify_count < ec->nodes)
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
ec_up (xlator_t *this, ec_t *ec)
{
        if (ec->timer != NULL) {
                gf_timer_call_cancel (this->ctx, ec->timer);
                ec->timer = NULL;
        }

        ec->up = 1;
        gf_msg (this->name, GF_LOG_INFO, 0,
                EC_MSG_EC_UP, "Going UP");
        gf_event (EVENT_EC_MIN_BRICKS_UP, "subvol=%s", this->name);
}

void
ec_down (xlator_t *this, ec_t *ec)
{
        if (ec->timer != NULL) {
                gf_timer_call_cancel(this->ctx, ec->timer);
                ec->timer = NULL;
        }

        ec->up = 0;
        gf_msg (this->name, GF_LOG_INFO, 0,
                EC_MSG_EC_DOWN, "Going DOWN");
        gf_event (EVENT_EC_MIN_BRICKS_NOT_UP, "subvol=%s", this->name);
}

void
ec_notify_cbk (void *data)
{
        ec_t *ec = data;
        glusterfs_event_t event = GF_EVENT_MAXVAL;
        gf_boolean_t propagate = _gf_false;

        LOCK(&ec->lock);
        {
                if (!ec->timer) {
                        /*
                         * Either child_up/child_down is already sent to parent
                         * This is a spurious wake up.
                         */
                        goto unlock;
                }

                gf_timer_call_cancel (ec->xl->ctx, ec->timer);
                ec->timer = NULL;

                event = ec_get_event_from_state (ec);
                /* If event is still MAXVAL then enough subvolumes didn't
                 * notify, treat it as CHILD_DOWN. */
                if (event == GF_EVENT_MAXVAL) {
                        event = GF_EVENT_CHILD_DOWN;
                        ec->xl_notify = (1ULL << ec->nodes) - 1ULL;
                        ec->xl_notify_count = ec->nodes;
                } else if (event == GF_EVENT_CHILD_UP) {
                        /* Rest of the bricks are still not coming up,
                         * notify that ec is up. Files/directories will be
                         * healed as in when they come up. */
                        ec_up (ec->xl, ec);
                }

                /* CHILD_DOWN should not come here as no grace period is given
                 * for notifying CHILD_DOWN. */

                propagate = _gf_true;
        }
unlock:
        UNLOCK(&ec->lock);

        if (propagate) {
                default_notify (ec->xl, event, NULL);
        }
}

void
ec_launch_notify_timer (xlator_t *this, ec_t *ec)
{
        struct timespec delay = {0, };

        gf_msg_debug (this->name, 0, "Initiating child-down timer");
        delay.tv_sec = 10;
        delay.tv_nsec = 0;
        ec->timer = gf_timer_call_after (this->ctx, delay, ec_notify_cbk, ec);
        if (ec->timer == NULL) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        EC_MSG_TIMER_CREATE_FAIL, "Cannot create timer "
                        "for delayed initialization");
        }
}

void
ec_handle_up (xlator_t *this, ec_t *ec, int32_t idx)
{
        if (((ec->xl_up >> idx) & 1) == 0) { /* Duplicate event */
                if (((ec->xl_notify >> idx) & 1) == 0) {
                        ec->xl_notify |= 1ULL << idx;
                        ec->xl_notify_count++;
                }
                ec->xl_up |= 1ULL << idx;
                ec->xl_up_count++;
        }
}

void
ec_handle_down (xlator_t *this, ec_t *ec, int32_t idx)
{
        if (((ec->xl_up >> idx) & 1) != 0) { /* Duplicate event */
                gf_msg_debug (this->name, 0, "Child %d is DOWN", idx);

                if (((ec->xl_notify >> idx) & 1) == 0) {
                        ec->xl_notify |= 1ULL << idx;
                        ec->xl_notify_count++;
                }

                ec->xl_up ^= 1ULL << idx;
                ec->xl_up_count--;
        }
}

gf_boolean_t
ec_disable_delays(ec_t *ec)
{
        ec->shutdown = _gf_true;

        return list_empty (&ec->pending_fops);
}

void
ec_pending_fops_completed(ec_t *ec)
{
        if (ec->shutdown) {
                default_notify (ec->xl, GF_EVENT_PARENT_DOWN, NULL);
        }
}

int32_t
ec_notify (xlator_t *this, int32_t event, void *data, void *data2)
{
        ec_t             *ec        = this->private;
        int32_t           idx       = 0;
        int32_t           error     = 0;
        glusterfs_event_t old_event = GF_EVENT_MAXVAL;
        dict_t            *input    = NULL;
        dict_t            *output   = NULL;
        gf_boolean_t      propagate = _gf_true;
        int32_t           orig_event = event;
        struct gf_upcall *up_data   = NULL;
        struct gf_upcall_cache_invalidation *up_ci = NULL;

        gf_msg_trace (this->name, 0, "NOTIFY(%d): %p, %p",
                event, data, data2);

        if (event == GF_EVENT_UPCALL) {
                up_data = (struct gf_upcall *)data;
                if (up_data->event_type == GF_UPCALL_CACHE_INVALIDATION) {
                        up_ci = (struct gf_upcall_cache_invalidation *)up_data->data;
                        up_ci->flags |= UP_INVAL_ATTR;
                }
                goto done;
        }

        if (event == GF_EVENT_TRANSLATOR_OP) {
                if (!ec->up) {
                        error = -1;
                } else {
                        input = data;
                        output = data2;
                        error = ec_xl_op (this, input, output);
                }
                goto out;
        }

        for (idx = 0; idx < ec->nodes; idx++) {
                if (ec->xl_list[idx] == data) {
                        if (event == GF_EVENT_CHILD_UP)
                                ec_selfheal_childup (ec, idx);
                        break;
                }
        }

        LOCK (&ec->lock);

        if (event == GF_EVENT_PARENT_UP) {
                /*
                 * Start a timer which sends appropriate event to parent
                 * xlator to prevent the 'mount' syscall from hanging.
                 */
                ec_launch_notify_timer (this, ec);
                goto unlock;
        } else if (event == GF_EVENT_PARENT_DOWN) {
                /* If there aren't pending fops running after we have waken up
                 * them, we immediately propagate the notification. */
                propagate = ec_disable_delays(ec);
                goto unlock;
        }

        if (idx < ec->nodes) { /* CHILD_* events */
                old_event = ec_get_event_from_state (ec);

                if (event == GF_EVENT_CHILD_UP) {
                        ec_handle_up (this, ec, idx);
                } else if (event == GF_EVENT_CHILD_DOWN) {
                        ec_handle_down (this, ec, idx);
                }

                event = ec_get_event_from_state (ec);

                if (event == GF_EVENT_CHILD_UP && !ec->up) {
                        ec_up (this, ec);
                } else if (event == GF_EVENT_CHILD_DOWN && ec->up) {
                        ec_down (this, ec);
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
                }
        }
unlock:
        UNLOCK (&ec->lock);

done:
        if (propagate) {
                error = default_notify (this, event, data);
        }

        if (ec->shd.iamshd &&
            ec->xl_notify_count == ec->nodes &&
            event == GF_EVENT_CHILD_UP) {
                ec_launch_replace_heal (ec);
        }
out:
        return error;
}

int32_t
notify (xlator_t *this, int32_t event, void *data, ...)
{
        int ret = -1;
        va_list         ap;
        void *data2 = NULL;

        va_start (ap, data);
        data2 = va_arg (ap, dict_t*);
        va_end (ap);
        ret = ec_notify (this, event, data, data2);

        return ret;
}

int32_t
init (xlator_t *this)
{
    ec_t *ec          = NULL;
    char *read_policy = NULL;
    char *extensions  = NULL;
    int32_t err;

    if (this->parents == NULL)
    {
        gf_msg (this->name, GF_LOG_WARNING, 0,
                EC_MSG_NO_PARENTS, "Volume does not have parents.");
    }

    ec = GF_MALLOC(sizeof(*ec), ec_mt_ec_t);
    if (ec == NULL)
    {
        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                EC_MSG_NO_MEMORY, "Failed to allocate private memory.");

        return -1;
    }
    memset(ec, 0, sizeof(*ec));

    this->private = ec;

    ec->xl = this;
    LOCK_INIT(&ec->lock);

    INIT_LIST_HEAD(&ec->pending_fops);
    INIT_LIST_HEAD(&ec->heal_waiting);
    INIT_LIST_HEAD(&ec->healing);

    ec->fop_pool = mem_pool_new(ec_fop_data_t, 1024);
    ec->cbk_pool = mem_pool_new(ec_cbk_data_t, 4096);
    ec->lock_pool = mem_pool_new(ec_lock_t, 1024);
    if ((ec->fop_pool == NULL) || (ec->cbk_pool == NULL) ||
        (ec->lock_pool == NULL))
    {
        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                EC_MSG_NO_MEMORY, "Failed to create memory pools.");

        goto failed;
    }

    if (ec_prepare_childs(this) != 0)
    {
        gf_msg (this->name, GF_LOG_ERROR, 0,
                EC_MSG_XLATOR_INIT_FAIL, "Failed to initialize xlator");

        goto failed;
    }

    if (ec_parse_options(this) != 0)
    {
        gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                EC_MSG_XLATOR_PARSE_OPT_FAIL, "Failed to parse xlator options");

        goto failed;
    }

    GF_OPTION_INIT("cpu-extensions", extensions, str, failed);

    err = ec_method_init(this, &ec->matrix, ec->fragments, ec->nodes,
                         ec->nodes * 2, extensions);
    if (err != 0) {
        gf_msg (this->name, GF_LOG_ERROR, -err, EC_MSG_MATRIX_FAILED,
                "Failed to initialize matrix management");

        goto failed;
    }

    GF_OPTION_INIT ("self-heal-daemon", ec->shd.enabled, bool, failed);
    GF_OPTION_INIT ("iam-self-heal-daemon", ec->shd.iamshd, bool, failed);
    GF_OPTION_INIT ("eager-lock", ec->eager_lock, bool, failed);
    GF_OPTION_INIT ("background-heals", ec->background_heals, uint32, failed);
    GF_OPTION_INIT ("heal-wait-qlength", ec->heal_wait_qlen, uint32, failed);
    ec_configure_background_heal_opts (ec, ec->background_heals,
                                       ec->heal_wait_qlen);
    GF_OPTION_INIT ("read-policy", read_policy, str, failed);
    if (ec_assign_read_policy (ec, read_policy))
            goto failed;

    GF_OPTION_INIT ("shd-max-threads", ec->shd.max_threads, uint32, failed);
    GF_OPTION_INIT ("shd-wait-qlength", ec->shd.wait_qlength, uint32, failed);

    this->itable = inode_table_new (EC_SHD_INODE_LRU_LIMIT, this);
    if (!this->itable)
            goto failed;

    if (ec->shd.iamshd)
            ec_selfheal_daemon_init (this);
    gf_msg_debug (this->name, 0, "Disperse translator initialized.");

    ec->leaf_to_subvolid = dict_new ();
    if (!ec->leaf_to_subvolid)
            goto failed;
    if (glusterfs_reachable_leaves (this, ec->leaf_to_subvolid)) {
        gf_msg (this->name, GF_LOG_ERROR, 0,
                EC_MSG_SUBVOL_BUILD_FAIL, "Failed to build subvol "
                "dictionary");
        goto failed;
    }

    if (ec_subvol_to_subvol_id_transform (ec, ec->leaf_to_subvolid) < 0) {
        gf_msg (this->name, GF_LOG_ERROR, 0,
                EC_MSG_SUBVOL_ID_DICT_SET_FAIL, "Failed to build subvol-id "
                "dictionary");
        goto failed;
    }

    return 0;

failed:
    __ec_destroy_private(this);

    return -1;
}

void fini(xlator_t * this)
{
    __ec_destroy_private(this);
}

int32_t ec_gf_access(call_frame_t * frame, xlator_t * this, loc_t * loc,
                     int32_t mask, dict_t * xdata)
{
    ec_access(frame, this, -1, EC_MINIMUM_ONE, default_access_cbk, NULL, loc,
              mask, xdata);

    return 0;
}

int32_t ec_gf_create(call_frame_t * frame, xlator_t * this, loc_t * loc,
                     int32_t flags, mode_t mode, mode_t umask, fd_t * fd,
                     dict_t * xdata)
{
    ec_create(frame, this, -1, EC_MINIMUM_MIN, default_create_cbk, NULL, loc,
              flags, mode, umask, fd, xdata);

    return 0;
}

int32_t ec_gf_discard(call_frame_t * frame, xlator_t * this, fd_t * fd,
                      off_t offset, size_t len, dict_t * xdata)
{
    default_discard_failure_cbk(frame, ENOTSUP);

    return 0;
}

int32_t ec_gf_entrylk(call_frame_t * frame, xlator_t * this,
                      const char * volume, loc_t * loc, const char * basename,
                      entrylk_cmd cmd, entrylk_type type, dict_t * xdata)
{
    int32_t minimum = EC_MINIMUM_ALL;
    if (cmd == ENTRYLK_UNLOCK)
            minimum = EC_MINIMUM_ONE;
    ec_entrylk(frame, this, -1, minimum, default_entrylk_cbk, NULL,
               volume, loc, basename, cmd, type, xdata);

    return 0;
}

int32_t ec_gf_fentrylk(call_frame_t * frame, xlator_t * this,
                       const char * volume, fd_t * fd, const char * basename,
                       entrylk_cmd cmd, entrylk_type type, dict_t * xdata)
{
    int32_t minimum = EC_MINIMUM_ALL;
    if (cmd == ENTRYLK_UNLOCK)
            minimum = EC_MINIMUM_ONE;
    ec_fentrylk(frame, this, -1, minimum, default_fentrylk_cbk, NULL,
                volume, fd, basename, cmd, type, xdata);

    return 0;
}

int32_t ec_gf_fallocate(call_frame_t * frame, xlator_t * this, fd_t * fd,
                        int32_t keep_size, off_t offset, size_t len,
                        dict_t * xdata)
{
    default_fallocate_failure_cbk(frame, ENOTSUP);

    return 0;
}

int32_t ec_gf_flush(call_frame_t * frame, xlator_t * this, fd_t * fd,
                    dict_t * xdata)
{
    ec_flush(frame, this, -1, EC_MINIMUM_MIN, default_flush_cbk, NULL, fd,
             xdata);

    return 0;
}

int32_t ec_gf_fsync(call_frame_t * frame, xlator_t * this, fd_t * fd,
                    int32_t datasync, dict_t * xdata)
{
    ec_fsync(frame, this, -1, EC_MINIMUM_MIN, default_fsync_cbk, NULL, fd,
             datasync, xdata);

    return 0;
}

int32_t ec_gf_fsyncdir(call_frame_t * frame, xlator_t * this, fd_t * fd,
                       int32_t datasync, dict_t * xdata)
{
    ec_fsyncdir(frame, this, -1, EC_MINIMUM_MIN, default_fsyncdir_cbk, NULL,
                fd, datasync, xdata);

    return 0;
}

int
ec_marker_populate_args (call_frame_t *frame, int type, int *gauge,
                         xlator_t **subvols)
{
        xlator_t *this = frame->this;
        ec_t *ec = this->private;

        memcpy (subvols, ec->xl_list, sizeof (*subvols) * ec->nodes);

        if (type == MARKER_XTIME_TYPE) {
                /*Don't error out on ENOENT/ENOTCONN */
                gauge[MCNT_NOTFOUND] = 0;
                gauge[MCNT_ENOTCONN] = 0;
        }

        return ec->nodes;
}

int32_t
ec_handle_heal_commands (call_frame_t *frame, xlator_t *this, loc_t *loc,
                         const char *name, dict_t *xdata)
{
        dict_t  *dict_rsp = NULL;
        int     op_ret = -1;
        int     op_errno = ENOMEM;

        if (!name || strcmp (name, GF_HEAL_INFO))
                return -1;

        op_errno = -ec_get_heal_info (this, loc, &dict_rsp);
        if (op_errno <= 0) {
                op_errno = op_ret = 0;
        }

        STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict_rsp, NULL);
        if (dict_rsp)
                dict_unref (dict_rsp);
        return 0;
}

int32_t
ec_gf_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                const char *name, dict_t *xdata)
{
        int     error = 0;
        ec_t    *ec = this->private;
        int32_t minimum = EC_MINIMUM_MIN;

        if (name && strcmp (name, EC_XATTR_HEAL) != 0) {
                EC_INTERNAL_XATTR_OR_GOTO(name, NULL, error, out);
        }

        if (ec_handle_heal_commands (frame, this, loc, name, xdata) == 0)
                return 0;

        if (cluster_handle_marker_getxattr (frame, loc, name, ec->vol_uuid,
                                            NULL, ec_marker_populate_args) == 0)
                return 0;

        if (name && (fnmatch (GF_XATTR_STIME_PATTERN, name, 0) == 0))
                minimum = EC_MINIMUM_ALL;

        ec_getxattr (frame, this, -1, minimum, default_getxattr_cbk,
                     NULL, loc, name, xdata);

        return 0;
out:
        error = ENODATA;
        STACK_UNWIND_STRICT (getxattr, frame, -1, error, NULL, NULL);
        return 0;
}

int32_t
ec_gf_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 const char *name, dict_t *xdata)
{
        int     error = 0;

        EC_INTERNAL_XATTR_OR_GOTO(name, NULL, error, out);

        ec_fgetxattr (frame, this, -1, EC_MINIMUM_MIN, default_fgetxattr_cbk,
                      NULL, fd, name, xdata);
        return 0;
out:
        error = ENODATA;
        STACK_UNWIND_STRICT (fgetxattr, frame, -1, error, NULL, NULL);
        return 0;
}

int32_t ec_gf_inodelk(call_frame_t * frame, xlator_t * this,
                      const char * volume, loc_t * loc, int32_t cmd,
                      struct gf_flock * flock, dict_t * xdata)
{
    int32_t minimum = EC_MINIMUM_ALL;
    if (flock->l_type == F_UNLCK)
            minimum = EC_MINIMUM_ONE;

    ec_inodelk(frame, this, &frame->root->lk_owner, -1, minimum,
               default_inodelk_cbk, NULL, volume, loc, cmd, flock, xdata);

    return 0;
}

int32_t ec_gf_finodelk(call_frame_t * frame, xlator_t * this,
                       const char * volume, fd_t * fd, int32_t cmd,
                       struct gf_flock * flock, dict_t * xdata)
{
    int32_t minimum = EC_MINIMUM_ALL;
    if (flock->l_type == F_UNLCK)
            minimum = EC_MINIMUM_ONE;
    ec_finodelk(frame, this, &frame->root->lk_owner, -1, minimum,
                default_finodelk_cbk, NULL, volume, fd, cmd, flock, xdata);

    return 0;
}

int32_t ec_gf_link(call_frame_t * frame, xlator_t * this, loc_t * oldloc,
                   loc_t * newloc, dict_t * xdata)
{
    ec_link(frame, this, -1, EC_MINIMUM_MIN, default_link_cbk, NULL, oldloc,
            newloc, xdata);

    return 0;
}

int32_t ec_gf_lk(call_frame_t * frame, xlator_t * this, fd_t * fd,
                 int32_t cmd, struct gf_flock * flock, dict_t * xdata)
{
    int32_t minimum = EC_MINIMUM_ALL;
    if (flock->l_type == F_UNLCK)
            minimum = EC_MINIMUM_ONE;
    ec_lk(frame, this, -1, minimum, default_lk_cbk, NULL, fd, cmd,
          flock, xdata);

    return 0;
}

int32_t ec_gf_lookup(call_frame_t * frame, xlator_t * this, loc_t * loc,
                     dict_t * xdata)
{
    ec_lookup(frame, this, -1, EC_MINIMUM_MIN, default_lookup_cbk, NULL, loc,
              xdata);

    return 0;
}

int32_t ec_gf_mkdir(call_frame_t * frame, xlator_t * this, loc_t * loc,
                    mode_t mode, mode_t umask, dict_t * xdata)
{
    ec_mkdir(frame, this, -1, EC_MINIMUM_MIN, default_mkdir_cbk, NULL, loc,
             mode, umask, xdata);

    return 0;
}

int32_t ec_gf_mknod(call_frame_t * frame, xlator_t * this, loc_t * loc,
                    mode_t mode, dev_t rdev, mode_t umask, dict_t * xdata)
{
    ec_mknod(frame, this, -1, EC_MINIMUM_MIN, default_mknod_cbk, NULL, loc,
             mode, rdev, umask, xdata);

    return 0;
}

int32_t ec_gf_open(call_frame_t * frame, xlator_t * this, loc_t * loc,
                   int32_t flags, fd_t * fd, dict_t * xdata)
{
    ec_open(frame, this, -1, EC_MINIMUM_MIN, default_open_cbk, NULL, loc,
            flags, fd, xdata);

    return 0;
}

int32_t ec_gf_opendir(call_frame_t * frame, xlator_t * this, loc_t * loc,
                      fd_t * fd, dict_t * xdata)
{
    ec_opendir(frame, this, -1, EC_MINIMUM_MIN, default_opendir_cbk, NULL, loc,
               fd, xdata);

    return 0;
}

int32_t ec_gf_readdir(call_frame_t * frame, xlator_t * this, fd_t * fd,
                      size_t size, off_t offset, dict_t * xdata)
{
    ec_readdir(frame, this, -1, EC_MINIMUM_ONE, default_readdir_cbk, NULL, fd,
               size, offset, xdata);

    return 0;
}

int32_t ec_gf_readdirp(call_frame_t * frame, xlator_t * this, fd_t * fd,
                       size_t size, off_t offset, dict_t * xdata)
{
    ec_readdirp(frame, this, -1, EC_MINIMUM_ONE, default_readdirp_cbk, NULL,
                fd, size, offset, xdata);

    return 0;
}

int32_t ec_gf_readlink(call_frame_t * frame, xlator_t * this, loc_t * loc,
                       size_t size, dict_t * xdata)
{
    ec_readlink(frame, this, -1, EC_MINIMUM_ONE, default_readlink_cbk, NULL,
                loc, size, xdata);

    return 0;
}

int32_t ec_gf_readv(call_frame_t * frame, xlator_t * this, fd_t * fd,
                    size_t size, off_t offset, uint32_t flags, dict_t * xdata)
{
    ec_readv(frame, this, -1, EC_MINIMUM_MIN, default_readv_cbk, NULL, fd,
             size, offset, flags, xdata);

    return 0;
}

int32_t
ec_gf_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                   const char *name, dict_t *xdata)
{
        int     error  = 0;

        EC_INTERNAL_XATTR_OR_GOTO (name, xdata, error, out);

        ec_removexattr (frame, this, -1, EC_MINIMUM_MIN,
                        default_removexattr_cbk, NULL, loc, name, xdata);

        return 0;
out:
        STACK_UNWIND_STRICT (removexattr, frame, -1, error, NULL);
        return 0;
}

int32_t
ec_gf_fremovexattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                    const char *name, dict_t *xdata)
{
        int     error  = 0;

        EC_INTERNAL_XATTR_OR_GOTO (name, xdata, error, out);

        ec_fremovexattr (frame, this, -1, EC_MINIMUM_MIN,
                         default_fremovexattr_cbk, NULL, fd, name, xdata);

        return 0;
out:
        STACK_UNWIND_STRICT (fremovexattr, frame, -1, error, NULL);
        return 0;
}

int32_t ec_gf_rename(call_frame_t * frame, xlator_t * this, loc_t * oldloc,
                     loc_t * newloc, dict_t * xdata)
{
    ec_rename(frame, this, -1, EC_MINIMUM_MIN, default_rename_cbk, NULL,
              oldloc, newloc, xdata);

    return 0;
}

int32_t ec_gf_rmdir(call_frame_t * frame, xlator_t * this, loc_t * loc,
                    int xflags, dict_t * xdata)
{
    ec_rmdir(frame, this, -1, EC_MINIMUM_MIN, default_rmdir_cbk, NULL, loc,
             xflags, xdata);

    return 0;
}

int32_t ec_gf_setattr(call_frame_t * frame, xlator_t * this, loc_t * loc,
                      struct iatt * stbuf, int32_t valid, dict_t * xdata)
{
    ec_setattr(frame, this, -1, EC_MINIMUM_MIN, default_setattr_cbk, NULL, loc,
               stbuf, valid, xdata);

    return 0;
}

int32_t ec_gf_fsetattr(call_frame_t * frame, xlator_t * this, fd_t * fd,
                       struct iatt * stbuf, int32_t valid, dict_t * xdata)
{
    ec_fsetattr(frame, this, -1, EC_MINIMUM_MIN, default_fsetattr_cbk, NULL,
                fd, stbuf, valid, xdata);

    return 0;
}

int32_t
ec_gf_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                dict_t *dict, int32_t flags, dict_t *xdata)
{
        int     error = 0;

        EC_INTERNAL_XATTR_OR_GOTO ("", dict, error, out);

        ec_setxattr (frame, this, -1, EC_MINIMUM_MIN, default_setxattr_cbk,
                     NULL, loc, dict, flags, xdata);

        return 0;
out:
        STACK_UNWIND_STRICT (setxattr, frame, -1, error, NULL);
        return 0;
}

int32_t
ec_gf_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 dict_t *dict, int32_t flags, dict_t *xdata)
{
        int     error = 0;

        EC_INTERNAL_XATTR_OR_GOTO ("", dict, error, out);

        ec_fsetxattr (frame, this, -1, EC_MINIMUM_MIN, default_fsetxattr_cbk,
                      NULL, fd, dict, flags, xdata);

        return 0;
out:
        STACK_UNWIND_STRICT (fsetxattr, frame, -1, error, NULL);
        return 0;
}

int32_t ec_gf_stat(call_frame_t * frame, xlator_t * this, loc_t * loc,
                   dict_t * xdata)
{
    ec_stat(frame, this, -1, EC_MINIMUM_MIN, default_stat_cbk, NULL, loc,
            xdata);

    return 0;
}

int32_t ec_gf_fstat(call_frame_t * frame, xlator_t * this, fd_t * fd,
                    dict_t * xdata)
{
    ec_fstat(frame, this, -1, EC_MINIMUM_MIN, default_fstat_cbk, NULL, fd,
             xdata);

    return 0;
}

int32_t ec_gf_statfs(call_frame_t * frame, xlator_t * this, loc_t * loc,
                     dict_t * xdata)
{
    ec_statfs(frame, this, -1, EC_MINIMUM_MIN, default_statfs_cbk, NULL, loc,
              xdata);

    return 0;
}

int32_t ec_gf_symlink(call_frame_t * frame, xlator_t * this,
                      const char * linkname, loc_t * loc, mode_t umask,
                      dict_t * xdata)
{
    ec_symlink(frame, this, -1, EC_MINIMUM_MIN, default_symlink_cbk, NULL,
               linkname, loc, umask, xdata);

    return 0;
}

int32_t ec_gf_truncate(call_frame_t * frame, xlator_t * this, loc_t * loc,
                       off_t offset, dict_t * xdata)
{
    ec_truncate(frame, this, -1, EC_MINIMUM_MIN, default_truncate_cbk, NULL,
                loc, offset, xdata);

    return 0;
}

int32_t ec_gf_ftruncate(call_frame_t * frame, xlator_t * this, fd_t * fd,
                        off_t offset, dict_t * xdata)
{
    ec_ftruncate(frame, this, -1, EC_MINIMUM_MIN, default_ftruncate_cbk, NULL,
                 fd, offset, xdata);

    return 0;
}

int32_t ec_gf_unlink(call_frame_t * frame, xlator_t * this, loc_t * loc,
                     int xflags, dict_t * xdata)
{
    ec_unlink(frame, this, -1, EC_MINIMUM_MIN, default_unlink_cbk, NULL, loc,
              xflags, xdata);

    return 0;
}

int32_t ec_gf_writev(call_frame_t * frame, xlator_t * this, fd_t * fd,
                     struct iovec * vector, int32_t count, off_t offset,
                     uint32_t flags, struct iobref * iobref, dict_t * xdata)
{
    ec_writev(frame, this, -1, EC_MINIMUM_MIN, default_writev_cbk, NULL, fd,
              vector, count, offset, flags, iobref, xdata);

    return 0;
}

int32_t ec_gf_xattrop(call_frame_t * frame, xlator_t * this, loc_t * loc,
                      gf_xattrop_flags_t optype, dict_t * xattr,
                      dict_t * xdata)
{
    ec_xattrop(frame, this, -1, EC_MINIMUM_MIN, default_xattrop_cbk, NULL, loc,
               optype, xattr, xdata);

    return 0;
}

int32_t ec_gf_fxattrop(call_frame_t * frame, xlator_t * this, fd_t * fd,
                       gf_xattrop_flags_t optype, dict_t * xattr,
                       dict_t * xdata)
{
    ec_fxattrop(frame, this, -1, EC_MINIMUM_MIN, default_fxattrop_cbk, NULL,
                fd, optype, xattr, xdata);

    return 0;
}

int32_t ec_gf_zerofill(call_frame_t * frame, xlator_t * this, fd_t * fd,
                       off_t offset, off_t len, dict_t * xdata)
{
    default_zerofill_failure_cbk(frame, ENOTSUP);

    return 0;
}

int32_t ec_gf_seek(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                   gf_seek_what_t what, dict_t *xdata)
{
    ec_seek(frame, this, -1, EC_MINIMUM_ONE, default_seek_cbk, NULL, fd,
            offset, what, xdata);

    return 0;
}

int32_t ec_gf_ipc(call_frame_t *frame, xlator_t *this, int32_t op,
                  dict_t *xdata)
{
    ec_ipc(frame, this, -1, EC_MINIMUM_MIN, default_ipc_cbk, NULL, op, xdata);
    return 0;
}

int32_t ec_gf_forget(xlator_t * this, inode_t * inode)
{
    uint64_t value = 0;
    ec_inode_t * ctx = NULL;

    if ((inode_ctx_del(inode, this, &value) == 0) && (value != 0))
    {
        ctx = (ec_inode_t *)(uintptr_t)value;
        GF_FREE(ctx);
    }

    return 0;
}

void ec_gf_release_fd(xlator_t * this, fd_t * fd)
{
    uint64_t value = 0;
    ec_fd_t * ctx = NULL;

    if ((fd_ctx_del(fd, this, &value) == 0) && (value != 0))
    {
        ctx = (ec_fd_t *)(uintptr_t)value;
        loc_wipe(&ctx->loc);
        GF_FREE(ctx);
    }
}

int32_t ec_gf_release(xlator_t * this, fd_t * fd)
{
    ec_gf_release_fd(this, fd);

    return 0;
}

int32_t ec_gf_releasedir(xlator_t * this, fd_t * fd)
{
    ec_gf_release_fd(this, fd);

    return 0;
}

int32_t ec_dump_private(xlator_t *this)
{
    ec_t *ec = NULL;
    char  key_prefix[GF_DUMP_MAX_BUF_LEN];
    char  tmp[65];

    GF_ASSERT(this);

    ec = this->private;
    GF_ASSERT(ec);

    snprintf(key_prefix, GF_DUMP_MAX_BUF_LEN, "%s.%s", this->type, this->name);
    gf_proc_dump_add_section(key_prefix);
    gf_proc_dump_write("nodes", "%u", ec->nodes);
    gf_proc_dump_write("redundancy", "%u", ec->redundancy);
    gf_proc_dump_write("fragment_size", "%u", ec->fragment_size);
    gf_proc_dump_write("stripe_size", "%u", ec->stripe_size);
    gf_proc_dump_write("childs_up", "%u", ec->xl_up_count);
    gf_proc_dump_write("childs_up_mask", "%s",
                       ec_bin(tmp, sizeof(tmp), ec->xl_up, ec->nodes));
    gf_proc_dump_write("background-heals", "%d", ec->background_heals);
    gf_proc_dump_write("heal-wait-qlength", "%d", ec->heal_wait_qlen);
    gf_proc_dump_write("healers", "%d", ec->healers);
    gf_proc_dump_write("heal-waiters", "%d", ec->heal_waiters);
    gf_proc_dump_write("read-policy", "%s", ec_read_policies[ec->read_policy]);

    return 0;
}

struct xlator_fops fops =
{
    .lookup       = ec_gf_lookup,
    .stat         = ec_gf_stat,
    .fstat        = ec_gf_fstat,
    .truncate     = ec_gf_truncate,
    .ftruncate    = ec_gf_ftruncate,
    .access       = ec_gf_access,
    .readlink     = ec_gf_readlink,
    .mknod        = ec_gf_mknod,
    .mkdir        = ec_gf_mkdir,
    .unlink       = ec_gf_unlink,
    .rmdir        = ec_gf_rmdir,
    .symlink      = ec_gf_symlink,
    .rename       = ec_gf_rename,
    .link         = ec_gf_link,
    .create       = ec_gf_create,
    .open         = ec_gf_open,
    .readv        = ec_gf_readv,
    .writev       = ec_gf_writev,
    .flush        = ec_gf_flush,
    .fsync        = ec_gf_fsync,
    .opendir      = ec_gf_opendir,
    .readdir      = ec_gf_readdir,
    .readdirp     = ec_gf_readdirp,
    .fsyncdir     = ec_gf_fsyncdir,
    .statfs       = ec_gf_statfs,
    .setxattr     = ec_gf_setxattr,
    .getxattr     = ec_gf_getxattr,
    .fsetxattr    = ec_gf_fsetxattr,
    .fgetxattr    = ec_gf_fgetxattr,
    .removexattr  = ec_gf_removexattr,
    .fremovexattr = ec_gf_fremovexattr,
    .lk           = ec_gf_lk,
    .inodelk      = ec_gf_inodelk,
    .finodelk     = ec_gf_finodelk,
    .entrylk      = ec_gf_entrylk,
    .fentrylk     = ec_gf_fentrylk,
    .xattrop      = ec_gf_xattrop,
    .fxattrop     = ec_gf_fxattrop,
    .setattr      = ec_gf_setattr,
    .fsetattr     = ec_gf_fsetattr,
    .fallocate    = ec_gf_fallocate,
    .discard      = ec_gf_discard,
    .zerofill     = ec_gf_zerofill,
    .seek         = ec_gf_seek,
    .ipc          = ec_gf_ipc
};

struct xlator_cbks cbks =
{
    .forget            = ec_gf_forget,
    .release           = ec_gf_release,
    .releasedir        = ec_gf_releasedir
};

struct xlator_dumpops dumpops = {
    .priv = ec_dump_private
};

struct volume_options options[] =
{
    {
        .key = { "redundancy" },
        .type = GF_OPTION_TYPE_INT,
        .description = "Maximum number of bricks that can fail "
                       "simultaneously without losing data."
    },
    {
        .key = { "self-heal-daemon" },
        .type = GF_OPTION_TYPE_BOOL,
        .description = "self-heal daemon enable/disable",
        .default_value = "enable",
    },
    { .key = {"iam-self-heal-daemon"},
      .type = GF_OPTION_TYPE_BOOL,
      .default_value = "off",
      .description = "This option differentiates if the disperse "
                     "translator is running as part of self-heal-daemon "
                     "or not."
    },
    { .key = {"eager-lock"},
      .type = GF_OPTION_TYPE_BOOL,
      .default_value = "on",
      .description = "Enable/Disable eager lock for disperse volume. "
                     "If a fop takes a lock and completes its operation, "
                     "it waits for next 1 second before releasing the lock, "
                     "to see if the lock can be reused for next fop from "
                     "the same client. If ec finds any lock contention within "
                     "1 second it releases the lock immediately before time "
                     "expires. This improves the performance of file operations."
                     "However, as it takes lock on first brick, for few operations "
                     "like read, discovery of lock contention might take long time "
                     "and can actually degrade the performance. "
                     "If eager lock is disabled, lock will be released as soon as fop "
                     "completes. "
    },
    { .key = {"background-heals"},
      .type = GF_OPTION_TYPE_INT,
      .min = 0,/*Disabling background heals*/
      .max = 256,
      .default_value = "8",
      .description = "This option can be used to control number of parallel"
                     " heals",
    },
    { .key = {"heal-wait-qlength"},
      .type = GF_OPTION_TYPE_INT,
      .min = 0,
      .max = 65536, /*Around 100MB as of now with sizeof(ec_fop_data_t) at 1800*/
      .default_value = "128",
      .description = "This option can be used to control number of heals"
                     " that can wait",
    },
    { .key  = {"heal-timeout"},
      .type = GF_OPTION_TYPE_INT,
      .min  = 60,
      .max  = INT_MAX,
      .default_value = "600",
      .description = "time interval for checking the need to self-heal "
                     "in self-heal-daemon"
    },
    { .key = {"read-policy" },
      .type = GF_OPTION_TYPE_STR,
      .value = {"round-robin", "gfid-hash"},
      .default_value = "round-robin",
      .description = "inode-read fops happen only on 'k' number of bricks in"
              " n=k+m disperse subvolume. 'round-robin' selects the read"
              " subvolume using round-robin algo. 'gfid-hash' selects read"
              " subvolume based on hash of the gfid of that file/directory.",
    },
    { .key   = {"shd-max-threads"},
      .type  = GF_OPTION_TYPE_INT,
      .min   = 1,
      .max   = 64,
      .default_value = "1",
      .description = "Maximum number of parallel heals SHD can do per local "
                      "brick.  This can substantially lower heal times, "
                      "but can also crush your bricks if you don't have "
                      "the storage hardware to support this."
    },
    { .key   = {"shd-wait-qlength"},
      .type  = GF_OPTION_TYPE_INT,
      .min   = 1,
      .max   = 655536,
      .default_value = "1024",
      .description = "This option can be used to control number of heals"
                     " that can wait in SHD per subvolume"
    },
    {
        .key = { "cpu-extensions" },
        .type = GF_OPTION_TYPE_STR,
        .value = { "none", "auto", "x64", "sse", "avx" },
        .default_value = "auto",
        .description = "force the cpu extensions to be used to accelerate the "
                       "galois field computations."
    },
    { }
};
