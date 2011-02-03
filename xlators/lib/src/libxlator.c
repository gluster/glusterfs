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




/* Aggregate all the <volid>.xtime attrs of the cluster and send the max*/
int32_t
cluster_markerxtime_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, dict_t *dict)

{

        int32_t            callcnt = 0;
        int                ret = -1;
        uint32_t          *net_timebuf;
        uint32_t           host_timebuf[2];
        char              *marker_xattr;
        struct marker_str *local;
        char              *vol_uuid;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log (this->name, GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        local = frame->local;
        if (!local || !local->vol_uuid) {
                gf_log (this->name, GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        if (local->esomerr) {
                 LOCK (&frame->lock);
                {
                        callcnt = --local->call_count;
                }
                goto done;
        }

        vol_uuid = local->vol_uuid;

        if (op_ret && op_errno == ENODATA) {
                LOCK (&frame->lock);
                {
                        callcnt = --local->call_count;
                        local->enodata_count++;
                }
                goto done;
        }

        if (op_ret && op_errno == ENOENT) {
                LOCK (&frame->lock);
                {
                        callcnt = --local->call_count;
                        local->enoent_count++;
                }
                goto done;
        }

        if (op_ret && op_errno == ENOTCONN) {
                LOCK (&frame->lock);
                {
                        callcnt = --local->call_count;
                        local->enotconn_count++;
                }
                goto done;
        }

        if (op_ret) {
                LOCK (&frame->lock);
                {
                        callcnt = --local->call_count;
                        local->esomerr = op_errno;
                }
                goto done;
        }




        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;
                if (!gf_asprintf (& marker_xattr, "%s.%s.%s",
                                MARKER_XATTR_PREFIX, vol_uuid, XTIME)) {
                        op_errno = ENOMEM;
                        goto done;
                }


                if (dict_get_ptr (dict, marker_xattr, (void **)&net_timebuf)) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Unable to get <uuid>.xtime attr");
                        local->noxtime_count++;
                        goto done;
                }

                if (local->has_xtime) {

                        get_hosttime (net_timebuf, host_timebuf);
                        if ( (host_timebuf[0]>local->host_timebuf[0]) ||
                                (host_timebuf[0] == local->host_timebuf[0] &&
                                 host_timebuf[1] >= local->host_timebuf[1])) {

                                update_timebuf (net_timebuf, local->net_timebuf);
                                update_timebuf (host_timebuf, local->host_timebuf);

                        }

                }
                else {
                        get_hosttime (net_timebuf, local->host_timebuf);
                        update_timebuf (net_timebuf, local->net_timebuf);
                        local->has_xtime = _gf_true;
                }



        }
done:
        UNLOCK (&frame->lock);

        if (!callcnt) {

                op_ret = 0;
                op_errno = 0;
                if (local->has_xtime) {
                        if (!dict) {
                                dict = dict_new();
                                if (ret) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        goto out;
                                }
                        }
                        ret = dict_set_static_bin (dict, marker_xattr,
                                           (void *)local->net_timebuf, 8);
                        if (ret) {
                                op_ret = -1;
                                op_errno = ENOMEM;
                                goto out;
                        }
                        goto out;
                }

                if (local->noxtime_count)
                        goto out;

                if (local->enodata_count) {
                        op_ret = -1;
                        op_errno = ENODATA;
                        goto out;
                }
                if (local->enotconn_count) {
                        op_ret = -1;
                        op_errno = ENOTCONN;
                        goto out;
                }
                if (local->enoent_count) {
                        op_ret = -1;
                        op_errno = ENOENT;
                        goto out;
                }
                else {
                        op_errno = local->esomerr;
                        goto out;
                }
out:
                if (local->xl_specf_unwind) {
                        frame->local = local->xl_local;
                        local->xl_specf_unwind (getxattr, frame, op_ret,
                                                 op_errno, dict);
                        return 0;
                }
                STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict);

        }

        return 0;

}

int32_t
cluster_markeruuid_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, dict_t *dict)
{
        int32_t              callcnt = 0;
        data_t              *data = NULL;
        struct volume_mark  *volmark = NULL;
        struct marker_str   *marker = NULL;
        char                *vol_uuid;


        if (!this || !frame || !cookie) {
                gf_log (this->name, GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        marker = frame->local;

        if (!marker) {
                gf_log (this->name, GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        vol_uuid = marker->vol_uuid;

        if (op_ret && (ENOENT == op_errno)) {
                LOCK (&frame->lock);
                {
                        callcnt = --marker->call_count;
                        marker->enoent_count++;
                }
                goto done;
        }

        if (op_ret && (ENOTCONN == op_errno)) {
                LOCK (&frame->lock);
                {
                        callcnt = --marker->call_count;
                        marker->enotconn_count++;
                }
                goto done;
        }

        if (!(data = dict_get (dict, GF_XATTR_MARKER_KEY))) {
                LOCK (&frame->lock);
                {
                        callcnt = --marker->call_count;
                }
                goto done;
        }

        volmark = (struct volume_mark *)data->data;

        LOCK (&frame->lock);
        {
                callcnt = --marker->call_count;

                if (marker_has_volinfo (marker)) {

                        if ((marker->volmark->major != volmark->major) ||
                            (marker->volmark->minor != volmark->minor)) {
                                op_ret = -1;
                                op_errno = EINVAL;
                                goto done;
                        }
                        else if (volmark->retval) {
                                data_unref ((data_t *) marker->volmark);
                                marker->volmark = volmark;
                                callcnt = 0;
                        }
                        else if ( (volmark->sec > marker->volmark->sec) ||
                                   ((volmark->sec == marker->volmark->sec)
                                      && (volmark->usec >= marker->volmark->usec))) {

                                GF_FREE (marker->volmark);
                                marker->volmark = memdup (volmark, sizeof (struct volume_mark));
                                VALIDATE_OR_GOTO (marker->volmark, done);
                        }

                } else {
                        marker->volmark = memdup (volmark, sizeof (struct volume_mark));
                        VALIDATE_OR_GOTO (marker->volmark, done);

                        uuid_unparse (volmark->uuid, vol_uuid);
                        if (volmark->retval)
                                callcnt = 0;
                }
        }
done:
        UNLOCK (&frame->lock);

        if (!callcnt) {
                op_ret = 0;
                op_errno = 0;
                if (marker_has_volinfo (marker)) {
                        if (!dict) {
                                dict = dict_new();
                                if (!dict) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        goto out;
                                }
                        }
                        if (dict_set_bin (dict, GF_XATTR_MARKER_KEY,
                                          marker->volmark,
                                          sizeof (struct volume_mark))) {
                                op_ret = -1;
                                op_errno = ENOMEM;
                        }
                        goto out;
                }
                if (marker->enotconn_count) {
                        op_ret = -1;
                        op_errno = ENOTCONN;
                        goto out;
                }
                if (marker->enoent_count) {
                        op_ret = -1;
                        op_errno = ENOENT;
                }
                else {
                        op_ret = -1;
                        op_errno = EINVAL;
                }

 out:
                if (marker->xl_specf_unwind) {
                        frame->local = marker->xl_local;
                        marker->xl_specf_unwind (getxattr, frame, op_ret,
                                                 op_errno, dict);
                        return 0;
                }
                STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict);
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
        int               i;
        struct marker_str *local;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (name, err);
        VALIDATE_OR_GOTO (xl_specf_getxattr_unwind, err);

        local = GF_CALLOC (sizeof (struct marker_str), 1,
                            gf_common_mt_libxl_marker_local);

        local->xl_local = xl_local;
        frame->local = local;

        local->call_count = count;

        local->xl_specf_unwind = xl_specf_getxattr_unwind;

        local->vol_uuid = vol_uuid;

        for (i=0; i < count; i++) {
                if (MARKER_UUID_TYPE == type)
                        STACK_WIND (frame, cluster_markeruuid_cbk,
                                    *(sub_volumes + i),
                                    (*(sub_volumes + i))->fops->getxattr,
                                    loc, name);
                else if (MARKER_XTIME_TYPE == type)
                        STACK_WIND (frame, cluster_markerxtime_cbk,
                                    *(sub_volumes + i),
                                    (*(sub_volumes + i))->fops->getxattr,
                                    loc, name);
                else {
                        gf_log (this->name, GF_LOG_WARNING,
                                 "Unrecognized type of marker attr recived");
                        STACK_WIND (frame, default_getxattr_cbk,
                                    *(sub_volumes + i),
                                    (*(sub_volumes + i))->fops->getxattr,
                                    loc, name);
                        break;
                }
        }

        return 0;
err:
        return -1;

}
