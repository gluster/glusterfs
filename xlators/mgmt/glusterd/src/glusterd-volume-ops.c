/*
   Copyright (c) 2011-2012 Red Hat, Inc. <http://www.redhat.com>
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

#ifdef HAVE_BD_XLATOR
#include <lvm2app.h>
#endif

#include "common-utils.h"
#include "syscall.h"
#include "cli1-xdr.h"
#include "xdr-generic.h"
#include "glusterd.h"
#include "glusterd-op-sm.h"
#include "glusterd-store.h"
#include "glusterd-utils.h"
#include "glusterd-volgen.h"
#include "run.h"

#define glusterd_op_start_volume_args_get(dict, volname, flags) \
        glusterd_op_stop_volume_args_get (dict, volname, flags)

extern int
_get_slave_status (dict_t *this, char *key, data_t *value, void *data);

int
__glusterd_handle_create_volume (rpcsvc_request_t *req)
{
        int32_t                 ret         = -1;
        gf_cli_req              cli_req     = {{0,}};
        dict_t                 *dict        = NULL;
        char                   *bricks      = NULL;
        char                   *volname     = NULL;
        int                    brick_count  = 0;
        void                   *cli_rsp     = NULL;
        char                    err_str[2048] = {0,};
        gf_cli_rsp              rsp         = {0,};
        xlator_t               *this        = NULL;
        char                   *free_ptr    = NULL;
        char                   *trans_type  = NULL;
        uuid_t                  volume_id   = {0,};
        uuid_t                  tmp_uuid    = {0};
        int32_t                 type        = 0;
        char                   *username    = NULL;
        char                   *password    = NULL;

        GF_ASSERT (req);

        this = THIS;
        GF_ASSERT(this);

        ret = -1;
        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                req->rpc_err = GARBAGE_ARGS;
                snprintf (err_str, sizeof (err_str), "Failed to decode request "
                          "received from cli");
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG, "Received create volume req");

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (err_str, sizeof (err_str), "Unable to decode "
                                  "the command");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "name");
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        if ((ret = glusterd_check_volume_exists (volname))) {
                snprintf (err_str, sizeof (err_str), "Volume %s already exists",
                          volname);
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &brick_count);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get brick count"
                          " for volume %s", volname);
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        ret = dict_get_int32 (dict, "type", &type);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get type of "
                          "volume %s", volname);
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        ret = dict_get_str (dict, "transport", &trans_type);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get "
                          "transport-type of volume %s", volname);
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }
        ret = dict_get_str (dict, "bricks", &bricks);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get bricks for "
                          "volume %s", volname);
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        if (!dict_get (dict, "force")) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get 'force' flag");
                goto out;
        }

        uuid_generate (volume_id);
        free_ptr = gf_strdup (uuid_utoa (volume_id));
        ret = dict_set_dynstr (dict, "volume-id", free_ptr);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to set volume "
                          "id of volume %s", volname);
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }
        free_ptr = NULL;

        /* generate internal username and password */

        uuid_generate (tmp_uuid);
        username = gf_strdup (uuid_utoa (tmp_uuid));
        ret = dict_set_dynstr (dict, "internal-username", username);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set username for "
                        "volume %s", volname);
                goto out;
        }

        uuid_generate (tmp_uuid);
        password = gf_strdup (uuid_utoa (tmp_uuid));
        ret = dict_set_dynstr (dict, "internal-password", password);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set password for "
                        "volume %s", volname);
                goto out;
        }

        ret = glusterd_op_begin_synctask (req, GD_OP_CREATE_VOLUME, dict);

out:
        if (ret) {
                rsp.op_ret = -1;
                rsp.op_errno = 0;
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str),
                                  "Operation failed");
                rsp.op_errstr = err_str;
                cli_rsp = &rsp;
                glusterd_to_cli (req, cli_rsp, NULL, 0, NULL,
                                 (xdrproc_t)xdr_gf_cli_rsp, dict);
                ret = 0; //Client response sent, prevent second response
        }

        GF_FREE(free_ptr);

        return ret;
}

int
glusterd_handle_create_volume (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_create_volume);
}

int
__glusterd_handle_cli_start_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        char                            *volname = NULL;
        dict_t                          *dict = NULL;
        glusterd_op_t                   cli_op = GD_OP_START_VOLUME;
        char                            errstr[2048] = {0,};
        xlator_t                        *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                snprintf (errstr, sizeof (errstr), "Failed to decode message "
                        "received from cli");
                req->rpc_err = GARBAGE_ARGS;
                gf_log (this->name, sizeof (errstr), "%s", errstr);
                goto out;
        }

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (errstr, sizeof (errstr), "Unable to decode "
                                  "the command");
                        goto out;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                snprintf (errstr, sizeof (errstr), "Unable to get volume name");
                gf_log (this->name, GF_LOG_ERROR, "%s", errstr);
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG, "Received start vol req"
                " for volume %s", volname);

        ret = glusterd_op_begin_synctask (req, GD_OP_START_VOLUME, dict);

out:
        free (cli_req.dict.dict_val); //its malloced by xdr

        if (ret) {
                if(errstr[0] == '\0')
                        snprintf (errstr, sizeof (errstr), "Operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, errstr);
        }

        return ret;
}

int
glusterd_handle_cli_start_volume (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_cli_start_volume);
}

int
__glusterd_handle_cli_stop_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        char                            *dup_volname = NULL;
        dict_t                          *dict = NULL;
        glusterd_op_t                   cli_op = GD_OP_STOP_VOLUME;
        xlator_t                        *this = NULL;
        char                            err_str[2048] = {0,};

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                snprintf (err_str, sizeof (err_str), "Failed to decode message "
                          "received from cli");
                req->rpc_err = GARBAGE_ARGS;
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }
        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (err_str, sizeof (err_str), "Unable to decode "
                                  "the command");
                        goto out;
                }
        }

        ret = dict_get_str (dict, "volname", &dup_volname);

        if (ret) {
                snprintf (err_str, sizeof (err_str), "Failed to get volume "
                          "name");
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG, "Received stop vol req "
                "for volume %s", dup_volname);

        ret = glusterd_op_begin_synctask (req, GD_OP_STOP_VOLUME, dict);

out:
        free (cli_req.dict.dict_val); //its malloced by xdr

        if (ret) {
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str),
                                  "Operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, err_str);
        }

        return ret;
}

int
glusterd_handle_cli_stop_volume (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_cli_stop_volume);
}

int
__glusterd_handle_cli_delete_volume (rpcsvc_request_t *req)
{
        int32_t        ret         = -1;
        gf_cli_req     cli_req     = {{0,},};
        glusterd_op_t  cli_op      = GD_OP_DELETE_VOLUME;
        dict_t        *dict        = NULL;
        char          *volname     = NULL;
        char          err_str[2048]= {0,};
        xlator_t      *this        = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                snprintf (err_str, sizeof (err_str), "Failed to decode request "
                          "received from cli");
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (err_str, sizeof (err_str), "Unable to decode "
                                  "the command");
                        goto out;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Failed to get volume "
                          "name");
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG, "Received delete vol req"
                "for volume %s", volname);

        ret = glusterd_op_begin_synctask (req, GD_OP_DELETE_VOLUME, dict);

out:
        free (cli_req.dict.dict_val); //its malloced by xdr

        if (ret) {
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str),
                                  "Operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, err_str);
        }

        return ret;
}

int
glusterd_handle_cli_delete_volume (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_cli_delete_volume);
}

int
__glusterd_handle_cli_heal_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        dict_t                          *dict = NULL;
        glusterd_op_t                   cli_op = GD_OP_HEAL_VOLUME;
        char                            *volname = NULL;
        glusterd_volinfo_t              *volinfo = NULL;
        xlator_t                        *this = NULL;
        char                            op_errstr[2048] = {0,};

        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        this = THIS;
        GF_ASSERT (this);

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (op_errstr, sizeof (op_errstr),
                        "Unable to decode the command");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                snprintf (op_errstr, sizeof (op_errstr), "Unable to find "
                          "volume name");
                gf_log (this->name, GF_LOG_ERROR, "%s", op_errstr);
                goto out;
        }

        gf_log (this->name, GF_LOG_INFO, "Received heal vol req "
                "for volume %s", volname);

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                snprintf (op_errstr, sizeof (op_errstr),
                          "Volume %s does not exist", volname);
                gf_log (this->name, GF_LOG_ERROR, "%s", op_errstr);
                goto out;
        }

        ret = glusterd_add_bricks_hname_path_to_dict (dict, volinfo);
        if (ret)
                goto out;

        ret = dict_set_int32 (dict, "count", volinfo->brick_count);
        if (ret)
                goto out;

        ret = glusterd_op_begin_synctask (req, GD_OP_HEAL_VOLUME, dict);

out:
        if (ret) {
                if (op_errstr[0] == '\0')
                        snprintf (op_errstr, sizeof (op_errstr),
                                  "operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, op_errstr);
        }

        return ret;
}

int
glusterd_handle_cli_heal_volume (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_cli_heal_volume);
}

int
__glusterd_handle_cli_statedump_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        char                            *volname = NULL;
        char                            *options = NULL;
        dict_t                          *dict = NULL;
        int32_t                         option_cnt = 0;
        glusterd_op_t                   cli_op = GD_OP_STATEDUMP_VOLUME;
        char                            err_str[2048] = {0,};
        xlator_t                        *this = NULL;
        glusterd_conf_t                 *priv = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        GF_ASSERT (req);

        ret = -1;
        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }
        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (err_str, sizeof (err_str), "Unable to "
                                  "decode the command");
                        goto out;
                }
        }
        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get the volume "
                          "name");
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        ret = dict_get_str (dict, "options", &options);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get options");
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        ret = dict_get_int32 (dict, "option_cnt", &option_cnt);
        if (ret) {
                snprintf (err_str , sizeof (err_str), "Unable to get option "
                          "count");
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        if (priv->op_version == GD_OP_VERSION_MIN &&
            strstr (options, "quotad")) {
                snprintf (err_str, sizeof (err_str), "The cluster is operating "
                          "at op-version 1. Taking quotad's statedump is "
                          "disallowed in this state");
                ret = -1;
                goto out;
        }

        gf_log (this->name, GF_LOG_INFO, "Received statedump request for "
                "volume %s with options %s", volname, options);

        ret = glusterd_op_begin_synctask (req, GD_OP_STATEDUMP_VOLUME, dict);

out:
        if (ret) {
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str),
                                  "Operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, err_str);
        }
        free (cli_req.dict.dict_val);

        return ret;
}

int
glusterd_handle_cli_statedump_volume (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_cli_statedump_volume);
}

#ifdef HAVE_BD_XLATOR
/*
 * Validates if given VG in the brick exists or not. Also checks if VG has
 * GF_XATTR_VOL_ID_KEY tag set to avoid using same VG for multiple bricks.
 * Tag is checked only during glusterd_op_stage_create_volume. Tag is set during
 * glusterd_validate_and_create_brickpath().
 * @brick - brick info, @check_tag - check for VG tag or not
 * @msg - Error message to return to caller
 */
int
glusterd_is_valid_vg (glusterd_brickinfo_t *brick, int check_tag, char *msg)
{
        lvm_t                     handle     = NULL;
        vg_t                      vg         = NULL;
        char                     *vg_name    = NULL;
        int                       retval     = 0;
        char                     *p          = NULL;
        char                     *ptr        = NULL;
        struct dm_list           *dm_lvlist = NULL;
        struct dm_list           *dm_seglist = NULL;
        struct lvm_lv_list       *lv_list    = NULL;
        struct lvm_property_value prop       = {0, };
        struct lvm_lvseg_list    *seglist    = NULL;
        struct dm_list           *taglist    = NULL;
        struct lvm_str_list      *strl       = NULL;

        handle = lvm_init (NULL);
        if (!handle) {
                sprintf (msg, "lvm_init failed, could not validate vg");
                return -1;
        }
        if (*brick->vg == '\0') { /* BD xlator has vg in brick->path */
                p = gf_strdup (brick->path);
                vg_name = strtok_r (p, "/", &ptr);
        } else
                vg_name = brick->vg;

        vg = lvm_vg_open (handle, vg_name, "r", 0);
        if (!vg) {
                sprintf (msg, "no such vg: %s", vg_name);
                retval = -1;
                goto out;
        }
        if (!check_tag)
                goto next;

        taglist = lvm_vg_get_tags (vg);
        if (!taglist)
                goto next;

        dm_list_iterate_items (strl, taglist) {
                if (!strncmp(strl->str, GF_XATTR_VOL_ID_KEY,
                             strlen (GF_XATTR_VOL_ID_KEY))) {
                            sprintf (msg, "VG %s is already part of"
                                    " a brick", vg_name);
                            retval = -1;
                            goto out;
                    }
        }
next:

        brick->caps = CAPS_BD | CAPS_OFFLOAD_COPY | CAPS_OFFLOAD_SNAPSHOT;

        dm_lvlist = lvm_vg_list_lvs (vg);
        if (!dm_lvlist)
                goto out;

        dm_list_iterate_items (lv_list, dm_lvlist) {
                dm_seglist = lvm_lv_list_lvsegs (lv_list->lv);
                dm_list_iterate_items (seglist, dm_seglist) {
                        prop = lvm_lvseg_get_property (seglist->lvseg,
                                                       "segtype");
                        if (!prop.is_valid || !prop.value.string)
                                continue;
                        if (!strcmp (prop.value.string, "thin-pool")) {
                                brick->caps |= CAPS_THIN;
                                gf_log (THIS->name, GF_LOG_INFO, "Thin Pool "
                                        "\"%s\" will be used for thin LVs",
                                        lvm_lv_get_name (lv_list->lv));
                                break;
                        }
                }
        }

        retval = 0;
out:
        if (vg)
                lvm_vg_close (vg);
        lvm_quit (handle);
        if (p)
                GF_FREE (p);
        return retval;
}
#endif

/* op-sm */
int
glusterd_op_stage_create_volume (dict_t *dict, char **op_errstr)
{
        int                                     ret = 0;
        char                                    *volname = NULL;
        gf_boolean_t                            exists = _gf_false;
        char                                    *bricks = NULL;
        char                                    *brick_list = NULL;
        char                                    *free_ptr = NULL;
        glusterd_brickinfo_t                    *brick_info = NULL;
        int32_t                                 brick_count = 0;
        int32_t                                 i = 0;
        char                                    *brick = NULL;
        char                                    *tmpptr = NULL;
        xlator_t                                *this = NULL;
        glusterd_conf_t                         *priv = NULL;
        char                                    msg[2048] = {0};
        uuid_t                                  volume_uuid;
        char                                    *volume_uuid_str;
        gf_boolean_t                             is_force = _gf_false;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);
        if (exists) {
                snprintf (msg, sizeof (msg), "Volume %s already exists",
                          volname);
                ret = -1;
                goto out;
        } else {
                ret = 0;
        }

        ret = dict_get_int32 (dict, "count", &brick_count);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get brick count "
                        "for volume %s", volname);
                goto out;
        }

        ret = dict_get_str (dict, "volume-id", &volume_uuid_str);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get volume id of "
                        "volume %s", volname);
                goto out;
        }

        ret = uuid_parse (volume_uuid_str, volume_uuid);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to parse volume id of"
                        " volume %s", volname);
                goto out;
        }

        ret = dict_get_str (dict, "bricks", &bricks);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get bricks for "
                        "volume %s", volname);
                goto out;
        }

        is_force = dict_get_str_boolean (dict, "force", _gf_false);

        if (bricks) {
                brick_list = gf_strdup (bricks);
                if (!brick_list) {
                        ret = -1;
                        goto out;
                } else {
                        free_ptr = brick_list;
                }
        }

        while ( i < brick_count) {
                i++;
                brick= strtok_r (brick_list, " \n", &tmpptr);
                brick_list = tmpptr;

                if (!glusterd_store_is_valid_brickpath (volname, brick) ||
                        !glusterd_is_valid_volfpath (volname, brick)) {
                        snprintf (msg, sizeof (msg), "brick path %s is too "
                                  "long.", brick);
                        ret = -1;
                        goto out;
                }

                ret = glusterd_brickinfo_new_from_brick (brick, &brick_info);
                if (ret)
                        goto out;

                ret = glusterd_new_brick_validate (brick, brick_info, msg,
                                                   sizeof (msg));
                if (ret)
                        goto out;

                ret = glusterd_resolve_brick (brick_info);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, FMTSTR_RESOLVE_BRICK,
                                brick_info->hostname, brick_info->path);
                        goto out;
                }

                if (!uuid_compare (brick_info->uuid, MY_UUID)) {
#ifdef HAVE_BD_XLATOR
                        if (brick_info->vg[0]) {
                                ret = glusterd_is_valid_vg (brick_info, 1, msg);
                                if (ret)
                                        goto out;
                        }
#endif
                        ret = glusterd_validate_and_create_brickpath (brick_info,
                                                          volume_uuid, op_errstr,
                                                          is_force);
                        if (ret)
                                goto out;
                        brick_list = tmpptr;
                }
                glusterd_brickinfo_delete (brick_info);
                brick_info = NULL;
        }
out:
        GF_FREE (free_ptr);
        if (brick_info)
                glusterd_brickinfo_delete (brick_info);

        if (msg[0] != '\0') {
                gf_log (this->name, GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
        }
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int
glusterd_op_stop_volume_args_get (dict_t *dict, char** volname, int *flags)
{
        int ret = -1;
        xlator_t *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        if (!dict || !volname || !flags)
                goto out;

        ret = dict_get_str (dict, "volname", volname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        ret = dict_get_int32 (dict, "flags", flags);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get flags");
                goto out;
        }
out:
        return ret;
}

int
glusterd_op_statedump_volume_args_get (dict_t *dict, char **volname,
                                       char **options, int *option_cnt)
{
        int ret = -1;

        if (!dict || !volname || !options || !option_cnt)
                goto out;

        ret = dict_get_str (dict, "volname", volname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volname");
                goto out;
        }

        ret = dict_get_str (dict, "options", options);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get options");
                goto out;
        }

        ret = dict_get_int32 (dict, "option_cnt", option_cnt);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get option count");
                goto out;
        }

out:
        return ret;
}

int
glusterd_op_stage_start_volume (dict_t *dict, char **op_errstr)
{
        int                                     ret = 0;
        char                                    *volname = NULL;
        int                                     flags = 0;
        gf_boolean_t                            exists = _gf_false;
        glusterd_volinfo_t                      *volinfo = NULL;
        glusterd_brickinfo_t                    *brickinfo = NULL;
        char                                    msg[2048] = {0,};
        glusterd_conf_t                         *priv = NULL;
        xlator_t                                *this = NULL;
        uuid_t                                  volume_id = {0,};
        char                                    volid[50] = {0,};
        char                                    xattr_volid[50] = {0,};
        int                                     caps = 0;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        ret = glusterd_op_start_volume_args_get (dict, &volname, &flags);
        if (ret)
                goto out;

        exists = glusterd_check_volume_exists (volname);

        if (!exists) {
                snprintf (msg, sizeof (msg), FMTSTR_CHECK_VOL_EXISTS, volname);
                ret = -1;
                goto out;
        }

        ret  = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, FMTSTR_CHECK_VOL_EXISTS,
                        volname);
                goto out;
        }

        ret = glusterd_validate_volume_id (dict, volinfo);
        if (ret)
                goto out;

        if (!(flags & GF_CLI_FLAG_OP_FORCE)) {
                if (glusterd_is_volume_started (volinfo)) {
                        snprintf (msg, sizeof (msg), "Volume %s already "
                                  "started", volname);
                        ret = -1;
                        goto out;
                }
        }

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                ret = glusterd_resolve_brick (brickinfo);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, FMTSTR_RESOLVE_BRICK,
                                brickinfo->hostname, brickinfo->path);
                        goto out;
                }

                if (uuid_compare (brickinfo->uuid, MY_UUID))
                        continue;

                ret = gf_lstat_dir (brickinfo->path, NULL);
                if (ret && (flags & GF_CLI_FLAG_OP_FORCE)) {
                        continue;
                } else if (ret) {
                        snprintf (msg, sizeof (msg), "Failed to find "
                                          "brick directory %s for volume %s. "
                                          "Reason : %s", brickinfo->path,
                                          volname, strerror (errno));
                        goto out;
                }
                ret = sys_lgetxattr (brickinfo->path, GF_XATTR_VOL_ID_KEY,
                                     volume_id, 16);
                if (ret < 0 && (!(flags & GF_CLI_FLAG_OP_FORCE))) {
                        snprintf (msg, sizeof (msg), "Failed to get "
                                  "extended attribute %s for brick dir %s. "
                                  "Reason : %s", GF_XATTR_VOL_ID_KEY,
                                  brickinfo->path, strerror (errno));
                        ret = -1;
                        goto out;
                } else if (ret < 0) {
                        ret = sys_lsetxattr (brickinfo->path,
                                             GF_XATTR_VOL_ID_KEY,
                                             volinfo->volume_id, 16,
                                             XATTR_CREATE);
                        if (ret) {
                                snprintf (msg, sizeof (msg), "Failed to set "
                                        "extended attribute %s on %s. Reason: "
                                        "%s", GF_XATTR_VOL_ID_KEY,
                                        brickinfo->path, strerror (errno));
                                goto out;
                        } else {
                                continue;
                        }
                }
                if (uuid_compare (volinfo->volume_id, volume_id)) {
                        snprintf (msg, sizeof (msg), "Volume id mismatch for "
                                  "brick %s:%s. Expected volume id %s, "
                                  "volume id %s found", brickinfo->hostname,
                                  brickinfo->path,
                                  uuid_utoa_r (volinfo->volume_id, volid),
                                  uuid_utoa_r (volume_id, xattr_volid));
                        ret = -1;
                        goto out;
                }
#ifdef HAVE_BD_XLATOR
                if (brickinfo->vg[0])
                        caps = CAPS_BD | CAPS_THIN |
                                CAPS_OFFLOAD_COPY | CAPS_OFFLOAD_SNAPSHOT;
                /* Check for VG/thin pool if its BD volume */
                if (brickinfo->vg[0]) {
                        ret = glusterd_is_valid_vg (brickinfo, 0, msg);
                        if (ret)
                                goto out;
                        /* if anyone of the brick does not have thin support,
                           disable it for entire volume */
                        caps &= brickinfo->caps;
                } else
                        caps = 0;
#endif
        }

        volinfo->caps = caps;
        ret = 0;
out:
        if (ret && (msg[0] != '\0')) {
                gf_log (this->name, GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
        }
        return ret;
}

int
glusterd_op_stage_stop_volume (dict_t *dict, char **op_errstr)
{
        int                                     ret = -1;
        char                                    *volname = NULL;
        int                                     flags = 0;
        gf_boolean_t                            exists = _gf_false;
        gf_boolean_t                            is_run = _gf_false;
        glusterd_volinfo_t                      *volinfo = NULL;
        char                                    msg[2048] = {0};
        xlator_t                                *this = NULL;
        gsync_status_param_t                    param = {0,};

        this = THIS;
        GF_ASSERT (this);

        ret = glusterd_op_stop_volume_args_get (dict, &volname, &flags);
        if (ret)
                goto out;

        exists = glusterd_check_volume_exists (volname);

        if (!exists) {
                snprintf (msg, sizeof (msg), FMTSTR_CHECK_VOL_EXISTS, volname);
                gf_log (this->name, GF_LOG_ERROR, "%s", msg);
                ret = -1;
                goto out;
        }

        ret  = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                snprintf (msg, sizeof (msg), FMTSTR_CHECK_VOL_EXISTS, volname);
                gf_log (this->name, GF_LOG_ERROR, "%s", msg);
                goto out;
        }

        ret = glusterd_validate_volume_id (dict, volinfo);
        if (ret)
                goto out;

        /* If 'force' flag is given, no check is required */
        if (flags & GF_CLI_FLAG_OP_FORCE)
                goto out;

        if (_gf_false == glusterd_is_volume_started (volinfo)) {
                snprintf (msg, sizeof(msg), "Volume %s "
                          "is not in the started state", volname);
                gf_log (this->name, GF_LOG_ERROR, "%s", msg);
                ret = -1;
                goto out;
        }
        ret = glusterd_check_gsync_running (volinfo, &is_run);
        if (ret && (is_run == _gf_false))
                gf_log (this->name, GF_LOG_WARNING, "Unable to get the status"
                        " of active "GEOREP" session");

        param.volinfo = volinfo;
        ret = dict_foreach (volinfo->gsync_slaves, _get_slave_status, &param);

        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "_get_slave_satus failed");
                snprintf (msg, sizeof(msg), GEOREP" Unable to get the status "
                          "of active "GEOREP" session for the volume '%s'.\n"
                          "Please check the log file for more info. Use "
                          "'force' option to ignore and stop the volume.",
                          volname);
                ret = -1;
                goto out;
        }

        if (is_run && param.is_active) {
                gf_log (this->name, GF_LOG_WARNING, GEOREP" sessions active"
                        "for the volume %s ", volname);
                snprintf (msg, sizeof(msg), GEOREP" sessions are active "
                          "for the volume '%s'.\nUse 'volume "GEOREP" "
                          "status' command for more info. Use 'force' "
                          "option to ignore and stop the volume.",
                          volname);
                ret = -1;
                goto out;
        }

        if (glusterd_is_rb_ongoing (volinfo)) {
                snprintf (msg, sizeof (msg), "Replace brick is in progress on "
                          "volume %s. Please retry after replace-brick "
                          "operation is committed or aborted", volname);
                gf_log (this->name, GF_LOG_WARNING, "replace-brick in progress "
                        "on volume %s", volname);
                ret = -1;
                goto out;
        }

        if (glusterd_is_defrag_on (volinfo)) {
                snprintf (msg, sizeof(msg), "rebalance session is "
                          "in progress for the volume '%s'", volname);
                gf_log (this->name, GF_LOG_WARNING, "%s", msg);
                ret = -1;
                goto out;
        }
        if (volinfo->rep_brick.rb_status != GF_RB_STATUS_NONE) {
                snprintf (msg, sizeof(msg), "replace-brick session is "
                          "in progress for the volume '%s'", volname);
                gf_log (this->name, GF_LOG_WARNING, "%s", msg);
                ret = -1;
                goto out;
        }

out:
        if (msg[0] != 0)
                *op_errstr = gf_strdup (msg);
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int
glusterd_op_stage_delete_volume (dict_t *dict, char **op_errstr)
{
        int                                     ret = 0;
        char                                    *volname = NULL;
        gf_boolean_t                            exists = _gf_false;
        glusterd_volinfo_t                      *volinfo = NULL;
        char                                    msg[2048] = {0};
        xlator_t                                *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);
        if (!exists) {
                snprintf (msg, sizeof (msg), FMTSTR_CHECK_VOL_EXISTS, volname);
                ret = -1;
                goto out;
        } else {
                ret = 0;
        }

        ret  = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                snprintf (msg, sizeof (msg), FMTSTR_CHECK_VOL_EXISTS, volname);
                goto out;
        }

        ret = glusterd_validate_volume_id (dict, volinfo);
        if (ret)
                goto out;

        if (glusterd_is_volume_started (volinfo)) {
                snprintf (msg, sizeof (msg), "Volume %s has been started."
                          "Volume needs to be stopped before deletion.",
                          volname);
                ret = -1;
                goto out;
        }

        ret = 0;

out:
        if (msg[0] != '\0') {
                gf_log (this->name, GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
        }
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int
glusterd_op_stage_heal_volume (dict_t *dict, char **op_errstr)
{
        int                                     ret = 0;
        char                                    *volname = NULL;
        gf_boolean_t                            enabled = _gf_false;
        glusterd_volinfo_t                      *volinfo = NULL;
        char                                    msg[2048];
        glusterd_conf_t                         *priv = NULL;
        dict_t                                  *opt_dict = NULL;
        gf_xl_afr_op_t                          heal_op = GF_AFR_OP_INVALID;
        xlator_t                                *this = NULL;

        this = THIS;
        priv = this->private;
        if (!priv) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR,
                        "priv is NULL");
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        ret  = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                ret = -1;
                snprintf (msg, sizeof (msg), "Volume %s does not exist", volname);
                gf_log (this->name, GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        ret = glusterd_validate_volume_id (dict, volinfo);
        if (ret)
                goto out;

        if (!glusterd_is_volume_replicate (volinfo)) {
                ret = -1;
                snprintf (msg, sizeof (msg), "Volume %s is not of type "
                          "replicate", volname);
                *op_errstr = gf_strdup (msg);
                gf_log (this->name, GF_LOG_WARNING, "%s", msg);
                goto out;
        }

        if (!glusterd_is_volume_started (volinfo)) {
                ret = -1;
                snprintf (msg, sizeof (msg), "Volume %s is not started.",
                          volname);
                gf_log (THIS->name, GF_LOG_WARNING, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        opt_dict = volinfo->dict;
        if (!opt_dict) {
                ret = 0;
                goto out;
        }

        enabled = dict_get_str_boolean (opt_dict, "cluster.self-heal-daemon",
                                        1);
        if (!enabled) {
                ret = -1;
                snprintf (msg, sizeof (msg), "Self-heal-daemon is "
                          "disabled. Heal will not be triggered on volume %s",
                          volname);
                gf_log (this->name, GF_LOG_WARNING, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        ret = dict_get_int32 (dict, "heal-op", (int32_t*)&heal_op);
        if (ret || (heal_op == GF_AFR_OP_INVALID)) {
                ret = -1;
                *op_errstr = gf_strdup("Invalid heal-op");
                gf_log (this->name, GF_LOG_WARNING, "%s", "Invalid heal-op");
                goto out;
        }

        switch (heal_op) {
                case GF_AFR_OP_INDEX_SUMMARY:
                case GF_AFR_OP_STATISTICS_HEAL_COUNT:
                case GF_AFR_OP_STATISTICS_HEAL_COUNT_PER_REPLICA:
                        break;
                default:
                        if (!glusterd_is_nodesvc_online("glustershd")){
                                ret = -1;
                                *op_errstr = gf_strdup ("Self-heal daemon is "
                                                "not running. Check self-heal "
                                                "daemon log file.");
                                gf_log (this->name, GF_LOG_WARNING, "%s",
                                        "Self-heal daemon is not running."
                                        "Check self-heal daemon log file.");
                                goto out;
                        }
        }

        ret = 0;
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int
glusterd_op_stage_statedump_volume (dict_t *dict, char **op_errstr)
{
        int                     ret = -1;
        char                    *volname = NULL;
        char                    *options = NULL;
        int                     option_cnt = 0;
        gf_boolean_t            is_running = _gf_false;
        glusterd_volinfo_t      *volinfo = NULL;
        char                    msg[2408] = {0,};
        xlator_t                *this     = NULL;
        glusterd_conf_t         *priv     = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        ret = glusterd_op_statedump_volume_args_get (dict, &volname, &options,
                                                     &option_cnt);
        if (ret)
                goto out;

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                snprintf (msg, sizeof(msg), FMTSTR_CHECK_VOL_EXISTS, volname);
                goto out;
        }

        ret = glusterd_validate_volume_id (dict, volinfo);
        if (ret)
                goto out;

        is_running = glusterd_is_volume_started (volinfo);
        if (!is_running) {
                snprintf (msg, sizeof(msg), "Volume %s is not in the started"
                          " state", volname);
                ret = -1;
                goto out;
        }

        if (priv->op_version == GD_OP_VERSION_MIN &&
            strstr (options, "quotad")) {
                snprintf (msg, sizeof (msg), "The cluster is operating "
                          "at op-version 1. Taking quotad's statedump is "
                          "disallowed in this state");
                ret = -1;
                goto out;
        }
        if ((strstr (options, "quotad")) &&
            (!glusterd_is_volume_quota_enabled (volinfo))) {
                    snprintf (msg, sizeof (msg), "Quota is not enabled on "
                              "volume %s", volname);
                    ret = -1;
                    goto out;
        }
out:
        if (ret && msg[0] != '\0')
                *op_errstr = gf_strdup (msg);
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_op_stage_clearlocks_volume (dict_t *dict, char **op_errstr)
{
        int                     ret = -1;
        char                    *volname = NULL;
        char                    *path    = NULL;
        char                    *type    = NULL;
        char                    *kind    = NULL;
        glusterd_volinfo_t      *volinfo = NULL;
        char                    msg[2048] = {0,};

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                snprintf (msg, sizeof(msg), "Failed to get volume name");
                gf_log (THIS->name, GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        ret = dict_get_str (dict, "path", &path);
        if (ret) {
                snprintf (msg, sizeof(msg), "Failed to get path");
                gf_log (THIS->name, GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        ret = dict_get_str (dict, "kind", &kind);
        if (ret) {
                snprintf (msg, sizeof(msg), "Failed to get kind");
                gf_log ("", GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        ret = dict_get_str (dict, "type", &type);
        if (ret) {
                snprintf (msg, sizeof(msg), "Failed to get type");
                gf_log ("", GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                snprintf (msg, sizeof(msg), "Volume %s does not exist",
                          volname);
                gf_log ("", GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        ret = glusterd_validate_volume_id (dict, volinfo);
        if (ret)
                goto out;

        if (!glusterd_is_volume_started (volinfo)) {
                snprintf (msg, sizeof(msg), "Volume %s is not started",
                          volname);
                gf_log ("", GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        ret = 0;
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_op_create_volume (dict_t *dict, char **op_errstr)
{
        int                   ret        = 0;
        char                 *volname    = NULL;
        glusterd_conf_t      *priv       = NULL;
        glusterd_volinfo_t   *volinfo    = NULL;
        gf_boolean_t          vol_added = _gf_false;
        glusterd_brickinfo_t *brickinfo  = NULL;
        xlator_t             *this       = NULL;
        char                 *brick      = NULL;
        int32_t               count      = 0;
        int32_t               i          = 1;
        char                 *bricks     = NULL;
        char                 *brick_list = NULL;
        char                 *free_ptr   = NULL;
        char                 *saveptr    = NULL;
        char                 *trans_type = NULL;
        char                 *str        = NULL;
        char                 *username   = NULL;
        char                 *password   = NULL;
        int                   caps       = 0;
        char                  msg[1024] __attribute__((unused)) = {0, };

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = glusterd_volinfo_new (&volinfo);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Unable to allocate memory for volinfo");
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        strncpy (volinfo->volname, volname, GLUSTERD_MAX_VOLUME_NAME);
        GF_ASSERT (volinfo->volname);

        ret = dict_get_int32 (dict, "type", &volinfo->type);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get type of volume"
                        " %s", volname);
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &volinfo->brick_count);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get brick count of"
                        " volume %s", volname);
                goto out;
        }

        ret = dict_get_int32 (dict, "port", &volinfo->port);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get port");
                goto out;
        }

        count = volinfo->brick_count;

        ret = dict_get_str (dict, "bricks", &bricks);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get bricks for "
                        "volume %s", volname);
                goto out;
        }

        /* replica-count 1 means, no replication, file is in one brick only */
        volinfo->replica_count = 1;
        /* stripe-count 1 means, no striping, file is present as a whole */
        volinfo->stripe_count = 1;

        if (GF_CLUSTER_TYPE_REPLICATE == volinfo->type) {
                ret = dict_get_int32 (dict, "replica-count",
                                      &volinfo->replica_count);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to get "
                                 "replica count for volume %s", volname);
                        goto out;
                }
        } else if (GF_CLUSTER_TYPE_STRIPE == volinfo->type) {
                ret = dict_get_int32 (dict, "stripe-count",
                                      &volinfo->stripe_count);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to get stripe"
                                " count for volume %s", volname);
                        goto out;
                }
        } else if (GF_CLUSTER_TYPE_STRIPE_REPLICATE == volinfo->type) {
                ret = dict_get_int32 (dict, "stripe-count",
                                      &volinfo->stripe_count);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to get stripe"
                                " count for volume %s", volname);
                        goto out;
                }
                ret = dict_get_int32 (dict, "replica-count",
                                      &volinfo->replica_count);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to get "
                                "replica count for volume %s", volname);
                        goto out;
                }
        }

        /* dist-leaf-count is the count of brick nodes for a given
           subvolume of distribute */
        volinfo->dist_leaf_count = glusterd_get_dist_leaf_count (volinfo);

        /* subvol_count is the count of number of subvolumes present
           for a given distribute volume */
        volinfo->subvol_count = (volinfo->brick_count /
                                 volinfo->dist_leaf_count);

        /* Keep sub-count same as earlier, for the sake of backward
           compatibility */
        if (volinfo->dist_leaf_count > 1)
                volinfo->sub_count = volinfo->dist_leaf_count;

        ret = dict_get_str (dict, "transport", &trans_type);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Unable to get transport type of volume %s", volname);
                goto out;
        }

        ret = dict_get_str (dict, "volume-id", &str);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Unable to get volume-id of volume %s", volname);
                goto out;
        }
        ret = uuid_parse (str, volinfo->volume_id);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "unable to parse uuid %s of volume %s", str, volname);
                goto out;
        }

        ret = dict_get_str (dict, "internal-username", &username);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "unable to get internal username of volume %s",
                        volname);
                goto out;
        }
        glusterd_auth_set_username (volinfo, username);

        ret = dict_get_str (dict, "internal-password", &password);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "unable to get internal password of volume %s",
                        volname);
                goto out;
        }
        glusterd_auth_set_password (volinfo, password);

        if (strcasecmp (trans_type, "rdma") == 0) {
                volinfo->transport_type = GF_TRANSPORT_RDMA;
                volinfo->nfs_transport_type = GF_TRANSPORT_RDMA;
        } else if (strcasecmp (trans_type, "tcp") == 0) {
                volinfo->transport_type = GF_TRANSPORT_TCP;
                volinfo->nfs_transport_type = GF_TRANSPORT_TCP;
        } else {
                volinfo->transport_type = GF_TRANSPORT_BOTH_TCP_RDMA;
                volinfo->nfs_transport_type = GF_DEFAULT_NFS_TRANSPORT;
        }

        if (bricks) {
                brick_list = gf_strdup (bricks);
                free_ptr = brick_list;
        }

        if (count)
                brick = strtok_r (brick_list+1, " \n", &saveptr);
        caps = CAPS_BD | CAPS_THIN | CAPS_OFFLOAD_COPY | CAPS_OFFLOAD_SNAPSHOT;

        while ( i <= count) {
                ret = glusterd_brickinfo_new_from_brick (brick, &brickinfo);
                if (ret)
                        goto out;

                ret = glusterd_resolve_brick (brickinfo);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, FMTSTR_RESOLVE_BRICK,
                                brickinfo->hostname, brickinfo->path);
                        goto out;
                }

#ifdef HAVE_BD_XLATOR
                if (!uuid_compare (brickinfo->uuid, MY_UUID)) {
                        if (brickinfo->vg[0]) {
                                ret = glusterd_is_valid_vg (brickinfo, 0, msg);
                                if (ret) {
                                        gf_log (this->name, GF_LOG_ERROR, "%s",
                                                msg);
                                        goto out;
                                }

                                /* if anyone of the brick does not have thin
                                   support, disable it for entire volume */
                                caps &= brickinfo->caps;


                        } else
                                caps = 0;
                }
#endif

                list_add_tail (&brickinfo->brick_list, &volinfo->bricks);
                brick = strtok_r (NULL, " \n", &saveptr);
                i++;
        }

        gd_update_volume_op_versions (volinfo);

        volinfo->caps = caps;

        ret = glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret) {
                glusterd_store_delete_volume (volinfo);
                *op_errstr = gf_strdup ("Failed to store the Volume information");
                goto out;
        }

        ret = glusterd_create_volfiles_and_notify_services (volinfo);
        if (ret) {
                *op_errstr = gf_strdup ("Failed to create volume files");
                goto out;
        }

        volinfo->rebal.defrag_status = 0;
        list_add_tail (&volinfo->vol_list, &priv->volumes);
        vol_added = _gf_true;

out:
        GF_FREE(free_ptr);
        if (!vol_added && volinfo)
                glusterd_volinfo_unref (volinfo);
        return ret;
}

int
glusterd_op_start_volume (dict_t *dict, char **op_errstr)
{
        int                                     ret = 0;
        char                                    *volname = NULL;
        int                                     flags = 0;
        glusterd_volinfo_t                      *volinfo = NULL;
        glusterd_brickinfo_t                    *brickinfo = NULL;
        xlator_t                                *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = glusterd_op_start_volume_args_get (dict, &volname, &flags);
        if (ret)
                goto out;

        ret  = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, FMTSTR_CHECK_VOL_EXISTS,
                        volname);
                goto out;
        }

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                ret = glusterd_brick_start (volinfo, brickinfo, _gf_true);
                /* If 'force' try to start all bricks regardless of success or
                 * failure
                 */
                if (!(flags & GF_CLI_FLAG_OP_FORCE) && ret)
                        goto out;
        }

        glusterd_set_volume_status (volinfo, GLUSTERD_STATUS_STARTED);

        ret = glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret)
                goto out;

        ret = glusterd_nodesvcs_handle_graph_change (volinfo);

out:
        gf_log (this->name, GF_LOG_DEBUG, "returning %d ", ret);
        return ret;
}


int
glusterd_op_stop_volume (dict_t *dict)
{
        int                                     ret = 0;
        int                                     flags = 0;
        char                                    *volname = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        glusterd_brickinfo_t                    *brickinfo = NULL;
        xlator_t                                *this = NULL;
        char                                    mountdir[PATH_MAX] = {0,};
        runner_t                                runner = {0,};
        char                                    pidfile[PATH_MAX] = {0,};

        this = THIS;
        GF_ASSERT (this);

        ret = glusterd_op_stop_volume_args_get (dict, &volname, &flags);
        if (ret)
                goto out;

        ret  = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, FMTSTR_CHECK_VOL_EXISTS,
                        volname);
                goto out;
        }

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                ret = glusterd_brick_stop (volinfo, brickinfo, _gf_false);
                if (ret)
                        goto out;
        }

        glusterd_set_volume_status (volinfo, GLUSTERD_STATUS_STOPPED);

        ret = glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret)
                goto out;

        /* If quota auxiliary mount is present, unmount it */
        GLUSTERFS_GET_AUX_MOUNT_PIDFILE (pidfile, volname);

        if (!gf_is_service_running (pidfile, NULL)) {
                gf_log (this->name, GF_LOG_DEBUG, "Aux mount of volume %s "
                        "absent", volname);
        } else {
                GLUSTERD_GET_QUOTA_AUX_MOUNT_PATH (mountdir, volname, "/");

                runinit (&runner);
                runner_add_args (&runner, "umount",

                #if GF_LINUX_HOST_OS
                                "-l",
                #endif
                                mountdir, NULL);
                ret = runner_run_reuse (&runner);
                if (ret)
                        gf_log (this->name, GF_LOG_ERROR, "umount on %s failed, "
                                "reason : %s", mountdir, strerror (errno));

                runner_end (&runner);
        }

        ret = glusterd_nodesvcs_handle_graph_change (volinfo);
out:
        return ret;
}

int
glusterd_op_delete_volume (dict_t *dict)
{
        int                                     ret = 0;
        char                                    *volname = NULL;
        glusterd_conf_t                         *priv = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        xlator_t                                *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        ret  = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, FMTSTR_CHECK_VOL_EXISTS,
                        volname);
                goto out;
        }

        ret = glusterd_remove_auxiliary_mount (volname);
        if (ret)
                goto out;

        ret = glusterd_delete_volume (volinfo);
out:
        gf_log (this->name, GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int
glusterd_op_heal_volume (dict_t *dict, char **op_errstr)
{
        int                                     ret = 0;
        /* Necessary subtasks of heal are completed in brick op */

        return ret;
}

int
glusterd_op_statedump_volume (dict_t *dict, char **op_errstr)
{
        int                     ret = 0;
        char                    *volname = NULL;
        char                    *options = NULL;
        int                     option_cnt = 0;
        glusterd_volinfo_t      *volinfo = NULL;
        glusterd_brickinfo_t    *brickinfo = NULL;

        ret = glusterd_op_statedump_volume_args_get (dict, &volname, &options,
                                                     &option_cnt);
        if (ret)
                goto out;

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret)
                goto out;
        gf_log ("", GF_LOG_DEBUG, "Performing statedump on volume %s", volname);
        if (strstr (options, "nfs") != NULL) {
                ret = glusterd_nfs_statedump (options, option_cnt, op_errstr);
                if (ret)
                        goto out;

        } else if (strstr (options, "quotad")) {
                ret = glusterd_quotad_statedump (options, option_cnt,
                                                 op_errstr);
                if (ret)
                        goto out;
        } else {
                list_for_each_entry (brickinfo, &volinfo->bricks,
                                                        brick_list) {
                        ret = glusterd_brick_statedump (volinfo, brickinfo,
                                                        options, option_cnt,
                                                        op_errstr);
                        /* Let us take the statedump of other bricks instead of
                         * exiting, if statedump of this brick fails.
                         */
                        if (ret)
                                gf_log (THIS->name, GF_LOG_WARNING, "could not "
                                        "take the statedump of the brick %s:%s."
                                        " Proceeding to other bricks",
                                        brickinfo->hostname, brickinfo->path);
                }
        }

out:
        return ret;
}

int
glusterd_clearlocks_send_cmd (glusterd_volinfo_t *volinfo, char *cmd,
                              char *path, char *result, char *errstr,
                              int err_len, char *mntpt)
{
        int               ret                   = -1;
        glusterd_conf_t  *priv                  = NULL;
        char             abspath[PATH_MAX]      = {0, };

        priv = THIS->private;

        snprintf (abspath, sizeof (abspath), "%s/%s", mntpt, path);
        ret = sys_lgetxattr (abspath, cmd, result, PATH_MAX);
        if (ret < 0) {
                snprintf (errstr, err_len, "clear-locks getxattr command "
                          "failed. Reason: %s", strerror (errno));
                gf_log (THIS->name, GF_LOG_DEBUG, "%s", errstr);
                goto out;
        }

        ret = 0;
out:
        return ret;
}

int
glusterd_clearlocks_rmdir_mount (glusterd_volinfo_t *volinfo, char *mntpt)
{
        int              ret               = -1;
        glusterd_conf_t *priv              = NULL;

        priv = THIS->private;

        ret = rmdir (mntpt);
        if (ret) {
                gf_log (THIS->name, GF_LOG_DEBUG, "rmdir failed");
                goto out;
        }

        ret = 0;
out:
        return ret;
}

void
glusterd_clearlocks_unmount (glusterd_volinfo_t *volinfo, char *mntpt)
{
        glusterd_conf_t  *priv               = NULL;
        runner_t          runner             = {0,};
        int               ret                = 0;

        priv = THIS->private;

        /*umount failures are ignored. Using stat we could have avoided
         * attempting to unmount a non-existent filesystem. But a failure of
         * stat() on mount can be due to network failures.*/

        runinit (&runner);
        runner_add_args (&runner, "/bin/umount", "-f", NULL);
        runner_argprintf (&runner, "%s", mntpt);

        synclock_unlock (&priv->big_lock);
        ret = runner_run (&runner);
        synclock_lock (&priv->big_lock);
        if (ret) {
                ret = 0;
                gf_log ("", GF_LOG_DEBUG,
                        "umount failed on maintenance client");
        }

        return;
}

int
glusterd_clearlocks_create_mount (glusterd_volinfo_t *volinfo, char **mntpt)
{
        int              ret                    = -1;
        glusterd_conf_t *priv                   = NULL;
        char             template[PATH_MAX]     = {0,};
        char            *tmpl                   = NULL;

        priv = THIS->private;

        snprintf (template, sizeof (template), "/tmp/%s.XXXXXX",
                  volinfo->volname);
        tmpl = mkdtemp (template);
        if (!tmpl) {
                gf_log (THIS->name, GF_LOG_DEBUG, "Couldn't create temporary "
                        "mount directory. Reason %s", strerror (errno));
                goto out;
        }

        *mntpt = gf_strdup (tmpl);
        ret = 0;
out:
        return ret;
}

int
glusterd_clearlocks_mount (glusterd_volinfo_t *volinfo, char **xl_opts,
                           char *mntpt)
{
        int             ret                             = -1;
        int             i                               = 0;
        glusterd_conf_t *priv                           = NULL;
        runner_t        runner                          = {0,};
        char            client_volfpath[PATH_MAX]       = {0,};
        char            self_heal_opts[3][1024]      = {"*replicate*.data-self-heal=off",
                                                        "*replicate*.metadata-self-heal=off",
                                                        "*replicate*.entry-self-heal=off"};

        priv = THIS->private;

        runinit (&runner);
        glusterd_get_trusted_client_filepath (client_volfpath, volinfo,
                                      volinfo->transport_type);
        runner_add_args (&runner, SBIN_DIR"/glusterfs", "-f", NULL);
        runner_argprintf (&runner, "%s", client_volfpath);
        runner_add_arg (&runner, "-l");
        runner_argprintf (&runner, DEFAULT_LOG_FILE_DIRECTORY
                          "/%s-clearlocks-mnt.log", volinfo->volname);
        if (volinfo->memory_accounting)
                runner_add_arg (&runner, "--mem-accounting");

        for (i = 0; i < volinfo->brick_count && xl_opts[i]; i++) {
                runner_add_arg (&runner, "--xlator-option");
                runner_argprintf (&runner, "%s", xl_opts[i]);
        }

        for (i = 0; i < 3; i++) {
                runner_add_args (&runner, "--xlator-option",
                                 self_heal_opts[i], NULL);
        }

        runner_argprintf (&runner, "%s", mntpt);
        synclock_unlock (&priv->big_lock);
        ret = runner_run (&runner);
        synclock_lock (&priv->big_lock);
        if (ret) {
                gf_log (THIS->name, GF_LOG_DEBUG,
                        "Could not start glusterfs");
                goto out;
        }
        gf_log (THIS->name, GF_LOG_DEBUG,
                "Started glusterfs successfully");

out:
        return ret;
}

int
glusterd_clearlocks_get_local_client_ports (glusterd_volinfo_t *volinfo,
                                            char **xl_opts)
{
        glusterd_brickinfo_t    *brickinfo      = NULL;
        glusterd_conf_t         *priv           = NULL;
        int                     index           = 0;
        int                     ret             = -1;
        int                     i               = 0;
        int                     port            = 0;

        GF_ASSERT (xl_opts);
        if (!xl_opts) {
                gf_log (THIS->name, GF_LOG_DEBUG, "Should pass non-NULL "
                        "xl_opts");
                goto out;
        }

        priv = THIS->private;

        index = -1;
        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                index++;
                if (uuid_compare (brickinfo->uuid, MY_UUID))
                        continue;

                port = pmap_registry_search (THIS, brickinfo->path,
                                             GF_PMAP_PORT_BRICKSERVER);
                if (!port) {
                        ret = -1;
                        gf_log (THIS->name, GF_LOG_DEBUG, "Couldn't get port "
                                " for brick %s:%s", brickinfo->hostname,
                                brickinfo->path);
                        goto out;
                }

                ret = gf_asprintf (&xl_opts[i], "%s-client-%d.remote-port=%d",
                                   volinfo->volname, index, port);
                if (ret == -1) {
                        xl_opts[i] = NULL;
                        goto out;
                }
                i++;
        }

        ret = 0;
out:
        return ret;
}

int
glusterd_op_clearlocks_volume (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
        int32_t                         ret                 = -1;
        int                             i                   = 0;
        char                            *volname            = NULL;
        char                            *path               = NULL;
        char                            *kind               = NULL;
        char                            *type               = NULL;
        char                            *opts               = NULL;
        char                            *cmd_str            = NULL;
        char                            *free_ptr           = NULL;
        char                            msg[PATH_MAX]       = {0,};
        char                            result[PATH_MAX]    = {0,};
        char                            *mntpt              = NULL;
        char                            **xl_opts           = NULL;
        glusterd_volinfo_t              *volinfo            = NULL;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Failed to get volume name");
                goto out;
        }
        gf_log ("", GF_LOG_DEBUG, "Performing clearlocks on volume %s", volname);

        ret = dict_get_str (dict, "path", &path);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Failed to get path");
                goto out;
        }

        ret = dict_get_str (dict, "kind", &kind);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Failed to get kind");
                goto out;
        }

        ret = dict_get_str (dict, "type", &type);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Failed to get type");
                goto out;
        }

        ret = dict_get_str (dict, "opts", &opts);
        if (ret)
                ret = 0;

        gf_log (THIS->name, GF_LOG_INFO, "Received clear-locks request for "
                "volume %s with kind %s type %s and options %s", volname,
                kind, type, opts);

        if (opts)
                ret = gf_asprintf (&cmd_str, GF_XATTR_CLRLK_CMD".t%s.k%s.%s",
                                   type, kind, opts);
        else
                ret = gf_asprintf (&cmd_str, GF_XATTR_CLRLK_CMD".t%s.k%s",
                                   type, kind);
        if (ret == -1)
                goto out;

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                snprintf (msg, sizeof (msg), "Volume %s doesn't exist.",
                          volname);
                gf_log (THIS->name, GF_LOG_ERROR, "%s", msg);
                goto out;
        }

        xl_opts = GF_CALLOC (volinfo->brick_count+1, sizeof (char*),
                             gf_gld_mt_charptr);
        if (!xl_opts)
                goto out;

        ret = glusterd_clearlocks_get_local_client_ports (volinfo, xl_opts);
        if (ret) {
                snprintf (msg, sizeof (msg), "Couldn't get port numbers of "
                          "local bricks");
                gf_log (THIS->name, GF_LOG_ERROR, "%s", msg);
                goto out;
        }

        ret = glusterd_clearlocks_create_mount (volinfo, &mntpt);
        if (ret) {
                snprintf (msg, sizeof (msg), "Creating mount directory "
                          "for clear-locks failed.");
                gf_log (THIS->name, GF_LOG_ERROR, "%s", msg);
                goto out;
        }

        ret = glusterd_clearlocks_mount (volinfo, xl_opts, mntpt);
        if (ret) {
                snprintf (msg, sizeof (msg), "Failed to mount clear-locks "
                          "maintenance client.");
                gf_log (THIS->name, GF_LOG_ERROR, "%s", msg);
                goto out;
        }

        ret = glusterd_clearlocks_send_cmd (volinfo, cmd_str, path, result,
                                            msg, sizeof (msg), mntpt);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "%s", msg);
                goto umount;
        }

        free_ptr = gf_strdup(result);
        if (dict_set_dynstr (rsp_dict, "lk-summary", free_ptr)) {
                GF_FREE (free_ptr);
                snprintf (msg, sizeof (msg), "Failed to set clear-locks "
                          "result");
                gf_log (THIS->name, GF_LOG_ERROR, "%s", msg);
        }

umount:
        glusterd_clearlocks_unmount (volinfo, mntpt);

        if (glusterd_clearlocks_rmdir_mount (volinfo, mntpt))
                gf_log (THIS->name, GF_LOG_WARNING, "Couldn't unmount "
                        "clear-locks mount point");

out:
        if (ret)
                *op_errstr = gf_strdup (msg);

        if (xl_opts) {
                for (i = 0; i < volinfo->brick_count && xl_opts[i]; i++)
                        GF_FREE (xl_opts[i]);
                GF_FREE (xl_opts);
        }

        GF_FREE (cmd_str);

        GF_FREE (mntpt);

        return ret;
}
