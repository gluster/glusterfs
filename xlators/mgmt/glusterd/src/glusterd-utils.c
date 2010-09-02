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
#include "glusterd-utils.h"
#include "glusterd-store.h"
#include "glusterd-volgen.h"

#include <sys/resource.h>
#include <inttypes.h>
#include <signal.h>
#include <sys/types.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#ifdef GF_SOLARIS_HOST_OS
#include <sys/sockio.h>
#endif

static glusterd_lock_t lock;

static int32_t
glusterd_get_lock_owner (uuid_t *uuid)
{
        uuid_copy (*uuid, lock.owner) ;
        return 0;
}

static int32_t
glusterd_set_lock_owner (uuid_t owner)
{
        uuid_copy (lock.owner, owner);
        //TODO: set timestamp
        return 0;
}

static int32_t
glusterd_unset_lock_owner (uuid_t owner)
{
        uuid_clear (lock.owner);
        //TODO: set timestamp
        return 0;
}

int32_t
glusterd_is_local_addr (char *hostname)
{
        int32_t         ret = -1;
        struct          addrinfo *result = NULL;
        struct          addrinfo *res = NULL;
        int32_t         found = 0;
        struct          ifconf buf = {0,};
        char            nodename[256] = {0,};

        if ((!strcmp (hostname, "localhost")) ||
             (!strcmp (hostname, "127.0.0.1"))) {
                found = 1;
                goto out;
        }

        ret = gethostname (nodename, 256);
        if (ret)
                goto out;

        if ((!strcmp (nodename, hostname))) {
                found = 1;
                goto out;
        }

        ret = getaddrinfo (hostname, NULL, NULL, &result);

        if (ret != 0) {
                gf_log ("", GF_LOG_ERROR, "error in getaddrinfo: %s\n",
                        gai_strerror(ret));
                goto out;
        }

        for (res = result; res != NULL; res = res->ai_next) {
                char hname[1024] = "";

                ret = getnameinfo (res->ai_addr, res->ai_addrlen, hname,
                                   NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
                if (ret)
                        goto out;

                if (!strncasecmp (hname, "127", 3)) {
                        ret = 0;
                        gf_log ("", GF_LOG_NORMAL, "local addr found");
                        found = 1;
                        break;
                }
        }

        if (!found) {
                int sd = -1;
                struct ifreq  *ifr = NULL;
                int32_t       size = 0;
                int32_t       num_req = 0;
                struct sockaddr_in sa = {0,};

                sd = socket (PF_UNIX, SOCK_DGRAM, 0);
                if (sd == -1)
                        goto out;

                buf.ifc_len = sizeof (struct ifreq);
                buf.ifc_req = GF_CALLOC (1, sizeof (struct ifreq),
                                         gf_gld_mt_ifreq);
                size = buf.ifc_len;

                ret = ioctl (sd, SIOCGIFCONF, &buf);
                if (ret) {
                        close (sd);
                        goto out;
                }

                while (size <= buf.ifc_len) {
                        size += sizeof (struct ifreq);
                        buf.ifc_len = size;
                        buf.ifc_req = GF_REALLOC (buf.ifc_req, size);
                        ret = ioctl (sd, SIOCGIFCONF, &buf);
                        if (ret) {
                                close (sd);
                                goto out;
                        }
                }

                ifr = buf.ifc_req;
                num_req = size / sizeof (struct ifreq) - 1;

                while (num_req--) {
                        char *addr = inet_ntoa ( *(struct in_addr *)
                                &ifr->ifr_addr.sa_data[sizeof(sa.sin_port)]);
                        if (!strcmp (addr, hostname)) {
                                gf_log ("", GF_LOG_DEBUG, "%s found as local",
                                        addr);
                                found = 1;
                        }
                        ifr++;
                }
        }




out:
        //if (result)
          //      freeaddrinfo (result);

        if (buf.ifc_req)
                GF_FREE (buf.ifc_req);

        return !found;
}

int32_t
glusterd_lock (uuid_t   uuid)
{

        uuid_t  owner;
        char    new_owner_str[50];
        char    owner_str[50];
        int     ret = -1;

        GF_ASSERT (uuid);
        uuid_unparse (uuid, new_owner_str);

        glusterd_get_lock_owner (&owner);

        if (!uuid_is_null (owner)) {
                uuid_unparse (owner, owner_str);
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to get lock"
                        " for uuid: %s, lock held by: %s", new_owner_str,
                        owner_str);
                goto out;
        }

        ret = glusterd_set_lock_owner (uuid);

        if (!ret) {
                gf_log ("glusterd", GF_LOG_NORMAL, "Cluster lock held by"
                         " %s", new_owner_str);
        }

out:
        return ret;
}


int32_t
glusterd_unlock (uuid_t uuid)
{
        uuid_t  owner;
        char    new_owner_str[50];
        char    owner_str[50];
        int32_t ret = -1;

        GF_ASSERT (uuid);
        uuid_unparse (uuid, new_owner_str);

        glusterd_get_lock_owner (&owner);

        if (NULL == owner) {
                gf_log ("glusterd", GF_LOG_ERROR, "Cluster lock not held!");
                goto out;
        }

        ret = uuid_compare (uuid, owner);

        if (ret) {
               uuid_unparse (owner, owner_str);
               gf_log ("glusterd", GF_LOG_ERROR, "Cluster lock held by %s"
                        " ,unlock req from %s!", owner_str, new_owner_str);
               goto out;
        }

        ret = glusterd_unset_lock_owner (uuid);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to clear cluster "
                        "lock");
                goto out;
        }

        ret = 0;

out:
        return ret;
}


int
glusterd_get_uuid (uuid_t *uuid)
{
        glusterd_conf_t         *priv = NULL;

        priv = THIS->private;

        GF_ASSERT (priv);

        uuid_copy (*uuid, priv->uuid);

        return 0;
}

int
glusterd_submit_request (glusterd_peerinfo_t *peerinfo, void *req,
                         call_frame_t *frame, rpc_clnt_prog_t *prog,
                         int procnum, struct iobref *iobref,
                         gd_serialize_t sfunc, xlator_t *this,
                         fop_cbk_fn_t cbkfn)
{
        int                     ret         = -1;
        struct iobuf            *iobuf      = NULL;
        int                     count      = 0;
        char                    new_iobref = 0, start_ping = 0;
        struct iovec            iov         = {0, };

        GF_ASSERT (peerinfo);
        GF_ASSERT (this);

        iobuf = iobuf_get (this->ctx->iobuf_pool);
        if (!iobuf) {
                goto out;
        };

        if (!iobref) {
                iobref = iobref_new ();
                if (!iobref) {
                        goto out;
                }

                new_iobref = 1;
        }

        iobref_add (iobref, iobuf);

        iov.iov_base = iobuf->ptr;
        iov.iov_len  = 128 * GF_UNIT_KB;

        /* Create the xdr payload */
        if (req && sfunc) {
                ret = sfunc (iov, req);
                if (ret == -1) {
                        goto out;
                }
                iov.iov_len = ret;
                count = 1;
        }
        /* Send the msg */
        ret = rpc_clnt_submit (peerinfo->rpc, prog, procnum, cbkfn,
                               &iov, count,
                               NULL, 0, iobref, frame, NULL, 0, NULL, 0, NULL);

        if (ret == 0) {
                pthread_mutex_lock (&peerinfo->rpc->conn.lock);
                {
                        if (!peerinfo->rpc->conn.ping_started) {
                                start_ping = 1;
                        }
                }
                pthread_mutex_unlock (&peerinfo->rpc->conn.lock);
        }

        if (start_ping)
                //client_start_ping ((void *) this);

        ret = 0;
out:
        if (new_iobref) {
                iobref_unref (iobref);
        }

        iobuf_unref (iobuf);

        return ret;
}


struct iobuf *
glusterd_serialize_reply (rpcsvc_request_t *req, void *arg,
                          gd_serialize_t sfunc, struct iovec *outmsg)
{
        struct iobuf            *iob = NULL;
        ssize_t                  retlen = -1;

        /* First, get the io buffer into which the reply in arg will
         * be serialized.
         */
        iob = iobuf_get (req->svc->ctx->iobuf_pool);
        if (!iob) {
                gf_log ("", GF_LOG_ERROR, "Failed to get iobuf");
                goto ret;
        }

        iobuf_to_iovec (iob, outmsg);
        /* Use the given serializer to translate the give C structure in arg
         * to XDR format which will be written into the buffer in outmsg.
         */
        /* retlen is used to received the error since size_t is unsigned and we
         * need -1 for error notification during encoding.
         */
        retlen = sfunc (*outmsg, arg);
        if (retlen == -1) {
                gf_log ("", GF_LOG_ERROR, "Failed to encode message");
                goto ret;
        }

        outmsg->iov_len = retlen;
ret:
        if (retlen == -1) {
                iobuf_unref (iob);
                iob = NULL;
        }

        return iob;
}

int
glusterd_submit_reply (rpcsvc_request_t *req, void *arg,
                       struct iovec *payload, int payloadcount,
                       struct iobref *iobref, gd_serialize_t sfunc)
{
        struct iobuf           *iob        = NULL;
        int                     ret        = -1;
        struct iovec            rsp        = {0,};
        char                    new_iobref = 0;

        if (!req) {
                GF_ASSERT (req);
                goto out;
        }


        if (!iobref) {
                iobref = iobref_new ();
                if (!iobref) {
                        gf_log ("", GF_LOG_ERROR, "out of memory");
                        goto out;
                }

                new_iobref = 1;
        }

        iob = glusterd_serialize_reply (req, arg, sfunc, &rsp);
        if (!iob) {
                gf_log ("", GF_LOG_ERROR, "Failed to serialize reply");
                goto out;
        }

        iobref_add (iobref, iob);

        ret = rpcsvc_submit_generic (req, &rsp, 1, payload, payloadcount,
                                     iobref);

        /* Now that we've done our job of handing the message to the RPC layer
         * we can safely unref the iob in the hope that RPC layer must have
         * ref'ed the iob on receiving into the txlist.
         */
        iobuf_unref (iob);
        if (ret == -1) {
                gf_log ("", GF_LOG_ERROR, "Reply submission failed");
                goto out;
        }

        ret = 0;
out:

        if (new_iobref) {
                iobref_unref (iobref);
        }

        return ret;
}

gf_boolean_t
glusterd_check_volume_exists (char *volname)
{
        char pathname[1024] = {0,};
        struct stat stbuf = {0,};
        int32_t ret = -1;
        glusterd_conf_t *priv = NULL;

        priv = THIS->private;

        snprintf (pathname, 1024, "%s/vols/%s", priv->workdir,
                  volname);

        ret = stat (pathname, &stbuf);

        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "Volume %s does not exist."
                        "stat failed with errno : %d on path: %s",
                        volname, errno, pathname);
                return _gf_false;
        }

        return _gf_true;
}

int32_t
glusterd_volinfo_new (glusterd_volinfo_t **volinfo)
{
        glusterd_volinfo_t      *new_volinfo = NULL;
        int32_t                 ret = -1;

        GF_ASSERT (volinfo);

        new_volinfo = GF_CALLOC (1, sizeof(*new_volinfo),
                                 gf_gld_mt_glusterd_volinfo_t);

        if (!new_volinfo)
                goto out;

        INIT_LIST_HEAD (&new_volinfo->vol_list);
        INIT_LIST_HEAD (&new_volinfo->bricks);

        ret = glusterd_default_xlator_options (new_volinfo);
        if (ret) {
                if (new_volinfo)
                        GF_FREE (new_volinfo);

                goto out;
        }

        *volinfo = new_volinfo;

        ret = 0;

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_brickinfo_delete (glusterd_brickinfo_t *brickinfo)
{
        int32_t         ret = -1;

        GF_ASSERT (brickinfo);

        list_del_init (&brickinfo->brick_list);

        GF_FREE (brickinfo);

        ret = 0;

        return ret;
}

int32_t
glusterd_volinfo_delete (glusterd_volinfo_t *volinfo)
{
        int32_t                 ret = -1;
        glusterd_brickinfo_t    *brickinfo = NULL;
        glusterd_brickinfo_t    *tmp = NULL;

        GF_ASSERT (volinfo);

        list_del_init (&volinfo->vol_list);

        list_for_each_entry_safe (brickinfo, tmp, &volinfo->bricks,
                                   brick_list) {
                ret = glusterd_brickinfo_delete (brickinfo);
                if (ret)
                        goto out;
        }

        dict_unref (volinfo->dict);

        GF_FREE (volinfo);
        ret = 0;

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}


int32_t
glusterd_brickinfo_new (glusterd_brickinfo_t **brickinfo)
{
        glusterd_brickinfo_t      *new_brickinfo = NULL;
        int32_t                   ret = -1;

        GF_ASSERT (brickinfo);

        new_brickinfo = GF_CALLOC (1, sizeof(*new_brickinfo),
                                   gf_gld_mt_glusterd_brickinfo_t);

        if (!new_brickinfo)
                goto out;

        INIT_LIST_HEAD (&new_brickinfo->brick_list);

        *brickinfo = new_brickinfo;

        ret = 0;

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_resolve_brick (glusterd_brickinfo_t *brickinfo)
{
        int32_t                 ret = -1;
        glusterd_peerinfo_t     *peerinfo = NULL;
        glusterd_conf_t         *priv = NULL;
        glusterd_peer_hostname_t        *host = NULL;

        priv = THIS->private;
        GF_ASSERT (priv);

        GF_ASSERT (brickinfo);

        ret = glusterd_friend_find (NULL, brickinfo->hostname, &peerinfo);

        if (!ret) {
                uuid_copy (brickinfo->uuid, peerinfo->uuid);
        } else {
                list_for_each_entry (host, &priv->hostnames, hostname_list) {
                        if (!strcmp (host->hostname, brickinfo->hostname)) {
                                uuid_copy (brickinfo->uuid, priv->uuid);
                                ret = 0;
                                break;
                        }

                }
        }

        if (ret) {
                ret = glusterd_is_local_addr (brickinfo->hostname);
                if (!ret)
                        uuid_copy (brickinfo->uuid, priv->uuid);
        }

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_brickinfo_from_brick (char *brick,
                               glusterd_brickinfo_t **brickinfo)
{
        int32_t                 ret = -1;
        glusterd_brickinfo_t    *new_brickinfo = NULL;
        char                    *hostname = NULL;
        char                    *path = NULL;
        char                    *tmp = NULL;

        GF_ASSERT (brick);
        GF_ASSERT (brickinfo);

        tmp = strdup (brick);

        hostname = strtok (tmp, ":");
        path = strtok (NULL, ":");

        GF_ASSERT (hostname);
        GF_ASSERT (path);

        ret = glusterd_brickinfo_new (&new_brickinfo);

        if (ret)
                goto out;

        strncpy (new_brickinfo->hostname, hostname, 1024);
        strncpy (new_brickinfo->path, path, 1024);

        *brickinfo = new_brickinfo;

        ret = 0;
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_brickinfo_get (char *brick, glusterd_volinfo_t *volinfo,
                        glusterd_brickinfo_t **brickinfo)
{
        int32_t                 ret = -1;
        char                    *hostname = NULL;
        char                    *path = NULL;
        char                    *dup_brick = NULL;
        char                    *free_ptr = NULL;
        glusterd_brickinfo_t    *tmp = NULL;

        GF_ASSERT (brick);
        GF_ASSERT (brickinfo);
        GF_ASSERT (volinfo);

        gf_log ("", GF_LOG_NORMAL, "brick: %s", brick);

        dup_brick = gf_strdup (brick);
        if (!dup_brick) {
                gf_log ("", GF_LOG_ERROR,
                        "Out of memory");
                ret = -1;
                goto out;
        } else {
                free_ptr = dup_brick;
        }

        hostname = strtok (dup_brick, ":");
        path = strtok (NULL, ":");

        if (!hostname || !path) {
                gf_log ("", GF_LOG_ERROR,
                        "brick %s is not of form <HOSTNAME>:<export-dir>",
                        brick);
                ret = -1;
                goto out;
        }

        list_for_each_entry (tmp, &volinfo->bricks, brick_list) {

                if ((!strcmp (tmp->hostname, hostname)) &&
                        !strcmp (tmp->path, path)) {
                        gf_log ("", GF_LOG_NORMAL, "Found brick");
                        ret = 0;
                        break;
                }
        }

        *brickinfo = tmp;

out:
        if (free_ptr)
                GF_FREE (free_ptr);

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_friend_cleanup (glusterd_peerinfo_t *peerinfo)
{
        GF_ASSERT (peerinfo);
        if (peerinfo->rpc) {
                peerinfo->rpc = NULL;
        }
        glusterd_peer_destroy (peerinfo);

        return 0;
}

int32_t
glusterd_volinfo_find (char *volname, glusterd_volinfo_t **volinfo)
{
        glusterd_volinfo_t      *tmp_volinfo = NULL;
        int32_t                 ret = -1;
        xlator_t                *this = NULL;
        glusterd_conf_t         *priv = NULL;

        GF_ASSERT (volname);

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;

        list_for_each_entry (tmp_volinfo, &priv->volumes, vol_list) {
                if (!strcmp (tmp_volinfo->volname, volname)) {
                        gf_log ("", GF_LOG_DEBUG, "Volume %s found", volname);
                        ret = 0;
                        *volinfo = tmp_volinfo;
                        break;
                }
        }


        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}



int32_t
glusterd_volume_start_glusterfs (glusterd_volinfo_t  *volinfo,
                                 glusterd_brickinfo_t  *brickinfo,
                                 int32_t count)
{
        int32_t                 ret = -1;
        xlator_t                *this = NULL;
        glusterd_conf_t         *priv = NULL;
        char                    pidfile[PATH_MAX] = {0,};
        char                    volfile[PATH_MAX] = {0,};
        char                    path[PATH_MAX] = {0,};
        char                    cmd_str[8192] = {0,};
        char                    rundir[PATH_MAX] = {0,};
        char                    exp_path[PATH_MAX] = {0,};
        char                    logfile[PATH_MAX] = {0,};
        int                     port = 0;

        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;

        GLUSTERD_GET_VOLUME_DIR (path, volinfo, priv);
        snprintf (rundir, PATH_MAX, "%s/run", path);
        ret = mkdir (rundir, 0777);

        if ((ret == -1) && (EEXIST != errno)) {
                gf_log ("", GF_LOG_ERROR, "Unable to create rundir %s",
                        rundir);
                goto out;
        }

        port = brickinfo->port;
        if (!port)
                port = pmap_registry_alloc (THIS);

        GLUSTERD_GET_BRICK_PIDFILE (pidfile, path, brickinfo->hostname,
                                    brickinfo->path);

        GLUSTERD_REMOVE_SLASH_FROM_PATH (brickinfo->path, exp_path);
        snprintf (volfile, PATH_MAX, "%s.%s.%s", volinfo->volname,
                  brickinfo->hostname, exp_path);

        if (!brickinfo->logfile) {
                snprintf (logfile, PATH_MAX, "%s/logs/%s.log",
                          priv->workdir, exp_path);
                brickinfo->logfile = gf_strdup (logfile);
        }

        snprintf (cmd_str, 8192,
                  "%s/sbin/glusterfs --xlator-option %s-server.listen-port=%d "
                  "-s localhost --volfile-id %s -p %s --brick-name %s "
                  "--brick-port %d -l %s", GFS_PREFIX, volinfo->volname,
                  port, volfile, pidfile, brickinfo->path, port,
                  brickinfo->logfile);
        ret = gf_system (cmd_str);

        if (ret == 0) {
                //pmap_registry_bind (THIS, port, brickinfo->path);
                brickinfo->port = port;
        }
out:
        return ret;
}

int32_t
glusterd_volume_stop_glusterfs (glusterd_volinfo_t  *volinfo,
                                glusterd_brickinfo_t   *brickinfo,
                                int32_t count)
{
        int32_t                 ret = -1;
        xlator_t                *this = NULL;
        glusterd_conf_t         *priv = NULL;
        char                    pidfile[PATH_MAX] = {0,};
        char                    path[PATH_MAX] = {0,};
        pid_t                   pid = -1;
        FILE                    *file = NULL;

        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;

        GLUSTERD_GET_VOLUME_DIR (path, volinfo, priv);
        GLUSTERD_GET_BRICK_PIDFILE (pidfile, path, brickinfo->hostname,
                                    brickinfo->path);

        file = fopen (pidfile, "r+");

        if (!file) {
                gf_log ("", GF_LOG_ERROR, "Unable to open pidfile: %s",
                                pidfile);
                ret = -1;
                goto out;
        }

        ret = fscanf (file, "%d", &pid);
        if (ret <= 0) {
                gf_log ("", GF_LOG_ERROR, "Unable to read pidfile: %s",
                                pidfile);
                ret = -1;
                goto out;
        }
        fclose (file);
        file = NULL;

        gf_log ("", GF_LOG_NORMAL, "Stopping glusterfs running in pid: %d",
                pid);

        ret = kill (pid, SIGTERM);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to kill pid %d", pid);
                goto out;
        }

        //pmap_registry_remove (THIS, brickinfo->port, brickinfo->path);

        ret = unlink (pidfile);

        if (ret && (ENOENT != errno)) {
                gf_log ("", GF_LOG_ERROR, "Unable to unlink pidfile: %s",
                                pidfile);
                goto out;
        }

        ret = 0;
out:
        if (file)
                fclose (file);
        return ret;
}

int32_t
glusterd_peer_hostname_new (char *hostname, glusterd_peer_hostname_t **name)
{
        glusterd_peer_hostname_t        *peer_hostname = NULL;
        int32_t                         ret = -1;

        GF_ASSERT (hostname);
        GF_ASSERT (name);

        peer_hostname = GF_CALLOC (1, sizeof (*peer_hostname),
                                   gf_gld_mt_peer_hostname_t);

        if (!peer_hostname)
                goto out;

        peer_hostname->hostname = gf_strdup (hostname);
        INIT_LIST_HEAD (&peer_hostname->hostname_list);

        *name = peer_hostname;
        ret = 0;

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_peer_destroy (glusterd_peerinfo_t *peerinfo)
{
        int32_t                         ret = -1;
        glusterd_peer_hostname_t        *name = NULL;
        glusterd_peer_hostname_t        *tmp = NULL;

        if (!peerinfo)
                goto out;

        ret = glusterd_store_delete_peerinfo (peerinfo);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Deleting peer info failed");
        }

        list_del_init (&peerinfo->uuid_list);
        list_for_each_entry_safe (name, tmp, &peerinfo->hostnames,
                                  hostname_list) {
                list_del_init (&name->hostname_list);
                GF_FREE (name->hostname);
        }

        list_del_init (&peerinfo->hostnames);
        GF_FREE (peerinfo);
        peerinfo = NULL;

        ret = 0;

out:
        return ret;
}


gf_boolean_t
glusterd_is_cli_op_req (int32_t op)
{
        switch (op) {
                case GD_MGMT_CLI_CREATE_VOLUME:
                case GD_MGMT_CLI_START_VOLUME:
                case GD_MGMT_CLI_STOP_VOLUME:
                case GD_MGMT_CLI_DELETE_VOLUME:
                case GD_MGMT_CLI_DEFRAG_VOLUME:
                case GD_MGMT_CLI_ADD_BRICK:
                case GD_MGMT_CLI_REMOVE_BRICK:
                case GD_MGMT_CLI_REPLACE_BRICK:
                case GD_MGMT_CLI_LOG_FILENAME:
                case GD_MGMT_CLI_LOG_LOCATE:
                case GD_MGMT_CLI_LOG_ROTATE:
                        return _gf_true;
                        break;
        }

        return _gf_false;
}


int
glusterd_volume_compute_cksum (glusterd_volinfo_t  *volinfo)
{
        int32_t                 ret = -1;
        glusterd_conf_t         *priv = NULL;
        char                    path[PATH_MAX] = {0,};
        char                    cksum_path[PATH_MAX] = {0,};
        char                    filepath[PATH_MAX] = {0,};
        DIR                     *dir = NULL;
        struct dirent           *entry = NULL;
        int                     fd = -1;
        uint32_t                cksum = 0;
        char                    buf[4096] = {0,};

        GF_ASSERT (volinfo);

        priv = THIS->private;
        GF_ASSERT (priv);

        GLUSTERD_GET_VOLUME_DIR (path, volinfo, priv);

        snprintf (cksum_path, sizeof (cksum_path), "%s/%s",
                  path, GLUSTERD_CKSUM_FILE);

        fd = open (cksum_path, O_RDWR | O_APPEND | O_CREAT| O_TRUNC, 0644);

        if (-1 == fd) {
                gf_log ("", GF_LOG_ERROR, "Unable to open %s, errno: %d",
                        cksum_path, errno);
                ret = -1;
                goto out;
        }


        dir = opendir (path);

        glusterd_for_each_entry (entry, dir);

/*        while (entry) {

                snprintf (filepath, sizeof (filepath), "%s/%s", path,
                          entry->d_name);

                if (!strcmp (entry->d_name, "bricks") ||
                     !strcmp (entry->d_name, "run")) {
                        glusterd_for_each_entry (entry, dir);
                        continue;
                }

                ret = get_checksum_for_path (filepath, &cksum);

                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Unable to get checksum"
                                " for path: %s", filepath);
                        goto out;
                }

                snprintf (buf, sizeof (buf), "%s=%u\n", entry->d_name, cksum);
                ret = write (fd, buf, strlen (buf));

                if (ret <= 0) {
                        ret = -1;
                        goto out;
                }

                glusterd_for_each_entry (entry, dir);
        }
*/

        snprintf (filepath, sizeof (filepath), "%s/%s", path,
                  GLUSTERD_VOLUME_INFO_FILE);

        ret = get_checksum_for_path (filepath, &cksum);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get checksum"
                        " for path: %s", filepath);
                goto out;
        }

        snprintf (buf, sizeof (buf), "%s=%u\n", "info", cksum);
        ret = write (fd, buf, strlen (buf));

        if (ret <= 0) {
                ret = -1;
                goto out;
        }

        ret = get_checksum_for_file (fd, &cksum);

        if (ret)
                goto out;

       volinfo->cksum = cksum;

out:
       if (fd > 0)
               close (fd);

       if (dir)
               closedir (dir);

       gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

       return ret;
}

int32_t
glusterd_add_volume_to_dict (glusterd_volinfo_t *volinfo,
                             dict_t  *dict, int32_t count)
{
        int32_t                 ret = -1;
        char                    key[512] = {0,};
        glusterd_brickinfo_t    *brickinfo = NULL;
        int32_t                 i = 1;

        GF_ASSERT (dict);
        GF_ASSERT (volinfo);

        snprintf (key, sizeof (key), "volume%d.name", count);
        ret = dict_set_str (dict, key, volinfo->volname);
        if (ret)
                goto out;

        memset (&key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.type", count);
        ret = dict_set_int32 (dict, key, volinfo->type);
        if (ret)
                goto out;

        memset (&key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.brick_count", count);
        ret = dict_set_int32 (dict, key, volinfo->brick_count);
        if (ret)
                goto out;

        memset (&key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.version", count);
        ret = dict_set_int32 (dict, key, volinfo->version);
        if (ret)
                goto out;

        memset (&key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.status", count);
        ret = dict_set_int32 (dict, key, volinfo->status);
        if (ret)
                goto out;

        memset (&key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.sub_count", count);
        ret = dict_set_int32 (dict, key, volinfo->sub_count);
        if (ret)
                goto out;

        memset (&key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.ckusm", count);
        ret = dict_set_int64 (dict, key, volinfo->cksum);
        if (ret)
                goto out;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                memset (&key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d.brick%d.hostname",
                          count, i);
                ret = dict_set_str (dict, key, brickinfo->hostname);
                if (ret)
                        goto out;

                memset (&key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d.brick%d.path",
                          count, i);
                ret = dict_set_str (dict, key, brickinfo->path);
                if (ret)
                        goto out;

                i++;
        }


out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

int32_t
glusterd_build_volume_dict (dict_t **vols)
{
        int32_t                 ret = -1;
        dict_t                  *dict = NULL;
        glusterd_conf_t         *priv = NULL;
        glusterd_volinfo_t      *volinfo = NULL;
        int32_t                 count = 0;

        priv = THIS->private;

        dict = dict_new ();

        if (!dict)
                goto out;

        list_for_each_entry (volinfo, &priv->volumes, vol_list) {
                count++;
                ret = glusterd_add_volume_to_dict (volinfo, dict, count);
                if (ret)
                        goto out;
        }


        ret = dict_set_int32 (dict, "count", count);
        if (ret)
                goto out;

        *vols = dict;
out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        if (ret)
                dict_unref (dict);

        return ret;
}

int32_t
glusterd_compare_friend_volume (dict_t *vols, int32_t count, int32_t *status)
{

        int32_t                 ret = -1;
        char                    key[512] = {0,};
        glusterd_volinfo_t      *volinfo = NULL;
        char                    *volname = NULL;
        uint32_t                cksum = 0;
        int32_t                 version = 0;

        GF_ASSERT (vols);
        GF_ASSERT (status);

        snprintf (key, sizeof (key), "volume%d.name", count);
        ret = dict_get_str (vols, key, &volname);
        if (ret)
                goto out;

        ret = glusterd_volinfo_find (volname, &volinfo);

        if (ret) {
                *status = GLUSTERD_VOL_COMP_UPDATE_REQ;
                ret = 0;
                goto out;
        }

        memset (&key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.version", count);
        ret = dict_get_int32 (vols, key, &version);
        if (ret)
                goto out;

        if (version != volinfo->version) {
                //Mismatch detected
                ret = 0;
                gf_log ("", GF_LOG_ERROR, "Version of volume %s differ."
                        "local version = %d, remote version = %d",
                        volinfo->volname, volinfo->version, version);
                *status = GLUSTERD_VOL_COMP_UPDATE_REQ;
                goto out;
        }

        //Now, versions are same, compare cksums.
        //
        memset (&key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.ckusm", count);
        ret = dict_get_uint32 (vols, key, &cksum);
        if (ret)
                goto out;

        if (cksum != volinfo->cksum) {
                ret = 0;
                gf_log ("", GF_LOG_ERROR, "Cksums of volume %s differ."
                        " local cksum = %d, remote cksum = %d",
                        volinfo->volname, volinfo->cksum, cksum);
                *status = GLUSTERD_VOL_COMP_RJT;
                goto out;
        }

        *status = GLUSTERD_VOL_COMP_SCS;

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with ret: %d, status: %d",
                ret, *status);
        return ret;
}

int32_t
glusterd_import_friend_volume (dict_t *vols, int count)
{

        int32_t                 ret = -1;
        glusterd_conf_t         *priv = NULL;
        char                    key[512] = {0,};
        glusterd_volinfo_t      *volinfo = NULL;
        char                    *volname = NULL;
        char                    *hostname = NULL;
        char                    *path = NULL;
        glusterd_brickinfo_t    *brickinfo = NULL;
        glusterd_brickinfo_t    *tmp = NULL;
        int                     new_volinfo = 0;
        int                     i = 1;

        GF_ASSERT (vols);

        snprintf (key, sizeof (key), "volume%d.name", count);
        ret = dict_get_str (vols, key, &volname);
        if (ret)
                goto out;

        priv = THIS->private;

        ret = glusterd_volinfo_find (volname, &volinfo);

        if (ret) {
                ret = glusterd_volinfo_new (&volinfo);
                if (ret)
                        goto out;
                strncpy (volinfo->volname, volname, sizeof (volinfo->volname));
                new_volinfo = 1;
        }


        memset (&key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.type", count);
        ret = dict_get_int32 (vols, key, &volinfo->type);
        if (ret)
                goto out;

        memset (&key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.brick_count", count);
        ret = dict_get_int32 (vols, key, &volinfo->brick_count);
        if (ret)
                goto out;

        memset (&key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.version", count);
        ret = dict_get_int32 (vols, key, &volinfo->version);
        if (ret)
                goto out;

        memset (&key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.status", count);
        ret = dict_get_int32 (vols, key, (int32_t *)&volinfo->status);
        if (ret)
                goto out;

        memset (&key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.sub_count", count);
        ret = dict_get_int32 (vols, key, &volinfo->sub_count);
        if (ret)
                goto out;

        memset (&key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.ckusm", count);
        ret = dict_get_uint32 (vols, key, &volinfo->cksum);
        if (ret)
                goto out;

        list_for_each_entry_safe (brickinfo, tmp, &volinfo->bricks,
                                   brick_list) {
                ret = glusterd_brickinfo_delete (brickinfo);
                if (ret)
                        goto out;
        }

        while (i <= volinfo->brick_count) {

                memset (&key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d.brick%d.hostname",
                          count, i);
                ret = dict_get_str (vols, key, &hostname);
                if (ret)
                        goto out;

                memset (&key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d.brick%d.path",
                          count, i);
                ret = dict_get_str (vols, key, &path);
                if (ret)
                        goto out;

                ret = glusterd_brickinfo_new (&brickinfo);
                if (ret)
                        goto out;

                strcpy (brickinfo->path, path);
                strcpy (brickinfo->hostname, hostname);

                list_add_tail (&brickinfo->brick_list, &volinfo->bricks);

                i++;
        }

        if (new_volinfo) {
                list_add_tail (&volinfo->vol_list, &priv->volumes);
                ret = glusterd_store_create_volume (volinfo);
        } else {
                ret = glusterd_store_update_volume (volinfo);
        }

        ret = glusterd_create_volfiles (volinfo);
        if (ret)
                goto out;

        //volinfo->version++;

        ret = glusterd_volume_compute_cksum (volinfo);
        if (ret)
                goto out;


out:
        gf_log ("", GF_LOG_DEBUG, "Returning with ret: %d", ret);
        return ret;
}


int32_t
glusterd_import_friend_volumes (dict_t  *vols)
{
        int32_t                 ret = -1;
        int32_t                 count = 0;
        int                     i = 1;

        GF_ASSERT (vols);

        ret = dict_get_int32 (vols, "count", &count);
        if (ret)
                goto out;

        while (i <= count) {
                ret = glusterd_import_friend_volume (vols, i);
                if (ret)
                        goto out;

                i++;
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_compare_friend_data (dict_t  *vols, int32_t *status)
{
        int32_t                 ret = -1;
        int32_t                 count = 0;
        int                     i = 1;

        GF_ASSERT (vols);
        GF_ASSERT (status);

        ret = dict_get_int32 (vols, "count", &count);
        if (ret)
                goto out;

        while (i <= count) {
                ret = glusterd_compare_friend_volume (vols, i, status);
                if (ret)
                        goto out;

                if (GLUSTERD_VOL_COMP_RJT == *status) {
                        ret = 0;
                        goto out;
                }

                i++;
        }

        if (GLUSTERD_VOL_COMP_UPDATE_REQ == *status) {
                ret = glusterd_import_friend_volumes (vols);
                if (ret)
                        goto out;
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with ret: %d, status: %d",
                ret, *status);

        return ret;
}

int
glusterd_file_copy (int out, int in)
{
        int     read_size = 0;
        char    buffer[16 * 1024];
        int     ret = -1;

        if (out <= 0 || in < 0) {
                gf_log ("", GF_LOG_ERROR, "Invalid File descriptors");
                goto out;
        }

        while (1) {
                read_size = read(in, buffer, sizeof(buffer));

                if (read_size == 0) {
                        ret = 0;
                        break;              /* end of file */
                }

                if (read_size < 0) {
                        ret = -1;
                        break; /*error reading file); */
                }
                write (out, buffer, (unsigned int) read_size);
        }
out:
        return ret;
}

gf_boolean_t
glusterd_is_nfs_started ()
{
        int32_t                 ret = -1;
        xlator_t                *this = NULL;
        glusterd_conf_t         *priv = NULL;
        char                    pidfile[PATH_MAX] = {0,};

        this = THIS;
        GF_ASSERT(this);

        priv = this->private;

        GLUSTERD_GET_NFS_PIDFILE(pidfile);
        ret = access (pidfile, F_OK);

        if (ret == 0)
                return _gf_true;
        else
                return _gf_false;
}

int32_t
glusterd_nfs_server_start ()
{
        int32_t                 ret = -1;
        xlator_t                *this = NULL;
        glusterd_conf_t         *priv = NULL;
        char                    pidfile[PATH_MAX] = {0,};
        char                    *volfile = NULL;
        char                    path[PATH_MAX] = {0,};
        char                    cmd_str[8192] = {0,};
        char                    rundir[PATH_MAX] = {0,};

        this = THIS;
        GF_ASSERT(this);

        priv = this->private;

        GLUSTERD_GET_NFS_DIR(path, priv);
        snprintf (rundir, PATH_MAX, "%s/run", path);
        ret = mkdir (rundir, 0777);

        if ((ret == -1) && (EEXIST != errno)) {
                gf_log ("", GF_LOG_ERROR, "Unable to create rundir %s",
                        rundir);
                goto out;
        }

        GLUSTERD_GET_NFS_PIDFILE(pidfile);
        volfile = glusterd_get_nfs_filepath ();
        if (!volfile) {
                ret = -1;
                goto out;
        }

        ret = access (volfile, F_OK);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Nfs Volfile %s is not present",
                        volfile);
                goto out;
        }

        snprintf (cmd_str, 8192,
                  "%s/sbin/glusterfs  -f %s -p %s",
                  GFS_PREFIX, volfile, pidfile);
        ret = gf_system (cmd_str);

out:
        if (volfile)
                GF_FREE(volfile);
        return ret;
}

int32_t
glusterd_nfs_server_stop ()
{
        int32_t                 ret = -1;
        xlator_t                *this = NULL;
        glusterd_conf_t         *priv = NULL;
        char                    pidfile[PATH_MAX] = {0,};
        char                    path[PATH_MAX] = {0,};
        pid_t                   pid = -1;
        FILE                    *file = NULL;

        this = THIS;
        GF_ASSERT(this);

        priv = this->private;

        GLUSTERD_GET_NFS_DIR(path, priv);
        GLUSTERD_GET_NFS_PIDFILE(pidfile);

        file = fopen (pidfile, "r+");

        if (!file) {
                gf_log ("", GF_LOG_ERROR, "Unable to open pidfile: %s",
                                pidfile);
                ret = -1;
                goto out;
        }

        ret = fscanf (file, "%d", &pid);
        if (ret <= 0) {
                gf_log ("", GF_LOG_ERROR, "Unable to read pidfile: %s",
                                pidfile);
                ret = -1;
                goto out;
        }
        fclose (file);
        file = NULL;

        gf_log ("", GF_LOG_NORMAL, "Stopping glusterfs running in pid: %d",
                pid);

        ret = kill (pid, SIGTERM);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to kill pid %d", pid);
                goto out;
        }

        ret = unlink (pidfile);

        if (ret && (ENOENT != errno)) {
                gf_log ("", GF_LOG_ERROR, "Unable to unlink pidfile: %s",
                                pidfile);
                goto out;
        }

        ret = 0;
out:
        if (file)
                fclose (file);
        return ret;
}

gf_boolean_t
glusterd_are_all_volumes_stopped ()
{
        glusterd_conf_t                         *priv = NULL;
        xlator_t                                *this = NULL;
        glusterd_volinfo_t                      *voliter = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        list_for_each_entry (voliter, &priv->volumes, vol_list) {
                if (voliter->status == GLUSTERD_STATUS_STARTED)
                        return _gf_false;
        }

        return _gf_true;

}

