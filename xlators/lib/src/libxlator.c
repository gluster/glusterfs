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
        if (!local)
                return;

        switch (op_errno) {
                case ENODATA:
                        local->enodata_count++;
                        break;
                case ENOENT:
                        local->enoent_count++;
                        break;
                case ENOTCONN:
                        local->enotconn_count++;
                        break;
                default:
                        break;
        }
}

/* Aggregate all the <volid>.xtime attrs of the cluster and send the max*/
int32_t
cluster_markerxtime_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int op_ret, int op_errno, dict_t *dict, dict_t *xdata)

{

        int32_t           callcnt         = 0;
        int               ret             = -1;
        uint32_t          *net_timebuf    = NULL;
        uint32_t          host_timebuf[2] = {0,};
        char              *marker_xattr   = NULL;
        xl_marker_local_t *local          = NULL;
        char              *vol_uuid       = NULL;
        char              need_unwind     = 0;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("", GF_LOG_DEBUG, "possible NULL deref");
                need_unwind = 1;
                goto out;
        }

        local = frame->local;
        if (!local || !local->vol_uuid) {
                gf_log (this->name, GF_LOG_DEBUG, "possible NULL deref");
                need_unwind = 1;
                goto out;
        }

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (local->esomerr)
                        goto unlock;

                vol_uuid = local->vol_uuid;

                if (op_ret) {
                        marker_local_incr_errcount (local, op_errno);
                        local->esomerr = op_errno;
                        goto unlock;
                }

                if (!gf_asprintf (&marker_xattr, "%s.%s.%s",
                                MARKER_XATTR_PREFIX, vol_uuid, XTIME)) {
                        op_errno = ENOMEM;
                        goto unlock;
                }


                if (dict_get_ptr (dict, marker_xattr, (void **)&net_timebuf)) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Unable to get <uuid>.xtime attr");
                        local->noxtime_count++;
                        goto unlock;
                }

                if (local->has_xtime) {
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
                        local->has_xtime = _gf_true;
                }

        }
unlock:
        UNLOCK (&frame->lock);

        if (!callcnt) {
                op_ret = 0;
                op_errno = 0;
                need_unwind = 1;

                if (local->has_xtime) {
                        if (!dict)
                                dict = dict_new();

                        ret = dict_set_static_bin (dict, marker_xattr,
                                           (void *)local->net_timebuf, 8);
                        if (ret) {
                                op_ret = -1;
                                op_errno = ENOMEM;
                                goto out;
                        }
                }

                if (local->noxtime_count)
                        goto out;

                if (local->enodata_count || local->enotconn_count ||
                                              local->enoent_count) {
                        op_ret = -1;
                        op_errno = local->enodata_count? ENODATA:
                                        local->enotconn_count? ENOTCONN:
                                        local->enoent_count? ENOENT:
                                        local->esomerr;
                }
        }

out:
        if (need_unwind && local && local->xl_specf_unwind) {
                frame->local = local->xl_local;
                local->xl_specf_unwind (frame, op_ret,
                                        op_errno, dict, xdata);
        } else if (need_unwind) {
                STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno,
                                     dict, xdata);
        }

        GF_FREE (marker_xattr);
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
        char                need_unwind = 0;
        char                *vol_uuid   = NULL;

        if (!this || !frame || !cookie) {
                gf_log ("", GF_LOG_DEBUG, "possible NULL deref");
                need_unwind = 1;
                goto out;
        }

        local = frame->local;

        if (!local) {
                gf_log (this->name, GF_LOG_DEBUG, "possible NULL deref");
                need_unwind = 1;
                goto out;
        }

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

                if (marker_has_volinfo (local)) {
                        if ((local->volmark->major != volmark->major) ||
                            (local->volmark->minor != volmark->minor)) {
                                op_ret = -1;
                                op_errno = EINVAL;
                                goto unlock;
                        }

                        if (local->retval)
                                goto unlock;
                        else if (volmark->retval) {
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
                        uuid_unparse (volmark->uuid, vol_uuid);
                        if (volmark->retval)
                                local->retval = volmark->retval;
                }
        }
unlock:
        UNLOCK (&frame->lock);

        if (!callcnt) {
                op_ret = 0;
                op_errno = 0;
                need_unwind = 1;

                if (marker_has_volinfo (local)) {
                        if (!dict)
                                dict = dict_new();

                        if (dict_set_bin (dict, GF_XATTR_MARKER_KEY,
                                          local->volmark,
                                          sizeof (struct volume_mark))) {
                                op_ret = -1;
                                op_errno = ENOMEM;
                        }
                } else {
                        op_ret = -1;
                        op_errno = local->enotconn_count? ENOTCONN:
                               local->enoent_count? ENOENT:EINVAL;
                }
        }

 out:
        if (need_unwind && local && local->xl_specf_unwind) {
                frame->local = local->xl_local;
                local->xl_specf_unwind (frame, op_ret,
                                        op_errno, dict, xdata);
                return 0;
        } else if (need_unwind){
                STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno,
                                     dict, xdata);
        }
        return 0;
}


int32_t
cluster_getmarkerattr (call_frame_t *frame,xlator_t *this, loc_t *loc,
                       const char *name, void *xl_local,
                       xlator_specf_unwind_t xl_specf_getxattr_unwind,
                       xlator_t **sub_volumes, int count, int type,
                       char *vol_uuid)
{
        int                i     = 0;
        xl_marker_local_t  *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (name, err);
        VALIDATE_OR_GOTO (xl_specf_getxattr_unwind, err);

        local = GF_CALLOC (sizeof (struct marker_str), 1,
                            gf_common_mt_libxl_marker_local);

        if (!local)
                goto err;

        local->xl_local = xl_local;
        local->call_count = count;
        local->xl_specf_unwind = xl_specf_getxattr_unwind;
        local->vol_uuid = vol_uuid;

        frame->local = local;

        for (i=0; i < count; i++) {
                if (MARKER_UUID_TYPE == type)
                        STACK_WIND (frame, cluster_markeruuid_cbk,
                                    *(sub_volumes + i),
                                    (*(sub_volumes + i))->fops->getxattr,
                                    loc, name, NULL);
                else if (MARKER_XTIME_TYPE == type)
                        STACK_WIND (frame, cluster_markerxtime_cbk,
                                    *(sub_volumes + i),
                                    (*(sub_volumes + i))->fops->getxattr,
                                    loc, name, NULL);
                else {
                        gf_log (this->name, GF_LOG_WARNING,
                                 "Unrecognized type (%d) of marker attr "
                                 "received", type);
                        STACK_WIND (frame, default_getxattr_cbk,
                                    *(sub_volumes + i),
                                    (*(sub_volumes + i))->fops->getxattr,
                                    loc, name, NULL);
                        break;
                }
        }

        return 0;
err:
        return -1;

}
