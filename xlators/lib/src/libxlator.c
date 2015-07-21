/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "mem-types.h"
#include "libxlator.h"


int marker_xtime_default_gauge[] = {
        [MCNT_FOUND]    =  1,
        [MCNT_NOTFOUND] = -1,
        [MCNT_ENODATA]  = -1,
        [MCNT_ENOTCONN] = -1,
        [MCNT_ENOENT]   = -1,
        [MCNT_EOTHER]   = -1,
};

int marker_uuid_default_gauge[] = {
        [MCNT_FOUND]    =  1,
        [MCNT_NOTFOUND] =  0,
        [MCNT_ENODATA]  =  0,
        [MCNT_ENOTCONN] =  0,
        [MCNT_ENOENT]   =  0,
        [MCNT_EOTHER]   =  0,
};

static int marker_idx_errno_map[] = {
        [MCNT_FOUND]    = EINVAL,
        [MCNT_NOTFOUND] = EINVAL,
        [MCNT_ENOENT]   = ENOENT,
        [MCNT_ENOTCONN] = ENOTCONN,
        [MCNT_ENODATA]  = ENODATA,
        [MCNT_EOTHER]   = EINVAL,
        [MCNT_MAX]      = 0,
};

/*Copy the contents of oldtimebuf to newtimbuf*/
static void
update_timebuf (uint32_t *oldtimbuf, uint32_t *newtimebuf)
{
        newtimebuf[0] =  (oldtimbuf[0]);
        newtimebuf[1] =  (oldtimbuf[1]);
}

/* Convert Timebuf in network order to host order */
static void
get_hosttime (uint32_t *oldtimbuf, uint32_t *newtimebuf)
{
        newtimebuf[0] = ntohl (oldtimbuf[0]);
        newtimebuf[1] = ntohl (oldtimbuf[1]);
}



/* Match the Incoming trusted.glusterfs.<uuid>.xtime against volume uuid */
int
match_uuid_local (const char *name, char *uuid)
{
        if (!uuid || !*uuid)
                return -1;

        name = strtail ((char *)name, MARKER_XATTR_PREFIX);
        if (!name || name++[0] != '.')
                return -1;

        name = strtail ((char *)name, uuid);
        if (!name || strcmp (name, ".xtime") != 0)
                return -1;

        return 0;
}

static void
marker_local_incr_errcount (xl_marker_local_t *local, int op_errno)
{
        marker_result_idx_t i = -1;

        if (!local)
                return;

        switch (op_errno) {
                case ENODATA:
                        i = MCNT_ENODATA;
                        break;
                case ENOENT:
                        i = MCNT_ENOENT;
                        break;
                case ENOTCONN:
                        i = MCNT_ENOTCONN;
                        break;
                default:
                        i = MCNT_EOTHER;
                        break;
        }

        local->count[i]++;
}

static int
evaluate_marker_results (int *gauge, int *count)
{
        int i = 0;
        int op_errno = 0;
        gf_boolean_t sane = _gf_true;

        /* check if the policy of the gauge is violated;
         * if yes, try to get the best errno, ie. look
         * for the first position where there is a more
         * specific kind of vioilation than the generic EINVAL
         */
        for (i = 0; i < MCNT_MAX; i++) {
                if (sane) {
                        if ((gauge[i] > 0 && count[i] < gauge[i]) ||
                            (gauge[i] < 0 && count[i] >= -gauge[i])) {
                                sane = _gf_false;
                                /* generic action: adopt corresponding errno */
                                op_errno = marker_idx_errno_map[i];
                        }
                } else {
                        /* already insane; trying to get a more informative
                         * errno by checking subsequent counters
                         */
                        if (count[i] > 0)
                                op_errno = marker_idx_errno_map[i];
                }
                if (op_errno && op_errno != EINVAL)
                        break;
        }

        return op_errno;
}

static void
cluster_marker_unwind (call_frame_t *frame, char *key, void *value, size_t size,
                       dict_t *dict)
{
        xl_marker_local_t *local   = frame->local;
        int               ret      = 0;
        int32_t           op_ret   = 0;
        int32_t           op_errno = 0;
        gf_boolean_t      unref    = _gf_false;

        frame->local = local->xl_local;

        if (local->count[MCNT_FOUND]) {
                if (!dict) {
                        dict = dict_new();
                        if (dict) {
                                unref = _gf_true;
                        } else {
                                op_ret = -1;
                                op_errno = ENOMEM;
                                goto out;
                        }
                }

                ret = dict_set_static_bin (dict, key, value, size);
                if (ret) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto out;
                }
        }

        op_errno = evaluate_marker_results (local->gauge, local->count);
        if (op_errno)
                op_ret = -1;

out:
        if (local->xl_specf_unwind) {
                local->xl_specf_unwind (frame, op_ret,
                                        op_errno, dict, NULL);
        } else {
                STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno,
                                     dict, NULL);
        }

        GF_FREE (local);
        if (unref)
                dict_unref (dict);

}

/* Aggregate all the <volid>.xtime attrs of the cluster and send the max*/
int32_t
cluster_markerxtime_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int op_ret, int op_errno, dict_t *dict, dict_t *xdata)

{

        int32_t           callcnt         = 0;
        uint32_t          *net_timebuf    = NULL;
        uint32_t          host_timebuf[2] = {0,};
        char              marker_xattr[128]   = {0};
        xl_marker_local_t *local          = NULL;

        local = frame->local;

        snprintf (marker_xattr, sizeof (marker_xattr), "%s.%s.%s",
                  MARKER_XATTR_PREFIX, local->vol_uuid, XTIME);

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret) {
                        marker_local_incr_errcount (local, op_errno);
                        goto unlock;
                }

                if (dict_get_ptr (dict, marker_xattr, (void **)&net_timebuf)) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Unable to get <uuid>.xtime attr");
                        local->count[MCNT_NOTFOUND]++;
                        goto unlock;
                }

                if (local->count[MCNT_FOUND]) {
                        get_hosttime (net_timebuf, host_timebuf);
                        if ( (host_timebuf[0]>local->host_timebuf[0]) ||
                                (host_timebuf[0] == local->host_timebuf[0] &&
                                 host_timebuf[1] >= local->host_timebuf[1])) {
                                update_timebuf (net_timebuf, local->net_timebuf);
                                update_timebuf (host_timebuf, local->host_timebuf);
                        }

                } else {
                        get_hosttime (net_timebuf, local->host_timebuf);
                        update_timebuf (net_timebuf, local->net_timebuf);
                        local->count[MCNT_FOUND]++;
                }

        }
unlock:
        UNLOCK (&frame->lock);

        if (callcnt == 0)
                cluster_marker_unwind (frame, marker_xattr, local->net_timebuf,
                                       8, dict);

        return 0;

}

int32_t
cluster_markeruuid_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, dict_t *dict, dict_t *xdata)
{
        int32_t             callcnt     = 0;
        struct volume_mark  *volmark    = NULL;
        xl_marker_local_t   *local      = NULL;
        int32_t             ret         = -1;
        char                *vol_uuid   = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;
                vol_uuid = local->vol_uuid;

                if (op_ret) {
                        marker_local_incr_errcount (local, op_errno);
                        goto unlock;
                }

                ret = dict_get_bin (dict, GF_XATTR_MARKER_KEY,
                                    (void *)&volmark);
                if (ret)
                        goto unlock;

                if (local->count[MCNT_FOUND]) {
                        if ((local->volmark->major != volmark->major) ||
                            (local->volmark->minor != volmark->minor)) {
                                op_ret = -1;
                                op_errno = EINVAL;
                                goto unlock;
                        }

                        if (local->retval) {
                                goto unlock;
                        } else if (volmark->retval) {
                                GF_FREE (local->volmark);
                                local->volmark =
                                        memdup (volmark, sizeof (*volmark));
                                local->retval = volmark->retval;
                        } else if ((volmark->sec > local->volmark->sec) ||
                                   ((volmark->sec == local->volmark->sec) &&
                                    (volmark->usec >= local->volmark->usec))) {
                                GF_FREE (local->volmark);
                                local->volmark =
                                      memdup (volmark, sizeof (*volmark));
                        }

                } else {
                        local->volmark = memdup (volmark, sizeof (*volmark));
                        VALIDATE_OR_GOTO (local->volmark, unlock);
                        gf_uuid_unparse (volmark->uuid, vol_uuid);
                        if (volmark->retval)
                                local->retval = volmark->retval;
                        local->count[MCNT_FOUND]++;
                }
        }
unlock:
        UNLOCK (&frame->lock);

        if (callcnt == 0)
                cluster_marker_unwind (frame, GF_XATTR_MARKER_KEY,
                                       local->volmark, sizeof (*local->volmark),
                                       dict);

        return 0;
}

int
gf_get_min_stime (xlator_t *this, dict_t *dst, char *key, data_t *value)
{
        int ret = -1;
        uint32_t *net_timebuf = NULL;
        uint32_t *value_timebuf = NULL;
        uint32_t host_timebuf[2] = {0,};
        uint32_t host_value_timebuf[2] = {0,};

        /* stime should be minimum of all the other nodes */
        ret = dict_get_bin (dst, key, (void **)&net_timebuf);
        if (ret < 0) {
                net_timebuf = GF_CALLOC (1, sizeof (int64_t),
                                           gf_common_mt_char);
                if (!net_timebuf)
                        goto out;

                ret = dict_set_bin (dst, key, net_timebuf, sizeof (int64_t));
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "key=%s: dict set failed", key);
                        goto error;
                }
        }

        value_timebuf = data_to_bin (value);
        if (!value_timebuf) {
                gf_log (this->name, GF_LOG_WARNING,
                        "key=%s: getting value of stime failed", key);
                ret = -1;
                goto out;
        }

        get_hosttime (value_timebuf, host_value_timebuf);
        get_hosttime (net_timebuf, host_timebuf);

        /* can't use 'min()' macro here as we need to compare two fields
           in the array, selectively */
        if ((host_value_timebuf[0] < host_timebuf[0]) ||
            ((host_value_timebuf[0] == host_timebuf[0]) &&
             (host_value_timebuf[1] < host_timebuf[1]))) {
                update_timebuf (value_timebuf, net_timebuf);
        }

        ret = 0;
out:
        return ret;
error:
        /* To be used only when net_timebuf is not set in the dict */
        if (net_timebuf)
                GF_FREE (net_timebuf);

        return ret;
}

int
gf_get_max_stime (xlator_t *this, dict_t *dst, char *key, data_t *value)
{
        int ret = -ENOMEM;
        uint32_t *net_timebuf = NULL;
        uint32_t *value_timebuf = NULL;
        uint32_t host_timebuf[2] = {0,};
        uint32_t host_value_timebuf[2] = {0,};

        /* stime should be maximum of all the other nodes */
        ret = dict_get_bin (dst, key, (void **)&net_timebuf);
        if (ret < 0) {
                net_timebuf = GF_CALLOC (1, sizeof (int64_t),
                                           gf_common_mt_char);
                if (!net_timebuf)
                        goto out;

                ret = dict_set_bin (dst, key, net_timebuf, sizeof (int64_t));
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "key=%s: dict set failed", key);
                        goto error;
                }
        }

        value_timebuf = data_to_bin (value);
        if (!value_timebuf) {
                gf_log (this->name, GF_LOG_WARNING,
                        "key=%s: getting value of stime failed", key);
                ret = -EINVAL;
                goto out;
        }

        get_hosttime (value_timebuf, host_value_timebuf);
        get_hosttime (net_timebuf, host_timebuf);

        /* can't use 'max()' macro here as we need to compare two fields
           in the array, selectively */
        if ((host_value_timebuf[0] > host_timebuf[0]) ||
            ((host_value_timebuf[0] == host_timebuf[0]) &&
             (host_value_timebuf[1] > host_timebuf[1]))) {
                update_timebuf (value_timebuf, net_timebuf);
        }

        ret = 0;
out:
        return ret;
error:
        /* To be used only when net_timebuf is not set in the dict */
        if (net_timebuf)
                GF_FREE (net_timebuf);

        return ret;
}

static int
_get_children_count (xlator_t *xl)
{
        int i = 0;
        xlator_list_t *trav = NULL;
        for (i = 0, trav = xl->children; trav ; trav = trav->next, i++) {
                /*'i' will have the value */
        }

        return i;
}

int
cluster_handle_marker_getxattr (call_frame_t *frame, loc_t *loc,
                                const char *name, char *vol_uuid,
                                xlator_specf_unwind_t unwind,
                                int (*populate_args) (call_frame_t *frame,
                                                      int type, int *gauge,
                                                      xlator_t **subvols))
{
        xlator_t                *this = frame->this;
        xlator_t                **subvols = NULL;
        int                     num_subvols = 0;
        int                     type = 0;
        int                i     = 0;
        int                     gauge[MCNT_MAX] = {0};
        xl_marker_local_t  *local = NULL;

        if (GF_CLIENT_PID_GSYNCD != frame->root->pid)
                return -EINVAL;

        if (name == NULL)
                return -EINVAL;

        if (strcmp (GF_XATTR_MARKER_KEY, name) == 0) {
                type = MARKER_UUID_TYPE;
                memcpy (gauge, marker_uuid_default_gauge, sizeof (gauge));
        } else if (match_uuid_local (name, vol_uuid) == 0) {
                type = MARKER_XTIME_TYPE;
                memcpy (gauge, marker_xtime_default_gauge, sizeof (gauge));
        } else {
                return -EINVAL;
        }

        num_subvols = _get_children_count (this);
        subvols = alloca (num_subvols * sizeof (*subvols));
        num_subvols = populate_args (frame, type, gauge, subvols);

        local = GF_CALLOC (sizeof (struct marker_str), 1,
                            gf_common_mt_libxl_marker_local);

        if (!local)
                goto fail;

        local->xl_local = frame->local;
        local->call_count = num_subvols;
        local->xl_specf_unwind = unwind;
        local->vol_uuid = vol_uuid;
        memcpy (local->gauge, gauge, sizeof (local->gauge));

        frame->local = local;

        for (i = 0; i < num_subvols; i++) {
                if (MARKER_UUID_TYPE == type)
                        STACK_WIND (frame, cluster_markeruuid_cbk,
                                    subvols[i],
                                    subvols[i]->fops->getxattr,
                                    loc, name, NULL);
                else if (MARKER_XTIME_TYPE == type)
                        STACK_WIND (frame, cluster_markerxtime_cbk,
                                    subvols[i],
                                    subvols[i]->fops->getxattr,
                                    loc, name, NULL);
        }

        return 0;
fail:
        if (unwind)
                unwind (frame, -1, ENOMEM, NULL, NULL);
        else
                default_getxattr_failure_cbk (frame, ENOMEM);
        return 0;
}
