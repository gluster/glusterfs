/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <unistd.h>
#include <sys/time.h>
#include <stdlib.h>
#include <fnmatch.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "afr-common.c"
#include "defaults.c"
#include "glusterfs.h"

static uint64_t pump_pid = 0;
static inline void
pump_fill_loc_info (loc_t *loc, struct iatt *iatt, struct iatt *parent)
{
        afr_update_loc_gfids (loc, iatt, parent);
        uuid_copy (loc->inode->gfid, iatt->ia_gfid);
}

static int
pump_mark_start_pending (xlator_t *this)
{
        afr_private_t  *priv      = NULL;
        pump_private_t *pump_priv = NULL;

        priv      = this->private;
        pump_priv = priv->pump_private;

        pump_priv->pump_start_pending = 1;

        return 0;
}

static int
is_pump_start_pending (xlator_t *this)
{
        afr_private_t  *priv      = NULL;
        pump_private_t *pump_priv = NULL;

        priv      = this->private;
        pump_priv = priv->pump_private;

        return (pump_priv->pump_start_pending);
}

static int
pump_remove_start_pending (xlator_t *this)
{
        afr_private_t  *priv      = NULL;
        pump_private_t *pump_priv = NULL;

        priv      = this->private;
        pump_priv = priv->pump_private;

        pump_priv->pump_start_pending = 0;

        return 0;
}

static pump_state_t
pump_get_state ()
{
        xlator_t *this = NULL;
        afr_private_t *priv = NULL;
        pump_private_t *pump_priv = NULL;

        pump_state_t ret;

        this = THIS;
        priv = this->private;
        pump_priv = priv->pump_private;

        LOCK (&pump_priv->pump_state_lock);
        {
                ret = pump_priv->pump_state;
        }
        UNLOCK (&pump_priv->pump_state_lock);

        return ret;
}

int
pump_change_state (xlator_t *this, pump_state_t state)
{
        afr_private_t *priv = NULL;
        pump_private_t *pump_priv = NULL;

        pump_state_t state_old;
        pump_state_t state_new;


        priv = this->private;
        pump_priv = priv->pump_private;

        GF_ASSERT (pump_priv);

        LOCK (&pump_priv->pump_state_lock);
        {
                state_old = pump_priv->pump_state;
                state_new = state;

                pump_priv->pump_state = state;

        }
        UNLOCK (&pump_priv->pump_state_lock);

        gf_log (this->name, GF_LOG_DEBUG,
                "Pump changing state from %d to %d",
                state_old,
                state_new);

        return  0;
}

static int
pump_set_resume_path (xlator_t *this, const char *path)
{
        int ret = 0;

        afr_private_t *priv = NULL;
        pump_private_t *pump_priv = NULL;

        priv = this->private;
        pump_priv = priv->pump_private;

        GF_ASSERT (pump_priv);

        LOCK (&pump_priv->resume_path_lock);
        {
                strncpy (pump_priv->resume_path, path, strlen (path) + 1);
        }
        UNLOCK (&pump_priv->resume_path_lock);

        return ret;
}

static int
pump_save_path (xlator_t *this, const char *path)
{
        afr_private_t *priv = NULL;
        pump_state_t state;
        dict_t *dict = NULL;
        loc_t  loc = {0};
        int dict_ret = 0;
        int ret = -1;

        state = pump_get_state ();
        if (state == PUMP_STATE_RESUME)
                return 0;

        priv = this->private;

        GF_ASSERT (priv->root_inode);

        afr_build_root_loc (this, &loc);

        dict = dict_new ();
        dict_ret = dict_set_str (dict, PUMP_PATH, (char *)path);
        if (dict_ret)
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to set the key %s", path, PUMP_PATH);

        ret = syncop_setxattr (PUMP_SOURCE_CHILD (this), &loc, dict, 0);

        if (ret < 0) {
                gf_log (this->name, GF_LOG_INFO,
                        "setxattr failed - could not save path=%s", path);
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "setxattr succeeded - saved path=%s", path);
        }

        dict_unref (dict);

        loc_wipe (&loc);
        return 0;
}

static int
pump_check_and_update_status (xlator_t *this)
{
        pump_state_t state;
        int ret = -1;

        state = pump_get_state ();

        switch (state) {

        case PUMP_STATE_RESUME:
        case PUMP_STATE_RUNNING:
        {
                ret = 0;
                break;
        }
        case PUMP_STATE_PAUSE:
        {
                ret = -1;
                break;
        }
        case PUMP_STATE_ABORT:
        {
                pump_save_path (this, "/");
                ret = -1;
                break;
        }
        default:
        {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Unknown pump state");
                ret = -1;
                break;
        }

        }

        return ret;
}

static const char *
pump_get_resume_path (xlator_t *this)
{
        afr_private_t *priv = NULL;
        pump_private_t *pump_priv = NULL;

        const char *resume_path = NULL;

        priv = this->private;
        pump_priv = priv->pump_private;

        resume_path = pump_priv->resume_path;

        return resume_path;
}

static int
pump_update_resume_state (xlator_t *this, const char *path)
{
        pump_state_t state;
        const char *resume_path = NULL;

        state = pump_get_state ();

        if (state == PUMP_STATE_RESUME) {
                resume_path = pump_get_resume_path (this);
                if (strcmp (resume_path, "/") == 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Reached the resume path (/). Proceeding to change state"
                                " to running");
                        pump_change_state (this, PUMP_STATE_RUNNING);
                } else if (strcmp (resume_path, path) == 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Reached the resume path. Proceeding to change state"
                                " to running");
                        pump_change_state (this, PUMP_STATE_RUNNING);
                } else {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Not yet hit the resume path:res-path=%s,path=%s",
                                resume_path, path);
                }
        }

        return 0;
}

static gf_boolean_t
is_pump_traversal_allowed (xlator_t *this, const char *path)
{
        pump_state_t state;
        const char *resume_path = NULL;
        gf_boolean_t ret = _gf_true;

        state = pump_get_state ();

        if (state == PUMP_STATE_RESUME) {
                resume_path = pump_get_resume_path (this);
                if (strstr (resume_path, path)) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "On the right path to resumption path");
                        ret = _gf_true;
                } else {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Not the right path to resuming=> ignoring traverse");
                        ret = _gf_false;
                }
        }

        return ret;
}

static int
pump_save_file_stats (xlator_t *this, const char *path)
{
        afr_private_t  *priv        = NULL;
        pump_private_t *pump_priv   = NULL;

        priv      = this->private;
        pump_priv = priv->pump_private;

        LOCK (&pump_priv->resume_path_lock);
        {
                pump_priv->number_files_pumped++;

                strncpy (pump_priv->current_file, path,
                         PATH_MAX);
        }
        UNLOCK (&pump_priv->resume_path_lock);

        return 0;
}

static int
gf_pump_traverse_directory (loc_t *loc)
{
        xlator_t        *this              = NULL;
        fd_t            *fd                = NULL;
        off_t           offset             = 0;
        loc_t           entry_loc          = {0};
        gf_dirent_t     *entry             = NULL;
        gf_dirent_t     *tmp               = NULL;
        gf_dirent_t     entries;
	struct iatt     iatt               = {0};
        struct iatt     parent             = {0};
	dict_t          *xattr_rsp         = NULL;
        int             ret                = 0;
        gf_boolean_t    is_directory_empty = _gf_true;
        gf_boolean_t    free_entries       = _gf_false;

        INIT_LIST_HEAD (&entries.list);
        this = THIS;

        GF_ASSERT (loc->inode);

	fd = fd_create (loc->inode, pump_pid);
        if (!fd) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to create fd for %s", loc->path);
                goto out;
        }

        ret = syncop_opendir (this, loc, fd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "opendir failed on %s", loc->path);
                goto out;
        }

        gf_log (this->name, GF_LOG_TRACE,
                "pump opendir on %s returned=%d",
                loc->path, ret);

        while (syncop_readdirp (this, fd, 131072, offset, NULL, &entries)) {
                free_entries = _gf_true;

                if (list_empty (&entries.list)) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "no more entries in directory");
                        goto out;
                }

                list_for_each_entry_safe (entry, tmp, &entries.list, list) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "found readdir entry=%s", entry->d_name);

                        offset = entry->d_off;
                        if (uuid_is_null (entry->d_stat.ia_gfid)) {
                                gf_log (this->name, GF_LOG_WARNING, "%s/%s: No "
                                        "gfid present skipping",
                                        loc->path, entry->d_name);
                                continue;
                        }
                        loc_wipe (&entry_loc);
                        ret = afr_build_child_loc (this, &entry_loc, loc,
                                                   entry->d_name);
                        if (ret)
                                goto out;

                        if (!IS_ENTRY_CWD (entry->d_name) &&
                            !IS_ENTRY_PARENT (entry->d_name)) {

                                    is_directory_empty = _gf_false;
                                    gf_log (this->name, GF_LOG_DEBUG,
                                            "lookup %s => %"PRId64,
                                            entry_loc.path,
                                            iatt.ia_ino);

                                    ret = syncop_lookup (this, &entry_loc, NULL,
                                                         &iatt, &xattr_rsp, &parent);

                                    if (ret) {
                                            gf_log (this->name, GF_LOG_ERROR,
                                                    "%s: lookup failed",
                                                    entry_loc.path);
                                            continue;
                                    }
                                    pump_fill_loc_info (&entry_loc, &iatt,
                                                       &parent);

                                    pump_update_resume_state (this, entry_loc.path);

                                    pump_save_path (this, entry_loc.path);
                                    pump_save_file_stats (this, entry_loc.path);

                                    ret = pump_check_and_update_status (this);
                                    if (ret < 0) {
                                            gf_log (this->name, GF_LOG_DEBUG,
                                                    "Pump beginning to exit out");
                                            goto out;
                                    }

                                    if (IA_ISDIR (iatt.ia_type)) {
                                            if (is_pump_traversal_allowed (this, entry_loc.path)) {
                                                    gf_log (this->name, GF_LOG_TRACE,
                                                            "entering dir=%s",
                                                            entry->d_name);
                                                    gf_pump_traverse_directory (&entry_loc);
                                            }
                                    }
                        }
                }

                gf_dirent_free (&entries);
                free_entries = _gf_false;
                gf_log (this->name, GF_LOG_TRACE,
                        "offset incremented to %d",
                        (int32_t ) offset);

        }

        ret = syncop_close (fd);
        if (ret < 0)
                gf_log (this->name, GF_LOG_DEBUG, "closing the fd failed");

        if (is_directory_empty && IS_ROOT_PATH (loc->path)) {
               pump_change_state (this, PUMP_STATE_RUNNING);
               gf_log (this->name, GF_LOG_INFO, "Empty source brick. "
                                "Nothing to be done.");
        }

out:
        if (entry_loc.path)
                loc_wipe (&entry_loc);
        if (free_entries)
                gf_dirent_free (&entries);
        return 0;
}

static int
pump_update_resume_path (xlator_t *this)
{
        const char *resume_path = NULL;

        resume_path = pump_get_resume_path (this);

        if (resume_path) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Found a path to resume from: %s",
                        resume_path);

        }else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Did not find a path=> setting to '/'");
                pump_set_resume_path (this, "/");
        }

        pump_change_state (this, PUMP_STATE_RESUME);

        return 0;
}

static int32_t
pump_xattr_cleaner (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        afr_private_t  *priv      = NULL;
        loc_t           loc       = {0};
        int             i         = 0;
        int             ret       = 0;
        int             source    = 0;
        int             sink      = 1;

        priv      = this->private;

        afr_build_root_loc (this, &loc);

        ret = syncop_removexattr (priv->children[source], &loc,
                                          PUMP_PATH);

        ret = syncop_removexattr (priv->children[sink], &loc,
                                  PUMP_SINK_COMPLETE);

        for (i = 0; i < priv->child_count; i++) {
                ret = syncop_removexattr (priv->children[i], &loc,
                                          PUMP_SOURCE_COMPLETE);
                if (ret) {
                        gf_log (this->name, GF_LOG_DEBUG, "removexattr "
                                "failed with %s", strerror (-ret));
                }
        }

        loc_wipe (&loc);
        return pump_command_reply (frame, this);
}

static int
pump_complete_migration (xlator_t *this)
{
        afr_private_t *priv = NULL;
        pump_private_t *pump_priv = NULL;
        dict_t *dict = NULL;
        pump_state_t state;
        loc_t  loc = {0};
        int dict_ret = 0;
        int ret = -1;

        priv = this->private;
        pump_priv = priv->pump_private;

        GF_ASSERT (priv->root_inode);

        afr_build_root_loc (this, &loc);

        dict = dict_new ();

        state = pump_get_state ();
        if (state == PUMP_STATE_RUNNING) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Pump finished pumping");

                pump_priv->pump_finished = _gf_true;

                dict_ret = dict_set_str (dict, PUMP_SOURCE_COMPLETE, "jargon");
                if (dict_ret)
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s: failed to set the key %s",
                                loc.path, PUMP_SOURCE_COMPLETE);

                ret = syncop_setxattr (PUMP_SOURCE_CHILD (this), &loc, dict, 0);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "setxattr failed - while  notifying source complete");
                }
                dict_ret = dict_set_str (dict, PUMP_SINK_COMPLETE, "jargon");
                if (dict_ret)
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s: failed to set the key %s",
                                loc.path, PUMP_SINK_COMPLETE);

                ret = syncop_setxattr (PUMP_SINK_CHILD (this), &loc, dict, 0);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "setxattr failed - while notifying sink complete");
                }

                pump_save_path (this, "/");

        } else if (state == PUMP_STATE_ABORT) {
                gf_log (this->name, GF_LOG_DEBUG, "Starting cleanup "
                        "of pump internal xattrs");
                call_resume (pump_priv->cleaner);
        }

        loc_wipe (&loc);
        return 0;
}

static int
pump_lookup_sink (loc_t *loc)
{
        xlator_t *this = NULL;
	struct iatt iatt, parent;
	dict_t *xattr_rsp;
        dict_t *xattr_req = NULL;
        int ret = 0;

        this = THIS;

        xattr_req = dict_new ();

        ret = afr_set_root_gfid (xattr_req);
        if (ret)
                goto out;

        ret = syncop_lookup (PUMP_SINK_CHILD (this), loc,
                             xattr_req, &iatt, &xattr_rsp, &parent);

        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Lookup on sink child failed");
                ret = -1;
                goto out;
        }

out:
        if (xattr_req)
                dict_unref (xattr_req);

        return ret;
}

static int
pump_task (void *data)
{
	xlator_t *this = NULL;
        afr_private_t *priv = NULL;


        loc_t loc = {0};
	struct iatt iatt, parent;
	dict_t *xattr_rsp = NULL;
        dict_t *xattr_req = NULL;

        int ret = -1;

        this = THIS;
        priv = this->private;

        GF_ASSERT (priv->root_inode);

        afr_build_root_loc (this, &loc);
        xattr_req = dict_new ();
        if (!xattr_req) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Out of memory");
                ret = -1;
                goto out;
        }

        afr_set_root_gfid (xattr_req);
        ret = syncop_lookup (this, &loc, xattr_req,
                             &iatt, &xattr_rsp, &parent);

        gf_log (this->name, GF_LOG_TRACE,
                "lookup: path=%s gfid=%s",
                loc.path, uuid_utoa (loc.inode->gfid));

        ret = pump_check_and_update_status (this);
        if (ret < 0) {
                goto out;
        }

        pump_update_resume_path (this);

        afr_set_root_gfid (xattr_req);
        ret = pump_lookup_sink (&loc);
        if (ret) {
                pump_update_resume_path (this);
                goto out;
        }

        gf_pump_traverse_directory (&loc);

        pump_complete_migration (this);
out:
        if (xattr_req)
                dict_unref (xattr_req);

        loc_wipe (&loc);
	return 0;
}


static int
pump_task_completion (int ret, call_frame_t *sync_frame, void *data)
{
        xlator_t *this = NULL;
        afr_private_t *priv = NULL;

        this = THIS;

        priv = this->private;

        inode_unref (priv->root_inode);
        STACK_DESTROY (sync_frame->root);

        gf_log (this->name, GF_LOG_DEBUG,
                "Pump xlator exiting");
	return 0;
}

int
pump_start (call_frame_t *pump_frame, xlator_t *this)
{
	afr_private_t *priv = NULL;
	pump_private_t *pump_priv = NULL;

	int ret = -1;

	priv = this->private;
        pump_priv = priv->pump_private;

        afr_set_lk_owner (pump_frame, this, pump_frame->root);
	pump_pid = (uint64_t) (unsigned long)pump_frame->root;

	ret = synctask_new (pump_priv->env, pump_task,
                            pump_task_completion,
                            pump_frame, NULL);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "starting pump failed");
                pump_change_state (this, PUMP_STATE_ABORT);
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "setting pump as started lk_owner: %s %"PRIu64,
                lkowner_utoa (&pump_frame->root->lk_owner), pump_pid);

        priv->use_afr_in_pump = 1;
out:
	return ret;
}

static int
pump_start_synctask (xlator_t *this)
{
        call_frame_t *frame = NULL;
        int ret = 0;

        frame = create_frame (this, this->ctx->pool);
        if (!frame) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory");
                ret = -1;
                goto out;
        }

        pump_change_state (this, PUMP_STATE_RUNNING);

        ret = pump_start (frame, this);

out:
        return ret;
}

int32_t
pump_cmd_start_setxattr_cbk (call_frame_t *frame,
                             void *cookie,
                             xlator_t *this,
                             int32_t op_ret,
                             int32_t op_errno, dict_t *xdata)

{
        call_frame_t *prev = NULL;
        afr_local_t *local = NULL;
        int ret = 0;

        local = frame->local;

        if (op_ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Could not initiate destination "
                        "brick connect");
                ret = op_ret;
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "Successfully initiated destination "
                "brick connect");

        pump_mark_start_pending (this);

        /* send the PARENT_UP as pump is ready now */
        prev = cookie;
        if (prev && prev->this)
                prev->this->notify (prev->this, GF_EVENT_PARENT_UP, this);

out:
        local->op_ret = ret;
        pump_command_reply (frame, this);

        return 0;
}

static int
pump_initiate_sink_connect (call_frame_t *frame, xlator_t *this)
{
        afr_local_t   *local     = NULL;
        afr_private_t *priv      = NULL;
        dict_t        *dict      = NULL;
        data_t        *data      = NULL;
        char          *clnt_cmd  = NULL;
        loc_t loc = {0};

        int ret = 0;

        priv  = this->private;
        local = frame->local;

        GF_ASSERT (priv->root_inode);

        afr_build_root_loc (this, &loc);

        data = data_ref (dict_get (local->dict, RB_PUMP_CMD_START));
        if (!data) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR,
                        "Could not get destination brick value");
                goto out;
        }

        dict = dict_new ();
        if (!dict) {
                ret = -1;
                goto out;
        }

        clnt_cmd = GF_CALLOC (1, data->len+1, gf_common_mt_char);
        if (!clnt_cmd) {
                ret = -1;
                goto out;
        }

        memcpy (clnt_cmd, data->data, data->len);
        clnt_cmd[data->len] = '\0';
        gf_log (this->name, GF_LOG_DEBUG, "Got destination brick %s\n",
                        clnt_cmd);

        ret = dict_set_dynstr (dict, CLIENT_CMD_CONNECT, clnt_cmd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Could not inititiate destination brick "
                        "connect");
                goto out;
        }

	STACK_WIND (frame,
		    pump_cmd_start_setxattr_cbk,
		    PUMP_SINK_CHILD(this),
		    PUMP_SINK_CHILD(this)->fops->setxattr,
		    &loc,
		    dict,
		    0, NULL);

        ret = 0;

out:
        if (dict)
                dict_unref (dict);

        if (data)
                data_unref (data);

        if (ret && clnt_cmd)
                GF_FREE (clnt_cmd);

        loc_wipe (&loc);
        return ret;
}

static int
is_pump_aborted (xlator_t *this)
{
        pump_state_t state;

        state = pump_get_state ();

        return ((state == PUMP_STATE_ABORT));
}

int32_t
pump_cmd_start_getxattr_cbk (call_frame_t *frame,
                             void *cookie,
                             xlator_t *this,
                             int32_t op_ret,
                             int32_t op_errno,
                             dict_t *dict, dict_t *xdata)
{
        afr_local_t *local = NULL;
        char *path = NULL;

        pump_state_t state;
        int ret = 0;
        int need_unwind = 0;
        int dict_ret = -1;

        local = frame->local;

        if (op_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "getxattr failed - changing pump "
                        "state to RUNNING with '/'");
                path = "/";
                ret = op_ret;
        } else {
                gf_log (this->name, GF_LOG_TRACE,
                        "getxattr succeeded");

                dict_ret =  dict_get_str (dict, PUMP_PATH, &path);
                if (dict_ret < 0)
                        path = "/";
        }

        state = pump_get_state ();
        if ((state == PUMP_STATE_RUNNING) ||
            (state == PUMP_STATE_RESUME)) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Pump is already started");
                ret = -1;
                goto out;
        }

        pump_set_resume_path (this, path);

        if (is_pump_aborted (this))
                /* We're re-starting pump afresh */
                ret = pump_initiate_sink_connect (frame, this);
        else {
                /* We're re-starting pump from a previous
                   pause */
                gf_log (this->name, GF_LOG_DEBUG,
                        "about to start synctask");
                ret = pump_start_synctask (this);
                need_unwind = 1;
        }

out:
        if ((ret < 0) || (need_unwind == 1)) {
                local->op_ret = ret;
                pump_command_reply (frame, this);
        }
	return 0;
}

int
pump_execute_status (call_frame_t *frame, xlator_t *this)
{
        afr_private_t *priv = NULL;
        pump_private_t *pump_priv = NULL;

        uint64_t number_files = 0;

        char filename[PATH_MAX];
        char summary[PATH_MAX+256];
        char *dict_str = NULL;

        int32_t op_ret = 0;
        int32_t op_errno = 0;

        dict_t *dict = NULL;
        int ret = -1;

        priv = this->private;
        pump_priv = priv->pump_private;

        LOCK (&pump_priv->resume_path_lock);
        {
                number_files  = pump_priv->number_files_pumped;
                strncpy (filename, pump_priv->current_file, PATH_MAX);
        }
        UNLOCK (&pump_priv->resume_path_lock);

        dict_str     = GF_CALLOC (1, PATH_MAX + 256, gf_afr_mt_char);
        if (!dict_str) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory");
                op_ret = -1;
                op_errno = ENOMEM;
                goto out;
        }

        if (pump_priv->pump_finished) {
                snprintf (summary, PATH_MAX+256,
                          "no_of_files=%"PRIu64, number_files);
        } else {
                snprintf (summary, PATH_MAX+256,
                          "no_of_files=%"PRIu64":current_file=%s",
                          number_files, filename);
        }
        snprintf (dict_str, PATH_MAX+256, "status=%d:%s",
                  (pump_priv->pump_finished)?1:0, summary);

        dict = dict_new ();

        ret = dict_set_dynstr (dict, RB_PUMP_CMD_STATUS, dict_str);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "dict_set_dynstr returned negative value");
        } else {
                dict_str = NULL;
        }

        op_ret = 0;

out:

        AFR_STACK_UNWIND (getxattr, frame, op_ret, op_errno, dict, NULL);

        if (dict)
                dict_unref (dict);

        GF_FREE (dict_str);

        return 0;
}

int
pump_execute_pause (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;

        local = frame->local;

        pump_change_state (this, PUMP_STATE_PAUSE);

        local->op_ret = 0;
        pump_command_reply (frame, this);

        return 0;
}

int
pump_execute_start (call_frame_t *frame, xlator_t *this)
{
        afr_private_t *priv = NULL;
        afr_local_t   *local = NULL;

        int ret = 0;
        loc_t loc = {0};

        priv = this->private;
        local = frame->local;

        if (!priv->root_inode) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Pump xlator cannot be started without an initial "
                        "lookup");
                ret = -1;
                goto out;
        }

        GF_ASSERT (priv->root_inode);

        afr_build_root_loc (this, &loc);

	STACK_WIND (frame,
		    pump_cmd_start_getxattr_cbk,
		    PUMP_SOURCE_CHILD(this),
		    PUMP_SOURCE_CHILD(this)->fops->getxattr,
		    &loc,
		    PUMP_PATH, NULL);

        ret = 0;

out:
        if (ret < 0) {
                local->op_ret = ret;
                pump_command_reply (frame, this);
        }

        loc_wipe (&loc);
	return 0;
}

static int
pump_cleanup_helper (void *data) {
        call_frame_t *frame = data;

        pump_xattr_cleaner (frame, 0, frame->this, 0, 0, NULL);

        return 0;
}

static int
pump_cleanup_done (int ret, call_frame_t *sync_frame, void *data)
{
        STACK_DESTROY (sync_frame->root);

        return 0;
}

int
pump_execute_commit (call_frame_t *frame, xlator_t *this)
{
        afr_private_t  *priv       = NULL;
        pump_private_t *pump_priv  = NULL;
        afr_local_t    *local      = NULL;
        call_frame_t   *sync_frame = NULL;
        int             ret        = 0;

        priv      = this->private;
        pump_priv = priv->pump_private;
        local     = frame->local;

        local->op_ret = 0;
        if (pump_priv->pump_finished) {
                pump_change_state (this, PUMP_STATE_COMMIT);
                sync_frame = create_frame (this, this->ctx->pool);
                ret = synctask_new (pump_priv->env, pump_cleanup_helper,
                                    pump_cleanup_done, sync_frame, frame);
                if (ret) {
                        gf_log (this->name, GF_LOG_DEBUG, "Couldn't create "
                                "synctask for cleaning up xattrs.");
                }

        } else {
                gf_log (this->name, GF_LOG_ERROR, "Commit can't proceed. "
                        "Migration in progress");
                local->op_ret = -1;
                local->op_errno = EINPROGRESS;
                pump_command_reply (frame, this);
        }

        return 0;
}
int
pump_execute_abort (call_frame_t *frame, xlator_t *this)
{
        afr_private_t  *priv       = NULL;
        pump_private_t *pump_priv  = NULL;
        afr_local_t    *local      = NULL;
        call_frame_t   *sync_frame = NULL;
        int             ret        = 0;

        priv      = this->private;
        pump_priv = priv->pump_private;
        local     = frame->local;

        pump_change_state (this, PUMP_STATE_ABORT);

        LOCK (&pump_priv->resume_path_lock);
        {
                pump_priv->number_files_pumped = 0;
                pump_priv->current_file[0] = '\0';
        }
        UNLOCK (&pump_priv->resume_path_lock);

        local->op_ret = 0;
        if (pump_priv->pump_finished) {
                sync_frame = create_frame (this, this->ctx->pool);
                ret = synctask_new (pump_priv->env, pump_cleanup_helper,
                                    pump_cleanup_done, sync_frame, frame);
                if (ret) {
                        gf_log (this->name, GF_LOG_DEBUG, "Couldn't create "
                                "synctask for cleaning up xattrs.");
                }

        } else {
                pump_priv->cleaner = fop_setxattr_cbk_stub (frame,
                                                            pump_xattr_cleaner,
                                                            0, 0, NULL);
        }

        return 0;
}

gf_boolean_t
pump_command_status (xlator_t *this, dict_t *dict)
{
        char *cmd = NULL;
        int dict_ret = -1;
        int ret = _gf_true;

        dict_ret = dict_get_str (dict, RB_PUMP_CMD_STATUS, &cmd);
        if (dict_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Not a pump status command");
                ret = _gf_false;
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "Hit a pump command - status");
        ret = _gf_true;

out:
        return ret;

}

gf_boolean_t
pump_command_pause (xlator_t *this, dict_t *dict)
{
        char *cmd = NULL;
        int dict_ret = -1;
        int ret = _gf_true;

        dict_ret = dict_get_str (dict, RB_PUMP_CMD_PAUSE, &cmd);
        if (dict_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Not a pump pause command");
                ret = _gf_false;
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "Hit a pump command - pause");
        ret = _gf_true;

out:
        return ret;

}

gf_boolean_t
pump_command_commit (xlator_t *this, dict_t *dict)
{
        char *cmd = NULL;
        int dict_ret = -1;
        int ret = _gf_true;

        dict_ret = dict_get_str (dict, RB_PUMP_CMD_COMMIT, &cmd);
        if (dict_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Not a pump commit command");
                ret = _gf_false;
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "Hit a pump command - commit");
        ret = _gf_true;

out:
        return ret;

}

gf_boolean_t
pump_command_abort (xlator_t *this, dict_t *dict)
{
        char *cmd = NULL;
        int dict_ret = -1;
        int ret = _gf_true;

        dict_ret = dict_get_str (dict, RB_PUMP_CMD_ABORT, &cmd);
        if (dict_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Not a pump abort command");
                ret = _gf_false;
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "Hit a pump command - abort");
        ret = _gf_true;

out:
        return ret;

}

gf_boolean_t
pump_command_start (xlator_t *this, dict_t *dict)
{
        char *cmd = NULL;
        int dict_ret = -1;
        int ret = _gf_true;

        dict_ret = dict_get_str (dict, RB_PUMP_CMD_START, &cmd);
        if (dict_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Not a pump start command");
                ret = _gf_false;
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "Hit a pump command - start");
        ret = _gf_true;

out:
        return ret;

}

struct _xattr_key {
        char *key;
        struct list_head list;
};

static int
__gather_xattr_keys (dict_t *dict, char *key, data_t *value,
                     void *data)
{
        struct list_head *  list  = data;
        struct _xattr_key * xkey  = NULL;

        if (!strncmp (key, AFR_XATTR_PREFIX,
                      strlen (AFR_XATTR_PREFIX))) {

                xkey = GF_CALLOC (1, sizeof (*xkey), gf_afr_mt_xattr_key);
                if (!xkey)
                        return -1;

                xkey->key = key;
                INIT_LIST_HEAD (&xkey->list);

                list_add_tail (&xkey->list, list);
        }
        return 0;
}

static void
__filter_xattrs (dict_t *dict)
{
        struct list_head keys;

        struct _xattr_key *key;
        struct _xattr_key *tmp;

        INIT_LIST_HEAD (&keys);

        dict_foreach (dict, __gather_xattr_keys,
                      (void *) &keys);

        list_for_each_entry_safe (key, tmp, &keys, list) {
                dict_del (dict, key->key);

                list_del_init (&key->list);

                GF_FREE (key);
        }
}

int32_t
pump_getxattr_cbk (call_frame_t *frame, void *cookie,
		  xlator_t *this, int32_t op_ret, int32_t op_errno,
		  dict_t *dict, dict_t *xdata)
{
	afr_private_t   *priv           = NULL;
	afr_local_t     *local          = NULL;
	xlator_t        **children      = NULL;
	int             unwind          = 1;
        int32_t         *last_index     = NULL;
        int32_t         next_call_child = -1;
        int32_t         read_child      = -1;
        int32_t         *fresh_children = NULL;


	priv     = this->private;
	children = priv->children;

	local = frame->local;

        read_child = (long) cookie;

	if (op_ret == -1) {
		last_index = &local->cont.getxattr.last_index;
                fresh_children = local->fresh_children;
                next_call_child = afr_next_call_child (fresh_children,
                                                       local->child_up,
                                                       priv->child_count,
                                                       last_index, read_child);
                if (next_call_child < 0)
                        goto out;

		unwind = 0;
		STACK_WIND_COOKIE (frame, pump_getxattr_cbk,
				   (void *) (long) read_child,
				   children[next_call_child],
				   children[next_call_child]->fops->getxattr,
				   &local->loc,
				   local->cont.getxattr.name, NULL);
	}

out:
	if (unwind) {
                if (op_ret >= 0 && dict)
                        __filter_xattrs (dict);

		AFR_STACK_UNWIND (getxattr, frame, op_ret, op_errno, dict, NULL);
	}

	return 0;
}

int32_t
pump_getxattr (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, const char *name, dict_t *xdata)
{
	afr_private_t *   priv       = NULL;
	xlator_t **       children   = NULL;
	int               call_child = 0;
	afr_local_t       *local     = NULL;
	int32_t           ret     = -1;
	int32_t           op_errno   = 0;
        uint64_t          read_child = 0;


	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv     = this->private;
	VALIDATE_OR_GOTO (priv->children, out);

	children = priv->children;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame, default_getxattr_cbk,
                            FIRST_CHILD (this),
                            (FIRST_CHILD (this))->fops->getxattr,
                            loc, name, xdata);
                return 0;
        }


	AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
	local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        if (name) {
                if (!strncmp (name, AFR_XATTR_PREFIX,
                              strlen (AFR_XATTR_PREFIX))) {

                        op_errno = ENODATA;
                        goto out;
                }

                if (!strcmp (name, RB_PUMP_CMD_STATUS)) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Hit pump command - status");
                        pump_execute_status (frame, this);
                        ret = 0;
                        goto out;
                }
        }

        local->fresh_children = GF_CALLOC (priv->child_count,
                                          sizeof (*local->fresh_children),
                                          gf_afr_mt_int32_t);
        if (!local->fresh_children) {
                ret = -1;
                op_errno = ENOMEM;
                goto out;
        }

        read_child = afr_inode_get_read_ctx (this, loc->inode, local->fresh_children);
        ret = afr_get_call_child (this, local->child_up, read_child,
                                     local->fresh_children,
                                     &call_child,
                                     &local->cont.getxattr.last_index);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }
	loc_copy (&local->loc, loc);
	if (name)
	  local->cont.getxattr.name       = gf_strdup (name);

	STACK_WIND_COOKIE (frame, pump_getxattr_cbk,
			   (void *) (long) call_child,
			   children[call_child], children[call_child]->fops->getxattr,
			   loc, name, xdata);

	ret = 0;
out:
	if (ret < 0)
		AFR_STACK_UNWIND (getxattr, frame, -1, op_errno, NULL, NULL);
	return 0;
}

static int
afr_setxattr_unwind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *   local = NULL;
	call_frame_t   *main_frame = NULL;

	local = frame->local;

	LOCK (&frame->lock);
	{
		if (local->transaction.main_frame)
			main_frame = local->transaction.main_frame;
		local->transaction.main_frame = NULL;
	}
	UNLOCK (&frame->lock);

	if (main_frame) {
		AFR_STACK_UNWIND (setxattr, main_frame,
                                  local->op_ret, local->op_errno, NULL);
	}
	return 0;
}

static int
afr_setxattr_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count  = -1;
	int need_unwind = 0;

	local = frame->local;
	priv = this->private;

	LOCK (&frame->lock);
	{
		if (op_ret != -1) {
			if (local->success_count == 0) {
				local->op_ret = op_ret;
			}
			local->success_count++;

			if (local->success_count == priv->child_count) {
				need_unwind = 1;
			}
		}

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	if (need_unwind)
		local->transaction.unwind (frame, this);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
	}

	return 0;
}

static int
afr_setxattr_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;

	int call_count = -1;
	int i = 0;

	local = frame->local;
	priv = this->private;

	call_count = afr_up_children_count (local->child_up, priv->child_count);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
		return 0;
	}

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_setxattr_wind_cbk,
					   (void *) (long) i,
					   priv->children[i],
					   priv->children[i]->fops->setxattr,
					   &local->loc,
					   local->cont.setxattr.dict,
					   local->cont.setxattr.flags, NULL);

			if (!--call_count)
				break;
		}
	}

	return 0;
}


static int
afr_setxattr_done (call_frame_t *frame, xlator_t *this)
{
	afr_local_t * local = frame->local;

	local->transaction.unwind (frame, this);

	AFR_STACK_DESTROY (frame);

	return 0;
}

int32_t
pump_setxattr_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno, dict_t *xdata)
{
	AFR_STACK_UNWIND (setxattr, frame, op_ret, op_errno, xdata);
	return 0;
}

int
pump_command_reply (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;

        local = frame->local;

        if (local->op_ret < 0)
                gf_log (this->name, GF_LOG_INFO,
                        "Command failed");
        else
                gf_log (this->name, GF_LOG_INFO,
                        "Command succeeded");

        AFR_STACK_UNWIND (setxattr,
                          frame,
                          local->op_ret,
                          local->op_errno, NULL);

        return 0;
}

int
pump_parse_command (call_frame_t *frame, xlator_t *this,
                    afr_local_t *local, dict_t *dict)
{

        int ret = -1;

        if (pump_command_start (this, dict)) {
                frame->local = local;
                local->dict = dict_ref (dict);
                ret = pump_execute_start (frame, this);

        } else if (pump_command_pause (this, dict)) {
                frame->local = local;
                local->dict = dict_ref (dict);
                ret = pump_execute_pause (frame, this);

        } else if (pump_command_abort (this, dict)) {
                frame->local = local;
                local->dict = dict_ref (dict);
                ret = pump_execute_abort (frame, this);

        } else if (pump_command_commit (this, dict)) {
                frame->local = local;
                local->dict = dict_ref (dict);
                ret = pump_execute_commit (frame, this);
        }
        return ret;
}

int
pump_setxattr (call_frame_t *frame, xlator_t *this,
               loc_t *loc, dict_t *dict, int32_t flags, dict_t *xdata)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	call_frame_t   *transaction_frame = NULL;
	int ret = -1;
	int op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

        GF_IF_INTERNAL_XATTR_GOTO ("trusted.glusterfs.pump*", dict,
                                   op_errno, out);

	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame, default_setxattr_cbk,
                            FIRST_CHILD (this),
                            (FIRST_CHILD (this))->fops->setxattr,
                            loc, dict, flags, xdata);
                return 0;
        }


	AFR_LOCAL_ALLOC_OR_GOTO (local, out);

	ret = afr_local_init (local, priv, &op_errno);
	if (ret < 0) {
                afr_local_cleanup (local, this);
		goto out;
        }

        ret = pump_parse_command (frame, this,
                                  local, dict);
        if (ret >= 0) {
                ret = 0;
                goto out;
        }

	transaction_frame = copy_frame (frame);
	if (!transaction_frame) {
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory.");
                op_errno = ENOMEM;
                ret = -1;
                afr_local_cleanup (local, this);
		goto out;
	}

	transaction_frame->local = local;

	local->op_ret = -1;

	local->cont.setxattr.dict  = dict_ref (dict);
	local->cont.setxattr.flags = flags;

	local->transaction.fop    = afr_setxattr_wind;
	local->transaction.done   = afr_setxattr_done;
	local->transaction.unwind = afr_setxattr_unwind;

	loc_copy (&local->loc, loc);

	local->transaction.main_frame = frame;
	local->transaction.start   = LLONG_MAX - 1;
	local->transaction.len     = 0;

	afr_transaction (transaction_frame, this, AFR_METADATA_TRANSACTION);

	ret = 0;
out:
	if (ret < 0) {
		if (transaction_frame)
			AFR_STACK_DESTROY (transaction_frame);
		AFR_STACK_UNWIND (setxattr, frame, -1, op_errno, NULL);
	}

	return 0;
}

/* Defaults */
static int32_t
pump_lookup (call_frame_t *frame,
             xlator_t *this,
             loc_t *loc,
             dict_t *xattr_req)
{
	afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_lookup_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup,
                            loc,
                            xattr_req);
                return 0;
        }

        afr_lookup (frame, this, loc, xattr_req);
        return 0;
}


static int32_t
pump_truncate (call_frame_t *frame,
               xlator_t *this,
               loc_t *loc,
               off_t offset, dict_t *xdata)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_truncate_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->truncate,
                            loc,
                            offset, xdata);
                return 0;
        }

        afr_truncate (frame, this, loc, offset, xdata);
        return 0;
}


static int32_t
pump_ftruncate (call_frame_t *frame,
                xlator_t *this,
                fd_t *fd,
                off_t offset, dict_t *xdata)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_ftruncate_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->ftruncate,
                            fd,
                            offset, xdata);
                return 0;
        }

        afr_ftruncate (frame, this, fd, offset, xdata);
        return 0;
}




int
pump_mknod (call_frame_t *frame, xlator_t *this,
            loc_t *loc, mode_t mode, dev_t rdev, mode_t umask, dict_t *xdata)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame, default_mknod_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->mknod,
                            loc, mode, rdev, umask, xdata);
                return 0;
        }
        afr_mknod (frame, this, loc, mode, rdev, umask, xdata);
        return 0;

}



int
pump_mkdir (call_frame_t *frame, xlator_t *this,
            loc_t *loc, mode_t mode, mode_t umask, dict_t *xdata)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame, default_mkdir_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->mkdir,
                            loc, mode, umask, xdata);
                return 0;
        }
        afr_mkdir (frame, this, loc, mode, umask, xdata);
        return 0;

}


static int32_t
pump_unlink (call_frame_t *frame,
             xlator_t *this,
             loc_t *loc, int xflag, dict_t *xdata)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_unlink_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->unlink,
                            loc, xflag, xdata);
                return 0;
        }
        afr_unlink (frame, this, loc, xflag, xdata);
        return 0;

}


static int
pump_rmdir (call_frame_t *frame, xlator_t *this,
            loc_t *loc, int flags, dict_t *xdata)
{
        afr_private_t *priv  = NULL;

	priv = this->private;

        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame, default_rmdir_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->rmdir,
                            loc, flags, xdata);
                return 0;
        }

        afr_rmdir (frame, this, loc, flags, xdata);
        return 0;

}



int
pump_symlink (call_frame_t *frame, xlator_t *this,
              const char *linkpath, loc_t *loc, mode_t umask, dict_t *xdata)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame, default_symlink_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->symlink,
                            linkpath, loc, umask, xdata);
                return 0;
        }
        afr_symlink (frame, this, linkpath, loc, umask, xdata);
        return 0;

}


static int32_t
pump_rename (call_frame_t *frame,
             xlator_t *this,
             loc_t *oldloc,
             loc_t *newloc, dict_t *xdata)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_rename_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->rename,
                            oldloc, newloc, xdata);
                return 0;
        }
        afr_rename (frame, this, oldloc, newloc, xdata);
        return 0;

}


static int32_t
pump_link (call_frame_t *frame,
           xlator_t *this,
           loc_t *oldloc,
           loc_t *newloc, dict_t *xdata)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_link_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->link,
                            oldloc, newloc, xdata);
                return 0;
        }
        afr_link (frame, this, oldloc, newloc, xdata);
        return 0;

}


static int32_t
pump_create (call_frame_t *frame, xlator_t *this,
             loc_t *loc, int32_t flags, mode_t mode,
             mode_t umask, fd_t *fd, dict_t *xdata)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame, default_create_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->create,
                            loc, flags, mode, umask, fd, xdata);
                return 0;
        }
        afr_create (frame, this, loc, flags, mode, umask, fd, xdata);
        return 0;

}


static int32_t
pump_open (call_frame_t *frame,
           xlator_t *this,
           loc_t *loc,
           int32_t flags, fd_t *fd, dict_t *xdata)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_open_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open,
                            loc, flags, fd, xdata);
                return 0;
        }
        afr_open (frame, this, loc, flags, fd, xdata);
        return 0;

}


static int32_t
pump_writev (call_frame_t *frame,
             xlator_t *this,
             fd_t *fd,
             struct iovec *vector,
             int32_t count,
             off_t off, uint32_t flags,
             struct iobref *iobref, dict_t *xdata)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_writev_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->writev,
                            fd,
                            vector,
                            count,
                            off, flags,
                            iobref, xdata);
                return 0;
        }

        afr_writev (frame, this, fd, vector, count, off, flags, iobref, xdata);
        return 0;
}


static int32_t
pump_flush (call_frame_t *frame,
            xlator_t *this,
            fd_t *fd, dict_t *xdata)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_flush_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->flush,
                            fd, xdata);
                return 0;
        }
        afr_flush (frame, this, fd, xdata);
        return 0;

}


static int32_t
pump_fsync (call_frame_t *frame,
            xlator_t *this,
            fd_t *fd,
            int32_t flags, dict_t *xdata)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_fsync_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fsync,
                            fd,
                            flags, xdata);
                return 0;
        }
        afr_fsync (frame, this, fd, flags, xdata);
        return 0;

}


static int32_t
pump_opendir (call_frame_t *frame,
              xlator_t *this,
              loc_t *loc, fd_t *fd, dict_t *xdata)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_opendir_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->opendir,
                            loc, fd, xdata);
                return 0;
        }
        afr_opendir (frame, this, loc, fd, xdata);
        return 0;

}


static int32_t
pump_fsyncdir (call_frame_t *frame,
               xlator_t *this,
               fd_t *fd,
               int32_t flags, dict_t *xdata)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_fsyncdir_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fsyncdir,
                            fd,
                            flags, xdata);
                return 0;
        }
        afr_fsyncdir (frame, this, fd, flags, xdata);
        return 0;

}


static int32_t
pump_xattrop (call_frame_t *frame,
              xlator_t *this,
              loc_t *loc,
              gf_xattrop_flags_t flags,
              dict_t *dict, dict_t *xdata)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_xattrop_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->xattrop,
                            loc,
                            flags,
                            dict, xdata);
                return 0;
        }
        afr_xattrop (frame, this, loc, flags, dict, xdata);
        return 0;

}

static int32_t
pump_fxattrop (call_frame_t *frame,
               xlator_t *this,
               fd_t *fd,
               gf_xattrop_flags_t flags,
               dict_t *dict, dict_t *xdata)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_fxattrop_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fxattrop,
                            fd,
                            flags,
                            dict, xdata);
                return 0;
        }
        afr_fxattrop (frame, this, fd, flags, dict, xdata);
        return 0;

}


static int32_t
pump_removexattr (call_frame_t *frame,
                  xlator_t *this,
                  loc_t *loc,
                  const char *name, dict_t *xdata)
{
        afr_private_t *priv     = NULL;
        int            op_errno = -1;

        VALIDATE_OR_GOTO (this, out);

        GF_IF_NATIVE_XATTR_GOTO ("trusted.glusterfs.pump*",
                                 name, op_errno, out);

        op_errno = 0;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_removexattr_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->removexattr,
                            loc,
                            name, xdata);
                return 0;
        }
        afr_removexattr (frame, this, loc, name, xdata);

 out:
        if (op_errno)
                AFR_STACK_UNWIND (removexattr, frame, -1, op_errno, NULL);
        return 0;

}



static int32_t
pump_readdir (call_frame_t *frame,
              xlator_t *this,
              fd_t *fd,
              size_t size,
              off_t off, dict_t *xdata)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_readdir_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readdir,
                            fd, size, off, xdata);
                return 0;
        }
        afr_readdir (frame, this, fd, size, off, xdata);
        return 0;

}


static int32_t
pump_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd,
               size_t size, off_t off, dict_t *dict)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_readdirp_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readdirp,
                            fd, size, off, dict);
                return 0;
        }
        afr_readdirp (frame, this, fd, size, off, dict);
        return 0;

}



static int32_t
pump_releasedir (xlator_t *this,
                 fd_t *fd)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (priv->use_afr_in_pump)
                afr_releasedir (this, fd);
	return 0;

}

static int32_t
pump_release (xlator_t *this,
              fd_t *fd)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (priv->use_afr_in_pump)
                afr_release (this, fd);
	return 0;

}

static int32_t
pump_forget (xlator_t *this, inode_t *inode)
{
        afr_private_t  *priv  = NULL;

        priv = this->private;
        if (priv->use_afr_in_pump)
                afr_forget (this, inode);

        return 0;
}

static int32_t
pump_setattr (call_frame_t *frame,
              xlator_t *this,
              loc_t *loc,
              struct iatt *stbuf,
              int32_t valid, dict_t *xdata)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_setattr_cbk,
                            FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->setattr,
                            loc, stbuf, valid, xdata);
                return 0;
        }
        afr_setattr (frame, this, loc, stbuf, valid, xdata);
        return 0;

}


static int32_t
pump_fsetattr (call_frame_t *frame,
               xlator_t *this,
               fd_t *fd,
               struct iatt *stbuf,
               int32_t valid, dict_t *xdata)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_fsetattr_cbk,
                            FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fsetattr,
                            fd, stbuf, valid, xdata);
                return 0;
        }
        afr_fsetattr (frame, this, fd, stbuf, valid, xdata);
        return 0;

}


/* End of defaults */


int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_afr_mt_end + 1);

        if (ret != 0) {
                gf_log(this->name, GF_LOG_ERROR, "Memory accounting init"
                                "failed");
                return ret;
        }

        return ret;
}

static int
is_xlator_pump_sink (xlator_t *child)
{
        return (child == PUMP_SINK_CHILD(THIS));
}

static int
is_xlator_pump_source (xlator_t *child)
{
        return (child == PUMP_SOURCE_CHILD(THIS));
}

int32_t
notify (xlator_t *this, int32_t event,
	void *data, ...)
{
        int ret = -1;
        xlator_t *child_xl = NULL;

        child_xl = (xlator_t *) data;

        ret = afr_notify (this, event, data, NULL);

	switch (event) {
	case GF_EVENT_CHILD_DOWN:
                if (is_xlator_pump_source (child_xl))
                        pump_change_state (this, PUMP_STATE_ABORT);
                break;

        case GF_EVENT_CHILD_UP:
                if (is_xlator_pump_sink (child_xl))
                        if (is_pump_start_pending (this)) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "about to start synctask");
                                ret = pump_start_synctask (this);
                                if (ret < 0)
                                        gf_log (this->name, GF_LOG_DEBUG,
                                                "Could not start pump "
                                                "synctask");
                                else
                                        pump_remove_start_pending (this);
                        }
        }

        return ret;
}

int32_t
init (xlator_t *this)
{
	afr_private_t * priv        = NULL;
        pump_private_t *pump_priv = NULL;
	int             child_count = 0;
	xlator_list_t * trav        = NULL;
	int             i           = 0;
	int             ret         = -1;
	GF_UNUSED int   op_errno    = 0;

        int source_child = 0;

	if (!this->children) {
		gf_log (this->name, GF_LOG_ERROR,
			"pump translator needs a source and sink"
                        "subvolumes defined.");
		return -1;
	}

	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"Volume is dangling.");
	}

	this->private = GF_CALLOC (1, sizeof (afr_private_t),
                                   gf_afr_mt_afr_private_t);
        if (!this->private)
                goto out;

	priv = this->private;
        LOCK_INIT (&priv->lock);
        LOCK_INIT (&priv->read_child_lock);
        //lock recovery is not done in afr
        pthread_mutex_init (&priv->mutex, NULL);
        INIT_LIST_HEAD (&priv->saved_fds);

        child_count = xlator_subvolume_count (this);
        if (child_count != 2) {
                gf_log (this->name, GF_LOG_ERROR,
                        "There should be exactly 2 children - one source "
                        "and one sink");
                return -1;
        }
	priv->child_count = child_count;

        priv->read_child = source_child;
        priv->favorite_child = source_child;
        priv->background_self_heal_count = 0;

	priv->data_self_heal     = "on";
	priv->metadata_self_heal = 1;
	priv->entry_self_heal    = 1;

        priv->data_self_heal_window_size = 16;

	priv->data_change_log     = 1;
	priv->metadata_change_log = 1;
	priv->entry_change_log    = 1;
        priv->use_afr_in_pump = 1;
        priv->sh_readdir_size = 65536;

	/* Locking options */

        /* Lock server count infact does not matter. Locks are held
           on all subvolumes, in this case being the source
           and the sink.
        */

	priv->strict_readdir = _gf_false;

	priv->wait_count = 1;
	priv->child_up = GF_CALLOC (sizeof (unsigned char), child_count,
                                 gf_afr_mt_char);
	if (!priv->child_up) {
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory.");
		op_errno = ENOMEM;
		goto out;
	}

	priv->children = GF_CALLOC (sizeof (xlator_t *), child_count,
                                 gf_afr_mt_xlator_t);
	if (!priv->children) {
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory.");
		op_errno = ENOMEM;
		goto out;
	}

        priv->pending_key = GF_CALLOC (sizeof (*priv->pending_key),
                                       child_count,
                                       gf_afr_mt_char);
        if (!priv->pending_key) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory.");
                op_errno = ENOMEM;
                goto out;
        }

	trav = this->children;
	i = 0;
	while (i < child_count) {
		priv->children[i] = trav->xlator;

                ret = gf_asprintf (&priv->pending_key[i], "%s.%s", AFR_XATTR_PREFIX,
                                   trav->xlator->name);
                if (-1 == ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "asprintf failed to set pending key");
                        op_errno = ENOMEM;
                        goto out;
                }

		trav = trav->next;
		i++;
	}

        ret = gf_asprintf (&priv->sh_domain, "%s-self-heal", this->name);
        if (-1 == ret) {
                op_errno = ENOMEM;
                goto out;
        }

        priv->first_lookup = 1;
        priv->root_inode = NULL;

        priv->last_event = GF_CALLOC (child_count, sizeof (*priv->last_event),
                                      gf_afr_mt_int32_t);
        if (!priv->last_event) {
                ret = -ENOMEM;
                goto out;
        }

	pump_priv = GF_CALLOC (1, sizeof (*pump_priv),
                            gf_afr_mt_pump_priv);
	if (!pump_priv) {
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory");
                op_errno = ENOMEM;
		goto out;
	}

        LOCK_INIT (&pump_priv->resume_path_lock);
        LOCK_INIT (&pump_priv->pump_state_lock);

        pump_priv->resume_path = GF_CALLOC (1, PATH_MAX,
                                            gf_afr_mt_char);
        if (!pump_priv->resume_path) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory");
                ret = -1;
                goto out;
        }

	pump_priv->env = this->ctx->env;
        if (!pump_priv->env) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Could not create new sync-environment");
                ret = -1;
                goto out;
        }

        /* keep more local here as we may need them for self-heal etc */
        this->local_pool = mem_pool_new (afr_local_t, 128);
        if (!this->local_pool) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to create local_t's memory pool");
                goto out;
        }

	priv->pump_private = pump_priv;

        pump_change_state (this, PUMP_STATE_ABORT);

	ret = 0;
out:
	return ret;
}

int
fini (xlator_t *this)
{
        afr_private_t * priv        = NULL;
        pump_private_t *pump_priv = NULL;

        priv      = this->private;
        this->private = NULL;
        if (!priv)
                goto out;

        pump_priv = priv->pump_private;
        if (!pump_priv)
                goto afr_priv;

        GF_FREE (pump_priv->resume_path);
        LOCK_DESTROY (&pump_priv->resume_path_lock);
        LOCK_DESTROY (&pump_priv->pump_state_lock);
        GF_FREE (pump_priv);
afr_priv:
        afr_priv_destroy (priv);
out:
	return 0;
}


struct xlator_fops fops = {
	.lookup      = pump_lookup,
	.open        = pump_open,
	.flush       = pump_flush,
	.fsync       = pump_fsync,
	.fsyncdir    = pump_fsyncdir,
	.xattrop     = pump_xattrop,
	.fxattrop    = pump_fxattrop,
        .getxattr    = pump_getxattr,

	/* inode write */
	.writev      = pump_writev,
	.truncate    = pump_truncate,
	.ftruncate   = pump_ftruncate,
	.setxattr    = pump_setxattr,
        .setattr     = pump_setattr,
	.fsetattr    = pump_fsetattr,
	.removexattr = pump_removexattr,

	/* dir read */
	.opendir     = pump_opendir,
	.readdir     = pump_readdir,
	.readdirp    = pump_readdirp,

	/* dir write */
	.create      = pump_create,
	.mknod       = pump_mknod,
	.mkdir       = pump_mkdir,
	.unlink      = pump_unlink,
	.rmdir       = pump_rmdir,
	.link        = pump_link,
	.symlink     = pump_symlink,
	.rename      = pump_rename,
};

struct xlator_dumpops dumpops = {
        .priv       = afr_priv_dump,
};


struct xlator_cbks cbks = {
	.release     = pump_release,
	.releasedir  = pump_releasedir,
        .forget      = pump_forget,
};

struct volume_options options[] = {
	{ .key  = {NULL} },
};
