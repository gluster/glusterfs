/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
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
#include "syncop.h"
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
#include "glusterfs-acl.h"
#include "glusterd-syncop.h"

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
#include <fnmatch.h>
#include <sys/statvfs.h>
#include <ifaddrs.h>
#ifdef HAVE_BD_XLATOR
#include <lvm2app.h>
#endif


#ifdef GF_LINUX_HOST_OS
#include <mntent.h>
#endif

#ifdef GF_SOLARIS_HOST_OS
#include <sys/sockio.h>
#endif

#define NFS_PROGRAM         100003
#define NFSV3_VERSION       3

#define MOUNT_PROGRAM       100005
#define MOUNTV3_VERSION     3
#define MOUNTV1_VERSION     1

#define NLM_PROGRAM         100021
#define NLMV4_VERSION       4
#define NLMV1_VERSION       1

#define CEILING_POS(X) (((X)-(int)(X)) > 0 ? (int)((X)+1) : (int)(X))

static glusterd_lock_t lock;

char*
gd_peer_uuid_str (glusterd_peerinfo_t *peerinfo)
{
        if ((peerinfo == NULL) || uuid_is_null (peerinfo->uuid))
                return NULL;

        if (peerinfo->uuid_str[0] == '\0')
                uuid_utoa_r (peerinfo->uuid, peerinfo->uuid_str);

        return peerinfo->uuid_str;
}


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

int32_t
glusterd_lock (uuid_t   uuid)
{

        uuid_t  owner;
        char    new_owner_str[50];
        char    owner_str[50];
        int     ret = -1;
        xlator_t *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (uuid);

        glusterd_get_lock_owner (&owner);

        if (!uuid_is_null (owner)) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get lock"
                        " for uuid: %s, lock held by: %s",
                        uuid_utoa_r (uuid, new_owner_str),
                        uuid_utoa_r (owner, owner_str));
                goto out;
        }

        ret = glusterd_set_lock_owner (uuid);

        if (!ret) {
                gf_log (this->name, GF_LOG_DEBUG, "Cluster lock held by"
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
        xlator_t *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (uuid);

        glusterd_get_lock_owner (&owner);

        if (uuid_is_null (owner)) {
                gf_log (this->name, GF_LOG_ERROR, "Cluster lock not held!");
                goto out;
        }

        ret = uuid_compare (uuid, owner);

        if (ret) {
               gf_log (this->name, GF_LOG_ERROR, "Cluster lock held by %s ,"
                       "unlock req from %s!", uuid_utoa_r (owner ,owner_str)
                        , uuid_utoa_r (uuid, new_owner_str));
               goto out;
        }

        ret = glusterd_unset_lock_owner (uuid);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to clear cluster "
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

        uuid_copy (*uuid, MY_UUID);

        return 0;
}

int
glusterd_submit_request_unlocked (struct rpc_clnt *rpc, void *req,
				  call_frame_t *frame, rpc_clnt_prog_t *prog,
				  int procnum, struct iobref *iobref,
				  xlator_t *this, fop_cbk_fn_t cbkfn,
				  xdrproc_t xdrproc)
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


int
glusterd_submit_request (struct rpc_clnt *rpc, void *req,
                         call_frame_t *frame, rpc_clnt_prog_t *prog,
                         int procnum, struct iobref *iobref,
                         xlator_t *this, fop_cbk_fn_t cbkfn, xdrproc_t xdrproc)
{
        glusterd_conf_t         *priv = THIS->private;
	int ret = -1;

	synclock_unlock (&priv->big_lock);
	{
		ret = glusterd_submit_request_unlocked (rpc, req, frame, prog,
							procnum, iobref, this,
							cbkfn, xdrproc);
	}
	synclock_lock (&priv->big_lock);

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
                gf_log (THIS->name, GF_LOG_DEBUG, "Volume %s does not exist."
                        "stat failed with errno : %d on path: %s",
                        volname, errno, pathname);
                return _gf_false;
        }

        return _gf_true;
}

glusterd_volinfo_t *
glusterd_volinfo_unref (glusterd_volinfo_t *volinfo)
{
        int refcnt = -1;

        pthread_mutex_lock (&volinfo->reflock);
        {
                refcnt = --volinfo->refcnt;
        }
        pthread_mutex_unlock (&volinfo->reflock);

        if (!refcnt) {
                glusterd_volinfo_delete (volinfo);
                return NULL;
        }

        return volinfo;
}

glusterd_volinfo_t *
glusterd_volinfo_ref (glusterd_volinfo_t *volinfo)
{
        pthread_mutex_lock (&volinfo->reflock);
        {
                ++volinfo->refcnt;
        }
        pthread_mutex_unlock (&volinfo->reflock);

        return volinfo;
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
                GF_FREE (new_volinfo);

                goto out;
        }

        new_volinfo->gsync_slaves = dict_new ();
        if (!new_volinfo->gsync_slaves) {
                GF_FREE (new_volinfo);

                goto out;
        }

        new_volinfo->xl = THIS;

        pthread_mutex_init (&new_volinfo->reflock, NULL);
        *volinfo = glusterd_volinfo_ref (new_volinfo);

        ret = 0;

out:
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

void
glusterd_auth_cleanup (glusterd_volinfo_t *volinfo) {

        GF_ASSERT (volinfo);

        GF_FREE (volinfo->auth.username);

        GF_FREE (volinfo->auth.password);
}

char *
glusterd_auth_get_username (glusterd_volinfo_t *volinfo) {

        GF_ASSERT (volinfo);

        return volinfo->auth.username;
}

char *
glusterd_auth_get_password (glusterd_volinfo_t *volinfo) {

        GF_ASSERT (volinfo);

        return volinfo->auth.password;
}

int32_t
glusterd_auth_set_username (glusterd_volinfo_t *volinfo, char *username) {

        GF_ASSERT (volinfo);
        GF_ASSERT (username);

        volinfo->auth.username = gf_strdup (username);
        return 0;
}

int32_t
glusterd_auth_set_password (glusterd_volinfo_t *volinfo, char *password) {

        GF_ASSERT (volinfo);
        GF_ASSERT (password);

        volinfo->auth.password = gf_strdup (password);
        return 0;
}

int32_t
glusterd_brickinfo_delete (glusterd_brickinfo_t *brickinfo)
{
        int32_t         ret = -1;

        GF_ASSERT (brickinfo);

        list_del_init (&brickinfo->brick_list);

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
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_volinfo_remove (glusterd_volinfo_t *volinfo)
{
        list_del_init (&volinfo->vol_list);
        glusterd_volinfo_unref (volinfo);
        return 0;
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
        GF_FREE (volinfo->logdir);
        if (volinfo->rebal.dict)
                dict_unref (volinfo->rebal.dict);

        gf_store_handle_destroy (volinfo->quota_conf_shandle);

        glusterd_auth_cleanup (volinfo);

        pthread_mutex_destroy (&volinfo->reflock);
        GF_FREE (volinfo);
        ret = 0;

out:
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);
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
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_resolve_brick (glusterd_brickinfo_t *brickinfo)
{
        int32_t                 ret = -1;
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (brickinfo);

        ret = glusterd_hostname_to_uuid (brickinfo->hostname, brickinfo->uuid);
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_brickinfo_new_from_brick (char *brick,
                                   glusterd_brickinfo_t **brickinfo)
{
        int32_t                 ret = -1;
        glusterd_brickinfo_t    *new_brickinfo = NULL;
        char                    *hostname = NULL;
        char                    *path = NULL;
        char                    *tmp_host = NULL;
        char                    *tmp_path = NULL;
        char                    *vg       = NULL;

        GF_ASSERT (brick);
        GF_ASSERT (brickinfo);

        tmp_host = gf_strdup (brick);
        if (tmp_host && !get_host_name (tmp_host, &hostname))
                goto out;
        tmp_path = gf_strdup (brick);
        if (tmp_path && !get_path_name (tmp_path, &path))
                goto out;

        GF_ASSERT (hostname);
        GF_ASSERT (path);

        ret = glusterd_brickinfo_new (&new_brickinfo);
        if (ret)
                goto out;

#ifdef HAVE_BD_XLATOR
        vg = strchr (path, '?');
        /* ? is used as a delimiter for vg */
        if (vg) {
                strncpy (new_brickinfo->vg, vg + 1, PATH_MAX - 1);
                *vg = '\0';
        }
        new_brickinfo->caps = CAPS_BD;
#else
        vg = NULL; /* Avoid compiler warnings when BD not enabled */
#endif
        ret = gf_canonicalize_path (path);
        if (ret)
                goto out;

        strncpy (new_brickinfo->hostname, hostname, 1024);
        strncpy (new_brickinfo->path, path, 1024);

        *brickinfo = new_brickinfo;

        ret = 0;
out:
        GF_FREE (tmp_host);
        if (tmp_host)
                GF_FREE (tmp_path);
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

static gf_boolean_t
_is_prefix (char *str1, char *str2)
{
        GF_ASSERT (str1);
        GF_ASSERT (str2);

        int             i = 0;
        int             len1 = 0;
        int             len2 = 0;
        int             small_len = 0;
        char            *bigger = NULL;
        gf_boolean_t    prefix = _gf_true;

        len1 = strlen (str1);
        len2 = strlen (str2);
        small_len = min (len1, len2);
        for (i = 0; i < small_len; i++) {
                if (str1[i] != str2[i]) {
                        prefix = _gf_false;
                        break;
                }
        }

        if (len1 < len2)
            bigger = str2;

        else if (len1 > len2)
            bigger = str1;

        else
            return prefix;

        if (bigger[small_len] != '/')
            prefix = _gf_false;

        return prefix;
}

/* Checks if @path is available in the peer identified by @uuid
 * 'availability' is determined by querying current state of volumes
 * in the cluster. */
gf_boolean_t
glusterd_is_brickpath_available (uuid_t uuid, char *path)
{
        glusterd_brickinfo_t    *brickinfo = NULL;
        glusterd_volinfo_t      *volinfo   = NULL;
        glusterd_conf_t         *priv      = NULL;
        gf_boolean_t            available  = _gf_false;
        char                    tmp_path[PATH_MAX+1] = {0};
        char                    tmp_brickpath[PATH_MAX+1] = {0};

        priv = THIS->private;

        strncpy (tmp_path, path, PATH_MAX);
        /* path may not yet exist */
        if (!realpath (path, tmp_path)) {
                if (errno != ENOENT) {
                        goto out;
                }
                /* When realpath(3) fails, tmp_path is undefined. */
                strncpy(tmp_path,path,PATH_MAX);
        }

        list_for_each_entry (volinfo, &priv->volumes, vol_list) {
                list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                        if (uuid_compare (uuid, brickinfo->uuid))
                                continue;

                        if (!realpath (brickinfo->path, tmp_brickpath)) {
                            if (errno == ENOENT)
                                strncpy (tmp_brickpath, brickinfo->path,
                                         PATH_MAX);
                            else
                                goto out;
                        }

                        if (_is_prefix (tmp_brickpath, tmp_path))
                                goto out;
                }
        }
        available = _gf_true;
out:
        return available;
}

#ifdef HAVE_BD_XLATOR
/*
 * Sets the tag of the format "trusted.glusterfs.volume-id:<uuid>" in
 * the brick VG. It is used to avoid using same VG for another brick.
 * @volume-id - gfid, @brick - brick info, @msg - Error message returned
 * to the caller
 */
int
glusterd_bd_set_vg_tag (unsigned char *volume_id, glusterd_brickinfo_t *brick,
                        char *msg, int msg_size)
{
        lvm_t        handle    = NULL;
        vg_t         vg        = NULL;
        char        *uuid      = NULL;
        int          ret       = -1;

        gf_asprintf (&uuid, "%s:%s", GF_XATTR_VOL_ID_KEY,
                     uuid_utoa (volume_id));
        if (!uuid) {
                snprintf (msg, sizeof(*msg), "Could not allocate memory "
                          "for tag");
                return -1;
        }

        handle = lvm_init (NULL);
        if (!handle) {
                snprintf (msg, sizeof(*msg), "lvm_init failed");
                goto out;
        }

        vg = lvm_vg_open (handle, brick->vg, "w", 0);
        if (!vg) {
                snprintf (msg, sizeof(*msg), "Could not open VG %s",
                          brick->vg);
                goto out;
        }

        if (lvm_vg_add_tag (vg, uuid) < 0) {
                snprintf (msg, sizeof(*msg), "Could not set tag %s for "
                          "VG %s", uuid, brick->vg);
                goto out;
        }
        lvm_vg_write (vg);
        ret = 0;
out:
        GF_FREE (uuid);

        if (vg)
                lvm_vg_close (vg);
        if (handle)
                lvm_quit (handle);

        return ret;
}
#endif

int
glusterd_validate_and_create_brickpath (glusterd_brickinfo_t *brickinfo,
                                        uuid_t volume_id, char **op_errstr,
                                        gf_boolean_t is_force)
{
        int          ret                 = -1;
        char         parentdir[PATH_MAX] = {0,};
        struct stat  parent_st           = {0,};
        struct stat  brick_st            = {0,};
        struct stat  root_st             = {0,};
        char         msg[2048]           = {0,};
        gf_boolean_t is_created          = _gf_false;

        ret = mkdir (brickinfo->path, 0777);
        if (ret) {
                if (errno != EEXIST) {
                        snprintf (msg, sizeof (msg), "Failed to create brick "
                                  "directory for brick %s:%s. Reason : %s ",
                                  brickinfo->hostname, brickinfo->path,
                                  strerror (errno));
                        goto out;
                }
        } else {
                is_created = _gf_true;
        }

        ret = lstat (brickinfo->path, &brick_st);
        if (ret) {
                snprintf (msg, sizeof (msg), "lstat failed on %s. Reason : %s",
                          brickinfo->path, strerror (errno));
                goto out;
        }

        if ((!is_created) && (!S_ISDIR (brick_st.st_mode))) {
                snprintf (msg, sizeof (msg), "The provided path %s which is "
                          "already present, is not a directory",
                          brickinfo->path);
                ret = -1;
                goto out;
        }

        snprintf (parentdir, sizeof (parentdir), "%s/..", brickinfo->path);

        ret = lstat ("/", &root_st);
        if (ret) {
                snprintf (msg, sizeof (msg), "lstat failed on /. Reason : %s",
                          strerror (errno));
                goto out;
        }

        ret = lstat (parentdir, &parent_st);
        if (ret) {
                snprintf (msg, sizeof (msg), "lstat failed on %s. Reason : %s",
                          parentdir, strerror (errno));
                goto out;
        }

        if (!is_force) {
                if (brick_st.st_dev != parent_st.st_dev) {
                        snprintf (msg, sizeof (msg), "The brick %s:%s is a "
                                  "mount point. Please create a sub-directory "
                                  "under the mount point and use that as the "
                                  "brick directory. Or use 'force' at the end "
                                  "of the command if you want to override this "
                                  "behavior.", brickinfo->hostname,
                                  brickinfo->path);
                        ret = -1;
                        goto out;
                }
                else if (parent_st.st_dev == root_st.st_dev) {
                        snprintf (msg, sizeof (msg), "The brick %s:%s is "
                                  "is being created in the root partition. It "
                                  "is recommended that you don't use the "
                                  "system's root partition for storage backend."
                                  " Or use 'force' at the end of the command if"
                                  " you want to override this behavior.",
                                  brickinfo->hostname, brickinfo->path);
                        ret = -1;
                        goto out;
                }
        }

#ifdef HAVE_BD_XLATOR
        if (brickinfo->vg[0]) {
                ret = glusterd_bd_set_vg_tag (volume_id, brickinfo, msg,
                                              sizeof(msg));
                if (ret)
                        goto out;
        }
#endif
        ret = glusterd_check_and_set_brick_xattr (brickinfo->hostname,
                                                  brickinfo->path, volume_id,
                                                  op_errstr, is_force);
        if (ret)
                goto out;

        ret = 0;

out:
        if (ret && is_created)
                rmdir (brickinfo->path);
        if (ret && !*op_errstr && msg[0] != '\0')
                *op_errstr = gf_strdup (msg);

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
        xlator_t                *this = NULL;

        this = THIS;

        if (uuid) {
                uuid_copy (peer_uuid, uuid);
        } else {
                ret = glusterd_hostname_to_uuid (hostname, peer_uuid);
                if (ret)
                        goto out;
        }
        ret = -1;
        list_for_each_entry (brickiter, &volinfo->bricks, brick_list) {

                if ((uuid_is_null (brickiter->uuid)) &&
                    (glusterd_resolve_brick (brickiter) != 0))
                        goto out;
                if (uuid_compare (peer_uuid, brickiter->uuid))
                        continue;

                if (strcmp (brickiter->path, path) == 0) {
                        gf_log (this->name, GF_LOG_DEBUG, LOGSTR_FOUND_BRICK,
                                brickiter->hostname, brickiter->path,
                                volinfo->volname);
                        ret = 0;
                        if (brickinfo)
                                *brickinfo = brickiter;
                        break;
                }
        }

out:
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_volume_brickinfo_get_by_brick (char *brick,
                                        glusterd_volinfo_t *volinfo,
                                        glusterd_brickinfo_t **brickinfo)
{
        int32_t                 ret = -1;
        glusterd_brickinfo_t    *tmp_brickinfo = NULL;

        GF_ASSERT (brick);
        GF_ASSERT (volinfo);

        ret = glusterd_brickinfo_new_from_brick (brick, &tmp_brickinfo);
        if (ret)
                goto out;

        ret = glusterd_volume_brickinfo_get (NULL, tmp_brickinfo->hostname,
                                             tmp_brickinfo->path, volinfo,
                                             brickinfo);
        (void) glusterd_brickinfo_delete (tmp_brickinfo);
out:
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
        glusterd_peerctx_t      *peerctx = NULL;
        gf_boolean_t            quorum_action = _gf_false;
        glusterd_conf_t         *priv = THIS->private;

        if (peerinfo->quorum_contrib != QUORUM_NONE)
                quorum_action = _gf_true;
        if (peerinfo->rpc) {
                /* cleanup the saved-frames before last unref */
                synclock_unlock (&priv->big_lock);
                rpc_clnt_connection_cleanup (&peerinfo->rpc->conn);
                synclock_lock (&priv->big_lock);

                peerctx = peerinfo->rpc->mydata;
                peerinfo->rpc->mydata = NULL;
                peerinfo->rpc = glusterd_rpc_clnt_unref (priv, peerinfo->rpc);
                peerinfo->rpc = NULL;
                if (peerctx) {
                        GF_FREE (peerctx->errstr);
                        GF_FREE (peerctx);
                }
        }
        glusterd_peer_destroy (peerinfo);

        if (quorum_action)
                glusterd_do_quorum_action ();
        return 0;
}

int
glusterd_volinfo_find_by_volume_id (uuid_t volume_id, glusterd_volinfo_t **volinfo)
{
        int32_t                 ret = -1;
        xlator_t                *this = NULL;
        glusterd_volinfo_t      *voliter = NULL;
        glusterd_conf_t         *priv = NULL;

        if (!volume_id)
                return -1;

        this = THIS;
        priv = this->private;

        list_for_each_entry (voliter, &priv->volumes, vol_list) {
                if (uuid_compare (volume_id, voliter->volume_id))
                        continue;
                *volinfo = voliter;
                ret = 0;
                gf_log (this->name, GF_LOG_DEBUG, "Volume %s found",
                        voliter->volname);
                break;
        }
        return ret;
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
        GF_ASSERT (priv);

        list_for_each_entry (tmp_volinfo, &priv->volumes, vol_list) {
                if (!strcmp (tmp_volinfo->volname, volname)) {
                        gf_log (this->name, GF_LOG_DEBUG, "Volume %s found", volname);
                        ret = 0;
                        *volinfo = tmp_volinfo;
                        break;
                }
        }

        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_service_stop (const char *service, char *pidfile, int sig,
                       gf_boolean_t force_kill)
{
        int32_t  ret = -1;
        pid_t    pid = -1;
        xlator_t *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        if (!gf_is_service_running (pidfile, &pid)) {
                ret = 0;
                gf_log (this->name, GF_LOG_INFO, "%s already stopped", service);
                goto out;
        }
        gf_log (this->name, GF_LOG_DEBUG, "Stopping gluster %s running in pid: "
                "%d", service, pid);

        ret = kill (pid, sig);
        if (ret) {
                switch (errno) {
                case ESRCH:
                        gf_log (this->name, GF_LOG_DEBUG, "%s is already stopped",
                                service);
                        ret = 0;
                        goto out;
                default:
                        gf_log (this->name, GF_LOG_ERROR, "Failed to kill %s: %s",
                                service, strerror (errno));
                }
        }
        if (!force_kill)
                goto out;

        sleep (1);
        if (gf_is_service_running (pidfile, NULL)) {
                ret = kill (pid, SIGKILL);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Unable to "
                                "kill pid %d reason: %s", pid,
                                strerror(errno));
                        goto out;
                }
        }

        ret = 0;
out:
        return ret;
}

void
glusterd_set_socket_filepath (char *sock_filepath, char *sockpath, size_t len)
{
        char md5_sum[MD5_DIGEST_LENGTH*2+1] = {0,};

        md5_wrapper ((unsigned char *) sock_filepath, strlen(sock_filepath), md5_sum);
        snprintf (sockpath, len, "%s/%s.socket", GLUSTERD_SOCK_DIR, md5_sum);
}

void
glusterd_set_brick_socket_filepath (glusterd_volinfo_t *volinfo,
                                    glusterd_brickinfo_t *brickinfo,
                                    char *sockpath, size_t len)
{
        char                    export_path[PATH_MAX] = {0,};
        char                    sock_filepath[PATH_MAX] = {0,};
        char                    volume_dir[PATH_MAX] = {0,};
        xlator_t                *this = NULL;
        glusterd_conf_t         *priv = NULL;
        int                     expected_file_len = 0;

        expected_file_len = strlen (GLUSTERD_SOCK_DIR) + strlen ("/") +
                            MD5_DIGEST_LENGTH*2 + strlen (".socket") + 1;
        GF_ASSERT (len >= expected_file_len);
        this = THIS;
        GF_ASSERT (this);

        priv = this->private;

        GLUSTERD_GET_VOLUME_DIR (volume_dir, volinfo, priv);
        GLUSTERD_REMOVE_SLASH_FROM_PATH (brickinfo->path, export_path);
        snprintf (sock_filepath, PATH_MAX, "%s/run/%s-%s",
                  volume_dir, brickinfo->hostname, export_path);

        glusterd_set_socket_filepath (sock_filepath, sockpath, len);
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
        char                    volume_id_str[64];
        char                    *brickid = NULL;
        dict_t                  *options = NULL;
        struct rpc_clnt         *rpc = NULL;
        glusterd_conf_t         *priv = THIS->private;

        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);

        if (brickinfo->rpc == NULL) {
                glusterd_set_brick_socket_filepath (volinfo, brickinfo,
                                                    socketpath,
                                                    sizeof (socketpath));

                /* Setting frame-timeout to 10mins (600seconds).
                 * Unix domain sockets ensures that the connection is reliable.
                 * The default timeout of 30mins used for unreliable network
                 * connections is too long for unix domain socket connections.
                 */
                ret = rpc_transport_unix_options_build (&options, socketpath,
                                                        600);
                if (ret)
                        goto out;

                uuid_utoa_r (volinfo->volume_id, volume_id_str);
                ret = gf_asprintf (&brickid, "%s:%s:%s", volume_id_str,
                                   brickinfo->hostname, brickinfo->path);
                if (ret < 0)
                        goto out;

                synclock_unlock (&priv->big_lock);
                ret = glusterd_rpc_create (&rpc, options,
                                           glusterd_brick_rpc_notify,
                                           brickid);
                synclock_lock (&priv->big_lock);
                if (ret) {
                        GF_FREE (brickid);
                        goto out;
                }
                brickinfo->rpc = rpc;
        }
out:

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

static int
_mk_rundir_p (glusterd_volinfo_t *volinfo)
{
        char voldir[PATH_MAX]   = {0,};
        char rundir[PATH_MAX]   = {0,};
        glusterd_conf_t *priv   = NULL;
        xlator_t        *this   = NULL;
        int             ret     = -1;

        this = THIS;
        priv = this->private;
        GLUSTERD_GET_VOLUME_DIR (voldir, volinfo, priv);
        snprintf (rundir, sizeof (rundir)-1, "%s/run", voldir);
        ret =  mkdir_p (rundir, 0777, _gf_true);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR, "Failed to create rundir");
        return ret;
}

int32_t
glusterd_volume_start_glusterfs (glusterd_volinfo_t  *volinfo,
                                 glusterd_brickinfo_t  *brickinfo,
                                 gf_boolean_t wait)
{
        int32_t                 ret = -1;
        xlator_t                *this = NULL;
        glusterd_conf_t         *priv = NULL;
        char                    pidfile[PATH_MAX+1] = {0,};
        char                    volfile[PATH_MAX] = {0,};
        runner_t                runner = {0,};
        char                    exp_path[PATH_MAX] = {0,};
        char                    logfile[PATH_MAX] = {0,};
        int                     port = 0;
        int                     rdma_port = 0;
        char                    socketpath[PATH_MAX] = {0};
        char                    glusterd_uuid[1024] = {0,};
        char                    valgrind_logfile[PATH_MAX] = {0};

        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = _mk_rundir_p (volinfo);
        if (ret)
                goto out;
        GLUSTERD_GET_BRICK_PIDFILE (pidfile, volinfo, brickinfo, priv);
        if (gf_is_service_running (pidfile, NULL))
                goto connect;

        port = brickinfo->port;
        if (!port)
                port = pmap_registry_alloc (THIS);

        /* Build the exp_path, before starting the glusterfsd even in
           valgrind mode. Otherwise all the glusterfsd processes start
           writing the valgrind log to the same file.
        */
        GLUSTERD_REMOVE_SLASH_FROM_PATH (brickinfo->path, exp_path);
        runinit (&runner);

        if (priv->valgrind) {
                /* Run bricks with valgrind */
                if (volinfo->logdir) {
                        snprintf (valgrind_logfile, PATH_MAX,
                                  "%s/valgrind-%s-%s.log",
                                  volinfo->logdir,
                                  volinfo->volname, exp_path);
                } else {
                        snprintf (valgrind_logfile, PATH_MAX,
                                  "%s/bricks/valgrind-%s-%s.log",
                                  DEFAULT_LOG_FILE_DIRECTORY,
                                  volinfo->volname, exp_path);
                }

                runner_add_args (&runner, "valgrind", "--leak-check=full",
                                 "--trace-children=yes", "--track-origins=yes",
                                 NULL);
                runner_argprintf (&runner, "--log-file=%s", valgrind_logfile);
        }

        snprintf (volfile, PATH_MAX, "%s.%s.%s", volinfo->volname,
                  brickinfo->hostname, exp_path);

        if (volinfo->logdir) {
                snprintf (logfile, PATH_MAX, "%s/%s.log",
                          volinfo->logdir, exp_path);
        } else {
                snprintf (logfile, PATH_MAX, "%s/bricks/%s.log",
                          DEFAULT_LOG_FILE_DIRECTORY, exp_path);
        }
        if (!brickinfo->logfile)
                brickinfo->logfile = gf_strdup (logfile);

        glusterd_set_brick_socket_filepath (volinfo, brickinfo, socketpath,
                                            sizeof (socketpath));

        (void) snprintf (glusterd_uuid, 1024, "*-posix.glusterd-uuid=%s",
                         uuid_utoa (MY_UUID));
        runner_add_args (&runner, SBIN_DIR"/glusterfsd",
                         "-s", brickinfo->hostname, "--volfile-id", volfile,
                         "-p", pidfile, "-S", socketpath,
                         "--brick-name", brickinfo->path,
                         "-l", brickinfo->logfile,
                         "--xlator-option", glusterd_uuid,
                         NULL);

        runner_add_arg (&runner, "--brick-port");
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

        if (volinfo->memory_accounting)
                runner_add_arg (&runner, "--mem-accounting");

        runner_log (&runner, "", GF_LOG_DEBUG, "Starting GlusterFS");
        if (wait) {
                synclock_unlock (&priv->big_lock);
                ret = runner_run (&runner);
                synclock_lock (&priv->big_lock);

        } else {
                ret = runner_run_nowait (&runner);
        }

        if (ret)
                goto out;

        brickinfo->port = port;
        brickinfo->rdma_port = rdma_port;

connect:
        ret = glusterd_brick_connect (volinfo, brickinfo);
        if (ret)
                goto out;
out:
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
                gf_log (this->name, GF_LOG_ERROR, "Failed to remove %s"
                        " error: %s", socketpath, strerror (errno));
        }

        return ret;
}

int32_t
glusterd_brick_disconnect (glusterd_brickinfo_t *brickinfo)
{
        rpc_clnt_t              *rpc = NULL;
        glusterd_conf_t         *priv = THIS->private;

        GF_ASSERT (brickinfo);

        if (!brickinfo) {
                gf_log_callingfn ("glusterd", GF_LOG_WARNING, "!brickinfo");
                return -1;
        }

        rpc            = brickinfo->rpc;
        brickinfo->rpc = NULL;

        if (rpc) {
                glusterd_rpc_clnt_unref (priv, rpc);
        }

        return 0;
}

int32_t
glusterd_volume_stop_glusterfs (glusterd_volinfo_t  *volinfo,
                                glusterd_brickinfo_t   *brickinfo,
                                gf_boolean_t del_brick)
{
        xlator_t                *this = NULL;
        glusterd_conf_t         *priv = NULL;
        char                    pidfile[PATH_MAX] = {0,};
        int                     ret = 0;

        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        if (del_brick)
                list_del_init (&brickinfo->brick_list);

        if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                (void) glusterd_brick_disconnect (brickinfo);
                GLUSTERD_GET_BRICK_PIDFILE (pidfile, volinfo, brickinfo, priv);
                ret = glusterd_service_stop ("brick", pidfile, SIGTERM, _gf_false);
                if (ret == 0) {
                        glusterd_set_brick_status (brickinfo, GF_BRICK_STOPPED);
                        (void) glusterd_brick_unlink_socket_file (volinfo, brickinfo);
                }
        }

        if (del_brick)
                glusterd_delete_brick (volinfo, brickinfo);

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

/* Free LINE[0..N-1] and then the LINE buffer.  */
static void
free_lines (char **line, size_t n)
{
  size_t i;
  for (i = 0; i < n; i++)
    GF_FREE (line[i]);
  GF_FREE (line);
}

char **
glusterd_readin_file (const char *filepath, int *line_count)
{
        int         ret                    = -1;
        int         n                      = 8;
        int         counter                = 0;
        char        buffer[PATH_MAX + 256] = {0};
        char      **lines                  = NULL;
        FILE       *fp                     = NULL;
        void       *p;

        fp = fopen (filepath, "r");
        if (!fp)
                goto out;

        lines = GF_CALLOC (1, n * sizeof (*lines), gf_gld_mt_charptr);
        if (!lines)
                goto out;

        for (counter = 0; fgets (buffer, sizeof (buffer), fp); counter++) {

                if (counter == n-1) {
                        n *= 2;
                        p = GF_REALLOC (lines, n * sizeof (char *));
                        if (!p) {
                                free_lines (lines, n/2);
                                lines = NULL;
                                goto out;
                        }
                        lines = p;
                }

                lines[counter] = gf_strdup (buffer);
        }

        lines[counter] = NULL;
        /* Reduce allocation to minimal size.  */
        p = GF_REALLOC (lines, (counter + 1) * sizeof (char *));
        if (!p) {
                free_lines (lines, counter);
                lines = NULL;
                goto out;
        }
        lines = p;

        *line_count = counter;
        ret = 0;

 out:
        if (ret)
                gf_log (THIS->name, GF_LOG_ERROR, "%s", strerror (errno));
        if (fp)
                fclose (fp);

        return lines;
}

int
glusterd_compare_lines (const void *a, const void *b) {

        return strcmp(* (char * const *) a, * (char * const *) b);
}

int
glusterd_sort_and_redirect (const char *src_filepath, int dest_fd)
{
        int            ret          = -1;
        int            line_count   = 0;
        int            counter      = 0;
        char         **lines        = NULL;


        if (!src_filepath || dest_fd < 0)
                goto out;

        lines = glusterd_readin_file (src_filepath, &line_count);
        if (!lines)
                goto out;

        qsort (lines, line_count, sizeof (*lines), glusterd_compare_lines);

        for (counter = 0; lines[counter]; counter++) {

                ret = write (dest_fd, lines[counter],
                             strlen (lines[counter]));
                if (ret < 0)
                        goto out;

                GF_FREE (lines[counter]);
        }

        ret = 0;
 out:
        GF_FREE (lines);

        return ret;
}

int
glusterd_volume_compute_cksum (glusterd_volinfo_t  *volinfo, char *cksum_path,
                               char *filepath, gf_boolean_t is_quota_conf,
                               uint32_t *cs)
{
        int32_t                 ret                     = -1;
        uint32_t                cksum                   = 0;
        int                     fd                      = -1;
        int                     sort_fd                 = 0;
        char                    sort_filepath[PATH_MAX] = {0};
        char                   *cksum_path_final        = NULL;
        char                    buf[4096]               = {0,};
        gf_boolean_t            unlink_sortfile         = _gf_false;
        glusterd_conf_t        *priv                    = NULL;
        xlator_t               *this                    = NULL;

        GF_ASSERT (volinfo);
        this = THIS;
        priv = THIS->private;
        GF_ASSERT (priv);

        fd = open (cksum_path, O_RDWR | O_APPEND | O_CREAT| O_TRUNC, 0600);

        if (-1 == fd) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to open %s,"
                        " errno: %d", cksum_path, errno);
                ret = -1;
                goto out;
        }

        if (!is_quota_conf) {
                snprintf (sort_filepath, sizeof (sort_filepath),
                          "/tmp/%s.XXXXXX", volinfo->volname);

                sort_fd = mkstemp (sort_filepath);
                if (sort_fd < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "Could not generate "
                                "temp file, reason: %s for volume: %s",
                                strerror (errno), volinfo->volname);
                        goto out;
                } else {
                        unlink_sortfile = _gf_true;
                }

                /* sort the info file, result in sort_filepath */

                ret = glusterd_sort_and_redirect (filepath, sort_fd);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "sorting info file "
                                "failed");
                        goto out;
                }

                ret = close (sort_fd);
                if (ret)
                        goto out;
        }

        cksum_path_final = is_quota_conf ? filepath : sort_filepath;

        ret = get_checksum_for_path (cksum_path_final, &cksum);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "unable to get "
                        "checksum for path: %s", cksum_path_final);
                goto out;
        }
        if (!is_quota_conf) {
                snprintf (buf, sizeof (buf), "%s=%u\n", "info", cksum);
                ret = write (fd, buf, strlen (buf));
                if (ret <= 0) {
                        ret = -1;
                        goto out;
                }
        }

        ret = get_checksum_for_file (fd, &cksum);
        if (ret)
                goto out;

        *cs = cksum;

out:
        if (fd > 0)
               close (fd);
        if (unlink_sortfile)
               unlink (sort_filepath);
        gf_log (this->name, GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

int glusterd_compute_cksum (glusterd_volinfo_t *volinfo,
                               gf_boolean_t is_quota_conf)
{
        int               ret                  = -1;
        uint32_t          cs                   = 0;
        char              cksum_path[PATH_MAX] = {0,};
        char              path[PATH_MAX]       = {0,};
        char              filepath[PATH_MAX]   = {0,};
        glusterd_conf_t  *conf                 = NULL;
        xlator_t         *this                 = NULL;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        GLUSTERD_GET_VOLUME_DIR (path, volinfo, conf);

        if (is_quota_conf) {
                snprintf (cksum_path, sizeof (cksum_path), "%s/%s", path,
                          GLUSTERD_VOL_QUOTA_CKSUM_FILE);
                snprintf (filepath, sizeof (filepath), "%s/%s", path,
                          GLUSTERD_VOLUME_QUOTA_CONFIG);
        } else {
                snprintf (cksum_path, sizeof (cksum_path), "%s/%s", path,
                          GLUSTERD_CKSUM_FILE);
                snprintf (filepath, sizeof (filepath), "%s/%s", path,
                          GLUSTERD_VOLUME_INFO_FILE);
        }

        ret = glusterd_volume_compute_cksum (volinfo, cksum_path, filepath,
                                             is_quota_conf, &cs);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to compute checksum "
                        "for volume %s", volinfo->volname);
                goto out;
        }

        if (is_quota_conf)
                volinfo->quota_conf_cksum = cs;
        else
                volinfo->cksum = cs;

        ret = 0;
out:
        return ret;
}

int
_add_dict_to_prdict (dict_t *this, char *key, data_t *value, void *data)
{
        glusterd_dict_ctx_t     *ctx = NULL;
        char                    optkey[512] = {0,};
        int                     ret = -1;

        ctx = data;
        snprintf (optkey, sizeof (optkey), "%s.%s%d", ctx->prefix,
                  ctx->key_name, ctx->opt_count);
        ret = dict_set_str (ctx->dict, optkey, key);
        if (ret)
                gf_log ("", GF_LOG_ERROR, "option add for %s%d %s",
                        ctx->key_name, ctx->opt_count, key);
        snprintf (optkey, sizeof (optkey), "%s.%s%d", ctx->prefix,
                  ctx->val_name, ctx->opt_count);
        ret = dict_set_str (ctx->dict, optkey, value->data);
        if (ret)
                gf_log ("", GF_LOG_ERROR, "option add for %s%d %s",
                        ctx->val_name, ctx->opt_count, value->data);
        ctx->opt_count++;

        return ret;
}

int32_t
glusterd_add_bricks_hname_path_to_dict (dict_t *dict,
                                        glusterd_volinfo_t *volinfo)
{
        glusterd_brickinfo_t    *brickinfo = NULL;
        int                     ret = 0;
        char                    key[256] = {0};
        int                     index = 0;


        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                snprintf (key, sizeof (key), "%d-hostname", index);
                ret = dict_set_str (dict, key, brickinfo->hostname);
                if (ret)
                        goto out;

                snprintf (key, sizeof (key), "%d-path", index);
                ret = dict_set_str (dict, key, brickinfo->path);
                if (ret)
                        goto out;

                index++;
        }
out:
        return ret;
}

int32_t
glusterd_add_volume_to_dict (glusterd_volinfo_t *volinfo,
                             dict_t  *dict, int32_t count)
{
        int32_t                 ret               = -1;
        char                    prefix[512]       = {0,};
        char                    key[512]          = {0,};
        glusterd_brickinfo_t    *brickinfo        = NULL;
        int32_t                 i                 = 1;
        char                    *volume_id_str    = NULL;
        char                    *src_brick        = NULL;
        char                    *dst_brick        = NULL;
        char                    *str              = NULL;
        glusterd_dict_ctx_t     ctx               = {0};
        char                    *rebalance_id_str = NULL;
        char                    *rb_id_str        = NULL;

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
        snprintf (key, sizeof (key), "volume%d.replica_count", count);
        ret = dict_set_int32 (dict, key, volinfo->replica_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.dist_count", count);
        ret = dict_set_int32 (dict, key, volinfo->dist_leaf_count);
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
        if (!volume_id_str) {
                ret = -1;
                goto out;
        }
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.volume_id", count);
        ret = dict_set_dynstr (dict, key, volume_id_str);
        if (ret)
                goto out;
        volume_id_str = NULL;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.username", count);
        str = glusterd_auth_get_username (volinfo);
        if (str) {
                ret = dict_set_dynstr (dict, key, gf_strdup (str));
                if (ret)
                        goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.password", count);
        str = glusterd_auth_get_password (volinfo);
        if (str) {
                ret = dict_set_dynstr (dict, key, gf_strdup (str));
                if (ret)
                        goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, 256, "volume%d.rebalance", count);
        ret = dict_set_int32 (dict, key, volinfo->rebal.defrag_cmd);
        if (ret)
                goto out;

        rebalance_id_str = gf_strdup (uuid_utoa
                        (volinfo->rebal.rebalance_id));
        if (!rebalance_id_str) {
                ret = -1;
                goto out;
        }
        memset (key, 0, sizeof (key));
        snprintf (key, 256, "volume%d.rebalance-id", count);
        ret = dict_set_dynstr (dict, key, rebalance_id_str);
        if (ret)
                goto out;
        rebalance_id_str = NULL;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.rebalance-op", count);
        ret = dict_set_uint32 (dict, key, volinfo->rebal.op);
        if (ret)
                goto out;

        if (volinfo->rebal.dict) {
                snprintf (prefix, sizeof (prefix), "volume%d", count);
                ctx.dict = dict;
                ctx.prefix = prefix;
                ctx.opt_count = 1;
                ctx.key_name = "rebal-dict-key";
                ctx.val_name = "rebal-dict-value";

                dict_foreach (volinfo->rebal.dict, _add_dict_to_prdict, &ctx);
                ctx.opt_count--;
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d.rebal-dict-count", count);
                ret = dict_set_int32 (dict, key, ctx.opt_count);
                if (ret)
                        goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, 256, "volume%d."GLUSTERD_STORE_KEY_RB_STATUS, count);
        ret = dict_set_int32 (dict, key, volinfo->rep_brick.rb_status);
        if (ret)
                goto out;

        if (volinfo->rep_brick.rb_status > GF_RB_STATUS_NONE) {

                memset (key, 0, sizeof (key));
                snprintf (key, 256, "volume%d."GLUSTERD_STORE_KEY_RB_SRC_BRICK,
                          count);
                gf_asprintf (&src_brick, "%s:%s",
                             volinfo->rep_brick.src_brick->hostname,
                             volinfo->rep_brick.src_brick->path);
                ret = dict_set_dynstr (dict, key, src_brick);
                if (ret)
                        goto out;

                memset (key, 0, sizeof (key));
                snprintf (key, 256, "volume%d."GLUSTERD_STORE_KEY_RB_DST_BRICK,
                          count);
                gf_asprintf (&dst_brick, "%s:%s",
                             volinfo->rep_brick.dst_brick->hostname,
                             volinfo->rep_brick.dst_brick->path);
                ret = dict_set_dynstr (dict, key, dst_brick);
                if (ret)
                        goto out;

                rb_id_str = gf_strdup (uuid_utoa (volinfo->rep_brick.rb_id));
                if (!rb_id_str) {
                        ret = -1;
                        goto out;
                }

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d.rb_id", count);
                ret = dict_set_dynstr (dict, key, rb_id_str);
                if (ret)
                        goto out;
                rb_id_str = NULL;
        }

        snprintf (prefix, sizeof (prefix), "volume%d", count);
        ctx.dict = dict;
        ctx.prefix = prefix;
        ctx.opt_count = 1;
        ctx.key_name = "key";
        ctx.val_name = "value";
        GF_ASSERT (volinfo->dict);

        dict_foreach (volinfo->dict, _add_dict_to_prdict, &ctx);
        ctx.opt_count--;
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.opt-count", count);
        ret = dict_set_int32 (dict, key, ctx.opt_count);
        if (ret)
                goto out;

        ctx.dict = dict;
        ctx.prefix = prefix;
        ctx.opt_count = 1;
        ctx.key_name = "slave-num";
        ctx.val_name = "slave-val";
        GF_ASSERT (volinfo->gsync_slaves);

        dict_foreach (volinfo->gsync_slaves, _add_dict_to_prdict, &ctx);
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

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d.brick%d.decommissioned",
                          count, i);
                ret = dict_set_int32 (dict, key, brickinfo->decommissioned);
                if (ret)
                        goto out;

                i++;
        }

        /* Add volume op-versions to dict. This prevents volume inconsistencies
         * in the cluster
         */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.op-version", count);
        ret = dict_set_int32 (dict, key, volinfo->op_version);
        if (ret)
                goto out;
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.client-op-version", count);
        ret = dict_set_int32 (dict, key, volinfo->client_op_version);

out:
        GF_FREE (volume_id_str);
        GF_FREE (rebalance_id_str);
        GF_FREE (rb_id_str);

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

int
glusterd_vol_add_quota_conf_to_dict (glusterd_volinfo_t *volinfo, dict_t* load,
                                     int vol_idx)
{
        int   fd                    = -1;
        char  *gfid_str             = NULL;
        unsigned char  buf[16]      = {0};
        char  key[PATH_MAX]         = {0};
        int   gfid_idx              = 0;
        int   ret                   = -1;
        xlator_t *this              = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = glusterd_store_create_quota_conf_sh_on_absence (volinfo);
        if (ret)
                goto out;

        fd = open (volinfo->quota_conf_shandle->path, O_RDONLY);
        if (fd == -1) {
                ret = -1;
                goto out;
        }

        ret = glusterd_store_quota_conf_skip_header (this, fd);
        if (ret)
                goto out;

        for (gfid_idx=0; ; gfid_idx++) {

                ret = read (fd, (void*)&buf, 16) ;
                if (ret <= 0) {
                        //Finished reading all entries in the conf file
                        break;
                }
                if (ret != 16) {
                        //This should never happen. We must have a multiple of
                        //entry_sz bytes in our configuration file.
                        gf_log (this->name, GF_LOG_CRITICAL, "Quota "
                                "configuration store may be corrupt.");
                        goto out;
                }

                gfid_str = gf_strdup (uuid_utoa (buf));
                if (!gfid_str) {
                        ret = -1;
                        goto out;
                }

                snprintf (key, sizeof(key)-1, "volume%d.gfid%d", vol_idx,
                          gfid_idx);
                key[sizeof(key)-1] = '\0';
                ret = dict_set_dynstr (load, key, gfid_str);
                if (ret) {
                        goto out;
                }

                gfid_str = NULL;
        }

        snprintf (key, sizeof(key)-1, "volume%d.gfid-count", vol_idx);
        key[sizeof(key)-1] = '\0';
        ret = dict_set_int32 (load, key, gfid_idx);
        if (ret)
                goto out;

        snprintf (key, sizeof(key)-1, "volume%d.quota-cksum", vol_idx);
        key[sizeof(key)-1] = '\0';
        ret = dict_set_uint32 (load, key, volinfo->quota_conf_cksum);
        if (ret)
                goto out;

        snprintf (key, sizeof(key)-1, "volume%d.quota-version", vol_idx);
        key[sizeof(key)-1] = '\0';
        ret = dict_set_uint32 (load, key, volinfo->quota_conf_version);
        if (ret)
                goto out;

        ret = 0;
out:
        if (fd != -1)
                close (fd);
        GF_FREE (gfid_str);
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
        glusterd_dict_ctx_t     ctx            = {0};

        priv = THIS->private;

        dict = dict_new ();

        if (!dict)
                goto out;

        list_for_each_entry (volinfo, &priv->volumes, vol_list) {
                count++;
                ret = glusterd_add_volume_to_dict (volinfo, dict, count);
                if (ret)
                        goto out;
                if (!glusterd_is_volume_quota_enabled (volinfo))
                        continue;
                ret = glusterd_vol_add_quota_conf_to_dict (volinfo, dict, count);
                if (ret)
                        goto out;
        }


        ret = dict_set_int32 (dict, "count", count);
        if (ret)
                goto out;

        ctx.dict = dict;
        ctx.prefix = "global";
        ctx.opt_count = 1;
        ctx.key_name = "key";
        ctx.val_name = "val";
        dict_foreach (priv->opts, _add_dict_to_prdict, &ctx);
        ctx.opt_count--;
        ret = dict_set_int32 (dict, "global-opt-count", ctx.opt_count);
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
glusterd_compare_friend_volume (dict_t *vols, int32_t count, int32_t *status,
                                char *hostname)
{

        int32_t                 ret = -1;
        char                    key[512] = {0,};
        glusterd_volinfo_t      *volinfo = NULL;
        char                    *volname = NULL;
        uint32_t                cksum = 0;
        uint32_t                quota_cksum = 0;
        uint32_t                quota_version = 0;
        int32_t                 version = 0;
        xlator_t                *this = NULL;

        GF_ASSERT (vols);
        GF_ASSERT (status);

        this = THIS;
        GF_ASSERT (this);

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
                gf_log (this->name, GF_LOG_ERROR, "Version of volume %s differ."
                        "local version = %d, remote version = %d on peer %s",
                        volinfo->volname, volinfo->version, version, hostname);
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
                gf_log (this->name, GF_LOG_ERROR, "Cksums of volume %s differ."
                        " local cksum = %u, remote cksum = %u on peer %s",
                        volinfo->volname, volinfo->cksum, cksum, hostname);
                *status = GLUSTERD_VOL_COMP_RJT;
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.quota-version", count);
        ret = dict_get_uint32 (vols, key, &quota_version);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG, "quota-version key absent for"
                        " volume %s in peer %s's response", volinfo->volname,
                        hostname);
                ret = 0;
        } else {
                if (quota_version > volinfo->quota_conf_version) {
                        //Mismatch detected
                        ret = 0;
                        gf_log (this->name, GF_LOG_ERROR, "Quota configuration "
                                "versions of volume %s differ. "
                                "local version = %d, remote version = %d "
                                "on peer %s", volinfo->volname,
                                volinfo->quota_conf_version, quota_version,
                                hostname);
                        *status = GLUSTERD_VOL_COMP_UPDATE_REQ;
                        goto out;
                } else if (quota_version < volinfo->quota_conf_version) {
                        *status = GLUSTERD_VOL_COMP_SCS;
                        goto out;
                }
        }

        //Now, versions are same, compare cksums.
        //
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.quota-cksum", count);
        ret = dict_get_uint32 (vols, key, &quota_cksum);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG, "quota checksum absent for "
                        "volume %s in peer %s's response", volinfo->volname,
                        hostname);
                ret = 0;
        } else {
                if (quota_cksum != volinfo->quota_conf_cksum) {
                        ret = 0;
                        gf_log (this->name, GF_LOG_ERROR, "Cksums of quota "
                                "configurations of volume %s differ. "
                                "local cksum = %u, remote cksum = %u on "
                                "peer %s", volinfo->volname,
                                volinfo->quota_conf_cksum, quota_cksum,
                                hostname);
                        *status = GLUSTERD_VOL_COMP_RJT;
                        goto out;
                }
        }
        *status = GLUSTERD_VOL_COMP_SCS;

out:
        gf_log (this->name, GF_LOG_DEBUG, "Returning with ret: %d, status: %d",
                ret, *status);
        return ret;
}

static int32_t
import_prdict_dict (dict_t *vols, dict_t  *dst_dict, char *key_prefix,
                    char *value_prefix, int opt_count, char *prefix)
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
                snprintf (key, sizeof (key), "%s.%s%d",
                          prefix, key_prefix, i);
                ret = dict_get_str (vols, key, &opt_key);
                if (ret) {
                        snprintf (msg, sizeof (msg), "Volume dict key not "
                                  "specified");
                        goto out;
                }

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.%s%d",
                          prefix, value_prefix, i);
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

gf_boolean_t
glusterd_is_quorum_option (char *option)
{
        gf_boolean_t    res = _gf_false;
        int             i = 0;
        char            *keys[] = {GLUSTERD_QUORUM_TYPE_KEY,
                                   GLUSTERD_QUORUM_RATIO_KEY, NULL};

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
        int             ret = 0;
        gf_boolean_t    reconfigured = _gf_false;
        gf_boolean_t    all = _gf_false;
        char            *oldquorum = NULL;
        char            *newquorum = NULL;
        char            *oldratio = NULL;
        char            *newratio = NULL;

        if ((strcmp ("all", option) != 0) &&
            !glusterd_is_quorum_option (option))
                goto out;

        if (strcmp ("all", option) == 0)
                all = _gf_true;

        if (all || (strcmp (GLUSTERD_QUORUM_TYPE_KEY, option) == 0)) {
                newquorum = value;
                ret = dict_get_str (options, GLUSTERD_QUORUM_TYPE_KEY,
                                    &oldquorum);
        }

        if (all || (strcmp (GLUSTERD_QUORUM_RATIO_KEY, option) == 0)) {
                newratio = value;
                ret = dict_get_str (options, GLUSTERD_QUORUM_RATIO_KEY,
                                    &oldratio);
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

static inline gf_boolean_t
_is_contributing_to_quorum (gd_quorum_contrib_t contrib)
{
        if ((contrib == QUORUM_UP) || (contrib == QUORUM_DOWN))
                return _gf_true;
        return _gf_false;
}

static inline gf_boolean_t
_does_quorum_meet (int active_count, int quorum_count)
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
        //Start with counting self
        inquorum_count = 1;
        if (active_count)
                *active_count = 1;
        list_for_each_entry (peerinfo, &conf->peers, uuid_list) {
                if (peerinfo->quorum_contrib == QUORUM_WAITING)
                        goto out;

                if (_is_contributing_to_quorum (peerinfo->quorum_contrib))
                        inquorum_count = inquorum_count + 1;

                if (active_count && (peerinfo->quorum_contrib == QUORUM_UP))
                        *active_count = *active_count + 1;
        }

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
out:
        return ret;
}

gf_boolean_t
glusterd_is_volume_in_server_quorum (glusterd_volinfo_t *volinfo)
{
        gf_boolean_t    res = _gf_false;
        char            *quorum_type = NULL;
        int             ret = 0;

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
        glusterd_conf_t         *conf = NULL;
        glusterd_volinfo_t      *volinfo = NULL;

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
        int                     quorum_count = 0;
        int                     active_count   = 0;
        gf_boolean_t            in = _gf_false;
        glusterd_conf_t         *conf = NULL;
        int                     ret = -1;

        conf = this->private;
        ret = glusterd_get_quorum_cluster_counts (this, &active_count,
                                                  &quorum_count);
        if (ret)
                goto out;

        if (!_does_quorum_meet (active_count, quorum_count)) {
                goto out;
        }

        in = _gf_true;
out:
        return in;
}

int
glusterd_spawn_daemons (void *opaque)
{
        glusterd_conf_t *conf = THIS->private;
        gf_boolean_t    start_bricks = !conf->restart_done;

        if (start_bricks) {
                glusterd_restart_bricks (conf);
                conf->restart_done = _gf_true;
        }
        glusterd_restart_gsyncds (conf);
        glusterd_restart_rebalance (conf);
        return 0;
}

void
glusterd_do_volume_quorum_action (xlator_t *this, glusterd_volinfo_t *volinfo,
                                  gf_boolean_t meets_quorum)
{
        glusterd_brickinfo_t    *brickinfo = NULL;
        glusterd_conf_t         *conf = NULL;

        conf = this->private;
        if (volinfo->status != GLUSTERD_STATUS_STARTED)
                goto out;

        if (!glusterd_is_volume_in_server_quorum (volinfo))
                meets_quorum = _gf_true;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (!glusterd_is_local_brick (this, volinfo, brickinfo))
                        continue;
                if (meets_quorum)
                        glusterd_brick_start (volinfo, brickinfo, _gf_false);
                else
                        glusterd_brick_stop (volinfo, brickinfo, _gf_false);
        }
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

                if (_does_quorum_meet (active_count, quorum_count))
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

int32_t
glusterd_import_friend_volume_opts (dict_t *vols, int count,
                                    glusterd_volinfo_t *volinfo)
{
        char                    key[512] = {0,};
        int32_t                 ret = -1;
        int                     opt_count = 0;
        char                    msg[2048] = {0};
        char                    volume_prefix[1024] = {0};

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.opt-count", count);
        ret = dict_get_int32 (vols, key, &opt_count);
        if (ret) {
                snprintf (msg, sizeof (msg), "Volume option count not "
                          "specified for %s", volinfo->volname);
                goto out;
        }

        snprintf (volume_prefix, sizeof (volume_prefix), "volume%d", count);
        ret = import_prdict_dict (vols, volinfo->dict, "key", "value",
                                  opt_count, volume_prefix);
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

        ret = import_prdict_dict (vols, volinfo->gsync_slaves, "slave-num",
                                  "slave-val", opt_count, volume_prefix);
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
        int                     decommissioned = 0;
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

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.brick%d.decommissioned",
                  vol_count, brick_count);
        ret = dict_get_int32 (vols, key, &decommissioned);
        if (ret) {
                /* For backward compatibility */
                ret = 0;
        }

        ret = glusterd_brickinfo_new (&new_brickinfo);
        if (ret)
                goto out;

        strcpy (new_brickinfo->path, path);
        strcpy (new_brickinfo->hostname, hostname);
        new_brickinfo->decommissioned = decommissioned;
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

static int
glusterd_import_quota_conf (dict_t *vols, int vol_idx,
                            glusterd_volinfo_t *new_volinfo)
{
        int     gfid_idx         = 0;
        int     gfid_count       = 0;
        int     ret              = -1;
        int     fd               = -1;
        char    key[PATH_MAX]    = {0};
        char    *gfid_str        = NULL;
        uuid_t   gfid            = {0,};
        xlator_t *this           = NULL;

        this = THIS;
        GF_ASSERT (this);

        if (!glusterd_is_volume_quota_enabled (new_volinfo)) {
                (void) glusterd_clean_up_quota_store (new_volinfo);
                return 0;
        }

        ret = glusterd_store_create_quota_conf_sh_on_absence (new_volinfo);
        if (ret)
                goto out;

        fd = gf_store_mkstemp (new_volinfo->quota_conf_shandle);
        if (fd < 0) {
                ret = -1;
                goto out;
        }

        snprintf (key, sizeof (key)-1, "volume%d.quota-cksum", vol_idx);
        key[sizeof(key)-1] = '\0';
        ret = dict_get_uint32 (vols, key, &new_volinfo->quota_conf_cksum);
        if (ret)
                gf_log (this->name, GF_LOG_DEBUG, "Failed to get quota cksum");

        snprintf (key, sizeof (key)-1, "volume%d.quota-version", vol_idx);
        key[sizeof(key)-1] = '\0';
        ret = dict_get_uint32 (vols, key, &new_volinfo->quota_conf_version);
        if (ret)
                gf_log (this->name, GF_LOG_DEBUG, "Failed to get quota "
                                                  "version");

        snprintf (key, sizeof (key)-1, "volume%d.gfid-count", vol_idx);
        key[sizeof(key)-1] = '\0';
        ret = dict_get_int32 (vols, key, &gfid_count);
        if (ret)
                goto out;

        ret = glusterd_store_quota_conf_stamp_header (this, fd);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to add header to tmp "
                        "file");
                goto out;
        }

        gfid_idx = 0;
        for (gfid_idx = 0; gfid_idx < gfid_count; gfid_idx++) {

                snprintf (key, sizeof (key)-1, "volume%d.gfid%d",
                          vol_idx, gfid_idx);
                key[sizeof(key)-1] = '\0';
                ret = dict_get_str (vols, key, &gfid_str);
                if (ret)
                        goto out;

                uuid_parse (gfid_str, gfid);
                ret = write (fd, (void*)gfid, 16);
                if (ret != 16) {
                        gf_log (this->name, GF_LOG_CRITICAL, "Unable to write "
                                "gfid %s into quota.conf for %s", gfid_str,
                                new_volinfo->volname);
                        ret = -1;
                        goto out;
                }

        }

        ret = gf_store_rename_tmppath (new_volinfo->quota_conf_shandle);

        ret = 0;

out:
        if (fd != -1)
                close (fd);

        if (!ret) {
                ret = glusterd_compute_cksum (new_volinfo, _gf_true);
                if (ret)
                        goto out;

                ret = glusterd_store_save_quota_version_and_cksum (new_volinfo);
                if (ret)
                        goto out;
        }

        if (ret && (fd > 0)) {
                gf_store_unlink_tmppath (new_volinfo->quota_conf_shandle);
                (void) gf_store_handle_destroy
                                              (new_volinfo->quota_conf_shandle);
                new_volinfo->quota_conf_shandle = NULL;
        }

        return ret;
}

int
gd_import_friend_volume_rebal_dict (dict_t *dict, int count,
                                    glusterd_volinfo_t *volinfo)
{
        int  ret        = -1;
        char key[256]   = {0,};
        int  dict_count  = 0;
        char prefix[64] = {0};

        GF_ASSERT (dict);
        GF_ASSERT (volinfo);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.rebal-dict-count", count);
        ret = dict_get_int32 (dict, key, &dict_count);
        if (ret) {
                /* Older peers will not have this dict */
                ret = 0;
                goto out;
        }

        volinfo->rebal.dict = dict_new ();
        if(!volinfo->rebal.dict) {
                ret = -1;
                goto out;
        }

        snprintf (prefix, sizeof (prefix), "volume%d", count);
        ret = import_prdict_dict (dict, volinfo->rebal.dict, "rebal-dict-key",
                                  "rebal-dict-value", dict_count, prefix);
out:
        if (ret && volinfo->rebal.dict)
                dict_unref (volinfo->rebal.dict);
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning with %d", ret);
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
        char               *str              = NULL;
        int                rb_status         = 0;
        char               *rebalance_id_str = NULL;
        char               *rb_id_str        = NULL;
        int                op_version        = 0;
        int                client_op_version = 0;

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
        if (ret)
                gf_log (THIS->name, GF_LOG_INFO,
                        "peer is possibly old version");

        /* not having a 'replica_count' key is not a error
           (as peer may be of old version) */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.replica_count", count);
        ret = dict_get_int32 (vols, key, &new_volinfo->replica_count);
        if (ret)
                gf_log (THIS->name, GF_LOG_INFO,
                        "peer is possibly old version");

        /* not having a 'dist_count' key is not a error
           (as peer may be of old version) */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.dist_count", count);
        ret = dict_get_int32 (vols, key, &new_volinfo->dist_leaf_count);
        if (ret)
                gf_log (THIS->name, GF_LOG_INFO,
                        "peer is possibly old version");
        new_volinfo->subvol_count = new_volinfo->brick_count/
                                    glusterd_get_dist_leaf_count (new_volinfo);
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

        uuid_parse (volume_id_str, new_volinfo->volume_id);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.username", count);
        ret = dict_get_str (vols, key, &str);
        if (!ret) {
                ret = glusterd_auth_set_username (new_volinfo, str);
                if (ret)
                        goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.password", count);
        ret = dict_get_str (vols, key, &str);
        if (!ret) {
                ret = glusterd_auth_set_password (new_volinfo, str);
                if (ret)
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

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.rebalance", count);
        ret = dict_get_uint32 (vols, key, &new_volinfo->rebal.defrag_cmd);
        if (ret) {
                snprintf (msg, sizeof (msg), "%s missing in payload for %s",
                          key, volname);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.rebalance-id", count);
        ret = dict_get_str (vols, key, &rebalance_id_str);
        if (ret) {
                /* This is not present in older glusterfs versions,
                 * so don't error out
                 */
                ret = 0;
        } else {
                uuid_parse (rebalance_id_str, new_volinfo->rebal.rebalance_id);
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.rebalance-op", count);
        ret = dict_get_uint32 (vols, key,(uint32_t *) &new_volinfo->rebal.op);
        if (ret) {
                /* This is not present in older glusterfs versions,
                 * so don't error out
                 */
                ret = 0;
        }
        ret = gd_import_friend_volume_rebal_dict (vols, count, new_volinfo);
        if (ret) {
                snprintf (msg, sizeof (msg), "Failed to import rebalance dict "
                          "for volume.");
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, 256, "volume%d."GLUSTERD_STORE_KEY_RB_STATUS, count);
        ret = dict_get_int32 (vols, key, &rb_status);
        if (ret)
                goto out;
        new_volinfo->rep_brick.rb_status = rb_status;

        if (new_volinfo->rep_brick.rb_status > GF_RB_STATUS_NONE) {

                memset (key, 0, sizeof (key));
                snprintf (key, 256, "volume%d."GLUSTERD_STORE_KEY_RB_SRC_BRICK,
                          count);
                ret = dict_get_str (vols, key, &src_brick);
                if (ret)
                        goto out;

                ret = glusterd_brickinfo_new_from_brick (src_brick,
                                        &new_volinfo->rep_brick.src_brick);
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

                ret = glusterd_brickinfo_new_from_brick (dst_brick,
                                     &new_volinfo->rep_brick.dst_brick);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Unable to create"
                                " dst brickinfo");
                        goto out;
                }

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d.rb_id", count);
                ret = dict_get_str (vols, key, &rb_id_str);
                if (ret) {
                        /* This is not present in older glusterfs versions,
                         * so don't error out
                         */
                        ret = 0;
                } else {
                        uuid_parse (rb_id_str, new_volinfo->rep_brick.rb_id);
                }
        }


        ret = glusterd_import_friend_volume_opts (vols, count, new_volinfo);
        if (ret)
                goto out;

        /* Import the volume's op-versions if available else set it to 1.
         * Not having op-versions implies this informtation was obtained from a
         * op-version 1 friend (gluster-3.3), ergo the cluster is at op-version
         * 1 and all volumes are at op-versions 1.
         *
         * Either both the volume op-versions should be absent or both should be
         * present. Only one being present is a failure
         */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.op-version", count);
        ret = dict_get_int32 (vols, key, &op_version);
        if (ret)
                ret = 0;
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.client-op-version", count);
        ret = dict_get_int32 (vols, key, &client_op_version);
        if (ret)
                ret = 0;

        if (op_version && client_op_version) {
                new_volinfo->op_version = op_version;
                new_volinfo->client_op_version = client_op_version;
        } else if (((op_version == 0) && (client_op_version != 0)) ||
                   ((op_version != 0) && (client_op_version == 0))) {
                ret = -1;
                gf_log ("glusterd", GF_LOG_ERROR,
                        "Only one volume op-version found");
                goto out;
        } else {
                new_volinfo->op_version = 1;
                new_volinfo->client_op_version = 1;
        }

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
                        /*TODO: may need to switch to 'atomic' flavour of
                         * brick_stop, once we make peer rpc program also
                         * synctask enabled*/
                        ret = glusterd_brick_stop (old_volinfo, old_brickinfo,
                                                   _gf_false);
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
         * Disconnect all bricks from stale_volinfo (unconditionally), since
         * they are being deleted subsequently.
         */
        if (glusterd_is_volume_started (stale_volinfo)) {
                if (glusterd_is_volume_started (valid_volinfo)) {
                        (void) glusterd_volinfo_stop_stale_bricks (valid_volinfo,
                                                                   stale_volinfo);
                        //Only valid bricks will be running now.
                        (void) glusterd_volinfo_copy_brick_portinfo (valid_volinfo,
                                                                     stale_volinfo);

                } else {
                        (void) glusterd_stop_bricks (stale_volinfo);
                }

                (void) glusterd_volume_disconnect_all_bricks (stale_volinfo);
        }
        /* Delete all the bricks and stores and vol files. They will be created
         * again by the valid_volinfo. Volume store delete should not be
         * performed because some of the bricks could still be running,
         * keeping pid files under run directory
         */
        (void) glusterd_delete_all_bricks (stale_volinfo);
        if (stale_volinfo->shandle) {
                unlink (stale_volinfo->shandle->path);
                (void) gf_store_handle_destroy (stale_volinfo->shandle);
                stale_volinfo->shandle = NULL;
        }
        (void) glusterd_volinfo_remove (stale_volinfo);
        return 0;
}

/* This function updates the rebalance information of the new volinfo using the
 * information from the old volinfo.
 */
int
gd_check_and_update_rebalance_info (glusterd_volinfo_t *old_volinfo,
                                    glusterd_volinfo_t *new_volinfo)
{
        int                  ret  = -1;
        glusterd_rebalance_t *old = NULL;
        glusterd_rebalance_t *new = NULL;

        GF_ASSERT (old_volinfo);
        GF_ASSERT (new_volinfo);

        old = &(old_volinfo->rebal);
        new = &(new_volinfo->rebal);

        //Disconnect from rebalance process
        if (old->defrag && old->defrag->rpc) {
                rpc_transport_disconnect (old->defrag->rpc->conn.trans);
        }

        if (!uuid_is_null (old->rebalance_id) &&
            uuid_compare (old->rebalance_id, new->rebalance_id)) {
                (void)gd_stop_rebalance_process (old_volinfo);
                goto out;
        }

        /* If the tasks match, copy the status and other information of the
         * rebalance process from old_volinfo to new_volinfo
         */
        new->defrag_status      = old->defrag_status;
        new->rebalance_files    = old->rebalance_files;
        new->rebalance_data     = old->rebalance_data;
        new->lookedup_files     = old->lookedup_files;
        new->skipped_files      = old->skipped_files;
        new->rebalance_failures = old->rebalance_failures;
        new->rebalance_time     = old->rebalance_time;
        new->dict               = (old->dict ? dict_ref (old->dict) : NULL);

        /* glusterd_rebalance_t.{op, id, defrag_cmd} are copied during volume
         * import
         * a new defrag object should come to life with rebalance being restarted
         */
out:
        return ret;
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
                (void) gd_check_and_update_rebalance_info (old_volinfo,
                                                           new_volinfo);
                (void) glusterd_delete_stale_volume (old_volinfo, new_volinfo);
        }

        if (glusterd_is_volume_started (new_volinfo)) {
                (void) glusterd_start_bricks (new_volinfo);
        }

        ret = glusterd_store_volinfo (new_volinfo, GLUSTERD_VOLINFO_VER_AC_NONE);
        ret = glusterd_create_volfiles_and_notify_services (new_volinfo);
        if (ret)
                goto out;

        ret = glusterd_import_quota_conf (vols, count, new_volinfo);
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

int
glusterd_get_global_opt_version (dict_t *opts, uint32_t *version)
{
        int     ret = -1;
        char    *version_str = NULL;

        ret = dict_get_str (opts, GLUSTERD_GLOBAL_OPT_VERSION, &version_str);
        if (ret)
                goto out;

        ret = gf_string2uint (version_str, version);
        if (ret)
                goto out;
        ret = 0;
out:
        return ret;
}

int
glusterd_get_next_global_opt_version_str (dict_t *opts, char **version_str)
{
        int             ret = -1;
        char            version_string[64] = {0};
        uint32_t        version = 0;

        ret = glusterd_get_global_opt_version (opts, &version);
        if (ret)
                goto out;
        version++;
        snprintf (version_string, sizeof (version_string), "%"PRIu32, version);
        *version_str = gf_strdup (version_string);
        if (*version_str)
                ret = 0;
out:
        return ret;
}

int32_t
glusterd_import_global_opts (dict_t *friend_data)
{
        xlator_t        *this = NULL;
        glusterd_conf_t *conf = NULL;
        int             ret = -1;
        dict_t          *import_options = NULL;
        int             count = 0;
        uint32_t        local_version = 0;
        uint32_t        remote_version = 0;

        this = THIS;
        conf = this->private;

        ret = dict_get_int32 (friend_data, "global-opt-count", &count);
        if (ret) {
                //old version peer
                ret = 0;
                goto out;
        }

        import_options = dict_new ();
        if (!import_options)
                goto out;
        ret = import_prdict_dict (friend_data, import_options, "key", "val",
                                  count, "global");
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to import"
                        " global options");
                goto out;
        }

        ret = glusterd_get_global_opt_version (conf->opts, &local_version);
        if (ret)
                goto out;
        ret = glusterd_get_global_opt_version (import_options, &remote_version);
        if (ret)
                goto out;
        if (remote_version > local_version) {
                ret = glusterd_store_options (this, import_options);
                if (ret)
                        goto out;
                dict_unref (conf->opts);
                conf->opts = dict_ref (import_options);
        }
        ret = 0;
out:
        if (import_options)
                dict_unref (import_options);
        return ret;
}

int32_t
glusterd_compare_friend_data (dict_t  *vols, int32_t *status, char *hostname)
{
        int32_t                 ret = -1;
        int32_t                 count = 0;
        int                     i = 1;
        gf_boolean_t            update = _gf_false;
        gf_boolean_t            stale_nfs = _gf_false;
        gf_boolean_t            stale_shd = _gf_false;
        gf_boolean_t            stale_qd  = _gf_false;

        GF_ASSERT (vols);
        GF_ASSERT (status);

        ret = dict_get_int32 (vols, "count", &count);
        if (ret)
                goto out;

        while (i <= count) {
                ret = glusterd_compare_friend_volume (vols, i, status,
                                                      hostname);
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
                if (glusterd_is_nodesvc_running ("quotad"))
                        stale_qd  = _gf_true;
                ret = glusterd_import_global_opts (vols);
                if (ret)
                        goto out;
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
                        if (stale_qd)
                                glusterd_quotad_stop ();
                }
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with ret: %d, status: %d",
                ret, *status);

        return ret;
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
        if (strcmp ("quotad", server) != 0)
                snprintf (volfile, len, "%s/%s-server.vol", dir, server);
        else
                snprintf (volfile, len, "%s/%s.vol", dir, server);
}

void
glusterd_nodesvc_set_online_status (char *server, gf_boolean_t status)
{
        glusterd_conf_t *priv = NULL;

        GF_ASSERT (server);
        priv = THIS->private;
        GF_ASSERT (priv);
        GF_ASSERT (priv->shd);
        GF_ASSERT (priv->nfs);
        GF_ASSERT (priv->quotad);

        if (!strcmp("glustershd", server))
                priv->shd->online = status;
        else if (!strcmp ("nfs", server))
                priv->nfs->online = status;
        else if (!strcmp ("quotad", server))
                priv->quotad->online = status;
}

gf_boolean_t
glusterd_is_nodesvc_online (char *server)
{
        glusterd_conf_t *conf = NULL;
        gf_boolean_t    online = _gf_false;

        GF_ASSERT (server);
        conf = THIS->private;
        GF_ASSERT (conf);
        GF_ASSERT (conf->shd);
        GF_ASSERT (conf->nfs);
        GF_ASSERT (conf->quotad);

        if (!strcmp (server, "glustershd"))
                online = conf->shd->online;
        else if (!strcmp (server, "nfs"))
                online = conf->nfs->online;
        else if (!strcmp (server, "quotad"))
                online = conf->quotad->online;

        return online;
}

int32_t
glusterd_nodesvc_set_socket_filepath (char *rundir, uuid_t uuid,
                                      char *socketpath, int len)
{
        char                    sockfilepath[PATH_MAX] = {0,};

        snprintf (sockfilepath, sizeof (sockfilepath), "%s/run-%s",
                  rundir, uuid_utoa (uuid));

        glusterd_set_socket_filepath (sockfilepath, socketpath, len);
        return 0;
}

struct rpc_clnt*
glusterd_pending_node_get_rpc (glusterd_pending_node_t *pending_node)
{
        struct rpc_clnt *rpc = NULL;
        glusterd_brickinfo_t    *brickinfo = NULL;
        nodesrv_t               *shd       = NULL;
        glusterd_volinfo_t      *volinfo   = NULL;
        nodesrv_t               *nfs       = NULL;
        nodesrv_t               *quotad    = NULL;

        GF_VALIDATE_OR_GOTO (THIS->name, pending_node, out);
        GF_VALIDATE_OR_GOTO (THIS->name, pending_node->node, out);

        if (pending_node->type == GD_NODE_BRICK) {
                brickinfo = pending_node->node;
                rpc       = brickinfo->rpc;

        } else if (pending_node->type == GD_NODE_SHD) {
                shd       = pending_node->node;
                rpc       = shd->rpc;

        } else if (pending_node->type == GD_NODE_REBALANCE) {
                volinfo = pending_node->node;
                if (volinfo->rebal.defrag)
                        rpc = volinfo->rebal.defrag->rpc;

        } else if (pending_node->type == GD_NODE_NFS) {
                nfs = pending_node->node;
                rpc = nfs->rpc;

        } else if (pending_node->type == GD_NODE_QUOTAD) {
                quotad = pending_node->node;
                rpc = quotad->rpc;

        } else {
                GF_ASSERT (0);
        }

out:
        return rpc;
}

struct rpc_clnt*
glusterd_nodesvc_get_rpc (char *server)
{
        glusterd_conf_t *priv   = NULL;
        struct rpc_clnt *rpc    = NULL;

        GF_ASSERT (server);
        priv = THIS->private;
        GF_ASSERT (priv);
        GF_ASSERT (priv->shd);
        GF_ASSERT (priv->nfs);
        GF_ASSERT (priv->quotad);

        if (!strcmp (server, "glustershd"))
                rpc = priv->shd->rpc;
        else if (!strcmp (server, "nfs"))
                rpc = priv->nfs->rpc;
        else if (!strcmp (server, "quotad"))
                rpc = priv->quotad->rpc;

        return rpc;
}

int32_t
glusterd_nodesvc_set_rpc (char *server, struct rpc_clnt *rpc)
{
        int             ret   = 0;
        xlator_t        *this = NULL;
        glusterd_conf_t *priv = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (priv->shd);
        GF_ASSERT (priv->nfs);
        GF_ASSERT (priv->quotad);

        if (!strcmp ("glustershd", server))
                priv->shd->rpc = rpc;
        else if (!strcmp ("nfs", server))
                priv->nfs->rpc = rpc;
        else if (!strcmp ("quotad", server))
                priv->quotad->rpc = rpc;

        return ret;
}

int32_t
glusterd_nodesvc_connect (char *server, char *socketpath) {
        int                     ret = 0;
        dict_t                  *options = NULL;
        struct rpc_clnt         *rpc = NULL;
        glusterd_conf_t         *priv = THIS->private;

        rpc = glusterd_nodesvc_get_rpc (server);

        if (rpc == NULL) {
                /* Setting frame-timeout to 10mins (600seconds).
                 * Unix domain sockets ensures that the connection is reliable.
                 * The default timeout of 30mins used for unreliable network
                 * connections is too long for unix domain socket connections.
                 */
                ret = rpc_transport_unix_options_build (&options, socketpath,
                                                        600);
                if (ret)
                        goto out;
                synclock_unlock (&priv->big_lock);
                ret = glusterd_rpc_create (&rpc, options,
                                           glusterd_nodesvc_rpc_notify,
                                           server);
                synclock_lock (&priv->big_lock);
                if (ret)
                        goto out;
                (void) glusterd_nodesvc_set_rpc (server, rpc);
        }
out:
        return ret;
}

int32_t
glusterd_nodesvc_disconnect (char *server)
{
        struct rpc_clnt         *rpc = NULL;
        glusterd_conf_t         *priv = THIS->private;

        rpc = glusterd_nodesvc_get_rpc (server);
        (void)glusterd_nodesvc_set_rpc (server, NULL);

        if (rpc)
                glusterd_rpc_clnt_unref (priv, rpc);

        return 0;
}

int32_t
glusterd_nodesvc_start (char *server, gf_boolean_t wait)
{
        int32_t                 ret                        = -1;
        xlator_t               *this                       = NULL;
        glusterd_conf_t        *priv                       = NULL;
        runner_t                runner                     = {0,};
        char                    pidfile[PATH_MAX]          = {0,};
        char                    logfile[PATH_MAX]          = {0,};
        char                    volfile[PATH_MAX]          = {0,};
        char                    rundir[PATH_MAX]           = {0,};
        char                    sockfpath[PATH_MAX] = {0,};
        char                    volfileid[256]             = {0};
        char                    glusterd_uuid_option[1024] = {0};
        char                    valgrind_logfile[PATH_MAX] = {0};

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

        glusterd_nodesvc_set_socket_filepath (rundir, MY_UUID,
                                              sockfpath, sizeof (sockfpath));

        runinit (&runner);

        if (priv->valgrind) {
                snprintf (valgrind_logfile, PATH_MAX,
                          "%s/valgrind-%s.log",
                          DEFAULT_LOG_FILE_DIRECTORY,
                          server);

                runner_add_args (&runner, "valgrind", "--leak-check=full",
                                 "--trace-children=yes", "--track-origins=yes",
                                 NULL);
                runner_argprintf (&runner, "--log-file=%s", valgrind_logfile);
        }

        runner_add_args (&runner, SBIN_DIR"/glusterfs",
                         "-s", "localhost",
                         "--volfile-id", volfileid,
                         "-p", pidfile,
                         "-l", logfile,
                         "-S", sockfpath, NULL);

        if (!strcmp (server, "glustershd")) {
                snprintf (glusterd_uuid_option, sizeof (glusterd_uuid_option),
                          "*replicate*.node-uuid=%s", uuid_utoa (MY_UUID));
                runner_add_args (&runner, "--xlator-option",
                                 glusterd_uuid_option, NULL);
        }
        if (!strcmp (server, "quotad")) {
                runner_add_args (&runner, "--xlator-option",
                                 "*replicate*.data-self-heal=off",
                                 "--xlator-option",
                                 "*replicate*.metadata-self-heal=off",
                                 "--xlator-option",
                                 "*replicate*.entry-self-heal=off", NULL);
        }
        runner_log (&runner, "", GF_LOG_DEBUG,
                    "Starting the nfs/glustershd services");

        if (!wait) {
                ret = runner_run_nowait (&runner);
        } else {
                synclock_unlock (&priv->big_lock);
                {
                        ret = runner_run (&runner);
                }
                synclock_lock (&priv->big_lock);
        }

        if (ret == 0) {
                glusterd_nodesvc_connect (server, sockfpath);
        }
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
        return glusterd_nodesvc_start ("glustershd", _gf_false);
}

int
glusterd_quotad_start ()
{
        return glusterd_nodesvc_start ("quotad", _gf_true);
}

gf_boolean_t
glusterd_is_nodesvc_running (char *server)
{
        char                    pidfile[PATH_MAX] = {0,};
        glusterd_conf_t         *priv = THIS->private;

        glusterd_get_nodesvc_pidfile (server, priv->workdir,
                                            pidfile, sizeof (pidfile));
        return gf_is_service_running (pidfile, NULL);
}

int32_t
glusterd_nodesvc_unlink_socket_file (char *server)
{
        int             ret = 0;
        char            sockfpath[PATH_MAX] = {0,};
        char            rundir[PATH_MAX] = {0,};
        glusterd_conf_t *priv = THIS->private;

        glusterd_get_nodesvc_rundir (server, priv->workdir,
                                     rundir, sizeof (rundir));

        glusterd_nodesvc_set_socket_filepath (rundir, MY_UUID,
                                              sockfpath, sizeof (sockfpath));

        ret = unlink (sockfpath);
        if (ret && (ENOENT == errno)) {
                ret = 0;
        } else {
                gf_log (THIS->name, GF_LOG_ERROR, "Failed to remove %s"
                        " error: %s", sockfpath, strerror (errno));
        }

        return ret;
}

int32_t
glusterd_nodesvc_stop (char *server, int sig)
{
        char                    pidfile[PATH_MAX] = {0,};
        glusterd_conf_t         *priv = THIS->private;
        int                     ret = 0;

        if (!glusterd_is_nodesvc_running (server))
                goto out;

        (void)glusterd_nodesvc_disconnect (server);

        glusterd_get_nodesvc_pidfile (server, priv->workdir,
                                            pidfile, sizeof (pidfile));
        ret = glusterd_service_stop (server, pidfile, sig, _gf_true);

        if (ret == 0) {
                glusterd_nodesvc_set_online_status (server, _gf_false);
                (void)glusterd_nodesvc_unlink_socket_file (server);
        }
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

        if (pmap_unset (NLM_PROGRAM, NLMV4_VERSION))
                gf_log ("", GF_LOG_INFO, "De-registered NLM v4 successfully");
        else
                gf_log ("", GF_LOG_ERROR, "De-registration of NLM v4 failed");

        if (pmap_unset (NLM_PROGRAM, NLMV1_VERSION))
                gf_log ("", GF_LOG_INFO, "De-registered NLM v1 successfully");
        else
                gf_log ("", GF_LOG_ERROR, "De-registration of NLM v1 failed");

        if (pmap_unset (ACL_PROGRAM, ACLV3_VERSION))
                gf_log ("", GF_LOG_INFO, "De-registered ACL v3 successfully");
        else
                gf_log ("", GF_LOG_ERROR, "De-registration of ACL v3 failed");
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
glusterd_quotad_stop ()
{
        return glusterd_nodesvc_stop ("quotad", SIGTERM);
}

int
glusterd_add_node_to_dict (char *server, dict_t *dict, int count,
                           dict_t *vol_opts)
{
        int                     ret = -1;
        glusterd_conf_t         *priv = THIS->private;
        char                    pidfile[PATH_MAX] = {0,};
        gf_boolean_t            running = _gf_false;
        int                     pid = -1;
        int                     port = 0;
        char                    key[1024] = {0,};

        glusterd_get_nodesvc_pidfile (server, priv->workdir, pidfile,
                                      sizeof (pidfile));
        //Consider service to be running only when glusterd sees it Online
        if (glusterd_is_nodesvc_online (server))
                running = gf_is_service_running (pidfile, &pid);

        /* For nfs-servers/self-heal-daemon setting
         * brick<n>.hostname = "NFS Server" / "Self-heal Daemon"
         * brick<n>.path = uuid
         * brick<n>.port = 0
         *
         * This might be confusing, but cli displays the name of
         * the brick as hostname+path, so this will make more sense
         * when output.
         */
        snprintf (key, sizeof (key), "brick%d.hostname", count);
        if (!strcmp (server, "nfs"))
                ret = dict_set_str (dict, key, "NFS Server");
        else if (!strcmp (server, "glustershd"))
                ret = dict_set_str (dict, key, "Self-heal Daemon");
        else if (!strcmp (server, "quotad"))
                ret = dict_set_str (dict, key, "Quota Daemon");
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.path", count);
        ret = dict_set_dynstr (dict, key, gf_strdup (uuid_utoa (MY_UUID)));
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.port", count);
        /* Port is available only for the NFS server.
         * Self-heal daemon doesn't provide any port for access
         * by entities other than gluster.
         */
        if (!strcmp (server, "nfs")) {
                if (dict_get (vol_opts, "nfs.port")) {
                        ret = dict_get_int32 (vol_opts, "nfs.port", &port);
                        if (ret)
                                goto out;
                } else
                        port = GF_NFS3_PORT;
        }
        ret = dict_set_int32 (dict, key, port);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.pid", count);
        ret = dict_set_int32 (dict, key, pid);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.status", count);
        ret = dict_set_int32 (dict, key, running);
        if (ret)
                goto out;


out:
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
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
glusterd_reconfigure_quotad ()
{
        return glusterd_reconfigure_nodesvc (glusterd_create_quotad_volfile);
}

int
glusterd_reconfigure_nfs ()
{
        int             ret             = -1;
        gf_boolean_t    identical       = _gf_false;

        /*
         * Check both OLD and NEW volfiles, if they are SAME by size
         * and cksum i.e. "character-by-character". If YES, then
         * NOTHING has been changed, just return.
         */
        ret = glusterd_check_nfs_volfile_identical (&identical);
        if (ret)
                goto out;

        if (identical) {
                ret = 0;
                goto out;
        }

        /*
         * They are not identical. Find out if the topology is changed
         * OR just the volume options. If just the options which got
         * changed, then inform the xlator to reconfigure the options.
         */
        identical = _gf_false; /* RESET the FLAG */
        ret = glusterd_check_nfs_topology_identical (&identical);
        if (ret)
                goto out;

        /* Topology is not changed, but just the options. But write the
         * options to NFS volfile, so that NFS will be reconfigured.
         */
        if (identical) {
                ret = glusterd_create_nfs_volfile();
                if (ret == 0) {/* Only if above PASSES */
                        ret = glusterd_fetchspec_notify (THIS);
                }
                goto out;
        }

        /*
         * NFS volfile's topology has been changed. NFS server needs
         * to be RESTARTED to ACT on the changed volfile.
         */
        ret = glusterd_check_generate_start_nfs ();

out:
        return ret;
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
glusterd_check_generate_start_quotad ()
{
        int ret = 0;

        ret = glusterd_check_generate_start_service (glusterd_create_quotad_volfile,
                                                     glusterd_quotad_stop,
                                                     glusterd_quotad_start);
        if (ret == -EINVAL)
                ret = 0;
        return ret;
}

int
glusterd_nodesvcs_batch_op (glusterd_volinfo_t *volinfo, int (*nfs_op) (),
                            int (*shd_op) (), int (*qd_op) ())
 {
        int     ret = 0;
        xlator_t *this = THIS;
        glusterd_conf_t *conf = NULL;

        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        ret = nfs_op ();
        if (ret)
                goto out;

        if (volinfo && !glusterd_is_volume_replicate (volinfo)) {
                ; //do nothing
        } else {
                ret = shd_op ();
                if (ret)
                        goto out;
        }

        if (conf->op_version == GD_OP_VERSION_MIN)
                goto out;

        if (volinfo && !glusterd_is_volume_quota_enabled (volinfo))
                goto out;

        ret = qd_op ();
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
                                           glusterd_shd_start,
                                           glusterd_quotad_start);
}

int
glusterd_nodesvcs_stop (glusterd_volinfo_t *volinfo)
{
        return glusterd_nodesvcs_batch_op (volinfo,
                                            glusterd_nfs_server_stop,
                                            glusterd_shd_stop,
                                            glusterd_quotad_stop);
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

gf_boolean_t
glusterd_all_replicate_volumes_stopped ()
{
        glusterd_conf_t                         *priv = NULL;
        xlator_t                                *this = NULL;
        glusterd_volinfo_t                      *voliter = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        list_for_each_entry (voliter, &priv->volumes, vol_list) {
                if (!glusterd_is_volume_replicate (voliter))
                        continue;
                if (voliter->status == GLUSTERD_STATUS_STARTED)
                        return _gf_false;
        }

        return _gf_true;
}

gf_boolean_t
glusterd_all_volumes_with_quota_stopped ()
{
        glusterd_conf_t                   *priv     = NULL;
        xlator_t                          *this     = NULL;
        glusterd_volinfo_t                *voliter  = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        list_for_each_entry (voliter, &priv->volumes, vol_list) {
                if (!glusterd_is_volume_quota_enabled (voliter))
                        continue;
                if (voliter->status == GLUSTERD_STATUS_STARTED)
                        return _gf_false;
        }

        return _gf_true;
}


int
glusterd_nodesvcs_handle_graph_change (glusterd_volinfo_t *volinfo)
{
        int (*shd_op) () = NULL;
        int (*nfs_op) () = NULL;
        int (*qd_op)  () = NULL;

        shd_op = glusterd_check_generate_start_shd;
        nfs_op = glusterd_check_generate_start_nfs;
        qd_op  = glusterd_check_generate_start_quotad;
        if (glusterd_are_all_volumes_stopped ()) {
                shd_op = glusterd_shd_stop;
                nfs_op = glusterd_nfs_server_stop;
                qd_op  = glusterd_quotad_stop;
        } else {
                if (glusterd_all_replicate_volumes_stopped()) {
                        shd_op = glusterd_shd_stop;
                }
                if (glusterd_all_volumes_with_quota_stopped ()) {
                        qd_op = glusterd_quotad_stop;
                }
        }

        return glusterd_nodesvcs_batch_op (volinfo, nfs_op, shd_op, qd_op);
}

int
glusterd_nodesvcs_handle_reconfigure (glusterd_volinfo_t *volinfo)
{
        return glusterd_nodesvcs_batch_op (volinfo,
                                           glusterd_reconfigure_nfs,
                                           glusterd_reconfigure_shd,
                                           glusterd_reconfigure_quotad);
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
                                                     volinfo, brickinfo);
                if (ret == 0)
                        /*Found*/
                        goto out;
        }
out:
        return ret;
}

int
glusterd_brick_start (glusterd_volinfo_t *volinfo,
                      glusterd_brickinfo_t *brickinfo,
                      gf_boolean_t wait)
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
                        gf_log (this->name, GF_LOG_ERROR, FMTSTR_RESOLVE_BRICK,
                                brickinfo->hostname, brickinfo->path);
                        goto out;
                }
        }

        if (uuid_compare (brickinfo->uuid, MY_UUID)) {
                ret = 0;
                goto out;
        }
        ret = glusterd_volume_start_glusterfs (volinfo, brickinfo, wait);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to start brick %s:%s",
                        brickinfo->hostname, brickinfo->path);
                goto out;
        }

out:
        gf_log (this->name, GF_LOG_DEBUG, "returning %d ", ret);
        return ret;
}

int
glusterd_restart_bricks (glusterd_conf_t *conf)
{
        glusterd_volinfo_t   *volinfo        = NULL;
        glusterd_brickinfo_t *brickinfo      = NULL;
        gf_boolean_t          start_nodesvcs = _gf_false;
        int                   ret            = 0;

        list_for_each_entry (volinfo, &conf->volumes, vol_list) {
                if (volinfo->status != GLUSTERD_STATUS_STARTED)
                        continue;
                start_nodesvcs = _gf_true;
                list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                        glusterd_brick_start (volinfo, brickinfo, _gf_false);
                }
        }

        if (start_nodesvcs)
                glusterd_nodesvcs_handle_graph_change (NULL);

        return ret;
}

int
_local_gsyncd_start (dict_t *this, char *key, data_t *value, void *data)
{
        char                        *path_list = NULL;
        char                        *slave = NULL;
        char                        *slave_ip = NULL;
        char                        *slave_vol = NULL;
        char                        *statefile = NULL;
        char                         buf[1024] = "faulty";
        int                          uuid_len = 0;
        int                          ret = 0;
        char                         uuid_str[64] = {0};
        glusterd_volinfo_t          *volinfo = NULL;
        char                         confpath[PATH_MAX] = "";
        char                        *op_errstr = NULL;
        glusterd_conf_t             *priv = NULL;

        GF_ASSERT (THIS);
        priv = THIS->private;
        GF_ASSERT (priv);
        GF_ASSERT (data);

        volinfo = data;
        slave = strchr(value->data, ':');
        if (slave)
                slave ++;
        else
                return 0;
        uuid_len = (slave - value->data - 1);

        strncpy (uuid_str, (char*)value->data, uuid_len);

        /* Getting Local Brickpaths */
        ret = glusterd_get_local_brickpaths (volinfo, &path_list);

        /*Generating the conf file path needed by gsyncd */
        ret = glusterd_get_slave_info (slave, &slave_ip,
                                       &slave_vol, &op_errstr);
        if (ret) {
                gf_log ("", GF_LOG_ERROR,
                        "Unable to fetch slave details.");
                ret = -1;
                goto out;
        }

        ret = snprintf (confpath, sizeof(confpath) - 1,
                        "%s/"GEOREP"/%s_%s_%s/gsyncd.conf",
                        priv->workdir, volinfo->volname,
                        slave_ip, slave_vol);
        confpath[ret] = '\0';

        /* Fetching the last status of the node */
        ret = glusterd_get_statefile_name (volinfo, slave,
                                           confpath, &statefile);
        if (ret) {
                if (!strstr(slave, "::"))
                        gf_log ("", GF_LOG_INFO,
                                "%s is not a valid slave url.", slave);
                else
                        gf_log ("", GF_LOG_INFO, "Unable to get"
                                " statefile's name");
                goto out;
        }

        ret = glusterd_gsync_read_frm_status (statefile, buf, sizeof (buf));
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "Unable to read the status");
                goto out;
        }

        /* Looks for the last status, to find if the sessiom was running
         * when the node went down. If the session was not started or
         * not started, do not restart the geo-rep session */
        if ((!strcmp (buf, "Not Started")) ||
            (!strcmp (buf, "Stopped"))) {
                gf_log ("", GF_LOG_INFO,
                        "Geo-Rep Session was not started between "
                        "%s and %s::%s. Not Restarting", volinfo->volname,
                        slave_ip, slave_vol);
                goto out;
        }

        glusterd_start_gsync (volinfo, slave, path_list, confpath,
                              uuid_str, NULL);

out:
        GF_FREE (path_list);
        GF_FREE (op_errstr);

        return ret;
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

inline int
glusterd_get_dist_leaf_count (glusterd_volinfo_t *volinfo)
{
    int rcount = volinfo->replica_count;
    int scount = volinfo->stripe_count;

    return (rcount ? rcount : 1) * (scount ? scount : 1);
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
                        if (localhost && !gf_is_local_addr (tmpbrkinfo->hostname))
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

glusterd_brickinfo_t*
glusterd_get_brickinfo_by_position (glusterd_volinfo_t *volinfo, uint32_t pos)
{
        glusterd_brickinfo_t    *tmpbrkinfo = NULL;

        list_for_each_entry (tmpbrkinfo, &volinfo->bricks,
                             brick_list) {
                if (pos == 0)
                        return tmpbrkinfo;
                pos--;
        }
        return NULL;
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

#ifdef GF_LINUX_HOST_OS
static int
glusterd_get_brick_root (char *path, char **mount_point)
{
        char           *ptr            = NULL;
        char           *mnt_pt         = NULL;
        struct stat     brickstat      = {0};
        struct stat     buf            = {0};

        if (!path)
                goto err;
        mnt_pt = gf_strdup (path);
        if (!mnt_pt)
                goto err;
        if (stat (mnt_pt, &brickstat))
                goto err;

        while ((ptr = strrchr (mnt_pt, '/')) &&
               ptr != mnt_pt) {

                *ptr = '\0';
                if (stat (mnt_pt, &buf)) {
                        gf_log (THIS->name, GF_LOG_ERROR, "error in "
                                "stat: %s", strerror (errno));
                        goto err;
                }

                if (brickstat.st_dev != buf.st_dev) {
                        *ptr = '/';
                        break;
                }
        }

        if (ptr == mnt_pt) {
                if (stat ("/", &buf)) {
                        gf_log (THIS->name, GF_LOG_ERROR, "error in "
                                "stat: %s", strerror (errno));
                        goto err;
                }
                if (brickstat.st_dev == buf.st_dev)
                        strcpy (mnt_pt, "/");
        }

        *mount_point = mnt_pt;
        return 0;

 err:
        GF_FREE (mnt_pt);
        return -1;
}

static char*
glusterd_parse_inode_size (char *stream, char *pattern)
{
        char *needle = NULL;
        char *trail  = NULL;

        needle = strstr (stream, pattern);
        if (!needle)
                goto out;

        needle = nwstrtail (needle, pattern);

        trail = needle;
        while (trail && isdigit (*trail)) trail++;
        if (trail)
                *trail = '\0';

out:
        return needle;
}

static int
glusterd_add_inode_size_to_dict (dict_t *dict, int count)
{
        int             ret               = -1;
        char            key[1024]         = {0};
        char            buffer[4096]      = {0};
        char           *inode_size        = NULL;
        char           *device            = NULL;
        char           *fs_name           = NULL;
        char           *cur_word          = NULL;
        char           *pattern           = NULL;
        char           *trail             = NULL;
        runner_t        runner            = {0, };

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.device", count);
        ret = dict_get_str (dict, key, &device);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.fs_name", count);
        ret = dict_get_str (dict, key, &fs_name);
        if (ret)
                goto out;

        runinit (&runner);
        runner_redir (&runner, STDOUT_FILENO, RUN_PIPE);
        /* get inode size for xfs or ext2/3/4 */
        if (!strcmp (fs_name, "xfs")) {

                runner_add_args (&runner, "xfs_info", device, NULL);
                pattern = "isize=";

        } else if (IS_EXT_FS(fs_name)) {

                runner_add_args (&runner, "tune2fs", "-l", device, NULL);
                pattern = "Inode size:";

        } else {
                ret = 0;
                gf_log (THIS->name, GF_LOG_INFO, "Skipped fetching "
                        "inode size for %s: FS type not recommended",
                        fs_name);
                goto out;
        }

        ret = runner_start (&runner);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "could not get inode "
                        "size for %s : %s package missing", fs_name,
                        ((strcmp (fs_name, "xfs")) ?
                         "e2fsprogs" : "xfsprogs"));
                goto out;
        }

        for (;;) {
                if (fgets (buffer, sizeof (buffer),
                    runner_chio (&runner, STDOUT_FILENO)) == NULL)
                        break;
                trail = strrchr (buffer, '\n');
                if (trail)
                        *trail = '\0';

                cur_word = glusterd_parse_inode_size (buffer, pattern);
                if (cur_word)
                        break;
        }

        ret = runner_end (&runner);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "%s exited with non-zero "
                        "exit status", ((!strcmp (fs_name, "xfs")) ?
                        "xfs_info" : "tune2fs"));
                goto out;
        }
        if (!cur_word) {
                ret = -1;
                gf_log (THIS->name, GF_LOG_ERROR, "Unable to retrieve inode "
                        "size using %s",
                        (!strcmp (fs_name, "xfs")? "xfs_info": "tune2fs"));
                goto out;
        }

        inode_size = gf_strdup (cur_word);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.inode_size", count);

        ret = dict_set_dynstr (dict, key, inode_size);

 out:
        if (ret)
                gf_log (THIS->name, GF_LOG_ERROR, "failed to get inode size");
        return ret;
}

static int
glusterd_add_brick_mount_details (glusterd_brickinfo_t *brickinfo,
                                  dict_t *dict, int count)
{
        int             ret                  = -1;
        char            key[1024]            = {0};
        char            base_key[1024]       = {0};
        char           *mnt_pt               = NULL;
        char           *fs_name              = NULL;
        char           *mnt_options          = NULL;
        char           *device               = NULL;
        FILE           *mtab                 = NULL;
        struct mntent  *entry                = NULL;

        snprintf (base_key, sizeof (base_key), "brick%d", count);

        ret = glusterd_get_brick_root (brickinfo->path, &mnt_pt);
        if (ret)
                goto out;

        mtab = setmntent (_PATH_MOUNTED, "r");
        if (!mtab) {
                ret = -1;
                goto out;
        }

        entry = getmntent (mtab);

        while (1) {
                if (!entry) {
                        ret = -1;
                        goto out;
                }
                if (!strcmp (entry->mnt_dir, mnt_pt) &&
                    strcmp (entry->mnt_type, "rootfs"))
                        break;
                entry = getmntent (mtab);
        }

        /* get device file */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.device", base_key);

        device = gf_strdup (entry->mnt_fsname);
        ret = dict_set_dynstr (dict, key, device);
        if (ret)
                goto out;

        /* fs type */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.fs_name", base_key);

        fs_name = gf_strdup (entry->mnt_type);
        ret = dict_set_dynstr (dict, key, fs_name);
        if (ret)
                goto out;

        /* mount options */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.mnt_options", base_key);

        mnt_options = gf_strdup (entry->mnt_opts);
        ret = dict_set_dynstr (dict, key, mnt_options);

 out:
        GF_FREE (mnt_pt);
        if (mtab)
                endmntent (mtab);

        return ret;
}
#endif

int
glusterd_add_brick_detail_to_dict (glusterd_volinfo_t *volinfo,
                                   glusterd_brickinfo_t *brickinfo,
                                   dict_t *dict, int count)
{
        int             ret               = -1;
        uint64_t        memtotal          = 0;
        uint64_t        memfree           = 0;
        uint64_t        inodes_total      = 0;
        uint64_t        inodes_free       = 0;
        uint64_t        block_size        = 0;
        char            key[1024]         = {0};
        char            base_key[1024]    = {0};
        struct statvfs  brickstat         = {0};
        xlator_t       *this              = NULL;

        this = THIS;
        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);
        GF_ASSERT (dict);

        snprintf (base_key, sizeof (base_key), "brick%d", count);

        ret = statvfs (brickinfo->path, &brickstat);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "statfs error: %s ",
                        strerror (errno));
                goto out;
        }

        /* file system block size */
        block_size = brickstat.f_bsize;
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.block_size", base_key);
        ret = dict_set_uint64 (dict, key, block_size);
        if (ret)
                goto out;

        /* free space in brick */
        memfree = brickstat.f_bfree * brickstat.f_bsize;
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.free", base_key);
        ret = dict_set_uint64 (dict, key, memfree);
        if (ret)
                goto out;

        /* total space of brick */
        memtotal = brickstat.f_blocks * brickstat.f_bsize;
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.total", base_key);
        ret = dict_set_uint64 (dict, key, memtotal);
        if (ret)
                goto out;

        /* inodes: total and free counts only for ext2/3/4 and xfs */
        inodes_total = brickstat.f_files;
        if (inodes_total) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.total_inodes", base_key);
                ret = dict_set_uint64 (dict, key, inodes_total);
                if (ret)
                        goto out;
        }

        inodes_free = brickstat.f_ffree;
        if (inodes_free) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.free_inodes", base_key);
                ret = dict_set_uint64 (dict, key, inodes_free);
                if (ret)
                        goto out;
        }
#ifdef GF_LINUX_HOST_OS
        ret = glusterd_add_brick_mount_details (brickinfo, dict, count);
        if (ret)
                goto out;

        ret = glusterd_add_inode_size_to_dict (dict, count);
#endif
 out:
        if (ret)
                gf_log (this->name, GF_LOG_DEBUG, "Error adding brick"
                        " detail to dict: %s", strerror (errno));
        return ret;
}

int32_t
glusterd_add_brick_to_dict (glusterd_volinfo_t *volinfo,
                            glusterd_brickinfo_t *brickinfo,
                            dict_t  *dict, int32_t count)
{

        int             ret                   = -1;
        int32_t         pid                   = -1;
        int32_t         brick_online          = -1;
        char           *peer_id_str           = NULL;
        char            key[1024]             = {0};
        char            base_key[1024]        = {0};
        char            pidfile[PATH_MAX]     = {0};
        xlator_t        *this                 = NULL;
        glusterd_conf_t *priv                 = NULL;


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

        /* add peer uuid */
        peer_id_str = gf_strdup (uuid_utoa (brickinfo->uuid));
        if (!peer_id_str) {
                ret = -1;
                goto out;
        }
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.peerid", base_key);
        ret = dict_set_dynstr (dict, key, peer_id_str);
        if (ret) {
                GF_FREE (peer_id_str);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.port", base_key);
        ret = dict_set_int32 (dict, key, brickinfo->port);
        if (ret)
                goto out;

        GLUSTERD_GET_BRICK_PIDFILE (pidfile, volinfo, brickinfo, priv);

        brick_online = gf_is_service_running (pidfile, &pid);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.pid", base_key);
        ret = dict_set_int32 (dict, key, pid);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.status", base_key);
        ret = dict_set_int32 (dict, key, brick_online);

out:
        if (ret)
                gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
glusterd_get_all_volnames (dict_t *dict)
{
        int                    ret        = -1;
        int32_t                vol_count  = 0;
        char                   key[256]   = {0};
        glusterd_volinfo_t    *entry      = NULL;
        glusterd_conf_t       *priv       = NULL;

        priv = THIS->private;
        GF_ASSERT (priv);

        list_for_each_entry (entry, &priv->volumes, vol_list) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "vol%d", vol_count);
                ret = dict_set_str (dict, key, entry->volname);
                if (ret)
                        goto out;

                vol_count++;
        }

        ret = dict_set_int32 (dict, "vol_count", vol_count);

 out:
        if (ret)
                gf_log (THIS->name, GF_LOG_ERROR, "failed to get all "
                        "volume names for status");
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
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (peerinfo);

        *peerinfo = NULL;
        priv    = this->private;

        GF_ASSERT (priv);

        if (uuid_is_null (uuid))
                return -1;

        list_for_each_entry (entry, &priv->peers, uuid_list) {
                if (!uuid_compare (entry->uuid, uuid)) {

                        gf_log (this->name, GF_LOG_DEBUG,
                                 "Friend found... state: %s",
                        glusterd_friend_sm_state_name_get (entry->state.state));
                        *peerinfo = entry;
                        return 0;
                }
        }

        gf_log (this->name, GF_LOG_DEBUG, "Friend with uuid: %s, not found",
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
        xlator_t                *this  = NULL;


        this = THIS;
        GF_ASSERT (hoststr);
        GF_ASSERT (peerinfo);

        *peerinfo = NULL;
        priv    = this->private;

        GF_ASSERT (priv);

        list_for_each_entry (entry, &priv->peers, uuid_list) {
                if (!strncasecmp (entry->hostname, hoststr,
                                  1024)) {

                        gf_log (this->name, GF_LOG_DEBUG,
                                 "Friend %s found.. state: %d", hoststr,
                                  entry->state.state);
                        *peerinfo = entry;
                        return 0;
                }
        }

        ret = getaddrinfo (hoststr, NULL, NULL, &addr);
        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "error in getaddrinfo: %s\n",
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
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "Friend %s found.. state: %d",
                                        hoststr, entry->state.state);
                                *peerinfo = entry;
                                freeaddrinfo (addr);
                                return 0;
                        }
                }
        }

out:
        gf_log (this->name, GF_LOG_DEBUG, "Unable to find friend: %s", hoststr);
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
                if (gf_is_local_addr (hostname)) {
                        uuid_copy (uuid, MY_UUID);
                        ret = 0;
                } else {
                        goto out;
                }
        } else {
                uuid_copy (uuid, peerinfo->uuid);
        }

out:
        gf_log (this->name, GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int
glusterd_brick_stop (glusterd_volinfo_t *volinfo,
                     glusterd_brickinfo_t *brickinfo,
                     gf_boolean_t del_brick)
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
                        gf_log (this->name, GF_LOG_ERROR, FMTSTR_RESOLVE_BRICK,
                                brickinfo->hostname, brickinfo->path);
                        goto out;
                }
        }

        if (uuid_compare (brickinfo->uuid, MY_UUID)) {
                ret = 0;
                if (del_brick)
                        glusterd_delete_brick (volinfo, brickinfo);
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG, "About to stop glusterfs"
                " for brick %s:%s", brickinfo->hostname,
                brickinfo->path);
        ret = glusterd_volume_stop_glusterfs (volinfo, brickinfo, del_brick);
        if (ret) {
                gf_log (this->name, GF_LOG_CRITICAL, "Unable to stop"
                        " brick: %s:%s", brickinfo->hostname,
                        brickinfo->path);
                goto out;
        }

out:
        gf_log (this->name, GF_LOG_DEBUG, "returning %d ", ret);
        return ret;
}

int
glusterd_is_defrag_on (glusterd_volinfo_t *volinfo)
{
        return (volinfo->rebal.defrag != NULL);
}

gf_boolean_t
glusterd_is_rb_ongoing (glusterd_volinfo_t *volinfo)
{
        gf_boolean_t     ret = _gf_false;

        GF_ASSERT (volinfo);

        if (glusterd_is_rb_started (volinfo) ||
            glusterd_is_rb_paused (volinfo))
                ret = _gf_true;

        return ret;
}

int
glusterd_new_brick_validate (char *brick, glusterd_brickinfo_t *brickinfo,
                             char *op_errstr, size_t len)
{
        glusterd_brickinfo_t    *newbrickinfo = NULL;
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
                ret = glusterd_brickinfo_new_from_brick (brick, &newbrickinfo);
                if (ret)
                        goto out;
                is_allocated = _gf_true;
        } else {
                newbrickinfo = brickinfo;
        }

        ret = glusterd_resolve_brick (newbrickinfo);
        if (ret) {
                snprintf(op_errstr, len, "Host %s is not in \'Peer "
                         "in Cluster\' state", newbrickinfo->hostname);
                goto out;
        }

        if (!uuid_compare (MY_UUID, newbrickinfo->uuid)) {
                /* brick is local */
                if (!glusterd_is_brickpath_available (newbrickinfo->uuid,
                                                      newbrickinfo->path)) {
                        snprintf(op_errstr, len, "Brick: %s not available."
                                 " Brick may be containing or be contained "
                                 "by an existing brick", brick);
                        ret = -1;
                        goto out;
                }

        } else {
                ret = glusterd_friend_find_by_uuid (newbrickinfo->uuid,
                                                    &peerinfo);
                if (ret) {
                        snprintf (op_errstr, len, "Failed to find host %s",
                                  newbrickinfo->hostname);
                        goto out;
                }

                if ((!peerinfo->connected)) {
                        snprintf(op_errstr, len, "Host %s not connected",
                                 newbrickinfo->hostname);
                        ret = -1;
                        goto out;
                }

                if (peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED) {
                        snprintf(op_errstr, len, "Host %s is not in \'Peer "
                                 "in Cluster\' state",
                                 newbrickinfo->hostname);
                        ret = -1;
                        goto out;
                }
        }

        ret = 0;
out:
        if (is_allocated)
                glusterd_brickinfo_delete (newbrickinfo);
        if (op_errstr[0] != '\0')
                gf_log (this->name, GF_LOG_ERROR, "%s", op_errstr);
        gf_log (this->name, GF_LOG_DEBUG, "returning %d ", ret);
        return ret;
}

int
glusterd_is_rb_started(glusterd_volinfo_t *volinfo)
{
        gf_log ("", GF_LOG_DEBUG,
                "is_rb_started:status=%d", volinfo->rep_brick.rb_status);
        return (volinfo->rep_brick.rb_status == GF_RB_STATUS_STARTED);

}

int
glusterd_is_rb_paused ( glusterd_volinfo_t *volinfo)
{
        gf_log ("", GF_LOG_DEBUG,
                "is_rb_paused:status=%d", volinfo->rep_brick.rb_status);

        return (volinfo->rep_brick.rb_status == GF_RB_STATUS_PAUSED);
}

inline int
glusterd_set_rb_status (glusterd_volinfo_t *volinfo, gf_rb_status_t status)
{
        gf_log ("", GF_LOG_DEBUG,
                "setting status from %d to %d",
                volinfo->rep_brick.rb_status,
                status);

        volinfo->rep_brick.rb_status = status;
        return 0;
}

inline int
glusterd_rb_check_bricks (glusterd_volinfo_t *volinfo,
                          glusterd_brickinfo_t *src, glusterd_brickinfo_t *dst)
{
        glusterd_replace_brick_t        *rb = NULL;

        GF_ASSERT (volinfo);

        rb = &volinfo->rep_brick;

        if (!rb->src_brick || !rb->dst_brick)
                return -1;

        if (strcmp (rb->src_brick->hostname, src->hostname) ||
            strcmp (rb->src_brick->path, src->path)) {
                gf_log("", GF_LOG_ERROR, "Replace brick src bricks differ");
                return -1;
        }

        if (strcmp (rb->dst_brick->hostname, dst->hostname) ||
            strcmp (rb->dst_brick->path, dst->path)) {
                gf_log ("", GF_LOG_ERROR, "Replace brick dst bricks differ");
                return -1;
        }

        return 0;
}

/*path needs to be absolute; works only on gfid, volume-id*/
static int
glusterd_is_uuid_present (char *path, char *xattr, gf_boolean_t *present)
{
        GF_ASSERT (path);
        GF_ASSERT (xattr);
        GF_ASSERT (present);

        int     ret      = -1;
        uuid_t  uid     = {0,};

        if (!path || !xattr || !present)
                goto out;

        ret = sys_lgetxattr (path, xattr, &uid, 16);

        if (ret >= 0) {
                *present = _gf_true;
                ret = 0;
                goto out;
        }

        switch (errno) {
#if defined(ENODATA)
                case ENODATA: /* FALLTHROUGH */
#endif
#if defined(ENOATTR) && (ENOATTR != ENODATA)
                case ENOATTR: /* FALLTHROUGH */
#endif
                case ENOTSUP:
                        *present = _gf_false;
                        ret = 0;
                        break;
                default:
                        break;
        }
out:
        return ret;
}

/*path needs to be absolute*/
static int
glusterd_is_path_in_use (char *path, gf_boolean_t *in_use, char **op_errstr)
{
        int             i               = 0;
        int             ret             = -1;
        gf_boolean_t    used            = _gf_false;
        char            dir[PATH_MAX]   = {0,};
        char            *curdir         = NULL;
        char            msg[2048]       = {0};
        char            *keys[3]         = {GFID_XATTR_KEY,
                                            GF_XATTR_VOL_ID_KEY,
                                            NULL};

        GF_ASSERT (path);
        if (!path)
                goto out;

        strcpy (dir, path);
        curdir = dir;
        do {
                for (i = 0; !used && keys[i]; i++) {
                        ret = glusterd_is_uuid_present (curdir, keys[i], &used);
                        if (ret)
                                goto out;
                }

                if (used)
                        break;

                curdir = dirname (curdir);
                if (!strcmp (curdir, "."))
                        goto out;


        } while (strcmp (curdir, "/"));

        if (!strcmp (curdir, "/")) {
                for (i = 0; !used && keys[i]; i++) {
                        ret = glusterd_is_uuid_present (curdir, keys[i], &used);
                        if (ret)
                                goto out;
                }
        }

        ret = 0;
        *in_use = used;
out:
        if (ret) {
                snprintf (msg, sizeof (msg), "Failed to get extended "
                          "attribute %s, reason: %s", keys[i],
                          strerror (errno));
        }

        if (*in_use) {
                if (!strcmp (path, curdir)) {
                        snprintf (msg, sizeof (msg), "%s is already part of a "
                          "volume", path);
                } else {
                        snprintf (msg, sizeof (msg), "parent directory %s is "
                          "already part of a volume", curdir);
                }
        }

        if (strlen (msg)) {
                gf_log (THIS->name, GF_LOG_ERROR, "%s", msg);
                *op_errstr = gf_strdup (msg);
        }

        return ret;
}

int
glusterd_check_and_set_brick_xattr (char *host, char *path, uuid_t uuid,
                                    char **op_errstr, gf_boolean_t is_force)
{
        int             ret             = -1;
        char            msg[2048]       = {0,};
        gf_boolean_t    in_use          = _gf_false;
        int             flags           = 0;

        /* Check for xattr support in backend fs */
        ret = sys_lsetxattr (path, "trusted.glusterfs.test",
                             "working", 8, 0);
        if (ret) {
                snprintf (msg, sizeof (msg), "Glusterfs is not"
                          " supported on brick: %s:%s.\nSetting"
                          " extended attributes failed, reason:"
                          " %s.", host, path, strerror(errno));
                goto out;

        } else {
                sys_lremovexattr (path, "trusted.glusterfs.test");
        }

        ret = glusterd_is_path_in_use (path, &in_use, op_errstr);
        if (ret)
                goto out;

        if (in_use && !is_force) {
                ret = -1;
                goto out;
        }


        if (!is_force)
                flags = XATTR_CREATE;

        ret = sys_lsetxattr (path, GF_XATTR_VOL_ID_KEY, uuid, 16,
                             flags);
        if (ret) {
                snprintf (msg, sizeof (msg), "Failed to set extended "
                          "attributes %s, reason: %s",
                          GF_XATTR_VOL_ID_KEY, strerror (errno));
                goto out;
        }

        ret = 0;
out:
        if (strlen (msg))
                *op_errstr = gf_strdup (msg);

        return ret;
}

int
glusterd_sm_tr_log_transition_add_to_dict (dict_t *dict,
                                           glusterd_sm_tr_log_t *log, int i,
                                           int count)
{
        int     ret = -1;
        char    key[512] = {0};
	char    timestr[64] = {0,};
        char    *str = NULL;

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
        gf_time_fmt (timestr, sizeof timestr, log->transitions[i].time,
                     gf_timefmt_FT);
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
        xlator_t                 *this = NULL;

        this = THIS;
        GF_ASSERT (this);

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
        gf_log (this->name, GF_LOG_DEBUG, "Transitioning from '%s' to '%s' "
                "due to event '%s'", log->state_name_get (old_state),
                log->state_name_get (new_state), log->event_name_get (event));
out:
        gf_log (this->name, GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int
glusterd_peerinfo_new (glusterd_peerinfo_t **peerinfo,
                       glusterd_friend_sm_state_t state, uuid_t *uuid,
                       const char *hostname, int port)
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

        if (new_peer->state.state == GD_FRIEND_STATE_BEFRIENDED)
                new_peer->quorum_contrib = QUORUM_WAITING;
        new_peer->port = port;
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
        int                     ret = 0;

        list_for_each_entry_safe (pending_node, tmp, list, list) {
                if (elem == pending_node->node) {
                        list_del_init (&pending_node->list);
                        GF_FREE (pending_node);
                        ret = 0;
                        goto out;
                }
        }
out:
        gf_log (THIS->name, GF_LOG_DEBUG, "returning %d", ret);
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

        glusterd_volinfo_remove (volinfo);
out:
        gf_log (THIS->name, GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int32_t
glusterd_delete_brick (glusterd_volinfo_t* volinfo,
                       glusterd_brickinfo_t *brickinfo)
{
        int             ret = 0;
        char      voldir[PATH_MAX] = {0,};
        glusterd_conf_t *priv = THIS->private;
        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);

        GLUSTERD_GET_VOLUME_DIR(voldir, volinfo, priv);

        glusterd_delete_volfile (volinfo, brickinfo);
        glusterd_store_delete_brick (brickinfo, voldir);
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
glusterd_get_local_brickpaths (glusterd_volinfo_t *volinfo, char **pathlist)
{
        char                 **path_tokens  = NULL;
        char                  *tmp_path_list = NULL;
        char                   path[PATH_MAX] = "";
        int32_t                count          = 0;
        int32_t                pathlen        = 0;
        int32_t                total_len      = 0;
        int32_t                ret            = 0;
        int                    i              = 0;
        glusterd_brickinfo_t  *brickinfo      = NULL;

        if ((!volinfo) || (!pathlist))
            goto out;

        path_tokens = GF_CALLOC (sizeof(char*), volinfo->brick_count,
                                 gf_gld_mt_charptr);
        if (!path_tokens) {
                gf_log ("", GF_LOG_DEBUG, "Could not allocate memory.");
                ret = -1;
                goto out;
        }

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (uuid_compare (brickinfo->uuid, MY_UUID))
                        continue;

                 pathlen = snprintf (path, sizeof(path),
                                     "--path=%s ", brickinfo->path);
                 if (pathlen < sizeof(path))
                        path[pathlen] = '\0';
                 else
                        path[sizeof(path)-1] = '\0';
                 path_tokens[count] = gf_strdup (path);
                 if (!path_tokens[count]) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Could not allocate memory.");
                        ret = -1;
                        goto out;
                 }
                 count++;
                 total_len += pathlen;
        }

        tmp_path_list = GF_CALLOC (sizeof(char), total_len + 1,
                                   gf_gld_mt_char);
        if (!tmp_path_list) {
                gf_log ("", GF_LOG_DEBUG, "Could not allocate memory.");
                ret = -1;
                goto out;
        }

        for (i = 0; i < count; i++)
                strcat (tmp_path_list, path_tokens[i]);

        if (count)
                *pathlist = tmp_path_list;

        ret = count;
out:
        for (i = 0; i < count; i++) {
                GF_FREE (path_tokens[i]);
                path_tokens[i] = NULL;
        }

        GF_FREE (path_tokens);
        path_tokens = NULL;

        if (ret == 0) {
                gf_log ("", GF_LOG_DEBUG, "No Local Bricks Present.");
                GF_FREE (tmp_path_list);
                tmp_path_list = NULL;
        }

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_start_gsync (glusterd_volinfo_t *master_vol, char *slave,
                      char *path_list, char *conf_path,
                      char *glusterd_uuid_str,
                      char **op_errstr)
{
        int32_t         ret     = 0;
        int32_t         status  = 0;
        char            uuid_str [64] = {0};
        runner_t        runner = {0,};
        xlator_t        *this = NULL;
        glusterd_conf_t *priv = NULL;
        int             errcode = 0;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        uuid_utoa_r (MY_UUID, uuid_str);

        if (!path_list) {
                ret = 0;
                gf_log ("", GF_LOG_DEBUG, "No Bricks in this node."
                        " Not starting gsyncd.");
                goto out;
        }

        ret = gsync_status (master_vol->volname, slave, conf_path, &status);
        if (status == 0)
                goto out;

        uuid_utoa_r (master_vol->volume_id, uuid_str);
        runinit (&runner);
        runner_add_args  (&runner, GSYNCD_PREFIX"/gsyncd",
                          path_list, "-c", NULL);
        runner_argprintf (&runner, "%s", conf_path);
        runner_argprintf (&runner, ":%s", master_vol->volname);
        runner_add_args  (&runner, slave, "--config-set", "session-owner",
                          uuid_str, NULL);
        synclock_unlock (&priv->big_lock);
        ret = runner_run (&runner);
        synclock_lock (&priv->big_lock);
        if (ret == -1) {
                errcode = -1;
                goto out;
        }

        runinit (&runner);
        runner_add_args  (&runner, GSYNCD_PREFIX"/gsyncd",
                          path_list, "--monitor", "-c", NULL);
        runner_argprintf (&runner, "%s", conf_path);
        runner_argprintf (&runner, ":%s", master_vol->volname);
        runner_argprintf (&runner, "--glusterd-uuid=%s",
                          uuid_utoa (priv->uuid));
        runner_add_arg   (&runner, slave);
        synclock_unlock (&priv->big_lock);
        ret = runner_run (&runner);
        synclock_lock (&priv->big_lock);
        if (ret == -1) {
                gf_asprintf (op_errstr, GEOREP" start failed for %s %s",
                             master_vol->volname, slave);
                goto out;
        }

        ret = 0;

out:
        if ((ret != 0) && errcode == -1) {
                if (op_errstr)
                        *op_errstr = gf_strdup ("internal error, cannot start "
                                                "the " GEOREP " session");
        }

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_recreate_volfiles (glusterd_conf_t *conf)
{

        glusterd_volinfo_t      *volinfo = NULL;
        int                      ret = 0;
        int                      op_ret = 0;

        GF_ASSERT (conf);
        list_for_each_entry (volinfo, &conf->volumes, vol_list) {
                ret = generate_brick_volfiles (volinfo);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR, "Failed to "
                                "regenerate brick volfiles for %s",
                                volinfo->volname);
                        op_ret = ret;
                }
                ret = generate_client_volfiles (volinfo, GF_CLIENT_TRUSTED);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR, "Failed to "
                                "regenerate trusted client volfiles for %s",
                                volinfo->volname);
                        op_ret = ret;
                }
                ret = generate_client_volfiles (volinfo, GF_CLIENT_OTHER);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR, "Failed to "
                                "regenerate client volfiles for %s",
                                volinfo->volname);
                        op_ret = ret;
                }
        }
        return op_ret;
}

int32_t
glusterd_handle_upgrade_downgrade (dict_t *options, glusterd_conf_t *conf)
{
        int              ret                            = 0;
        char            *type                           = NULL;
        gf_boolean_t     upgrade                        = _gf_false;
        gf_boolean_t     downgrade                      = _gf_false;
        gf_boolean_t     regenerate_volfiles            = _gf_false;
        gf_boolean_t     terminate                      = _gf_false;

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
                        regenerate_volfiles = _gf_true;
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
        else
                terminate = _gf_true;
        if (regenerate_volfiles) {
                ret = glusterd_recreate_volfiles (conf);
        }
out:
        if (terminate && (ret == 0))
                kill (getpid(), SIGTERM);
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

int
glusterd_set_dump_options (char *dumpoptions_path, char *options,
                           int option_cnt)
{
        int     ret = 0;
        char    *dup_options = NULL;
        char    *option = NULL;
        char    *tmpptr = NULL;
        FILE    *fp = NULL;
        int     nfs_cnt = 0;

        if (0 == option_cnt ||
            (option_cnt == 1 && (!strcmp (options, "nfs ")))) {
                ret = 0;
                goto out;
        }

        fp = fopen (dumpoptions_path, "w");
        if (!fp) {
                ret = -1;
                goto out;
        }
        dup_options = gf_strdup (options);
        gf_log ("", GF_LOG_INFO, "Received following statedump options: %s",
                dup_options);
        option = strtok_r (dup_options, " ", &tmpptr);
        while (option) {
                if (!strcmp (option, "nfs")) {
                        if (nfs_cnt > 0) {
                                unlink (dumpoptions_path);
                                ret = 0;
                                goto out;
                        }
                        nfs_cnt++;
                        option = strtok_r (NULL, " ", &tmpptr);
                        continue;
                }
                fprintf (fp, "%s=yes\n", option);
                option = strtok_r (NULL, " ", &tmpptr);
        }

out:
        if (fp)
                fclose (fp);
        GF_FREE (dup_options);
        return ret;
}

int
glusterd_brick_statedump (glusterd_volinfo_t *volinfo,
                          glusterd_brickinfo_t *brickinfo,
                          char *options, int option_cnt, char **op_errstr)
{
        int                     ret = -1;
        xlator_t                *this = NULL;
        glusterd_conf_t         *conf = NULL;
        char                    pidfile_path[PATH_MAX] = {0,};
        char                    dumpoptions_path[PATH_MAX] = {0,};
        FILE                    *pidfile = NULL;
        pid_t                   pid = -1;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        if (uuid_is_null (brickinfo->uuid)) {
                ret = glusterd_resolve_brick (brickinfo);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "Cannot resolve brick %s:%s",
                                brickinfo->hostname, brickinfo->path);
                        goto out;
                }
        }

        if (uuid_compare (brickinfo->uuid, MY_UUID)) {
                ret = 0;
                goto out;
        }

        GLUSTERD_GET_BRICK_PIDFILE (pidfile_path, volinfo, brickinfo, conf);

        pidfile = fopen (pidfile_path, "r");
        if (!pidfile) {
                gf_log ("", GF_LOG_ERROR, "Unable to open pidfile: %s",
                        pidfile_path);
                ret = -1;
                goto out;
        }

        ret = fscanf (pidfile, "%d", &pid);
        if (ret <= 0) {
                gf_log ("", GF_LOG_ERROR, "Unable to get pid of brick process");
                ret = -1;
                goto out;
        }

        snprintf (dumpoptions_path, sizeof (dumpoptions_path),
                  DEFAULT_VAR_RUN_DIRECTORY"/glusterdump.%d.options", pid);
        ret = glusterd_set_dump_options (dumpoptions_path, options, option_cnt);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error while parsing the statedump "
                        "options");
                ret = -1;
                goto out;
        }

        gf_log ("", GF_LOG_INFO, "Performing statedump on brick with pid %d",
                pid);

        kill (pid, SIGUSR1);

        sleep (1);
        ret = 0;
out:
        unlink (dumpoptions_path);
        if (pidfile)
                fclose (pidfile);
        return ret;
}

int
glusterd_nfs_statedump (char *options, int option_cnt, char **op_errstr)
{
        int                     ret = -1;
        xlator_t                *this = NULL;
        glusterd_conf_t         *conf = NULL;
        char                    pidfile_path[PATH_MAX] = {0,};
        char                    path[PATH_MAX] = {0,};
        FILE                    *pidfile = NULL;
        pid_t                   pid = -1;
        char                    dumpoptions_path[PATH_MAX] = {0,};
        char                    *option = NULL;
        char                    *tmpptr = NULL;
        char                    *dup_options = NULL;
        char                    msg[256] = {0,};

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        dup_options = gf_strdup (options);
        option = strtok_r (dup_options, " ", &tmpptr);
        if (strcmp (option, "nfs")) {
                snprintf (msg, sizeof (msg), "for nfs statedump, options should"
                          " be after the key nfs");
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }

        GLUSTERD_GET_NFS_DIR (path, conf);
        GLUSTERD_GET_NFS_PIDFILE (pidfile_path, path);

        pidfile = fopen (pidfile_path, "r");
        if (!pidfile) {
                gf_log ("", GF_LOG_ERROR, "Unable to open pidfile: %s",
                        pidfile_path);
                ret = -1;
                goto out;
        }

        ret = fscanf (pidfile, "%d", &pid);
        if (ret <= 0) {
                gf_log ("", GF_LOG_ERROR, "Unable to get pid of brick process");
                ret = -1;
                goto out;
        }

        snprintf (dumpoptions_path, sizeof (dumpoptions_path),
                  DEFAULT_VAR_RUN_DIRECTORY"/glusterdump.%d.options", pid);
        ret = glusterd_set_dump_options (dumpoptions_path, options, option_cnt);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error while parsing the statedump "
                        "options");
                ret = -1;
                goto out;
        }

        gf_log ("", GF_LOG_INFO, "Performing statedump on nfs server with "
                "pid %d", pid);

        kill (pid, SIGUSR1);

        sleep (1);

        ret = 0;
out:
        if (pidfile)
                fclose (pidfile);
        unlink (dumpoptions_path);
        GF_FREE (dup_options);
        return ret;
}

int
glusterd_quotad_statedump (char *options, int option_cnt, char **op_errstr)
{
        int                     ret = -1;
        xlator_t                *this = NULL;
        glusterd_conf_t         *conf = NULL;
        char                    pidfile_path[PATH_MAX] = {0,};
        char                    path[PATH_MAX] = {0,};
        FILE                    *pidfile = NULL;
        pid_t                   pid = -1;
        char                    dumpoptions_path[PATH_MAX] = {0,};
        char                    *option = NULL;
        char                    *tmpptr = NULL;
        char                    *dup_options = NULL;
        char                    msg[256] = {0,};

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        dup_options = gf_strdup (options);
        option = strtok_r (dup_options, " ", &tmpptr);
        if (strcmp (option, "quotad")) {
                snprintf (msg, sizeof (msg), "for quotad statedump, options "
                          "should be after the key 'quotad'");
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }

        GLUSTERD_GET_QUOTAD_DIR (path, conf);
        GLUSTERD_GET_QUOTAD_PIDFILE (pidfile_path, path);

        pidfile = fopen (pidfile_path, "r");
        if (!pidfile) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to open pidfile: %s",
                        pidfile_path);
                ret = -1;
                goto out;
        }

        ret = fscanf (pidfile, "%d", &pid);
        if (ret <= 0) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get pid of quotad "
                        "process");
                ret = -1;
                goto out;
        }

        snprintf (dumpoptions_path, sizeof (dumpoptions_path),
                  DEFAULT_VAR_RUN_DIRECTORY"/glusterdump.%d.options", pid);
        ret = glusterd_set_dump_options (dumpoptions_path, options, option_cnt);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "error while parsing "
                        "statedump options");
                ret = -1;
                goto out;
        }

        gf_log (this->name, GF_LOG_INFO, "Performing statedump on quotad with "
                "pid %d", pid);

        kill (pid, SIGUSR1);

        sleep (1);

        ret = 0;
out:
        if (pidfile)
                fclose (pidfile);
        unlink (dumpoptions_path);
        GF_FREE (dup_options);
        return ret;
}

/* Checks if the given peer contains all the bricks belonging to the
 * given volume. Returns true if it does else returns false
 */
gf_boolean_t
glusterd_friend_contains_vol_bricks (glusterd_volinfo_t *volinfo,
                                     uuid_t friend_uuid)
{
        gf_boolean_t            ret = _gf_true;
        glusterd_brickinfo_t    *brickinfo = NULL;

        GF_ASSERT (volinfo);

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (uuid_compare (friend_uuid, brickinfo->uuid)) {
                        ret = _gf_false;
                        break;
                }
        }
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

/* Remove all volumes which completely belong to given friend
 */
int
glusterd_friend_remove_cleanup_vols (uuid_t uuid)
{
        int                     ret = -1;
        glusterd_conf_t         *priv = NULL;
        glusterd_volinfo_t      *volinfo = NULL;
        glusterd_volinfo_t      *tmp_volinfo = NULL;

        priv = THIS->private;
        GF_ASSERT (priv);

        list_for_each_entry_safe (volinfo, tmp_volinfo,
                                  &priv->volumes, vol_list) {
                if (glusterd_friend_contains_vol_bricks (volinfo, uuid)) {
                        gf_log (THIS->name, GF_LOG_INFO,
                                "Deleting stale volume %s", volinfo->volname);
                        ret = glusterd_delete_volume (volinfo);
                        if (ret) {
                                gf_log (THIS->name, GF_LOG_ERROR,
                                        "Error deleting stale volume");
                                goto out;
                        }
                }
        }
        ret = 0;
out:
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

/* Check if the all peers are connected and befriended, except the peer
 * specified (the peer being detached)
 */
gf_boolean_t
glusterd_chk_peers_connected_befriended (uuid_t skip_uuid)
{
        gf_boolean_t            ret = _gf_true;
        glusterd_peerinfo_t     *peerinfo = NULL;
        glusterd_conf_t         *priv = NULL;

        priv= THIS->private;
        GF_ASSERT (priv);

        list_for_each_entry (peerinfo, &priv->peers, uuid_list) {

                if (!uuid_is_null (skip_uuid) && !uuid_compare (skip_uuid,
                                                           peerinfo->uuid))
                        continue;

                if ((GD_FRIEND_STATE_BEFRIENDED != peerinfo->state.state)
                    || !(peerinfo->connected)) {
                        ret = _gf_false;
                        break;
                }
        }
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %s",
                (ret?"TRUE":"FALSE"));
        return ret;
}

void
glusterd_get_client_filepath (char *filepath, glusterd_volinfo_t *volinfo,
                              gf_transport_type type)
{
        char  path[PATH_MAX] = {0,};
        glusterd_conf_t *priv = NULL;

        priv = THIS->private;

        GLUSTERD_GET_VOLUME_DIR (path, volinfo, priv);

        if ((volinfo->transport_type == GF_TRANSPORT_BOTH_TCP_RDMA) &&
            (type == GF_TRANSPORT_RDMA))
                snprintf (filepath, PATH_MAX, "%s/%s.rdma-fuse.vol",
                          path, volinfo->volname);
        else
                snprintf (filepath, PATH_MAX, "%s/%s-fuse.vol",
                          path, volinfo->volname);
}

void
glusterd_get_trusted_client_filepath (char *filepath,
                                      glusterd_volinfo_t *volinfo,
                                      gf_transport_type type)
{
        char  path[PATH_MAX] = {0,};
        glusterd_conf_t *priv = NULL;

        priv = THIS->private;

        GLUSTERD_GET_VOLUME_DIR (path, volinfo, priv);

        if ((volinfo->transport_type == GF_TRANSPORT_BOTH_TCP_RDMA) &&
            (type == GF_TRANSPORT_RDMA))
                snprintf (filepath, PATH_MAX,
                          "%s/trusted-%s.rdma-fuse.vol",
                          path, volinfo->volname);
        else
                snprintf (filepath, PATH_MAX,
                          "%s/trusted-%s-fuse.vol",
                          path, volinfo->volname);
}

int
glusterd_volume_defrag_restart (glusterd_volinfo_t *volinfo, char *op_errstr,
                              size_t len, int cmd, defrag_cbk_fn_t cbk)
{
        xlator_t        *this             = NULL;
        glusterd_conf_t *priv             = NULL;
        char            pidfile[PATH_MAX] = {0,};
        int             ret               = -1;
        pid_t           pid               = 0;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        if (!priv)
                return ret;

        /* Don't start the rebalance process if the stautus is already
         * completed, stopped or failed. If the status is started, check if
         * there is an existing process already and connect to it. If not, then
         * start the rebalance process
         */
        switch (volinfo->rebal.defrag_status) {
        case GF_DEFRAG_STATUS_COMPLETE:
        case GF_DEFRAG_STATUS_STOPPED:
        case GF_DEFRAG_STATUS_FAILED:
                break;
        case GF_DEFRAG_STATUS_STARTED:
                GLUSTERD_GET_DEFRAG_PID_FILE(pidfile, volinfo, priv);
                if (gf_is_service_running (pidfile, &pid)) {
                        glusterd_rebalance_rpc_create (volinfo, _gf_true);
                        break;
                }
        case GF_DEFRAG_STATUS_NOT_STARTED:
                glusterd_handle_defrag_start (volinfo, op_errstr, len, cmd,
                                              cbk, volinfo->rebal.op);
                break;
        default:
                gf_log (this->name, GF_LOG_ERROR, "Unknown defrag status (%d)."
                        "Not starting rebalance process for %s.",
                        volinfo->rebal.defrag_status, volinfo->volname);
                break;
        }

        return ret;
}

int
glusterd_restart_rebalance (glusterd_conf_t *conf)
{
        glusterd_volinfo_t       *volinfo = NULL;
        int                      ret = 0;
        char                     op_errstr[256];

        list_for_each_entry (volinfo, &conf->volumes, vol_list) {
                if (!volinfo->rebal.defrag_cmd)
                        continue;
                if (!gd_should_i_start_rebalance (volinfo))
                        continue;
                glusterd_volume_defrag_restart (volinfo, op_errstr, 256,
                                        volinfo->rebal.defrag_cmd, NULL);
        }
        return ret;
}

void
glusterd_volinfo_reset_defrag_stats (glusterd_volinfo_t *volinfo)
{
        glusterd_rebalance_t *rebal = NULL;
        GF_ASSERT (volinfo);

        rebal = &volinfo->rebal;
        rebal->rebalance_files = 0;
        rebal->rebalance_data = 0;
        rebal->lookedup_files = 0;
        rebal->rebalance_failures = 0;
        rebal->rebalance_time = 0;
        rebal->skipped_files = 0;

}

/* Return hostname for given uuid if it exists
 * else return NULL
 */
char *
glusterd_uuid_to_hostname (uuid_t uuid)
{
        char                    *hostname = NULL;
        glusterd_conf_t         *priv = NULL;
        glusterd_peerinfo_t     *entry = NULL;

        priv = THIS->private;
        GF_ASSERT (priv);

        if (!uuid_compare (MY_UUID, uuid)) {
                hostname = gf_strdup ("localhost");
        }
        if (!list_empty (&priv->peers)) {
                list_for_each_entry (entry, &priv->peers, uuid_list) {
                        if (!uuid_compare (entry->uuid, uuid)) {
                                hostname = gf_strdup (entry->hostname);
                                break;
                        }
                }
        }

        return hostname;
}

gf_boolean_t
glusterd_is_local_brick (xlator_t *this, glusterd_volinfo_t *volinfo,
                         glusterd_brickinfo_t *brickinfo)
{
        gf_boolean_t    local = _gf_false;
        int             ret = 0;
        glusterd_conf_t *conf = NULL;

        if (uuid_is_null (brickinfo->uuid)) {
                ret = glusterd_resolve_brick (brickinfo);
                if (ret)
                        goto out;
        }
        conf = this->private;
        local = !uuid_compare (brickinfo->uuid, MY_UUID);
out:
        return local;
}
int
glusterd_validate_volume_id (dict_t *op_dict, glusterd_volinfo_t *volinfo)
{
        int     ret             = -1;
        char    *volid_str      = NULL;
        uuid_t  vol_uid         = {0, };
        xlator_t *this          = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = dict_get_str (op_dict, "vol-id", &volid_str);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get volume id for "
                        "volume %s", volinfo->volname);
                goto out;
        }
        ret = uuid_parse (volid_str, vol_uid);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to parse volume id "
                        "for volume %s", volinfo->volname);
                goto out;
        }

        if (uuid_compare (vol_uid, volinfo->volume_id)) {
                gf_log (this->name, GF_LOG_ERROR, "Volume ids of volume %s - %s"
                        " and %s - are different. Possibly a split brain among "
                        "peers.", volinfo->volname, volid_str,
                        uuid_utoa (volinfo->volume_id));
                ret = -1;
                goto out;
        }

out:
        return ret;
}

int
glusterd_defrag_volume_status_update (glusterd_volinfo_t *volinfo,
                                      dict_t *rsp_dict)
{
        int                             ret = 0;
        uint64_t                        files = 0;
        uint64_t                        size = 0;
        uint64_t                        lookup = 0;
        gf_defrag_status_t              status = GF_DEFRAG_STATUS_NOT_STARTED;
        uint64_t                        failures = 0;
        uint64_t                        skipped = 0;
        xlator_t                       *this = NULL;
        double                          run_time = 0;

        this = THIS;

        ret = dict_get_uint64 (rsp_dict, "files", &files);
        if (ret)
                gf_log (this->name, GF_LOG_TRACE,
                        "failed to get file count");

        ret = dict_get_uint64 (rsp_dict, "size", &size);
        if (ret)
                gf_log (this->name, GF_LOG_TRACE,
                        "failed to get size of xfer");

        ret = dict_get_uint64 (rsp_dict, "lookups", &lookup);
        if (ret)
                gf_log (this->name, GF_LOG_TRACE,
                        "failed to get lookedup file count");

        ret = dict_get_int32 (rsp_dict, "status", (int32_t *)&status);
        if (ret)
                gf_log (this->name, GF_LOG_TRACE,
                        "failed to get status");

        ret = dict_get_uint64 (rsp_dict, "failures", &failures);
        if (ret)
                gf_log (this->name, GF_LOG_TRACE,
                        "failed to get failure count");

        ret = dict_get_uint64 (rsp_dict, "skipped", &skipped);
        if (ret)
                gf_log (this->name, GF_LOG_TRACE,
                        "failed to get skipped count");

        ret = dict_get_double (rsp_dict, "run-time", &run_time);
        if (ret)
                gf_log (this->name, GF_LOG_TRACE,
                        "failed to get run-time");

        if (files)
                volinfo->rebal.rebalance_files = files;
        if (size)
                volinfo->rebal.rebalance_data = size;
        if (lookup)
                volinfo->rebal.lookedup_files = lookup;
        if (status)
                volinfo->rebal.defrag_status = status;
        if (failures)
                volinfo->rebal.rebalance_failures = failures;
        if (skipped)
                volinfo->rebal.skipped_files = skipped;
        if (run_time)
                volinfo->rebal.rebalance_time = run_time;

        return ret;
}

int
glusterd_check_topology_identical (const char   *filename1,
                                   const char   *filename2,
                                   gf_boolean_t *identical)
{
        int                     ret    = -1; /* FAILURE */
        xlator_t                *this  = THIS;
        FILE                    *fp1   = NULL;
        FILE                    *fp2   = NULL;
        glusterfs_graph_t       *grph1 = NULL;
        glusterfs_graph_t       *grph2 = NULL;

        /* Invalid xlator, Nothing to do */
        if (!this)
                return (-1);

        /* Sanitize the inputs */
        GF_VALIDATE_OR_GOTO (this->name, filename1, out);
        GF_VALIDATE_OR_GOTO (this->name, filename2, out);
        GF_VALIDATE_OR_GOTO (this->name, identical, out);

        /* fopen() the volfile1 to create the graph */
        fp1 = fopen (filename1, "r");
        if (fp1 == NULL) {
                gf_log (this->name, GF_LOG_ERROR, "fopen() on file: %s failed "
                        "(%s)", filename1, strerror (errno));
                goto out;
        }

        /* fopen() the volfile2 to create the graph */
        fp2 = fopen (filename2, "r");
        if (fp2 == NULL) {
                gf_log (this->name, GF_LOG_ERROR, "fopen() on file: %s failed "
                        "(%s)", filename2, strerror (errno));
                goto out;
        }

        /* create the graph for filename1 */
        grph1 = glusterfs_graph_construct(fp1);
        if (grph1 == NULL)
                goto out;

        /* create the graph for filename2 */
        grph2 = glusterfs_graph_construct(fp2);
        if (grph2 == NULL)
                goto out;

        /* compare the graph topology */
        *identical = is_graph_topology_equal(grph1, grph2);
        ret = 0; /* SUCCESS */
out:
        if (fp1)
                fclose(fp1);
        if (fp2)
                fclose(fp2);
        if (grph1)
                glusterfs_graph_destroy(grph1);
        if (grph2)
                glusterfs_graph_destroy(grph2);

        gf_log (this->name, GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

int
glusterd_check_files_identical (char *filename1, char *filename2,
                                gf_boolean_t *identical)
{
        int                     ret = -1;
        struct stat             buf1 = {0,};
        struct stat             buf2 = {0,};
        uint32_t                cksum1 = 0;
        uint32_t                cksum2 = 0;
        xlator_t                *this = NULL;

        GF_ASSERT (filename1);
        GF_ASSERT (filename2);
        GF_ASSERT (identical);

        this = THIS;

        ret = stat (filename1, &buf1);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "stat on file: %s failed "
                        "(%s)", filename1, strerror (errno));
                goto out;
        }

        ret = stat (filename2, &buf2);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "stat on file: %s failed "
                        "(%s)", filename2, strerror (errno));
                goto out;
        }

        if (buf1.st_size != buf2.st_size) {
                *identical = _gf_false;
                goto out;
        }

        ret = get_checksum_for_path (filename1, &cksum1);
        if (ret)
                goto out;


        ret = get_checksum_for_path (filename2, &cksum2);
        if (ret)
                goto out;

        if (cksum1 != cksum2)
                *identical = _gf_false;
        else
                *identical = _gf_true;

out:
        gf_log (this->name, GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

int
glusterd_volset_help (dict_t *dict, char **op_errstr)
{
        int                     ret = -1;
        gf_boolean_t            xml_out = _gf_false;
        xlator_t                *this = NULL;

        this = THIS;

        if (!dict) {
                if (!(dict = glusterd_op_get_ctx ())) {
                        ret = 0;
                        goto out;
                }
        }

        if (dict_get (dict, "help" )) {
                xml_out = _gf_false;

        } else if (dict_get (dict, "help-xml" )) {
                xml_out = _gf_true;
#if (HAVE_LIB_XML)
                ret = 0;
#else
                gf_log (this->name, GF_LOG_ERROR,
                        "libxml not present in the system");
                if (op_errstr)
                        *op_errstr = gf_strdup ("Error: xml libraries not "
                                                "present to produce "
                                                "xml-output");
                goto out;
#endif

        } else {
                goto out;
        }

        ret = glusterd_get_volopt_content (dict, xml_out);
        if (ret && op_errstr)
                *op_errstr = gf_strdup ("Failed to get volume options help");
 out:

        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_to_cli (rpcsvc_request_t *req, gf_cli_rsp *arg, struct iovec *payload,
                 int payloadcount, struct iobref *iobref, xdrproc_t xdrproc,
                 dict_t *dict)
{
        int                ret = -1;
        char               *cmd = NULL;
        int                op_ret = 0;
        char               *op_errstr = NULL;
        int                op_errno = 0;
        xlator_t           *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        op_ret = arg->op_ret;
        op_errstr = arg->op_errstr;
        op_errno = arg->op_errno;

        ret = dict_get_str (dict, "cmd-str", &cmd);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR, "Failed to get command "
                        "string");

        if (cmd) {
                if (op_ret)
                        gf_cmd_log ("", "%s : FAILED %s %s", cmd,
                                       (op_errstr)? ":" : " ",
                                       (op_errstr)? op_errstr : " ");
                else
                        gf_cmd_log ("", "%s : SUCCESS", cmd);
        }

        glusterd_submit_reply (req, arg, payload, payloadcount, iobref,
                               (xdrproc_t) xdrproc);
        if (dict)
                dict_unref (dict);

        return ret;
}

static int32_t
glusterd_append_gsync_status (dict_t *dst, dict_t *src)
{
        int                ret = 0;
        char               *stop_msg = NULL;

        ret = dict_get_str (src, "gsync-status", &stop_msg);
        if (ret) {
                ret = 0;
                goto out;
        }

        ret = dict_set_dynstr (dst, "gsync-status", gf_strdup (stop_msg));
        if (ret) {
                gf_log ("glusterd", GF_LOG_WARNING, "Unable to set the stop"
                        "message in the ctx dictionary");
                goto out;
        }

        ret = 0;
 out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;

}

int32_t
glusterd_append_status_dicts (dict_t *dst, dict_t *src)
{
        char                sts_val_name[PATH_MAX] = {0, };
        int                 dst_count              = 0;
        int                 src_count              = 0;
        int                 i                      = 0;
        int                 ret                    = 0;
        gf_gsync_status_t  *sts_val                = NULL;
        gf_gsync_status_t  *dst_sts_val            = NULL;

        GF_ASSERT (dst);

        if (src == NULL)
                goto out;

        ret = dict_get_int32 (dst, "gsync-count", &dst_count);
        if (ret)
                dst_count = 0;

        ret = dict_get_int32 (src, "gsync-count", &src_count);
        if (ret || !src_count) {
                gf_log ("", GF_LOG_DEBUG, "Source brick empty");
                ret = 0;
                goto out;
        }

        for (i = 0; i < src_count; i++) {
                memset (sts_val_name, '\0', sizeof(sts_val_name));
                snprintf (sts_val_name, sizeof(sts_val_name), "status_value%d", i);

                ret = dict_get_bin (src, sts_val_name, (void **) &sts_val);
                if (ret)
                        goto out;

                dst_sts_val = GF_CALLOC (1, sizeof(gf_gsync_status_t),
                                         gf_common_mt_gsync_status_t);
                if (!dst_sts_val) {
                        gf_log ("", GF_LOG_ERROR, "Out Of Memory");
                        goto out;
                }

                memcpy (dst_sts_val, sts_val, sizeof(gf_gsync_status_t));

                memset (sts_val_name, '\0', sizeof(sts_val_name));
                snprintf (sts_val_name, sizeof(sts_val_name), "status_value%d", i + dst_count);

                ret = dict_set_bin (dst, sts_val_name, dst_sts_val, sizeof(gf_gsync_status_t));
                if (ret)
                        goto out;
        }

        ret = dict_set_int32 (dst, "gsync-count", dst_count+src_count);

 out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;

}

int32_t
glusterd_gsync_use_rsp_dict (dict_t *aggr, dict_t *rsp_dict, char *op_errstr)
{
        dict_t             *ctx = NULL;
        int                ret = 0;
        char               *conf_path = NULL;

        if (aggr) {
                ctx = aggr;

        } else {
                ctx = glusterd_op_get_ctx ();
                if (!ctx) {
                        gf_log ("", GF_LOG_ERROR,
                                "Operation Context is not present");
                        GF_ASSERT (0);
                }
        }

        if (rsp_dict) {
                ret = glusterd_append_status_dicts (ctx, rsp_dict);
                if (ret)
                        goto out;

                ret = glusterd_append_gsync_status (ctx, rsp_dict);
                if (ret)
                        goto out;

                ret = dict_get_str (rsp_dict, "conf_path", &conf_path);
                if (!ret && conf_path) {
                        ret = dict_set_dynstr (ctx, "conf_path",
                                            gf_strdup(conf_path));
                        if (ret) {
                                gf_log ("", GF_LOG_ERROR,
                                        "Unable to store conf path.");
                                goto out;
                        }
                }
        }
        if ((op_errstr) && (strcmp ("", op_errstr))) {
                ret = dict_set_dynstr (ctx, "errstr", gf_strdup(op_errstr));
                if (ret)
                        goto out;
        }

        ret = 0;
 out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d ", ret);
        return ret;
}

int32_t
glusterd_rb_use_rsp_dict (dict_t *aggr, dict_t *rsp_dict)
{
        int32_t  src_port = 0;
        int32_t  dst_port = 0;
        int      ret      = 0;
        dict_t  *ctx      = NULL;


        if (aggr) {
                ctx = aggr;

        } else {
                ctx = glusterd_op_get_ctx ();
                if (!ctx) {
                        gf_log ("", GF_LOG_ERROR,
                                "Operation Context is not present");
                        GF_ASSERT (0);
                }
        }

        if (rsp_dict) {
                ret = dict_get_int32 (rsp_dict, "src-brick-port", &src_port);
                if (ret == 0) {
                        gf_log ("", GF_LOG_DEBUG,
                                "src-brick-port=%d found", src_port);
                }

                ret = dict_get_int32 (rsp_dict, "dst-brick-port", &dst_port);
                if (ret == 0) {
                        gf_log ("", GF_LOG_DEBUG,
                                "dst-brick-port=%d found", dst_port);
                }

        }

        if (src_port) {
                ret = dict_set_int32 (ctx, "src-brick-port",
                                      src_port);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Could not set src-brick");
                        goto out;
                }
        }

        if (dst_port) {
                ret = dict_set_int32 (ctx, "dst-brick-port",
                                      dst_port);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Could not set dst-brick");
                        goto out;
                }

        }

out:
        return ret;

}

int32_t
glusterd_sync_use_rsp_dict (dict_t *aggr, dict_t *rsp_dict)
{
        int      ret      = 0;

        GF_ASSERT (rsp_dict);

        if (!rsp_dict) {
                goto out;
        }

        ret = glusterd_import_friend_volumes (rsp_dict);
out:
        return ret;

}

static int
_profile_volume_add_friend_rsp (dict_t *this, char *key, data_t *value,
                               void *data)
{
        char    new_key[256] = {0};
        glusterd_pr_brick_rsp_conv_t *rsp_ctx = NULL;
        data_t  *new_value = NULL;
        int     brick_count = 0;
        char    brick_key[256];

        if (strcmp (key, "count") == 0)
                return 0;
        sscanf (key, "%d%s", &brick_count, brick_key);
        rsp_ctx = data;
        new_value = data_copy (value);
        GF_ASSERT (new_value);
        snprintf (new_key, sizeof (new_key), "%d%s",
                  rsp_ctx->count + brick_count, brick_key);
        dict_set (rsp_ctx->dict, new_key, new_value);
        return 0;
}

int
glusterd_profile_volume_use_rsp_dict (dict_t *aggr, dict_t *rsp_dict)
{
        int     ret = 0;
        glusterd_pr_brick_rsp_conv_t rsp_ctx = {0};
        int32_t brick_count = 0;
        int32_t count = 0;
        dict_t  *ctx_dict = NULL;
        glusterd_op_t   op = GD_OP_NONE;

        GF_ASSERT (rsp_dict);

        ret = dict_get_int32 (rsp_dict, "count", &brick_count);
        if (ret) {
                ret = 0; //no bricks in the rsp
                goto out;
        }

        op = glusterd_op_get_op ();
        GF_ASSERT (GD_OP_PROFILE_VOLUME == op);
        if (aggr) {
                ctx_dict = aggr;

        } else {
                ctx_dict = glusterd_op_get_ctx ();
        }

        ret = dict_get_int32 (ctx_dict, "count", &count);
        rsp_ctx.count = count;
        rsp_ctx.dict = ctx_dict;
        dict_foreach (rsp_dict, _profile_volume_add_friend_rsp, &rsp_ctx);
        dict_del (ctx_dict, "count");
        ret = dict_set_int32 (ctx_dict, "count", count + brick_count);
out:
        return ret;
}

static int
glusterd_volume_status_add_peer_rsp (dict_t *this, char *key, data_t *value,
                                     void *data)
{
        glusterd_status_rsp_conv_t      *rsp_ctx = NULL;
        data_t                          *new_value = NULL;
        char                            brick_key[1024] = {0,};
        char                            new_key[1024] = {0,};
        int32_t                         index = 0;
        int32_t                         ret = 0;

        /* Skip the following keys, they are already present in the ctx_dict */
        /* Also, skip all the task related pairs. They will be added to the
         * ctx_dict later
         */
        if (!strcmp (key, "count") || !strcmp (key, "cmd") ||
            !strcmp (key, "brick-index-max") || !strcmp (key, "other-count") ||
            !strncmp (key, "task", 4))
                return 0;

        rsp_ctx = data;
        new_value = data_copy (value);
        GF_ASSERT (new_value);

        sscanf (key, "brick%d.%s", &index, brick_key);

        if (index > rsp_ctx->brick_index_max) {
                snprintf (new_key, sizeof (new_key), "brick%d.%s",
                          index + rsp_ctx->other_count, brick_key);
        } else {
                strncpy (new_key, key, sizeof (new_key));
                new_key[sizeof (new_key) - 1] = 0;
        }

        ret = dict_set (rsp_ctx->dict, new_key, new_value);
        if (ret)
                gf_log ("", GF_LOG_ERROR, "Unable to set key: %s in dict",
                        key);

        return 0;
}

static int
glusterd_volume_status_copy_tasks_to_ctx_dict (dict_t *this, char *key,
                                               data_t *value, void *data)
{
        int     ret = 0;
        dict_t  *ctx_dict = NULL;
        data_t  *new_value = NULL;

        if (strncmp (key, "task", 4))
                return 0;

        ctx_dict = data;
        GF_ASSERT (ctx_dict);

        new_value = data_copy (value);
        GF_ASSERT (new_value);

        ret = dict_set (ctx_dict, key, new_value);

        return ret;
}

int
glusterd_volume_status_aggregate_tasks_status (dict_t *ctx_dict,
                                               dict_t *rsp_dict)
{
        int             ret             = -1;
        xlator_t        *this           = NULL;
        int             local_count     = 0;
        int             remote_count    = 0;
        int             i               = 0;
        int             j               = 0;
        char            key[128]        = {0,};
        char            *task_type      = NULL;
        int             local_status    = 0;
        int             remote_status   = 0;
        char            *local_task_id  = NULL;
        char            *remote_task_id = NULL;

        GF_ASSERT (ctx_dict);
        GF_ASSERT (rsp_dict);

        this = THIS;
        GF_ASSERT (this);

        ret = dict_get_int32 (rsp_dict, "tasks", &remote_count);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to get remote task count");
                goto out;
        }
        /* Local count will not be present when this is called for the first
         * time with the origins rsp_dict
         */
        ret = dict_get_int32 (ctx_dict, "tasks", &local_count);
        if (ret) {
                ret = dict_foreach (rsp_dict,
                                glusterd_volume_status_copy_tasks_to_ctx_dict,
                                ctx_dict);
                if (ret)
                        gf_log (this->name, GF_LOG_ERROR, "Failed to copy tasks"
                                "to ctx_dict.");
                goto out;
        }

        if (local_count != remote_count) {
                gf_log (this->name, GF_LOG_ERROR, "Local tasks count (%d) and "
                        "remote tasks count (%d) do not match. Not aggregating "
                        "tasks status.", local_count, remote_count);
                ret = -1;
                goto out;
        }

        /* Update the tasks statuses. For every remote tasks, search for the
         * local task, and update the local task status based on the remote
         * status.
         */
        for (i = 0; i < remote_count; i++) {

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "task%d.type", i);
                ret = dict_get_str (rsp_dict, key, &task_type);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to get task typpe from rsp dict");
                        goto out;
                }

                /* Skip replace-brick status as it is going to be the same on
                 * all peers. rb_status is set by the replace brick commit
                 * function on all peers based on the replace brick command.
                 * We return the value of rb_status as the status for a
                 * replace-brick task in a 'volume status' command.
                 */
                if (!strcmp (task_type, "Replace brick"))
                        continue;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "task%d.status", i);
                ret = dict_get_int32 (rsp_dict, key, &remote_status);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to get task status from rsp dict");
                        goto out;
                }
                snprintf (key, sizeof (key), "task%d.id", i);
                ret = dict_get_str (rsp_dict, key, &remote_task_id);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to get task id from rsp dict");
                        goto out;
                }
                for (j = 0; j < local_count; j++) {
                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "task%d.id", j);
                        ret = dict_get_str (ctx_dict, key, &local_task_id);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to get local task-id");
                                goto out;
                        }

                        if (strncmp (remote_task_id, local_task_id,
                                     strlen (remote_task_id))) {
                                /* Quit if a matching local task is not found */
                                if (j == (local_count - 1)) {
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "Could not find matching local "
                                                "task for task %s",
                                                remote_task_id);
                                        goto out;
                                }
                                continue;
                        }

                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "task%d.status", j);
                        ret = dict_get_int32 (ctx_dict, key, &local_status);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to get local task status");
                                goto out;
                        }

                        /* Rebalance has 5 states,
                         * NOT_STARTED, STARTED, STOPPED, COMPLETE, FAILED
                         * The precedence used to determine the aggregate status
                         * is as below,
                         * STARTED > FAILED > STOPPED > COMPLETE > NOT_STARTED
                         */
                        /* TODO: Move this to a common place utilities that both
                         * CLI and glusterd need.
                         * Till then if the below algorithm is changed, change
                         * it in cli_xml_output_vol_rebalance_status in
                         * cli-xml-output.c
                         */
                        ret = 0;
                        int rank[] = {
                                [GF_DEFRAG_STATUS_STARTED] = 1,
                                [GF_DEFRAG_STATUS_FAILED] = 2,
                                [GF_DEFRAG_STATUS_STOPPED] = 3,
                                [GF_DEFRAG_STATUS_COMPLETE] = 4,
                                [GF_DEFRAG_STATUS_NOT_STARTED] = 5
                        };
                        if (rank[remote_status] <= rank[local_status])
                                        ret = dict_set_int32 (ctx_dict, key,
                                                              remote_status);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR, "Failed to "
                                        "update task status");
                                goto out;
                        }
                        break;
                }
        }

out:
        return ret;
}

gf_boolean_t
glusterd_status_has_tasks (int cmd) {
        if (((cmd & GF_CLI_STATUS_MASK) == GF_CLI_STATUS_NONE) &&
             (cmd & GF_CLI_STATUS_VOL))
                return _gf_true;
        return _gf_false;
}

int
glusterd_volume_status_copy_to_op_ctx_dict (dict_t *aggr, dict_t *rsp_dict)
{
        int                             ret = 0;
        glusterd_status_rsp_conv_t      rsp_ctx = {0};
        int32_t                         cmd = GF_CLI_STATUS_NONE;
        int32_t                         node_count = 0;
        int32_t                         other_count = 0;
        int32_t                         brick_index_max = -1;
        int32_t                         rsp_node_count = 0;
        int32_t                         rsp_other_count = 0;
        int                             vol_count = -1;
        int                             i = 0;
        dict_t                          *ctx_dict = NULL;
        char                            key[PATH_MAX] = {0,};
        char                            *volname = NULL;

        GF_ASSERT (rsp_dict);

        if (aggr) {
                ctx_dict = aggr;

        } else {
                ctx_dict = glusterd_op_get_ctx (GD_OP_STATUS_VOLUME);

        }

        ret = dict_get_int32 (ctx_dict, "cmd", &cmd);
        if (ret)
                goto out;

        if (cmd & GF_CLI_STATUS_ALL && is_origin_glusterd ()) {
                ret = dict_get_int32 (rsp_dict, "vol_count", &vol_count);
                if (ret == 0) {
                        ret = dict_set_int32 (ctx_dict, "vol_count",
                                              vol_count);
                        if (ret)
                                goto out;

                        for (i = 0; i < vol_count; i++) {
                                memset (key, 0, sizeof (key));
                                snprintf (key, sizeof (key), "vol%d", i);
                                ret = dict_get_str (rsp_dict, key, &volname);
                                if (ret)
                                        goto out;

                                ret = dict_set_str (ctx_dict, key, volname);
                                if (ret)
                                        goto out;
                        }
                }
        }

        if ((cmd & GF_CLI_STATUS_TASKS) != 0)
                goto aggregate_tasks;

        ret = dict_get_int32 (rsp_dict, "count", &rsp_node_count);
        if (ret) {
                ret = 0; //no bricks in the rsp
                goto out;
        }

        ret = dict_get_int32 (rsp_dict, "other-count", &rsp_other_count);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "Failed to get other count from rsp_dict");
                goto out;
        }

        ret = dict_get_int32 (ctx_dict, "count", &node_count);
        ret = dict_get_int32 (ctx_dict, "other-count", &other_count);
        if (!dict_get (ctx_dict, "brick-index-max")) {
                ret = dict_get_int32 (rsp_dict, "brick-index-max", &brick_index_max);
                if (ret)
                        goto out;
                ret = dict_set_int32 (ctx_dict, "brick-index-max", brick_index_max);
                if (ret)
                        goto out;

        } else {
                ret = dict_get_int32 (ctx_dict, "brick-index-max", &brick_index_max);
        }

        rsp_ctx.count = node_count;
        rsp_ctx.brick_index_max = brick_index_max;
        rsp_ctx.other_count = other_count;
        rsp_ctx.dict = ctx_dict;

        dict_foreach (rsp_dict, glusterd_volume_status_add_peer_rsp, &rsp_ctx);

        ret = dict_set_int32 (ctx_dict, "count", node_count + rsp_node_count);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "Failed to update node count");
                goto out;
        }

        ret = dict_set_int32 (ctx_dict, "other-count",
                              (other_count + rsp_other_count));
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "Failed to update other-count");
                goto out;
        }

aggregate_tasks:
        /* Tasks are only present for a normal status command for a volume or
         * for an explicit tasks status command for a volume
         */
        if (!(cmd & GF_CLI_STATUS_ALL) &&
            (((cmd & GF_CLI_STATUS_TASKS) != 0) ||
             glusterd_status_has_tasks (cmd)))
                ret = glusterd_volume_status_aggregate_tasks_status (ctx_dict,
                                                                     rsp_dict);

out:
        return ret;
}

int
glusterd_volume_rebalance_use_rsp_dict (dict_t *aggr, dict_t *rsp_dict)
{
        char                 key[256]      = {0,};
        char                *node_uuid     = NULL;
        char                *node_uuid_str = NULL;
        char                *volname       = NULL;
        dict_t              *ctx_dict      = NULL;
        double               elapsed_time  = 0;
        glusterd_conf_t     *conf          = NULL;
        glusterd_op_t        op            = GD_OP_NONE;
        glusterd_peerinfo_t *peerinfo      = NULL;
        glusterd_volinfo_t  *volinfo       = NULL;
        int                  ret           = 0;
        int32_t              index         = 0;
        int32_t              count         = 0;
        int32_t              current_index = 2;
        int32_t              value32       = 0;
        uint64_t             value         = 0;
        char                *peer_uuid_str = NULL;

        GF_ASSERT (rsp_dict);
        conf = THIS->private;

        op = glusterd_op_get_op ();
        GF_ASSERT ((GD_OP_REBALANCE == op) ||
                   (GD_OP_DEFRAG_BRICK_VOLUME == op));

        if (aggr) {
                ctx_dict = aggr;

        } else {
                ctx_dict = glusterd_op_get_ctx (op);

        }

        if (!ctx_dict)
                goto out;

        ret = dict_get_str (ctx_dict, "volname", &volname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        ret  = glusterd_volinfo_find (volname, &volinfo);

        if (ret)
                goto out;

        ret = dict_get_int32 (rsp_dict, "count", &index);
        if (ret)
                gf_log ("", GF_LOG_ERROR, "failed to get index");

        memset (key, 0, 256);
        snprintf (key, 256, "node-uuid-%d", index);
        ret = dict_get_str (rsp_dict, key, &node_uuid);
        if (!ret) {
                node_uuid_str = gf_strdup (node_uuid);

                /* Finding the index of the node-uuid in the peer-list */
                list_for_each_entry (peerinfo, &conf->peers, uuid_list) {
                        peer_uuid_str = gd_peer_uuid_str (peerinfo);
                        if (strcmp (peer_uuid_str, node_uuid_str) == 0)
                                break;

                        current_index++;
                }

                /* Setting the largest index value as the total count. */
                ret = dict_get_int32 (ctx_dict, "count", &count);
                if (count < current_index) {
                    ret = dict_set_int32 (ctx_dict, "count", current_index);
                    if (ret)
                            gf_log ("", GF_LOG_ERROR, "Failed to set count");
                }

                /* Setting the same index for the node, as is in the peerlist.*/
                memset (key, 0, 256);
                snprintf (key, 256, "node-uuid-%d", current_index);
                ret = dict_set_dynstr (ctx_dict, key, node_uuid_str);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_DEBUG,
                                "failed to set node-uuid");
                }
        }

        snprintf (key, 256, "files-%d", index);
        ret = dict_get_uint64 (rsp_dict, key, &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "files-%d", current_index);
                ret = dict_set_uint64 (ctx_dict, key, value);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_DEBUG,
                                "failed to set the file count");
                }
        }

        memset (key, 0, 256);
        snprintf (key, 256, "size-%d", index);
        ret = dict_get_uint64 (rsp_dict, key, &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "size-%d", current_index);
                ret = dict_set_uint64 (ctx_dict, key, value);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_DEBUG,
                                "failed to set the size of migration");
                }
        }

        memset (key, 0, 256);
        snprintf (key, 256, "lookups-%d", index);
        ret = dict_get_uint64 (rsp_dict, key, &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "lookups-%d", current_index);
                ret = dict_set_uint64 (ctx_dict, key, value);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_DEBUG,
                                "failed to set lookuped file count");
                }
        }

        memset (key, 0, 256);
        snprintf (key, 256, "status-%d", index);
        ret = dict_get_int32 (rsp_dict, key, &value32);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "status-%d", current_index);
                ret = dict_set_int32 (ctx_dict, key, value32);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_DEBUG,
                                "failed to set status");
                }
        }

        memset (key, 0, 256);
        snprintf (key, 256, "failures-%d", index);
        ret = dict_get_uint64 (rsp_dict, key, &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "failures-%d", current_index);
                ret = dict_set_uint64 (ctx_dict, key, value);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_DEBUG,
                                "failed to set failure count");
                }
        }

        memset (key, 0, 256);
        snprintf (key, 256, "skipped-%d", index);
        ret = dict_get_uint64 (rsp_dict, key, &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "skipped-%d", current_index);
                ret = dict_set_uint64 (ctx_dict, key, value);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_DEBUG,
                                "failed to set skipped count");
                }
        }
        memset (key, 0, 256);
        snprintf (key, 256, "run-time-%d", index);
        ret = dict_get_double (rsp_dict, key, &elapsed_time);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "run-time-%d", current_index);
                ret = dict_set_double (ctx_dict, key, elapsed_time);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_DEBUG,
                                "failed to set run-time");
                }
        }

        ret = 0;

out:
        return ret;
}

int
glusterd_sys_exec_output_rsp_dict (dict_t *dst, dict_t *src)
{
        char           output_name[PATH_MAX] = "";
        char          *output = NULL;
        int            ret      = 0;
        int            i      = 0;
        int            len    = 0;
        int            src_output_count      = 0;
        int            dst_output_count      = 0;

        if (!dst || !src) {
                gf_log ("", GF_LOG_ERROR, "Source or Destination "
                        "dict is empty.");
                goto out;
        }

        ret = dict_get_int32 (dst, "output_count", &dst_output_count);

        ret = dict_get_int32 (src, "output_count", &src_output_count);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "No output from source");
                ret = 0;
                goto out;
        }

        for (i = 1; i <= src_output_count; i++) {
                len = snprintf (output_name, sizeof(output_name) - 1,
                                "output_%d", i);
                output_name[len] = '\0';
                ret = dict_get_str (src, output_name, &output);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Unable to fetch %s",
                                output_name);
                        goto out;
                }

                len = snprintf (output_name, sizeof(output_name) - 1,
                                "output_%d", i+dst_output_count);
                output_name[len] = '\0';
                ret = dict_set_dynstr (dst, output_name, gf_strdup (output));
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Unable to set %s",
                                output_name);
                        goto out;
                }
        }

        ret = dict_set_int32 (dst, "output_count",
                              dst_output_count+src_output_count);
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_use_rsp_dict (dict_t *aggr, dict_t *rsp_dict)
{
        int            ret      = 0;
        glusterd_op_t  op       = GD_OP_NONE;

        op = glusterd_op_get_op ();
        GF_ASSERT (aggr);
        GF_ASSERT (rsp_dict);

        if (!aggr)
                goto out;
        dict_copy (rsp_dict, aggr);
out:
        return ret;
}

int
glusterd_volume_heal_use_rsp_dict (dict_t *aggr, dict_t *rsp_dict)
{
        int            ret      = 0;
        dict_t        *ctx_dict = NULL;
        glusterd_op_t  op       = GD_OP_NONE;

        GF_ASSERT (rsp_dict);

        op = glusterd_op_get_op ();
        GF_ASSERT (GD_OP_HEAL_VOLUME == op);

        if (aggr) {
                ctx_dict = aggr;

        } else {
                ctx_dict = glusterd_op_get_ctx (op);
        }

        if (!ctx_dict)
                goto out;
        dict_copy (rsp_dict, ctx_dict);
out:
        return ret;
}

int
_profile_volume_add_brick_rsp (dict_t *this, char *key, data_t *value,
                             void *data)
{
        char    new_key[256] = {0};
        glusterd_pr_brick_rsp_conv_t *rsp_ctx = NULL;
        data_t  *new_value = NULL;

        rsp_ctx = data;
        new_value = data_copy (value);
        GF_ASSERT (new_value);
        snprintf (new_key, sizeof (new_key), "%d-%s", rsp_ctx->count, key);
        dict_set (rsp_ctx->dict, new_key, new_value);
        return 0;
}

int
glusterd_volume_quota_copy_to_op_ctx_dict (dict_t *dict, dict_t *rsp_dict)
{
        int        ret            = -1;
        int        i              = 0;
        int        count          = 0;
        int        rsp_dict_count = 0;
        char      *uuid_str       = NULL;
        char      *uuid_str_dup   = NULL;
        char       key[256]       = {0,};
        xlator_t  *this           = NULL;
        int        type           = GF_QUOTA_OPTION_TYPE_NONE;

        this = THIS;
        GF_ASSERT (this);

        ret = dict_get_int32 (dict, "type", &type);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get quota opcode");
                goto out;
        }

        if ((type != GF_QUOTA_OPTION_TYPE_LIMIT_USAGE) &&
            (type != GF_QUOTA_OPTION_TYPE_REMOVE)) {
                dict_copy (rsp_dict, dict);
                ret = 0;
                goto out;
        }

        ret = dict_get_int32 (rsp_dict, "count", &rsp_dict_count);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get the count of "
                        "gfids from the rsp dict");
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &count);
        if (ret)
                /* The key "count" is absent in op_ctx when this function is
                 * called after self-staging on the originator. This must not
                 * be treated as error.
                 */
                gf_log (this->name, GF_LOG_DEBUG, "Failed to get count of gfids"
                        " from req dict. This could be because count is not yet"
                        " copied from rsp_dict into op_ctx");

        for (i = 0; i < rsp_dict_count; i++) {
                snprintf (key, sizeof(key)-1, "gfid%d", i);

                ret = dict_get_str (rsp_dict, key, &uuid_str);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to get gfid "
                                "from rsp dict");
                        goto out;
                }

                snprintf (key, sizeof (key)-1, "gfid%d", i + count);

                uuid_str_dup = gf_strdup (uuid_str);
                if (!uuid_str_dup) {
                        ret = -1;
                        goto out;
                }

                ret = dict_set_dynstr (dict, key, uuid_str_dup);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to set gfid "
                                "from rsp dict into req dict");
                        GF_FREE (uuid_str_dup);
                        goto out;
                }
        }

        ret = dict_set_int32 (dict, "count", rsp_dict_count + count);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,  "Failed to set aggregated "
                        "count in req dict");
                goto out;
        }

out:
        return ret;
}

int
glusterd_profile_volume_brick_rsp (void *pending_entry,
                                   dict_t *rsp_dict, dict_t *op_ctx,
                                   char **op_errstr, gd_node_type type)
{
        int                             ret = 0;
        glusterd_pr_brick_rsp_conv_t    rsp_ctx = {0};
        int32_t                         count = 0;
        char                            brick[PATH_MAX+1024] = {0};
        char                            key[256] = {0};
        char                            *full_brick = NULL;
        glusterd_brickinfo_t            *brickinfo = NULL;
        xlator_t                        *this = NULL;
        glusterd_conf_t                 *priv = NULL;

        GF_ASSERT (rsp_dict);
        GF_ASSERT (op_ctx);
        GF_ASSERT (op_errstr);
        GF_ASSERT (pending_entry);

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_int32 (op_ctx, "count", &count);
        if (ret) {
                count = 1;
        } else {
                count++;
        }
        snprintf (key, sizeof (key), "%d-brick", count);
        if (type == GD_NODE_BRICK) {
                brickinfo = pending_entry;
                snprintf (brick, sizeof (brick), "%s:%s", brickinfo->hostname,
                          brickinfo->path);
        } else if (type == GD_NODE_NFS) {
                snprintf (brick, sizeof (brick), "%s", uuid_utoa (MY_UUID));
        }
        full_brick = gf_strdup (brick);
        GF_ASSERT (full_brick);
        ret = dict_set_dynstr (op_ctx, key, full_brick);

        rsp_ctx.count = count;
        rsp_ctx.dict = op_ctx;
        dict_foreach (rsp_dict, _profile_volume_add_brick_rsp, &rsp_ctx);
        dict_del (op_ctx, "count");
        ret = dict_set_int32 (op_ctx, "count", count);
        return ret;
}

//input-key: <replica-id>:<child-id>-*
//output-key: <brick-id>-*
int
_heal_volume_add_shd_rsp (dict_t *this, char *key, data_t *value, void *data)
{
        char                            new_key[256] = {0,};
        char                            int_str[16] = {0};
        data_t                          *new_value = NULL;
        char                            *rxl_end = NULL;
        char                            *rxl_child_end = NULL;
        glusterd_volinfo_t              *volinfo = NULL;
        int                             rxl_id = 0;
        int                             rxl_child_id = 0;
        int                             brick_id = 0;
        int                             int_len = 0;
        int                             ret = 0;
        glusterd_heal_rsp_conv_t        *rsp_ctx = NULL;
        glusterd_brickinfo_t            *brickinfo = NULL;

        rsp_ctx = data;
        rxl_end = strchr (key, '-');
        if (!rxl_end)
                goto out;

        int_len = strlen (key) - strlen (rxl_end);
        strncpy (int_str, key, int_len);
        int_str[int_len] = '\0';
        ret = gf_string2int (int_str, &rxl_id);
        if (ret)
                goto out;

        rxl_child_end = strchr (rxl_end + 1, '-');
        if (!rxl_child_end)
                goto out;

        int_len = strlen (rxl_end) - strlen (rxl_child_end) - 1;
        strncpy (int_str, rxl_end + 1, int_len);
        int_str[int_len] = '\0';
        ret = gf_string2int (int_str, &rxl_child_id);
        if (ret)
                goto out;

        volinfo = rsp_ctx->volinfo;
        brick_id = rxl_id * volinfo->replica_count + rxl_child_id;

        if (!strcmp (rxl_child_end, "-status")) {
                brickinfo = glusterd_get_brickinfo_by_position (volinfo,
                                                                brick_id);
                if (!brickinfo)
                        goto out;
                if (!glusterd_is_local_brick (rsp_ctx->this, volinfo,
                                              brickinfo))
                        goto out;
        }
        new_value = data_copy (value);
        snprintf (new_key, sizeof (new_key), "%d%s", brick_id, rxl_child_end);
        dict_set (rsp_ctx->dict, new_key, new_value);

out:
        return 0;
}

int
_heal_volume_add_shd_rsp_of_statistics (dict_t *this, char *key, data_t
                                             *value, void *data)
{
        char                            new_key[256] = {0,};
        char                            int_str[16] = {0,};
        char                            key_begin_string[128] = {0,};
        data_t                          *new_value = NULL;
        char                            *rxl_end = NULL;
        char                            *rxl_child_end = NULL;
        glusterd_volinfo_t              *volinfo = NULL;
        char                            *key_begin_str = NULL;
        int                             rxl_id = 0;
        int                             rxl_child_id = 0;
        int                             brick_id = 0;
        int                             int_len = 0;
        int                             ret = 0;
        glusterd_heal_rsp_conv_t        *rsp_ctx = NULL;
        glusterd_brickinfo_t            *brickinfo = NULL;

        rsp_ctx = data;
        key_begin_str = strchr (key, '-');
        if (!key_begin_str)
                goto out;

        int_len = strlen (key) - strlen (key_begin_str);
        strncpy (key_begin_string, key, int_len);
        key_begin_string[int_len] = '\0';

        rxl_end = strchr (key_begin_str + 1, '-');
        if (!rxl_end)
                goto out;

        int_len = strlen (key_begin_str) - strlen (rxl_end) - 1;
        strncpy (int_str, key_begin_str + 1, int_len);
        int_str[int_len] = '\0';
        ret = gf_string2int (int_str, &rxl_id);
        if (ret)
                goto out;


        rxl_child_end = strchr (rxl_end + 1, '-');
        if (!rxl_child_end)
                goto out;

        int_len = strlen (rxl_end) - strlen (rxl_child_end) - 1;
        strncpy (int_str, rxl_end + 1, int_len);
        int_str[int_len] = '\0';
        ret = gf_string2int (int_str, &rxl_child_id);
        if (ret)
                goto out;

        volinfo = rsp_ctx->volinfo;
        brick_id = rxl_id * volinfo->replica_count + rxl_child_id;

        brickinfo = glusterd_get_brickinfo_by_position (volinfo, brick_id);
        if (!brickinfo)
                goto out;
        if (!glusterd_is_local_brick (rsp_ctx->this, volinfo, brickinfo))
                goto out;

        new_value = data_copy (value);
        snprintf (new_key, sizeof (new_key), "%s-%d%s", key_begin_string,
                  brick_id, rxl_child_end);
        dict_set (rsp_ctx->dict, new_key, new_value);

out:
        return 0;

}

int
glusterd_heal_volume_brick_rsp (dict_t *req_dict, dict_t *rsp_dict,
                                dict_t *op_ctx, char **op_errstr)
{
        int                             ret = 0;
        glusterd_heal_rsp_conv_t        rsp_ctx = {0};
        char                            *volname = NULL;
        glusterd_volinfo_t              *volinfo = NULL;
        int                             heal_op = -1;

        GF_ASSERT (rsp_dict);
        GF_ASSERT (op_ctx);
        GF_ASSERT (op_errstr);

        ret = dict_get_str (req_dict, "volname", &volname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        ret = dict_get_int32 (req_dict, "heal-op", &heal_op);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get heal_op");
                goto out;
        }


        ret  = glusterd_volinfo_find (volname, &volinfo);

        if (ret)
                goto out;

        rsp_ctx.dict = op_ctx;
        rsp_ctx.volinfo = volinfo;
        rsp_ctx.this = THIS;
        if (heal_op == GF_AFR_OP_STATISTICS)
                dict_foreach (rsp_dict, _heal_volume_add_shd_rsp_of_statistics,
                              &rsp_ctx);
        else
                dict_foreach (rsp_dict, _heal_volume_add_shd_rsp, &rsp_ctx);


out:
        return ret;
}

int
_status_volume_add_brick_rsp (dict_t *this, char *key, data_t *value,
                              void *data)
{
        char                            new_key[256] = {0,};
        data_t                          *new_value = 0;
        glusterd_pr_brick_rsp_conv_t    *rsp_ctx = NULL;

        rsp_ctx = data;
        new_value = data_copy (value);
        snprintf (new_key, sizeof (new_key), "brick%d.%s", rsp_ctx->count, key);
        dict_set (rsp_ctx->dict, new_key, new_value);

        return 0;
}

int
glusterd_status_volume_brick_rsp (dict_t *rsp_dict, dict_t *op_ctx,
                                  char **op_errstr)
{
        int                             ret = 0;
        glusterd_pr_brick_rsp_conv_t    rsp_ctx = {0};
        int32_t                         count = 0;
        int                             index = 0;

        GF_ASSERT (rsp_dict);
        GF_ASSERT (op_ctx);
        GF_ASSERT (op_errstr);

        ret = dict_get_int32 (op_ctx, "count", &count);
        if (ret) {
                count = 0;
        } else {
                count++;
        }
        ret = dict_get_int32 (rsp_dict, "index", &index);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Couldn't get node index");
                goto out;
        }
        dict_del (rsp_dict, "index");

        rsp_ctx.count = index;
        rsp_ctx.dict = op_ctx;
        dict_foreach (rsp_dict, _status_volume_add_brick_rsp, &rsp_ctx);
        ret = dict_set_int32 (op_ctx, "count", count);

out:
        return ret;
}

int
glusterd_defrag_volume_node_rsp (dict_t *req_dict, dict_t *rsp_dict,
                                 dict_t *op_ctx)
{
        int                             ret = 0;
        char                            *volname = NULL;
        glusterd_volinfo_t              *volinfo = NULL;
        char                            key[256] = {0,};
        int32_t                         i = 0;
        char                            buf[1024] = {0,};
        char                            *node_str = NULL;
        glusterd_conf_t                 *priv = NULL;

        priv = THIS->private;
        GF_ASSERT (req_dict);

        ret = dict_get_str (req_dict, "volname", &volname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        ret  = glusterd_volinfo_find (volname, &volinfo);

        if (ret)
                goto out;

        if (rsp_dict) {
                ret = glusterd_defrag_volume_status_update (volinfo,
                                                            rsp_dict);
        }

        if (!op_ctx) {
                dict_copy (rsp_dict, op_ctx);
                goto out;
        }

        ret = dict_get_int32 (op_ctx, "count", &i);
        i++;

        ret = dict_set_int32 (op_ctx, "count", i);
        if (ret)
                gf_log (THIS->name, GF_LOG_ERROR, "Failed to set count");

        snprintf (buf, 1024, "%s", uuid_utoa (MY_UUID));
        node_str = gf_strdup (buf);

        snprintf (key, 256, "node-uuid-%d",i);
        ret = dict_set_dynstr (op_ctx, key, node_str);
        if (ret)
                gf_log (THIS->name, GF_LOG_ERROR,
                        "failed to set node-uuid");

        memset (key, 0 , 256);
        snprintf (key, 256, "files-%d", i);
        ret = dict_set_uint64 (op_ctx, key, volinfo->rebal.rebalance_files);
        if (ret)
                gf_log (THIS->name, GF_LOG_ERROR,
                        "failed to set file count");

        memset (key, 0 , 256);
        snprintf (key, 256, "size-%d", i);
        ret = dict_set_uint64 (op_ctx, key, volinfo->rebal.rebalance_data);
        if (ret)
                gf_log (THIS->name, GF_LOG_ERROR,
                        "failed to set size of xfer");

        memset (key, 0 , 256);
        snprintf (key, 256, "lookups-%d", i);
        ret = dict_set_uint64 (op_ctx, key, volinfo->rebal.lookedup_files);
        if (ret)
                gf_log (THIS->name, GF_LOG_ERROR,
                        "failed to set lookedup file count");

        memset (key, 0 , 256);
        snprintf (key, 256, "status-%d", i);
        ret = dict_set_int32 (op_ctx, key, volinfo->rebal.defrag_status);
        if (ret)
                gf_log (THIS->name, GF_LOG_ERROR,
                        "failed to set status");

        memset (key, 0 , 256);
        snprintf (key, 256, "failures-%d", i);
        ret = dict_set_uint64 (op_ctx, key, volinfo->rebal.rebalance_failures);
        if (ret)
                gf_log (THIS->name, GF_LOG_ERROR,
                        "failed to set failure count");

        memset (key, 0 , 256);
        snprintf (key, 256, "skipped-%d", i);
        ret = dict_set_uint64 (op_ctx, key, volinfo->rebal.skipped_files);
        if (ret)
                gf_log (THIS->name, GF_LOG_ERROR,
                        "failed to set skipped count");

        memset (key, 0, 256);
        snprintf (key, 256, "run-time-%d", i);
        ret = dict_set_double (op_ctx, key, volinfo->rebal.rebalance_time);
        if (ret)
                gf_log (THIS->name, GF_LOG_ERROR,
                        "failed to set run-time");

out:
        return ret;
}
int32_t
glusterd_handle_node_rsp (dict_t *req_dict, void *pending_entry,
                          glusterd_op_t op, dict_t *rsp_dict, dict_t *op_ctx,
                          char **op_errstr, gd_node_type type)
{
        int                     ret = 0;

        GF_ASSERT (op_errstr);

        switch (op) {
        case GD_OP_PROFILE_VOLUME:
                ret = glusterd_profile_volume_brick_rsp (pending_entry,
                                                         rsp_dict, op_ctx,
                                                         op_errstr, type);
                break;
        case GD_OP_STATUS_VOLUME:
                ret = glusterd_status_volume_brick_rsp (rsp_dict, op_ctx,
                                                        op_errstr);
                break;

        case GD_OP_DEFRAG_BRICK_VOLUME:
                glusterd_defrag_volume_node_rsp (req_dict,
                                                 rsp_dict, op_ctx);
                break;

        case GD_OP_HEAL_VOLUME:
                ret = glusterd_heal_volume_brick_rsp (req_dict, rsp_dict,
                                                      op_ctx, op_errstr);
                break;
        default:
                break;
        }

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

/* Should be used only when an operation is in progress, as that is the only
 * time a lock_owner is set
 */
gf_boolean_t
is_origin_glusterd ()
{
        int     ret = 0;
        uuid_t  lock_owner = {0,};

        ret = glusterd_get_lock_owner (&lock_owner);
        if (ret)
                return _gf_false;

        return (uuid_compare (MY_UUID, lock_owner) == 0);
}

int
glusterd_generate_and_set_task_id (dict_t *dict, char *key)
{
        int             ret = -1;
        uuid_t          task_id = {0,};
        char            *uuid_str = NULL;
        xlator_t        *this = NULL;

        GF_ASSERT (dict);

        this = THIS;
        GF_ASSERT (this);

        uuid_generate (task_id);
        uuid_str = gf_strdup (uuid_utoa (task_id));
        if (!uuid_str) {
                ret = -1;
                goto out;
        }

        ret = dict_set_dynstr (dict, key, uuid_str);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set %s in dict",
                        key);
                goto out;
        }
        gf_log (this->name, GF_LOG_INFO, "Generated task-id %s for key %s",
                uuid_str, key);

out:
        if (ret)
                GF_FREE (uuid_str);
        return ret;
}

int
glusterd_copy_uuid_to_dict (uuid_t uuid, dict_t *dict, char *key)
{
        int             ret = -1;
        char            tmp_str[40] = {0,};
        char            *task_id_str = NULL;

        GF_ASSERT (dict);
        GF_ASSERT (key);

        uuid_unparse (uuid, tmp_str);
        task_id_str = gf_strdup (tmp_str);
        if (!task_id_str)
                return -1;

        ret = dict_set_dynstr (dict, key, task_id_str);
        if (ret) {
                GF_FREE (task_id_str);
                gf_log (THIS->name, GF_LOG_ERROR,
                        "Error setting uuid in dict with key %s", key);
        }

        return 0;
}

int
_update_volume_op_versions (dict_t *this, char *key, data_t *value, void *data)
{
        int                op_version = 0;
        glusterd_volinfo_t *ctx       = NULL;
        gf_boolean_t       enabled    = _gf_true;
        int                ret        = -1;

        GF_ASSERT (data);
        ctx = data;

        op_version = glusterd_get_op_version_for_key (key);

        if (gd_is_xlator_option (key) || gd_is_boolean_option (key)) {
                ret = gf_string2boolean (value->data, &enabled);
                if (ret)
                        return 0;

                if (!enabled)
                        return 0;
        }

        if (op_version > ctx->op_version)
                ctx->op_version = op_version;

        if (gd_is_client_option (key) &&
            (op_version > ctx->client_op_version))
                ctx->client_op_version = op_version;

        return 0;
}

void
gd_update_volume_op_versions (glusterd_volinfo_t *volinfo)
{
        glusterd_conf_t *conf = NULL;
        gf_boolean_t    ob_enabled = _gf_false;

        GF_ASSERT (volinfo);

        conf = THIS->private;
        GF_ASSERT (conf);

        /* Reset op-versions to minimum */
        volinfo->op_version = 1;
        volinfo->client_op_version = 1;

        dict_foreach (volinfo->dict, _update_volume_op_versions, volinfo);

        /* Special case for open-behind
         * If cluster op-version >= 2 and open-behind hasn't been explicitly
         * disabled, volume op-versions must be updated to account for it
         */

        /* TODO: Remove once we have a general way to update automatically
         * enabled features
         */
        if (conf->op_version >= 2) {
                ob_enabled = dict_get_str_boolean (volinfo->dict,
                                                   "performance.open-behind",
                                                   _gf_true);
                if (ob_enabled) {

                        if (volinfo->op_version < 2)
                                volinfo->op_version = 2;
                        if (volinfo->client_op_version < 2)
                                volinfo->client_op_version = 2;
                }
        }

        return;
}

/* A task is committed/completed once the task-id for it is cleared */
gf_boolean_t
gd_is_remove_brick_committed (glusterd_volinfo_t *volinfo)
{
        GF_ASSERT (volinfo);

        if ((GD_OP_REMOVE_BRICK == volinfo->rebal.op) &&
            !uuid_is_null (volinfo->rebal.rebalance_id))
                        return _gf_false;

        return _gf_true;
}

gf_boolean_t
glusterd_are_vol_all_peers_up (glusterd_volinfo_t *volinfo,
                               struct list_head *peers,
                               char **down_peerstr)
{
        glusterd_peerinfo_t   *peerinfo  = NULL;
        glusterd_brickinfo_t  *brickinfo = NULL;
        gf_boolean_t           ret       = _gf_false;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (!uuid_compare (brickinfo->uuid, MY_UUID))
                        continue;

                list_for_each_entry (peerinfo, peers, uuid_list) {
                        if (uuid_compare (peerinfo->uuid, brickinfo->uuid))
                                continue;

                        /*Found peer who owns the brick, return false
                         * if peer is not connected or not friend */
                        if (!(peerinfo->connected) ||
                           (peerinfo->state.state !=
                             GD_FRIEND_STATE_BEFRIENDED)) {
                                *down_peerstr = gf_strdup (peerinfo->hostname);
                                gf_log ("", GF_LOG_DEBUG, "Peer %s is down. ",
                                        peerinfo->hostname);
                                goto out;
                        }
                }
       }

        ret = _gf_true;
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

gf_boolean_t
glusterd_is_status_tasks_op (glusterd_op_t op, dict_t *dict)
{
        int           ret             = -1;
        uint32_t      cmd             = GF_CLI_STATUS_NONE;
        gf_boolean_t  is_status_tasks = _gf_false;

        if (op != GD_OP_STATUS_VOLUME)
                goto out;

        ret = dict_get_uint32 (dict, "cmd", &cmd);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Failed to get opcode");
                goto out;
        }

        if (cmd & GF_CLI_STATUS_TASKS)
                is_status_tasks = _gf_true;

out:
        return is_status_tasks;
}

/* Tells if rebalance needs to be started for the given volume on the peer
 *
 * Rebalance should be started on a peer only if an involved brick is present on
 * the peer.
 *
 * For a normal rebalance, if any one brick of the given volume is present on
 * the peer, the rebalance process should be started.
 *
 * For a rebalance as part of a remove-brick operation, the rebalance process
 * should be started only if one of the bricks being removed is present on the
 * peer
 */
gf_boolean_t
gd_should_i_start_rebalance  (glusterd_volinfo_t *volinfo) {
        gf_boolean_t         retval     = _gf_false;
        int                  ret        = -1;
        glusterd_brickinfo_t *brick     = NULL;
        int                  count      = 0;
        int                  i          = 0;
        char                 key[1023]  = {0,};
        char                 *brickname = NULL;


        switch (volinfo->rebal.op) {
        case GD_OP_REBALANCE:
                list_for_each_entry (brick, &volinfo->bricks, brick_list) {
                        if (uuid_compare (MY_UUID, brick->uuid) == 0) {
                                retval = _gf_true;
                                break;
                        }
                }
                break;
        case GD_OP_REMOVE_BRICK:
                ret = dict_get_int32 (volinfo->rebal.dict, "count", &count);
                if (ret) {
                        goto out;
                }
                for (i = 1; i <= count; i++) {
                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "brick%d", i);
                        ret = dict_get_str (volinfo->rebal.dict, key,
                                            &brickname);
                        if (ret)
                                goto out;
                        ret = glusterd_volume_brickinfo_get_by_brick (brickname,
                                                                      volinfo,
                                                                      &brick);
                        if (ret)
                                goto out;
                        if (uuid_compare (MY_UUID, brick->uuid) == 0) {
                                retval = _gf_true;
                                break;
                        }
                }
                break;
        default:
                break;
        }

out:
        return retval;
}

int
glusterd_is_volume_quota_enabled (glusterd_volinfo_t *volinfo)
{
        return (glusterd_volinfo_get_boolean (volinfo, VKEY_FEATURES_QUOTA));
}

int
glusterd_validate_and_set_gfid (dict_t *op_ctx, dict_t *req_dict,
                                char **op_errstr)
{
        int        ret           = -1;
        int        count         = 0;
        int        i             = 0;
        int        op_code       = GF_QUOTA_OPTION_TYPE_NONE;
        uuid_t     uuid1         = {0};
        uuid_t     uuid2         = {0,};
        char      *path          = NULL;
        char       key[256]      = {0,};
        char      *uuid1_str     = NULL;
        char      *uuid1_str_dup = NULL;
        char      *uuid2_str     = NULL;
        xlator_t  *this          = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = dict_get_int32 (op_ctx, "type", &op_code);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get quota opcode");
                goto out;
        }

        if ((op_code != GF_QUOTA_OPTION_TYPE_LIMIT_USAGE) &&
            (op_code != GF_QUOTA_OPTION_TYPE_REMOVE)) {
                ret = 0;
                goto out;
        }

        ret = dict_get_str (op_ctx, "path", &path);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get path");
                goto out;
        }

        ret = dict_get_int32 (op_ctx, "count", &count);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get count");
                goto out;
        }

        /* If count is 0, fail the command with ENOENT.
         *
         * If count is 1, treat gfid0 as the gfid on which the operation
         * is to be performed and resume the command.
         *
         * if count > 1, get the 0th gfid from the op_ctx and,
         * compare it with the remaining 'count -1' gfids.
         * If they are found to be the same, set gfid0 in the op_ctx and
         * resume the operation, else error out.
         */

        if (count == 0) {
                gf_asprintf (op_errstr, "Failed to get trusted.gfid attribute "
                             "on path %s. Reason : %s", path,
                             strerror (ENOENT));
                ret = -1;
                goto out;
        }

        snprintf (key, sizeof (key) - 1, "gfid%d", 0);

        ret = dict_get_str (op_ctx, key, &uuid1_str);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get key '%s'",
                        key);
                goto out;
        }

        uuid_parse (uuid1_str, uuid1);

        for (i = 1; i < count; i++) {
                snprintf (key, sizeof (key)-1, "gfid%d", i);

                ret = dict_get_str (op_ctx, key, &uuid2_str);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to get key "
                                "'%s'", key);
                        goto out;
                }

                uuid_parse (uuid2_str, uuid2);

                if (uuid_compare (uuid1, uuid2)) {
                        gf_asprintf (op_errstr, "gfid mismatch between %s and "
                                     "%s for path %s", uuid1_str, uuid2_str,
                                     path);
                        ret = -1;
                        goto out;
                }
        }

        if (i == count) {
                uuid1_str_dup = gf_strdup (uuid1_str);
                if (!uuid1_str_dup) {
                        ret = -1;
                        goto out;
                }

                ret = dict_set_dynstr (req_dict, "gfid", uuid1_str_dup);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to set gfid");
                        GF_FREE (uuid1_str_dup);
                        goto out;
                }
        } else {
                gf_log (this->name, GF_LOG_ERROR, "Failed to iterate through %d"
                        " entries in the req dict", count);
                ret = -1;
                goto out;
        }

        ret = 0;
out:
        return ret;
}

void
glusterd_clean_up_quota_store (glusterd_volinfo_t *volinfo)
{
        char      voldir[PATH_MAX]         = {0,};
        char      quota_confpath[PATH_MAX] = {0,};
        char      cksum_path[PATH_MAX]     = {0,};
        xlator_t  *this                    = NULL;
        glusterd_conf_t *conf              = NULL;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        GLUSTERD_GET_VOLUME_DIR (voldir, volinfo, conf);

        snprintf (quota_confpath, sizeof (quota_confpath), "%s/%s", voldir,
                  GLUSTERD_VOLUME_QUOTA_CONFIG);
        snprintf (cksum_path, sizeof (cksum_path), "%s/%s", voldir,
                  GLUSTERD_VOL_QUOTA_CKSUM_FILE);

        unlink (quota_confpath);
        unlink (cksum_path);

        gf_store_handle_destroy (volinfo->quota_conf_shandle);
        volinfo->quota_conf_shandle = NULL;
        volinfo->quota_conf_version = 0;

}

#define QUOTA_CONF_HEADER                                                \
        "GlusterFS Quota conf | version: v%d.%d\n"

int
glusterd_store_quota_conf_skip_header (xlator_t *this, int fd)
{
        char buf[PATH_MAX] = {0,};

        snprintf (buf, sizeof(buf)-1, QUOTA_CONF_HEADER, 1, 1);
        return gf_skip_header_section (fd, strlen (buf));
}

int
glusterd_store_quota_conf_stamp_header (xlator_t *this, int fd)
{
        char buf[PATH_MAX]  = {0,};
        int  buf_len        = 0;
        ssize_t  ret        = -1;
        ssize_t  written    = 0;

        snprintf (buf, sizeof(buf)-1, QUOTA_CONF_HEADER, 1, 1);
        buf_len = strlen (buf);
        for (written = 0; written != buf_len; written += ret) {
                ret = write (fd, buf + written, buf_len - written);
                if (ret == -1) {
                        goto out;
                }
        }

        ret = 0;
out:
        return ret;
}

int
glusterd_remove_auxiliary_mount (char *volname)
{
        int       ret                = -1;
        runner_t  runner             = {0,};
        char      mountdir[PATH_MAX] = {0,};
        char      pidfile[PATH_MAX]  = {0,};
        xlator_t *this               = NULL;

        this = THIS;
        GF_ASSERT (this);

        GLUSTERFS_GET_AUX_MOUNT_PIDFILE (pidfile, volname);

        if (!gf_is_service_running (pidfile, NULL)) {
                gf_log (this->name, GF_LOG_DEBUG, "Aux mount of volume %s "
                        "absent, hence returning", volname);
                return 0;
        }

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

        rmdir (mountdir);
        return ret;
}

/* Stops the rebalance process of the given volume
 */
int
gd_stop_rebalance_process (glusterd_volinfo_t *volinfo)
{
        int              ret               = -1;
        xlator_t        *this              = NULL;
        glusterd_conf_t *conf              = NULL;
        char             pidfile[PATH_MAX] = {0,};

        GF_ASSERT (volinfo);

        this = THIS;
        GF_ASSERT (this);

        conf = this->private;
        GF_ASSERT (conf);

        GLUSTERD_GET_DEFRAG_PID_FILE (pidfile, volinfo, conf);
        ret = glusterd_service_stop ("rebalance", pidfile, SIGTERM, _gf_true);

        return ret;
}

rpc_clnt_t *
glusterd_rpc_clnt_unref (glusterd_conf_t *conf, rpc_clnt_t *rpc)
{
        rpc_clnt_t *ret = NULL;

        GF_ASSERT (conf);
        GF_ASSERT (rpc);
        synclock_unlock (&conf->big_lock);
        ret = rpc_clnt_unref (rpc);
        synclock_lock (&conf->big_lock);

        return ret;
}

