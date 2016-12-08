/*
   Copyright (c) 2011-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifdef HAVE_BD_XLATOR
#include <lvm2app.h>
#endif

#include "common-utils.h"
#include "syscall.h"
#include "cli1-xdr.h"
#include "xdr-generic.h"
#include "glusterd.h"
#include "glusterd-op-sm.h"
#include "glusterd-geo-rep.h"
#include "glusterd-store.h"
#include "glusterd-utils.h"
#include "glusterd-volgen.h"
#include "glusterd-messages.h"
#include "run.h"
#include "glusterd-snapshot-utils.h"
#include "glusterd-svc-mgmt.h"
#include "glusterd-svc-helper.h"
#include "glusterd-shd-svc.h"
#include "glusterd-snapd-svc.h"
#include "glusterd-mgmt.h"
#include "glusterd-server-quorum.h"

#include <stdint.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdlib.h>

#define glusterd_op_start_volume_args_get(dict, volname, flags) \
        glusterd_op_stop_volume_args_get (dict, volname, flags)

gf_ai_compare_t
glusterd_compare_addrinfo (struct addrinfo *first, struct addrinfo *next)
{
        int             ret = -1;
        struct addrinfo *tmp1 = NULL;
        struct addrinfo *tmp2 = NULL;
        char            firstip[NI_MAXHOST] = {0.};
        char            nextip[NI_MAXHOST] = {0,};

        for (tmp1 = first; tmp1 != NULL; tmp1 = tmp1->ai_next) {
                ret = getnameinfo (tmp1->ai_addr, tmp1->ai_addrlen, firstip,
                                   NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
                if (ret)
                        return GF_AI_COMPARE_ERROR;
                for (tmp2 = next; tmp2 != NULL; tmp2 = tmp2->ai_next) {
                      ret = getnameinfo (tmp2->ai_addr, tmp2->ai_addrlen,
                                         nextip, NI_MAXHOST, NULL, 0,
                                         NI_NUMERICHOST);
                      if (ret)
                              return GF_AI_COMPARE_ERROR;
                      if (!strcmp (firstip, nextip)) {
                              return GF_AI_COMPARE_MATCH;
                      }
                }
        }
        return GF_AI_COMPARE_NO_MATCH;
}

/* Check for non optimal brick order for replicate :
 * Checks if bricks belonging to a replicate volume
 * are present on the same server
 */
int32_t
glusterd_check_brick_order(dict_t *dict, char *err_str)
{
        int             ret             = -1;
        int             i               = 0;
        int             j               = 0;
        int             k               = 0;
        xlator_t        *this           = NULL;
        addrinfo_list_t *ai_list        = NULL;
        addrinfo_list_t *ai_list_tmp1   = NULL;
        addrinfo_list_t *ai_list_tmp2   = NULL;
        char            *brick          = NULL;
        char            *brick_list     = NULL;
        char            *brick_list_dup = NULL;
        char            *brick_list_ptr = NULL;
        char            *tmpptr         = NULL;
        char            *volname        = NULL;
        int32_t         brick_count     = 0;
        int32_t         type            = GF_CLUSTER_TYPE_NONE;
        int32_t         sub_count       = 0;
        struct addrinfo *ai_info        = NULL;

        const char      failed_string[2048] = "Failed to perform brick order "
                                "check. Use 'force' at the end of the command"
                                " if you want to override this behavior. ";
        const char      found_string[2048]  = "Multiple bricks of a %s "
                                "volume are present on the same server. This "
                                "setup is not optimal. Use 'force' at the "
                                "end of the command if you want to override "
                                "this behavior. ";

        this = THIS;

        GF_ASSERT(this);

        ai_list = malloc (sizeof (addrinfo_list_t));
        ai_list->info = NULL;
        CDS_INIT_LIST_HEAD (&ai_list->list);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Unable to get volume name");
                goto out;
        }

        ret = dict_get_int32 (dict, "type", &type);
        if (ret) {
                snprintf (err_str, 512, "Unable to get type of volume %s",
                          volname);
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "%s", err_str);
                goto out;
        }

        ret = dict_get_str (dict, "bricks", &brick_list);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Bricks check : Could not "
                        "retrieve bricks list");
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &brick_count);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Bricks check : Could not "
                        "retrieve brick count");
                goto out;
        }

        if (type != GF_CLUSTER_TYPE_DISPERSE) {
                ret = dict_get_int32 (dict, "replica-count", &sub_count);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED, "Bricks check : Could"
                                " not retrieve replica count");
                        goto out;
                }
                gf_msg_debug (this->name, 0, "Replicate cluster type "
                        "found. Checking brick order.");
        } else {
                ret = dict_get_int32 (dict, "disperse-count", &sub_count);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED, "Bricks check : Could"
                                " not retrieve disperse count");
                        goto out;
                }
                gf_msg (this->name, GF_LOG_INFO, 0,
                        GD_MSG_DISPERSE_CLUSTER_FOUND, "Disperse cluster type"
                        " found. Checking brick order.");
        }

        brick_list_dup = brick_list_ptr = gf_strdup(brick_list);
        /* Resolve hostnames and get addrinfo */
        while (i < brick_count) {
                ++i;
                brick = strtok_r (brick_list_dup, " \n", &tmpptr);
                brick_list_dup = tmpptr;
                if (brick == NULL)
                        goto check_failed;
                brick = strtok_r (brick, ":", &tmpptr);
                if (brick == NULL)
                        goto check_failed;
                ret = getaddrinfo (brick, NULL, NULL, &ai_info);
                if (ret != 0) {
                        ret = 0;
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                               GD_MSG_HOSTNAME_RESOLVE_FAIL,
                               "unable to resolve "
                                "host name");
                        goto out;
                }
                ai_list_tmp1 = malloc (sizeof (addrinfo_list_t));
                if (ai_list_tmp1 == NULL) {
                        ret = 0;
                        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                                GD_MSG_NO_MEMORY, "failed to allocate "
                                "memory");
                        goto out;
                }
                ai_list_tmp1->info = ai_info;
                cds_list_add_tail (&ai_list_tmp1->list, &ai_list->list);
                ai_list_tmp1 = NULL;
        }

        i = 0;
        ai_list_tmp1 = cds_list_entry (ai_list->list.next,
                                       addrinfo_list_t, list);

        /* Check for bad brick order */
        while (i < brick_count) {
                ++i;
                ai_info = ai_list_tmp1->info;
                ai_list_tmp1 = cds_list_entry (ai_list_tmp1->list.next,
                                               addrinfo_list_t, list);
                if (0 == i % sub_count) {
                        j = 0;
                        continue;
                }
                ai_list_tmp2 = ai_list_tmp1;
                k = j;
                while (k < sub_count - 1) {
                        ++k;
                        ret = glusterd_compare_addrinfo (ai_info,
                                                         ai_list_tmp2->info);
                        if (GF_AI_COMPARE_ERROR == ret)
                                goto check_failed;
                        if (GF_AI_COMPARE_MATCH == ret)
                                goto found_bad_brick_order;
                        ai_list_tmp2 = cds_list_entry (ai_list_tmp2->list.next,
                                                       addrinfo_list_t, list);
                }
                ++j;
        }
        gf_msg_debug (this->name, 0, "Brick order okay");
        ret = 0;
        goto out;

check_failed:
        gf_msg (this->name, GF_LOG_ERROR, 0,
                GD_MSG_BAD_BRKORDER_CHECK_FAIL, "Failed bad brick order check");
        snprintf (err_str, sizeof (failed_string), failed_string);
        ret = -1;
        goto out;

found_bad_brick_order:
        gf_msg (this->name, GF_LOG_INFO, 0,
                GD_MSG_BAD_BRKORDER, "Bad brick order found");
        if (type == GF_CLUSTER_TYPE_DISPERSE) {
                snprintf (err_str, sizeof (found_string), found_string, "disperse");
        } else {
                snprintf (err_str, sizeof (found_string), found_string, "replicate");
        }

        ret = -1;
out:
        ai_list_tmp2 = NULL;
        GF_FREE (brick_list_ptr);
        cds_list_for_each_entry (ai_list_tmp1, &ai_list->list, list) {
                if (ai_list_tmp1->info)
                          freeaddrinfo (ai_list_tmp1->info);
                free (ai_list_tmp2);
                ai_list_tmp2 = ai_list_tmp1;
        }
        free (ai_list_tmp2);
        return ret;
}

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
        glusterd_conf_t        *conf        = NULL;
        char                   *free_ptr    = NULL;
        char                   *trans_type  = NULL;
        char                   *address_family_str  = NULL;
        uuid_t                  volume_id   = {0,};
        uuid_t                  tmp_uuid    = {0};
        int32_t                 type        = 0;
        char                   *username    = NULL;
        char                   *password    = NULL;

        GF_ASSERT (req);

        this = THIS;
        GF_ASSERT(this);

        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, conf, out);

        ret = -1;
        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                req->rpc_err = GARBAGE_ARGS;
                snprintf (err_str, sizeof (err_str), "Failed to decode request "
                          "received from cli");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "%s", err_str);
                goto out;
        }

        gf_msg_debug (this->name, 0, "Received create volume req");

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL,
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", err_str);
                goto out;
        }

        if ((ret = glusterd_check_volume_exists (volname))) {
                snprintf (err_str, sizeof (err_str), "Volume %s already exists",
                          volname);
                gf_msg (this->name, GF_LOG_ERROR, EEXIST,
                        GD_MSG_VOL_ALREADY_EXIST, "%s", err_str);
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &brick_count);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get brick count"
                          " for volume %s", volname);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", err_str);
                goto out;
        }

        ret = dict_get_int32 (dict, "type", &type);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get type of "
                          "volume %s", volname);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", err_str);
                goto out;
        }



        ret = dict_get_str (dict, "transport", &trans_type);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get "
                          "transport-type of volume %s", volname);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", err_str);
                goto out;
        }

        ret = dict_get_str (this->options, "transport.address-family",
                        &address_family_str);

        if (!ret) {
                ret = dict_set_dynstr_with_alloc (dict,
                                "transport.address-family",
                                address_family_str);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to set transport.address-family");
                        goto out;
                }
        } else if (!strcmp(trans_type, "tcp")) {
                /* Setting default as inet for trans_type tcp if the op-version
                 * is >= 3.8.0
                 */
                if (conf->op_version >= GD_OP_VERSION_3_8_0) {
                        ret = dict_set_dynstr_with_alloc (dict,
                                        "transport.address-family",
                                        "inet");
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "failed to set "
                                        "transport.address-family");
                                goto out;
                        }
                }
        }
        ret = dict_get_str (dict, "bricks", &bricks);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get bricks for "
                          "volume %s", volname);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", err_str);
                goto out;
        }

        if (!dict_get (dict, "force")) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Failed to get 'force' flag");
                goto out;
        }

        gf_uuid_generate (volume_id);
        free_ptr = gf_strdup (uuid_utoa (volume_id));
        ret = dict_set_dynstr (dict, "volume-id", free_ptr);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to set volume "
                          "id of volume %s", volname);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED, "%s", err_str);
                goto out;
        }
        free_ptr = NULL;

        /* generate internal username and password */

        gf_uuid_generate (tmp_uuid);
        username = gf_strdup (uuid_utoa (tmp_uuid));
        ret = dict_set_dynstr (dict, "internal-username", username);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED, "Failed to set username for "
                        "volume %s", volname);
                goto out;
        }

        gf_uuid_generate (tmp_uuid);
        password = gf_strdup (uuid_utoa (tmp_uuid));
        ret = dict_set_dynstr (dict, "internal-password", password);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED, "Failed to set password for "
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
        glusterd_conf_t                 *conf = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);

        conf = this->private;
        GF_ASSERT (conf);
        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                snprintf (errstr, sizeof (errstr), "Failed to decode message "
                        "received from cli");
                req->rpc_err = GARBAGE_ARGS;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "%s", errstr);
                goto out;
        }

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL,
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", errstr);
                goto out;
        }

        gf_msg_debug (this->name, 0, "Received start vol req"
                      " for volume %s", volname);

        if (conf->op_version <= GD_OP_VERSION_3_7_6) {
                gf_msg_debug (this->name, 0, "The cluster is operating at "
                          "version less than or equal to %d. Volume start "
                          "falling back to syncop framework.",
                          GD_OP_VERSION_3_7_6);
                ret = glusterd_op_begin_synctask (req, GD_OP_START_VOLUME,
                                                  dict);
        } else {
                ret = glusterd_mgmt_v3_initiate_all_phases (req,
                                                            GD_OP_START_VOLUME,
                                                            dict);
        }
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "%s", err_str);
                goto out;
        }
        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL,
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", err_str);
                goto out;
        }

        gf_msg_debug (this->name, 0, "Received stop vol req "
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "%s", err_str);
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
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL,
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", err_str);
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_msg_debug (this->name, 0, "Received delete vol req"
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
glusterd_handle_shd_option_for_tier (glusterd_volinfo_t *volinfo,
                                     char *value, dict_t *dict)
{
        int             count           = 0;
        char            dict_key[1024]  = {0, };
        char           *key             = NULL;
        int             ret             = 0;

        key = gd_get_shd_key (volinfo->tier_info.cold_type);
        if (key) {
                count++;
                snprintf (dict_key, sizeof (dict_key), "key%d", count);
                ret = dict_set_str (dict, dict_key, key);
                if (ret)
                        goto out;
                snprintf (dict_key, sizeof (dict_key), "value%d", count);
                ret = dict_set_str (dict, dict_key, value);
                if (ret)
                        goto out;
        }

        key = gd_get_shd_key (volinfo->tier_info.hot_type);
        if (key) {
                count++;
                snprintf (dict_key, sizeof (dict_key), "key%d", count);
                ret = dict_set_str (dict, dict_key, key);
                if (ret)
                        goto out;
                snprintf (dict_key, sizeof (dict_key), "value%d", count);
                ret = dict_set_str (dict, dict_key, value);
                if (ret)
                        goto out;
        }

        ret = dict_set_int32 (dict, "count", count);
        if (ret)
                goto out;

out:
        return ret;
}
static int
glusterd_handle_heal_options_enable_disable (rpcsvc_request_t *req,
                                             dict_t *dict,
                                             glusterd_volinfo_t *volinfo)
{
        gf_xl_afr_op_t                  heal_op = GF_SHD_OP_INVALID;
        int                             ret = 0;
        char                            *key = NULL;
        char                            *value = NULL;

        ret = dict_get_int32 (dict, "heal-op", (int32_t *)&heal_op);
        if (ret || (heal_op == GF_SHD_OP_INVALID)) {
                ret = -1;
                goto out;
        }

        if ((heal_op != GF_SHD_OP_HEAL_ENABLE) &&
            (heal_op != GF_SHD_OP_HEAL_DISABLE) &&
            (heal_op != GF_SHD_OP_GRANULAR_ENTRY_HEAL_ENABLE) &&
            (heal_op != GF_SHD_OP_GRANULAR_ENTRY_HEAL_DISABLE)) {
                ret = -EINVAL;
                goto out;
        }

        if (((heal_op == GF_SHD_OP_GRANULAR_ENTRY_HEAL_ENABLE) ||
            (heal_op == GF_SHD_OP_GRANULAR_ENTRY_HEAL_DISABLE)) &&
            (volinfo->type == GF_CLUSTER_TYPE_DISPERSE)) {
                ret = -1;
                goto out;
        }

        if ((heal_op == GF_SHD_OP_HEAL_ENABLE) ||
            (heal_op == GF_SHD_OP_GRANULAR_ENTRY_HEAL_ENABLE)) {
                value = "enable";
        } else if ((heal_op == GF_SHD_OP_HEAL_DISABLE) ||
                   (heal_op == GF_SHD_OP_GRANULAR_ENTRY_HEAL_DISABLE)) {
                value = "disable";
        }

       /* Convert this command to volume-set command based on volume type */
        if (volinfo->type == GF_CLUSTER_TYPE_TIER) {
                switch (heal_op) {
                case GF_SHD_OP_HEAL_ENABLE:
                case GF_SHD_OP_HEAL_DISABLE:
                        ret = glusterd_handle_shd_option_for_tier (volinfo,
                                                                   value, dict);
                        if (!ret)
                                goto set_volume;
                        goto out;
                        /* For any other heal_op, including granular-entry heal,
                         * just break out of the block but don't goto out yet.
                         */
                default:
                        break;
                }
        }

       if ((heal_op == GF_SHD_OP_HEAL_ENABLE) ||
           (heal_op == GF_SHD_OP_HEAL_DISABLE)) {
                key = volgen_get_shd_key (volinfo->type);
                if (!key) {
                        ret = -1;
                        goto out;
                }
        } else {
                key = "cluster.granular-entry-heal";
                ret = dict_set_int8 (dict, "is-special-key", 1);
                if (ret)
                        goto out;
        }

        ret = dict_set_str (dict, "key1", key);
        if (ret)
                goto out;

        ret = dict_set_str (dict, "value1", value);
        if (ret)
                goto out;

        ret = dict_set_int32 (dict, "count", 1);
        if (ret)
                goto out;

set_volume:
        ret = glusterd_op_begin_synctask (req, GD_OP_SET_VOLUME, dict);

out:
        return ret;
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
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL,
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", op_errstr);
                goto out;
        }

        gf_msg (this->name, GF_LOG_INFO, 0,
                GD_MSG_HEAL_VOL_REQ_RCVD, "Received heal vol req "
                "for volume %s", volname);

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                snprintf (op_errstr, sizeof (op_errstr),
                          "Volume %s does not exist", volname);
                goto out;
        }

        ret = glusterd_handle_heal_options_enable_disable (req, dict, volinfo);
        if (ret == -EINVAL) {
                ret = 0;
        } else {
                /*
                 * If the return value is -ve but not -EINVAL then the command
                 * failed. If the return value is 0 then the synctask for the
                 * op has begun, so in both cases just 'goto out'. If there was
                 * a failure it will respond with an error, otherwise the
                 * synctask will take the responsibility of sending the
                 * response.
                 */
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GLUSTERD_OP_FAILED, "%s", op_errstr);
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
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL,
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", err_str);
                goto out;
        }

        ret = dict_get_str (dict, "options", &options);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get options");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", err_str);
                goto out;
        }

        ret = dict_get_int32 (dict, "option_cnt", &option_cnt);
        if (ret) {
                snprintf (err_str , sizeof (err_str), "Unable to get option "
                          "count");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", err_str);
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

        gf_msg (this->name, GF_LOG_INFO, 0,
                GD_MSG_STATEDUMP_VOL_REQ_RCVD, "Received statedump request for "
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
                                gf_msg (THIS->name, GF_LOG_INFO, 0,
                                        GD_MSG_THINPOOLS_FOR_THINLVS,
                                        "Thin Pool "
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
glusterd_op_stage_create_volume (dict_t *dict, char **op_errstr,
                                 dict_t *rsp_dict)
{
        int                                     ret = 0;
        char                                    *volname = NULL;
        gf_boolean_t                            exists = _gf_false;
        char                                    *bricks = NULL;
        char                                    *brick_list = NULL;
        char                                    *free_ptr = NULL;
        char                                    key[PATH_MAX] = "";
        glusterd_brickinfo_t                    *brick_info = NULL;
        int32_t                                 brick_count = 0;
        int32_t                                 local_brick_count = 0;
        int32_t                                 i = 0;
        int32_t                                 type = 0;
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
        GF_ASSERT (rsp_dict);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get volume name");
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get brick count "
                        "for volume %s", volname);
                goto out;
        }

        ret = dict_get_str (dict, "volume-id", &volume_uuid_str);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get volume id of "
                        "volume %s", volname);
                goto out;
        }

        ret = gf_uuid_parse (volume_uuid_str, volume_uuid);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_UUID_PARSE_FAIL,
                        "Unable to parse volume id of"
                        " volume %s", volname);
                goto out;
        }

        ret = dict_get_str (dict, "bricks", &bricks);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Unable to get bricks for "
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

                if (!glusterd_store_is_valid_brickpath (volname, brick)) {
                        snprintf (msg, sizeof (msg), "brick path %s is too "
                                  "long.", brick);
                        ret = -1;
                        goto out;
                }

                if (!glusterd_is_valid_volfpath (volname, brick)) {
                        snprintf (msg, sizeof (msg), "Volume file path for "
                                  "volume %s and brick path %s is too long.",
                                   volname, brick);
                        ret = -1;
                        goto out;
                }

                ret = glusterd_brickinfo_new_from_brick (brick, &brick_info,
                                                         _gf_true, op_errstr);
                if (ret)
                        goto out;

                ret = glusterd_new_brick_validate (brick, brick_info, msg,
                                                   sizeof (msg), NULL);
                if (ret)
                        goto out;

                ret = glusterd_resolve_brick (brick_info);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                               GD_MSG_RESOLVE_BRICK_FAIL,
                               FMTSTR_RESOLVE_BRICK,
                                brick_info->hostname, brick_info->path);
                        goto out;
                }

                if (!gf_uuid_compare (brick_info->uuid, MY_UUID)) {
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

                        /* A bricks mount dir is required only by snapshots which were
                         * introduced in gluster-3.6.0
                         */
                        if (priv->op_version >= GD_OP_VERSION_3_6_0) {
                                ret = glusterd_get_brick_mount_dir
                                        (brick_info->path, brick_info->hostname,
                                         brick_info->mount_dir);
                                if (ret) {
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_BRICK_MOUNTDIR_GET_FAIL,
                                                "Failed to get brick mount_dir");
                                        goto out;
                                }

                                snprintf (key, sizeof(key), "brick%d.mount_dir",
                                          i);
                                ret = dict_set_dynstr_with_alloc
                                        (rsp_dict, key, brick_info->mount_dir);
                                if (ret) {
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_DICT_SET_FAILED,
                                                "Failed to set %s", key);
                                        goto out;
                                }
                        }
                        local_brick_count = i;

                        brick_list = tmpptr;
                }
                glusterd_brickinfo_delete (brick_info);
                brick_info = NULL;
        }

        /*Check brick order if the volume type is replicate or disperse. If
         * force at the end of command not given then check brick order.
         */
        if (is_origin_glusterd (dict)) {
                ret = dict_get_int32 (dict, "type", &type);
                if (ret) {
                        snprintf (msg, sizeof (msg), "Unable to get type of "
                                  "volume %s", volname);
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_DICT_GET_FAILED, "%s", msg);
                        goto out;
                }

                if (!is_force) {
                        if ((type == GF_CLUSTER_TYPE_REPLICATE) ||
                            (type == GF_CLUSTER_TYPE_STRIPE_REPLICATE) ||
                            (type == GF_CLUSTER_TYPE_DISPERSE)) {
                                ret = glusterd_check_brick_order(dict, msg);
                                if (ret) {
                                        gf_msg(this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_BAD_BRKORDER, "Not "
                                               "creating volume because of bad "
                                               "brick order");
                                        goto out;
                                }
                        }
                }
        }

        ret = dict_set_int32 (rsp_dict, "brick_count", local_brick_count);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Failed to set local_brick_count");
                goto out;
        }
out:
        GF_FREE (free_ptr);
        if (brick_info)
                glusterd_brickinfo_delete (brick_info);

        if (msg[0] != '\0') {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_OP_STAGE_CREATE_VOL_FAIL, "%s", msg);
                *op_errstr = gf_strdup (msg);
        }
        gf_msg_debug (this->name, 0, "Returning %d", ret);

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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get volume name");
                goto out;
        }

        ret = dict_get_int32 (dict, "flags", flags);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get flags");
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
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get volname");
                goto out;
        }

        ret = dict_get_str (dict, "options", options);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get options");
                goto out;
        }

        ret = dict_get_int32 (dict, "option_cnt", option_cnt);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get option count");
                goto out;
        }

out:
        return ret;
}

int
glusterd_op_stage_start_volume (dict_t *dict, char **op_errstr,
                                dict_t *rsp_dict)
{
        int                                     ret = 0;
        char                                    *volname = NULL;
        char                                    key[PATH_MAX] = "";
        int                                     flags = 0;
        int32_t                                 brick_count = 0;
        int32_t                                 local_brick_count = 0;
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
        GF_ASSERT (rsp_dict);

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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLINFO_GET_FAIL, FMTSTR_CHECK_VOL_EXISTS,
                        volname);
                goto out;
        }

        /* This is an incremental approach to have all the volinfo objects ref
         * count. The first attempt is made in volume start transaction to
         * ensure it doesn't race with import volume where stale volume is
         * deleted. There are multiple instances of GlusterD crashing in
         * bug-948686.t because of this. Once this approach is full proof, all
         * other volinfo objects will be refcounted.
         */
        glusterd_volinfo_ref (volinfo);

        if (priv->op_version > GD_OP_VERSION_3_7_5) {
                ret = glusterd_validate_quorum (this, GD_OP_START_VOLUME, dict,
                                                op_errstr);
                if (ret) {
                        gf_msg (this->name, GF_LOG_CRITICAL, 0,
                                GD_MSG_SERVER_QUORUM_NOT_MET,
                                "Server quorum not met. Rejecting operation.");
                        goto out;
                }
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

        cds_list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                brick_count++;
                ret = glusterd_resolve_brick (brickinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_RESOLVE_BRICK_FAIL, FMTSTR_RESOLVE_BRICK,
                                brickinfo->hostname, brickinfo->path);
                        goto out;
                }

                if ((gf_uuid_compare (brickinfo->uuid, MY_UUID)) ||
                    (brickinfo->snap_status == -1))
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
                        if (ret == -1) {
                                snprintf (msg, sizeof (msg), "Failed to set "
                                        "extended attribute %s on %s. Reason: "
                                        "%s", GF_XATTR_VOL_ID_KEY,
                                        brickinfo->path, strerror (errno));
                                goto out;
                        } else {
                                continue;
                        }
                }
                if (gf_uuid_compare (volinfo->volume_id, volume_id)) {
                        snprintf (msg, sizeof (msg), "Volume id mismatch for "
                                  "brick %s:%s. Expected volume id %s, "
                                  "volume id %s found", brickinfo->hostname,
                                  brickinfo->path,
                                  uuid_utoa_r (volinfo->volume_id, volid),
                                  uuid_utoa_r (volume_id, xattr_volid));
                        ret = -1;
                        goto out;
                }

                /* A bricks mount dir is required only by snapshots which were
                 * introduced in gluster-3.6.0
                 */
                if (priv->op_version >= GD_OP_VERSION_3_6_0) {
                        if (strlen(brickinfo->mount_dir) < 1) {
                                ret = glusterd_get_brick_mount_dir
                                        (brickinfo->path, brickinfo->hostname,
                                         brickinfo->mount_dir);
                                if (ret) {
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_BRICK_MOUNTDIR_GET_FAIL,
                                                "Failed to get brick mount_dir");
                                        goto out;
                                }

                                snprintf (key, sizeof(key), "brick%d.mount_dir",
                                          brick_count);
                                ret = dict_set_dynstr_with_alloc
                                        (rsp_dict, key, brickinfo->mount_dir);
                                if (ret) {
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_DICT_SET_FAILED,
                                                "Failed to set %s", key);
                                        goto out;
                                }
                                local_brick_count = brick_count;
                        }
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

        ret = dict_set_int32 (rsp_dict, "brick_count", local_brick_count);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Failed to set local_brick_count");
                goto out;
        }

        volinfo->caps = caps;
        ret = 0;
out:
        if (volinfo)
                glusterd_volinfo_unref (volinfo);

        if (ret && (msg[0] != '\0')) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_OP_STAGE_START_VOL_FAIL, "%s", msg);
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOL_NOT_FOUND, "%s", msg);
                ret = -1;
                goto out;
        }

        ret  = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                snprintf (msg, sizeof (msg), FMTSTR_CHECK_VOL_EXISTS, volname);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLINFO_GET_FAIL, "%s", msg);
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                       GD_MSG_VOL_NOT_STARTED, "%s", msg);
                ret = -1;
                goto out;
        }

        /* If geo-rep is configured, for this volume, it should be stopped. */
        param.volinfo = volinfo;
        ret = glusterd_check_geo_rep_running (&param, op_errstr);
        if (ret || param.is_active) {
                ret = -1;
                goto out;
        }
        ret = glusterd_check_ganesha_export (volinfo);
        if (ret) {
                ret = ganesha_manage_export(dict, "off", op_errstr);
                if (ret) {
                        gf_msg (THIS->name, GF_LOG_WARNING, 0,
                                GD_MSG_NFS_GNS_UNEXPRT_VOL_FAIL, "Could not "
                                        "unexport volume via NFS-Ganesha");
                        ret = 0;
                }
        }

        if (glusterd_is_defrag_on (volinfo)) {
                snprintf (msg, sizeof(msg), "rebalance session is "
                          "in progress for the volume '%s'", volname);
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_OIP, "%s", msg);
                ret = -1;
                goto out;
        }

out:
        if (msg[0] != 0)
                *op_errstr = gf_strdup (msg);
        gf_msg_debug (this->name, 0, "Returning %d", ret);

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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get volume name");
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

        if (volinfo->snap_count > 0 ||
            !cds_list_empty (&volinfo->snap_volumes)) {
                snprintf (msg, sizeof (msg), "Cannot delete Volume %s ,"
                        "as it has %"PRIu64" snapshots. "
                        "To delete the volume, "
                        "first delete all the snapshots under it.",
                          volname, volinfo->snap_count);
                ret = -1;
                goto out;
        }

        if (!glusterd_are_all_peers_up ()) {
                ret = -1;
                snprintf (msg, sizeof(msg), "Some of the peers are down");
                goto out;
        }

        ret = 0;

out:
        if (msg[0] != '\0') {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_OP_STAGE_DELETE_VOL_FAIL, "%s", msg);
                *op_errstr = gf_strdup (msg);
        }
        gf_msg_debug (this->name, 0, "Returning %d", ret);

        return ret;
}

static int
glusterd_handle_heal_cmd (xlator_t *this, glusterd_volinfo_t *volinfo,
                          dict_t *dict, char **op_errstr)
{
        glusterd_conf_t          *priv        = NULL;
        gf_xl_afr_op_t           heal_op      = GF_SHD_OP_INVALID;
        int                      ret          = 0;
        char                     msg[2408]    = {0,};
        char                     *offline_msg = "Self-heal daemon is not running. "
                                      "Check self-heal daemon log file.";

        priv = this->private;
        ret = dict_get_int32 (dict, "heal-op", (int32_t*)&heal_op);
        if (ret) {
                ret = -1;
                *op_errstr = gf_strdup("Heal operation not specified");
                goto out;
        }

        switch (heal_op) {
        case GF_SHD_OP_INVALID:
        case GF_SHD_OP_HEAL_ENABLE: /* This op should be handled in volume-set*/
        case GF_SHD_OP_HEAL_DISABLE:/* This op should be handled in volume-set*/
        case GF_SHD_OP_GRANULAR_ENTRY_HEAL_ENABLE: /* This op should be handled in volume-set */
        case GF_SHD_OP_GRANULAR_ENTRY_HEAL_DISABLE: /* This op should be handled in volume-set */
        case GF_SHD_OP_SBRAIN_HEAL_FROM_BIGGER_FILE:/*glfsheal cmd*/
        case GF_SHD_OP_SBRAIN_HEAL_FROM_LATEST_MTIME:/*glfsheal cmd*/
        case GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK:/*glfsheal cmd*/
                ret = -1;
                *op_errstr = gf_strdup("Invalid heal-op");
                goto out;

        case GF_SHD_OP_HEAL_INDEX:
        case GF_SHD_OP_HEAL_FULL:
                if (!glusterd_is_shd_compatible_volume (volinfo)) {
                        ret = -1;
                        snprintf (msg, sizeof (msg), "Volume %s is not of type "
                                  "replicate or disperse", volinfo->volname);
                        *op_errstr = gf_strdup (msg);
                        goto out;
                }

                if (!priv->shd_svc.online) {
                        ret = -1;
                        *op_errstr = gf_strdup (offline_msg);
                        goto out;
                }
                break;
        case GF_SHD_OP_INDEX_SUMMARY:
        case GF_SHD_OP_SPLIT_BRAIN_FILES:
        case GF_SHD_OP_STATISTICS:
        case GF_SHD_OP_STATISTICS_HEAL_COUNT:
        case GF_SHD_OP_STATISTICS_HEAL_COUNT_PER_REPLICA:
                if (!glusterd_is_volume_replicate (volinfo)) {
                        ret = -1;
                        snprintf (msg, sizeof (msg), "Volume %s is not of type "
                                  "replicate", volinfo->volname);
                        *op_errstr = gf_strdup (msg);
                        goto out;
                }

                if (!priv->shd_svc.online) {
                        ret = -1;
                        *op_errstr = gf_strdup (offline_msg);
                        goto out;
                }
                break;
        case GF_SHD_OP_HEALED_FILES:
        case GF_SHD_OP_HEAL_FAILED_FILES:
                ret = -1;
                snprintf (msg, sizeof (msg), "Command not supported. "
                          "Please use \"gluster volume heal %s info\" "
                          "and logs to find the heal information.",
                          volinfo->volname);
                *op_errstr = gf_strdup (msg);
                goto out;

        }
out:
        if (ret)
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_HANDLE_HEAL_CMD_FAIL, "%s", *op_errstr);
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
        xlator_t                                *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        if (!priv) {
                ret = -1;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_PRIV_NULL,
                        "priv is NULL");
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get volume name");
                goto out;
        }

        ret  = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                ret = -1;
                snprintf (msg, sizeof (msg), "Volume %s does not exist", volname);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOL_NOT_FOUND, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        ret = glusterd_validate_volume_id (dict, volinfo);
        if (ret)
                goto out;

        if (!glusterd_is_volume_started (volinfo)) {
                ret = -1;
                snprintf (msg, sizeof (msg), "Volume %s is not started.",
                          volname);
                gf_msg (THIS->name, GF_LOG_WARNING, 0,
                        GD_MSG_VOL_NOT_STARTED, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        opt_dict = volinfo->dict;
        if (!opt_dict) {
                ret = 0;
                goto out;
        }
        enabled = gd_is_self_heal_enabled (volinfo, opt_dict);
        if (!enabled) {
                ret = -1;
                snprintf (msg, sizeof (msg), "Self-heal-daemon is "
                          "disabled. Heal will not be triggered on volume %s",
                          volname);
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_SELF_HEALD_DISABLED, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        ret = glusterd_handle_heal_cmd (this, volinfo, dict, op_errstr);
        if (ret)
                goto out;

        ret = 0;
out:
        gf_msg_debug ("glusterd", 0, "Returning %d", ret);

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
        gf_msg_debug (this->name, 0, "Returning %d", ret);
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
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        ret = dict_get_str (dict, "path", &path);
        if (ret) {
                snprintf (msg, sizeof(msg), "Failed to get path");
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        ret = dict_get_str (dict, "kind", &kind);
        if (ret) {
                snprintf (msg, sizeof(msg), "Failed to get kind");
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        ret = dict_get_str (dict, "type", &type);
        if (ret) {
                snprintf (msg, sizeof(msg), "Failed to get type");
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                snprintf (msg, sizeof(msg), "Volume %s does not exist",
                          volname);
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_VOL_NOT_FOUND, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        ret = glusterd_validate_volume_id (dict, volinfo);
        if (ret)
                goto out;

        if (!glusterd_is_volume_started (volinfo)) {
                snprintf (msg, sizeof(msg), "Volume %s is not started",
                          volname);
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_VOL_NOT_STARTED, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        ret = 0;
out:
        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
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
        int                   brickid    = 0;
        char                  msg[1024] __attribute__((unused)) = {0, };
        char                 *brick_mount_dir = NULL;
        char                  key[PATH_MAX]   = "";
        char                 *address_family_str = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = glusterd_volinfo_new (&volinfo);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        GD_MSG_NO_MEMORY,
                        "Unable to allocate memory for volinfo");
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get volume name");
                goto out;
        }

        strncpy (volinfo->volname, volname, sizeof(volinfo->volname) - 1);
        GF_ASSERT (volinfo->volname);

        ret = dict_get_int32 (dict, "type", &volinfo->type);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get type of volume"
                        " %s", volname);
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &volinfo->brick_count);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get brick count of"
                        " volume %s", volname);
                goto out;
        }

        ret = dict_get_int32 (dict, "port", &volinfo->port);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get port");
                goto out;
        }

        count = volinfo->brick_count;

        ret = dict_get_str (dict, "bricks", &bricks);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get bricks for "
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
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED, "Failed to get "
                                 "replica count for volume %s", volname);
                        goto out;
                }
                ret = dict_get_int32 (dict, "arbiter-count",
                                      &volinfo->arbiter_count);
        } else if (GF_CLUSTER_TYPE_STRIPE == volinfo->type) {
                ret = dict_get_int32 (dict, "stripe-count",
                                      &volinfo->stripe_count);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED, "Failed to get stripe"
                                " count for volume %s", volname);
                        goto out;
                }
        } else if (GF_CLUSTER_TYPE_STRIPE_REPLICATE == volinfo->type) {
                ret = dict_get_int32 (dict, "stripe-count",
                                      &volinfo->stripe_count);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED, "Failed to get stripe"
                                " count for volume %s", volname);
                        goto out;
                }
                ret = dict_get_int32 (dict, "replica-count",
                                      &volinfo->replica_count);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED, "Failed to get "
                                "replica count for volume %s", volname);
                        goto out;
                }
                ret = dict_get_int32 (dict, "arbiter-count",
                                      &volinfo->arbiter_count);
        } else if (GF_CLUSTER_TYPE_DISPERSE == volinfo->type) {
                ret = dict_get_int32 (dict, "disperse-count",
                                      &volinfo->disperse_count);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED, "Failed to get "
                                 "disperse count for volume %s", volname);
                        goto out;
                }
                ret = dict_get_int32 (dict, "redundancy-count",
                                      &volinfo->redundancy_count);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED, "Failed to get "
                                 "redundancy count for volume %s", volname);
                        goto out;
                }
                if (priv->op_version < GD_OP_VERSION_3_6_0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_UNSUPPORTED_VERSION, "Disperse volume "
                                "needs op-version 3.6.0 or higher");
                        ret = -1;
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Unable to get transport type of volume %s", volname);
                goto out;
        }

        ret = dict_get_str (dict, "volume-id", &str);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Unable to get volume-id of volume %s", volname);
                goto out;
        }
        ret = gf_uuid_parse (str, volinfo->volume_id);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_UUID_PARSE_FAIL,
                        "unable to parse uuid %s of volume %s", str, volname);
                goto out;
        }

        ret = dict_get_str (dict, "internal-username", &username);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "unable to get internal username of volume %s",
                        volname);
                goto out;
        }
        glusterd_auth_set_username (volinfo, username);

        ret = dict_get_str (dict, "internal-password", &password);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "unable to get internal password of volume %s",
                        volname);
                goto out;
        }
        glusterd_auth_set_password (volinfo, password);

        if (strcasecmp (trans_type, "rdma") == 0) {
                volinfo->transport_type = GF_TRANSPORT_RDMA;
        } else if (strcasecmp (trans_type, "tcp") == 0) {
                volinfo->transport_type = GF_TRANSPORT_TCP;
        } else {
                volinfo->transport_type = GF_TRANSPORT_BOTH_TCP_RDMA;
        }

        if (bricks) {
                brick_list = gf_strdup (bricks);
                free_ptr = brick_list;
        }

        if (count)
                brick = strtok_r (brick_list+1, " \n", &saveptr);
        caps = CAPS_BD | CAPS_THIN | CAPS_OFFLOAD_COPY | CAPS_OFFLOAD_SNAPSHOT;

        brickid = glusterd_get_next_available_brickid (volinfo);
        if (brickid < 0)
                goto out;
        while ( i <= count) {
                ret = glusterd_brickinfo_new_from_brick (brick, &brickinfo,
                                                         _gf_true, op_errstr);
                if (ret)
                        goto out;

                GLUSTERD_ASSIGN_BRICKID_TO_BRICKINFO (brickinfo, volinfo,
                                                      brickid++);

                ret = glusterd_resolve_brick (brickinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_RESOLVE_BRICK_FAIL, FMTSTR_RESOLVE_BRICK,
                                brickinfo->hostname, brickinfo->path);
                        goto out;
                }

                /* A bricks mount dir is required only by snapshots which were
                 * introduced in gluster-3.6.0
                 */
                if (priv->op_version >= GD_OP_VERSION_3_6_0) {
                        brick_mount_dir = NULL;
                        snprintf (key, sizeof(key), "brick%d.mount_dir", i);
                        ret = dict_get_str (dict, key, &brick_mount_dir);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_GET_FAILED,
                                        "%s not present", key);
                                goto out;
                        }
                        strncpy (brickinfo->mount_dir, brick_mount_dir,
                                 sizeof(brickinfo->mount_dir));
                }

#ifdef HAVE_BD_XLATOR
                if (!gf_uuid_compare (brickinfo->uuid, MY_UUID)
                    && brickinfo->vg[0]) {
                        ret = glusterd_is_valid_vg (brickinfo, 0, msg);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_INVALID_VG, "%s", msg);
                                goto out;
                        }

                        /* if anyone of the brick does not have thin
                           support, disable it for entire volume */
                        caps &= brickinfo->caps;
                } else {
                                caps = 0;
                }

#endif

                cds_list_add_tail (&brickinfo->brick_list, &volinfo->bricks);
                brick = strtok_r (NULL, " \n", &saveptr);
                i++;
        }

        ret = glusterd_enable_default_options (volinfo, NULL);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_FAIL_DEFAULT_OPT_SET, "Failed to set default "
                        "options on create for volume %s", volinfo->volname);
                goto out;
        }

        ret = dict_get_str (dict, "transport.address-family",
                        &address_family_str);

        if (!ret) {
                ret = dict_set_dynstr_with_alloc(volinfo->dict,
                                "transport.address-family", address_family_str);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to set transport.address-family for %s",
                                        volinfo->volname);
                        goto out;
                }
        }

        gd_update_volume_op_versions (volinfo);

        volinfo->caps = caps;

        ret = glusterd_store_volinfo (volinfo,
                                      GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret) {
                glusterd_store_delete_volume (volinfo);
                *op_errstr = gf_strdup ("Failed to store the "
                                        "Volume information");
                goto out;
        }

        ret = glusterd_create_volfiles_and_notify_services (volinfo);
        if (ret) {
                *op_errstr = gf_strdup ("Failed to create volume files");
                goto out;
        }

        volinfo->rebal.defrag_status = 0;
        glusterd_list_add_order (&volinfo->vol_list, &priv->volumes,
                                 glusterd_compare_volume_name);
        vol_added = _gf_true;

out:
        GF_FREE(free_ptr);
        if (!vol_added && volinfo)
                glusterd_volinfo_unref (volinfo);
        return ret;
}

int
glusterd_start_volume (glusterd_volinfo_t *volinfo, int flags,
                       gf_boolean_t wait)

{
        int                             ret             = 0;
        glusterd_brickinfo_t           *brickinfo       = NULL;
        xlator_t                       *this            = NULL;
        glusterd_volinfo_ver_ac_t       verincrement    = 0;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (volinfo);

        cds_list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                ret = glusterd_brick_start (volinfo, brickinfo, wait);
                /* If 'force' try to start all bricks regardless of success or
                 * failure
                 */
                if (!(flags & GF_CLI_FLAG_OP_FORCE) && ret)
                        goto out;
        }

        /* Increment the volinfo version only if there is a
         * change in status. Force option can be used to start
         * dead bricks even if the volume is in started state.
         * In such case volume status will be GLUSTERD_STATUS_STARTED.
         * Therefore we should not increment the volinfo version.*/
        if (GLUSTERD_STATUS_STARTED != volinfo->status) {
                verincrement = GLUSTERD_VOLINFO_VER_AC_INCREMENT;
        } else {
                verincrement = GLUSTERD_VOLINFO_VER_AC_NONE;
        }

        glusterd_set_volume_status (volinfo, GLUSTERD_STATUS_STARTED);

        ret = glusterd_store_volinfo (volinfo, verincrement);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLINFO_SET_FAIL,
                        "Failed to store volinfo of "
                        "%s volume", volinfo->volname);
                goto out;
        }
out:
        gf_msg_trace (this->name, 0, "returning %d ", ret);
        return ret;
}

int
glusterd_op_start_volume (dict_t *dict, char **op_errstr)
{
        int                         ret             = 0;
        int32_t                     brick_count     = 0;
        char                       *brick_mount_dir = NULL;
        char                        key[PATH_MAX]   = "";
        char                       *volname         = NULL;
        char                       *str             = NULL;
        gf_boolean_t                option          = _gf_false;
        int                         flags           = 0;
        glusterd_volinfo_t         *volinfo         = NULL;
        glusterd_brickinfo_t       *brickinfo       = NULL;
        xlator_t                   *this            = NULL;
        glusterd_conf_t            *conf            = NULL;
        glusterd_svc_t             *svc             = NULL;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        ret = glusterd_op_start_volume_args_get (dict, &volname, &flags);
        if (ret)
                goto out;

        ret  = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOL_NOT_FOUND, FMTSTR_CHECK_VOL_EXISTS,
                        volname);
                goto out;
        }

        /* This is an incremental approach to have all the volinfo objects ref
         * count. The first attempt is made in volume start transaction to
         * ensure it doesn't race with import volume where stale volume is
         * deleted. There are multiple instances of GlusterD crashing in
         * bug-948686.t because of this. Once this approach is full proof, all
         * other volinfo objects will be refcounted.
         */
        glusterd_volinfo_ref (volinfo);

        /* A bricks mount dir is required only by snapshots which were
         * introduced in gluster-3.6.0
         */
        if (conf->op_version >= GD_OP_VERSION_3_6_0) {
                cds_list_for_each_entry (brickinfo, &volinfo->bricks,
                                         brick_list) {
                        brick_count++;
                        /* Don't check bricks that are not owned by you
                         */
                        if (gf_uuid_compare (brickinfo->uuid, MY_UUID))
                                continue;
                        if (strlen(brickinfo->mount_dir) < 1) {
                                brick_mount_dir = NULL;
                                snprintf (key, sizeof(key), "brick%d.mount_dir",
                                          brick_count);
                                ret = dict_get_str (dict, key,
                                                    &brick_mount_dir);
                                if (ret) {
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_DICT_GET_FAILED,
                                                "%s not present", key);
                                        goto out;
                                }
                                strncpy (brickinfo->mount_dir, brick_mount_dir,
                                         sizeof(brickinfo->mount_dir));
                        }
                }
        }

        ret = dict_get_str (conf->opts, GLUSTERD_STORE_KEY_GANESHA_GLOBAL, &str);
        if (ret != 0) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        GD_MSG_DICT_GET_FAILED, "Global dict not present.");
                ret = 0;

        } else {
                ret = gf_string2boolean (str, &option);
                /* Check if the feature is enabled and set nfs-disable to true */
                if (option) {
                        gf_msg_debug (this->name, 0, "NFS-Ganesha is enabled");
                        /* Gluster-nfs should not start when NFS-Ganesha is enabled*/
                        ret = dict_set_str (volinfo->dict, NFS_DISABLE_MAP_KEY, "on");
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_SET_FAILED, "Failed to set nfs.disable for"
                                        "volume %s", volname);
                                goto out;
                        }
                }
        }

        ret = glusterd_start_volume (volinfo, flags, _gf_true);
        if (ret)
                goto out;

        if (!volinfo->is_snap_volume) {
                svc = &(volinfo->snapd.svc);
                ret = svc->manager (svc, volinfo, PROC_START_NO_WAIT);
                if (ret)
                        goto out;
        }
        if (conf->op_version <= GD_OP_VERSION_3_7_6) {
                /*
                 * Starting tier daemon on originator node will fail if
                 * atleast one of the peer host  brick for the volume.
                 * Because The bricks in the peer haven't started when you
                 * commit on originator node.
                 * Please upgrade to version greater than GD_OP_VERSION_3_7_6
                 */
                if (volinfo->type == GF_CLUSTER_TYPE_TIER) {
                        if (volinfo->rebal.op != GD_OP_REMOVE_BRICK) {
                                glusterd_defrag_info_set (volinfo, dict,
                                                GF_DEFRAG_CMD_START_TIER,
                                                GF_DEFRAG_CMD_START,
                                                GD_OP_REBALANCE);
                        }
                        glusterd_restart_rebalance_for_volume (volinfo);
                }
        } else {
                /* Starting tier daemon is moved into post validate phase */
        }


        ret = glusterd_svcs_manager (volinfo);

out:
        if (volinfo)
                glusterd_volinfo_unref (volinfo);

        gf_msg_trace (this->name, 0, "returning %d ", ret);
        return ret;
}

int
glusterd_stop_volume (glusterd_volinfo_t *volinfo)
{
        int                     ret                     = -1;
        glusterd_brickinfo_t    *brickinfo              = NULL;
        char                    mountdir[PATH_MAX]      = {0,};
        char                    pidfile[PATH_MAX]       = {0,};
        xlator_t                *this                   = NULL;
        glusterd_svc_t          *svc                    = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_VALIDATE_OR_GOTO (this->name, volinfo, out);

        cds_list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                ret = glusterd_brick_stop (volinfo, brickinfo, _gf_false);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_BRICK_STOP_FAIL, "Failed to stop "
                                "brick (%s)", brickinfo->path);
                        goto out;
                }
        }

        glusterd_set_volume_status (volinfo, GLUSTERD_STATUS_STOPPED);

        ret = glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLINFO_SET_FAIL, "Failed to store volinfo of "
                        "%s volume", volinfo->volname);
                goto out;
        }

        /* If quota auxiliary mount is present, unmount it */
        GLUSTERFS_GET_AUX_MOUNT_PIDFILE (pidfile, volinfo->volname);

        if (!gf_is_service_running (pidfile, NULL)) {
                gf_msg_debug (this->name, 0, "Aux mount of volume %s "
                        "absent", volinfo->volname);
        } else {
                GLUSTERD_GET_QUOTA_AUX_MOUNT_PATH (mountdir, volinfo->volname,
                                                   "/");

                ret = gf_umount_lazy (this->name, mountdir, 0);
                if (ret)
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_UNOUNT_FAILED,
                                "umount on %s failed",
                                mountdir);
        }

        if (!volinfo->is_snap_volume) {
                svc = &(volinfo->snapd.svc);
                ret = svc->manager (svc, volinfo, PROC_START_NO_WAIT);
                if (ret)
                        goto out;
        }

        if (volinfo->type == GF_CLUSTER_TYPE_TIER) {
                svc = &(volinfo->tierd.svc);
                ret = svc->manager (svc, volinfo, PROC_START_NO_WAIT);
                if (ret)
                        goto out;
        }

        ret = glusterd_svcs_manager (volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOL_GRAPH_CHANGE_NOTIFY_FAIL, "Failed to notify graph "
                        "change for %s volume", volinfo->volname);

                goto out;
        }

out:
        return ret;
}

int
glusterd_op_stop_volume (dict_t *dict)
{
        int                                     ret = 0;
        int                                     flags = 0;
        char                                    *volname = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        xlator_t                                *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = glusterd_op_stop_volume_args_get (dict, &volname, &flags);
        if (ret)
                goto out;

        ret  = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOL_NOT_FOUND, FMTSTR_CHECK_VOL_EXISTS,
                        volname);
                goto out;
        }

        ret = glusterd_stop_volume (volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOL_STOP_FAILED, "Failed to stop %s volume",
                        volname);
                goto out;
        }
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get volume name");
                goto out;
        }

        ret  = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOL_NOT_FOUND, FMTSTR_CHECK_VOL_EXISTS,
                        volname);
                goto out;
        }

        ret = glusterd_remove_auxiliary_mount (volname);
        if (ret)
                goto out;

        ret = glusterd_delete_volume (volinfo);
out:
        gf_msg_debug (this->name, 0, "returning %d", ret);
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
        gf_msg_debug ("glusterd", 0, "Performing statedump on volume %s", volname);
        if (strstr (options, "nfs") != NULL) {
                ret = glusterd_nfs_statedump (options, option_cnt, op_errstr);
                if (ret)
                        goto out;

        } else if (strstr (options, "quotad")) {
                ret = glusterd_quotad_statedump (options, option_cnt,
                                                 op_errstr);
                if (ret)
                        goto out;

        } else if (strstr (options, "client")) {
                ret = glusterd_client_statedump (volname, options, option_cnt,
                                                op_errstr);
                if (ret)
                        goto out;

        } else {
                cds_list_for_each_entry (brickinfo, &volinfo->bricks,
                                         brick_list) {
                        ret = glusterd_brick_statedump (volinfo, brickinfo,
                                                        options, option_cnt,
                                                        op_errstr);
                        /* Let us take the statedump of other bricks instead of
                         * exiting, if statedump of this brick fails.
                         */
                        if (ret)
                                gf_msg (THIS->name, GF_LOG_WARNING, 0,
                                        GD_MSG_BRK_STATEDUMP_FAIL, "could not "
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
        char             abspath[PATH_MAX]      = {0, };

        snprintf (abspath, sizeof (abspath), "%s/%s", mntpt, path);
        ret = sys_lgetxattr (abspath, cmd, result, PATH_MAX);
        if (ret < 0) {
                snprintf (errstr, err_len, "clear-locks getxattr command "
                          "failed. Reason: %s", strerror (errno));
                gf_msg_debug (THIS->name, 0, "%s", errstr);
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

        ret = sys_rmdir (mntpt);
        if (ret) {
                gf_msg_debug (THIS->name, 0, "rmdir failed");
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
        runner_add_args (&runner, _PATH_UMOUNT, "-f", NULL);
        runner_argprintf (&runner, "%s", mntpt);

        synclock_unlock (&priv->big_lock);
        ret = runner_run (&runner);
        synclock_lock (&priv->big_lock);
        if (ret) {
                ret = 0;
                gf_msg_debug ("glusterd", 0,
                        "umount failed on maintenance client");
        }

        return;
}

int
glusterd_clearlocks_create_mount (glusterd_volinfo_t *volinfo, char **mntpt)
{
        int              ret                    = -1;
        char             template[PATH_MAX]     = {0,};
        char            *tmpl                   = NULL;

        snprintf (template, sizeof (template), "/tmp/%s.XXXXXX",
                  volinfo->volname);
        tmpl = mkdtemp (template);
        if (!tmpl) {
                gf_msg_debug (THIS->name, 0, "Couldn't create temporary "
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
                gf_msg_debug (THIS->name, 0,
                        "Could not start glusterfs");
                goto out;
        }
        gf_msg_debug (THIS->name, 0,
                "Started glusterfs successfully");

out:
        return ret;
}

int
glusterd_clearlocks_get_local_client_ports (glusterd_volinfo_t *volinfo,
                                            char **xl_opts)
{
        glusterd_brickinfo_t    *brickinfo          = NULL;
        char                    brickname[PATH_MAX] = {0,};
        int                     index               = 0;
        int                     ret                 = -1;
        int                     i                   = 0;
        int                     port                = 0;

        GF_ASSERT (xl_opts);
        if (!xl_opts) {
                gf_msg_debug (THIS->name, 0, "Should pass non-NULL "
                        "xl_opts");
                goto out;
        }

        index = -1;
        cds_list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                index++;
                if (gf_uuid_compare (brickinfo->uuid, MY_UUID))
                        continue;

                if (volinfo->transport_type == GF_TRANSPORT_RDMA) {
                        snprintf (brickname, sizeof(brickname), "%s.rdma",
                                  brickinfo->path);
                } else
                        snprintf (brickname, sizeof(brickname), "%s",
                                  brickinfo->path);

                port = pmap_registry_search (THIS, brickname,
                                             GF_PMAP_PORT_BRICKSERVER,
                                             _gf_false);
                if (!port) {
                        ret = -1;
                        gf_msg_debug (THIS->name, 0, "Couldn't get port "
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
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Failed to get volume name");
                goto out;
        }
        gf_msg_debug ("glusterd", 0, "Performing clearlocks on volume %s", volname);

        ret = dict_get_str (dict, "path", &path);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Failed to get path");
                goto out;
        }

        ret = dict_get_str (dict, "kind", &kind);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Failed to get kind");
                goto out;
        }

        ret = dict_get_str (dict, "type", &type);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Failed to get type");
                goto out;
        }

        ret = dict_get_str (dict, "opts", &opts);
        if (ret)
                ret = 0;

        gf_msg (THIS->name, GF_LOG_INFO, 0,
                GD_MSG_CLRCLK_VOL_REQ_RCVD,
                "Received clear-locks request for "
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
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOL_NOT_FOUND, "%s", msg);
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
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRK_PORT_NUM_GET_FAIL, "%s", msg);
                goto out;
        }

        ret = glusterd_clearlocks_create_mount (volinfo, &mntpt);
        if (ret) {
                snprintf (msg, sizeof (msg), "Creating mount directory "
                          "for clear-locks failed.");
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_CLRLOCKS_MOUNTDIR_CREATE_FAIL, "%s", msg);
                goto out;
        }

        ret = glusterd_clearlocks_mount (volinfo, xl_opts, mntpt);
        if (ret) {
                snprintf (msg, sizeof (msg), "Failed to mount clear-locks "
                          "maintenance client.");
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_CLRLOCKS_CLNT_MOUNT_FAIL, "%s", msg);
                goto out;
        }

        ret = glusterd_clearlocks_send_cmd (volinfo, cmd_str, path, result,
                                            msg, sizeof (msg), mntpt);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_CLRCLK_SND_CMD_FAIL, "%s", msg);
                goto umount;
        }

        free_ptr = gf_strdup(result);
        if (dict_set_dynstr (rsp_dict, "lk-summary", free_ptr)) {
                GF_FREE (free_ptr);
                snprintf (msg, sizeof (msg), "Failed to set clear-locks "
                          "result");
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED, "%s", msg);
        }

umount:
        glusterd_clearlocks_unmount (volinfo, mntpt);

        if (glusterd_clearlocks_rmdir_mount (volinfo, mntpt))
                gf_msg (THIS->name, GF_LOG_WARNING, 0,
                        GD_MSG_CLRLOCKS_CLNT_UMOUNT_FAIL, "Couldn't unmount "
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
