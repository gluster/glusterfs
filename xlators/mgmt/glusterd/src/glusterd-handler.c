/*
  Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif
#include <inttypes.h>


#include "globals.h"
#include "glusterfs.h"
#include "compat.h"
#include "dict.h"
#include "protocol-common.h"
#include "xlator.h"
#include "logging.h"
#include "timer.h"
#include "defaults.h"
#include "compat.h"
#include "compat-errno.h"
#include "statedump.h"
#include "glusterd-mem-types.h"
#include "glusterd.h"
#include "glusterd-sm.h"
#include "glusterd-op-sm.h"
#include "glusterd-utils.h"
#include "glusterd-store.h"

#include "glusterd1.h"
#include "cli1.h"
#include "rpc-clnt.h"
#include "glusterd1-xdr.h"

#include <sys/resource.h>
#include <inttypes.h>

#include "defaults.c"
#include "common-utils.h"

static int
glusterd_friend_find_by_hostname (const char *hoststr,
                                  glusterd_peerinfo_t  **peerinfo)
{
        int                     ret = -1;
        glusterd_conf_t         *priv = NULL;
        glusterd_peerinfo_t     *entry = NULL;
        glusterd_peer_hostname_t *name = NULL;
        struct addrinfo         *addr = NULL;
        struct addrinfo         *p = NULL;
        char                    *host = NULL;
        struct sockaddr_in6     *s6 = NULL;
        struct sockaddr_in      *s4 = NULL;
        struct in_addr          *in_addr = NULL;
        char                    hname[1024] = {0,};

        GF_ASSERT (hoststr);
        GF_ASSERT (peerinfo);

        *peerinfo = NULL;
        priv    = THIS->private;

        GF_ASSERT (priv);

        list_for_each_entry (entry, &priv->peers, uuid_list) {
                list_for_each_entry (name, &entry->hostnames, hostname_list) {
                        if (!strncmp (name->hostname, hoststr,
                                        1024)) {

                        gf_log ("glusterd", GF_LOG_NORMAL,
                                 "Friend %s found.. state: %d", hoststr,
                                  entry->state.state);
                        *peerinfo = entry;
                        return 0;
                        }
                }
        }

        ret = getaddrinfo(hoststr, NULL, NULL, &addr);
        if (ret != 0) {
                gf_log ("", GF_LOG_ERROR, "error in getaddrinfo: %s\n",
                        gai_strerror(ret));
                goto out;
        }

        for (p = addr; p != NULL; p = p->ai_next) {
                switch (p->ai_family) {
                        case AF_INET:
                                s4 = (struct sockaddr_in *) p->ai_addr;
                                in_addr = &s4->sin_addr;
                                break;
                        case AF_INET6:
                                s6 = (struct sockaddr_in6 *) p->ai_addr;
                                in_addr =(struct in_addr *) &s6->sin6_addr;
                                break;
                       default: ret = -1;
                                goto out;
                }
                host = inet_ntoa(*in_addr);

                ret = getnameinfo (p->ai_addr, p->ai_addrlen, hname,
                                   1024, NULL, 0, 0);
                if (ret)
                        goto out;

                list_for_each_entry (entry, &priv->peers, uuid_list) {
                        list_for_each_entry (name, &entry->hostnames,
                                             hostname_list) {
                                if (!strncmp (name->hostname, host,
                                    1024) || !strncmp (name->hostname,hname,
                                    1024)) {
                                        gf_log ("glusterd", GF_LOG_NORMAL,
                                                "Friend %s found.. state: %d", 
                                                hoststr, entry->state.state);
                                        *peerinfo = entry;
                                        freeaddrinfo (addr);
                                        return 0;
                                }
                        } 
                } 
        }

out:
        if (addr)
                freeaddrinfo (addr); 
        return -1;
}

static int
glusterd_friend_find_by_uuid (uuid_t uuid,
                              glusterd_peerinfo_t  **peerinfo)
{
        int                     ret = -1;
        glusterd_conf_t         *priv = NULL;
        glusterd_peerinfo_t     *entry = NULL;

        GF_ASSERT (peerinfo);

        *peerinfo = NULL;
        priv    = THIS->private;

        GF_ASSERT (priv);

        if (uuid_is_null (uuid))
                return -1;

        list_for_each_entry (entry, &priv->peers, uuid_list) {
                if (!uuid_compare (entry->uuid, uuid)) {

                        gf_log ("glusterd", GF_LOG_NORMAL,
                                 "Friend found.. state: %d",
                                  entry->state.state);
                        *peerinfo = entry;
                        return 0;
                }
        }

        return ret;
}

static int
glusterd_handle_friend_req (rpcsvc_request_t *req, uuid_t  uuid,
                            char *hostname, int port, dict_t *dict)
{
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_friend_req_ctx_t       *ctx = NULL;

        if (!port)
                port = 6969; // TODO: use define values.

        ret = glusterd_friend_find (uuid, hostname, &peerinfo);

        if (ret) {
                gf_log ("glusterd", GF_LOG_NORMAL,
                         "Unable to find peer");

        }

        ret = glusterd_friend_sm_new_event
                        (GD_FRIEND_EVENT_RCVD_FRIEND_REQ, &event);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "event generation failed: %d", ret);
                return ret;
        }

        event->peerinfo = peerinfo;

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_friend_req_ctx_t);

        if (!ctx) {
                gf_log ("", GF_LOG_ERROR, "Unable to allocate memory");
                ret = -1;
                goto out;
        }

        uuid_copy (ctx->uuid, uuid);
        if (hostname)
                ctx->hostname = gf_strdup (hostname);
        ctx->req = req;
        ctx->vols = dict;

        event->ctx = ctx;

        ret = glusterd_friend_sm_inject_event (event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to inject event %d, "
                        "ret = %d", event->event, ret);
                goto out;
        }

        ret = 0;

out:
        if (0 != ret) {
                if (ctx && ctx->hostname)
                        GF_FREE (ctx->hostname);
                if (ctx && ctx->vols)
                        dict_destroy (ctx->vols);
                if (ctx)
                        GF_FREE (ctx);
        }

        return ret;
}


static int
glusterd_handle_unfriend_req (rpcsvc_request_t *req, uuid_t  uuid,
                              char *hostname, int port)
{
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_friend_req_ctx_t       *ctx = NULL;

        if (!port)
                port = 6969; //TODO: use define'd macro

        ret = glusterd_friend_find (uuid, hostname, &peerinfo);

        if (ret) {
                gf_log ("glusterd", GF_LOG_NORMAL,
                         "Unable to find peer");

        }

        ret = glusterd_friend_sm_new_event
                        (GD_FRIEND_EVENT_RCVD_REMOVE_FRIEND, &event);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "event generation failed: %d", ret);
                return ret;
        }

        event->peerinfo = peerinfo;

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_friend_req_ctx_t);

        if (!ctx) {
                gf_log ("", GF_LOG_ERROR, "Unable to allocate memory");
                ret = -1;
                goto out;
        }

        uuid_copy (ctx->uuid, uuid);
        if (hostname)
                ctx->hostname = gf_strdup (hostname);
        ctx->req = req;

        event->ctx = ctx;

        ret = glusterd_friend_sm_inject_event (event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to inject event %d, "
                        "ret = %d", event->event, ret);
                goto out;
        }

        ret = 0;

out:
        if (0 != ret) {
                if (ctx && ctx->hostname)
                        GF_FREE (ctx->hostname);
                if (ctx)
                        GF_FREE (ctx);
        }

        return ret;
}

static int
glusterd_add_peer_detail_to_dict (glusterd_peerinfo_t   *peerinfo,
                                  dict_t  *friends, int   count)
{

        int             ret = -1;
        char            key[256] = {0, };

        GF_ASSERT (peerinfo);
        GF_ASSERT (friends);

        snprintf (key, 256, "friend%d.uuid", count);
        uuid_unparse (peerinfo->uuid, peerinfo->uuid_str);
        ret = dict_set_str (friends, key, peerinfo->uuid_str);
        if (ret)
                goto out;

        snprintf (key, 256, "friend%d.hostname", count);
        ret = dict_set_str (friends, key, peerinfo->hostname);
        if (ret)
                goto out;

        snprintf (key, 256, "friend%d.port", count);
        ret = dict_set_int32 (friends, key, peerinfo->port);
        if (ret)
                goto out;

        snprintf (key, 256, "friend%d.state", count);
        ret = dict_set_int32 (friends, key, (int32_t)peerinfo->state.state);
        if (ret)
                goto out;

        snprintf (key, 256, "friend%d.connected", count);
        ret = dict_set_int32 (friends, key, (int32_t)peerinfo->connected);
        if (ret)
                goto out;

out:
        return ret;
}

int
glusterd_add_volume_detail_to_dict (glusterd_volinfo_t *volinfo,
                                    dict_t  *volumes, int   count)
{

        int                     ret = -1;
        char                    key[256] = {0, };
        glusterd_brickinfo_t    *brickinfo = NULL;
        char                    *buf = NULL;
        int                     i = 1;

        GF_ASSERT (volinfo);
        GF_ASSERT (volumes);

        snprintf (key, 256, "volume%d.name", count);
        ret = dict_set_str (volumes, key, volinfo->volname);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.type", count);
        ret = dict_set_int32 (volumes, key, volinfo->type);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.status", count);
        ret = dict_set_int32 (volumes, key, volinfo->status);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.brick_count", count);
        ret = dict_set_int32 (volumes, key, volinfo->brick_count);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.transport", count);
        ret = dict_set_int32 (volumes, key, volinfo->transport_type);
        if (ret)
                goto out;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                char    brick[1024] = {0,};
                snprintf (key, 256, "volume%d.brick%d", count, i);
                snprintf (brick, 1024, "%s:%s", brickinfo->hostname,
                          brickinfo->path);
                buf = gf_strdup (brick);
                ret = dict_set_dynstr (volumes, key, buf);
                if (ret)
                        goto out;
                i++;
        }
out:
        return ret;
}

int
glusterd_friend_find (uuid_t uuid, char *hostname,
                      glusterd_peerinfo_t **peerinfo)
{
        int     ret = -1;

        if (uuid) {
                ret = glusterd_friend_find_by_uuid (uuid, peerinfo);

                if (ret) {
                        gf_log ("glusterd", GF_LOG_NORMAL,
                                 "Unable to find peer by uuid");
                } else {
                        goto out;
                }

        }

        if (hostname) {
                ret = glusterd_friend_find_by_hostname (hostname, peerinfo);

                if (ret) {
                        gf_log ("glusterd", GF_LOG_NORMAL,
                                "Unable to find hostname: %s", hostname);
                } else {
                        goto out;
                }
        }

out:
        return ret;
}

int
glusterd_handle_cluster_lock (rpcsvc_request_t *req)
{
        gd1_mgmt_cluster_lock_req       lock_req = {{0},};
        int32_t                         ret = -1;
        char                            str[50] = {0,};
        glusterd_op_lock_ctx_t          *ctx = NULL;

        GF_ASSERT (req);

        if (!gd_xdr_to_mgmt_cluster_lock_req (req->msg[0], &lock_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }
        uuid_unparse (lock_req.uuid, str);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received LOCK from uuid: %s", str);


        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_op_lock_ctx_t);

        if (!ctx) {
                //respond here
                return -1;
        }

        uuid_copy (ctx->uuid, lock_req.uuid);
        ctx->req = req;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_LOCK, ctx);

out:
        gf_log ("", GF_LOG_NORMAL, "Returning %d", ret);

        return ret;
}

int
glusterd_handle_stage_op (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        char                            str[50] = {0,};
        gd1_mgmt_stage_op_req           stage_req = {{0,}};
        glusterd_op_stage_ctx_t         *ctx = NULL;

        GF_ASSERT (req);

        if (!gd_xdr_to_mgmt_stage_op_req (req->msg[0], &stage_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        uuid_unparse (stage_req.uuid, str);
        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received stage op from uuid: %s", str);

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_op_stage_ctx_t);

        if (!ctx) {
                //respond here
                return -1;
        }

        //CHANGE THIS
        uuid_copy (ctx->stage_req.uuid, stage_req.uuid);
        ctx->stage_req.op = stage_req.op;
        ctx->stage_req.buf.buf_len = stage_req.buf.buf_len;
        ctx->stage_req.buf.buf_val = GF_CALLOC (1, stage_req.buf.buf_len,
                                                gf_gld_mt_string);
        if (!ctx->stage_req.buf.buf_val)
                goto out;

        memcpy (ctx->stage_req.buf.buf_val, stage_req.buf.buf_val,
                stage_req.buf.buf_len);

        ctx->req   = req;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_STAGE_OP, ctx);

out:
        if (stage_req.buf.buf_val)
                free (stage_req.buf.buf_val);//malloced by xdr
        return ret;
}

int
glusterd_handle_commit_op (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        char                            str[50] = {0,};
        gd1_mgmt_commit_op_req          commit_req = {{0},};
        glusterd_op_commit_ctx_t        *ctx = NULL;

        GF_ASSERT (req);

        if (!gd_xdr_to_mgmt_commit_op_req (req->msg[0], &commit_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        uuid_unparse (commit_req.uuid, str);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received commit op from uuid: %s", str);

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_op_commit_ctx_t);

        if (!ctx) {
                //respond here
                return -1;
        }

        ctx->req = req;
        //CHANGE THIS
        uuid_copy (ctx->stage_req.uuid, commit_req.uuid);
        ctx->stage_req.op = commit_req.op;
        ctx->stage_req.buf.buf_len = commit_req.buf.buf_len;
        ctx->stage_req.buf.buf_val = GF_CALLOC (1, commit_req.buf.buf_len,
                                                gf_gld_mt_string);
        if (!ctx->stage_req.buf.buf_val)
                goto out;

        memcpy (ctx->stage_req.buf.buf_val, commit_req.buf.buf_val,
                commit_req.buf.buf_len);

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_COMMIT_OP, ctx);

out:
        if (commit_req.buf.buf_val)
                free (commit_req.buf.buf_val);//malloced by xdr
        return ret;
}

int
glusterd_handle_cli_probe (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_probe_req               cli_req = {0,};
        glusterd_peerinfo_t             *peerinfo = NULL;
        GF_ASSERT (req);

        if (!gf_xdr_to_cli_probe_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                gf_log ("", GF_LOG_ERROR, "xdr decoding error");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_cmd_log ("peer probe", " on host %s:%d", cli_req.hostname,
                    cli_req.port);
        gf_log ("glusterd", GF_LOG_NORMAL, "Received CLI probe req %s %d",
                cli_req.hostname, cli_req.port);

        if (!(ret = glusterd_is_local_addr(cli_req.hostname))) {
                glusterd_xfer_cli_probe_resp (req, 0, GF_PROBE_LOCALHOST,
                                              cli_req.hostname, cli_req.port);
                goto out;
        }
        if (!(ret = glusterd_friend_find_by_hostname(cli_req.hostname,
                                         &peerinfo))) {
                if ((peerinfo->state.state != GD_FRIEND_STATE_REQ_RCVD)
                    || (peerinfo->state.state != GD_FRIEND_STATE_DEFAULT)) {

                        gf_log ("glusterd", GF_LOG_NORMAL, "Probe host %s port %d"
                               "already a friend", cli_req.hostname, cli_req.port);
                        glusterd_xfer_cli_probe_resp (req, 0, GF_PROBE_FRIEND,
                                                      cli_req.hostname, cli_req.port);
                        goto out;
                }
        }
        ret = glusterd_probe_begin (req, cli_req.hostname, cli_req.port);

        gf_cmd_log ("peer probe","on host %s:%d %s",cli_req.hostname, cli_req.port,
                    (ret) ? "FAILED" : "SUCCESS");
out:
        if (cli_req.hostname)
                free (cli_req.hostname);//its malloced by xdr
        return ret;
}

int
glusterd_handle_cli_deprobe (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_probe_req               cli_req = {0,};

        GF_ASSERT (req);

        if (!gf_xdr_to_cli_probe_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received CLI deprobe req");


        ret = glusterd_deprobe_begin (req, cli_req.hostname, cli_req.port);

        gf_cmd_log ("peer deprobe", "on host %s:%d %s", cli_req.hostname,
                    cli_req.port, (ret) ? "FAILED" : "SUCCESS");
out:
        if (cli_req.hostname)
                free (cli_req.hostname);//malloced by xdr
        return ret;
}

int
glusterd_handle_cli_list_friends (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_peer_list_req           cli_req = {0,};
        dict_t                          *dict = NULL;

        GF_ASSERT (req);

        if (!gf_xdr_to_cli_peer_list_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received cli list req");

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = glusterd_list_friends (req, dict, cli_req.flags);

out:
        if (dict)
                dict_unref (dict);
        return ret;
}

int
glusterd_check_and_rebalance (glusterd_volinfo_t *volinfo, char *dir)
{
        int                     ret                = -1;
        int                     dst_fd             = -1;
        int                     src_fd             = -1;
        DIR                    *fd                 = NULL;
        glusterd_defrag_info_t *defrag             = NULL;
        struct dirent          *entry              = NULL;
        struct stat             stbuf              = {0,};
        struct stat             new_stbuf          = {0,};
        char                    full_path[1024]    = {0,};
        char                    tmp_filename[1024] = {0,};
        char                    value[128]         = {0,};

        defrag = volinfo->defrag;
        if (!defrag)
                goto out;


        /* Fix files at this level */
        fd = opendir (dir);
        if (!fd)
                goto out;
        while ((entry = readdir (fd))) {
                if (!entry)
                        break;

                if (!strcmp (entry->d_name, ".") || !strcmp (entry->d_name, ".."))
                        continue;

                snprintf (full_path, 1024, "%s/%s", dir, entry->d_name);

                ret = stat (full_path, &stbuf);
                if (ret == -1)
                        continue;

                if (S_ISDIR (stbuf.st_mode)) {
                        /* Fix the layout of the directory */
                        getxattr (full_path, "trusted.distribute.fix.layout",
                                  &value, 128);
                        continue;
                }
                if (S_ISREG (stbuf.st_mode) && ((stbuf.st_mode & 01000) == 01000)) {
                        /* TODO: run the defrag */
                        snprintf (tmp_filename, 1024, "%s/.%s.gfs%llu", dir,
                                  entry->d_name,
                                  (unsigned long long)stbuf.st_size);

                        dst_fd = creat (tmp_filename, (stbuf.st_mode & ~01000));
                        if (dst_fd == -1)
                                continue;

                        src_fd = open (full_path, O_RDONLY);
                        if (src_fd == -1) {
                                close (dst_fd);
                                continue;
                        }

                        while (1) {
                                ret = read (src_fd, defrag->databuf, 131072);
                                if (!ret || (ret < 0)) {
                                        close (dst_fd);
                                        close (src_fd);
                                        break;
                                }
                                ret = write (dst_fd, defrag->databuf, ret);
                                if (ret < 0) {
                                        close (dst_fd);
                                        close (src_fd);
                                        break;
                                }
                        }

                        ret = stat (full_path, &new_stbuf);
                        if (ret < 0)
                                continue;
                        if (new_stbuf.st_mtime != stbuf.st_mtime)
                                continue;

                        ret = rename (tmp_filename, full_path);
                        if (ret != -1) {
                                LOCK (&defrag->lock);
                                {
                                        defrag->total_files += 1;
                                        defrag->total_data += stbuf.st_size;
                                }
                                UNLOCK (&defrag->lock);
                        }
                } else {
                        LOCK (&defrag->lock);
                        {
                                if (S_ISREG (stbuf.st_mode))
                                        defrag->num_files_lookedup += 1;
                        }
                        UNLOCK (&defrag->lock);
                }

                if (volinfo->defrag_status == GF_DEFRAG_STATUS_STOPED) {
                        closedir (fd);
                        goto out;
                }
        }
        closedir (fd);

        /* Iterate over directories */
        fd = opendir (dir);
        if (!fd)
                goto out;
        while ((entry = readdir (fd))) {
                if (!entry)
                        break;

                if (!strcmp (entry->d_name, ".") || !strcmp (entry->d_name, ".."))
                        continue;

                snprintf (full_path, 1024, "%s/%s", dir, entry->d_name);

                ret = stat (full_path, &stbuf);
                if (ret == -1)
                        continue;

                if (S_ISDIR (stbuf.st_mode)) {
                        /* iterate in subdirectories */
                        ret = glusterd_check_and_rebalance (volinfo, full_path);
                        if (ret)
                                break;
                }
        }

        closedir (fd);

        if (!entry)
                ret = 0;
out:
        return ret;
}

void *
glusterd_defrag_start (void *data)
{
        glusterd_volinfo_t     *volinfo = data;
        glusterd_defrag_info_t *defrag  = NULL;
        char                    cmd_str[1024] = {0,};
        int                     ret     = -1;
        struct stat             stbuf   = {0,};
        char                    value[128] = {0,};

        defrag = volinfo->defrag;
        if (!defrag)
                goto out;

        sleep (1);
        ret = stat (defrag->mount, &stbuf);
        if ((ret == -1) && (errno == ENOTCONN)) {
                /* Wait for some more time before starting rebalance */
                sleep (2);
                ret = stat (defrag->mount, &stbuf);
                if (ret == -1) {
                        volinfo->defrag_status   = GF_DEFRAG_STATUS_FAILED;
                        volinfo->rebalance_files = 0;
                        volinfo->rebalance_data  = 0;
                        volinfo->lookedup_files  = 0;
                        goto out;
                }
        }

        /* Fix the root ('/') first */
        getxattr (defrag->mount, "trusted.distribute.fix.layout", &value, 128);

        ret = glusterd_check_and_rebalance (volinfo, defrag->mount);

        /* TODO: This should run in a thread, and finish the thread when
           the task is complete. While defrag is running, keep updating
           files */

        volinfo->defrag_status   = GF_DEFRAG_STATUS_COMPLETE;
        volinfo->rebalance_files = defrag->total_files;
        volinfo->rebalance_data  = defrag->total_data;
        volinfo->lookedup_files  = defrag->num_files_lookedup;
out:
        if (defrag) {
                gf_log ("defrag", GF_LOG_NORMAL, "defrag on %s complete",
                        defrag->mount);

                snprintf (cmd_str, 1024, "umount -l %s", defrag->mount);
                ret = system (cmd_str);
                LOCK_DESTROY (&defrag->lock);
                GF_FREE (defrag);
        }
        volinfo->defrag = NULL;

        return NULL;
}

int
glusterd_defrag_stop (glusterd_volinfo_t *volinfo,
                      gf1_cli_defrag_vol_rsp *rsp)
{
        /* TODO: set a variaeble 'stop_defrag' here, it should be checked
           in defrag loop */
        if (!volinfo || !volinfo->defrag)
                goto out;

        LOCK (&volinfo->defrag->lock);
        {
                volinfo->defrag_status = GF_DEFRAG_STATUS_STOPED;
                rsp->files = volinfo->defrag->total_files;
                rsp->size = volinfo->defrag->total_data;
        }
        UNLOCK (&volinfo->defrag->lock);

        rsp->op_ret = 0;
out:
        return 0;
}

int
glusterd_defrag_status_get (glusterd_volinfo_t *volinfo,
                            gf1_cli_defrag_vol_rsp *rsp)
{
        if (!volinfo)
                goto out;

        if (volinfo->defrag) {
                LOCK (&volinfo->defrag->lock);
                {
                        rsp->files = volinfo->defrag->total_files;
                        rsp->size = volinfo->defrag->total_data;
                        rsp->lookedup_files = volinfo->defrag->num_files_lookedup;
                }
                UNLOCK (&volinfo->defrag->lock);
        } else {
                rsp->files = volinfo->rebalance_files;
                rsp->size  = volinfo->rebalance_data;
                rsp->lookedup_files = volinfo->lookedup_files;
        }

        rsp->op_errno = volinfo->defrag_status;
        rsp->op_ret = 0;
out:
        return 0;
}

int
glusterd_handle_defrag_volume (rpcsvc_request_t *req)
{
        int32_t                ret           = -1;
        gf1_cli_defrag_vol_req cli_req       = {0,};
        glusterd_conf_t         *priv = NULL;
        char                   cmd_str[4096] = {0,};
        glusterd_volinfo_t      *volinfo = NULL;
        glusterd_defrag_info_t *defrag =  NULL;
        gf1_cli_defrag_vol_rsp rsp = {0,};

        GF_ASSERT (req);

        priv    = THIS->private;
        if (!gf_xdr_to_cli_defrag_vol_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        switch (cli_req.cmd) {
                case GF_DEFRAG_CMD_START:
                        gf_cmd_log ("Volume rebalance"," on volname: %s "
                                    "cmd: start, attempted", cli_req.volname);
                        break;
                case GF_DEFRAG_CMD_STOP:
                        gf_cmd_log ("Volume rebalance"," on volname: %s "
                                    "cmd: stop, attempted", cli_req.volname);
                        break;
                default:
                        break;
        }
        gf_log ("glusterd", GF_LOG_NORMAL, "Received defrag volume on %s",
                cli_req.volname);

        rsp.volname = cli_req.volname;
        rsp.op_ret = -1;
        if (glusterd_volinfo_find(cli_req.volname, &volinfo)) {
                gf_log ("glusterd", GF_LOG_NORMAL, "Received defrag on invalid"
                        " volname %s", cli_req.volname);
                goto out;
        }

        if (volinfo->status != GLUSTERD_STATUS_STARTED) {
                gf_log ("glusterd", GF_LOG_NORMAL, "Received defrag on stopped"
                        " volname %s", cli_req.volname);
                goto out;
        }

        switch (cli_req.cmd) {
        case GF_DEFRAG_CMD_START:
        {
                if (volinfo->defrag) {
                        gf_log ("glusterd", GF_LOG_DEBUG,
                                "defrag on volume %s already started",
                                cli_req.volname);
                        goto out;
                }

                volinfo->defrag = GF_CALLOC (1, sizeof (glusterd_defrag_info_t),
                                             gf_gld_mt_defrag_info);
                if (!volinfo->defrag)
                        goto out;

                defrag = volinfo->defrag;

                LOCK_INIT (&defrag->lock);
                snprintf (defrag->mount, 1024, "%s/mount/%s",
                          priv->workdir, cli_req.volname);
                /* Create a directory, mount glusterfs over it, start glusterfs-defrag */
                snprintf (cmd_str, 4096, "mkdir -p %s", defrag->mount);
                ret = system (cmd_str);

                if (ret) {
                        gf_log("glusterd", GF_LOG_DEBUG, "command: %s failed", cmd_str);
                        goto out;
                }

                snprintf (cmd_str, 4096, "%s/sbin/glusterfs -s localhost "
                          "--volfile-id %s --volume-name %s-quick-read "
                          "--xlator-option *dht.unhashed-sticky-bit=yes "
                          "--xlator-option *dht.use-readdirp=yes "
                          "--xlator-option *dht.lookup-unhashed=yes %s",
                          GFS_PREFIX, cli_req.volname, cli_req.volname,
                          defrag->mount);
                ret = gf_system (cmd_str);
                if (ret) {
                        gf_log("glusterd", GF_LOG_DEBUG, "command: %s failed", cmd_str);
                        goto out;
                }

                volinfo->defrag_status = GF_DEFRAG_STATUS_STARTED;
                rsp.op_ret = 0;

                ret = pthread_create (&defrag->th, NULL, glusterd_defrag_start,
                                      volinfo);
                if (ret) {
                        snprintf (cmd_str, 1024, "umount -l %s", defrag->mount);
                        ret = system (cmd_str);
                        rsp.op_ret = -1;
                }
                break;
        }
        case GF_DEFRAG_CMD_STOP:
                ret = glusterd_defrag_stop (volinfo, &rsp);
                break;
        case GF_DEFRAG_CMD_STATUS:
                ret = glusterd_defrag_status_get (volinfo, &rsp);
                break;
        default:
                break;
        }
        if (ret)
                gf_log("glusterd", GF_LOG_DEBUG, "command: %s failed",cmd_str);

        if (cli_req.cmd != GF_DEFRAG_CMD_STATUS) {
                gf_cmd_log ("volume rebalance"," on volname: %s %d %s",
                            cli_req.volname,
                            cli_req.cmd, ((ret)?"FAILED":"SUCCESS"));
        }

out:

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gf_xdr_serialize_cli_defrag_vol_rsp);
        if (cli_req.volname)
                free (cli_req.volname);//malloced by xdr
        return ret;
}

int
glusterd_handle_cli_get_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_get_vol_req             cli_req = {0,};
        dict_t                          *dict = NULL;

        GF_ASSERT (req);

        if (!gf_xdr_to_cli_get_vol_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received get vol req");

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = glusterd_get_volumes (req, dict, cli_req.flags);

out:
        if (dict)
                dict_unref (dict);
        return ret;
}

int
glusterd_handle_create_volume (rpcsvc_request_t *req)
{
        int32_t                 ret         = -1;
        gf1_cli_create_vol_req  cli_req     = {0,};
        dict_t                 *dict        = NULL;
        glusterd_brickinfo_t   *brickinfo   = NULL;
        char                   *brick       = NULL;
        char                   *bricks      = NULL;
        char                   *volname     = NULL;
        int                    brick_count = 0;
        char                   *tmpptr      = NULL;
        int                    i           = 0;
        glusterd_peerinfo_t    *peerinfo    = NULL;
        char                   *brick_list  = NULL;
        void                   *cli_rsp     = NULL;
        char                    err_str[1048];
        gf1_cli_create_vol_rsp  rsp         = {0,};
        glusterd_conf_t        *priv        = NULL;
        int                     err_ret     = 0;
        xlator_t               *this        = NULL;
        char                   *free_ptr    = NULL;
        char                   *trans_type  = NULL;
        uuid_t                  volume_id   = {0,};
        char                    volid[64]   = {0,};

        GF_ASSERT (req);

        this = THIS;
        GF_ASSERT(this);

        priv = this->private;

        if (!gf_xdr_to_cli_create_vol_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received create volume req");

        if (cli_req.bricks.bricks_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.bricks.bricks_val,
                                        cli_req.bricks.bricks_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.bricks.bricks_val;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }
        gf_cmd_log ("Volume create", "on volname: %s attempted", volname);

        if ((ret = glusterd_check_volume_exists (volname))) {
                snprintf(err_str, 1048, "Volname %s already exists",
                         volname);
                gf_log ("glusterd", GF_LOG_ERROR, "%s", err_str);
                err_ret = 1;
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &brick_count);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get count");
                goto out;
        }

        ret = dict_get_str (dict, "transport", &trans_type);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get transport-type");
                goto out;
        }
        ret = dict_get_str (dict, "bricks", &bricks);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get bricks");
                goto out;
        }

        uuid_generate (volume_id);
        uuid_unparse (volume_id, volid);
        free_ptr = gf_strdup (volid);
        ret = dict_set_dynstr (dict, "volume-id", free_ptr);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "unable to set volume-id");
                goto out;
        }
        free_ptr = NULL;

        if (bricks) {
                brick_list = gf_strdup (bricks);
                free_ptr = brick_list;
        }

        gf_cmd_log ("Volume create", "on volname: %s type:%s count:%d bricks:%s",
                    cli_req.volname, ((cli_req.type == 0)? "DEFAULT":
                    ((cli_req.type == 1)? "STRIPE":"REPLICATE")), cli_req.count,
                    bricks);

        while ( i < brick_count) {
                i++;
                brick= strtok_r (brick_list, " \n", &tmpptr);
                brick_list = tmpptr;
                if (brickinfo)
                        glusterd_brickinfo_delete (brickinfo);
                ret = glusterd_brickinfo_from_brick (brick, &brickinfo);
                if (ret)
                        goto out;

                if(!(ret = glusterd_is_local_addr (brickinfo->hostname)))
                        goto brick_validation;        //localhost, continue without validation

                ret = glusterd_friend_find_by_hostname (brickinfo->hostname,
                                                        &peerinfo);
                if (ret) {
                        snprintf (err_str, 1048, "Host %s not a friend",
                                  brickinfo->hostname);
                        gf_log ("glusterd", GF_LOG_ERROR, "%s", err_str);
                        err_ret = 1;
                        goto out;
                }
                if ((!peerinfo->connected) ||
                    (peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED)) {
                        snprintf(err_str, 1048, "Host %s not connected",
                                 brickinfo->hostname);
                        gf_log ("glusterd", GF_LOG_ERROR, "%s", err_str);
                        err_ret = 1;
                        goto out;
                }
brick_validation:
                err_ret = glusterd_is_exisiting_brick (brickinfo->hostname,
                                                       brickinfo->path);
                if (err_ret) {
                        snprintf(err_str, 1048, "Brick: %s already in use",
                                 brick);
                        goto out;
                }
        }
        ret = glusterd_create_volume (req, dict);

        gf_cmd_log ("Volume create", "on volname: %s %s", volname,
                    ((ret || err_ret) != 0) ? "FAILED": "SUCCESS");

out:
        if ((err_ret || ret) && dict)
                dict_unref (dict);
        if (err_ret) {
                rsp.op_ret = -1;
                rsp.op_errno = 0;
                rsp.volname = "";
                rsp.op_errstr = err_str;
                cli_rsp = &rsp;
                glusterd_submit_reply(req, cli_rsp, NULL, 0, NULL,
                                      gf_xdr_serialize_cli_create_vol_rsp);
                if (!glusterd_opinfo_unlock())
                        gf_log ("glusterd", GF_LOG_ERROR, "Unlock on opinfo"
                                " failed");
                ret = 0; //Client response sent, prevent second response
        }

        if (free_ptr)
                GF_FREE(free_ptr);
        if (brickinfo)
                glusterd_brickinfo_delete (brickinfo);
        if (cli_req.volname)
                free (cli_req.volname); // its a malloced by xdr
        return ret;
}

int
glusterd_handle_cli_start_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_start_vol_req           cli_req = {0,};
        int32_t                         flags = 0;

        GF_ASSERT (req);

        if (!gf_xdr_to_cli_start_vol_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received start vol req"
                "for volume %s", cli_req.volname);

        ret = glusterd_start_volume (req, cli_req.volname, flags);

        gf_cmd_log ("volume start","on volname: %s %s", cli_req.volname,
                    ((ret == 0) ? "SUCCESS": "FAILED"));

out:
        if (cli_req.volname)
                free (cli_req.volname); //its malloced by xdr
        return ret;
}


int
glusterd_handle_cli_stop_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_stop_vol_req           cli_req = {0,};

        GF_ASSERT (req);

        if (!gf_xdr_to_cli_stop_vol_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received stop vol req"
                "for volume %s", cli_req.volname);

        ret = glusterd_stop_volume (req, cli_req.volname, cli_req.flags);

        gf_cmd_log ("Volume stop","on volname: %s %s", cli_req.volname,
                    ((ret)?"FAILED":"SUCCESS"));

out:
        if (cli_req.volname)
                free (cli_req.volname); //its malloced by xdr
        return ret;
}

int
glusterd_handle_cli_delete_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_delete_vol_req          cli_req = {0,};
        int32_t                         flags = 0;

        GF_ASSERT (req);

        if (!gf_xdr_to_cli_delete_vol_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }
        gf_cmd_log ("Volume delete","on volname: %s attempted", cli_req.volname);

        gf_log ("glusterd", GF_LOG_NORMAL, "Received delete vol req"
                "for volume %s", cli_req.volname);

        ret = glusterd_delete_volume (req, cli_req.volname, flags);

        gf_cmd_log ("Volume delete", "on volname: %s %s", cli_req.volname,
                   ((ret) ? "FAILED" : "SUCCESS"));

out:
        if (cli_req.volname)
                free (cli_req.volname); //its malloced by xdr
        return ret;
}

int
glusterd_handle_add_brick (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_add_brick_req          cli_req = {0,};
        dict_t                          *dict = NULL;
        glusterd_brickinfo_t            *brickinfo = NULL;
        char                            *brick = NULL;
        char                            *bricks = NULL;
        char                            *volname = NULL;
        int                             brick_count = 0;
        char                            *tmpptr = NULL;
        int                             i = 0;
        glusterd_peerinfo_t             *peerinfo = NULL;
        char                            *brick_list = NULL;
        void                            *cli_rsp = NULL;
        char                            err_str[1048];
        gf1_cli_add_brick_rsp           rsp = {0,};
        glusterd_volinfo_t              *volinfo = NULL;
        int32_t                         err_ret = 0;
        glusterd_conf_t                 *priv = NULL;
        xlator_t                        *this = NULL;
        char                            *free_ptr = NULL;

        this = THIS;
        GF_ASSERT(this);

        priv = this->private;

        GF_ASSERT (req);

        if (!gf_xdr_to_cli_add_brick_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_cmd_log ("Volume add-brick", "on volname: %s attempted",
                    cli_req.volname);
        gf_log ("glusterd", GF_LOG_NORMAL, "Received add brick req");

        if (cli_req.bricks.bricks_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.bricks.bricks_val,
                                        cli_req.bricks.bricks_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.bricks.bricks_val;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        if (!(ret = glusterd_check_volume_exists (volname))) {
                snprintf(err_str, 1048, "Volname %s does not exist",
                         volname);
                gf_log ("glusterd", GF_LOG_ERROR, "%s", err_str);
                err_ret = -1;
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &brick_count);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get count");
                goto out;
        }

        if (!(ret = glusterd_volinfo_find (volname, &volinfo))) {
                if (volinfo->type == GF_CLUSTER_TYPE_NONE)
                        goto brick_val;
                if (!brick_count || !volinfo->sub_count)
                        goto brick_val;

                if (volinfo->brick_count < volinfo->sub_count) {
                        if ((volinfo->sub_count - volinfo->brick_count) == brick_count)
                                goto brick_val;
                }

                if ((brick_count % volinfo->sub_count) != 0) {
                        snprintf(err_str, 2048, "Incorrect number of bricks"
                                " supplied %d for type %s with count %d",
                                brick_count, (volinfo->type == 1)? "STRIPE":
                                "REPLICATE", volinfo->sub_count);
                        gf_log("glusterd", GF_LOG_ERROR, "%s", err_str);
                        err_ret = 1;
                        goto out;
                }
        } else {
                gf_log("", GF_LOG_ERROR, "Unable to get volinfo for volname"
                       " %s", volname);
                goto out;
        }

brick_val:
        ret = dict_get_str (dict, "bricks", &bricks);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get bricks");
                goto out;
        }

        if (bricks)
                brick_list = gf_strdup (bricks);
        if (!brick_list) {
                gf_log ("", GF_LOG_ERROR, "Out of memory");
                ret = -1;
                goto out;
        } else {
                free_ptr = brick_list;
        }

        gf_cmd_log ("Volume add-brick", "volname: %s type %s count:%d bricks:%s"
                    ,volname, ((volinfo->type == 0)? "DEFAULT" : ((volinfo->type
                    == 1)? "STRIPE": "REPLICATE")), brick_count, brick_list);

        while ( i < brick_count) {
                i++;
                brick= strtok_r (brick_list, " \n", &tmpptr);
                brick_list = tmpptr;
                if (brickinfo)
                        glusterd_brickinfo_delete (brickinfo);
                ret = glusterd_brickinfo_from_brick (brick, &brickinfo);
                if (ret)
                        goto out;
                if(!(ret = glusterd_is_local_addr(brickinfo->hostname)))
                        goto brick_validation;       //localhost, continue without validation
                ret = glusterd_friend_find_by_hostname(brickinfo->hostname,
                                                        &peerinfo);
                if (ret) {
                        snprintf(err_str, 1048, "Host %s not a friend",
                                 brickinfo->hostname);
                        gf_log ("glusterd", GF_LOG_ERROR, "%s", err_str);
                        err_ret = 1;
                        goto out;
                }
                if ((!peerinfo->connected) ||
                    (peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED)) {
                        snprintf(err_str, 1048, "Host %s not connected",
                                 brickinfo->hostname);
                        gf_log ("glusterd", GF_LOG_ERROR, "%s", err_str);
                        err_ret = 1;
                        goto out;
                }
brick_validation:
                err_ret = glusterd_is_exisiting_brick (brickinfo->hostname,
                                                       brickinfo->path);
                if (err_ret) {
                        snprintf(err_str, 1048, "Brick: %s already in use",
                                 brick);
                        goto out;
                }
        }

        ret = glusterd_add_brick (req, dict);

        gf_cmd_log ("Volume add-brick","on volname: %s %s", volname,
                   ((ret || err_ret) != 0)? "FAILED" : "SUCCESS");

out:
        if ((err_ret || ret) && dict)
                dict_unref (dict);
        if (err_ret) {
                rsp.op_ret = -1;
                rsp.op_errno = 0;
                rsp.volname = "";
                rsp.op_errstr = err_str;
                cli_rsp = &rsp;
                glusterd_submit_reply(req, cli_rsp, NULL, 0, NULL,
                                      gf_xdr_serialize_cli_add_brick_rsp);
                if (!glusterd_opinfo_unlock())
                        gf_log ("glusterd", GF_LOG_ERROR, "Unlock on "
                               "opinfo failed");

                ret = 0; //sent error to cli, prevent second reply
        }
        if (brickinfo)
                glusterd_brickinfo_delete (brickinfo);
        if (free_ptr)
                GF_FREE (free_ptr);
        if (cli_req.volname)
                free (cli_req.volname); //its malloced by xdr
        return ret;
}

int
glusterd_handle_replace_brick (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_replace_brick_req          cli_req = {0,};
        dict_t                          *dict = NULL;
        char                            *src_brick = NULL;
        char                            *dst_brick = NULL;
        int32_t                         op = 0;
        char                            operation[8];

        GF_ASSERT (req);

        if (!gf_xdr_to_cli_replace_brick_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_cmd_log ("Volume replace-brick","on volname: %s attempted", cli_req.volname);

        gf_log ("glusterd", GF_LOG_NORMAL, "Received replace brick req");

        if (cli_req.bricks.bricks_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.bricks.bricks_val,
                                        cli_req.bricks.bricks_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.bricks.bricks_val;
                }
        }

        ret = dict_get_int32 (dict, "operation", &op);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "dict_get on operation failed");
                goto out;
        }

        ret = dict_get_str (dict, "src-brick", &src_brick);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get src brick");
                goto out;
        }
        gf_log ("", GF_LOG_DEBUG,
                "src brick=%s", src_brick);

        ret = dict_get_str (dict, "dst-brick", &dst_brick);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get dest brick");
                goto out;
        }

        gf_log ("", GF_LOG_DEBUG,
                "dst brick=%s", dst_brick);

        switch (op) {
                case GF_REPLACE_OP_START: strcpy (operation, "start");
                        break;
                case GF_REPLACE_OP_COMMIT: strcpy (operation, "commit");
                        break;
                case GF_REPLACE_OP_PAUSE:  strcpy (operation, "pause");
                        break;
                case GF_REPLACE_OP_ABORT:  strcpy (operation, "abort");
                        break;
                case GF_REPLACE_OP_STATUS: strcpy (operation, "status");
                        break;
                default:strcpy (operation, "unknown");
                        break;
        }

        gf_cmd_log ("Volume replace-brick","volname: %s src_brick:%s"
                    " dst_brick:%s op:%s",cli_req.volname, src_brick, dst_brick
                    ,operation);


        ret = glusterd_replace_brick (req, dict);

        gf_cmd_log ("Volume replace-brick","on volname: %s %s", cli_req.volname,
                   (ret) ? "FAILED" : "SUCCESS");

out:
        if (ret && dict)
                dict_unref (dict);
        if (cli_req.volname)
                free (cli_req.volname);//malloced by xdr
        return ret;
}

int
glusterd_handle_set_volume (rpcsvc_request_t *req)
{
        int32_t                           ret = -1;
        gf1_cli_set_vol_req           cli_req = {0,};
        dict_t                          *dict = NULL;

        GF_ASSERT (req);

        if (!gf_xdr_to_cli_set_vol_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
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
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                }
        }

        ret = glusterd_set_volume (req, dict);

out:
        return ret;
}

int
glusterd_handle_remove_brick (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_remove_brick_req        cli_req = {0,};
        dict_t                          *dict = NULL;
        int32_t                         count = 0;
        char                            *brick = NULL;
        char                            key[256] = {0,};
        char                            *brick_list = NULL;
        int                             i = 1;
        glusterd_volinfo_t              *volinfo = NULL;
        glusterd_brickinfo_t            *brickinfo = NULL;
        int32_t                         pos = 0;
        int32_t                         sub_volume = 0;
        int32_t                         sub_volume_start = 0;
        int32_t                         sub_volume_end = 0;
        glusterd_brickinfo_t            *tmp = NULL;
        int32_t                         err_ret = 0;
        char                            *err_str = NULL;
        gf1_cli_remove_brick_rsp        rsp = {0,};
        void                            *cli_rsp = NULL;
        char                            vol_type[256] = {0,};

        GF_ASSERT (req);

        if (!gf_xdr_to_cli_remove_brick_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_cmd_log ("Volume remove-brick","on volname: %s attempted",cli_req.volname);
        gf_log ("glusterd", GF_LOG_NORMAL, "Received rem brick req");

        if (cli_req.bricks.bricks_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.bricks.bricks_val,
                                        cli_req.bricks.bricks_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.bricks.bricks_val;
                }
        }

        ret = dict_get_int32 (dict, "count", &count);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get count");
                goto out;
        }

        err_str = GF_MALLOC (2048 * sizeof(*err_str),gf_common_mt_char);

        if (!err_str) {
                gf_log ("",GF_LOG_ERROR,"glusterd_handle_remove_brick: "
                        "Unable to get memory");
                ret = -1;
                goto out;
        }

        ret = glusterd_volinfo_find (cli_req.volname, &volinfo);
        if (ret) {
                 snprintf (err_str, 2048, "volname %s not found",
                          cli_req.volname);
                 gf_log ("", GF_LOG_ERROR, "%s", err_str);
                 err_ret = 1;
                 goto out;
        }

        if (volinfo->type == GF_CLUSTER_TYPE_REPLICATE)
                strcpy (vol_type, "replica");
        else if (volinfo->type == GF_CLUSTER_TYPE_STRIPE)
                strcpy (vol_type, "stripe");
        else
                strcpy (vol_type, "distribute");

        if ((volinfo->type == (GF_CLUSTER_TYPE_REPLICATE ||
             GF_CLUSTER_TYPE_STRIPE)) &&
            !(volinfo->brick_count <= volinfo->sub_count)) {
                if (volinfo->sub_count && (count % volinfo->sub_count != 0)) {
                        snprintf (err_str, 2048, "Remove brick incorrect"
                                  " brick count of %d for %s %d",
                                   count, vol_type, volinfo->sub_count);
                        gf_log ("", GF_LOG_ERROR, "%s", err_str);
                        err_ret = 1;
                        ret = -1;
                        goto out;
                }

        }

        brick_list = GF_MALLOC (120000 * sizeof(*brick_list),gf_common_mt_char);

        if (!brick_list) {
                gf_log ("",GF_LOG_ERROR,"glusterd_handle_remove_brick: "
                        "Unable to get memory");
                ret = -1;
                goto out;
        }

        strcpy (brick_list, " ");
        while ( i <= count) {
                snprintf (key, 256, "brick%d", i);
                ret = dict_get_str (dict, key, &brick);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Unable to get %s", key);
                        goto out;
                }
                gf_log ("", GF_LOG_DEBUG, "Remove brick count %d brick: %s",
                        i, brick);

                ret = glusterd_brickinfo_get(brick, volinfo, &brickinfo);
                if (ret) {
                        snprintf(err_str, 2048," Incorrect brick %s for volname"
                                " %s", brick, cli_req.volname);
                        gf_log ("", GF_LOG_ERROR, "%s", err_str);
                        err_ret = 1;
                        goto out;
                }
                strcat(brick_list, brick);
                strcat(brick_list, " ");

                i++;
                if ((volinfo->type == GF_CLUSTER_TYPE_NONE) ||
                    (volinfo->brick_count <= volinfo->sub_count))
                        continue;
 
                pos = 0;
                list_for_each_entry (tmp, &volinfo->bricks, brick_list) {

                        if ((!strcmp (tmp->hostname,brickinfo->hostname)) &&
                            !strcmp (tmp->path, brickinfo->path)) {
                                gf_log ("", GF_LOG_NORMAL, "Found brick");
                                if (!sub_volume && volinfo->sub_count) {
                                        sub_volume = (pos / volinfo->
                                                      sub_count) + 1;
                                        sub_volume_start = volinfo->sub_count *
                                                           (sub_volume - 1);
                                        sub_volume_end = (volinfo->sub_count *
                                                          sub_volume) -1 ;
                                } else {
                                        if (pos < sub_volume_start ||
                                            pos >sub_volume_end) {
                                                ret = -1;
                                                snprintf(err_str, 2048,"Bricks"
                                                         " not from same subvol"
                                                         " for %s", vol_type);
                                                gf_log ("",GF_LOG_ERROR,
                                                        "%s", err_str);
                                                err_ret = 1;
                                                goto out;
                                        }
                                }
                                break;
                        }
                        pos++;
                }
        }
        gf_cmd_log ("Volume remove-brick","volname: %s count:%d bricks:%s",
                    cli_req.volname, count, brick_list);

        ret = glusterd_remove_brick (req, dict);

        gf_cmd_log ("Volume remove-brick","on volname: %s %s",cli_req.volname,
                    (ret) ? "FAILED" : "SUCCESS");

out:
        if ((ret || err_ret) && dict)
                dict_unref (dict);
        if (err_ret) {
                rsp.op_ret = -1;
                rsp.op_errno = 0;
                rsp.volname = "";
                rsp.op_errstr = err_str;
                cli_rsp = &rsp;
                glusterd_submit_reply(req, cli_rsp, NULL, 0, NULL,
                                      gf_xdr_serialize_cli_remove_brick_rsp);
                if (!glusterd_opinfo_unlock())
                        gf_log ("glusterd", GF_LOG_ERROR, "Unlock on "
                               "opinfo failed");

                ret = 0; //sent error to cli, prevent second reply

        }
        if (brick_list)
                GF_FREE (brick_list);
        if (err_str)
                GF_FREE (err_str);
        if (cli_req.volname)
                free (cli_req.volname); //its malloced by xdr
        return ret;
}

int
glusterd_handle_log_filename (rpcsvc_request_t *req)
{
        int32_t                   ret     = -1;
        gf1_cli_log_filename_req  cli_req = {0,};
        dict_t                   *dict    = NULL;

        GF_ASSERT (req);

        if (!gf_xdr_to_cli_log_filename_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received log filename req "
                "for volume %s", cli_req.volname);

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_dynmstr (dict, "volname", cli_req.volname);
        if (ret)
                goto out;
        ret = dict_set_dynmstr (dict, "brick", cli_req.brick);
        if (ret)
                goto out;
        ret = dict_set_dynmstr (dict, "path", cli_req.path);
        if (ret)
                goto out;

        ret = glusterd_log_filename (req, dict);

out:
        if (ret && dict)
                dict_unref (dict);
        return ret;
}

int
glusterd_handle_log_locate (rpcsvc_request_t *req)
{
        int32_t                 ret     = -1;
        gf1_cli_log_locate_req  cli_req = {0,};
        gf1_cli_log_locate_rsp  rsp     = {0,};
        glusterd_conf_t        *priv = NULL;
        glusterd_volinfo_t     *volinfo = NULL;
        glusterd_brickinfo_t   *brickinfo = NULL;
        char                    tmp_str[PATH_MAX] = {0,};

        GF_ASSERT (req);

        priv    = THIS->private;

        if (!gf_xdr_to_cli_log_locate_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received log locate req "
                "for volume %s", cli_req.volname);

        if (strchr (cli_req.brick, ':')) {
                /* TODO: need to get info of only that brick and then
                   tell what is the exact location */
                gf_log ("", GF_LOG_DEBUG, "brick : %s", cli_req.brick);
        }

        ret = glusterd_volinfo_find (cli_req.volname, &volinfo);
        if (ret) {
                rsp.path = "request sent on non-existent volume";
                goto out;
        }

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (brickinfo->logfile) {
                        strcpy (tmp_str, brickinfo->logfile);
                        rsp.path = dirname (tmp_str);
                } else {
                        snprintf (tmp_str, PATH_MAX, "%s/logs/bricks/",
                                  priv->workdir);
                        rsp.path = tmp_str;
                }
                break;
        }

        ret = 0;
out:
        rsp.op_ret = ret;
        if (!rsp.path)
                rsp.path = "";

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gf_xdr_serialize_cli_log_locate_rsp);

        if (cli_req.brick)
                free (cli_req.brick); //its malloced by xdr
        if (cli_req.volname)
                free (cli_req.volname); //its malloced by xdr
        return ret;
}

int
glusterd_handle_log_rotate (rpcsvc_request_t *req)
{
        int32_t                 ret     = -1;
        gf1_cli_log_rotate_req  cli_req = {0,};
        dict_t                 *dict    = NULL;

        GF_ASSERT (req);

        if (!gf_xdr_to_cli_log_rotate_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received log rotate req "
                "for volume %s", cli_req.volname);

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_dynmstr (dict, "volname", cli_req.volname);
        if (ret)
                goto out;

        ret = dict_set_dynmstr (dict, "brick", cli_req.brick);
        if (ret)
                goto out;

        ret = dict_set_uint64 (dict, "rotate-key", (uint64_t)time (NULL));
        if (ret)
                goto out;

        ret = glusterd_log_rotate (req, dict);

out:
        if (ret && dict)
                dict_unref (dict);
        return ret;
}

int
glusterd_handle_sync_volume (rpcsvc_request_t *req)
{
        int32_t                          ret     = -1;
        gf1_cli_sync_volume_req          cli_req = {0,};
        dict_t                           *dict = NULL;
        gf1_cli_sync_volume_rsp          cli_rsp = {0.};
        char                             msg[2048] = {0,};
        gf_boolean_t                     free_hostname = _gf_true;
        gf_boolean_t                     free_volname = _gf_true;
        glusterd_volinfo_t               *volinfo = NULL;

        GF_ASSERT (req);

        if (!gf_xdr_to_cli_sync_volume_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }
        gf_log ("glusterd", GF_LOG_NORMAL, "Received volume sync req "
                "for volume %s",
                (cli_req.flags & GF_CLI_SYNC_ALL) ? "all" : cli_req.volname);

        dict = dict_new ();
        if (!dict) {
                gf_log ("", GF_LOG_ERROR, "Can't allocate sync vol dict");
                goto out;
        }

        if (!glusterd_is_local_addr (cli_req.hostname)) {
                ret = -1;
                snprintf (msg, sizeof (msg), "sync from localhost"
                          " not allowed");
                goto out;
        }

        ret = dict_set_dynmstr (dict, "hostname", cli_req.hostname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "hostname set failed");
                snprintf (msg, sizeof (msg), "hostname set failed");
                goto out;
        } else {
                free_hostname = _gf_false;
        }

        ret = dict_set_int32 (dict, "flags", cli_req.flags);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "volume flags set failed");
                snprintf (msg, sizeof (msg), "volume flags set failed");
                goto out;
        }

        if (!cli_req.flags) {
                ret = glusterd_volinfo_find (cli_req.volname, &volinfo);
                if (!ret) {
                        snprintf (msg, sizeof (msg), "please delete the "
                                 "volume: %s before sync", cli_req.volname);
                        ret = -1;
                        goto out;
                }

                ret = dict_set_dynmstr (dict, "volname", cli_req.volname);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "volume name set failed");
                        snprintf (msg, sizeof (msg), "volume name set failed");
                        goto out;
                } else {
                        free_volname = _gf_false;
                }
        } else {
                free_volname = _gf_false;
                if (glusterd_volume_count_get ()) {
                        snprintf (msg, sizeof (msg), "please delete all the "
                                 "volumes before full sync");
                        ret = -1;
                        goto out;
                }
        }

        ret = glusterd_sync_volume (req, dict);

out:
        if (ret) {
                cli_rsp.op_ret = -1;
                cli_rsp.op_errstr = msg;
                glusterd_submit_reply(req, &cli_rsp, NULL, 0, NULL,
                                      gf_xdr_from_cli_sync_volume_rsp);
                if (free_hostname && cli_req.hostname)
                        free (cli_req.hostname);
                if (free_volname && cli_req.volname)
                        free (cli_req.volname);
                if (dict)
                        dict_unref (dict);
                if (!glusterd_opinfo_unlock())
                        gf_log ("glusterd", GF_LOG_ERROR, "Unlock on "
                               "opinfo failed");

                ret = 0; //sent error to cli, prevent second reply
        }

        return ret;
}

int
glusterd_op_lock_send_resp (rpcsvc_request_t *req, int32_t status)
{

        gd1_mgmt_cluster_lock_rsp       rsp = {{0},};
        int                             ret = -1;

        GF_ASSERT (req);
        glusterd_get_uuid (&rsp.uuid);
        rsp.op_ret = status;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gd_xdr_serialize_mgmt_cluster_lock_rsp);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Responded, ret: %d", ret);

        return 0;
}

int
glusterd_op_unlock_send_resp (rpcsvc_request_t *req, int32_t status)
{

        gd1_mgmt_cluster_unlock_rsp     rsp = {{0},};
        int                             ret = -1;

        GF_ASSERT (req);
        rsp.op_ret = status;
        glusterd_get_uuid (&rsp.uuid);

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gd_xdr_serialize_mgmt_cluster_unlock_rsp);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Responded to unlock, ret: %d", ret);

        return ret;
}

int
glusterd_handle_cluster_unlock (rpcsvc_request_t *req)
{
        gd1_mgmt_cluster_unlock_req     unlock_req = {{0}, };
        int32_t                         ret = -1;
        char                            str[50] = {0, };
        glusterd_op_lock_ctx_t          *ctx = NULL;

        GF_ASSERT (req);

        if (!gd_xdr_to_mgmt_cluster_unlock_req (req->msg[0], &unlock_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        uuid_unparse (unlock_req.uuid, str);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received UNLOCK from uuid: %s", str);

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_op_lock_ctx_t);

        if (!ctx) {
                //respond here
                return -1;
        }
        uuid_copy (ctx->uuid, unlock_req.uuid);
        ctx->req = req;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_UNLOCK, ctx);

out:
        return ret;
}

int
glusterd_op_stage_send_resp (rpcsvc_request_t   *req,
                             int32_t op, int32_t status, char *op_errstr)
{

        gd1_mgmt_stage_op_rsp           rsp = {{0},};
        int                             ret = -1;

        GF_ASSERT (req);
        rsp.op_ret = status;
        glusterd_get_uuid (&rsp.uuid);
        rsp.op = op;
        if (op_errstr)
                rsp.op_errstr = op_errstr;
        else
                rsp.op_errstr = "";

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gd_xdr_serialize_mgmt_stage_op_rsp);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Responded to stage, ret: %d", ret);

        return ret;
}

int
glusterd_op_commit_send_resp (rpcsvc_request_t *req,
                               int32_t op, int32_t status, char *op_errstr,
                               dict_t *rsp_dict)
{
        gd1_mgmt_commit_op_rsp          rsp      = {{0}, };
        int                             ret      = -1;

        GF_ASSERT (req);
        rsp.op_ret = status;
        glusterd_get_uuid (&rsp.uuid);
        rsp.op = op;

        if (op_errstr)
                rsp.op_errstr = op_errstr;
        else
                rsp.op_errstr = "";

        ret = dict_allocate_and_serialize (rsp_dict,
                                           &rsp.dict.dict_val,
                                           (size_t *)&rsp.dict.dict_len);
        if (ret < 0) {
                gf_log ("", GF_LOG_DEBUG,
                        "failed to get serialized length of dict");
                goto out;
        }


        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gd_xdr_serialize_mgmt_commit_op_rsp);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Responded to commit, ret: %d", ret);

out:
        if (rsp.dict.dict_val)
                GF_FREE (rsp.dict.dict_val);
        return ret;
}

int
glusterd_handle_incoming_friend_req (rpcsvc_request_t *req)
{
        int32_t                 ret = -1;
        gd1_mgmt_friend_req     friend_req = {{0},};
        char                    str[50] = {0,};
        dict_t                  *dict = NULL;

        GF_ASSERT (req);
        if (!gd_xdr_to_mgmt_friend_req (req->msg[0], &friend_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }
        uuid_unparse (friend_req.uuid, str);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received probe from uuid: %s", str);

        dict = dict_new ();
        if (!dict) {
                ret = -1;
                goto out;
        }

        ret = dict_unserialize (friend_req.vols.vols_val,
                                friend_req.vols.vols_len,
                                &dict);

        if (ret)
                goto out;
        else
                dict->extra_stdfree = friend_req.vols.vols_val;

        ret = glusterd_handle_friend_req (req, friend_req.uuid,
                                          friend_req.hostname, friend_req.port,
                                          dict);

out:
        if (ret && dict)
                dict_unref (dict);
        if (friend_req.hostname)
                free (friend_req.hostname);//malloced by xdr

        return ret;
}

int
glusterd_handle_incoming_unfriend_req (rpcsvc_request_t *req)
{
        int32_t                 ret = -1;
        gd1_mgmt_friend_req     friend_req = {{0},};
        char                    str[50];

        GF_ASSERT (req);
        if (!gd_xdr_to_mgmt_friend_req (req->msg[0], &friend_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }
        uuid_unparse (friend_req.uuid, str);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received unfriend from uuid: %s", str);

        ret = glusterd_handle_unfriend_req (req, friend_req.uuid,
                                            friend_req.hostname, friend_req.port);

out:
        if (friend_req.hostname)
                free (friend_req.hostname);//malloced by xdr
        if (friend_req.vols.vols_val)
                free (friend_req.vols.vols_val);//malloced by xdr
        return ret;
}

int
glusterd_handle_friend_update (rpcsvc_request_t *req)
{
        int32_t                 ret = -1;
        gd1_mgmt_friend_update     friend_req = {{0},};
        char                    str[50] = {0,};
        glusterd_peerinfo_t     *peerinfo = NULL;
        glusterd_conf_t         *priv = NULL;
        xlator_t                *this = NULL;
        glusterd_peerinfo_t     *tmp = NULL;
        gd1_mgmt_friend_update_rsp rsp = {{0},};
        dict_t                  *dict = NULL;
        char                    key[100] = {0,};
        char                    *uuid_buf = NULL;
        char                    *hostname = NULL;
        int                     i = 1;
        int                     count = 0;
        uuid_t                  uuid = {0,};

        GF_ASSERT (req);

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        if (!gd_xdr_to_mgmt_friend_update (req->msg[0], &friend_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }
        uuid_unparse (friend_req.uuid, str);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received friend update from uuid: %s", str);

        if (friend_req.friends.friends_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (friend_req.friends.friends_val,
                                        friend_req.friends.friends_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = friend_req.friends.friends_val;
                }
        }

        ret = dict_get_int32 (dict, "count", &count);
        if (ret)
                goto out;

        while ( i <= count) {
                snprintf (key, sizeof (key), "friend%d.uuid", i);
                ret = dict_get_str (dict, key, &uuid_buf);
                if (ret)
                        goto out;
                uuid_parse (uuid_buf, uuid);
                snprintf (key, sizeof (key), "friend%d.hostname", i);
                ret = dict_get_str (dict, key, &hostname);
                if (ret)
                        goto out;

                gf_log ("", GF_LOG_NORMAL, "Received uuid: %s, hostname:%s",
                                uuid_buf, hostname);

                if (!uuid_compare (uuid, priv->uuid)) {
                        gf_log ("", GF_LOG_NORMAL, "Received my uuid as Friend");
                        i++;
                        continue;
                }

                ret = glusterd_friend_find (uuid, hostname, &tmp);

                if (!ret) {
                        i++;
                        continue;
                }

                ret = glusterd_friend_add (hostname, friend_req.port,
                                           GD_FRIEND_STATE_BEFRIENDED,
                                           &uuid, NULL, &peerinfo, 0);

                i++;
        }

out:
        uuid_copy (rsp.uuid, priv->uuid);
        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gd_xdr_serialize_mgmt_friend_update_rsp);
        if (dict)
                dict_unref (dict);
        return ret;
}

int
glusterd_handle_probe_query (rpcsvc_request_t *req)
{
        int32_t             ret = -1;
        char                str[50];
        xlator_t            *this = NULL;
        glusterd_conf_t     *conf = NULL;
        gd1_mgmt_probe_req  probe_req = {{0},};
        gd1_mgmt_probe_rsp  rsp = {{0},};
        glusterd_peer_hostname_t        *name = NULL;
        glusterd_peerinfo_t             *peerinfo = NULL;
        char               remote_hostname[UNIX_PATH_MAX + 1] = {0,};

        GF_ASSERT (req);

        if (!gd_xdr_to_mgmt_probe_req (req->msg[0], &probe_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }


        this = THIS;

        conf = this->private;
        uuid_unparse (probe_req.uuid, str);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received probe from uuid: %s", str);

        ret = glusterd_remote_hostname_get (req, remote_hostname,
                                            sizeof (remote_hostname));
        if (ret) {
                GF_ASSERT (0);
                goto out;
        }
        ret = glusterd_friend_find (probe_req.uuid, remote_hostname, &peerinfo);
        if ((ret == 0 ) || list_empty (&conf->peers)) {
                ret = glusterd_peer_hostname_new (probe_req.hostname, &name);

                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Unable to get new peer_hostname");
                } else {
                        list_add_tail (&name->hostname_list, &conf->hostnames);
                }

        } else {
                rsp.op_ret = -1;
                rsp.op_errno = GF_PROBE_ANOTHER_CLUSTER;
        }

        uuid_copy (rsp.uuid, conf->uuid);

        rsp.hostname = probe_req.hostname;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gd_xdr_serialize_mgmt_probe_rsp);

        gf_log ("glusterd", GF_LOG_NORMAL, "Responded to %s, op_ret: %d, "
                "op_errno: %d, ret: %d", probe_req.hostname,
                rsp.op_ret, rsp.op_errno, ret);

out:
        if (probe_req.hostname)
                free (probe_req.hostname);//malloced by xdr
        return ret;
}

int
glusterd_friend_remove (uuid_t uuid, char *hostname)
{
        int                           ret = 0;
        glusterd_peerinfo_t           *peerinfo = NULL;

        ret = glusterd_friend_find (uuid, hostname, &peerinfo);
        if (ret)
                goto out;

        ret = glusterd_friend_cleanup (peerinfo);
out:
        gf_log ("", GF_LOG_DEBUG, "returning %d");
        return ret;
}

int
glusterd_friend_add (const char *hoststr, int port,
                     glusterd_friend_sm_state_t state,
                     uuid_t *uuid,
                     struct rpc_clnt    *rpc,
                     glusterd_peerinfo_t **friend,
                     gf_boolean_t restore)
{
        int                     ret = 0;
        glusterd_conf_t         *priv = NULL;
        glusterd_peerinfo_t     *peerinfo = NULL;
        dict_t                  *options = NULL;
        struct rpc_clnt_config  rpc_cfg = {0,};
        glusterd_peer_hostname_t *name = NULL;
        char                    *hostname = NULL;

        priv = THIS->private;

        peerinfo = GF_CALLOC (1, sizeof(*peerinfo), gf_gld_mt_peerinfo_t);

        if (!peerinfo)
                return -1;

        if (friend)
                *friend = peerinfo;

        INIT_LIST_HEAD (&peerinfo->hostnames);
        peerinfo->state.state = state;
        if (hoststr) {
                ret =  glusterd_peer_hostname_new ((char *)hoststr, &name);
                if (ret)
                        goto out;
                list_add_tail (&peerinfo->hostnames, &name->hostname_list);
                rpc_cfg.remote_host = gf_strdup (hoststr);
                peerinfo->hostname = gf_strdup (hoststr);
        }
        INIT_LIST_HEAD (&peerinfo->uuid_list);

        list_add_tail (&peerinfo->uuid_list, &priv->peers);

        if (uuid) {
                uuid_copy (peerinfo->uuid, *uuid);
        }


        if (hoststr) {
                options = dict_new ();
                if (!options)
                        return -1;

                hostname = gf_strdup((char*)hoststr);
                if (!hostname) {
                        ret = -1;
                        goto out;
                }

                ret = dict_set_dynstr (options, "remote-host", hostname);
                if (ret)
                        goto out;


                if (!port)
                        port = GLUSTERD_DEFAULT_PORT;

                rpc_cfg.remote_port = port;

                ret = dict_set_int32 (options, "remote-port", port);
                if (ret)
                        goto out;

                ret = dict_set_str (options, "transport.address-family", "inet");
                if (ret)
                        goto out;

                rpc = rpc_clnt_init (&rpc_cfg, options, THIS->ctx, THIS->name);

                if (!rpc) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "rpc init failed for peer: %s!", hoststr);
                        ret = -1;
                        goto out;
                }

                ret = rpc_clnt_register_notify (rpc, glusterd_rpc_notify,
                                                peerinfo);

                peerinfo->rpc = rpc;

        }

        if (!restore)
                ret = glusterd_store_update_peerinfo (peerinfo);


out:
        gf_log ("glusterd", GF_LOG_NORMAL, "connect returned %d", ret);
        if (rpc_cfg.remote_host)
                GF_FREE (rpc_cfg.remote_host);
        return ret;
}



int
glusterd_probe_begin (rpcsvc_request_t *req, const char *hoststr, int port)
{
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_probe_ctx_t            *ctx = NULL;

        GF_ASSERT (hoststr);

        ret = glusterd_friend_find (NULL, (char *)hoststr, &peerinfo);

        if (ret) {
                gf_log ("glusterd", GF_LOG_NORMAL, "Unable to find peerinfo"
                        " for host: %s (%d)", hoststr, port);
                ret = glusterd_friend_add ((char *)hoststr, port,
                                           GD_FRIEND_STATE_DEFAULT,
                                           NULL, NULL, &peerinfo, 0);
        }

        ret = glusterd_friend_sm_new_event
                        (GD_FRIEND_EVENT_PROBE, &event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to get new event");
                return ret;
        }

        ctx = GF_CALLOC (1, sizeof(*ctx), gf_gld_mt_probe_ctx_t);

        if (!ctx) {
                return ret;
        }

        ctx->hostname = gf_strdup (hoststr);
        ctx->port = port;
        ctx->req = req;

        event->peerinfo = peerinfo;
        event->ctx = ctx;

        ret = glusterd_friend_sm_inject_event (event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to inject event %d, "
                        "ret = %d", event->event, ret);
                return ret;
        }

        if (!peerinfo->connected) {
                return  GLUSTERD_CONNECTION_AWAITED;
        }


        return ret;
}

int
glusterd_deprobe_begin (rpcsvc_request_t *req, const char *hoststr, int port)
{
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_probe_ctx_t            *ctx = NULL;

        GF_ASSERT (hoststr);
        GF_ASSERT (req);

        ret = glusterd_friend_find (NULL, (char *)hoststr, &peerinfo);

        if (ret) {
                gf_log ("glusterd", GF_LOG_NORMAL, "Unable to find peerinfo"
                        " for host: %s %d", hoststr, port);
                goto out;
        }

        if (!peerinfo->rpc) {
                //handle this case
                goto out;
        }

        ret = glusterd_friend_sm_new_event
                (GD_FRIEND_EVENT_INIT_REMOVE_FRIEND, &event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR,
                                "Unable to get new event");
                return ret;
        }

        ctx = GF_CALLOC (1, sizeof(*ctx), gf_gld_mt_probe_ctx_t);

        if (!ctx) {
                goto out;
        }

        ctx->hostname = gf_strdup (hoststr);
        ctx->port = port;
        ctx->req = req;

        event->ctx = ctx;

        event->peerinfo = peerinfo;

        ret = glusterd_friend_sm_inject_event (event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to inject event %d, "
                        "ret = %d", event->event, ret);
                goto out;
        }

out:
        return ret;
}


int
glusterd_xfer_friend_remove_resp (rpcsvc_request_t *req, char *hostname, int port)
{
        gd1_mgmt_friend_rsp  rsp = {{0}, };
        int32_t              ret = -1;
        xlator_t             *this = NULL;
        glusterd_conf_t      *conf = NULL;

        GF_ASSERT (hostname);

        rsp.op_ret = 0;
        this = THIS;
        GF_ASSERT (this);

        conf = this->private;

        uuid_copy (rsp.uuid, conf->uuid);
        rsp.hostname = hostname;
        rsp.port = port;
        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gd_xdr_serialize_mgmt_friend_rsp);


        gf_log ("glusterd", GF_LOG_NORMAL,
                "Responded to %s (%d), ret: %d", hostname, port, ret);
        return ret;
}

int
glusterd_xfer_friend_add_resp (rpcsvc_request_t *req, char *hostname, int port,
                               int32_t op_ret, int32_t op_errno)
{
        gd1_mgmt_friend_rsp  rsp = {{0}, };
        int32_t              ret = -1;
        xlator_t             *this = NULL;
        glusterd_conf_t      *conf = NULL;

        GF_ASSERT (hostname);

        this = THIS;
        GF_ASSERT (this);

        conf = this->private;

        uuid_copy (rsp.uuid, conf->uuid);
        rsp.op_ret = op_ret;
        rsp.op_errno = op_errno;
        rsp.hostname = gf_strdup (hostname);
        rsp.port = port;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gd_xdr_serialize_mgmt_friend_rsp);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Responded to %s (%d), ret: %d", hostname, port, ret);
        if (rsp.hostname)
                GF_FREE (rsp.hostname)
        return ret;
}

int
glusterd_xfer_cli_probe_resp (rpcsvc_request_t *req, int32_t op_ret,
                              int32_t op_errno, char *hostname, int port)
{
        gf1_cli_probe_rsp    rsp = {0, };
        int32_t              ret = -1;

        GF_ASSERT (req);

        rsp.op_ret = op_ret;
        rsp.op_errno = op_errno;
        rsp.hostname = hostname;
        rsp.port = port;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gf_xdr_serialize_cli_probe_rsp);

        gf_log ("glusterd", GF_LOG_NORMAL, "Responded to CLI, ret: %d",ret);

        return ret;
}

int
glusterd_xfer_cli_deprobe_resp (rpcsvc_request_t *req, int32_t op_ret,
                                int32_t op_errno, char *hostname)
{
        gf1_cli_deprobe_rsp    rsp = {0, };
        int32_t                ret = -1;

        GF_ASSERT (req);

        rsp.op_ret = op_ret;
        rsp.op_errno = op_errno;
        rsp.hostname = hostname;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gf_xdr_serialize_cli_deprobe_rsp);

        gf_log ("glusterd", GF_LOG_NORMAL, "Responded to CLI, ret: %d",ret);

        return ret;
}
int32_t
glusterd_op_txn_begin ()
{
        int32_t                 ret = -1;
        glusterd_conf_t         *priv = NULL;
        int32_t                 locked = 0;

        priv = THIS->private;
        GF_ASSERT (priv);

        ret = glusterd_lock (priv->uuid);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR,
                        "Unable to acquire local lock, ret: %d", ret);
                goto out;
        }

        locked = 1;
        gf_log ("glusterd", GF_LOG_NORMAL, "Acquired local lock");

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_START_LOCK, NULL);

        gf_log ("glusterd", GF_LOG_NORMAL, "Returning %d", ret);

out:
        if (locked && ret)
                glusterd_unlock (priv->uuid);
        return ret;
}

int32_t
glusterd_create_volume (rpcsvc_request_t *req, dict_t *dict)
{
        int32_t  ret       = -1;
        data_t  *data = NULL;

        GF_ASSERT (req);
        GF_ASSERT (dict);

        glusterd_op_set_op (GD_OP_CREATE_VOLUME);

        glusterd_op_set_ctx (GD_OP_CREATE_VOLUME, dict);

        glusterd_op_set_ctx_free (GD_OP_CREATE_VOLUME, _gf_true);

        glusterd_op_set_req (req);

        data = dict_get (dict, "volname");
        if (!data)
                goto out;

        data = dict_get (dict, "type");
        if (!data)
                goto out;

        data = dict_get (dict, "count");
        if (!data)
                goto out;

        data = dict_get (dict, "bricks");
        if (!data)
                goto out;

        data = dict_get (dict, "transport");
        if (!data)
                goto out;

        data = dict_get (dict, "volume-id");
        if (!data)
                goto out;

        ret = glusterd_op_txn_begin ();

out:
        return ret;
}

int32_t
glusterd_start_volume (rpcsvc_request_t *req, char *volname, int flags)
{
        int32_t      ret       = -1;
        glusterd_op_start_volume_ctx_t  *ctx = NULL;

        GF_ASSERT (req);
        GF_ASSERT (volname);

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_start_volume_ctx_t);

        if (!ctx)
                goto out;

        strncpy (ctx->volume_name, volname, GD_VOLUME_NAME_MAX);

        glusterd_op_set_op (GD_OP_START_VOLUME);

        glusterd_op_set_ctx (GD_OP_START_VOLUME, ctx);
        glusterd_op_set_ctx_free (GD_OP_START_VOLUME, _gf_true);
        glusterd_op_set_req (req);

        ret = glusterd_op_txn_begin ();

out:
        return ret;
}

int32_t
glusterd_stop_volume (rpcsvc_request_t *req, char *volname, int flags)
{
        int32_t      ret        = -1;
        dict_t       *ctx       = NULL;
        char         *dup_volname = NULL;

        GF_ASSERT (req);
        GF_ASSERT (volname);

        ctx = dict_new ();

        if (!ctx)
                goto out;

        dup_volname = gf_strdup(volname);
        if (!dup_volname)
                goto out;

        ret = dict_set_dynstr (ctx, "volname", dup_volname);
        if (ret)
                goto out;

        ret = dict_set_int32 (ctx, "flags", flags);
        if (ret)
                goto out;

        glusterd_op_set_op (GD_OP_STOP_VOLUME);

        glusterd_op_set_ctx (GD_OP_STOP_VOLUME, ctx);
        glusterd_op_set_ctx_free (GD_OP_STOP_VOLUME, _gf_true);
        glusterd_op_set_req (req);

        ret = glusterd_op_txn_begin ();

out:
        if (ret && ctx)
                dict_unref (ctx);
        return ret;
}

int32_t
glusterd_delete_volume (rpcsvc_request_t *req, char *volname, int flags)
{
        int32_t      ret       = -1;
        glusterd_op_delete_volume_ctx_t  *ctx = NULL;

        GF_ASSERT (req);
        GF_ASSERT (volname);

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_delete_volume_ctx_t);

        if (!ctx)
                goto out;

        strncpy (ctx->volume_name, volname, GD_VOLUME_NAME_MAX);

        glusterd_op_set_op (GD_OP_DELETE_VOLUME);

        glusterd_op_set_ctx (GD_OP_DELETE_VOLUME, ctx);
        glusterd_op_set_ctx_free (GD_OP_DELETE_VOLUME, _gf_true);
        glusterd_op_set_req (req);

        ret = glusterd_op_txn_begin ();

out:
        return ret;
}

int32_t
glusterd_add_brick (rpcsvc_request_t *req, dict_t *dict)
{
        int32_t      ret       = -1;

        GF_ASSERT (req);
        GF_ASSERT (dict);

        glusterd_op_set_op (GD_OP_ADD_BRICK);

        glusterd_op_set_ctx (GD_OP_ADD_BRICK, dict);
        glusterd_op_set_ctx_free (GD_OP_ADD_BRICK, _gf_true);
        glusterd_op_set_req (req);

        ret = glusterd_op_txn_begin ();

        return ret;
}

int32_t
glusterd_replace_brick (rpcsvc_request_t *req, dict_t *dict)
{
        int32_t      ret       = -1;

        GF_ASSERT (req);
        GF_ASSERT (dict);

        glusterd_op_set_op (GD_OP_REPLACE_BRICK);

        glusterd_op_set_ctx (GD_OP_REPLACE_BRICK, dict);

        glusterd_op_set_ctx_free (GD_OP_REPLACE_BRICK, _gf_true);
        glusterd_op_set_req (req);

        ret = glusterd_op_txn_begin ();

        return ret;
}

int32_t
glusterd_set_volume (rpcsvc_request_t *req, dict_t *dict)
{
        int32_t      ret       = -1;

        GF_ASSERT (req);
        GF_ASSERT (dict);

        glusterd_op_set_op (GD_OP_SET_VOLUME);

        glusterd_op_set_ctx (GD_OP_SET_VOLUME, dict);

        glusterd_op_set_ctx_free (GD_OP_SET_VOLUME, _gf_true);

	glusterd_op_set_cli_op (GD_MGMT_CLI_SET_VOLUME);

	glusterd_op_set_req (req);

        ret = glusterd_op_txn_begin ();

        return ret;
}

int32_t
glusterd_remove_brick (rpcsvc_request_t *req, dict_t *dict)
{
        int32_t      ret       = -1;

        GF_ASSERT (req);
        GF_ASSERT (dict);

        glusterd_op_set_op (GD_OP_REMOVE_BRICK);

        glusterd_op_set_ctx (GD_OP_REMOVE_BRICK, dict);
        glusterd_op_set_ctx_free (GD_OP_REMOVE_BRICK, _gf_true);
        glusterd_op_set_req (req);

        ret = glusterd_op_txn_begin ();

        return ret;
}

int32_t
glusterd_log_filename (rpcsvc_request_t *req, dict_t *dict)
{
        int32_t      ret       = -1;

        GF_ASSERT (req);
        GF_ASSERT (dict);

        glusterd_op_set_op (GD_OP_LOG_FILENAME);
        glusterd_op_set_ctx (GD_OP_LOG_FILENAME, dict);
        glusterd_op_set_ctx_free (GD_OP_LOG_FILENAME, _gf_true);
        glusterd_op_set_req (req);

        ret = glusterd_op_txn_begin ();

        return ret;
}


int32_t
glusterd_log_rotate (rpcsvc_request_t *req, dict_t *dict)
{
        int32_t      ret       = -1;

        GF_ASSERT (req);
        GF_ASSERT (dict);

        glusterd_op_set_op (GD_OP_LOG_ROTATE);
        glusterd_op_set_ctx (GD_OP_LOG_ROTATE, dict);
        glusterd_op_set_ctx_free (GD_OP_LOG_ROTATE, _gf_true);
        glusterd_op_set_req (req);

        ret = glusterd_op_txn_begin ();

        return ret;
}

int32_t
glusterd_sync_volume (rpcsvc_request_t *req, dict_t *ctx)
{
        int32_t      ret       = -1;

        GF_ASSERT (req);
        GF_ASSERT (ctx);

        glusterd_op_set_op (GD_OP_SYNC_VOLUME);
        glusterd_op_set_ctx (GD_OP_SYNC_VOLUME, ctx);
        glusterd_op_set_ctx_free (GD_OP_SYNC_VOLUME, _gf_true);
        glusterd_op_set_req (req);

        ret = glusterd_op_txn_begin ();

        return ret;
}


int32_t
glusterd_list_friends (rpcsvc_request_t *req, dict_t *dict, int32_t flags)
{
        int32_t                 ret = -1;
        glusterd_conf_t         *priv = NULL;
        glusterd_peerinfo_t     *entry = NULL;
        int32_t                 count = 0;
        dict_t                  *friends = NULL;
        gf1_cli_peer_list_rsp   rsp = {0,};

        priv = THIS->private;
        GF_ASSERT (priv);

        if (!list_empty (&priv->peers)) {
                friends = dict_new ();
                if (!friends) {
                        gf_log ("", GF_LOG_WARNING, "Out of Memory");
                        goto out;
                }
        } else {
                ret = 0;
                goto out;
        }

        if (flags == GF_CLI_LIST_ALL) {
                        list_for_each_entry (entry, &priv->peers, uuid_list) {
                                count++;
                                ret = glusterd_add_peer_detail_to_dict (entry,
                                                                friends, count);
                                if (ret)
                                        goto out;

                        }

                        ret = dict_set_int32 (friends, "count", count);

                        if (ret)
                                goto out;
        }

        ret = dict_allocate_and_serialize (friends, &rsp.friends.friends_val,
                                           (size_t *)&rsp.friends.friends_len);

        if (ret)
                goto out;

        ret = 0;
out:

        if (friends)
                dict_unref (friends);

        rsp.op_ret = ret;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gf_xdr_serialize_cli_peer_list_rsp);
        if (rsp.friends.friends_val)
                GF_FREE (rsp.friends.friends_val);

        return ret;
}

int32_t
glusterd_get_volumes (rpcsvc_request_t *req, dict_t *dict, int32_t flags)
{
        int32_t                 ret = -1;
        glusterd_conf_t         *priv = NULL;
        glusterd_volinfo_t      *entry = NULL;
        int32_t                 count = 0;
        dict_t                  *volumes = NULL;
        gf1_cli_get_vol_rsp     rsp = {0,};
        char                    *volname = NULL;

        priv = THIS->private;
        GF_ASSERT (priv);

        volumes = dict_new ();
        if (!volumes) {
                gf_log ("", GF_LOG_WARNING, "Out of Memory");
                goto out;
        }

        if (list_empty (&priv->volumes)) {
                ret = 0;
                goto respond;
        }

        if (flags == GF_CLI_GET_VOLUME_ALL) {
                list_for_each_entry (entry, &priv->volumes, vol_list) {
                        ret = glusterd_add_volume_detail_to_dict (entry,
                                                        volumes, count);
                        if (ret)
                                goto respond;

                        count++;

                }

        } else if (flags == GF_CLI_GET_NEXT_VOLUME) {
                ret = dict_get_str (dict, "volname", &volname);

                if (ret) {
                        if (priv->volumes.next) {
                                entry = list_entry (priv->volumes.next,
                                                    typeof (*entry),
                                                    vol_list);
                        }
                } else {
                        ret = glusterd_volinfo_find (volname, &entry);
                        if (ret)
                                goto respond;
                        entry = list_entry (entry->vol_list.next,
                                            typeof (*entry),
                                            vol_list);
                }

                if (&entry->vol_list == &priv->volumes) {
                       goto respond;
                } else {
                        ret = glusterd_add_volume_detail_to_dict (entry,
                                                         volumes, count);
                        if (ret)
                                goto respond;

                        count++;
                }
        } else if (flags == GF_CLI_GET_VOLUME) {
                ret = dict_get_str (dict, "volname", &volname);
                if (ret)
                        goto respond;

                ret = glusterd_volinfo_find (volname, &entry);
                if (ret)
                        goto respond;

                ret = glusterd_add_volume_detail_to_dict (entry,
                                                 volumes, count);
                if (ret)
                        goto respond;

                count++;
        }

respond:
        ret = dict_set_int32 (volumes, "count", count);
        if (ret)
                goto out;

        ret = dict_allocate_and_serialize (volumes, &rsp.volumes.volumes_val,
                                           (size_t *)&rsp.volumes.volumes_len);

        if (ret)
                goto out;

        ret = 0;
out:
        rsp.op_ret = ret;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gf_xdr_serialize_cli_peer_list_rsp);

        if (volumes)
                dict_unref (volumes);

        if (rsp.volumes.volumes_val)
                GF_FREE (rsp.volumes.volumes_val);
        return ret;
}

int
glusterd_rpc_notify (struct rpc_clnt *rpc, void *mydata, rpc_clnt_event_t event,
                     void *data)
{
        xlator_t                *this = NULL;
        char                    *handshake = "on";
        glusterd_conf_t         *conf = NULL;
        int                     ret = 0;
        glusterd_peerinfo_t     *peerinfo = NULL;

        peerinfo = mydata;
        this = THIS;
        conf = this->private;


        switch (event) {
        case RPC_CLNT_CONNECT:
        {

                gf_log (this->name, GF_LOG_DEBUG, "got RPC_CLNT_CONNECT");
                peerinfo->connected = 1;
                glusterd_friend_sm ();
                glusterd_op_sm ();

                if ((ret < 0) || (strcasecmp (handshake, "on"))) {
                        //ret = glusterd_handshake (this, peerinfo->rpc);

                } else {
                        //conf->rpc->connected = 1;
                        ret = default_notify (this, GF_EVENT_CHILD_UP, NULL);
                }
                break;
        }

        case RPC_CLNT_DISCONNECT:

                //Inject friend disconnected here

                gf_log (this->name, GF_LOG_DEBUG, "got RPC_CLNT_DISCONNECT");
                peerinfo->connected = 0;

                //default_notify (this, GF_EVENT_CHILD_DOWN, NULL);
                break;

        default:
                gf_log (this->name, GF_LOG_TRACE,
                        "got some other RPC event %d", event);
                ret = 0;
                break;
        }

        return ret;
}
