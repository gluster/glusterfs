/*
  Copyright (c) 2006-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
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
#include "md5.h"
#include "run.h"
#include "compat-errno.h"
#include "statedump.h"
#include "syscall.h"
#include "glusterd-mem-types.h"
#include "glusterd.h"
#include "glusterd-op-sm.h"
#include "glusterd-sm.h"
#include "glusterd-utils.h"
#include "glusterd-store.h"
#include "glusterd-volgen.h"
#include "glusterd-pmap.h"

#include "xdr-generic.h"

#include <sys/resource.h>
#include <inttypes.h>
#include <signal.h>
#include <sys/types.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <rpc/pmap_clnt.h>
#include <unistd.h>
#ifdef GF_SOLARIS_HOST_OS
#include <sys/sockio.h>
#endif

#define MOUNT_PROGRAM 100005
#define NFS_PROGRAM 100003
#define NFSV3_VERSION 3
#define MOUNTV3_VERSION 3
#define MOUNTV1_VERSION 1

char    *glusterd_sock_dir = "/tmp";
static glusterd_lock_t lock;

int32_t
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

gf_boolean_t
glusterd_is_fuse_available ()
{

        int     fd = 0;

        fd = open ("/dev/fuse", O_RDWR);

        if (fd > -1 && !close (fd))
                return _gf_true;
        else
                return _gf_false;
}

gf_boolean_t
glusterd_is_loopback_localhost (const struct sockaddr *sa, char *hostname)
{
        GF_ASSERT (sa);

        gf_boolean_t is_local = _gf_false;
        const struct in_addr *addr4 = NULL;
        const struct in6_addr *addr6 = NULL;
        uint8_t      *ap   = NULL;
        struct in6_addr loopbackaddr6 = IN6ADDR_LOOPBACK_INIT;

        switch (sa->sa_family) {
                case AF_INET:
                        addr4 = &(((struct sockaddr_in *)sa)->sin_addr);
                        ap = (uint8_t*)&addr4->s_addr;
                        if (ap[0] == 127)
                                is_local = _gf_true;
                        break;

                case AF_INET6:
                        addr6 = &(((struct sockaddr_in6 *)sa)->sin6_addr);
                        if (memcmp (addr6, &loopbackaddr6,
                                    sizeof (loopbackaddr6)) == 0)
                                is_local = _gf_true;
                        break;

                default:
                        if (hostname)
                                gf_log ("glusterd", GF_LOG_ERROR,
                                        "unknown address family %d for %s",
                                        sa->sa_family, hostname);
                        break;
        }

        return is_local;
}

char *
get_ip_from_addrinfo (struct addrinfo *addr, char **ip)
{
        char buf[64];
        void *in_addr = NULL;
        struct sockaddr_in *s4 = NULL;
        struct sockaddr_in6 *s6 = NULL;

        switch (addr->ai_family)
        {
                case AF_INET:
                        s4 = (struct sockaddr_in *)addr->ai_addr;
                        in_addr = &s4->sin_addr;
                        break;

                case AF_INET6:
                        s6 = (struct sockaddr_in6 *)addr->ai_addr;
                        in_addr = &s6->sin6_addr;
                        break;

                default:
                        gf_log ("glusterd", GF_LOG_ERROR, "Invalid family");
                        return NULL;
        }

        if (!inet_ntop(addr->ai_family, in_addr, buf, sizeof(buf))) {
                gf_log ("glusterd", GF_LOG_ERROR, "String conversion failed");
                return NULL;
        }

        *ip = strdup (buf);
        return *ip;
}

int32_t
glusterd_is_local_addr (char *hostname)
{
        int32_t         ret = -1;
        struct          addrinfo *result = NULL;
        struct          addrinfo *res = NULL;
        int32_t         found = 0;
        int             sd = -1;
        char            *ip = NULL;

        ret = getaddrinfo (hostname, NULL, NULL, &result);

        if (ret != 0) {
                gf_log ("", GF_LOG_ERROR, "error in getaddrinfo: %s\n",
                        gai_strerror(ret));
                goto out;
        }

        for (res = result; res != NULL; res = res->ai_next) {
                found = glusterd_is_loopback_localhost (res->ai_addr, hostname);
                if (found)
                        goto out;
        }

        for (res = result; res != NULL; res = res->ai_next) {
                gf_log ("glusterd", GF_LOG_DEBUG, "%s ", get_ip_from_addrinfo (res, &ip));
                sd = socket (res->ai_family, SOCK_DGRAM, 0);
                if (sd == -1)
                        goto out;
                /*If bind succeeds then its a local address*/
                ret = bind (sd, res->ai_addr, res->ai_addrlen);
                if (ret == 0) {
                        found = _gf_true;
                        gf_log ("glusterd", GF_LOG_INFO, "%s is local", get_ip_from_addrinfo (res, &ip));
                        close (sd);
                        break;
                }
                close (sd);
        }

out:
        if (result)
                freeaddrinfo (result);

        if (found)
                gf_log ("glusterd", GF_LOG_DEBUG, "%s is local", hostname);
        else
                gf_log ("glusterd", GF_LOG_DEBUG, "%s is not local", hostname);

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

        glusterd_get_lock_owner (&owner);

        if (!uuid_is_null (owner)) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to get lock"
                        " for uuid: %s, lock held by: %s",
                        uuid_utoa_r (uuid, new_owner_str),
                        uuid_utoa_r (owner, owner_str));
                goto out;
        }

        ret = glusterd_set_lock_owner (uuid);

        if (!ret) {
                gf_log ("glusterd", GF_LOG_INFO, "Cluster lock held by"
                         " %s", uuid_utoa (uuid));
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

        glusterd_get_lock_owner (&owner);

        if (NULL == owner) {
                gf_log ("glusterd", GF_LOG_ERROR, "Cluster lock not held!");
                goto out;
        }

        ret = uuid_compare (uuid, owner);

        if (ret) {
               gf_log ("glusterd", GF_LOG_ERROR, "Cluster lock held by %s"
                        " ,unlock req from %s!", uuid_utoa_r (owner ,owner_str)
                        , uuid_utoa_r (uuid, new_owner_str));
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
glusterd_submit_request (struct rpc_clnt *rpc, void *req,
                         call_frame_t *frame, rpc_clnt_prog_t *prog,
                         int procnum, struct iobref *iobref,
                         xlator_t *this, fop_cbk_fn_t cbkfn, xdrproc_t xdrproc)
{
        int                     ret         = -1;
        struct iobuf            *iobuf      = NULL;
        int                     count      = 0;
        char                    new_iobref = 0, start_ping = 0;
        struct iovec            iov         = {0, };
        ssize_t                 req_size    = 0;

        GF_ASSERT (rpc);
        GF_ASSERT (this);

        if (req) {
                req_size = xdr_sizeof (xdrproc, req);
                iobuf = iobuf_get2 (this->ctx->iobuf_pool, req_size);
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
                iov.iov_len  = iobuf_pagesize (iobuf);

                /* Create the xdr payload */
                ret = xdr_serialize_generic (iov, req, xdrproc);
                if (ret == -1) {
                        goto out;
                }
                iov.iov_len = ret;
                count = 1;
        }

        /* Send the msg */
        ret = rpc_clnt_submit (rpc, prog, procnum, cbkfn,
                               &iov, count,
                               NULL, 0, iobref, frame, NULL, 0, NULL, 0, NULL);

        if (ret == 0) {
                pthread_mutex_lock (&rpc->conn.lock);
                {
                        if (!rpc->conn.ping_started) {
                                start_ping = 1;
                        }
                }
                pthread_mutex_unlock (&rpc->conn.lock);
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
                          struct iovec *outmsg, xdrproc_t xdrproc)
{
        struct iobuf            *iob = NULL;
        ssize_t                  retlen = -1;
        ssize_t                  rsp_size = 0;

        /* First, get the io buffer into which the reply in arg will
         * be serialized.
         */
        rsp_size = xdr_sizeof (xdrproc, arg);
        iob = iobuf_get2 (req->svc->ctx->iobuf_pool, rsp_size);
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
        retlen = xdr_serialize_generic (*outmsg, arg, xdrproc);
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
                       struct iobref *iobref, xdrproc_t xdrproc)
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

        iob = glusterd_serialize_reply (req, arg, &rsp, xdrproc);
        if (!iob) {
                gf_log ("", GF_LOG_ERROR, "Failed to serialize reply");
        } else {
                iobref_add (iobref, iob);
        }

        ret = rpcsvc_submit_generic (req, &rsp, 1, payload, payloadcount,
                                     iobref);

        /* Now that we've done our job of handing the message to the RPC layer
         * we can safely unref the iob in the hope that RPC layer must have
         * ref'ed the iob on receiving into the txlist.
         */
        if (ret == -1) {
                gf_log ("", GF_LOG_ERROR, "Reply submission failed");
                goto out;
        }

        ret = 0;
out:

        if (new_iobref) {
                iobref_unref (iobref);
        }

        if (iob)
                iobuf_unref (iob);
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

        new_volinfo->dict = dict_new ();
        if (!new_volinfo->dict) {
                if (new_volinfo)
                        GF_FREE (new_volinfo);

                goto out;
        }

        new_volinfo->gsync_slaves = dict_new ();
        if (!new_volinfo->gsync_slaves) {
                if (new_volinfo)
                        GF_FREE (new_volinfo);

                goto out;
        }

        new_volinfo->xl = THIS;

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

        if (brickinfo->logfile)
                GF_FREE (brickinfo->logfile);
        GF_FREE (brickinfo);

        ret = 0;

        return ret;
}

int32_t
glusterd_volume_brickinfos_delete (glusterd_volinfo_t *volinfo)
{
        glusterd_brickinfo_t    *brickinfo = NULL;
        glusterd_brickinfo_t    *tmp = NULL;
        int32_t                 ret = 0;

        GF_ASSERT (volinfo);

        list_for_each_entry_safe (brickinfo, tmp, &volinfo->bricks,
                                   brick_list) {
                ret = glusterd_brickinfo_delete (brickinfo);
                if (ret)
                        goto out;
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_volinfo_delete (glusterd_volinfo_t *volinfo)
{
        int32_t                 ret = -1;

        GF_ASSERT (volinfo);

        list_del_init (&volinfo->vol_list);

        ret = glusterd_volume_brickinfos_delete (volinfo);
        if (ret)
                goto out;
        if (volinfo->dict)
                dict_unref (volinfo->dict);
        if (volinfo->gsync_slaves)
                dict_unref (volinfo->gsync_slaves);
        if (volinfo->logdir)
                GF_FREE (volinfo->logdir);

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

        GF_ASSERT (brickinfo);

        ret = glusterd_hostname_to_uuid (brickinfo->hostname, brickinfo->uuid);
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
        char                    *tmp_host = NULL;
        char                    *tmp_path = NULL;

        GF_ASSERT (brick);
        GF_ASSERT (brickinfo);

        tmp_host = gf_strdup (brick);
        if (tmp_host)
                get_host_name (tmp_host, &hostname);
        tmp_path = gf_strdup (brick);
        if (tmp_path)
                get_path_name (tmp_path, &path);

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
        if (tmp_host)
                GF_FREE (tmp_host);
        if (tmp_host)
                GF_FREE (tmp_path);
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_volume_brickinfo_get (uuid_t uuid, char *hostname, char *path,
                               glusterd_volinfo_t *volinfo,
                               glusterd_brickinfo_t **brickinfo)
{
        glusterd_brickinfo_t    *brickiter = NULL;
        uuid_t                  peer_uuid = {0};
        int32_t                 ret = -1;
        int32_t                 brick_path_len = 0;
        int32_t                 path_len = 0;
        int32_t                 smaller_path = 0;
        gf_boolean_t            is_path_smaller = _gf_true;

        if (uuid) {
                uuid_copy (peer_uuid, uuid);
        } else {
                ret = glusterd_hostname_to_uuid (hostname, peer_uuid);
                if (ret)
                        goto out;
        }
        ret = -1;
        path_len = strlen (path);
        list_for_each_entry (brickiter, &volinfo->bricks, brick_list) {

                if (uuid_is_null (brickiter->uuid)) {
                        ret = glusterd_resolve_brick (brickiter);
                        if (ret)
                                goto out;
                }
                brick_path_len = strlen (brickiter->path);
                smaller_path = min (brick_path_len, path_len);
                if (smaller_path != path_len)
                        is_path_smaller = _gf_false;
                if ((!uuid_compare (peer_uuid, brickiter->uuid)) &&
                        !strcmp (brickiter->path, path)) {
                        gf_log ("", GF_LOG_INFO, "Found brick");
                        ret = 0;
                        if (brickinfo)
                                *brickinfo = brickiter;
                        break;
                } else {
                        if ((!uuid_compare (peer_uuid, brickiter->uuid)) &&
                            !strncmp (brickiter->path, path, smaller_path)) {
                                if (is_path_smaller == _gf_true)  {
                                        if (brickiter->path[smaller_path] == '/') {
                                                ret = 0;
                                                gf_log ("", GF_LOG_INFO,
                                                        "given path %s lies"
                                                        " within %s", path,
                                                        brickiter->path);
                                                *brickinfo = brickiter;
                                                break;
                                        }
                                } else
                                        if (path[smaller_path] == '/') {
                                                gf_log ("", GF_LOG_INFO,
                                                        "brick %s is a part of"
                                                        " %s", brickiter->path,
                                                        path);
                                                ret = 0;
                                                *brickinfo = brickiter;
                                                break;
                                        }
                        }
                        ret = -1;
                }
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_volume_brickinfo_get_by_brick (char *brick,
                                        glusterd_volinfo_t *volinfo,
                                        glusterd_brickinfo_t **brickinfo)
{
        int32_t                 ret = -1;
        char                    *hostname = NULL;
        char                    *path = NULL;
        char                    *tmp_host = NULL;
        char                    *tmp_path = NULL;

        GF_ASSERT (brick);
        GF_ASSERT (volinfo);

        gf_log ("", GF_LOG_INFO, "brick: %s", brick);

        tmp_host = gf_strdup (brick);
        if (tmp_host)
                get_host_name (tmp_host, &hostname);
        tmp_path = gf_strdup (brick);
        if (tmp_path)
                get_path_name (tmp_path, &path);

        if (!hostname || !path) {
                gf_log ("", GF_LOG_ERROR,
                        "brick %s is not of form <HOSTNAME>:<export-dir>",
                        brick);
                ret = -1;
                goto out;
        }

        ret = glusterd_volume_brickinfo_get (NULL, hostname, path, volinfo,
                                             brickinfo);
out:
        if (tmp_host)
                GF_FREE (tmp_host);
        if (tmp_path)
                GF_FREE (tmp_path);
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

gf_boolean_t
glusterd_is_brick_decommissioned (glusterd_volinfo_t *volinfo, char *hostname,
                                  char *path)
{
        gf_boolean_t            decommissioned = _gf_false;
        glusterd_brickinfo_t    *brickinfo = NULL;
        int                     ret = -1;

        ret = glusterd_volume_brickinfo_get (NULL, hostname, path, volinfo,
                                             &brickinfo);
        if (ret)
                goto out;
        decommissioned = brickinfo->decommissioned;
out:
        return decommissioned;
}

int32_t
glusterd_friend_cleanup (glusterd_peerinfo_t *peerinfo)
{
        GF_ASSERT (peerinfo);
        glusterd_peerctx_t *peerctx = NULL;

        if (peerinfo->rpc) {
                peerctx = peerinfo->rpc->mydata;
                peerinfo->rpc->mydata = NULL;
                peerinfo->rpc = rpc_clnt_unref (peerinfo->rpc);
                peerinfo->rpc = NULL;
                if (peerctx)
                        GF_FREE (peerctx);
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
glusterd_service_stop (const char *service, char *pidfile, int sig,
                       gf_boolean_t force_kill)
{
        int32_t  ret = -1;
        pid_t    pid = -1;
        FILE    *file = NULL;
        gf_boolean_t is_locked = _gf_false;

        file = fopen (pidfile, "r+");

        if (!file) {
                gf_log ("", GF_LOG_ERROR, "Unable to open pidfile: %s",
                                pidfile);
                if (errno == ENOENT) {
                        gf_log ("",GF_LOG_TRACE, "%s may not be running",
                                service);
                        ret = 0;
                        goto out;
                }
                ret = -1;
                goto out;
        }
        ret = lockf (fileno (file), F_TLOCK, 0);
        if (!ret) {
                is_locked = _gf_true;
                ret = unlink (pidfile);
                if (ret && (ENOENT != errno)) {
                        gf_log ("", GF_LOG_ERROR, "Unable to "
                                "unlink stale pidfile: %s", pidfile);
                } else if (ret && (ENOENT == errno)){
                        ret = 0;
                        gf_log ("", GF_LOG_INFO, "Brick already stopped");
                }
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

        gf_log ("", GF_LOG_INFO, "Stopping gluster %s running in pid: %d",
                service, pid);

        ret = kill (pid, sig);

        if (force_kill) {
                sleep (1);
                file = fopen (pidfile, "r+");
                if (!file) {
                        ret = 0;
                        goto out;
                }
                ret = lockf (fileno (file), F_TLOCK, 0);
                if (ret && ((EAGAIN == errno) || (EACCES == errno))) {
                        ret = kill (pid, SIGKILL);
                        if (ret) {
                                gf_log ("", GF_LOG_ERROR, "Unable to "
                                        "kill pid %d reason: %s", pid,
                                        strerror(errno));
                                goto out;
                        }

                } else if (0 == ret){
                        is_locked = _gf_true;
                }
                ret = unlink (pidfile);
                if (ret && (ENOENT != errno)) {
                        gf_log ("", GF_LOG_ERROR, "Unable to "
                                "unlink pidfile: %s", pidfile);
                        goto out;
                }
        }

        ret = 0;
out:
        if (is_locked && file)
                if (lockf (fileno (file), F_ULOCK, 0) < 0)
                        gf_log ("", GF_LOG_WARNING, "Cannot unlock pidfile: %s"
                                " reason: %s", pidfile, strerror(errno));
        if (file)
                fclose (file);
        return ret;
}

void
glusterd_set_brick_socket_filepath (glusterd_volinfo_t *volinfo,
                                    glusterd_brickinfo_t *brickinfo,
                                    char *sockpath, size_t len)
{
        char                    export_path[PATH_MAX] = {0,};
        char                    sock_filepath[PATH_MAX] = {0,};
        char                    md5_sum[MD5_DIGEST_LEN*2+1] = {0,};
        char                    volume_dir[PATH_MAX] = {0,};
        xlator_t                *this = NULL;
        glusterd_conf_t         *priv = NULL;
        int                     expected_file_len = 0;

        expected_file_len = strlen (glusterd_sock_dir) + strlen ("/") +
                            MD5_DIGEST_LEN*2 + strlen (".socket") + 1;
        GF_ASSERT (len >= expected_file_len);
        this = THIS;
        GF_ASSERT (this);

        priv = this->private;

        GLUSTERD_GET_VOLUME_DIR (volume_dir, volinfo, priv);
        GLUSTERD_REMOVE_SLASH_FROM_PATH (brickinfo->path, export_path);
        snprintf (sock_filepath, PATH_MAX, "%s/run/%s-%s",
                  volume_dir, brickinfo->hostname, export_path);
        _get_md5_str (md5_sum, sizeof (md5_sum),
                              (uint8_t*)sock_filepath, strlen (sock_filepath));

        snprintf (sockpath, len, "%s/%s.socket", glusterd_sock_dir, md5_sum);
}

/* connection happens only if it is not aleady connected,
 * reconnections are taken care by rpc-layer
 */
int32_t
glusterd_brick_connect (glusterd_volinfo_t  *volinfo,
                        glusterd_brickinfo_t  *brickinfo)
{
        int                     ret = 0;
        char                    socketpath[PATH_MAX] = {0};
        dict_t                  *options = NULL;
        struct rpc_clnt         *rpc = NULL;

        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);

        if (brickinfo->rpc == NULL) {
                glusterd_set_brick_socket_filepath (volinfo, brickinfo,
                                                    socketpath,
                                                    sizeof (socketpath));
                ret = rpc_clnt_transport_unix_options_build (&options, socketpath);
                if (ret)
                        goto out;
                ret = glusterd_rpc_create (&rpc, options,
                                           glusterd_brick_rpc_notify,
                                           brickinfo);
                if (ret)
                        goto out;
                brickinfo->rpc = rpc;
        }
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_volume_start_glusterfs (glusterd_volinfo_t  *volinfo,
                                 glusterd_brickinfo_t  *brickinfo)
{
        int32_t                 ret = -1;
        xlator_t                *this = NULL;
        glusterd_conf_t         *priv = NULL;
        char                    pidfile[PATH_MAX] = {0,};
        char                    volfile[PATH_MAX] = {0,};
        char                    path[PATH_MAX] = {0,};
        runner_t                runner = {0,};
        char                    rundir[PATH_MAX] = {0,};
        char                    exp_path[PATH_MAX] = {0,};
        char                    logfile[PATH_MAX] = {0,};
        int                     port = 0;
        int                     rdma_port = 0;
        FILE                    *file = NULL;
        gf_boolean_t            is_locked = _gf_false;
        char                    socketpath[PATH_MAX] = {0};

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

        glusterd_set_brick_socket_filepath (volinfo, brickinfo, socketpath,
                                            sizeof (socketpath));
        GLUSTERD_GET_BRICK_PIDFILE (pidfile, path, brickinfo->hostname,
                                    brickinfo->path);

        file = fopen (pidfile, "r+");
        if (file) {
                ret = lockf (fileno (file), F_TLOCK, 0);
                if (ret && ((EAGAIN == errno) || (EACCES == errno))) {
                        ret = 0;
                        gf_log ("", GF_LOG_INFO, "brick %s:%s "
                                "already started", brickinfo->hostname,
                                brickinfo->path);
                        goto connect;
                }
        }

        ret = pmap_registry_search (this, brickinfo->path,
                                    GF_PMAP_PORT_BRICKSERVER);
        if (ret) {
                ret = 0;
                file = fopen (pidfile, "r+");
                if (file) {
                        ret = lockf (fileno (file), F_TLOCK, 0);
                        if (ret && ((EAGAIN == errno) || (EACCES == errno))) {
                                ret = 0;
                                gf_log ("", GF_LOG_INFO, "brick %s:%s "
                                        "already started", brickinfo->hostname,
                                        brickinfo->path);
                                goto connect;
                        } else if (0 == ret) {
                                is_locked = _gf_true;
                        }
                }
                /* This means, pmap has the entry, remove it */
                ret = pmap_registry_remove (this, 0, brickinfo->path,
                                            GF_PMAP_PORT_BRICKSERVER, NULL);
        }
        unlink (pidfile);

        gf_log ("", GF_LOG_INFO, "About to start glusterfs"
                " for brick %s:%s", brickinfo->hostname,
                brickinfo->path);
        GLUSTERD_REMOVE_SLASH_FROM_PATH (brickinfo->path, exp_path);
        snprintf (volfile, PATH_MAX, "%s.%s.%s", volinfo->volname,
                  brickinfo->hostname, exp_path);

        if (!brickinfo->logfile && volinfo->logdir) {
                snprintf (logfile, PATH_MAX, "%s/%s.log", volinfo->logdir,
                                                          exp_path);
                brickinfo->logfile = gf_strdup (logfile);
        } else if (!brickinfo->logfile) {
                snprintf (logfile, PATH_MAX, "%s/bricks/%s.log",
                          DEFAULT_LOG_FILE_DIRECTORY, exp_path);
                brickinfo->logfile = gf_strdup (logfile);
        }

        port = brickinfo->port;
        if (!port)
                port = pmap_registry_alloc (THIS);

        runinit (&runner);
        runner_add_args (&runner, GFS_PREFIX"/sbin/glusterfsd",
                         "-s", "localhost", "--volfile-id", volfile,
                         "-p", pidfile, "-S", socketpath,
                         "--brick-name", brickinfo->path,
                         "-l", brickinfo->logfile, "--brick-port",  NULL);

        if (volinfo->transport_type != GF_TRANSPORT_BOTH_TCP_RDMA) {
                runner_argprintf (&runner, "%d", port);
        } else {
                rdma_port = brickinfo->rdma_port;
                if (!rdma_port)
                        rdma_port = pmap_registry_alloc (THIS);
                runner_argprintf (&runner, "%d,%d", port, rdma_port);
                runner_add_arg (&runner, "--xlator-option");
                runner_argprintf (&runner, "%s-server.transport.rdma.listen-port=%d",
                                  volinfo->volname, rdma_port);
        }

        runner_add_arg (&runner, "--xlator-option");
        runner_argprintf (&runner, "%s-server.listen-port=%d",
                          volinfo->volname, port);

        runner_log (&runner, "", GF_LOG_DEBUG, "Starting GlusterFS");
        ret = runner_run (&runner);

        if (ret == 0) {
                //pmap_registry_bind (THIS, port, brickinfo->path);
                brickinfo->port = port;
                brickinfo->rdma_port = rdma_port;
        }

connect:
        ret = glusterd_brick_connect (volinfo, brickinfo);
        if (ret)
                goto out;
out:
        if (is_locked && file)
                if (lockf (fileno (file), F_ULOCK, 0) < 0)
                        gf_log ("", GF_LOG_WARNING, "Cannot unlock pidfile: %s"
                                " reason: %s", pidfile, strerror(errno));
        if (file)
                fclose (file);
        return ret;
}

int32_t
glusterd_brick_unlink_socket_file (glusterd_volinfo_t *volinfo,
                                   glusterd_brickinfo_t *brickinfo)
{
        char                    path[PATH_MAX] = {0,};
        char                    socketpath[PATH_MAX] = {0};
        xlator_t                *this = NULL;
        glusterd_conf_t         *priv = NULL;
        int                     ret = 0;

        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GLUSTERD_GET_VOLUME_DIR (path, volinfo, priv);
        glusterd_set_brick_socket_filepath (volinfo, brickinfo, socketpath,
                                            sizeof (socketpath));
        ret = unlink (socketpath);
        if (ret && (ENOENT == errno)) {
                ret = 0;
        } else {
                gf_log ("glusterd", GF_LOG_ERROR, "Failed to remove %s"
                        " error: %s", socketpath, strerror (errno));
        }

        return ret;
}

int32_t
glusterd_brick_disconnect (glusterd_brickinfo_t *brickinfo)
{
        GF_ASSERT (brickinfo);

        if (brickinfo->rpc) {
                rpc_clnt_unref (brickinfo->rpc);
                brickinfo->rpc = NULL;
        }
        return 0;
}

int32_t
glusterd_volume_stop_glusterfs (glusterd_volinfo_t  *volinfo,
                                glusterd_brickinfo_t   *brickinfo)
{
        xlator_t                *this = NULL;
        glusterd_conf_t         *priv = NULL;
        char                    pidfile[PATH_MAX] = {0,};
        char                    path[PATH_MAX] = {0,};
        int                     ret = 0;

        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        (void) glusterd_brick_disconnect (brickinfo);

        GLUSTERD_GET_VOLUME_DIR (path, volinfo, priv);
        GLUSTERD_GET_BRICK_PIDFILE (pidfile, path, brickinfo->hostname,
                                    brickinfo->path);

        ret = glusterd_service_stop ("brick", pidfile, SIGTERM, _gf_false);
        if (ret == 0) {
                glusterd_set_brick_status (brickinfo, GF_BRICK_STOPPED);
                (void) glusterd_brick_unlink_socket_file (volinfo, brickinfo);
        }
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

int
glusterd_volume_compute_cksum (glusterd_volinfo_t  *volinfo)
{
        int32_t                 ret = -1;
        glusterd_conf_t         *priv = NULL;
        char                    path[PATH_MAX] = {0,};
        char                    cksum_path[PATH_MAX] = {0,};
        char                    filepath[PATH_MAX] = {0,};
        int                     fd = -1;
        uint32_t                cksum = 0;
        char                    buf[4096] = {0,};
        char                    sort_filepath[PATH_MAX] = {0};
        gf_boolean_t            unlink_sortfile = _gf_false;
        int                     sort_fd = 0;
        runner_t                runner;

        GF_ASSERT (volinfo);

        priv = THIS->private;
        GF_ASSERT (priv);

        GLUSTERD_GET_VOLUME_DIR (path, volinfo, priv);

        snprintf (cksum_path, sizeof (cksum_path), "%s/%s",
                  path, GLUSTERD_CKSUM_FILE);

        fd = open (cksum_path, O_RDWR | O_APPEND | O_CREAT| O_TRUNC, 0644);

        if (-1 == fd) {
                gf_log (THIS->name, GF_LOG_ERROR, "Unable to open %s, errno: %d",
                        cksum_path, errno);
                ret = -1;
                goto out;
        }

        snprintf (filepath, sizeof (filepath), "%s/%s", path,
                  GLUSTERD_VOLUME_INFO_FILE);
        snprintf (sort_filepath, sizeof (sort_filepath), "/tmp/%s.XXXXXX",
                  volinfo->volname);
        sort_fd = mkstemp (sort_filepath);
        if (sort_fd < 0) {
                gf_log (THIS->name, GF_LOG_ERROR, "Could not generate temp file, "
                        "reason: %s for volume: %s", strerror (errno),
                        volinfo->volname);
                goto out;
        } else {
                unlink_sortfile = _gf_true;
        }

        /* sort the info file, result in sort_filepath */
        runinit (&runner);
        runner_add_args (&runner, "sort", filepath, NULL);
        runner_redir (&runner, STDOUT_FILENO, sort_fd);

        ret = runner_run (&runner);
        close (sort_fd);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to sort file %s to %s",
                        filepath, sort_filepath);
                goto out;
        }
        ret = get_checksum_for_path (sort_filepath, &cksum);

        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Unable to get checksum"
                        " for path: %s", sort_filepath);
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
        if (unlink_sortfile)
               unlink (sort_filepath);
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

void
_add_volinfo_dict_to_prdict (dict_t *this, char *key, data_t *value, void *data)
{
        glusterd_voldict_ctx_t   *ctx = NULL;
        char                    optkey[512] = {0,};
        int                     ret = -1;

        ctx = data;
        snprintf (optkey, sizeof (optkey), "volume%d.%s%d", ctx->count,
                  ctx->key_name, ctx->opt_count);
        ret = dict_set_str (ctx->dict, optkey, key);
        if (ret)
                gf_log ("", GF_LOG_ERROR, "option add for %s%d %s",
                        ctx->key_name, ctx->count, key);
        snprintf (optkey, sizeof (optkey), "volume%d.%s%d", ctx->count,
                  ctx->val_name, ctx->opt_count);
        ret = dict_set_str (ctx->dict, optkey, value->data);
        if (ret)
                gf_log ("", GF_LOG_ERROR, "option add for %s%d %s",
                        ctx->val_name, ctx->count, value->data);
        ctx->opt_count++;

        return;
}

int32_t
glusterd_add_volume_to_dict (glusterd_volinfo_t *volinfo,
                             dict_t  *dict, int32_t count)
{
        int32_t                 ret             = -1;
        char                    key[512]        = {0,};
        glusterd_brickinfo_t    *brickinfo      = NULL;
        int32_t                 i               = 1;
        char                    *volume_id_str  = NULL;
        char                    *src_brick      = NULL;
        char                    *dst_brick      = NULL;
        glusterd_voldict_ctx_t   ctx            = {0};

        GF_ASSERT (dict);
        GF_ASSERT (volinfo);

        snprintf (key, sizeof (key), "volume%d.name", count);
        ret = dict_set_str (dict, key, volinfo->volname);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.type", count);
        ret = dict_set_int32 (dict, key, volinfo->type);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.brick_count", count);
        ret = dict_set_int32 (dict, key, volinfo->brick_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.version", count);
        ret = dict_set_int32 (dict, key, volinfo->version);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.status", count);
        ret = dict_set_int32 (dict, key, volinfo->status);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.sub_count", count);
        ret = dict_set_int32 (dict, key, volinfo->sub_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.stripe_count", count);
        ret = dict_set_int32 (dict, key, volinfo->stripe_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.ckusm", count);
        ret = dict_set_int64 (dict, key, volinfo->cksum);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.transport_type", count);
        ret = dict_set_uint32 (dict, key, volinfo->transport_type);
        if (ret)
                goto out;

        volume_id_str = gf_strdup (uuid_utoa (volinfo->volume_id));
        if (!volume_id_str)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.volume_id", count);
        ret = dict_set_dynstr (dict, key, volume_id_str);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, 256, "volume%d."GLUSTERD_STORE_KEY_RB_STATUS, count);
        ret = dict_set_int32 (dict, key, volinfo->rb_status);
        if (ret)
                goto out;

        if (volinfo->rb_status > GF_RB_STATUS_NONE) {

                memset (key, 0, sizeof (key));
                snprintf (key, 256, "volume%d."GLUSTERD_STORE_KEY_RB_SRC_BRICK,
                          count);
                gf_asprintf (&src_brick, "%s:%s",
                             volinfo->src_brick->hostname,
                             volinfo->src_brick->path);
                ret = dict_set_dynstr (dict, key, src_brick);
                if (ret)
                        goto out;

                memset (key, 0, sizeof (key));
                snprintf (key, 256, "volume%d."GLUSTERD_STORE_KEY_RB_DST_BRICK,
                          count);
                gf_asprintf (&dst_brick, "%s:%s",
                             volinfo->dst_brick->hostname,
                             volinfo->dst_brick->path);
                ret = dict_set_dynstr (dict, key, dst_brick);
                if (ret)
                        goto out;
        }

        ctx.dict = dict;
        ctx.count = count;
        ctx.opt_count = 1;
        ctx.key_name = "key";
        ctx.val_name = "value";
        GF_ASSERT (volinfo->dict);

        dict_foreach (volinfo->dict, _add_volinfo_dict_to_prdict, &ctx);
        ctx.opt_count--;
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.opt-count", count);
        ret = dict_set_int32 (dict, key, ctx.opt_count);
        if (ret)
                goto out;

        ctx.dict = dict;
        ctx.count = count;
        ctx.opt_count = 1;
        ctx.key_name = "slave-num";
        ctx.val_name = "slave-val";
        GF_ASSERT (volinfo->gsync_slaves);

        dict_foreach (volinfo->gsync_slaves, _add_volinfo_dict_to_prdict, &ctx);
        ctx.opt_count--;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.gsync-count", count);
        ret = dict_set_int32 (dict, key, ctx.opt_count);
        if (ret)
                goto out;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d.brick%d.hostname",
                          count, i);
                ret = dict_set_str (dict, key, brickinfo->hostname);
                if (ret)
                        goto out;

                memset (key, 0, sizeof (key));
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

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.version", count);
        ret = dict_get_int32 (vols, key, &version);
        if (ret)
                goto out;

        if (version > volinfo->version) {
                //Mismatch detected
                ret = 0;
                gf_log ("", GF_LOG_ERROR, "Version of volume %s differ."
                        "local version = %d, remote version = %d",
                        volinfo->volname, volinfo->version, version);
                *status = GLUSTERD_VOL_COMP_UPDATE_REQ;
                goto out;
        } else if (version < volinfo->version) {
		*status = GLUSTERD_VOL_COMP_SCS;
		goto out;
	}

        //Now, versions are same, compare cksums.
        //
        memset (key, 0, sizeof (key));
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

static int32_t
import_prdict_volinfo_dict (dict_t *vols, dict_t  *dst_dict, char *key_prefix,
                            char *value_prefix, int opt_count, int count)
{
        char                    key[512] = {0,};
        int32_t                 ret = 0;
        int                     i = 1;
        char                    *opt_key = NULL;
        char                    *opt_val = NULL;
        char                    *dup_opt_val = NULL;
        char                    msg[2048] = {0};

        while (i <= opt_count) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d.%s%d",
                          count, key_prefix, i);
                ret = dict_get_str (vols, key, &opt_key);
                if (ret) {
                        snprintf (msg, sizeof (msg), "Volume dict key not "
                                  "specified");
                        goto out;
                }

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d.%s%d",
                          count, value_prefix, i);
                ret = dict_get_str (vols, key, &opt_val);
                if (ret) {
                        snprintf (msg, sizeof (msg), "Volume dict value not "
                                  "specified");
                        goto out;
                }
                dup_opt_val = gf_strdup (opt_val);
                if (!dup_opt_val) {
                        ret = -1;
                        goto out;
                }
                ret = dict_set_dynstr (dst_dict, opt_key, dup_opt_val);
                if (ret) {
                        snprintf (msg, sizeof (msg), "Volume set %s %s "
                                  "unsuccessful", opt_key, dup_opt_val);
                        goto out;
                }
                i++;
        }

out:
        if (msg[0])
                gf_log ("glusterd", GF_LOG_ERROR, "%s", msg);
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;

}

int32_t
glusterd_import_friend_volume_opts (dict_t *vols, int count,
                                    glusterd_volinfo_t *volinfo)
{
        char                    key[512] = {0,};
        int32_t                 ret = -1;
        int                     opt_count = 0;
        char                    msg[2048] = {0};

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.opt-count", count);
        ret = dict_get_int32 (vols, key, &opt_count);
        if (ret) {
                snprintf (msg, sizeof (msg), "Volume option count not "
                          "specified for %s", volinfo->volname);
                goto out;
        }

        ret = import_prdict_volinfo_dict (vols, volinfo->dict, "key",
                                          "value", opt_count, count);
        if (ret) {
                snprintf (msg, sizeof (msg), "Unable to import options dict "
                          "specified for %s", volinfo->volname);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.gsync-count", count);
        ret = dict_get_int32 (vols, key, &opt_count);
        if (ret) {
                snprintf (msg, sizeof (msg), "Gsync count not "
                          "specified for %s", volinfo->volname);
                goto out;
        }

        ret = import_prdict_volinfo_dict (vols, volinfo->gsync_slaves,
                                          "slave-num", "slave-val", opt_count,
                                          count);
        if (ret) {
                snprintf (msg, sizeof (msg), "Unable to import gsync sessions "
                          "specified for %s", volinfo->volname);
                goto out;
        }

out:
        if (msg[0])
                gf_log ("glusterd", GF_LOG_ERROR, "%s", msg);
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_import_new_brick (dict_t *vols, int32_t vol_count,
                           int32_t brick_count,
                           glusterd_brickinfo_t **brickinfo)
{
        char                    key[512] = {0,};
        int                     ret = -1;
        char                    *hostname = NULL;
        char                    *path = NULL;
        glusterd_brickinfo_t    *new_brickinfo = NULL;
        char                    msg[2048] = {0};

        GF_ASSERT (vols);
        GF_ASSERT (vol_count >= 0);
        GF_ASSERT (brickinfo);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.brick%d.hostname",
                  vol_count, brick_count);
        ret = dict_get_str (vols, key, &hostname);
        if (ret) {
                snprintf (msg, sizeof (msg), "%s missing in payload", key);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.brick%d.path",
                  vol_count, brick_count);
        ret = dict_get_str (vols, key, &path);
        if (ret) {
                snprintf (msg, sizeof (msg), "%s missing in payload", key);
                goto out;
        }

        ret = glusterd_brickinfo_new (&new_brickinfo);
        if (ret)
                goto out;

        strcpy (new_brickinfo->path, path);
        strcpy (new_brickinfo->hostname, hostname);
        //peerinfo might not be added yet
        (void) glusterd_resolve_brick (new_brickinfo);
        ret = 0;
        *brickinfo = new_brickinfo;
out:
        if (msg[0])
                gf_log ("glusterd", GF_LOG_ERROR, "%s", msg);
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_import_bricks (dict_t *vols, int32_t vol_count,
                        glusterd_volinfo_t *new_volinfo)
{
        int                     ret = -1;
        int                     brick_count = 1;
        glusterd_brickinfo_t     *new_brickinfo = NULL;

        GF_ASSERT (vols);
        GF_ASSERT (vol_count >= 0);
        GF_ASSERT (new_volinfo);
        while (brick_count <= new_volinfo->brick_count) {

                ret = glusterd_import_new_brick (vols, vol_count, brick_count,
                                                 &new_brickinfo);
                if (ret)
                        goto out;
                list_add_tail (&new_brickinfo->brick_list, &new_volinfo->bricks);
                brick_count++;
        }
        ret = 0;
out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_import_volinfo (dict_t *vols, int count,
                         glusterd_volinfo_t **volinfo)
{
        int                ret               = -1;
        char               key[256]          = {0};
        char               *volname          = NULL;
        glusterd_volinfo_t *new_volinfo      = NULL;
        char               *volume_id_str    = NULL;
        char               msg[2048]         = {0};
        char               *src_brick        = NULL;
        char               *dst_brick        = NULL;
        int                rb_status         = 0;

        GF_ASSERT (vols);
        GF_ASSERT (volinfo);

        snprintf (key, sizeof (key), "volume%d.name", count);
        ret = dict_get_str (vols, key, &volname);
        if (ret) {
                snprintf (msg, sizeof (msg), "%s missing in payload", key);
                goto out;
        }

        ret = glusterd_volinfo_new (&new_volinfo);
        if (ret)
                goto out;
        strncpy (new_volinfo->volname, volname, sizeof (new_volinfo->volname));


        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.type", count);
        ret = dict_get_int32 (vols, key, &new_volinfo->type);
        if (ret) {
                snprintf (msg, sizeof (msg), "%s missing in payload for %s",
                          key, volname);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.brick_count", count);
        ret = dict_get_int32 (vols, key, &new_volinfo->brick_count);
        if (ret) {
                snprintf (msg, sizeof (msg), "%s missing in payload for %s",
                          key, volname);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.version", count);
        ret = dict_get_int32 (vols, key, &new_volinfo->version);
        if (ret) {
                snprintf (msg, sizeof (msg), "%s missing in payload for %s",
                          key, volname);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.status", count);
        ret = dict_get_int32 (vols, key, (int32_t *)&new_volinfo->status);
        if (ret) {
                snprintf (msg, sizeof (msg), "%s missing in payload for %s",
                          key, volname);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.sub_count", count);
        ret = dict_get_int32 (vols, key, &new_volinfo->sub_count);
        if (ret) {
                snprintf (msg, sizeof (msg), "%s missing in payload for %s",
                          key, volname);
                goto out;
        }

        /* not having a 'stripe_count' key is not a error
           (as peer may be of old version) */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.stripe_count", count);
        ret = dict_get_int32 (vols, key, &new_volinfo->stripe_count);
        if (!ret) {
                if (new_volinfo->stripe_count)
                        new_volinfo->replica_count = (new_volinfo->sub_count /
                                                      new_volinfo->stripe_count);
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.ckusm", count);
        ret = dict_get_uint32 (vols, key, &new_volinfo->cksum);
        if (ret) {
                snprintf (msg, sizeof (msg), "%s missing in payload for %s",
                          key, volname);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.volume_id", count);
        ret = dict_get_str (vols, key, &volume_id_str);
        if (ret) {
                snprintf (msg, sizeof (msg), "%s missing in payload for %s",
                          key, volname);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.transport_type", count);
        ret = dict_get_uint32 (vols, key, &new_volinfo->transport_type);
        if (ret) {
                snprintf (msg, sizeof (msg), "%s missing in payload for %s",
                          key, volname);
                goto out;
        }

        uuid_parse (volume_id_str, new_volinfo->volume_id);

        memset (key, 0, sizeof (key));
        snprintf (key, 256, "volume%d."GLUSTERD_STORE_KEY_RB_STATUS, count);
        ret = dict_get_int32 (vols, key, &rb_status);
        if (ret)
                goto out;
        new_volinfo->rb_status = rb_status;

        if (new_volinfo->rb_status > GF_RB_STATUS_NONE) {

                memset (key, 0, sizeof (key));
                snprintf (key, 256, "volume%d."GLUSTERD_STORE_KEY_RB_SRC_BRICK,
                          count);
                ret = dict_get_str (vols, key, &src_brick);
                if (ret)
                        goto out;

                ret = glusterd_brickinfo_from_brick (src_brick,
                                                     &new_volinfo->src_brick);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Unable to create"
                                " src brickinfo");
                        goto out;
                }

                memset (key, 0, sizeof (key));
                snprintf (key, 256, "volume%d."GLUSTERD_STORE_KEY_RB_DST_BRICK,
                          count);
                ret = dict_get_str (vols, key, &dst_brick);
                if (ret)
                        goto out;

                ret = glusterd_brickinfo_from_brick (dst_brick,
                                                     &new_volinfo->dst_brick);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Unable to create"
                                " dst brickinfo");
                        goto out;
                }
        }


        ret = glusterd_import_friend_volume_opts (vols, count, new_volinfo);
        if (ret)
                goto out;
        ret = glusterd_import_bricks (vols, count, new_volinfo);
        if (ret)
                goto out;
        *volinfo = new_volinfo;
out:
        if (msg[0])
                gf_log ("glusterd", GF_LOG_ERROR, "%s", msg);
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_volume_disconnect_all_bricks (glusterd_volinfo_t *volinfo)
{
        int                  ret = 0;
        glusterd_brickinfo_t *brickinfo = NULL;
        GF_ASSERT (volinfo);

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (glusterd_is_brick_started (brickinfo)) {
                        ret = glusterd_brick_disconnect (brickinfo);
                        if (ret) {
                                gf_log ("glusterd", GF_LOG_ERROR, "Failed to "
                                        "disconnect %s:%s", brickinfo->hostname,
                                        brickinfo->path);
                                break;
                        }
                }
        }

        return ret;
}

int32_t
glusterd_volinfo_copy_brick_portinfo (glusterd_volinfo_t *new_volinfo,
                                      glusterd_volinfo_t *old_volinfo)
{
        glusterd_brickinfo_t *new_brickinfo = NULL;
        glusterd_brickinfo_t *old_brickinfo = NULL;

        int             ret = 0;
        GF_ASSERT (new_volinfo);
        GF_ASSERT (old_volinfo);
        if (_gf_false == glusterd_is_volume_started (new_volinfo))
                goto out;
        list_for_each_entry (new_brickinfo, &new_volinfo->bricks, brick_list) {
                ret = glusterd_volume_brickinfo_get (new_brickinfo->uuid,
                                                     new_brickinfo->hostname,
                                                     new_brickinfo->path,
                                                     old_volinfo, &old_brickinfo);
                if ((0 == ret) && glusterd_is_brick_started (old_brickinfo)) {
                        new_brickinfo->port = old_brickinfo->port;
                }
        }
out:
        ret = 0;
        return ret;
}

int32_t
glusterd_volinfo_stop_stale_bricks (glusterd_volinfo_t *new_volinfo,
                                    glusterd_volinfo_t *old_volinfo)
{
        glusterd_brickinfo_t *new_brickinfo = NULL;
        glusterd_brickinfo_t *old_brickinfo = NULL;

        int             ret = 0;
        GF_ASSERT (new_volinfo);
        GF_ASSERT (old_volinfo);
        if (_gf_false == glusterd_is_volume_started (old_volinfo))
                goto out;
        list_for_each_entry (old_brickinfo, &old_volinfo->bricks, brick_list) {
                ret = glusterd_volume_brickinfo_get (old_brickinfo->uuid,
                                                     old_brickinfo->hostname,
                                                     old_brickinfo->path,
                                                     new_volinfo, &new_brickinfo);
                if (ret) {
                        ret = glusterd_brick_stop (old_volinfo, old_brickinfo);
                        if (ret)
                                gf_log ("glusterd", GF_LOG_ERROR, "Failed to "
                                        "stop brick %s:%s", old_brickinfo->hostname,
                                        old_brickinfo->path);
                }
        }
        ret = 0;
out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_delete_stale_volume (glusterd_volinfo_t *stale_volinfo,
                              glusterd_volinfo_t *valid_volinfo)
{
        GF_ASSERT (stale_volinfo);
        GF_ASSERT (valid_volinfo);

        /* If stale volume is in started state, copy the port numbers of the
         * local bricks if they exist in the valid volume information.
         * stop stale bricks. Stale volume information is going to be deleted.
         * Which deletes the valid brick information inside stale volinfo.
         * We dont want brick_rpc_notify to access already deleted brickinfo.
         * Disconnect valid bricks.
         */
        if (glusterd_is_volume_started (stale_volinfo)) {
                if (glusterd_is_volume_started (valid_volinfo)) {
                        (void) glusterd_volinfo_stop_stale_bricks (valid_volinfo,
                                                                   stale_volinfo);
                        //Only valid bricks will be running now.
                        (void) glusterd_volinfo_copy_brick_portinfo (valid_volinfo,
                                                                     stale_volinfo);
                        (void) glusterd_volume_disconnect_all_bricks (stale_volinfo);
                } else {
                        (void) glusterd_stop_bricks (stale_volinfo);
                }
        }
        /* Delete all the bricks and stores and vol files. They will be created
         * again by the valid_volinfo. Volume store delete should not be
         * performed because some of the bricks could still be running,
         * keeping pid files under run directory
         */
        (void) glusterd_delete_all_bricks (stale_volinfo);
        if (stale_volinfo->shandle) {
                unlink (stale_volinfo->shandle->path);
                (void) glusterd_store_handle_destroy (stale_volinfo->shandle);
                stale_volinfo->shandle = NULL;
        }
        (void) glusterd_volinfo_delete (stale_volinfo);
        return 0;
}

int32_t
glusterd_import_friend_volume (dict_t *vols, size_t count)
{

        int32_t                 ret = -1;
        glusterd_conf_t         *priv = NULL;
        xlator_t                *this = NULL;
        glusterd_volinfo_t      *old_volinfo = NULL;
        glusterd_volinfo_t      *new_volinfo = NULL;

        GF_ASSERT (vols);

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        ret = glusterd_import_volinfo (vols, count, &new_volinfo);
        if (ret)
                goto out;

        ret = glusterd_volinfo_find (new_volinfo->volname, &old_volinfo);
        if (0 == ret) {
                (void) glusterd_delete_stale_volume (old_volinfo, new_volinfo);
        }

        if (glusterd_is_volume_started (new_volinfo)) {
                (void) glusterd_start_bricks (new_volinfo);
        }

        ret = glusterd_store_volinfo (new_volinfo, GLUSTERD_VOLINFO_VER_AC_NONE);
        ret = glusterd_create_volfiles_and_notify_services (new_volinfo);
        if (ret)
                goto out;

        list_add_tail (&new_volinfo->vol_list, &priv->volumes);
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
        gf_boolean_t            update = _gf_false;
        gf_boolean_t            stale_nfs = _gf_false;
        gf_boolean_t            stale_shd = _gf_false;

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
                if (GLUSTERD_VOL_COMP_UPDATE_REQ == *status)
                        update = _gf_true;

                i++;
        }

        if (update) {
                if (glusterd_is_nodesvc_running ("nfs"))
                        stale_nfs = _gf_true;
                if (glusterd_is_nodesvc_running ("glustershd"))
                        stale_shd = _gf_true;
                ret = glusterd_import_friend_volumes (vols);
                if (ret)
                        goto out;
                if (_gf_false == glusterd_are_all_volumes_stopped ()) {
                        ret = glusterd_nodesvcs_handle_graph_change (NULL);
                } else {
                        if (stale_nfs)
                                glusterd_nfs_server_stop ();
                        if (stale_shd)
                                glusterd_shd_stop ();
                }
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with ret: %d, status: %d",
                ret, *status);

        return ret;
}

gf_boolean_t
glusterd_is_service_running (char *pidfile)
{
        FILE            *file = NULL;
        gf_boolean_t    running = _gf_false;
        gf_boolean_t    locked = _gf_false;
        int             ret = 0;
        int             fno = 0;

        file = fopen (pidfile, "r+");
        if (!file)
                goto out;

        fno = fileno (file);
        ret = lockf (fno, F_TLOCK, 0);
        if (!ret) {
                locked = _gf_true;
                goto out;
        }

        running = _gf_true;
out:
        if (locked) {
                GF_ASSERT (file);
                if (lockf (fno, F_ULOCK, 0) < 0)
                        gf_log ("", GF_LOG_WARNING, "Cannot unlock pidfile: %s"
                                " reason: %s", pidfile, strerror(errno));
        }
        if (file)
                fclose (file);
        return running;
}

void
glusterd_get_nodesvc_dir (char *server, char *workdir,
                                char *path, size_t len)
{
        GF_ASSERT (len == PATH_MAX);
        snprintf (path, len, "%s/%s", workdir, server);
}

void
glusterd_get_nodesvc_rundir (char *server, char *workdir,
                                   char *path, size_t len)
{
        char    dir[PATH_MAX] = {0};
        GF_ASSERT (len == PATH_MAX);

        glusterd_get_nodesvc_dir (server, workdir, dir, sizeof (dir));
        snprintf (path, len, "%s/run", dir);
}

void
glusterd_get_nodesvc_pidfile (char *server, char *workdir,
                                    char *path, size_t len)
{
        char    dir[PATH_MAX] = {0};
        GF_ASSERT (len == PATH_MAX);

        glusterd_get_nodesvc_rundir (server, workdir, dir, sizeof (dir));
        snprintf (path, len, "%s/%s.pid", dir, server);
}

void
glusterd_get_nodesvc_volfile (char *server, char *workdir,
                                    char *volfile, size_t len)
{
        char  dir[PATH_MAX] = {0,};
        GF_ASSERT (len == PATH_MAX);

        glusterd_get_nodesvc_dir (server, workdir, dir, sizeof (dir));
        snprintf (volfile, len, "%s/%s-server.vol", dir, server);
}

int32_t
glusterd_nodesvc_start (char *server, gf_boolean_t pmap_signin)
{
        int32_t                 ret = -1;
        xlator_t                *this = NULL;
        glusterd_conf_t         *priv = NULL;
        char                    pidfile[PATH_MAX] = {0,};
        char                    logfile[PATH_MAX] = {0,};
        char                    volfile[PATH_MAX] = {0,};
        char                    rundir[PATH_MAX] = {0,};
        char                    volfileid[256]   = {0};

        this = THIS;
        GF_ASSERT(this);

        priv = this->private;

        glusterd_get_nodesvc_rundir (server, priv->workdir,
                                           rundir, sizeof (rundir));
        ret = mkdir (rundir, 0777);

        if ((ret == -1) && (EEXIST != errno)) {
                gf_log ("", GF_LOG_ERROR, "Unable to create rundir %s",
                        rundir);
                goto out;
        }

        glusterd_get_nodesvc_pidfile (server, priv->workdir,
                                            pidfile, sizeof (pidfile));
        glusterd_get_nodesvc_volfile (server, priv->workdir,
                                            volfile, sizeof (volfile));
        ret = access (volfile, F_OK);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "%s Volfile %s is not present",
                        server, volfile);
                goto out;
        }

        snprintf (logfile, PATH_MAX, "%s/%s.log", DEFAULT_LOG_FILE_DIRECTORY,
                  server);
        snprintf (volfileid, sizeof (volfileid), "gluster/%s", server);

        if (pmap_signin)
                ret = runcmd (GFS_PREFIX"/sbin/glusterfs", "-s", "localhost",
                              "--volfile-id", volfileid,
                              "-p", pidfile, "-l", logfile, NULL);
        else
                ret = runcmd (GFS_PREFIX"/sbin/glusterfs", "-f", volfile,
                              "-p", pidfile, "-l", logfile, NULL);

out:
        return ret;
}

int
glusterd_nfs_server_start ()
{
        return glusterd_nodesvc_start ("nfs", _gf_false);
}

int
glusterd_shd_start ()
{
        return glusterd_nodesvc_start ("glustershd", _gf_true);
}

gf_boolean_t
glusterd_is_nodesvc_running (char *server)
{
        char                    pidfile[PATH_MAX] = {0,};
        glusterd_conf_t         *priv = THIS->private;

        glusterd_get_nodesvc_pidfile (server, priv->workdir,
                                            pidfile, sizeof (pidfile));
        return glusterd_is_service_running (pidfile);
}

int32_t
glusterd_nodesvc_stop (char *server, int sig)
{
        char                    pidfile[PATH_MAX] = {0,};
        glusterd_conf_t         *priv = THIS->private;
        int                     ret = 0;

        if (!glusterd_is_nodesvc_running (server))
                goto out;
        glusterd_get_nodesvc_pidfile (server, priv->workdir,
                                            pidfile, sizeof (pidfile));
        ret = glusterd_service_stop (server, pidfile, sig, _gf_true);
out:
        return ret;
}

void
glusterd_nfs_pmap_deregister ()
{
        if (pmap_unset (MOUNT_PROGRAM, MOUNTV3_VERSION))
                gf_log ("", GF_LOG_INFO, "De-registered MOUNTV3 successfully");
        else
                gf_log ("", GF_LOG_ERROR, "De-register MOUNTV3 is unsuccessful");

        if (pmap_unset (MOUNT_PROGRAM, MOUNTV1_VERSION))
                gf_log ("", GF_LOG_INFO, "De-registered MOUNTV1 successfully");
        else
                gf_log ("", GF_LOG_ERROR, "De-register MOUNTV1 is unsuccessful");

        if (pmap_unset (NFS_PROGRAM, NFSV3_VERSION))
                gf_log ("", GF_LOG_INFO, "De-registered NFSV3 successfully");
        else
                gf_log ("", GF_LOG_ERROR, "De-register NFSV3 is unsuccessful");

}

int
glusterd_nfs_server_stop ()
{
        int                     ret = 0;
        gf_boolean_t            deregister = _gf_false;

        if (glusterd_is_nodesvc_running ("nfs"))
                deregister = _gf_true;
        ret = glusterd_nodesvc_stop ("nfs", SIGKILL);
        if (ret)
                goto out;
        if (deregister)
                glusterd_nfs_pmap_deregister ();
out:
        return ret;
}

int
glusterd_shd_stop ()
{
        return glusterd_nodesvc_stop ("glustershd", SIGTERM);
}

int
glusterd_remote_hostname_get (rpcsvc_request_t *req, char *remote_host, int len)
{
        GF_ASSERT (req);
        GF_ASSERT (remote_host);
        GF_ASSERT (req->trans);

        char *name = NULL;
        char *hostname = NULL;
        char *tmp_host = NULL;
        int  ret = 0;

        name = req->trans->peerinfo.identifier;
        tmp_host = gf_strdup (name);
        if (tmp_host)
                get_host_name (tmp_host, &hostname);

        GF_ASSERT (hostname);
        if (!hostname) {
                memset (remote_host, 0, len);
                ret = -1;
                goto out;
        }

        strncpy (remote_host, hostname, strlen (hostname));


out:
        if (tmp_host)
                GF_FREE (tmp_host);
        return ret;
}

int
glusterd_check_generate_start_service (int (*create_volfile) (),
                                       int (*stop) (), int (*start) ())
{
        int ret = -1;

        ret = create_volfile ();
        if (ret)
                goto out;

        ret = stop ();
        if (ret)
                goto out;

        ret = start ();
out:
        return ret;
}

int
glusterd_reconfigure_nodesvc (int (*create_volfile) ())
{
        int ret = -1;

        ret = create_volfile ();
        if (ret)
                goto out;

        ret = glusterd_fetchspec_notify (THIS);
out:
        return ret;
}

int
glusterd_reconfigure_shd ()
{
        int (*create_volfile) () = glusterd_create_shd_volfile;
        return glusterd_reconfigure_nodesvc (create_volfile);
}

int
glusterd_check_generate_start_nfs ()
{
        int ret = 0;

        ret = glusterd_check_generate_start_service (glusterd_create_nfs_volfile,
                                                     glusterd_nfs_server_stop,
                                                     glusterd_nfs_server_start);
        return ret;
}

int
glusterd_check_generate_start_shd ()
{
        int ret = 0;

        ret = glusterd_check_generate_start_service (glusterd_create_shd_volfile,
                                                     glusterd_shd_stop,
                                                     glusterd_shd_start);
        if (ret == -EINVAL)
                ret = 0;
        return ret;
}

int
glusterd_nodesvcs_batch_op (glusterd_volinfo_t *volinfo,
                             int (*nfs_op) (), int (*shd_op) ())
{
        int     ret = 0;

        ret = nfs_op ();
        if (ret)
                goto out;

        if (volinfo && !glusterd_is_volume_replicate (volinfo))
                goto out;

        ret = shd_op ();
        if (ret)
                goto out;
out:
        return ret;
}

int
glusterd_nodesvcs_start (glusterd_volinfo_t *volinfo)
{
        return glusterd_nodesvcs_batch_op (volinfo,
                                            glusterd_nfs_server_start,
                                            glusterd_shd_start);
}

int
glusterd_nodesvcs_stop (glusterd_volinfo_t *volinfo)
{
        return glusterd_nodesvcs_batch_op (volinfo,
                                            glusterd_nfs_server_stop,
                                            glusterd_shd_stop);
}

int
glusterd_nodesvcs_handle_graph_change (glusterd_volinfo_t *volinfo)
{
        return glusterd_nodesvcs_batch_op (volinfo,
                                      glusterd_check_generate_start_nfs,
                                      glusterd_check_generate_start_shd);
}

int
glusterd_nodesvcs_handle_reconfigure (glusterd_volinfo_t *volinfo)
{
        return glusterd_nodesvcs_batch_op (volinfo,
                                            glusterd_check_generate_start_nfs,
                                            glusterd_reconfigure_shd);
}

int
glusterd_volume_count_get (void)
{
        glusterd_volinfo_t      *tmp_volinfo = NULL;
        int32_t                 ret = 0;
        xlator_t                *this = NULL;
        glusterd_conf_t         *priv = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;

        list_for_each_entry (tmp_volinfo, &priv->volumes, vol_list) {
                ret++;
        }


        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;

}

int
glusterd_brickinfo_get (uuid_t uuid, char *hostname, char *path,
                        glusterd_brickinfo_t **brickinfo)
{
        glusterd_volinfo_t              *volinfo     = NULL;
        glusterd_conf_t                 *priv = NULL;
        xlator_t                        *this = NULL;
        int                             ret = -1;

        GF_ASSERT (path);

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;

        list_for_each_entry (volinfo, &priv->volumes, vol_list) {

                ret = glusterd_volume_brickinfo_get (uuid, hostname, path,
                                                     volinfo,
                                                     brickinfo);
                if (!ret)
                        goto out;
        }
out:
        return ret;
}

int
glusterd_brick_start (glusterd_volinfo_t *volinfo,
                      glusterd_brickinfo_t *brickinfo)
{
        int                                     ret   = -1;
        xlator_t                                *this = NULL;
        glusterd_conf_t                         *conf = NULL;

        if ((!brickinfo) || (!volinfo))
                goto out;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        if (uuid_is_null (brickinfo->uuid)) {
                ret = glusterd_resolve_brick (brickinfo);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "cannot resolve brick: %s:%s",
                                brickinfo->hostname, brickinfo->path);
                        goto out;
                }
        }

        if (uuid_compare (brickinfo->uuid, conf->uuid)) {
                ret = 0;
                goto out;
        }
        ret = glusterd_volume_start_glusterfs (volinfo, brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to start "
                        "glusterfs, ret: %d", ret);
                goto out;
        }

out:
        gf_log ("", GF_LOG_DEBUG, "returning %d ", ret);
        return ret;
}

int
glusterd_restart_bricks (glusterd_conf_t *conf)
{
        glusterd_volinfo_t       *volinfo = NULL;
        glusterd_brickinfo_t     *brickinfo = NULL;
        int                      ret = 0;
        gf_boolean_t             start_nodesvcs = _gf_false;

        GF_ASSERT (conf);

        list_for_each_entry (volinfo, &conf->volumes, vol_list) {
                //If volume status is not started, do not proceed
                if (volinfo->status == GLUSTERD_STATUS_STARTED) {
                        list_for_each_entry (brickinfo, &volinfo->bricks,
                                             brick_list) {
                                glusterd_brick_start (volinfo, brickinfo);
                        }
                        start_nodesvcs = _gf_true;
                }
        }
        if (start_nodesvcs)
                glusterd_nodesvcs_handle_graph_change (NULL);
        return ret;
}

void
_local_gsyncd_start (dict_t *this, char *key, data_t *value, void *data)
{
        char                        *slave = NULL;
        int                          uuid_len = 0;
        char                         uuid_str[64] = {0};
        glusterd_volinfo_t           *volinfo = NULL;

        volinfo = data;
        GF_ASSERT (volinfo);
        slave = strchr(value->data, ':');
        if (slave)
                slave ++;
        else
                return;
        uuid_len = (slave - value->data - 1);


        strncpy (uuid_str, (char*)value->data, uuid_len);
        glusterd_start_gsync (volinfo, slave, uuid_str, NULL);
}

int
glusterd_volume_restart_gsyncds (glusterd_volinfo_t *volinfo)
{
        GF_ASSERT (volinfo);

        dict_foreach (volinfo->gsync_slaves, _local_gsyncd_start, volinfo);
        return 0;
}

int
glusterd_restart_gsyncds (glusterd_conf_t *conf)
{
        glusterd_volinfo_t       *volinfo = NULL;
        int                      ret = 0;

        list_for_each_entry (volinfo, &conf->volumes, vol_list) {
                glusterd_volume_restart_gsyncds (volinfo);
        }
        return ret;
}

int
glusterd_get_brickinfo (xlator_t *this, const char *brickname, int port,
                        gf_boolean_t localhost, glusterd_brickinfo_t **brickinfo)
{
        glusterd_conf_t         *priv = NULL;
        glusterd_volinfo_t      *volinfo = NULL;
        glusterd_brickinfo_t    *tmpbrkinfo = NULL;
        int                     ret = -1;

        GF_ASSERT (brickname);
        GF_ASSERT (this);

        priv = this->private;
        list_for_each_entry (volinfo, &priv->volumes, vol_list) {
                list_for_each_entry (tmpbrkinfo, &volinfo->bricks,
                                     brick_list) {
                        if (localhost && glusterd_is_local_addr (tmpbrkinfo->hostname))
                                continue;
                        if (!strcmp(tmpbrkinfo->path, brickname) &&
                            (tmpbrkinfo->port == port)) {
                                *brickinfo = tmpbrkinfo;
                                return 0;
                        }
                }
        }
        return ret;
}

void
glusterd_set_brick_status (glusterd_brickinfo_t  *brickinfo,
                            gf_brick_status_t status)
{
        GF_ASSERT (brickinfo);
        brickinfo->status = status;
        if (GF_BRICK_STARTED == status) {
                gf_log ("glusterd", GF_LOG_DEBUG, "Setting brick %s:%s status "
                        "to started", brickinfo->hostname, brickinfo->path);
        } else {
                gf_log ("glusterd", GF_LOG_DEBUG, "Setting brick %s:%s status "
                        "to stopped", brickinfo->hostname, brickinfo->path);
        }
}

gf_boolean_t
glusterd_is_brick_started (glusterd_brickinfo_t  *brickinfo)
{
        GF_ASSERT (brickinfo);
        return (brickinfo->status == GF_BRICK_STARTED);
}

int
glusterd_friend_brick_belongs (glusterd_volinfo_t *volinfo,
                               glusterd_brickinfo_t *brickinfo, void* uuid)
{
        int             ret = -1;

        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);
        GF_ASSERT (uuid);

        if (uuid_is_null (brickinfo->uuid)) {
                ret = glusterd_resolve_brick (brickinfo);
                if (ret) {
                        GF_ASSERT (0);
                        goto out;
                }
        }
        if (!uuid_compare (brickinfo->uuid, *((uuid_t *)uuid)))
                return 0;
out:
        return -1;
}

int32_t
glusterd_add_brick_to_dict (glusterd_volinfo_t *volinfo,
                            glusterd_brickinfo_t *brickinfo,
                            dict_t  *dict, int32_t count)
{

        int             ret = -1;
        char            key[8192] = {0,};
        char            base_key[8192] = {0};
        char            pidfile[PATH_MAX] = {0};
        char            path[PATH_MAX] = {0};
        FILE            *file = NULL;
        int32_t         pid = -1;
        xlator_t        *this = NULL;
        glusterd_conf_t *priv = NULL;

        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);
        GF_ASSERT (dict);

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;

        snprintf (base_key, sizeof (base_key), "brick%d", count);
        snprintf (key, sizeof (key), "%s.hostname", base_key);
        ret = dict_set_str (dict, key, brickinfo->hostname);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.path", base_key);
        ret = dict_set_str (dict, key, brickinfo->path);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.port", base_key);
        ret = dict_set_int32 (dict, key, brickinfo->port);
        if (ret)
                goto out;


        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.status", base_key);
        ret = dict_set_int32 (dict, key, brickinfo->signed_in);
        if (ret)
                goto out;

        if (!brickinfo->signed_in)
                goto out;


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

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.pid", base_key);
        ret = dict_set_int32 (dict, key, pid);
        if (ret)
                goto out;

out:
        if (ret)
                gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int
glusterd_all_volume_cond_check (glusterd_condition_func func, int status,
                                void *ctx)
{
        glusterd_conf_t         *priv = NULL;
        glusterd_volinfo_t      *volinfo = NULL;
        glusterd_brickinfo_t    *brickinfo = NULL;
        int                     ret = -1;
        xlator_t                *this = NULL;

        this = THIS;
        priv = this->private;

        list_for_each_entry (volinfo, &priv->volumes, vol_list) {
                list_for_each_entry (brickinfo, &volinfo->bricks,
                                     brick_list) {
                        ret = func (volinfo, brickinfo, ctx);
                        if (ret != status) {
                                ret = -1;
                                goto out;
                        }
                }
        }
        ret = 0;
out:
        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int
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

                        gf_log ("glusterd", GF_LOG_DEBUG,
                                 "Friend found... state: %s",
                        glusterd_friend_sm_state_name_get (entry->state.state));
                        *peerinfo = entry;
                        return 0;
                }
        }

        gf_log ("glusterd", GF_LOG_DEBUG, "Friend with uuid: %s, not found",
                uuid_utoa (uuid));
        return ret;
}


int
glusterd_friend_find_by_hostname (const char *hoststr,
                                  glusterd_peerinfo_t  **peerinfo)
{
        int                     ret = -1;
        glusterd_conf_t         *priv = NULL;
        glusterd_peerinfo_t     *entry = NULL;
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
                if (!strncasecmp (entry->hostname, hoststr,
                                  1024)) {

                        gf_log ("glusterd", GF_LOG_DEBUG,
                                 "Friend %s found.. state: %d", hoststr,
                                  entry->state.state);
                        *peerinfo = entry;
                        return 0;
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
                        if (!strncasecmp (entry->hostname, host,
                            1024) || !strncasecmp (entry->hostname,hname,
                            1024)) {
                                gf_log ("glusterd", GF_LOG_DEBUG,
                                        "Friend %s found.. state: %d",
                                        hoststr, entry->state.state);
                                *peerinfo = entry;
                                freeaddrinfo (addr);
                                return 0;
                        }
                }
        }

out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Unable to find friend: %s", hoststr);
        if (addr)
                freeaddrinfo (addr);
        return -1;
}

int
glusterd_hostname_to_uuid (char *hostname, uuid_t uuid)
{
        GF_ASSERT (hostname);
        GF_ASSERT (uuid);

        glusterd_peerinfo_t     *peerinfo = NULL;
        glusterd_conf_t         *priv = NULL;
        int                     ret = -1;
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        ret = glusterd_friend_find_by_hostname (hostname, &peerinfo);
        if (ret) {
                ret = glusterd_is_local_addr (hostname);
                if (ret)
                        goto out;
                else
                        uuid_copy (uuid, priv->uuid);
        } else {
                uuid_copy (uuid, peerinfo->uuid);
        }

out:
        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int
glusterd_brick_stop (glusterd_volinfo_t *volinfo,
                     glusterd_brickinfo_t *brickinfo)
{
        int                                     ret   = -1;
        xlator_t                                *this = NULL;
        glusterd_conf_t                         *conf = NULL;

        if ((!brickinfo) || (!volinfo))
                goto out;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        if (uuid_is_null (brickinfo->uuid)) {
                ret = glusterd_resolve_brick (brickinfo);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "cannot resolve brick: %s:%s",
                                brickinfo->hostname, brickinfo->path);
                        goto out;
                }
        }

        if (uuid_compare (brickinfo->uuid, conf->uuid)) {
                ret = 0;
                goto out;
        }

        gf_log ("", GF_LOG_INFO, "About to stop glusterfs"
                " for brick %s:%s", brickinfo->hostname,
                brickinfo->path);
        ret = glusterd_volume_stop_glusterfs (volinfo, brickinfo);
        if (ret) {
                gf_log ("", GF_LOG_CRITICAL, "Unable to remove"
                        " brick: %s:%s", brickinfo->hostname,
                        brickinfo->path);
                goto out;
        }

out:
        gf_log ("", GF_LOG_DEBUG, "returning %d ", ret);
        return ret;
}

int
glusterd_is_defrag_on (glusterd_volinfo_t *volinfo)
{
        return (volinfo->defrag != NULL);
}

int
glusterd_is_replace_running (glusterd_volinfo_t *volinfo, glusterd_brickinfo_t *brickinfo)
{
        int ret = 0;
        char *src_hostname = NULL;
        char *brick_hostname = NULL;

        if (volinfo->src_brick) {
                src_hostname = gf_strdup (volinfo->src_brick->hostname);
                if (!src_hostname) {
                        ret = -1;
                        goto out;
                }
        } else {
                gf_log ("glusterd", GF_LOG_DEBUG,
                        "replace brick is not running");
                goto out;
        }

        brick_hostname = gf_strdup (brickinfo->hostname);
        if (!brick_hostname) {
                ret = -1;
                goto out;
        }
        if (!glusterd_is_local_addr (src_hostname) && !glusterd_is_local_addr (brick_hostname)) {
                if (glusterd_is_rb_started (volinfo) || glusterd_is_rb_paused (volinfo))
                        ret = -1;
        }

out:
        if (src_hostname)
                GF_FREE (src_hostname);
        if (brick_hostname)
                GF_FREE (brick_hostname);
        return ret;
}

int
glusterd_new_brick_validate (char *brick, glusterd_brickinfo_t *brickinfo,
                             char *op_errstr, size_t len)
{
        glusterd_brickinfo_t    *newbrickinfo = NULL;
        glusterd_brickinfo_t    *tmpbrkinfo = NULL;
        int                     ret = -1;
        gf_boolean_t            is_allocated = _gf_false;
        glusterd_peerinfo_t     *peerinfo = NULL;
        glusterd_conf_t         *priv = NULL;
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);


        GF_ASSERT (brick);
        GF_ASSERT (op_errstr);

        if (!brickinfo) {
                ret = glusterd_brickinfo_from_brick (brick, &newbrickinfo);
                if (ret)
                        goto out;
                is_allocated = _gf_true;
        } else {
                newbrickinfo = brickinfo;
        }

        ret = glusterd_resolve_brick (newbrickinfo);
        if (ret) {
                snprintf (op_errstr, len, "Host %s not a friend",
                          newbrickinfo->hostname);
                gf_log ("glusterd", GF_LOG_ERROR, "%s", op_errstr);
                goto out;
        }

        if (!uuid_compare (priv->uuid, newbrickinfo->uuid))
                goto brick_validation;
        ret = glusterd_friend_find_by_uuid (newbrickinfo->uuid, &peerinfo);
        if (ret)
                goto out;
        if ((!peerinfo->connected) ||
            (peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED)) {
                snprintf(op_errstr, len, "Host %s not connected",
                         newbrickinfo->hostname);
                gf_log ("glusterd", GF_LOG_ERROR, "%s", op_errstr);
                ret = -1;
                goto out;
        }
brick_validation:
        ret = glusterd_brickinfo_get (newbrickinfo->uuid,
                                      newbrickinfo->hostname,
                                      newbrickinfo->path, &tmpbrkinfo);
        if (!ret) {
                snprintf(op_errstr, len, "Brick: %s already in use",
                         brick);
                gf_log ("", GF_LOG_ERROR, "%s", op_errstr);
                ret = -1;
                goto out;
        } else {
                ret = 0;
        }
out:
        if (is_allocated && newbrickinfo)
                glusterd_brickinfo_delete (newbrickinfo);
        gf_log ("", GF_LOG_DEBUG, "returning %d ", ret);
        return ret;
}

int
glusterd_is_rb_started(glusterd_volinfo_t *volinfo)
{
        gf_log ("", GF_LOG_DEBUG,
                "is_rb_started:status=%d", volinfo->rb_status);
        return (volinfo->rb_status == GF_RB_STATUS_STARTED);

}

int
glusterd_is_rb_paused ( glusterd_volinfo_t *volinfo)
{
        gf_log ("", GF_LOG_DEBUG,
                "is_rb_paused:status=%d", volinfo->rb_status);

        return (volinfo->rb_status == GF_RB_STATUS_PAUSED);
}

inline int
glusterd_set_rb_status (glusterd_volinfo_t *volinfo, gf_rb_status_t status)
{
        gf_log ("", GF_LOG_DEBUG,
                "setting status from %d to %d",
                volinfo->rb_status,
                status);

        volinfo->rb_status = status;
        return 0;
}

inline int
glusterd_rb_check_bricks (glusterd_volinfo_t *volinfo,
                          glusterd_brickinfo_t *src, glusterd_brickinfo_t *dst)
{
        if (!volinfo->src_brick || !volinfo->dst_brick)
                return -1;

        if (strcmp (volinfo->src_brick->hostname, src->hostname) ||
            strcmp (volinfo->src_brick->path, src->path)) {
                gf_log("", GF_LOG_ERROR, "Replace brick src bricks differ");
                return -1;
        }
        if (strcmp (volinfo->dst_brick->hostname, dst->hostname) ||
            strcmp (volinfo->dst_brick->path, dst->path)) {
                gf_log ("", GF_LOG_ERROR, "Replace brick dst bricks differ");
                return -1;
        }
        return 0;
}

int
glusterd_brick_create_path (char *host, char *path, uuid_t uuid, mode_t mode,
                            char **op_errstr)
{
        int     ret = -1;
        char    msg[2048] = {0};
        struct  stat st_buf = {0};
        uuid_t  gfid = {0,};
        uuid_t  old_uuid = {0,};
        char    old_uuid_buf[64] = {0,};

        ret = stat (path, &st_buf);
        if ((!ret) && (!S_ISDIR (st_buf.st_mode))) {
                snprintf (msg, sizeof (msg), "brick %s:%s, "
                          "path %s is not a directory", host, path, path);
                gf_log ("", GF_LOG_ERROR, "%s", msg);
                ret = -1;
                goto out;
        } else if (!ret) {
                goto check_xattr;
        } else {
                ret = mkdir (path, mode);
                if (ret) {
                        snprintf (msg, sizeof (msg), "brick: %s:%s, path "
                                  "creation failed, reason: %s",
                                  host, path, strerror(errno));
                        gf_log ("glusterd", GF_LOG_ERROR, "%s", msg);
                        goto out;
                } else {
                        goto check_xattr;
                }
        }

/* To check if filesystem is read-only
   and if it supports extended attributes */
check_xattr:
        ret = sys_lsetxattr (path, "trusted.glusterfs.test",
                             "working", 8, 0);
        if (ret) {
                snprintf (msg, sizeof (msg), "glusterfs is not"
                          " supported on brick: %s:%s.\nSetting"
                          " extended attributes failed, reason:"
                          " %s.", host, path, strerror(errno));
                gf_log ("glusterd", GF_LOG_ERROR, "%s", msg);
                goto out;
        } else {
                /* Remove xattr *cannot* fail after setting it succeeded */
                sys_lremovexattr (path, "trusted.glusterfs.test");
        }

        /* Now check if the export directory has some other 'gfid',
           other than that of root '/' */
        ret = sys_lgetxattr (path, "trusted.gfid", gfid, 16);
        if (ret == 16) {
                if (__is_root_gfid (gfid) != 0) {
                        gf_log (THIS->name, GF_LOG_WARNING,
                                "%s: gfid (%s) is not that of glusterfs '/' ",
                                path, uuid_utoa (gfid));
                        snprintf (msg, sizeof (msg),
                                  "'%s:%s' gfid (%s) is not that of "
                                  "glusterfs '/' ", host, path, uuid_utoa (gfid));
                        ret = -1;
                        goto out;
                }
        } else if (ret != -1) {
                /* Wrong 'gfid' is set, it should be error */
                ret = -1;
                snprintf (msg, sizeof (msg), "'%s:%s' has wrong entry"
                          "for 'gfid'.", host, path);
                goto out;
        } else if ((ret == -1) && (errno != ENODATA)) {
                /* Wrong 'gfid' is set, it should be error */
                snprintf (msg, sizeof (msg), "'%s:%s' has failed to fetch "
                          "'gfid' (%s)", host, path, strerror (errno));
                goto out;
        }

        ret = 0;
        if (!uuid)
                goto out;

        /* This 'key' is set when the volume is started for the first time */
        ret = sys_lgetxattr (path, "trusted.glusterfs.volume-id",
                             old_uuid, 16);
        if (ret == 16) {
                if (uuid_compare (old_uuid, uuid)) {
                        uuid_utoa_r (old_uuid, old_uuid_buf);
                        gf_log (THIS->name, GF_LOG_WARNING,
                                "%s: mismatching volume-id (%s) recieved. "
                                "already is a part of volume %s ",
                                path, uuid_utoa (uuid), old_uuid_buf);
                        snprintf (msg, sizeof (msg), "'%s:%s' has been part of "
                                  "a deleted volume with id %s. Please "
                                  "re-create the brick directory.",
                                  host, path, old_uuid_buf);
                        ret = -1;
                        goto out;
                }
        } else if (ret != -1) {
                /* Wrong 'volume-id' is set, it should be error */
                ret = -1;
                snprintf (msg, sizeof (msg), "'%s:%s' has wrong entry"
                          "for 'volume-id'.", host, path);
                goto out;
        } else if ((ret == -1) && (errno != ENODATA)) {
                /* Wrong 'volume-id' is set, it should be error */
                snprintf (msg, sizeof (msg), "'%s:%s' : failed to fetch "
                          "'volume-id' (%s)", host, path, strerror (errno));
                goto out;

        }
        /* if 'ret == -1' then 'volume-id' not set, seems to be a fresh
           directory */

        ret = 0;
out:
        if (msg[0] != '\0')
                *op_errstr = gf_strdup (msg);

        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int
glusterd_sm_tr_log_transition_add_to_dict (dict_t *dict,
                                           glusterd_sm_tr_log_t *log, int i,
                                           int count)
{
        int     ret = -1;
        char    key[512] = {0};
	char    timestr[256] = {0,};
        char    *str = NULL;
	struct tm   tm = {0};

        GF_ASSERT (dict);
        GF_ASSERT (log);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "log%d-old-state", count);
        str = log->state_name_get (log->transitions[i].old_state);
        ret = dict_set_str (dict, key, str);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "log%d-event", count);
        str = log->event_name_get (log->transitions[i].event);
        ret = dict_set_str (dict, key, str);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "log%d-new-state", count);
        str = log->state_name_get (log->transitions[i].new_state);
        ret = dict_set_str (dict, key, str);
        if (ret)
                goto out;


        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "log%d-time", count);
	localtime_r ((const time_t*)&log->transitions[i].time, &tm);
        memset (timestr, 0, sizeof (timestr));
	strftime (timestr, 256, "%Y-%m-%d %H:%M:%S", &tm);
        str = gf_strdup (timestr);
        ret = dict_set_dynstr (dict, key, str);
        if (ret)
                goto out;

out:
        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int
glusterd_sm_tr_log_add_to_dict (dict_t *dict,
                                glusterd_sm_tr_log_t *circular_log)
{
        int     ret = -1;
        int     i = 0;
        int     start = 0;
        int     end     = 0;
        int     index = 0;
        char    key[256] = {0};
        glusterd_sm_tr_log_t *log = NULL;
        int     count = 0;

        GF_ASSERT (dict);
        GF_ASSERT (circular_log);

        log = circular_log;
        if (!log->count)
                return 0;

        if (log->count == log->size)
                start = log->current + 1;

        end = start + log->count;
        for (i = start; i < end; i++, count++) {
                index = i % log->count;
                ret = glusterd_sm_tr_log_transition_add_to_dict (dict, log, index,
                                                                 count);
                if (ret)
                        goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "count");
        ret = dict_set_int32 (dict, key, log->count);

out:
        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int
glusterd_sm_tr_log_init (glusterd_sm_tr_log_t *log,
                         char * (*state_name_get) (int),
                         char * (*event_name_get) (int),
                         size_t  size)
{
        glusterd_sm_transition_t *transitions = NULL;
        int                      ret = -1;

        GF_ASSERT (size > 0);
        GF_ASSERT (log && state_name_get && event_name_get);

        if (!log || !state_name_get || !event_name_get || (size <= 0))
                goto out;

        transitions = GF_CALLOC (size, sizeof (*transitions),
                                 gf_gld_mt_sm_tr_log_t);
        if (!transitions)
                goto out;

        log->transitions = transitions;
        log->size        = size;
        log->state_name_get = state_name_get;
        log->event_name_get = event_name_get;
        ret = 0;

out:
        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

void
glusterd_sm_tr_log_delete (glusterd_sm_tr_log_t *log)
{
        if (!log)
                return;
        if (log->transitions)
                GF_FREE (log->transitions);
        return;
}

int
glusterd_sm_tr_log_transition_add (glusterd_sm_tr_log_t *log,
                                   int old_state, int new_state,
                                   int event)
{
        glusterd_sm_transition_t *transitions = NULL;
        int                      ret = -1;
        int                      next = 0;

        GF_ASSERT (log);
        if (!log)
                goto out;

        transitions = log->transitions;
        if (!transitions)
                goto out;

        if (log->count)
                next = (log->current + 1) % log->size;
        else
                next = 0;

        transitions[next].old_state = old_state;
        transitions[next].new_state = new_state;
        transitions[next].event     = event;
        time (&transitions[next].time);
        log->current = next;
        if (log->count < log->size)
                log->count++;
        ret = 0;
        gf_log ("glusterd", GF_LOG_DEBUG, "Transitioning from '%s' to '%s' "
                "due to event '%s'", log->state_name_get (old_state),
                log->state_name_get (new_state), log->event_name_get (event));
out:
        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int
glusterd_peerinfo_new (glusterd_peerinfo_t **peerinfo,
                       glusterd_friend_sm_state_t state,
                       uuid_t *uuid, const char *hostname)
{
        glusterd_peerinfo_t      *new_peer = NULL;
        int                      ret = -1;

        GF_ASSERT (peerinfo);
        if (!peerinfo)
                goto out;

        new_peer = GF_CALLOC (1, sizeof (*new_peer), gf_gld_mt_peerinfo_t);
        if (!new_peer)
                goto out;

        new_peer->state.state = state;
        if (hostname)
                new_peer->hostname = gf_strdup (hostname);

        INIT_LIST_HEAD (&new_peer->uuid_list);

        if (uuid) {
                uuid_copy (new_peer->uuid, *uuid);
        }

        ret = glusterd_sm_tr_log_init (&new_peer->sm_log,
                                       glusterd_friend_sm_state_name_get,
                                       glusterd_friend_sm_event_name_get,
                                       GLUSTERD_TR_LOG_SIZE);
        if (ret)
                goto out;

        *peerinfo = new_peer;
out:
        if (ret && new_peer)
                glusterd_friend_cleanup (new_peer);
        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int32_t
glusterd_peer_destroy (glusterd_peerinfo_t *peerinfo)
{
        int32_t                         ret = -1;

        if (!peerinfo)
                goto out;

        ret = glusterd_store_delete_peerinfo (peerinfo);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Deleting peer info failed");
        }

        list_del_init (&peerinfo->uuid_list);
        if (peerinfo->hostname)
                GF_FREE (peerinfo->hostname);
        glusterd_sm_tr_log_delete (&peerinfo->sm_log);
        GF_FREE (peerinfo);
        peerinfo = NULL;

        ret = 0;

out:
        return ret;
}

int
glusterd_remove_pending_entry (struct list_head *list, void *elem)
{
        glusterd_pending_node_t *pending_node = NULL;
        glusterd_pending_node_t *tmp = NULL;
        int                     ret = -1;

        list_for_each_entry_safe (pending_node, tmp, list, list) {
                if (elem == pending_node->node) {
                        list_del_init (&pending_node->list);
                        GF_FREE (pending_node);
                        ret = 0;
                        goto out;
                }
        }
out:
        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;

}

int
glusterd_clear_pending_nodes (struct list_head *list)
{
        glusterd_pending_node_t *pending_node = NULL;
        glusterd_pending_node_t *tmp = NULL;

        list_for_each_entry_safe (pending_node, tmp, list, list) {
                list_del_init (&pending_node->list);
                GF_FREE (pending_node);
        }

        return 0;
}

gf_boolean_t
glusterd_peerinfo_is_uuid_unknown (glusterd_peerinfo_t *peerinfo)
{
        GF_ASSERT (peerinfo);

        if (uuid_is_null (peerinfo->uuid))
                return _gf_true;
        return _gf_false;
}

int32_t
glusterd_delete_volume (glusterd_volinfo_t *volinfo)
{
        int             ret = -1;
        GF_ASSERT (volinfo);

        ret = glusterd_store_delete_volume (volinfo);

        if (ret)
                goto out;

        ret = glusterd_volinfo_delete (volinfo);
out:
        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int32_t
glusterd_delete_brick (glusterd_volinfo_t* volinfo,
                       glusterd_brickinfo_t *brickinfo)
{
        int             ret = 0;
        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);

#ifdef DEBUG
        ret = glusterd_volume_brickinfo_get (brickinfo->uuid,
                                             brickinfo->hostname,
                                             brickinfo->path, volinfo, NULL);
        GF_ASSERT (0 == ret);
#endif
        glusterd_delete_volfile (volinfo, brickinfo);
        glusterd_store_delete_brick (volinfo, brickinfo);
        glusterd_brickinfo_delete (brickinfo);
        volinfo->brick_count--;
        return ret;
}

int32_t
glusterd_delete_all_bricks (glusterd_volinfo_t* volinfo)
{
        int             ret = 0;
        glusterd_brickinfo_t *brickinfo = NULL;
        glusterd_brickinfo_t *tmp = NULL;

        GF_ASSERT (volinfo);

        list_for_each_entry_safe (brickinfo, tmp, &volinfo->bricks, brick_list) {
                ret = glusterd_delete_brick (volinfo, brickinfo);
        }
        return ret;
}

int
mkdir_if_missing (char *path)
{
        struct stat st = {0,};
        int        ret = 0;

        ret = mkdir (path, 0777);
        if (!ret || errno == EEXIST)
                ret = stat (path, &st);
        if (ret == -1 || !S_ISDIR (st.st_mode))
                gf_log ("", GF_LOG_WARNING, "Failed to create the"
                        " directory %s", path);

        return ret;
}

int
glusterd_start_gsync (glusterd_volinfo_t *master_vol, char *slave,
                      char *glusterd_uuid_str, char **op_errstr)
{
        int32_t         ret     = 0;
        int32_t         status  = 0;
        char            buf[PATH_MAX]   = {0,};
        char            uuid_str [64] = {0};
        runner_t        runner = {0,};
        xlator_t        *this = NULL;
        glusterd_conf_t *priv = NULL;
        int             errcode = 0;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        uuid_utoa_r (priv->uuid, uuid_str);
        if (strcmp (uuid_str, glusterd_uuid_str))
                goto out;

        ret = gsync_status (master_vol->volname, slave, &status);
        if (status == 0)
                goto out;

        snprintf (buf, PATH_MAX, "%s/"GEOREP"/%s", priv->workdir, master_vol->volname);
        ret = mkdir_if_missing (buf);
        if (ret) {
                errcode = -1;
                goto out;
        }

        snprintf (buf, PATH_MAX, DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"/%s",
                  master_vol->volname);
        ret = mkdir_if_missing (buf);
        if (ret) {
                errcode = -1;
                goto out;
        }

        uuid_utoa_r (master_vol->volume_id, uuid_str);
        runinit (&runner);
        runner_add_args  (&runner, GSYNCD_PREFIX"/gsyncd", "-c", NULL);
        runner_argprintf (&runner, "%s/"GSYNC_CONF, priv->workdir);
        runner_argprintf (&runner, ":%s", master_vol->volname);
        runner_add_args  (&runner, slave, "--config-set", "session-owner",
                          uuid_str, NULL);
        ret = runner_run (&runner);
        if (ret == -1) {
                errcode = -1;
                goto out;
        }

        runinit (&runner);
        runner_add_args  (&runner, GSYNCD_PREFIX"/gsyncd", "--monitor", "-c", NULL);
        runner_argprintf (&runner, "%s/"GSYNC_CONF, priv->workdir);
        runner_argprintf (&runner, ":%s", master_vol->volname);
        runner_add_arg   (&runner, slave);
        ret = runner_run (&runner);
        if (ret == -1) {
                gf_asprintf (op_errstr, GEOREP" start failed for %s %s",
                             master_vol->volname, slave);
                goto out;
        }

        ret = 0;

out:
        if ((ret != 0) && errcode == -1) {
                if (op_errstr)
                        *op_errstr = gf_strdup ("internal error, cannot start"
                                                "the " GEOREP " session");
        }

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_recreate_bricks (glusterd_conf_t *conf)
{

        glusterd_volinfo_t      *volinfo = NULL;
        int                      ret = 0;

        GF_ASSERT (conf);
        list_for_each_entry (volinfo, &conf->volumes, vol_list) {
                ret = generate_brick_volfiles (volinfo);
        }
        return ret;
}

int32_t
glusterd_handle_upgrade_downgrade (dict_t *options, glusterd_conf_t *conf)
{
        int              ret                            = 0;
        char            *type                           = NULL;
        gf_boolean_t     upgrade                        = _gf_false;
        gf_boolean_t     downgrade                      = _gf_false;
        gf_boolean_t     regenerate_brick_volfiles      = _gf_false;

        ret = dict_get_str (options, "upgrade", &type);
        if (!ret) {
                ret = gf_string2boolean (type, &upgrade);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR, "upgrade option "
                                "%s is not a valid boolean type", type);
                        ret = -1;
                        goto out;
                }
                if (_gf_true == upgrade)
                        regenerate_brick_volfiles = _gf_true;
        }

        ret = dict_get_str (options, "downgrade", &type);
        if (!ret) {
                ret = gf_string2boolean (type, &downgrade);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR, "downgrade option "
                                "%s is not a valid boolean type", type);
                        ret = -1;
                        goto out;
                }
        }

        if (upgrade && downgrade) {
                gf_log ("glusterd", GF_LOG_ERROR, "Both upgrade and downgrade"
                        " options are set. Only one should be on");
                ret = -1;
                goto out;
        }

        if (!upgrade && !downgrade)
                ret = 0;
        if (regenerate_brick_volfiles) {
                ret = glusterd_recreate_bricks (conf);
        }
out:
        return ret;
}

gf_boolean_t
glusterd_is_volume_replicate (glusterd_volinfo_t *volinfo)
{
        gf_boolean_t    replicates = _gf_false;
        if (volinfo && ((volinfo->type == GF_CLUSTER_TYPE_REPLICATE) ||
            (volinfo->type == GF_CLUSTER_TYPE_STRIPE_REPLICATE)))
                replicates = _gf_true;
        return replicates;
}
