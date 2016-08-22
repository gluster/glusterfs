/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "common-utils.h"
#include "glusterd.h"
#include "glusterd-utils.h"
#include "glusterd-messages.h"
#include "glusterd-server-quorum.h"
#include "glusterd-syncop.h"
#include "glusterd-op-sm.h"

#define CEILING_POS(X) (((X)-(int)(X)) > 0 ? (int)((X)+1) : (int)(X))

static gf_boolean_t
glusterd_is_get_op (xlator_t *this, glusterd_op_t op, dict_t *dict)
{
        char            *key            = NULL;
        char            *volname        = NULL;
        int             ret             = 0;

        if (op == GD_OP_STATUS_VOLUME)
                return _gf_true;

        if (op == GD_OP_SET_VOLUME) {
                /*check for set volume help*/
                ret = dict_get_str (dict, "volname", &volname);
                if (volname &&
                    ((strcmp (volname, "help") == 0) ||
                     (strcmp (volname, "help-xml") == 0))) {
                        ret = dict_get_str (dict, "key1", &key);
                        if (ret < 0)
                                return _gf_true;
                }
        }
        return _gf_false;
}

gf_boolean_t
glusterd_is_quorum_validation_required (xlator_t *this, glusterd_op_t op,
                                        dict_t *dict)
{
        gf_boolean_t    required        = _gf_true;
        char            *key            = NULL;
        char            *key_fixed      = NULL;
        int             ret             = -1;

        if (glusterd_is_get_op (this, op, dict)) {
                required = _gf_false;
                goto out;
        }
        if ((op != GD_OP_SET_VOLUME) && (op != GD_OP_RESET_VOLUME))
                goto out;
        if (op == GD_OP_SET_VOLUME)
                ret = dict_get_str (dict, "key1", &key);
        else if (op == GD_OP_RESET_VOLUME)
                ret = dict_get_str (dict, "key", &key);
        if (ret)
                goto out;
        ret = glusterd_check_option_exists (key, &key_fixed);
        if (ret <= 0)
                goto out;
        if (key_fixed)
                key = key_fixed;
        if (glusterd_is_quorum_option (key))
                required = _gf_false;
out:
        GF_FREE (key_fixed);
        return required;
}

int
glusterd_validate_quorum (xlator_t *this, glusterd_op_t op,
                             dict_t *dict, char **op_errstr)
{
        int                      ret     = 0;
        char                    *volname = NULL;
        glusterd_volinfo_t      *volinfo = NULL;
        char                    *errstr  = NULL;

        errstr = "Quorum not met. Volume operation not allowed.";
        if (!glusterd_is_quorum_validation_required (this, op, dict))
                goto out;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                ret = 0;
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                ret = 0;
                goto out;
        }

        if (does_gd_meet_server_quorum (this)) {
                ret = 0;
                goto out;
        }

        if (glusterd_is_volume_in_server_quorum (volinfo)) {
                ret = -1;
                *op_errstr = gf_strdup (errstr);
                goto out;
        }
        ret = 0;
out:
        return ret;
}

gf_boolean_t
glusterd_is_quorum_option (char *option)
{
        gf_boolean_t    res     = _gf_false;
        int             i       = 0;
        static const char * const keys[] = {GLUSTERD_QUORUM_TYPE_KEY,
                                            GLUSTERD_QUORUM_RATIO_KEY,
                                            NULL};

        for (i = 0; keys[i]; i++) {
                if (strcmp (option, keys[i]) == 0) {
                        res = _gf_true;
                        break;
                }
        }
        return res;
}

gf_boolean_t
glusterd_is_quorum_changed (dict_t *options, char *option, char *value)
{
        int             ret             = 0;
        gf_boolean_t    reconfigured    = _gf_false;
        gf_boolean_t    all             = _gf_false;
        char            *oldquorum      = NULL;
        char            *newquorum      = NULL;
        char            *oldratio       = NULL;
        char            *newratio       = NULL;
        xlator_t        *this           = NULL;

        this = THIS;

        if ((strcmp ("all", option) != 0) &&
            !glusterd_is_quorum_option (option))
                goto out;

        if (strcmp ("all", option) == 0)
                all = _gf_true;

        if (all || (strcmp (GLUSTERD_QUORUM_TYPE_KEY, option) == 0)) {
                newquorum = value;
                ret = dict_get_str (options, GLUSTERD_QUORUM_TYPE_KEY,
                                    &oldquorum);
                if (ret)
                        gf_msg (this->name, GF_LOG_DEBUG, 0,
                                GD_MSG_DICT_GET_FAILED,
                                "dict_get_str failed on %s",
                                GLUSTERD_QUORUM_TYPE_KEY);
        }

        if (all || (strcmp (GLUSTERD_QUORUM_RATIO_KEY, option) == 0)) {
                newratio = value;
                ret = dict_get_str (options, GLUSTERD_QUORUM_RATIO_KEY,
                                    &oldratio);
                if (ret)
                        gf_msg (this->name, GF_LOG_DEBUG, 0,
                                GD_MSG_DICT_GET_FAILED,
                                "dict_get_str failed on %s",
                                GLUSTERD_QUORUM_RATIO_KEY);
        }

        reconfigured = _gf_true;

        if (oldquorum && newquorum && (strcmp (oldquorum, newquorum) == 0))
                reconfigured = _gf_false;
        if (oldratio && newratio && (strcmp (oldratio, newratio) == 0))
                reconfigured = _gf_false;

        if ((oldratio == NULL) && (newratio == NULL) && (oldquorum == NULL) &&
            (newquorum == NULL))
                reconfigured = _gf_false;
out:
        return reconfigured;
}

static gf_boolean_t
_is_contributing_to_quorum (gd_quorum_contrib_t contrib)
{
        if ((contrib == QUORUM_UP) || (contrib == QUORUM_DOWN))
                return _gf_true;
        return _gf_false;
}

gf_boolean_t
does_quorum_meet (int active_count, int quorum_count)
{
        return (active_count >= quorum_count);
}

int
glusterd_get_quorum_cluster_counts (xlator_t *this, int *active_count,
                                    int *quorum_count)
{
        glusterd_peerinfo_t *peerinfo      = NULL;
        glusterd_conf_t     *conf          = NULL;
        int                 ret            = -1;
        int                 inquorum_count = 0;
        char                *val           = NULL;
        double              quorum_percentage = 0.0;
        gf_boolean_t        ratio          = _gf_false;
        int                 count          = 0;

        conf = this->private;

        /* Start with counting self */
        inquorum_count = 1;
        if (active_count)
                *active_count = 1;

        rcu_read_lock ();
        cds_list_for_each_entry_rcu (peerinfo, &conf->peers, uuid_list) {
                if (_is_contributing_to_quorum (peerinfo->quorum_contrib))
                        inquorum_count = inquorum_count + 1;
                if (active_count && (peerinfo->quorum_contrib == QUORUM_UP))
                        *active_count = *active_count + 1;
        }
        rcu_read_unlock ();

        ret = dict_get_str (conf->opts, GLUSTERD_QUORUM_RATIO_KEY, &val);
        if (ret == 0) {
                ratio = _gf_true;
                ret = gf_string2percent (val, &quorum_percentage);
                if (!ret)
                        ratio = _gf_true;
        }
        if (ratio)
                count = CEILING_POS (inquorum_count *
                                     quorum_percentage / 100.0);
        else
                count = (inquorum_count * 50 / 100) + 1;

        *quorum_count = count;
        ret = 0;

        return ret;
}

gf_boolean_t
glusterd_is_volume_in_server_quorum (glusterd_volinfo_t *volinfo)
{
        gf_boolean_t    res             = _gf_false;
        char            *quorum_type    = NULL;
        int             ret             = 0;

        ret = dict_get_str (volinfo->dict, GLUSTERD_QUORUM_TYPE_KEY,
                            &quorum_type);
        if (ret)
                goto out;

        if (strcmp (quorum_type, GLUSTERD_SERVER_QUORUM) == 0)
                res = _gf_true;
out:
        return res;
}

gf_boolean_t
glusterd_is_any_volume_in_server_quorum (xlator_t *this)
{
        glusterd_conf_t     *conf     = NULL;
        glusterd_volinfo_t  *volinfo  = NULL;

        conf = this->private;
        list_for_each_entry (volinfo, &conf->volumes, vol_list) {
                if (glusterd_is_volume_in_server_quorum (volinfo)) {
                        return _gf_true;
                }
        }
        return _gf_false;
}

gf_boolean_t
does_gd_meet_server_quorum (xlator_t *this)
{
        int                     quorum_count    = 0;
        int                     active_count    = 0;
        gf_boolean_t            in              = _gf_false;
        int                     ret             = -1;

        ret = glusterd_get_quorum_cluster_counts (this, &active_count,
                                                  &quorum_count);
        if (ret)
                goto out;

        if (!does_quorum_meet (active_count, quorum_count)) {
                goto out;
        }

        in = _gf_true;
out:
        return in;
}

void
glusterd_do_volume_quorum_action (xlator_t *this, glusterd_volinfo_t *volinfo,
                                  gf_boolean_t meets_quorum)
{
        glusterd_brickinfo_t *brickinfo     = NULL;
        gd_quorum_status_t   quorum_status  = NOT_APPLICABLE_QUORUM;
        gf_boolean_t         follows_quorum = _gf_false;

        if (volinfo->status != GLUSTERD_STATUS_STARTED) {
                volinfo->quorum_status = NOT_APPLICABLE_QUORUM;
                goto out;
        }

        follows_quorum = glusterd_is_volume_in_server_quorum (volinfo);
        if (follows_quorum) {
                if (meets_quorum)
                        quorum_status = MEETS_QUORUM;
                else
                        quorum_status = DOESNT_MEET_QUORUM;
        } else {
                quorum_status = NOT_APPLICABLE_QUORUM;
        }

        /*
         * The following check is added to prevent spurious brick starts when
         * events occur that affect quorum.
         * Example:
         * There is a cluster of 10 peers. Volume is in quorum. User
         * takes down one brick from the volume to perform maintenance.
         * Suddenly one of the peers go down. Cluster is still in quorum. But
         * because of this 'peer going down' event, quorum is calculated and
         * the bricks that are down are brought up again. In this process it
         * also brings up the brick that is purposefully taken down.
         */
        if (volinfo->quorum_status == quorum_status)
                goto out;

        if (quorum_status == MEETS_QUORUM) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        GD_MSG_SERVER_QUORUM_MET_STARTING_BRICKS,
                        "Server quorum regained for volume %s. Starting local "
                        "bricks.", volinfo->volname);
                gf_event (EVENT_QUORUM_REGAINED, "volume=%s", volinfo->volname);
        } else if (quorum_status == DOESNT_MEET_QUORUM) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        GD_MSG_SERVER_QUORUM_LOST_STOPPING_BRICKS,
                        "Server quorum lost for volume %s. Stopping local "
                        "bricks.", volinfo->volname);
                gf_event (EVENT_QUORUM_LOST, "volume=%s", volinfo->volname);
        }

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (!glusterd_is_local_brick (this, volinfo, brickinfo))
                        continue;
                if (quorum_status == DOESNT_MEET_QUORUM)
                        glusterd_brick_stop (volinfo, brickinfo, _gf_false);
                else
                        glusterd_brick_start (volinfo, brickinfo, _gf_false);
        }
        volinfo->quorum_status = quorum_status;
out:
        return;
}

int
glusterd_do_quorum_action ()
{
        xlator_t            *this          = NULL;
        glusterd_conf_t     *conf          = NULL;
        glusterd_volinfo_t  *volinfo       = NULL;
        int                 ret            = 0;
        int                 active_count   = 0;
        int                 quorum_count   = 0;
        gf_boolean_t        meets          = _gf_false;

        this = THIS;
        conf = this->private;

        conf->pending_quorum_action = _gf_true;
        ret = glusterd_lock (conf->uuid);
        if (ret)
                goto out;

        {
                ret = glusterd_get_quorum_cluster_counts (this, &active_count,
                                                          &quorum_count);
                if (ret)
                        goto unlock;

                if (does_quorum_meet (active_count, quorum_count))
                        meets = _gf_true;
                list_for_each_entry (volinfo, &conf->volumes, vol_list) {
                        glusterd_do_volume_quorum_action (this, volinfo, meets);
                }
        }
unlock:
        (void)glusterd_unlock (conf->uuid);
        conf->pending_quorum_action = _gf_false;
out:
        return ret;
}

/* ret = 0 represents quorum is not met
 * ret = 1 represents quorum is met
 * ret = 2 represents quorum not applicable
 */

int
check_quorum_for_brick_start (glusterd_volinfo_t *volinfo,
                              gf_boolean_t node_quorum)
{
        gf_boolean_t        volume_quorum  =  _gf_false;
        int                 ret            = 0;

        volume_quorum = glusterd_is_volume_in_server_quorum (volinfo);
        if (volume_quorum) {
                if (node_quorum)
                        ret = 1;
        } else {
                ret = 2;
        }
        return ret;
}

