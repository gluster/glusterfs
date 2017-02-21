/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include <inttypes.h>

#if defined(GF_LINUX_HOST_OS)
#include <mntent.h>
#else
#include "mntent_compat.h"
#endif
#include <dlfcn.h>
#if (HAVE_LIB_XML)
#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>
#endif

#include "globals.h"
#include "glusterfs.h"
#include "compat.h"
#include "dict.h"
#include "xlator.h"
#include "logging.h"
#include "glusterd-messages.h"
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
#include "glusterd-geo-rep.h"
#include "glusterd-sm.h"
#include "glusterd-utils.h"
#include "glusterd-store.h"
#include "glusterd-volgen.h"
#include "glusterd-pmap.h"
#include "glusterfs-acl.h"
#include "glusterd-syncop.h"
#include "glusterd-mgmt.h"
#include "glusterd-locks.h"
#include "glusterd-messages.h"
#include "glusterd-volgen.h"
#include "glusterd-snapshot-utils.h"
#include "glusterd-svc-mgmt.h"
#include "glusterd-svc-helper.h"
#include "glusterd-shd-svc.h"
#include "glusterd-nfs-svc.h"
#include "glusterd-quotad-svc.h"
#include "glusterd-snapd-svc.h"
#include "glusterd-bitd-svc.h"
#include "glusterd-server-quorum.h"
#include "quota-common-utils.h"
#include "common-utils.h"

#include "xdr-generic.h"
#include <sys/resource.h>
#include <inttypes.h>
#include <signal.h>
#include <sys/types.h>
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

int
send_attach_req (xlator_t *this, struct rpc_clnt *rpc, char *path, int op);

static gf_boolean_t
is_brick_mx_enabled ()
{
        char            *value = NULL;
        int             ret = 0;
        gf_boolean_t    enabled = _gf_false;
        xlator_t        *this = NULL;
        glusterd_conf_t *priv = NULL;

        this = THIS;

        priv = this->private;

        ret = dict_get_str (priv->opts, GLUSTERD_BRICK_MULTIPLEX_KEY, &value);

        if (!ret)
                ret = gf_string2boolean (value, &enabled);

        return ret ? _gf_false: enabled;
}

extern struct volopt_map_entry glusterd_volopt_map[];
extern glusterd_all_vol_opts valid_all_vol_opts[];

static glusterd_lock_t lock;

static int
_brick_for_each (glusterd_volinfo_t *volinfo, dict_t *mod_dict,
                         void *data,
               int (*fn) (glusterd_volinfo_t *, glusterd_brickinfo_t *,
                          dict_t *mod_dict, void *))
{
        int                  ret        = 0;
        glusterd_brickinfo_t *brickinfo = NULL;
        xlator_t             *this = THIS;

        cds_list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                gf_msg_debug (this->name, 0, "Found a brick - %s:%s",
                              brickinfo->hostname, brickinfo->path);
                ret = fn (volinfo, brickinfo, mod_dict, data);
                if (ret)
                        goto out;
        }
out:
        return ret;
}

int
glusterd_volume_brick_for_each (glusterd_volinfo_t *volinfo, void *data,
               int (*fn) (glusterd_volinfo_t *, glusterd_brickinfo_t *,
                          dict_t *mod_dict, void *))
{
        dict_t             *mod_dict    = NULL;
        glusterd_volinfo_t *dup_volinfo = NULL;
        int                ret          = 0;

        if (volinfo->type != GF_CLUSTER_TYPE_TIER) {
                ret = _brick_for_each (volinfo, NULL, data, fn);
                if (ret)
                        goto out;
        } else {
                ret = glusterd_create_sub_tier_volinfo (volinfo, &dup_volinfo,
                                                        _gf_true,
                                                        volinfo->volname);
                if (ret)
                        goto out;

                mod_dict = dict_new();
                if (!mod_dict) {
                        ret = -1;
                        goto out;
                }

                ret = dict_set_str (mod_dict, "hot-brick", "on");
                if (ret)
                        goto out;

                ret = _brick_for_each (dup_volinfo, mod_dict, data, fn);
                if (ret)
                        goto out;
                GF_FREE (dup_volinfo);
                dup_volinfo = NULL;
                ret = glusterd_create_sub_tier_volinfo (volinfo, &dup_volinfo,
                                                        _gf_false,
                                                        volinfo->volname);
                if (ret)
                        goto out;
                ret = _brick_for_each (dup_volinfo, NULL, data, fn);
                if (ret)
                        goto out;
        }
out:
        if (dup_volinfo)
                glusterd_volinfo_delete (dup_volinfo);

        if (mod_dict)
                dict_unref (mod_dict);
        return ret;
}

int32_t
glusterd_get_lock_owner (uuid_t *uuid)
{
        gf_uuid_copy (*uuid, lock.owner) ;
        return 0;
}

static int32_t
glusterd_set_lock_owner (uuid_t owner)
{
        gf_uuid_copy (lock.owner, owner);
        //TODO: set timestamp
        return 0;
}

static int32_t
glusterd_unset_lock_owner (uuid_t owner)
{
        gf_uuid_clear (lock.owner);
        //TODO: set timestamp
        return 0;
}

gf_boolean_t
glusterd_is_fuse_available ()
{

        int     fd = 0;

#ifdef __NetBSD__
	fd = open ("/dev/puffs", O_RDWR);
#else
        fd = open ("/dev/fuse", O_RDWR);
#endif

        if (fd > -1 && !sys_close (fd))
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

        if (!gf_uuid_is_null (owner)) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GLUSTERD_LOCK_FAIL, "Unable to get lock"
                        " for uuid: %s, lock held by: %s",
                        uuid_utoa_r (uuid, new_owner_str),
                        uuid_utoa_r (owner, owner_str));
                goto out;
        }

        ret = glusterd_set_lock_owner (uuid);

        if (!ret) {
                gf_msg_debug (this->name, 0, "Cluster lock held by"
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

        if (gf_uuid_is_null (owner)) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GLUSTERD_LOCK_FAIL, "Cluster lock not held!");
                goto out;
        }

        ret = gf_uuid_compare (uuid, owner);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GLUSTERD_LOCK_FAIL, "Cluster lock held by %s ,"
                        "unlock req from %s!", uuid_utoa_r (owner, owner_str),
                        uuid_utoa_r (uuid, new_owner_str));
                goto out;
        }

        ret = glusterd_unset_lock_owner (uuid);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GLUSTERD_UNLOCK_FAIL, "Unable to clear cluster "
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

        gf_uuid_copy (*uuid, MY_UUID);

        return 0;
}

int
glusterd_submit_request (struct rpc_clnt *rpc, void *req,
                         call_frame_t *frame, rpc_clnt_prog_t *prog,
                         int procnum, struct iobref *iobref,
                         xlator_t *this, fop_cbk_fn_t cbkfn, xdrproc_t xdrproc)
{
        char                    new_iobref  = 0;
        int                     ret         = -1;
        int                     count       = 0;
        ssize_t                 req_size    = 0;
        struct iobuf           *iobuf       = NULL;
        struct iovec            iov         = {0, };

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
        rpc_clnt_submit (rpc, prog, procnum, cbkfn, &iov, count, NULL, 0,
                         iobref, frame, NULL, 0, NULL, 0, NULL);

        /* Unconditionally set ret to 0 here. This is to guard against a double
         * STACK_DESTROY in case of a failure in rpc_clnt_submit AFTER the
         * request is sent over the wire: once in the callback function of the
         * request and once in the error codepath of some of the callers of
         * glusterd_submit_request().
         */
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
                gf_msg ("glusterd", GF_LOG_ERROR, ENOMEM,
                        GD_MSG_NO_MEMORY,
                        "Failed to get iobuf");
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
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_ENCODE_FAIL, "Failed to encode message");
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
                        gf_msg ("glusterd", GF_LOG_ERROR, ENOMEM,
                                GD_MSG_NO_MEMORY, "out of memory");
                        goto out;
                }

                new_iobref = 1;
        }

        iob = glusterd_serialize_reply (req, arg, &rsp, xdrproc);
        if (!iob) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_SERIALIZE_MSG_FAIL, "Failed to serialize reply");
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
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_REPLY_SUBMIT_FAIL, "Reply submission failed");
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
        glusterd_volinfo_t *volinfo = NULL;

        return (glusterd_volinfo_find (volname, &volinfo) == 0);
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

        LOCK_INIT (&new_volinfo->lock);
        CDS_INIT_LIST_HEAD (&new_volinfo->vol_list);
        CDS_INIT_LIST_HEAD (&new_volinfo->snapvol_list);
        CDS_INIT_LIST_HEAD (&new_volinfo->bricks);
        CDS_INIT_LIST_HEAD (&new_volinfo->snap_volumes);

        new_volinfo->dict = dict_new ();
        if (!new_volinfo->dict) {
                GF_FREE (new_volinfo);

                goto out;
        }

        new_volinfo->gsync_slaves = dict_new ();
        if (!new_volinfo->gsync_slaves) {
                dict_unref (new_volinfo->dict);
                GF_FREE (new_volinfo);
                goto out;
        }

        new_volinfo->gsync_active_slaves = dict_new ();
        if (!new_volinfo->gsync_active_slaves) {
                dict_unref (new_volinfo->dict);
                dict_unref (new_volinfo->gsync_slaves);
                GF_FREE (new_volinfo);
                goto out;
        }

        snprintf (new_volinfo->parent_volname, GD_VOLUME_NAME_MAX, "N/A");

        new_volinfo->snap_max_hard_limit = GLUSTERD_SNAPS_MAX_HARD_LIMIT;

        new_volinfo->xl = THIS;

        new_volinfo->snapd.svc.build = glusterd_snapdsvc_build;
        new_volinfo->snapd.svc.build (&(new_volinfo->snapd.svc));

        new_volinfo->tierd.svc.build = glusterd_tierdsvc_build;
        new_volinfo->tierd.svc.build (&(new_volinfo->tierd.svc));

        pthread_mutex_init (&new_volinfo->reflock, NULL);
        *volinfo = glusterd_volinfo_ref (new_volinfo);

        ret = 0;

out:
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);
        return ret;
}

/* This function will create a new volinfo and then
 * dup the entries from volinfo to the new_volinfo.
 *
 * @param volinfo       volinfo which will be duplicated
 * @param dup_volinfo   new volinfo which will be created
 * @param set_userauth  if this true then auth info is also set
 *
 * @return 0 on success else -1
 */
int32_t
glusterd_volinfo_dup (glusterd_volinfo_t *volinfo,
                      glusterd_volinfo_t **dup_volinfo,
                      gf_boolean_t set_userauth)
{
        int32_t                 ret             = -1;
        xlator_t                *this           = NULL;
        glusterd_volinfo_t      *new_volinfo    = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_VALIDATE_OR_GOTO (this->name, volinfo, out);
        GF_VALIDATE_OR_GOTO (this->name, dup_volinfo, out);

        ret = glusterd_volinfo_new (&new_volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLINFO_SET_FAIL, "not able to create the "
                        "duplicate volinfo for the volume %s",
                        volinfo->volname);
                goto out;
        }

        new_volinfo->type = volinfo->type;
        new_volinfo->replica_count = volinfo->replica_count;
        new_volinfo->arbiter_count = volinfo->arbiter_count;
        new_volinfo->stripe_count = volinfo->stripe_count;
        new_volinfo->disperse_count = volinfo->disperse_count;
        new_volinfo->redundancy_count = volinfo->redundancy_count;
        new_volinfo->dist_leaf_count = volinfo->dist_leaf_count;
        new_volinfo->sub_count = volinfo->sub_count;
        new_volinfo->subvol_count = volinfo->subvol_count;
        new_volinfo->transport_type = volinfo->transport_type;
        new_volinfo->brick_count = volinfo->brick_count;
        new_volinfo->tier_info = volinfo->tier_info;
        new_volinfo->quota_conf_version = volinfo->quota_conf_version;
        new_volinfo->quota_xattr_version = volinfo->quota_xattr_version;
        new_volinfo->snap_max_hard_limit = volinfo->snap_max_hard_limit;
        new_volinfo->quota_conf_cksum = volinfo->quota_conf_cksum;

        dict_copy (volinfo->dict, new_volinfo->dict);
        dict_copy (volinfo->gsync_slaves, new_volinfo->gsync_slaves);
        dict_copy (volinfo->gsync_active_slaves,
                   new_volinfo->gsync_active_slaves);
        gd_update_volume_op_versions (new_volinfo);

        if (set_userauth) {
                glusterd_auth_set_username (new_volinfo,
                                            volinfo->auth.username);
                glusterd_auth_set_password (new_volinfo,
                                            volinfo->auth.password);
        }

        *dup_volinfo = new_volinfo;
        ret = 0;
out:
        if (ret && (NULL != new_volinfo)) {
                (void) glusterd_volinfo_delete (new_volinfo);
        }
        return ret;
}

/* This function will duplicate brickinfo
 *
 * @param brickinfo     Source brickinfo
 * @param dup_brickinfo Destination brickinfo
 *
 * @return 0 on success else -1
 */
int32_t
glusterd_brickinfo_dup (glusterd_brickinfo_t *brickinfo,
                        glusterd_brickinfo_t *dup_brickinfo)
{
        int32_t         ret     = -1;
        xlator_t        *this   = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_VALIDATE_OR_GOTO (this->name, brickinfo, out);
        GF_VALIDATE_OR_GOTO (this->name, dup_brickinfo, out);

        strcpy (dup_brickinfo->hostname, brickinfo->hostname);
        strcpy (dup_brickinfo->path, brickinfo->path);
        strcpy (dup_brickinfo->real_path, brickinfo->real_path);
        strcpy (dup_brickinfo->device_path, brickinfo->device_path);
        strcpy (dup_brickinfo->fstype, brickinfo->fstype);
        strcpy (dup_brickinfo->mnt_opts, brickinfo->mnt_opts);
        ret = gf_canonicalize_path (dup_brickinfo->path);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_CANONICALIZE_FAIL, "Failed to canonicalize "
                        "brick path");
                goto out;
        }
        gf_uuid_copy (dup_brickinfo->uuid, brickinfo->uuid);

        dup_brickinfo->port = brickinfo->port;
        dup_brickinfo->rdma_port = brickinfo->rdma_port;
        if (NULL != brickinfo->logfile) {
                dup_brickinfo->logfile = gf_strdup (brickinfo->logfile);
                if (NULL == dup_brickinfo->logfile) {
                        ret = -1;
                        goto out;
                }
        }
        strcpy (dup_brickinfo->brick_id, brickinfo->brick_id);
        strcpy (dup_brickinfo->mount_dir, brickinfo->mount_dir);
        dup_brickinfo->status = brickinfo->status;
        dup_brickinfo->snap_status = brickinfo->snap_status;
out:
        return ret;
}
int32_t
glusterd_create_sub_tier_volinfo (glusterd_volinfo_t *volinfo,
                                  glusterd_volinfo_t **dup_volinfo,
                                  gf_boolean_t is_hot_tier,
                                  const char *new_volname)
{
        glusterd_brickinfo_t *brickinfo       = NULL;
        glusterd_brickinfo_t *brickinfo_dup   = NULL;
        gd_tier_info_t       *tier_info       = NULL;
        int                   i               = 0;
        int                   ret             = -1;

        tier_info = &(volinfo->tier_info);

        ret = glusterd_volinfo_dup (volinfo, dup_volinfo, _gf_true);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOL_OP_FAILED,
                        "Failed to create volinfo");
                return ret;
        }

        gf_uuid_copy ((*dup_volinfo)->volume_id, volinfo->volume_id);
        (*dup_volinfo)->is_snap_volume   = volinfo->is_snap_volume;
        (*dup_volinfo)->status           = volinfo->status;
        (*dup_volinfo)->snapshot         = volinfo->snapshot;

        memcpy (&(*dup_volinfo)->tier_info, &volinfo->tier_info,
                sizeof (volinfo->tier_info));

        strncpy ((*dup_volinfo)->volname, new_volname,
                  sizeof((*dup_volinfo)->volname)-1);

        cds_list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                i++;

                if (is_hot_tier) {
                        if (i > volinfo->tier_info.hot_brick_count)
                                break;
                } else {
                        if (i <= volinfo->tier_info.hot_brick_count)
                                continue;
                }

                ret = glusterd_brickinfo_new (&brickinfo_dup);
                if (ret) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_BRICK_NEW_INFO_FAIL, "Failed to create "
                                "new brickinfo");
                        goto out;
                }


                glusterd_brickinfo_dup (brickinfo, brickinfo_dup);
                cds_list_add_tail (&brickinfo_dup->brick_list,
                              &((*dup_volinfo)->bricks));
        }

        if (is_hot_tier) {
            (*dup_volinfo)->type             = tier_info->hot_type;
            (*dup_volinfo)->replica_count    = tier_info->hot_replica_count;
            (*dup_volinfo)->brick_count      = tier_info->hot_brick_count;
            (*dup_volinfo)->dist_leaf_count  =
                                   glusterd_get_dist_leaf_count(*dup_volinfo);

        } else {
            (*dup_volinfo)->type             = tier_info->cold_type;
            (*dup_volinfo)->replica_count    = tier_info->cold_replica_count;
            (*dup_volinfo)->disperse_count   = tier_info->cold_disperse_count;
            (*dup_volinfo)->redundancy_count = tier_info->cold_redundancy_count;
            (*dup_volinfo)->dist_leaf_count  = tier_info->cold_dist_leaf_count;
            (*dup_volinfo)->brick_count      = tier_info->cold_brick_count;
        }
out:
        if (ret && *dup_volinfo) {
                glusterd_volinfo_delete (*dup_volinfo);
                *dup_volinfo = NULL;
        }

        return ret;

}

/*
 * gd_vol_is_geo_rep_active:
 *      This function checks for any running geo-rep session for
 *      the volume given.
 *
 * Return Value:
 *      _gf_true : If any running geo-rep session.
 *      _gf_false: If no running geo-rep session.
 */

gf_boolean_t
gd_vol_is_geo_rep_active (glusterd_volinfo_t *volinfo)
{
        gf_boolean_t     active = _gf_false;

        GF_ASSERT (volinfo);

        if (volinfo->gsync_active_slaves &&
            volinfo->gsync_active_slaves->count > 0)
                active = _gf_true;

        return active;
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

        cds_list_del_init (&brickinfo->brick_list);

        (void) gf_store_handle_destroy (brickinfo->shandle);

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

        cds_list_for_each_entry_safe (brickinfo, tmp, &volinfo->bricks,
                                      brick_list) {
                ret = glusterd_brickinfo_delete (brickinfo);
                if (ret)
                        goto out;
        }

out:
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_volinfo_remove (glusterd_volinfo_t *volinfo)
{
        cds_list_del_init (&volinfo->vol_list);
        glusterd_volinfo_unref (volinfo);
        return 0;
}

int32_t
glusterd_volinfo_delete (glusterd_volinfo_t *volinfo)
{
        int32_t                 ret = -1;

        GF_ASSERT (volinfo);

        cds_list_del_init (&volinfo->vol_list);
        cds_list_del_init (&volinfo->snapvol_list);

        ret = glusterd_volume_brickinfos_delete (volinfo);
        if (ret)
                goto out;
        if (volinfo->dict)
                dict_unref (volinfo->dict);
        if (volinfo->gsync_slaves)
                dict_unref (volinfo->gsync_slaves);
        if (volinfo->gsync_active_slaves)
                dict_unref (volinfo->gsync_active_slaves);
        GF_FREE (volinfo->logdir);
        if (volinfo->rebal.dict)
                dict_unref (volinfo->rebal.dict);

        gf_store_handle_destroy (volinfo->quota_conf_shandle);
        gf_store_handle_destroy (volinfo->shandle);
        gf_store_handle_destroy (volinfo->node_state_shandle);
        gf_store_handle_destroy (volinfo->snapd.handle);

        glusterd_auth_cleanup (volinfo);

        pthread_mutex_destroy (&volinfo->reflock);
        GF_FREE (volinfo);
        ret = 0;

out:
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);
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

        CDS_INIT_LIST_HEAD (&new_brickinfo->brick_list);

        *brickinfo = new_brickinfo;

        ret = 0;

out:
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_get_next_available_brickid (glusterd_volinfo_t *volinfo)
{
        glusterd_brickinfo_t *brickinfo    = NULL;
        char                 *token        = NULL;
        int                   brickid      = 0;
        int                   max_brickid  = -1;
        int                   ret          = -1;

        cds_list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                token = strrchr (brickinfo->brick_id, '-');
                ret = gf_string2int32 (++token, &brickid);
                if (ret < 0) {
                        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_BRICK_ID_GEN_FAILED,
                                "Unable to generate brick ID");
                        return ret;
                }
                if (brickid > max_brickid)
                        max_brickid = brickid;
        }

        return max_brickid + 1 ;
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
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_get_brick_mount_dir (char *brickpath, char *hostname, char *mount_dir)
{
        char                   *mnt_pt        = NULL;
        char                   *brick_dir     = NULL;
        int32_t                 ret           = -1;
        uuid_t                  brick_uuid    = {0, };
        xlator_t               *this          = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (brickpath);
        GF_ASSERT (hostname);
        GF_ASSERT (mount_dir);

        ret = glusterd_hostname_to_uuid (hostname, brick_uuid);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_HOSTNAME_TO_UUID_FAIL,
                        "Failed to convert hostname %s to uuid",
                        hostname);
                goto out;
        }

        if (!gf_uuid_compare (brick_uuid, MY_UUID)) {
                ret = glusterd_get_brick_root (brickpath, &mnt_pt);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_BRICKPATH_ROOT_GET_FAIL,
                                "Could not get the root of the brick path %s",
                                brickpath);
                        goto out;
                }

                if (strncmp (brickpath, mnt_pt, strlen(mnt_pt))) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_BRKPATH_MNTPNT_MISMATCH,
                                "brick: %s brick mount: %s",
                                brickpath, mnt_pt);
                        ret = -1;
                        goto out;
                }

                brick_dir = &brickpath[strlen (mnt_pt)];
                brick_dir++;

                snprintf (mount_dir, PATH_MAX, "/%s", brick_dir);
        }

out:
        if (mnt_pt)
                GF_FREE(mnt_pt);

        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_brickinfo_new_from_brick (char *brick,
                                   glusterd_brickinfo_t **brickinfo,
                                   gf_boolean_t construct_real_path,
                                   char **op_errstr)
{
        char                   *hostname      = NULL;
        char                   *path          = NULL;
        char                   *tmp_host      = NULL;
        char                   *tmp_path      = NULL;
        char                   *vg            = NULL;
        int32_t                 ret           = -1;
        glusterd_brickinfo_t   *new_brickinfo = NULL;
        xlator_t               *this          = NULL;
        char                    abspath[PATH_MAX] = {0};

        this = THIS;
        GF_ASSERT (this);
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

        if (construct_real_path) {
                ret = glusterd_hostname_to_uuid (new_brickinfo->hostname,
                                                 new_brickinfo->uuid);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_HOSTNAME_TO_UUID_FAIL,
                                "Failed to convert hostname %s to uuid",
                                hostname);
                        if (op_errstr)
                                gf_asprintf (op_errstr, "Host %s is not in "
                                             "\'Peer in Cluster\' state",
                                             new_brickinfo->hostname);
                        goto out;
                }
        }

        if (construct_real_path &&
            !gf_uuid_compare (new_brickinfo->uuid, MY_UUID)
            && new_brickinfo->real_path[0] == '\0') {
                if (!realpath (new_brickinfo->path, abspath)) {
                        /* ENOENT indicates that brick path has not been created
                         * which is a valid scenario */
                        if (errno != ENOENT) {
                                gf_msg (this->name, GF_LOG_CRITICAL, errno,
                                        GD_MSG_BRICKINFO_CREATE_FAIL, "realpath"
                                        " () failed for brick %s. The "
                                        "underlying filesystem may be in bad "
                                        "state", new_brickinfo->path);
                                ret = -1;
                                goto out;
                        }
                }
                strncpy (new_brickinfo->real_path, abspath, strlen(abspath));
        }

        *brickinfo = new_brickinfo;

        ret = 0;
out:
        GF_FREE (tmp_host);
        if (tmp_host)
                GF_FREE (tmp_path);

        gf_msg_debug (this->name, 0, "Returning %d", ret);
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

        cds_list_for_each_entry (volinfo, &priv->volumes, vol_list) {
                cds_list_for_each_entry (brickinfo, &volinfo->bricks,
                                         brick_list) {
                        if (gf_uuid_compare (uuid, brickinfo->uuid))
                                continue;
                        if (_is_prefix (brickinfo->real_path, tmp_path))
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

        ret = sys_mkdir (brickinfo->path, 0777);
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

        ret = sys_lstat (brickinfo->path, &brick_st);
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

        ret = sys_lstat ("/", &root_st);
        if (ret) {
                snprintf (msg, sizeof (msg), "lstat failed on /. Reason : %s",
                          strerror (errno));
                goto out;
        }

        ret = sys_lstat (parentdir, &parent_st);
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
                        snprintf (msg, sizeof (msg), "The brick %s:%s "
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
                sys_rmdir (brickinfo->path);
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
                gf_uuid_copy (peer_uuid, uuid);
        } else {
                ret = glusterd_hostname_to_uuid (hostname, peer_uuid);
                if (ret)
                        goto out;
        }
        ret = -1;
        cds_list_for_each_entry (brickiter, &volinfo->bricks, brick_list) {

                if ((gf_uuid_is_null (brickiter->uuid)) &&
                    (glusterd_resolve_brick (brickiter) != 0))
                        goto out;
                if (gf_uuid_compare (peer_uuid, brickiter->uuid))
                        continue;

                if (strcmp (brickiter->path, path) == 0) {
                        gf_msg_debug (this->name, 0, LOGSTR_FOUND_BRICK,
                                brickiter->hostname, brickiter->path,
                                volinfo->volname);
                        ret = 0;
                        if (brickinfo)
                                *brickinfo = brickiter;
                        break;
                }
        }

out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_volume_brickinfo_get_by_brick (char *brick,
                                        glusterd_volinfo_t *volinfo,
                                        glusterd_brickinfo_t **brickinfo,
                                        gf_boolean_t construct_real_path)
{
        int32_t                 ret = -1;
        glusterd_brickinfo_t    *tmp_brickinfo = NULL;

        GF_ASSERT (brick);
        GF_ASSERT (volinfo);

        ret = glusterd_brickinfo_new_from_brick (brick, &tmp_brickinfo,
                                                 construct_real_path, NULL);
        if (ret)
                goto out;

        ret = glusterd_volume_brickinfo_get (NULL, tmp_brickinfo->hostname,
                                             tmp_brickinfo->path, volinfo,
                                             brickinfo);
        (void) glusterd_brickinfo_delete (tmp_brickinfo);
out:
        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
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

        cds_list_for_each_entry (voliter, &priv->volumes, vol_list) {
                if (gf_uuid_compare (volume_id, voliter->volume_id))
                        continue;
                *volinfo = voliter;
                ret = 0;
                gf_msg_debug (this->name, 0, "Volume %s found",
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

        cds_list_for_each_entry (tmp_volinfo, &priv->volumes, vol_list) {
                if (!strcmp (tmp_volinfo->volname, volname)) {
                        gf_msg_debug (this->name, 0, "Volume %s found",
                                volname);
                        ret = 0;
                        *volinfo = tmp_volinfo;
                        break;
                }
        }

        gf_msg_debug (this->name, 0, "Returning %d", ret);
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
                gf_msg (this->name, GF_LOG_INFO, 0,
                        GD_MSG_ALREADY_STOPPED,
                        "%s already stopped", service);
                goto out;
        }
        gf_msg_debug (this->name, 0, "Stopping gluster %s running in pid: "
                "%d", service, pid);

        ret = kill (pid, sig);
        if (ret) {
                switch (errno) {
                case ESRCH:
                        gf_msg_debug (this->name, 0, "%s is already stopped",
                                service);
                        ret = 0;
                        goto out;
                default:
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_SVC_KILL_FAIL, "Unable to kill %s "
                                "service, reason:%s", service,
                                strerror (errno));
                }
        }
        if (!force_kill)
                goto out;

        sleep (1);
        if (gf_is_service_running (pidfile, NULL)) {
                ret = kill (pid, SIGKILL);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_PID_KILL_FAIL, "Unable to kill pid:%d, "
                                "reason:%s", pid, strerror(errno));
                        goto out;
                }
        }

        ret = 0;
out:
        return ret;
}

int32_t
glusterd_service_stop_nolock (const char *service, char *pidfile, int sig,
                              gf_boolean_t force_kill)
{
        int32_t    ret                = -1;
        pid_t      pid                = -1;
        xlator_t  *this               = NULL;
        FILE      *file               = NULL;

        this = THIS;
        GF_ASSERT (this);

        file = fopen (pidfile, "r+");
        if (file) {
                ret = fscanf (file, "%d", &pid);
                if (ret <= 0) {
                        gf_msg_debug (this->name, 0,
                                      "Unable to read pidfile: %s", pidfile);
                        goto out;
                }
        }

        if (kill (pid, 0) < 0) {
                ret = 0;
                gf_msg_debug (this->name, 0, "%s process not running: (%d) %s",
                              service, pid, strerror (errno));
                goto out;
        }
        gf_msg_debug (this->name, 0, "Stopping gluster %s service running with "
                      "pid: %d", service, pid);

        ret = kill (pid, sig);
        if (ret) {
                switch (errno) {
                case ESRCH:
                        gf_msg_debug (this->name, 0, "%s is already stopped",
                                service);
                        ret = 0;
                        goto out;
                default:
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_SVC_KILL_FAIL, "Unable to kill %s "
                                "service, reason:%s", service,
                                strerror (errno));
                }
        }
        if (!force_kill)
                goto out;

        sleep (1);
        if (kill(pid, 0) == 0) {
                ret = kill (pid, SIGKILL);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_PID_KILL_FAIL, "Unable to kill pid:%d, "
                                "reason:%s", pid, strerror(errno));
                        goto out;
                }
        }

        ret = 0;

out:
        if (file)
                fclose (file);

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
        if (is_brick_mx_enabled ()) {
                snprintf (sockpath, len, "%s/run/daemon-%s.socket",
                          volume_dir, brickinfo->hostname);
        } else {
                char                    export_path[PATH_MAX] = {0,};
                char                    sock_filepath[PATH_MAX] = {0,};
                GLUSTERD_REMOVE_SLASH_FROM_PATH (brickinfo->path, export_path);
                snprintf (sock_filepath, PATH_MAX, "%s/run/%s-%s",
                          volume_dir, brickinfo->hostname, export_path);

                glusterd_set_socket_filepath (sock_filepath, sockpath, len);
        }
}

/* connection happens only if it is not aleady connected,
 * reconnections are taken care by rpc-layer
 */
int32_t
glusterd_brick_connect (glusterd_volinfo_t  *volinfo,
                        glusterd_brickinfo_t  *brickinfo, char *socketpath)
{
        int                     ret = 0;
        char                    volume_id_str[64];
        char                    *brickid = NULL;
        dict_t                  *options = NULL;
        struct rpc_clnt         *rpc = NULL;

        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);
        GF_ASSERT (socketpath);

        if (brickinfo->rpc == NULL) {
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

                ret = glusterd_rpc_create (&rpc, options,
                                           glusterd_brick_rpc_notify,
                                           brickid, _gf_false);
                if (ret) {
                        GF_FREE (brickid);
                        goto out;
                }
                brickinfo->rpc = rpc;
        }
out:

        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
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
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_CREATE_DIR_FAILED, "Failed to create rundir");
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
        char                    *bind_address = NULL;
        char                    socketpath[PATH_MAX] = {0};
        char                    glusterd_uuid[1024] = {0,};
        char                    valgrind_logfile[PATH_MAX] = {0};
        char                    rdma_brick_path[PATH_MAX] = {0,};
        struct rpc_clnt         *rpc = NULL;
        rpc_clnt_connection_t   *conn  = NULL;

        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        if (brickinfo->snap_status == -1) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        GD_MSG_SNAPSHOT_PENDING,
                        "Snapshot is pending on %s:%s. "
                        "Hence not starting the brick",
                        brickinfo->hostname,
                        brickinfo->path);
                ret = 0;
                goto out;
        }

        GLUSTERD_GET_BRICK_PIDFILE (pidfile, volinfo, brickinfo, priv);
        if (gf_is_service_running (pidfile, NULL)) {
                goto connect;
        }

        /*
         * There are all sorts of races in the start/stop code that could leave
         * a UNIX-domain socket or RPC-client object associated with a
         * long-dead incarnation of this brick, while the new incarnation is
         * listening on a new socket at the same path and wondering why we
         * haven't shown up.  To avoid the whole mess and be on the safe side,
         * we just blow away anything that might have been left over, and start
         * over again.
         */
        glusterd_set_brick_socket_filepath (volinfo, brickinfo, socketpath,
                                            sizeof (socketpath));
        (void) glusterd_unlink_file (socketpath);
        rpc = brickinfo->rpc;
        if (rpc) {
                brickinfo->rpc = NULL;
                conn = &rpc->conn;
                if (conn->reconnect) {
                        (void ) gf_timer_call_cancel (rpc->ctx, conn->reconnect);
                        //rpc_clnt_unref (rpc);
                }
                rpc_clnt_unref (rpc);
        }

        port = pmap_assign_port (THIS, brickinfo->port, brickinfo->path);

        /* Build the exp_path, before starting the glusterfsd even in
           valgrind mode. Otherwise all the glusterfsd processes start
           writing the valgrind log to the same file.
        */
        GLUSTERD_REMOVE_SLASH_FROM_PATH (brickinfo->path, exp_path);

retry:
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

        if (volinfo->is_snap_volume) {
                snprintf (volfile, PATH_MAX,"/%s/%s/%s.%s.%s",
                          GLUSTERD_VOL_SNAP_DIR_PREFIX,
                          volinfo->snapshot->snapname, volinfo->volname,
                          brickinfo->hostname, exp_path);
        } else {
                snprintf (volfile, PATH_MAX, "%s.%s.%s", volinfo->volname,
                          brickinfo->hostname, exp_path);
        }

        if (volinfo->logdir) {
                snprintf (logfile, PATH_MAX, "%s/%s.log",
                          volinfo->logdir, exp_path);
        } else {
                snprintf (logfile, PATH_MAX, "%s/bricks/%s.log",
                          DEFAULT_LOG_FILE_DIRECTORY, exp_path);
        }
        if (!brickinfo->logfile)
                brickinfo->logfile = gf_strdup (logfile);

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
                snprintf (rdma_brick_path, sizeof(rdma_brick_path), "%s.rdma",
                          brickinfo->path);
                rdma_port = pmap_assign_port (THIS, brickinfo->rdma_port,
                                              rdma_brick_path);
                runner_argprintf (&runner, "%d,%d", port, rdma_port);
                runner_add_arg (&runner, "--xlator-option");
                runner_argprintf (&runner, "%s-server.transport.rdma.listen-port=%d",
                                  volinfo->volname, rdma_port);
        }

        runner_add_arg (&runner, "--xlator-option");
        runner_argprintf (&runner, "%s-server.listen-port=%d",
                          volinfo->volname, port);

        if (dict_get_str (this->options, "transport.socket.bind-address",
                          &bind_address) == 0) {
                runner_add_arg (&runner, "--xlator-option");
                runner_argprintf (&runner, "transport.socket.bind-address=%s",
                                  bind_address);
        }

        if (volinfo->transport_type == GF_TRANSPORT_RDMA)
                runner_argprintf (&runner, "--volfile-server-transport=rdma");
        else if (volinfo->transport_type == GF_TRANSPORT_BOTH_TCP_RDMA)
                runner_argprintf (&runner,
                                  "--volfile-server-transport=socket,rdma");

        if (volinfo->memory_accounting)
                runner_add_arg (&runner, "--mem-accounting");

        runner_log (&runner, "", 0, "Starting GlusterFS");

        brickinfo->port = port;
        brickinfo->rdma_port = rdma_port;
        brickinfo->started_here = _gf_true;

        if (wait) {
                synclock_unlock (&priv->big_lock);
                ret = runner_run (&runner);
                synclock_lock (&priv->big_lock);

                if (ret == -EADDRINUSE) {
                        /* retry after getting a new port */
                        gf_msg (this->name, GF_LOG_WARNING, -ret,
                                GD_MSG_SRC_BRICK_PORT_UNAVAIL,
                                "Port %d is used by other process", port);

                        port = pmap_registry_alloc (this);
                        if (!port) {
                                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                                        GD_MSG_NO_FREE_PORTS,
                                        "Couldn't allocate a port");
                                ret = -1;
                                goto out;
                        }
                        gf_msg (this->name, GF_LOG_NOTICE, 0,
                                GD_MSG_RETRY_WITH_NEW_PORT,
                                "Retrying to start brick %s with new port %d",
                                brickinfo->path, port);
                        goto retry;
                }
        } else {
                ret = runner_run_nowait (&runner);
        }

        if (ret) {
                brickinfo->port = 0;
                brickinfo->rdma_port = 0;
                goto out;
        }

connect:
        ret = glusterd_brick_connect (volinfo, brickinfo, socketpath);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRICK_DISCONNECTED,
                        "Failed to connect to brick %s:%s on %s",
                        brickinfo->hostname, brickinfo->path, socketpath);
                goto out;
        }

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

        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GLUSTERD_GET_VOLUME_DIR (path, volinfo, priv);
        glusterd_set_brick_socket_filepath (volinfo, brickinfo, socketpath,
                                            sizeof (socketpath));

        return glusterd_unlink_file (socketpath);
}

int32_t
glusterd_brick_disconnect (glusterd_brickinfo_t *brickinfo)
{
        rpc_clnt_t              *rpc = NULL;
        glusterd_conf_t         *priv = THIS->private;

        GF_ASSERT (brickinfo);

        if (!brickinfo) {
                gf_msg_callingfn ("glusterd", GF_LOG_WARNING, EINVAL,
                                  GD_MSG_BRICK_NOT_FOUND, "!brickinfo");
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
        xlator_t        *this                   = NULL;
        int             ret                     = 0;
        char            *op_errstr              = NULL;

        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);

        this = THIS;
        GF_ASSERT (this);

        if (del_brick)
                cds_list_del_init (&brickinfo->brick_list);

        if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                /*
                 * In a post-multiplexing world, even if we're not actually
                 * doing any multiplexing, just dropping the RPC connection
                 * isn't enough.  There might be many such connections during
                 * the brick daemon's lifetime, even if we only consider the
                 * management RPC port (because tests etc. might be manually
                 * attaching and detaching bricks).  Therefore, we have to send
                 * an actual signal instead.
                 */
                if (is_brick_mx_enabled ()) {
                        (void) send_attach_req (this, brickinfo->rpc,
                                                brickinfo->path,
                                                GLUSTERD_BRICK_TERMINATE);
                } else {
                        (void) glusterd_brick_terminate (volinfo, brickinfo,
                                                         NULL, 0, &op_errstr);
                        if (op_errstr) {
                                GF_FREE (op_errstr);
                        }
                }
                (void) glusterd_brick_disconnect (brickinfo);
                ret = 0;
        }

        if (del_brick)
                glusterd_delete_brick (volinfo, brickinfo);

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
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        GD_MSG_READIN_FILE_FAILED, "%s", strerror (errno));
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

                ret = sys_write (dest_fd, lines[counter],
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
        mode_t                  orig_umask              = 0;

        GF_ASSERT (volinfo);
        this = THIS;
        priv = THIS->private;
        GF_ASSERT (priv);

        fd = open (cksum_path, O_RDWR | O_APPEND | O_CREAT| O_TRUNC, 0600);

        if (-1 == fd) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_FILE_OP_FAILED, "Unable to open %s,"
                        " errno: %d", cksum_path, errno);
                ret = -1;
                goto out;
        }

        if (!is_quota_conf) {
                snprintf (sort_filepath, sizeof (sort_filepath),
                          "/tmp/%s.XXXXXX", volinfo->volname);

                orig_umask = umask(S_IRWXG | S_IRWXO);
                sort_fd = mkstemp (sort_filepath);
                umask(orig_umask);
                if (sort_fd < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_FILE_OP_FAILED, "Could not generate "
                                "temp file, reason: %s for volume: %s",
                                strerror (errno), volinfo->volname);
                        goto out;
                } else {
                        unlink_sortfile = _gf_true;
                }

                /* sort the info file, result in sort_filepath */

                ret = glusterd_sort_and_redirect (filepath, sort_fd);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_FILE_OP_FAILED, "sorting info file "
                                "failed");
                        goto out;
                }

                ret = sys_close (sort_fd);
                if (ret)
                        goto out;
        }

        cksum_path_final = is_quota_conf ? filepath : sort_filepath;

        ret = get_checksum_for_path (cksum_path_final, &cksum);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_CKSUM_GET_FAIL, "unable to get "
                        "checksum for path: %s", cksum_path_final);
                goto out;
        }
        if (!is_quota_conf) {
                snprintf (buf, sizeof (buf), "%s=%u\n", "info", cksum);
                ret = sys_write (fd, buf, strlen (buf));
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
               sys_close (fd);
        if (unlink_sortfile)
               sys_unlink (sort_filepath);
        gf_msg_debug (this->name, 0, "Returning with %d", ret);

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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_CKSUM_COMPUTE_FAIL, "Failed to compute checksum "
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
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED, "option add for %s%d %s",
                        ctx->key_name, ctx->opt_count, key);
        snprintf (optkey, sizeof (optkey), "%s.%s%d", ctx->prefix,
                  ctx->val_name, ctx->opt_count);
        ret = dict_set_str (ctx->dict, optkey, value->data);
        if (ret)
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "option add for %s%d %s",
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


        cds_list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
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

/* The prefix represents the type of volume to be added.
 * It will be "volume" for normal volumes, and snap# like
 * snap1, snap2, for snapshot volumes
 */
int32_t
glusterd_add_volume_to_dict (glusterd_volinfo_t *volinfo,
                             dict_t  *dict, int32_t count,
                             char *prefix)
{
        int32_t                 ret               = -1;
        char                    pfx[512]          = {0,};
        char                    key[512]          = {0,};
        glusterd_brickinfo_t    *brickinfo        = NULL;
        int32_t                 i                 = 1;
        char                    *volume_id_str    = NULL;
        char                    *str              = NULL;
        glusterd_dict_ctx_t     ctx               = {0};
        char                    *rebalance_id_str = NULL;
        char                    *rb_id_str        = NULL;
        xlator_t                *this             = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (volinfo);
        GF_ASSERT (prefix);

        snprintf (key, sizeof (key), "%s%d.name", prefix, count);
        ret = dict_set_str (dict, key, volinfo->volname);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.type", prefix, count);
        ret = dict_set_int32 (dict, key, volinfo->type);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.brick_count", prefix, count);
        ret = dict_set_int32 (dict, key, volinfo->brick_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.version", prefix, count);
        ret = dict_set_int32 (dict, key, volinfo->version);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.status", prefix, count);
        ret = dict_set_int32 (dict, key, volinfo->status);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.sub_count", prefix, count);
        ret = dict_set_int32 (dict, key, volinfo->sub_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.stripe_count", prefix, count);
        ret = dict_set_int32 (dict, key, volinfo->stripe_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.replica_count", prefix, count);
        ret = dict_set_int32 (dict, key, volinfo->replica_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.arbiter_count", prefix, count);
        ret = dict_set_int32 (dict, key, volinfo->arbiter_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.disperse_count", prefix, count);
        ret = dict_set_int32 (dict, key, volinfo->disperse_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.redundancy_count", prefix, count);
        ret = dict_set_int32 (dict, key, volinfo->redundancy_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.dist_count", prefix, count);
        ret = dict_set_int32 (dict, key, volinfo->dist_leaf_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.ckusm", prefix, count);
        ret = dict_set_int64 (dict, key, volinfo->cksum);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.transport_type", prefix, count);
        ret = dict_set_uint32 (dict, key, volinfo->transport_type);
        if (ret)
                goto out;

         /* tiering related variables */

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.cold_brick_count", prefix, count);
        ret = dict_set_uint32 (dict, key, volinfo->tier_info.cold_brick_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.cold_type", prefix, count);
        ret = dict_set_uint32 (dict, key, volinfo->tier_info.cold_type);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.cold_replica_count", prefix, count);
        ret = dict_set_uint32 (dict, key,
                               volinfo->tier_info.cold_replica_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.cold_disperse_count", prefix, count);
        ret = dict_set_uint32 (dict, key,
                               volinfo->tier_info.cold_disperse_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.cold_redundancy_count",
                               prefix, count);
        ret = dict_set_uint32 (dict, key,
                               volinfo->tier_info.cold_redundancy_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.cold_dist_count", prefix, count);
        ret = dict_set_uint32 (dict, key,
                               volinfo->tier_info.cold_dist_leaf_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.hot_brick_count", prefix, count);
        ret = dict_set_uint32 (dict, key, volinfo->tier_info.hot_brick_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.hot_type", prefix, count);
        ret = dict_set_uint32 (dict, key, volinfo->tier_info.hot_type);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.hot_replica_count", prefix, count);
        ret = dict_set_uint32 (dict, key,
                               volinfo->tier_info.hot_replica_count);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d", prefix, count);
        ret = gd_add_vol_snap_details_to_dict (dict, key, volinfo);
        if (ret)
                goto out;

        volume_id_str = gf_strdup (uuid_utoa (volinfo->volume_id));
        if (!volume_id_str) {
                ret = -1;
                goto out;
        }
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.volume_id", prefix, count);
        ret = dict_set_dynstr (dict, key, volume_id_str);
        if (ret)
                goto out;
        volume_id_str = NULL;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.username", prefix, count);
        str = glusterd_auth_get_username (volinfo);
        if (str) {
                ret = dict_set_dynstr (dict, key, gf_strdup (str));
                if (ret)
                        goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.password", prefix, count);
        str = glusterd_auth_get_password (volinfo);
        if (str) {
                ret = dict_set_dynstr (dict, key, gf_strdup (str));
                if (ret)
                        goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, 256, "%s%d.rebalance", prefix, count);
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
        snprintf (key, 256, "%s%d.rebalance-id", prefix, count);
        ret = dict_set_dynstr (dict, key, rebalance_id_str);
        if (ret)
                goto out;
        rebalance_id_str = NULL;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.rebalance-op", prefix, count);
        ret = dict_set_uint32 (dict, key, volinfo->rebal.op);
        if (ret)
                goto out;

        if (volinfo->rebal.dict) {
                snprintf (pfx, sizeof (pfx), "%s%d", prefix, count);
                ctx.dict = dict;
                ctx.prefix = pfx;
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

        snprintf (pfx, sizeof (pfx), "%s%d", prefix, count);
        ctx.dict = dict;
        ctx.prefix = pfx;
        ctx.opt_count = 1;
        ctx.key_name = "key";
        ctx.val_name = "value";
        GF_ASSERT (volinfo->dict);

        dict_foreach (volinfo->dict, _add_dict_to_prdict, &ctx);
        ctx.opt_count--;
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.opt-count", prefix, count);
        ret = dict_set_int32 (dict, key, ctx.opt_count);
        if (ret)
                goto out;

        ctx.dict = dict;
        ctx.prefix = pfx;
        ctx.opt_count = 1;
        ctx.key_name = "slave-num";
        ctx.val_name = "slave-val";
        GF_ASSERT (volinfo->gsync_slaves);

        dict_foreach (volinfo->gsync_slaves, _add_dict_to_prdict, &ctx);
        ctx.opt_count--;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.gsync-count", prefix, count);
        ret = dict_set_int32 (dict, key, ctx.opt_count);
        if (ret)
                goto out;

        cds_list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s%d.brick%d.hostname",
                          prefix, count, i);
                ret = dict_set_str (dict, key, brickinfo->hostname);
                if (ret)
                        goto out;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s%d.brick%d.path",
                          prefix, count, i);
                ret = dict_set_str (dict, key, brickinfo->path);
                if (ret)
                        goto out;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s%d.brick%d.decommissioned",
                          prefix, count, i);
                ret = dict_set_int32 (dict, key, brickinfo->decommissioned);
                if (ret)
                        goto out;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s%d.brick%d.brick_id",
                          prefix, count, i);
                ret = dict_set_str (dict, key, brickinfo->brick_id);
                if (ret)
                        goto out;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s%d.brick%d.uuid",
                          prefix, count, i);
                ret = dict_set_dynstr_with_alloc (dict, key,
                                                  uuid_utoa(brickinfo->uuid));
                if (ret)
                        goto out;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s%d.brick%d", prefix, count, i);
                ret = gd_add_brick_snap_details_to_dict (dict, key, brickinfo);
                if (ret)
                        goto out;

                i++;
        }

        /* Add volume op-versions to dict. This prevents volume inconsistencies
         * in the cluster
         */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.op-version", prefix, count);
        ret = dict_set_int32 (dict, key, volinfo->op_version);
        if (ret)
                goto out;
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.client-op-version", prefix, count);
        ret = dict_set_int32 (dict, key, volinfo->client_op_version);
        if (ret)
                goto out;

        /*Add volume Capability (BD Xlator) to dict*/
        memset (key, 0 ,sizeof (key));
        snprintf (key, sizeof (key), "%s%d.caps", prefix, count);
        ret = dict_set_int32 (dict, key, volinfo->caps);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.quota-xattr-version", prefix, count);
        ret = dict_set_int32 (dict, key, volinfo->quota_xattr_version);
out:
        GF_FREE (volume_id_str);
        GF_FREE (rebalance_id_str);
        GF_FREE (rb_id_str);

        gf_msg_debug (this->name, 0, "Returning with %d", ret);
        return ret;
}

/* The prefix represents the type of volume to be added.
 * It will be "volume" for normal volumes, and snap# like
 * snap1, snap2, for snapshot volumes
 */
int
glusterd_vol_add_quota_conf_to_dict (glusterd_volinfo_t *volinfo, dict_t* load,
                                     int vol_idx, char *prefix)
{
        int            fd                    = -1;
        unsigned char  buf[16]               = {0};
        char           key[PATH_MAX]         = {0};
        int            gfid_idx              = 0;
        int            ret                   = -1;
        xlator_t      *this                  = NULL;
        char           type                  = 0;
        float          version               = 0.0f;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (prefix);

        ret = glusterd_store_create_quota_conf_sh_on_absence (volinfo);
        if (ret)
                goto out;

        fd = open (volinfo->quota_conf_shandle->path, O_RDONLY);
        if (fd == -1) {
                ret = -1;
                goto out;
        }

        ret = quota_conf_read_version (fd, &version);
        if (ret)
                goto out;

        for (gfid_idx=0; ; gfid_idx++) {
                ret = quota_conf_read_gfid (fd, buf, &type, version);
                if (ret == 0) {
                        break;
                } else if (ret < 0) {
                        gf_msg (this->name, GF_LOG_CRITICAL, 0,
                                GD_MSG_QUOTA_CONF_CORRUPT, "Quota "
                                "configuration store may be corrupt.");
                        goto out;
                }

                snprintf (key, sizeof(key)-1, "%s%d.gfid%d", prefix,
                          vol_idx, gfid_idx);
                ret = dict_set_dynstr_with_alloc (load, key, uuid_utoa (buf));
                if (ret)
                        goto out;

                snprintf (key, sizeof(key)-1, "%s%d.gfid-type%d", prefix,
                          vol_idx, gfid_idx);
                ret = dict_set_int8 (load, key, type);
                if (ret)
                        goto out;
        }

        snprintf (key, sizeof(key)-1, "%s%d.gfid-count", prefix, vol_idx);
        key[sizeof(key)-1] = '\0';
        ret = dict_set_int32 (load, key, gfid_idx);
        if (ret)
                goto out;

        snprintf (key, sizeof(key)-1, "%s%d.quota-cksum", prefix, vol_idx);
        key[sizeof(key)-1] = '\0';
        ret = dict_set_uint32 (load, key, volinfo->quota_conf_cksum);
        if (ret)
                goto out;

        snprintf (key, sizeof(key)-1, "%s%d.quota-version", prefix, vol_idx);
        key[sizeof(key)-1] = '\0';
        ret = dict_set_uint32 (load, key, volinfo->quota_conf_version);
        if (ret)
                goto out;

        ret = 0;
out:
        if (fd != -1)
                sys_close (fd);
        return ret;
}

int32_t
glusterd_add_volumes_to_export_dict (dict_t **peer_data)
{
        int32_t                 ret = -1;
        dict_t                  *dict = NULL;
        glusterd_conf_t         *priv = NULL;
        glusterd_volinfo_t      *volinfo = NULL;
        int32_t                 count = 0;
        glusterd_dict_ctx_t     ctx            = {0};
        xlator_t               *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        dict = dict_new ();
        if (!dict)
                goto out;

        cds_list_for_each_entry (volinfo, &priv->volumes, vol_list) {
                count++;
                ret = glusterd_add_volume_to_dict (volinfo, dict, count,
                                                   "volume");
                if (ret)
                        goto out;
                if (!glusterd_is_volume_quota_enabled (volinfo))
                        continue;
                ret = glusterd_vol_add_quota_conf_to_dict (volinfo, dict,
                                                           count, "volume");
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

        *peer_data = dict;
out:
        if (ret)
                dict_unref (dict);

        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_compare_friend_volume (dict_t *peer_data, int32_t count,
                                int32_t *status, char *hostname)
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

        GF_ASSERT (peer_data);
        GF_ASSERT (status);

        this = THIS;
        GF_ASSERT (this);

        snprintf (key, sizeof (key), "volume%d.name", count);
        ret = dict_get_str (peer_data, key, &volname);
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
        ret = dict_get_int32 (peer_data, key, &version);
        if (ret)
                goto out;

        if (version > volinfo->version) {
                //Mismatch detected
                ret = 0;
                gf_msg (this->name, GF_LOG_INFO, 0, GD_MSG_VOL_VERS_MISMATCH,
                        "Version of volume %s differ. local version = %d, "
                        "remote version = %d on peer %s", volinfo->volname,
                        volinfo->version, version, hostname);
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
        ret = dict_get_uint32 (peer_data, key, &cksum);
        if (ret)
                goto out;

        if (cksum != volinfo->cksum) {
                ret = 0;
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_CKSUM_VERS_MISMATCH,
                        "Version of Cksums %s differ. local cksum = %u, remote "
                        "cksum = %u on peer %s", volinfo->volname,
                        volinfo->cksum, cksum, hostname);
                *status = GLUSTERD_VOL_COMP_RJT;
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "volume%d.quota-version", count);
        ret = dict_get_uint32 (peer_data, key, &quota_version);
        if (ret) {
                gf_msg_debug (this->name, 0, "quota-version key absent for"
                        " volume %s in peer %s's response", volinfo->volname,
                        hostname);
                ret = 0;
        } else {
                if (quota_version > volinfo->quota_conf_version) {
                        //Mismatch detected
                        ret = 0;
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                GD_MSG_QUOTA_CONFIG_VERS_MISMATCH,
                                "Quota configuration versions of volume %s "
                                "differ. local version = %d, remote version = "
                                "%d on peer %s", volinfo->volname,
                                volinfo->quota_conf_version,
                                quota_version, hostname);
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
        ret = dict_get_uint32 (peer_data, key, &quota_cksum);
        if (ret) {
                gf_msg_debug (this->name, 0, "quota checksum absent for "
                        "volume %s in peer %s's response", volinfo->volname,
                        hostname);
                ret = 0;
        } else {
                if (quota_cksum != volinfo->quota_conf_cksum) {
                        ret = 0;
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_QUOTA_CONFIG_CKSUM_MISMATCH, "Cksums of "
                                "quota configuration of volume %s differ. local"
                                " cksum = %u, remote  cksum = %u on peer %s",
                                volinfo->volname, volinfo->quota_conf_cksum,
                                quota_cksum, hostname);
                        *status = GLUSTERD_VOL_COMP_RJT;
                        goto out;
                }
        }
        *status = GLUSTERD_VOL_COMP_SCS;

out:
        if (*status == GLUSTERD_VOL_COMP_RJT) {
                gf_event (EVENT_COMPARE_FRIEND_VOLUME_FAILED, "volume=%s",
                          volinfo->volname);
        }
        gf_msg_debug (this->name, 0, "Returning with ret: %d, status: %d",
                ret, *status);
        return ret;
}

static int32_t
import_prdict_dict (dict_t *peer_data, dict_t  *dst_dict, char *key_prefix,
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
                ret = dict_get_str (peer_data, key, &opt_key);
                if (ret) {
                        snprintf (msg, sizeof (msg), "Volume dict key not "
                                  "specified");
                        goto out;
                }

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.%s%d",
                          prefix, value_prefix, i);
                ret = dict_get_str (peer_data, key, &opt_val);
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
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_IMPORT_PRDICT_DICT, "%s", msg);
        gf_msg_debug ("glusterd", 0, "Returning with %d", ret);
        return ret;

}


int
glusterd_spawn_daemons (void *opaque)
{
        glusterd_conf_t *conf = THIS->private;
        int             ret             = -1;

        synclock_lock (&conf->big_lock);
        glusterd_restart_bricks (conf);
        glusterd_restart_gsyncds (conf);
        glusterd_restart_rebalance (conf);
        ret = glusterd_snapdsvc_restart ();
        ret = glusterd_tierdsvc_restart ();

        return ret;
}


int32_t
glusterd_import_friend_volume_opts (dict_t *peer_data, int count,
                                    glusterd_volinfo_t *volinfo,
                                    char *prefix)
{
        char                    key[512] = {0,};
        int32_t                 ret = -1;
        int                     opt_count = 0;
        char                    msg[2048] = {0};
        char                    volume_prefix[1024] = {0};

        GF_ASSERT (peer_data);
        GF_ASSERT (volinfo);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.opt-count", prefix, count);
        ret = dict_get_int32 (peer_data, key, &opt_count);
        if (ret) {
                snprintf (msg, sizeof (msg), "Volume option count not "
                          "specified for %s", volinfo->volname);
                goto out;
        }

        snprintf (volume_prefix, sizeof (volume_prefix), "%s%d", prefix, count);
        ret = import_prdict_dict (peer_data, volinfo->dict, "key", "value",
                                  opt_count, volume_prefix);
        if (ret) {
                snprintf (msg, sizeof (msg), "Unable to import options dict "
                          "specified for %s", volinfo->volname);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.gsync-count", prefix, count);
        ret = dict_get_int32 (peer_data, key, &opt_count);
        if (ret) {
                snprintf (msg, sizeof (msg), "Gsync count not "
                          "specified for %s", volinfo->volname);
                goto out;
        }

        ret = import_prdict_dict (peer_data, volinfo->gsync_slaves, "slave-num",
                                  "slave-val", opt_count, volume_prefix);
        if (ret) {
                snprintf (msg, sizeof (msg), "Unable to import gsync sessions "
                          "specified for %s", volinfo->volname);
                goto out;
        }

out:
        if (msg[0])
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_VOL_OPTS_IMPORT_FAIL, "%s", msg);
        gf_msg_debug ("glusterd", 0, "Returning with %d", ret);
        return ret;
}

/* The prefix represents the type of volume to be added.
 * It will be "volume" for normal volumes, and snap# like
 * snap1, snap2, for snapshot volumes
 */
int32_t
glusterd_import_new_brick (dict_t *peer_data, int32_t vol_count,
                           int32_t brick_count,
                           glusterd_brickinfo_t **brickinfo,
                           char *prefix)
{
        char                    key[512] = {0,};
        int                     ret = -1;
        char                    *hostname = NULL;
        char                    *path = NULL;
        char                    *brick_id = NULL;
        int                     decommissioned = 0;
        glusterd_brickinfo_t    *new_brickinfo = NULL;
        char                    msg[2048] = {0};
        char                    *brick_uuid_str = NULL;

        GF_ASSERT (peer_data);
        GF_ASSERT (vol_count >= 0);
        GF_ASSERT (brickinfo);
        GF_ASSERT (prefix);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.brick%d.hostname",
                  prefix, vol_count, brick_count);
        ret = dict_get_str (peer_data, key, &hostname);
        if (ret) {
                snprintf (msg, sizeof (msg), "%s missing in payload", key);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.brick%d.path",
                  prefix, vol_count, brick_count);
        ret = dict_get_str (peer_data, key, &path);
        if (ret) {
                snprintf (msg, sizeof (msg), "%s missing in payload", key);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.brick%d.brick_id",
                  prefix, vol_count, brick_count);
        ret = dict_get_str (peer_data, key, &brick_id);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.brick%d.decommissioned",
                  prefix, vol_count, brick_count);
        ret = dict_get_int32 (peer_data, key, &decommissioned);
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
        if (brick_id)
                strcpy (new_brickinfo->brick_id, brick_id);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.brick%d", prefix, vol_count,
                  brick_count);
        ret = gd_import_new_brick_snap_details (peer_data, key, new_brickinfo);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.brick%d.uuid",
                  prefix, vol_count, brick_count);
        ret = dict_get_str (peer_data, key, &brick_uuid_str);
        if (ret)
                goto out;
        gf_uuid_parse (brick_uuid_str, new_brickinfo->uuid);

        *brickinfo = new_brickinfo;
out:
        if (msg[0]) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_BRICK_IMPORT_FAIL, "%s", msg);
                gf_event (EVENT_IMPORT_BRICK_FAILED, "peer=%s;brick=%s",
                          new_brickinfo->hostname, new_brickinfo->path);
        }
        gf_msg_debug ("glusterd", 0, "Returning with %d", ret);
        return ret;
}

/* The prefix represents the type of volume to be added.
 * It will be "volume" for normal volumes, and snap# like
 * snap1, snap2, for snapshot volumes
 */
static int32_t
glusterd_import_bricks (dict_t *peer_data, int32_t vol_count,
                        glusterd_volinfo_t *new_volinfo, char *prefix)
{
        int                     ret = -1;
        int                     brick_count = 1;
        int                     brickid = 0;
        glusterd_brickinfo_t     *new_brickinfo = NULL;

        GF_ASSERT (peer_data);
        GF_ASSERT (vol_count >= 0);
        GF_ASSERT (new_volinfo);
        GF_ASSERT (prefix);
        while (brick_count <= new_volinfo->brick_count) {

                ret = glusterd_import_new_brick (peer_data, vol_count,
                                                 brick_count,
                                                 &new_brickinfo, prefix);
                if (ret)
                        goto out;
                if (new_brickinfo->brick_id[0] == '\0')
                        /*We were probed from a peer having op-version
                         less than GD_OP_VER_PERSISTENT_AFR_XATTRS*/
                        GLUSTERD_ASSIGN_BRICKID_TO_BRICKINFO (new_brickinfo,
                                                              new_volinfo,
                                                              brickid++);
                cds_list_add_tail (&new_brickinfo->brick_list,
                                   &new_volinfo->bricks);
                brick_count++;
        }
        ret = 0;
out:
        gf_msg_debug ("glusterd", 0, "Returning with %d", ret);
        return ret;
}

/* The prefix represents the type of volume to be added.
 * It will be "volume" for normal volumes, and snap# like
 * snap1, snap2, for snapshot volumes
 */
int
glusterd_import_quota_conf (dict_t *peer_data, int vol_idx,
                            glusterd_volinfo_t *new_volinfo,
                            char *prefix)
{
        int       gfid_idx         = 0;
        int       gfid_count       = 0;
        int       ret              = -1;
        int       fd               = -1;
        char      key[PATH_MAX]    = {0};
        char     *gfid_str         = NULL;
        uuid_t    gfid             = {0,};
        xlator_t *this             = NULL;
        int8_t    gfid_type        = 0;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (peer_data);
        GF_ASSERT (prefix);

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

        snprintf (key, sizeof (key)-1, "%s%d.quota-cksum", prefix, vol_idx);
        key[sizeof(key)-1] = '\0';
        ret = dict_get_uint32 (peer_data, key, &new_volinfo->quota_conf_cksum);
        if (ret)
                gf_msg_debug (this->name, 0, "Failed to get quota cksum");

        snprintf (key, sizeof (key)-1, "%s%d.quota-version", prefix, vol_idx);
        key[sizeof(key)-1] = '\0';
        ret = dict_get_uint32 (peer_data, key,
                               &new_volinfo->quota_conf_version);
        if (ret)
                gf_msg_debug (this->name, 0, "Failed to get quota "
                                                  "version");

        snprintf (key, sizeof (key)-1, "%s%d.gfid-count", prefix, vol_idx);
        key[sizeof(key)-1] = '\0';
        ret = dict_get_int32 (peer_data, key, &gfid_count);
        if (ret)
                goto out;

        ret = glusterd_quota_conf_write_header (fd);
        if (ret)
                goto out;

        gfid_idx = 0;
        for (gfid_idx = 0; gfid_idx < gfid_count; gfid_idx++) {

                snprintf (key, sizeof (key)-1, "%s%d.gfid%d",
                          prefix, vol_idx, gfid_idx);
                ret = dict_get_str (peer_data, key, &gfid_str);
                if (ret)
                        goto out;

                snprintf (key, sizeof (key)-1, "%s%d.gfid-type%d",
                          prefix, vol_idx, gfid_idx);
                ret = dict_get_int8 (peer_data, key, &gfid_type);
                if (ret)
                        gfid_type = GF_QUOTA_CONF_TYPE_USAGE;

                gf_uuid_parse (gfid_str, gfid);
                ret = glusterd_quota_conf_write_gfid (fd, gfid,
                                                      (char)gfid_type);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_CRITICAL, errno,
                                GD_MSG_QUOTA_CONF_WRITE_FAIL, "Unable to write "
                                "gfid %s into quota.conf for %s", gfid_str,
                                new_volinfo->volname);
                        ret = -1;
                        goto out;
                }
        }

        ret = gf_store_rename_tmppath (new_volinfo->quota_conf_shandle);

        ret = 0;

out:
        if (!ret) {
                ret = glusterd_compute_cksum (new_volinfo, _gf_true);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_CKSUM_COMPUTE_FAIL,
                                "Failed to compute checksum");
                        goto clear_quota_conf;
                }

                ret = glusterd_store_save_quota_version_and_cksum (new_volinfo);
                if (ret)
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_QUOTA_CKSUM_VER_STORE_FAIL,
                                "Failed to save quota version and checksum");
        }

clear_quota_conf:
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
        gf_msg_debug (THIS->name, 0, "Returning with %d", ret);
        return ret;
}

/* The prefix represents the type of volume to be added.
 * It will be "volume" for normal volumes, and snap# like
 * snap1, snap2, for snapshot volumes
 */
int32_t
glusterd_import_volinfo (dict_t *peer_data, int count,
                         glusterd_volinfo_t **volinfo,
                         char *prefix)
{
        int                ret               = -1;
        char               key[256]          = {0};
        char               *parent_volname   = NULL;
        char               *volname          = NULL;
        glusterd_volinfo_t *new_volinfo      = NULL;
        char               *volume_id_str    = NULL;
        char               msg[2048]         = {0};
        char               *str              = NULL;
        char               *rebalance_id_str = NULL;
        int                op_version        = 0;
        int                client_op_version = 0;

        GF_ASSERT (peer_data);
        GF_ASSERT (volinfo);
        GF_ASSERT (prefix);

        snprintf (key, sizeof (key), "%s%d.name", prefix, count);
        ret = dict_get_str (peer_data, key, &volname);
        if (ret) {
                snprintf (msg, sizeof (msg), "%s missing in payload", key);
                goto out;
        }

        ret = glusterd_volinfo_new (&new_volinfo);
        if (ret)
                goto out;
        strncpy (new_volinfo->volname, volname,
                 sizeof(new_volinfo->volname) - 1);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.type", prefix, count);
        ret = dict_get_int32 (peer_data, key, &new_volinfo->type);
        if (ret) {
                snprintf (msg, sizeof (msg), "%s missing in payload for %s",
                          key, volname);
                goto out;
        }

        snprintf (key, sizeof (key), "%s%d.parent_volname", prefix, count);
        ret = dict_get_str (peer_data, key, &parent_volname);
        if (!ret)
                strncpy (new_volinfo->parent_volname, parent_volname,
                         sizeof(new_volinfo->parent_volname));

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.brick_count", prefix, count);
        ret = dict_get_int32 (peer_data, key, &new_volinfo->brick_count);
        if (ret) {
                snprintf (msg, sizeof (msg), "%s missing in payload for %s",
                          key, volname);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.version", prefix, count);
        ret = dict_get_int32 (peer_data, key, &new_volinfo->version);
        if (ret) {
                snprintf (msg, sizeof (msg), "%s missing in payload for %s",
                          key, volname);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.status", prefix, count);
        ret = dict_get_int32 (peer_data, key, (int32_t *)&new_volinfo->status);
        if (ret) {
                snprintf (msg, sizeof (msg), "%s missing in payload for %s",
                          key, volname);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.sub_count", prefix, count);
        ret = dict_get_int32 (peer_data, key, &new_volinfo->sub_count);
        if (ret) {
                snprintf (msg, sizeof (msg), "%s missing in payload for %s",
                          key, volname);
                goto out;
        }

        /* not having a 'stripe_count' key is not a error
           (as peer may be of old version) */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.stripe_count", prefix, count);
        ret = dict_get_int32 (peer_data, key, &new_volinfo->stripe_count);
        if (ret)
                gf_msg (THIS->name, GF_LOG_INFO, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "peer is possibly old version");

        /* not having a 'replica_count' key is not a error
           (as peer may be of old version) */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.replica_count", prefix, count);
        ret = dict_get_int32 (peer_data, key, &new_volinfo->replica_count);
        if (ret)
                gf_msg (THIS->name, GF_LOG_INFO, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "peer is possibly old version");

        /* not having a 'arbiter_count' key is not a error
           (as peer may be of old version) */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.arbiter_count", prefix, count);
        ret = dict_get_int32 (peer_data, key, &new_volinfo->arbiter_count);
        if (ret)
                gf_msg (THIS->name, GF_LOG_INFO, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "peer is possibly old version");

        /* not having a 'disperse_count' key is not a error
           (as peer may be of old version) */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.disperse_count", prefix, count);
        ret = dict_get_int32 (peer_data, key, &new_volinfo->disperse_count);
        if (ret)
                gf_msg (THIS->name, GF_LOG_INFO, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "peer is possibly old version");

        /* not having a 'redundancy_count' key is not a error
           (as peer may be of old version) */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.redundancy_count", prefix, count);
        ret = dict_get_int32 (peer_data, key, &new_volinfo->redundancy_count);
        if (ret)
                gf_msg (THIS->name, GF_LOG_INFO, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "peer is possibly old version");

        /* not having a 'dist_count' key is not a error
           (as peer may be of old version) */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.dist_count", prefix, count);
        ret = dict_get_int32 (peer_data, key, &new_volinfo->dist_leaf_count);
        if (ret)
                gf_msg (THIS->name, GF_LOG_INFO, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "peer is possibly old version");

        /* not having a 'hot_brick_count' key is not a error
           (as peer may be of old version) */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.hot_brick_count", prefix, count);
        ret = dict_get_int32 (peer_data, key,
                              &new_volinfo->tier_info.hot_brick_count);
        if (ret)
                gf_msg_debug (THIS->name, 0,
                        "peer is possibly old version");

        /* not having a 'hot_type' key is not a error
           (as peer may be of old version) */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.hot_type", prefix, count);
        ret = dict_get_int32 (peer_data, key,
                              &new_volinfo->tier_info.hot_type);
        if (ret)
                gf_msg_debug (THIS->name, 0,
                        "peer is possibly old version");

        /* not having a 'hot_replica_count' key is not a error
           (as peer may be of old version) */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.hot_replica_count", prefix, count);
        ret = dict_get_int32 (peer_data, key,
                              &new_volinfo->tier_info.hot_replica_count);
        if (ret)
                gf_msg_debug (THIS->name, 0,
                        "peer is possibly old version");

        /* not having a 'cold_brick_count' key is not a error
           (as peer may be of old version) */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.cold_brick_count", prefix, count);
        ret = dict_get_int32 (peer_data, key,
                              &new_volinfo->tier_info.cold_brick_count);
        if (ret)
                gf_msg_debug (THIS->name, 0,
                        "peer is possibly old version");

        /* not having a 'cold_type' key is not a error
           (as peer may be of old version) */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.cold_type", prefix, count);
        ret = dict_get_int32 (peer_data, key,
                              &new_volinfo->tier_info.cold_type);
        if (ret)
                gf_msg_debug (THIS->name, 0,
                        "peer is possibly old version");

        /* not having a 'cold_replica_count' key is not a error
           (as peer may be of old version) */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.cold_replica_count", prefix, count);
        ret = dict_get_int32 (peer_data, key,
                              &new_volinfo->tier_info.cold_replica_count);
        if (ret)
                gf_msg_debug (THIS->name, 0,
                        "peer is possibly old version");

        /* not having a 'cold_disperse_count' key is not a error
           (as peer may be of old version) */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.cold_disperse_count", prefix, count);
        ret = dict_get_int32 (peer_data, key,
                              &new_volinfo->tier_info.cold_disperse_count);
        if (ret)
                gf_msg_debug (THIS->name, 0,
                        "peer is possibly old version");

        /* not having a 'cold_redundancy_count' key is not a error
           (as peer may be of old version) */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.cold_redundancy_count",
                               prefix, count);
        ret = dict_get_int32 (peer_data, key,
                              &new_volinfo->tier_info.cold_redundancy_count);
        if (ret)
                gf_msg_debug (THIS->name, 0,
                        "peer is possibly old version");

        /* not having a 'cold_dist_count' key is not a error
           (as peer may be of old version) */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.cold_dist_count", prefix, count);
        ret = dict_get_int32 (peer_data, key,
                              &new_volinfo->tier_info.cold_dist_leaf_count);
        if (ret)
                gf_msg_debug (THIS->name, 0,
                        "peer is possibly old version");

        new_volinfo->subvol_count = new_volinfo->brick_count/
                                    glusterd_get_dist_leaf_count (new_volinfo);
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.ckusm", prefix, count);
        ret = dict_get_uint32 (peer_data, key, &new_volinfo->cksum);
        if (ret) {
                snprintf (msg, sizeof (msg), "%s missing in payload for %s",
                          key, volname);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.volume_id", prefix, count);
        ret = dict_get_str (peer_data, key, &volume_id_str);
        if (ret) {
                snprintf (msg, sizeof (msg), "%s missing in payload for %s",
                          key, volname);
                goto out;
        }

        gf_uuid_parse (volume_id_str, new_volinfo->volume_id);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.username", prefix, count);
        ret = dict_get_str (peer_data, key, &str);
        if (!ret) {
                ret = glusterd_auth_set_username (new_volinfo, str);
                if (ret)
                        goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.password", prefix, count);
        ret = dict_get_str (peer_data, key, &str);
        if (!ret) {
                ret = glusterd_auth_set_password (new_volinfo, str);
                if (ret)
                        goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.transport_type", prefix, count);
        ret = dict_get_uint32 (peer_data, key, &new_volinfo->transport_type);
        if (ret) {
                snprintf (msg, sizeof (msg), "%s missing in payload for %s",
                          key, volname);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.rebalance", prefix, count);
        ret = dict_get_uint32 (peer_data, key, &new_volinfo->rebal.defrag_cmd);
        if (ret) {
                snprintf (msg, sizeof (msg), "%s missing in payload for %s",
                          key, volname);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.rebalance-id", prefix, count);
        ret = dict_get_str (peer_data, key, &rebalance_id_str);
        if (ret) {
                /* This is not present in older glusterfs versions,
                 * so don't error out
                 */
                ret = 0;
        } else {
                gf_uuid_parse (rebalance_id_str, new_volinfo->rebal.rebalance_id);
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.rebalance-op", prefix, count);
        ret = dict_get_uint32 (peer_data, key,
                               (uint32_t *) &new_volinfo->rebal.op);
        if (ret) {
                /* This is not present in older glusterfs versions,
                 * so don't error out
                 */
                ret = 0;
        }
        ret = gd_import_friend_volume_rebal_dict (peer_data, count,
                                                  new_volinfo);
        if (ret) {
                snprintf (msg, sizeof (msg), "Failed to import rebalance dict "
                          "for volume.");
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d", prefix, count);
        ret = gd_import_volume_snap_details (peer_data, new_volinfo, key,
                                             volname);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_SNAP_DETAILS_IMPORT_FAIL,
                        "Failed to import snapshot "
                        "details for volume %s", volname);
                goto out;
        }

        ret = glusterd_import_friend_volume_opts (peer_data, count,
                                                  new_volinfo, prefix);
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
        snprintf (key, sizeof (key), "%s%d.op-version", prefix, count);
        ret = dict_get_int32 (peer_data, key, &op_version);
        if (ret)
                ret = 0;
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.client-op-version", prefix, count);
        ret = dict_get_int32 (peer_data, key, &client_op_version);
        if (ret)
                ret = 0;

        if (op_version && client_op_version) {
                new_volinfo->op_version = op_version;
                new_volinfo->client_op_version = client_op_version;
        } else if (((op_version == 0) && (client_op_version != 0)) ||
                   ((op_version != 0) && (client_op_version == 0))) {
                ret = -1;
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Only one volume op-version found");
                goto out;
        } else {
                new_volinfo->op_version = 1;
                new_volinfo->client_op_version = 1;
        }

        memset (key, 0 ,sizeof (key));
        snprintf (key, sizeof (key), "%s%d.caps", prefix, count);
        /*This is not present in older glusterfs versions, so ignore ret value*/
        ret = dict_get_int32 (peer_data, key, &new_volinfo->caps);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s%d.quota-xattr-version", prefix, count);
        /*This is not present in older glusterfs versions, so ignore ret value*/
        ret = dict_get_int32 (peer_data, key,
                              &new_volinfo->quota_xattr_version);

        ret = glusterd_import_bricks (peer_data, count, new_volinfo, prefix);
        if (ret)
                goto out;

        *volinfo = new_volinfo;
out:
        if (msg[0]) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_VOLINFO_IMPORT_FAIL, "%s", msg);
                gf_event (EVENT_IMPORT_VOLUME_FAILED, "volume=%s",
                          new_volinfo->volname);
        }
        gf_msg_debug ("glusterd", 0, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_volume_disconnect_all_bricks (glusterd_volinfo_t *volinfo)
{
        int                  ret = 0;
        glusterd_brickinfo_t *brickinfo = NULL;
        GF_ASSERT (volinfo);

        cds_list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (glusterd_is_brick_started (brickinfo)) {
                        ret = glusterd_brick_disconnect (brickinfo);
                        if (ret) {
                                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                        GD_MSD_BRICK_DISCONNECT_FAIL,
                                        "Failed to "
                                        "disconnect %s:%s", brickinfo->hostname,
                                        brickinfo->path);
                                break;
                        }
                }
        }

        return ret;
}

int32_t
glusterd_volinfo_copy_brickinfo (glusterd_volinfo_t *old_volinfo,
                                 glusterd_volinfo_t *new_volinfo)
{
        glusterd_brickinfo_t   *new_brickinfo       = NULL;
        glusterd_brickinfo_t   *old_brickinfo       = NULL;
        glusterd_conf_t        *priv                = NULL;
        int                     ret                 = 0;
        xlator_t               *this                = NULL;
        char                    abspath[PATH_MAX]   = {0};

        GF_ASSERT (new_volinfo);
        GF_ASSERT (old_volinfo);
        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        cds_list_for_each_entry (new_brickinfo, &new_volinfo->bricks,
                                 brick_list) {
                ret = glusterd_volume_brickinfo_get (new_brickinfo->uuid,
                                                     new_brickinfo->hostname,
                                                     new_brickinfo->path,
                                                     old_volinfo,
                                                     &old_brickinfo);
                if (ret == 0) {
                        new_brickinfo->port = old_brickinfo->port;

                        if (old_brickinfo->real_path == '\0') {
                                if (!realpath (new_brickinfo->path, abspath)) {
                                        /* Here an ENOENT should also be a
                                         * failure as the brick is expected to
                                         * be in existance
                                         */
                                        gf_msg (this->name, GF_LOG_CRITICAL,
                                                errno,
                                                GD_MSG_BRICKINFO_CREATE_FAIL,
                                                "realpath () failed for brick "
                                                "%s. The underlying filesystem "
                                                "may be in bad state",
                                                new_brickinfo->path);
                                        ret = -1;
                                        goto out;
                                }
                                strncpy (new_brickinfo->real_path, abspath,
                                         strlen(abspath));
                        } else {
                                strncpy (new_brickinfo->real_path,
                                         old_brickinfo->real_path,
                                         strlen (old_brickinfo->real_path));
                        }
                }
        }
        ret = 0;

out:
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
        cds_list_for_each_entry (old_brickinfo, &old_volinfo->bricks,
                                 brick_list) {
                ret = glusterd_volume_brickinfo_get (old_brickinfo->uuid,
                                                     old_brickinfo->hostname,
                                                     old_brickinfo->path,
                                                     new_volinfo,
                                                     &new_brickinfo);
                /* If the brick is stale, i.e it's not a part of the new volume
                 * or if it's part of the new volume and is pending a snap,
                 * then stop the brick process
                 */
                if (ret || (new_brickinfo->snap_status == -1)) {
                        /*TODO: may need to switch to 'atomic' flavour of
                         * brick_stop, once we make peer rpc program also
                         * synctask enabled*/
                        ret = glusterd_brick_stop (old_volinfo, old_brickinfo,
                                                   _gf_false);
                        if (ret)
                                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                        GD_MSG_BRICK_STOP_FAIL, "Failed to stop"
                                        " brick %s:%s", old_brickinfo->hostname,
                                        old_brickinfo->path);
                }
        }
        ret = 0;
out:
        gf_msg_debug ("glusterd", 0, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_delete_stale_volume (glusterd_volinfo_t *stale_volinfo,
                              glusterd_volinfo_t *valid_volinfo)
{
        int32_t                  ret            = -1;
        glusterd_volinfo_t      *temp_volinfo   = NULL;
        glusterd_volinfo_t      *voliter        = NULL;
        xlator_t                *this           = NULL;
        glusterd_svc_t          *svc            = NULL;

        GF_ASSERT (stale_volinfo);
        GF_ASSERT (valid_volinfo);
        this = THIS;
        GF_ASSERT (this);

        /* Copy snap_volumes list from stale_volinfo to valid_volinfo */
        valid_volinfo->snap_count = 0;
        cds_list_for_each_entry_safe (voliter, temp_volinfo,
                                      &stale_volinfo->snap_volumes,
                                      snapvol_list) {
                cds_list_add_tail (&voliter->snapvol_list,
                                   &valid_volinfo->snap_volumes);
                valid_volinfo->snap_count++;
        }

        if ((!gf_uuid_is_null (stale_volinfo->restored_from_snap)) &&
            (gf_uuid_compare (stale_volinfo->restored_from_snap,
                           valid_volinfo->restored_from_snap))) {
                ret = glusterd_lvm_snapshot_remove (NULL, stale_volinfo);
                if (ret) {
                        gf_msg(this->name, GF_LOG_WARNING, 0,
                                GD_MSG_SNAP_REMOVE_FAIL,
                               "Failed to remove lvm snapshot for "
                               "restored volume %s", stale_volinfo->volname);
                }
        }

        /* If stale volume is in started state, stop the stale bricks if the new
         * volume is started else, stop all bricks.
         * We dont want brick_rpc_notify to access already deleted brickinfo,
         * so disconnect all bricks from stale_volinfo (unconditionally), since
         * they are being deleted subsequently.
         */
        if (glusterd_is_volume_started (stale_volinfo)) {
                if (glusterd_is_volume_started (valid_volinfo)) {
                        (void) glusterd_volinfo_stop_stale_bricks (valid_volinfo,
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
                sys_unlink (stale_volinfo->shandle->path);
                (void) gf_store_handle_destroy (stale_volinfo->shandle);
                stale_volinfo->shandle = NULL;
        }

        /* Marking volume as stopped, so that svc manager stops snapd
         * and we are deleting the volume.
         */
        stale_volinfo->status = GLUSTERD_STATUS_STOPPED;

        if (!stale_volinfo->is_snap_volume) {
                svc = &(stale_volinfo->snapd.svc);
                (void) svc->manager (svc, stale_volinfo, PROC_START_NO_WAIT);
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
        if (glusterd_defrag_rpc_get (old->defrag)) {
                rpc_transport_disconnect (old->defrag->rpc->conn.trans,
                                          _gf_false);
                glusterd_defrag_rpc_put (old->defrag);
        }

        if (!gf_uuid_is_null (old->rebalance_id) &&
            gf_uuid_compare (old->rebalance_id, new->rebalance_id)) {
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

        /* glusterd_rebalance_t.{op, id, defrag_cmd} are copied during volume
         * import
         * a new defrag object should come to life with rebalance being restarted
         */
out:
        return ret;
}

int32_t
glusterd_import_friend_volume (dict_t *peer_data, size_t count)
{

        int32_t                 ret = -1;
        glusterd_conf_t         *priv = NULL;
        xlator_t                *this = NULL;
        glusterd_volinfo_t      *old_volinfo = NULL;
        glusterd_volinfo_t      *new_volinfo = NULL;
        glusterd_svc_t          *svc         = NULL;

        GF_ASSERT (peer_data);

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        ret = glusterd_import_volinfo (peer_data, count,
                                       &new_volinfo, "volume");
        if (ret)
                goto out;

        if (!new_volinfo) {
                gf_msg_debug (this->name, 0,
                        "Not importing snap volume");
                goto out;
        }

        ret = glusterd_volinfo_find (new_volinfo->volname, &old_volinfo);
        if (0 == ret) {
                /* Ref count the old_volinfo such that deleting it doesn't crash
                 * if its been already in use by other thread
                 */
                glusterd_volinfo_ref (old_volinfo);
                (void) gd_check_and_update_rebalance_info (old_volinfo,
                                                           new_volinfo);

                /* Copy brick ports & real_path from the old volinfo always.
                 * The old_volinfo will be cleaned up and this information
                 * could be lost
                 */
                (void) glusterd_volinfo_copy_brickinfo (old_volinfo,
                                                        new_volinfo);

                (void) glusterd_delete_stale_volume (old_volinfo, new_volinfo);
                glusterd_volinfo_unref (old_volinfo);
        }

        if (glusterd_is_volume_started (new_volinfo)) {
                (void) glusterd_start_bricks (new_volinfo);
                if (glusterd_is_snapd_enabled (new_volinfo)) {
                        svc = &(new_volinfo->snapd.svc);
                        if (svc->manager (svc, new_volinfo,
                                          PROC_START_NO_WAIT)){
                                gf_event (EVENT_SVC_MANAGER_FAILED,
                                          "svc_name=%s", svc->name);
                        }
                }
        }

        ret = glusterd_store_volinfo (new_volinfo, GLUSTERD_VOLINFO_VER_AC_NONE);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLINFO_STORE_FAIL, "Failed to store "
                        "volinfo for volume %s", new_volinfo->volname);
                goto out;
        }

        ret = glusterd_create_volfiles_and_notify_services (new_volinfo);
        if (ret)
                goto out;

        ret = glusterd_import_quota_conf (peer_data, count,
                                          new_volinfo, "volume");
        if (ret) {
                gf_event (EVENT_IMPORT_QUOTA_CONF_FAILED, "volume=%s",
                          new_volinfo->volname);
                goto out;
        }
        glusterd_list_add_order (&new_volinfo->vol_list, &priv->volumes,
                                 glusterd_compare_volume_name);

out:
        gf_msg_debug ("glusterd", 0, "Returning with ret: %d", ret);
        return ret;
}

int32_t
glusterd_import_friend_volumes (dict_t *peer_data)
{
        int32_t                 ret = -1;
        int32_t                 count = 0;
        int                     i = 1;

        GF_ASSERT (peer_data);

        ret = dict_get_int32 (peer_data, "count", &count);
        if (ret)
                goto out;

        while (i <= count) {
                ret = glusterd_import_friend_volume (peer_data, i);
                if (ret)
                        goto out;
                i++;
        }

out:
        gf_msg_debug ("glusterd", 0, "Returning with %d", ret);
        return ret;
}

int
glusterd_get_global_server_quorum_ratio (dict_t *opts, double *quorum)
{
        int      ret        = -1;
        char    *quorum_str = NULL;

        ret = dict_get_str (opts, GLUSTERD_QUORUM_RATIO_KEY, &quorum_str);
        if (ret)
                goto out;

        ret = gf_string2percent (quorum_str, quorum);
        if (ret)
                goto out;
        ret = 0;
out:
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
        double          old_quorum = 0.0;
        double          new_quorum = 0.0;

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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GLOBAL_OPT_IMPORT_FAIL, "Failed to import"
                        " global options");
                goto out;
        }

        /* Not handling ret since server-quorum-ratio might not yet be set */
        ret = glusterd_get_global_server_quorum_ratio (conf->opts,
                                                       &old_quorum);
        ret = glusterd_get_global_server_quorum_ratio (import_options,
                                                       &new_quorum);

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

                /* If server quorum ratio has changed, restart bricks to
                 * recompute if quorum is met. If quorum is not met bricks are
                 * not started and those already running are stopped
                 */
                if (old_quorum != new_quorum)
                        glusterd_restart_bricks (conf);
        }

        ret = 0;
out:
        if (import_options)
                dict_unref (import_options);
        return ret;
}

int32_t
glusterd_compare_friend_data (dict_t *peer_data, int32_t *status,
                              char *hostname)
{
        int32_t          ret       = -1;
        int32_t          count     = 0;
        int              i         = 1;
        gf_boolean_t     update    = _gf_false;
        xlator_t        *this      = NULL;
        glusterd_conf_t *priv      = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (peer_data);
        GF_ASSERT (status);

        priv = this->private;
        GF_ASSERT (priv);
        ret = glusterd_import_global_opts (peer_data);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GLOBAL_OPT_IMPORT_FAIL, "Importing global "
                        "options failed");
                goto out;
        }

        ret = dict_get_int32 (peer_data, "count", &count);
        if (ret)
                goto out;

        while (i <= count) {
                ret = glusterd_compare_friend_volume (peer_data, i, status,
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
                ret = glusterd_import_friend_volumes (peer_data);
                if (ret)
                        goto out;
                glusterd_svcs_manager (NULL);
        }

out:
        gf_msg_debug (this->name, 0,
                "Returning with ret: %d, status: %d", ret, *status);
        return ret;
}

struct rpc_clnt*
glusterd_defrag_rpc_get (glusterd_defrag_info_t *defrag)
{
        struct rpc_clnt *rpc = NULL;

        if (!defrag)
                return NULL;

        LOCK (&defrag->lock);
        {
                rpc = rpc_clnt_ref (defrag->rpc);
        }
        UNLOCK (&defrag->lock);
        return rpc;
}

struct rpc_clnt*
glusterd_defrag_rpc_put (glusterd_defrag_info_t *defrag)
{
        struct rpc_clnt *rpc = NULL;

        if (!defrag)
                return NULL;

        LOCK (&defrag->lock);
        {
                rpc = rpc_clnt_unref (defrag->rpc);
                defrag->rpc = rpc;
        }
        UNLOCK (&defrag->lock);
        return rpc;
}

struct rpc_clnt*
glusterd_pending_node_get_rpc (glusterd_pending_node_t *pending_node)
{
        struct rpc_clnt *rpc = NULL;
        glusterd_brickinfo_t    *brickinfo = NULL;
        glusterd_volinfo_t      *volinfo   = NULL;
        glusterd_svc_t          *svc       = NULL;

        GF_VALIDATE_OR_GOTO (THIS->name, pending_node, out);
        GF_VALIDATE_OR_GOTO (THIS->name, pending_node->node, out);

        if (pending_node->type == GD_NODE_BRICK) {
                brickinfo = pending_node->node;
                rpc       = brickinfo->rpc;

        } else if (pending_node->type == GD_NODE_SHD ||
                   pending_node->type == GD_NODE_NFS ||
                   pending_node->type == GD_NODE_QUOTAD ||
                   pending_node->type == GD_NODE_SCRUB) {
                svc = pending_node->node;
                rpc = svc->conn.rpc;
        } else if (pending_node->type == GD_NODE_REBALANCE) {
                volinfo = pending_node->node;
                rpc = glusterd_defrag_rpc_get (volinfo->rebal.defrag);

        } else if (pending_node->type == GD_NODE_SNAPD) {
                volinfo = pending_node->node;
                rpc = volinfo->snapd.svc.conn.rpc;
        } else if (pending_node->type == GD_NODE_TIERD) {
                volinfo = pending_node->node;
                rpc = volinfo->tierd.svc.conn.rpc;
        } else {
                GF_ASSERT (0);
        }

out:
        return rpc;
}

void
glusterd_pending_node_put_rpc (glusterd_pending_node_t *pending_node)
{
        glusterd_volinfo_t      *volinfo = NULL;

        switch (pending_node->type) {
        case GD_NODE_REBALANCE:
                volinfo = pending_node->node;
                glusterd_defrag_rpc_put (volinfo->rebal.defrag);
                break;

        case GD_NODE_TIERD:
                volinfo = pending_node->node;
                glusterd_defrag_rpc_put (volinfo->tier.defrag);
                break;
        default:
                break;
        }

}

int32_t
glusterd_unlink_file (char *sockfpath)
{
        int             ret = 0;

        ret = sys_unlink (sockfpath);
        if (ret) {
                if (ENOENT == errno)
                        ret = 0;
                else
                        gf_msg (THIS->name, GF_LOG_ERROR, errno,
                                GD_MSG_FILE_OP_FAILED, "Failed to remove %s"
                                " error: %s", sockfpath, strerror (errno));
        }

        return ret;
}

void
glusterd_nfs_pmap_deregister ()
{
        if (pmap_unset (MOUNT_PROGRAM, MOUNTV3_VERSION))
                gf_msg ("glusterd", GF_LOG_INFO, 0,
                        GD_MSG_DEREGISTER_SUCCESS,
                        "De-registered MOUNTV3 successfully");
        else
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_PMAP_UNSET_FAIL,
                        "De-register MOUNTV3 is unsuccessful");

        if (pmap_unset (MOUNT_PROGRAM, MOUNTV1_VERSION))
                gf_msg ("glusterd", GF_LOG_INFO, 0,
                        GD_MSG_DEREGISTER_SUCCESS,
                        "De-registered MOUNTV1 successfully");
        else
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_PMAP_UNSET_FAIL,
                        "De-register MOUNTV1 is unsuccessful");

        if (pmap_unset (NFS_PROGRAM, NFSV3_VERSION))
                gf_msg ("glusterd", GF_LOG_INFO, 0,
                        GD_MSG_DEREGISTER_SUCCESS,
                        "De-registered NFSV3 successfully");
        else
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_PMAP_UNSET_FAIL,
                        "De-register NFSV3 is unsuccessful");

        if (pmap_unset (NLM_PROGRAM, NLMV4_VERSION))
                gf_msg ("glusterd", GF_LOG_INFO, 0,
                        GD_MSG_DEREGISTER_SUCCESS,
                        "De-registered NLM v4 successfully");
        else
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_PMAP_UNSET_FAIL,
                        "De-registration of NLM v4 failed");

        if (pmap_unset (NLM_PROGRAM, NLMV1_VERSION))
                gf_msg ("glusterd", GF_LOG_INFO, 0,
                        GD_MSG_DEREGISTER_SUCCESS,
                        "De-registered NLM v1 successfully");
        else
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_PMAP_UNSET_FAIL,
                        "De-registration of NLM v1 failed");

        if (pmap_unset (ACL_PROGRAM, ACLV3_VERSION))
                gf_msg ("glusterd", GF_LOG_INFO, 0,
                        GD_MSG_DEREGISTER_SUCCESS,
                        "De-registered ACL v3 successfully");
        else
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_PMAP_UNSET_FAIL,
                        "De-registration of ACL v3 failed");
}

int
glusterd_add_node_to_dict (char *server, dict_t *dict, int count,
                           dict_t *vol_opts)
{
        int                     ret               = -1;
        char                    pidfile[PATH_MAX] = {0,};
        gf_boolean_t            running           = _gf_false;
        int                     pid               = -1;
        int                     port              = 0;
        glusterd_svc_t         *svc               = NULL;
        char                    key[1024]         = {0,};
        xlator_t               *this              = NULL;
        glusterd_conf_t        *priv              = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        glusterd_svc_build_pidfile_path (server, priv->workdir, pidfile,
                                         sizeof (pidfile));

        if (strcmp(server, priv->shd_svc.name) == 0)
                svc = &(priv->shd_svc);
        else if (strcmp(server, priv->nfs_svc.name) == 0)
                svc = &(priv->nfs_svc);
        else if (strcmp(server, priv->quotad_svc.name) == 0)
                svc = &(priv->quotad_svc);
        else if (strcmp(server, priv->bitd_svc.name) == 0)
                svc = &(priv->bitd_svc);
        else if (strcmp(server, priv->scrub_svc.name) == 0)
                svc = &(priv->scrub_svc);

        //Consider service to be running only when glusterd sees it Online
        if (svc->online)
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
        if (!strcmp (server, priv->nfs_svc.name))
                ret = dict_set_str (dict, key, "NFS Server");
        else if (!strcmp (server, priv->shd_svc.name))
                ret = dict_set_str (dict, key, "Self-heal Daemon");
        else if (!strcmp (server, priv->quotad_svc.name))
                ret = dict_set_str (dict, key, "Quota Daemon");
        else if (!strcmp (server, priv->bitd_svc.name))
                ret = dict_set_str (dict, key, "Bitrot Daemon");
        else if (!strcmp (server, priv->scrub_svc.name))
                ret = dict_set_str (dict, key, "Scrubber Daemon");
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
        if (!strcmp (server, priv->nfs_svc.name)) {
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
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);
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
        char *canon = NULL;
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

        if ((gf_get_hostname_from_ip(hostname,&canon) == 0) && canon) {
                GF_FREE(tmp_host);
                tmp_host = hostname = canon;
        }

        strncpy (remote_host, hostname, strlen (hostname));


out:
        GF_FREE (tmp_host);
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

        cds_list_for_each_entry (voliter, &priv->volumes, vol_list) {
                if (voliter->status == GLUSTERD_STATUS_STARTED)
                        return _gf_false;
        }

        return _gf_true;

}

gf_boolean_t
glusterd_all_shd_compatible_volumes_stopped ()
{
        glusterd_conf_t                         *priv = NULL;
        xlator_t                                *this = NULL;
        glusterd_volinfo_t                      *voliter = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        cds_list_for_each_entry (voliter, &priv->volumes, vol_list) {
                if (!glusterd_is_shd_compatible_volume (voliter))
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

        cds_list_for_each_entry (voliter, &priv->volumes, vol_list) {
                if (!glusterd_is_volume_quota_enabled (voliter))
                        continue;
                if (voliter->status == GLUSTERD_STATUS_STARTED)
                        return _gf_false;
        }

        return _gf_true;
}

gf_boolean_t
glusterd_have_volumes ()
{
        xlator_t        *this = NULL;
        glusterd_conf_t *priv = NULL;
        gf_boolean_t    volumes_exist = _gf_false;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", (this != NULL), out);

        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, (priv != NULL), out);


        volumes_exist = !cds_list_empty (&priv->volumes);
out:
        return volumes_exist;
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

        cds_list_for_each_entry (tmp_volinfo, &priv->volumes, vol_list) {
                ret++;
        }


        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
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

        cds_list_for_each_entry (volinfo, &priv->volumes, vol_list) {

                ret = glusterd_volume_brickinfo_get (uuid, hostname, path,
                                                     volinfo, brickinfo);
                if (ret == 0)
                        /*Found*/
                        goto out;
        }
out:
        return ret;
}

static int32_t
my_callback (struct rpc_req *req, struct iovec *iov, int count, void *v_frame)
{
        call_frame_t    *frame  = v_frame;

        STACK_DESTROY (frame->root);

        return 0;
}

int
send_attach_req (xlator_t *this, struct rpc_clnt *rpc, char *path, int op)
{
        int            ret      = -1;
        struct iobuf  *iobuf    = NULL;
        struct iobref *iobref   = NULL;
        struct iovec   iov      = {0, };
        ssize_t        req_size = 0;
        call_frame_t  *frame    = NULL;
        gd1_mgmt_brick_op_req   brick_req;
        void                    *req = &brick_req;
        void          *errlbl   = &&err;
        extern struct rpc_clnt_program gd_brick_prog;

        if (!rpc) {
                gf_log (this->name, GF_LOG_ERROR, "called with null rpc");
                return -1;
        }

        brick_req.op = op;
        brick_req.name = path;
        brick_req.input.input_val = NULL;
        brick_req.input.input_len = 0;

        req_size = xdr_sizeof ((xdrproc_t)xdr_gd1_mgmt_brick_op_req, req);
        iobuf = iobuf_get2 (rpc->ctx->iobuf_pool, req_size);
        if (!iobuf) {
                goto *errlbl;
        }
        errlbl = &&maybe_free_iobuf;

        iov.iov_base = iobuf->ptr;
        iov.iov_len  = iobuf_pagesize (iobuf);

        iobref = iobref_new ();
        if (!iobref) {
                goto *errlbl;
        }
        errlbl = &&free_iobref;

        frame = create_frame (this, this->ctx->pool);
        if (!frame) {
                goto *errlbl;
        }

        iobref_add (iobref, iobuf);
        /*
         * Drop our reference to the iobuf.  The iobref should already have
         * one after iobref_add, so when we unref that we'll free the iobuf as
         * well.  This allows us to pass just the iobref as frame->local.
         */
        iobuf_unref (iobuf);
        /* Set the pointer to null so we don't free it on a later error. */
        iobuf = NULL;

        /* Create the xdr payload */
        ret = xdr_serialize_generic (iov, req,
                                     (xdrproc_t)xdr_gd1_mgmt_brick_op_req);
        if (ret == -1) {
                goto *errlbl;
        }

        iov.iov_len = ret;

        /* Send the msg */
        ret = rpc_clnt_submit (rpc, &gd_brick_prog, op,
                               my_callback, &iov, 1, NULL, 0, iobref, frame,
                               NULL, 0, NULL, 0, NULL);
        return ret;

free_iobref:
        iobref_unref (iobref);
maybe_free_iobuf:
        if (iobuf) {
                iobuf_unref (iobuf);
        }
err:
        return -1;
}

extern size_t
build_volfile_path (char *volume_id, char *path,
                    size_t path_len, char *trusted_str);


static int
attach_brick (xlator_t *this,
              glusterd_brickinfo_t *brickinfo,
              glusterd_brickinfo_t *other_brick,
              glusterd_volinfo_t *volinfo,
              glusterd_volinfo_t *other_vol)
{
        glusterd_conf_t *conf                   = this->private;
        char            pidfile1[PATH_MAX]      = {0};
        char            pidfile2[PATH_MAX]      = {0};
        char            unslashed[PATH_MAX]     = {'\0',};
        char            full_id[PATH_MAX]       = {'\0',};
        char            path[PATH_MAX]          = {'\0',};
        int             ret;

        gf_log (this->name, GF_LOG_INFO,
                "add brick %s to existing process for %s",
                brickinfo->path, other_brick->path);

        GLUSTERD_REMOVE_SLASH_FROM_PATH (brickinfo->path, unslashed);

        ret = pmap_registry_extend (this, other_brick->port,
                                    brickinfo->path);
        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "adding brick to process failed");
                return -1;
        }

        brickinfo->port = other_brick->port;
        brickinfo->status = GF_BRICK_STARTED;
        brickinfo->started_here = _gf_true;
        brickinfo->rpc = rpc_clnt_ref (other_brick->rpc);

        GLUSTERD_GET_BRICK_PIDFILE (pidfile1, other_vol, other_brick, conf);
        GLUSTERD_GET_BRICK_PIDFILE (pidfile2, volinfo, brickinfo, conf);
        (void) sys_unlink (pidfile2);
        (void) sys_link (pidfile1, pidfile2);

        if (volinfo->is_snap_volume) {
                snprintf (full_id, sizeof(full_id), "/%s/%s/%s.%s.%s",
                          GLUSTERD_VOL_SNAP_DIR_PREFIX,
                          volinfo->snapshot->snapname,
                          volinfo->volname, brickinfo->hostname, unslashed);
        } else {
                snprintf (full_id, sizeof(full_id), "%s.%s.%s",
                          volinfo->volname, brickinfo->hostname, unslashed);
        }
        (void) build_volfile_path (full_id, path, sizeof(path), NULL);

        int tries = 0;
        while (tries++ <= 10) {
                ret = send_attach_req (this, other_brick->rpc, path,
                                       GLUSTERD_BRICK_ATTACH);
                if (!ret) {
                        return 0;
                }
                /*
                 * It might not actually be safe to manipulate the lock like
                 * this, but if we don't then the connection can never actually
                 * complete and retries are useless.  Unfortunately, all of the
                 * alternatives (e.g. doing all of this in a separate thread)
                 * are much more complicated and risky.  TBD: see if there's a
                 * better way
                 */
                synclock_unlock (&conf->big_lock);
                sleep (1);
                synclock_lock (&conf->big_lock);
        }

        gf_log (this->name, GF_LOG_WARNING,
                "attach failed for %s", brickinfo->path);
        return ret;
}

static gf_boolean_t
unsafe_option (dict_t *this, char *key, data_t *value, void *arg)
{
        /*
         * Certain options are safe because they're already being handled other
         * ways, such as being copied down to the bricks (all auth options) or
         * being made irrelevant (event-threads).  All others are suspect and
         * must be checked in the next function.
         */
        if (fnmatch ("*auth*", key, 0) == 0) {
                return _gf_false;
        }

        if (fnmatch ("*event-threads", key, 0) == 0) {
                return _gf_false;
        }

        return _gf_true;
}

static int
opts_mismatch (dict_t *dict1, char *key, data_t *value1, void *dict2)
{
        data_t  *value2         = dict_get (dict2, key);
        int32_t min_len;

        /*
         * If the option is only present on one, we can either look at the
         * default or assume a mismatch.  Looking at the default is pretty
         * hard, because that's part of a structure within each translator and
         * there's no dlopen interface to get at it, so we assume a mismatch.
         * If the user really wants them to match (and for their bricks to be
         * multiplexed, they can always reset the option).
         */
        if (!value2) {
                gf_log (THIS->name, GF_LOG_DEBUG, "missing option %s", key);
                return -1;
        }

        min_len = MIN (value1->len, value2->len);
        if (strncmp (value1->data, value2->data, min_len) != 0) {
                gf_log (THIS->name, GF_LOG_DEBUG,
                        "option mismatch, %s, %s != %s",
                        key, value1->data, value2->data);
                return -1;
        }

        return 0;
}

/* This name was just getting too long, hence the abbreviations. */
static glusterd_brickinfo_t *
find_compat_brick_in_vol (glusterd_conf_t *conf,
                          glusterd_volinfo_t *srch_vol, /* volume to search */
                          glusterd_volinfo_t *comp_vol, /* volume to compare */
                          glusterd_brickinfo_t *brickinfo)
{
        xlator_t                *this                   = THIS;
        glusterd_brickinfo_t    *other_brick;
        char                    pidfile2[PATH_MAX]      = {0};
        int32_t                 pid2                    = -1;

        /*
         * If comp_vol is provided, we have to check *volume* compatibility
         * before we can check *brick* compatibility.
         */
        if (comp_vol) {
                /*
                 * It's kind of a shame that we have to do this check in both
                 * directions, but an option might only exist on one of the two
                 * dictionaries and dict_foreach_match will only find that one.
                 */

                gf_log (THIS->name, GF_LOG_DEBUG,
                        "comparing options for %s and %s",
                         comp_vol->volname, srch_vol->volname);

                if (dict_foreach_match (comp_vol->dict, unsafe_option, NULL,
                                        opts_mismatch, srch_vol->dict) < 0) {
                        gf_log (THIS->name, GF_LOG_DEBUG, "failure forward");
                        return NULL;
                }

                if (dict_foreach_match (srch_vol->dict, unsafe_option, NULL,
                                        opts_mismatch, comp_vol->dict) < 0) {
                        gf_log (THIS->name, GF_LOG_DEBUG, "failure backward");
                        return NULL;
                }

                gf_log (THIS->name, GF_LOG_DEBUG, "all options match");
        }

        cds_list_for_each_entry (other_brick, &srch_vol->bricks,
                                 brick_list) {
                if (other_brick == brickinfo) {
                        continue;
                }
                if (!other_brick->started_here) {
                        continue;
                }
                if (strcmp (brickinfo->hostname, other_brick->hostname) != 0) {
                        continue;
                }
                GLUSTERD_GET_BRICK_PIDFILE (pidfile2, srch_vol, other_brick,
                                            conf);
                if (!gf_is_service_running (pidfile2, &pid2)) {
                        gf_log (this->name, GF_LOG_INFO,
                                "cleaning up dead brick %s:%s",
                                other_brick->hostname, other_brick->path);
                        other_brick->started_here = _gf_false;
                        sys_unlink (pidfile2);
                        continue;
                }
                return other_brick;
        }

        return NULL;
}

static glusterd_brickinfo_t *
find_compatible_brick (glusterd_conf_t *conf,
                       glusterd_volinfo_t *volinfo,
                       glusterd_brickinfo_t *brickinfo,
                       glusterd_volinfo_t **other_vol_p)
{
        glusterd_brickinfo_t    *other_brick = NULL;
        glusterd_volinfo_t      *other_vol   = NULL;
        glusterd_snap_t         *snap        = NULL;

        /* Just return NULL here if multiplexing is disabled. */
        if (!is_brick_mx_enabled ()) {
                return NULL;
        }

        other_brick = find_compat_brick_in_vol (conf, volinfo, NULL, brickinfo);
        if (other_brick) {
                *other_vol_p = volinfo;
                return other_brick;
        }

        /*
         * This check is necessary because changes to a volume's
         * transport options aren't propagated to snapshots.  Such a
         * change might break compatibility between the two, but we
         * have no way to "evict" a brick from the process it's
         * currently in.  If we keep it separate from the start, we
         * avoid the problem.  Note that snapshot bricks can still be
         * colocated with one another, even if they're for different
         * volumes, because the only thing likely to differ is their
         * auth options and those are not a factor in determining
         * compatibility.
         *
         * The very same immutability of snapshot bricks' transport
         * options, which can make them incompatible with their parent
         * volumes, ensures that once-compatible snapshot bricks will
         * remain compatible.  However, the same is not true for bricks
         * belonging to two non-snapshot volumes.  In that case, a
         * change to one might break compatibility and require them to
         * be separated, which is not yet done.
         *
         * TBD: address the option-change issue for non-snapshot bricks
         */
        if (!volinfo->is_snap_volume) {
                cds_list_for_each_entry (other_vol, &conf->volumes, vol_list) {
                        if (other_vol == volinfo) {
                                continue;
                        }
                        other_brick = find_compat_brick_in_vol (conf,
                                                                other_vol,
                                                                volinfo,
                                                                brickinfo);
                        if (other_brick) {
                                *other_vol_p = other_vol;
                                return other_brick;
                        }
                }
        } else {
                cds_list_for_each_entry (snap, &conf->snapshots, snap_list) {
                    cds_list_for_each_entry (other_vol, &snap->volumes,
                                             vol_list) {
                        if (other_vol == volinfo) {
                                continue;
                        }
                        other_brick = find_compat_brick_in_vol (conf,
                                                                other_vol,
                                                                volinfo,
                                                                brickinfo);
                        if (other_brick) {
                                *other_vol_p = other_vol;
                                return other_brick;
                        }
                    }
                }
        }

        return NULL;
}

int
glusterd_brick_start (glusterd_volinfo_t *volinfo,
                      glusterd_brickinfo_t *brickinfo,
                      gf_boolean_t wait)
{
        int                     ret   = -1;
        xlator_t                *this = NULL;
        glusterd_brickinfo_t    *other_brick;
        glusterd_conf_t         *conf = NULL;
        int32_t                 pid                   = -1;
        char                    pidfile[PATH_MAX]     = {0};
        FILE                    *fp;
        char                    socketpath[PATH_MAX]  = {0};
        glusterd_volinfo_t      *other_vol;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;

        if ((!brickinfo) || (!volinfo))
                goto out;

        if (gf_uuid_is_null (brickinfo->uuid)) {
                ret = glusterd_resolve_brick (brickinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_RESOLVE_BRICK_FAIL,
                                FMTSTR_RESOLVE_BRICK,
                                brickinfo->hostname, brickinfo->path);
                        gf_event (EVENT_BRICKPATH_RESOLVE_FAILED,
                                  "peer=%s;volume=%s;brick=%s",
                                  brickinfo->hostname, volinfo->volname,
                                  brickinfo->path);
                        goto out;
                }
        }

        if (gf_uuid_compare (brickinfo->uuid, MY_UUID)) {
                ret = 0;
                goto out;
        }

        GLUSTERD_GET_BRICK_PIDFILE (pidfile, volinfo, brickinfo, conf);
        if (gf_is_service_running (pidfile, &pid)) {
                /*
                 * In general, if the pidfile exists and points to a running
                 * process, this will already be set.  However, that's not the
                 * case when we're starting up and bricks are already running.
                 */
                if (brickinfo->status != GF_BRICK_STARTED) {
                        gf_log (this->name, GF_LOG_INFO,
                                "discovered already-running brick %s",
                                brickinfo->path);
                        //brickinfo->status = GF_BRICK_STARTED;
                        (void) pmap_registry_bind (this,
                                        brickinfo->port, brickinfo->path,
                                        GF_PMAP_PORT_BRICKSERVER, NULL);
                        /*
                         * This will unfortunately result in a separate RPC
                         * connection per brick, even though they're all in
                         * the same process.  It works, but it would be nicer
                         * if we could find a pre-existing connection to that
                         * same port (on another brick) and re-use that.
                         * TBD: re-use RPC connection across bricks
                         */
                        glusterd_set_brick_socket_filepath (volinfo, brickinfo,
                                        socketpath, sizeof (socketpath));
                        (void) glusterd_brick_connect (volinfo, brickinfo,
                                        socketpath);
                }
                return 0;
        }

        ret = _mk_rundir_p (volinfo);
        if (ret)
                goto out;

        other_brick = find_compatible_brick (conf, volinfo, brickinfo,
                                             &other_vol);
        if (other_brick) {
                ret = attach_brick (this, brickinfo, other_brick,
                                    volinfo, other_vol);
                if (ret == 0) {
                        goto out;
                }
        }

        /*
         * This hack is necessary because our brick-process management is a
         * total nightmare.  We expect a brick process's socket and pid files
         * to be ready *immediately* after we start it.  Ditto for it calling
         * back to bind its port.  Unfortunately, none of that is realistic.
         * Any process takes non-zero time to start up.  This has *always* been
         * racy and unsafe; it just became more visible with multiplexing.
         *
         * The right fix would be to do all of this setup *in the parent*,
         * which would include (among other things) getting the PID back from
         * the "runner" code.  That's all prohibitively difficult and risky.
         * To work around the more immediate problems, we create a stub pidfile
         * here to let gf_is_service_running know that we expect the process to
         * be there shortly, and then it gets filled in with a real PID when
         * the process does finish starting up.
         *
         * TBD: pray for GlusterD 2 to be ready soon.
         */
        (void) sys_unlink (pidfile);
        fp = fopen (pidfile, "w+");
        if (fp) {
                (void) fprintf (fp, "0\n");
                (void) fclose (fp);
        }

        ret = glusterd_volume_start_glusterfs (volinfo, brickinfo, wait);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRICK_DISCONNECTED,
                        "Unable to start brick %s:%s",
                        brickinfo->hostname, brickinfo->path);
                gf_event (EVENT_BRICK_START_FAILED,
                         "peer=%s;volume=%s;brick=%s", brickinfo->hostname,
                         volinfo->volname, brickinfo->path);
                goto out;
        }

out:
        gf_msg_debug (this->name, 0, "returning %d ", ret);
        return ret;
}

int
glusterd_restart_bricks (glusterd_conf_t *conf)
{
        int                   ret            = 0;
        glusterd_volinfo_t   *volinfo        = NULL;
        glusterd_brickinfo_t *brickinfo      = NULL;
        glusterd_snap_t      *snap           = NULL;
        gf_boolean_t          start_svcs     = _gf_false;
        xlator_t             *this           = NULL;
        int                   active_count   = 0;
        int                   quorum_count   = 0;
        gf_boolean_t          node_quorum    = _gf_false;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", this, out);

        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, conf, out);

        ret = glusterd_get_quorum_cluster_counts (this, &active_count,
                                                  &quorum_count);
        if (ret)
                goto out;

        if (does_quorum_meet (active_count, quorum_count))
                node_quorum = _gf_true;

        cds_list_for_each_entry (volinfo, &conf->volumes, vol_list) {
                if (volinfo->status != GLUSTERD_STATUS_STARTED)
                        continue;
                gf_msg_debug (this->name, 0, "starting the volume %s",
                        volinfo->volname);

                /* Check the quorum, if quorum is not met, don't start the
                   bricks. Stop bricks in case they are running.
                */
                ret = check_quorum_for_brick_start (volinfo, node_quorum);
                if (ret == 0) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                GD_MSG_SERVER_QUORUM_NOT_MET, "Skipping brick "
                                "restart for volume %s as quorum is not met",
                                volinfo->volname);
                        (void) glusterd_stop_bricks (volinfo);
                        continue;
                } else if (ret == 2 && conf->restart_done == _gf_true) {
                        /* If glusterd has been restarted and quorum is not
                         * applicable then do not restart the bricks as this
                         * might start bricks brought down purposely, say for
                         * maintenance
                         */
                        continue;
                } else {
                        if (start_svcs == _gf_false) {
                                start_svcs = _gf_true;
                                glusterd_svcs_manager (NULL);
                        }

                        cds_list_for_each_entry (brickinfo, &volinfo->bricks,
                                                 brick_list) {
                                glusterd_brick_start (volinfo, brickinfo,
                                                     _gf_false);
                        }
                }
        }

        cds_list_for_each_entry (snap, &conf->snapshots, snap_list) {
                cds_list_for_each_entry (volinfo, &snap->volumes, vol_list) {
                        if (volinfo->status != GLUSTERD_STATUS_STARTED)
                                continue;
                        /* Check the quorum, if quorum is not met, don't start
                         * the bricks
                        */
                        ret = check_quorum_for_brick_start (volinfo,
                                                            node_quorum);
                        if (ret == 0) {
                                gf_msg (this->name, GF_LOG_INFO, 0,
                                        GD_MSG_SERVER_QUORUM_NOT_MET, "Skipping"
                                        " brick restart for volume %s as "
                                        "quorum is not met", volinfo->volname);
                                continue;
                        }
                        if (start_svcs == _gf_false) {
                                start_svcs = _gf_true;
                                glusterd_svcs_manager (volinfo);
                        }
                        start_svcs = _gf_true;
                        gf_msg_debug (this->name, 0, "starting the snap "
                                "volume %s", volinfo->volname);
                        cds_list_for_each_entry (brickinfo, &volinfo->bricks,
                                                 brick_list) {
                                glusterd_brick_start (volinfo, brickinfo,
                                                      _gf_false);
                        }
                }
        }
        ret = 0;

out:
        conf->restart_done = _gf_true;
        return ret;
}

int
_local_gsyncd_start (dict_t *this, char *key, data_t *value, void *data)
{
        char               *path_list                   = NULL;
        char               *slave                       = NULL;
        char               *slave_url                   = NULL;
        char               *slave_vol                   = NULL;
        char               *slave_host                  = NULL;
        char               *statefile                   = NULL;
        char                buf[1024]                   = "faulty";
        int                 uuid_len                    = 0;
        int                 ret                         = 0;
        int                 op_ret                      = 0;
        int                 ret_status                  = 0;
        char                uuid_str[64]                = {0};
        glusterd_volinfo_t *volinfo                     = NULL;
        char                confpath[PATH_MAX]          = "";
        char               *op_errstr                   = NULL;
        glusterd_conf_t    *priv                        = NULL;
        gf_boolean_t        is_template_in_use          = _gf_false;
        gf_boolean_t        is_paused                   = _gf_false;
        char               *key1                        = NULL;
        xlator_t           *this1                       = NULL;

        this1 = THIS;
        GF_ASSERT (this1);
        priv = this1->private;
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
        ret = glusterd_get_slave_info (slave, &slave_url, &slave_host,
                                       &slave_vol, &op_errstr);
        if (ret) {
                gf_msg (this1->name, GF_LOG_ERROR, 0,
                        GD_MSG_SLAVEINFO_FETCH_ERROR,
                        "Unable to fetch slave details.");
                ret = -1;
                goto out;
        }

        ret = snprintf (confpath, sizeof(confpath) - 1,
                        "%s/"GEOREP"/%s_%s_%s/gsyncd.conf",
                        priv->workdir, volinfo->volname,
                        slave_host, slave_vol);
        confpath[ret] = '\0';

        /* Fetching the last status of the node */
        ret = glusterd_get_statefile_name (volinfo, slave,
                                           confpath, &statefile,
                                           &is_template_in_use);
        if (ret) {
                if (!strstr(slave, "::"))
                        gf_msg (this1->name, GF_LOG_INFO, 0,
                                GD_MSG_SLAVE_URL_INVALID,
                                "%s is not a valid slave url.", slave);
                else
                        gf_msg (this1->name, GF_LOG_INFO, 0,
                                GD_MSG_GET_STATEFILE_NAME_FAILED,
                                "Unable to get"
                                " statefile's name");
                goto out;
        }

        /* If state-file entry is missing from the config file,
         * do not start gsyncd on restart */
        if (is_template_in_use) {
                gf_msg (this1->name, GF_LOG_INFO, 0,
                        GD_MSG_NO_STATEFILE_ENTRY,
                        "state-file entry is missing in config file."
                        "Not Restarting");
                goto out;
        }

        is_template_in_use = _gf_false;

        ret = gsync_status (volinfo->volname, slave, confpath,
                            &ret_status, &is_template_in_use);
        if (ret == -1) {
                gf_msg (this1->name, GF_LOG_INFO, 0,
                        GD_MSG_GSYNC_VALIDATION_FAIL,
                        GEOREP" start option validation failed ");
                ret = 0;
                goto out;
        }

        if (is_template_in_use == _gf_true) {
                gf_msg (this1->name, GF_LOG_INFO, 0,
                        GD_MSG_PIDFILE_NOT_FOUND,
                        "pid-file entry is missing in config file."
                        "Not Restarting");
                ret = 0;
                goto out;
        }

        ret = glusterd_gsync_read_frm_status (statefile, buf, sizeof (buf));
        if (ret <= 0) {
                gf_msg (this1->name, GF_LOG_ERROR, 0,
                        GD_MSG_STAT_FILE_READ_FAILED,
                        "Unable to read the status");
                goto out;
        }

        /* Move the pointer two characters ahead to surpass '//' */
        if ((key1 = strchr (slave, '/')))
                key1 = key1 + 2;

        /* Looks for the last status, to find if the session was running
         * when the node went down. If the session was just created or
         * stopped, do not restart the geo-rep session */
        if ((!strcmp (buf, "Created")) ||
            (!strcmp (buf, "Stopped"))) {
                gf_msg (this1->name, GF_LOG_INFO, 0,
                        GD_MSG_GEO_REP_START_FAILED,
                        "Geo-Rep Session was not started between "
                        "%s and %s::%s. Not Restarting", volinfo->volname,
                        slave_url, slave_vol);
                goto out;
        } else if (strstr(buf, "Paused")) {
                is_paused = _gf_true;
        } else if ((!strcmp (buf, "Config Corrupted"))) {
                gf_msg (this1->name, GF_LOG_INFO, 0,
                        GD_MSG_RECOVERING_CORRUPT_CONF,
                        "Recovering from a corrupted config. "
                        "Not Restarting. Use start (force) to "
                        "start the session between %s and %s::%s.",
                        volinfo->volname,
                        slave_url, slave_vol);
                goto out;
        }

        if (is_paused) {
                glusterd_start_gsync (volinfo, slave, path_list, confpath,
                                      uuid_str, NULL, _gf_true);
        }
        else {
               /* Add slave to the dict indicating geo-rep session is running*/
               ret = dict_set_dynstr_with_alloc (volinfo->gsync_active_slaves,
                                                 key1, "running");
               if (ret) {
                       gf_msg (this1->name, GF_LOG_ERROR, 0,
                               GD_MSG_DICT_SET_FAILED,
                               "Unable to set key:%s"
                               " value:running in the dict", key1);
                       goto out;
               }
               ret = glusterd_start_gsync (volinfo, slave, path_list, confpath,
                                      uuid_str, NULL, _gf_false);
               if (ret)
                       dict_del (volinfo->gsync_active_slaves, key1);
        }

out:
        if (statefile)
                GF_FREE (statefile);

        if (is_template_in_use) {
               op_ret = glusterd_create_status_file (volinfo->volname, slave,
                                                     slave_host, slave_vol,
                                                     "Config Corrupted");
               if (op_ret) {
                        gf_msg (this1->name, GF_LOG_ERROR, 0,
                                GD_MSG_STATUSFILE_CREATE_FAILED,
                                "Unable to create status file"
                                ". Error : %s", strerror (errno));
                        ret = op_ret;
               }
        }

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

        cds_list_for_each_entry (volinfo, &conf->volumes, vol_list) {
                glusterd_volume_restart_gsyncds (volinfo);
        }
        return ret;
}

int
glusterd_calc_dist_leaf_count (int rcount, int scount)
{
        return (rcount ? rcount : 1) * (scount ? scount : 1);
}

int
glusterd_get_dist_leaf_count (glusterd_volinfo_t *volinfo)
{
    int rcount = volinfo->replica_count;
    int scount = volinfo->stripe_count;

    if (volinfo->type == GF_CLUSTER_TYPE_DISPERSE)
        return volinfo->disperse_count;

    return glusterd_calc_dist_leaf_count (rcount, scount);
}

int
glusterd_get_brickinfo (xlator_t *this, const char *brickname, int port,
                        glusterd_brickinfo_t **brickinfo)
{
        glusterd_conf_t         *priv = NULL;
        glusterd_volinfo_t      *volinfo = NULL;
        glusterd_brickinfo_t    *tmpbrkinfo = NULL;
        int                     ret = -1;

        GF_ASSERT (brickname);
        GF_ASSERT (this);

        priv = this->private;
        cds_list_for_each_entry (volinfo, &priv->volumes, vol_list) {
                cds_list_for_each_entry (tmpbrkinfo, &volinfo->bricks,
                                         brick_list) {
                        if (gf_uuid_compare (tmpbrkinfo->uuid, MY_UUID))
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

        cds_list_for_each_entry (tmpbrkinfo, &volinfo->bricks, brick_list) {
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
                gf_msg_debug ("glusterd", 0, "Setting brick %s:%s status "
                        "to started", brickinfo->hostname, brickinfo->path);
        } else {
                gf_msg_debug ("glusterd", 0, "Setting brick %s:%s status "
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

        if (gf_uuid_is_null (brickinfo->uuid)) {
                ret = glusterd_resolve_brick (brickinfo);
                if (ret) {
                        GF_ASSERT (0);
                        goto out;
                }
        }
        if (!gf_uuid_compare (brickinfo->uuid, *((uuid_t *)uuid)))
                return 0;
out:
        return -1;
}

int
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
        if (sys_stat (mnt_pt, &brickstat))
                goto err;

        while ((ptr = strrchr (mnt_pt, '/')) &&
               ptr != mnt_pt) {

                *ptr = '\0';
                if (sys_stat (mnt_pt, &buf)) {
                        gf_msg (THIS->name, GF_LOG_ERROR, errno,
                                GD_MSG_FILE_OP_FAILED, "error in "
                                "stat: %s", strerror (errno));
                        goto err;
                }

                if (brickstat.st_dev != buf.st_dev) {
                        *ptr = '/';
                        break;
                }
        }

        if (ptr == mnt_pt) {
                if (sys_stat ("/", &buf)) {
                        gf_msg (THIS->name, GF_LOG_ERROR, errno,
                                GD_MSG_FILE_OP_FAILED, "error in "
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

static struct fs_info {
        char *fs_type_name;
        char *fs_tool_name;
        char *fs_tool_arg;
        char *fs_tool_pattern;
        char *fs_tool_pkg;
} glusterd_fs[] = {
        /* some linux have these in /usr/sbin/and others in /sbin/? */
        { "xfs", "xfs_info", NULL, "isize=", "xfsprogs" },
        { "ext3", "tune2fs", "-l", "Inode size:", "e2fsprogs" },
        { "ext4", "tune2fs", "-l", "Inode size:", "e2fsprogs" },
        { "btrfs", NULL, NULL, NULL, NULL },
        { NULL, NULL, NULL, NULL, NULL}
};

static int
glusterd_add_inode_size_to_dict (dict_t *dict, int count)
{
        int             ret               = -1;
        char            key[1024]         = {0};
        char            buffer[4096]      = {0};
        char           *device            = NULL;
        char           *fs_name           = NULL;
        char           *cur_word          = NULL;
        char           *trail             = NULL;
        runner_t        runner            = {0, };
        struct fs_info *fs                = NULL;
        char            fs_tool_name[256] = {0, };
        static dict_t  *cached_fs         = NULL;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.device", count);
        ret = dict_get_str (dict, key, &device);
        if (ret)
                goto out;

        if (cached_fs) {
                if (dict_get_str (cached_fs, device, &cur_word) == 0) {
                        goto cached;
                }
        } else {
                cached_fs = dict_new ();
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.fs_name", count);
        ret = dict_get_str (dict, key, &fs_name);
        if (ret)
                goto out;

        runinit (&runner);
        runner_redir (&runner, STDOUT_FILENO, RUN_PIPE);

        for (fs = glusterd_fs ; fs->fs_type_name; fs++) {
                if (strcmp (fs_name, fs->fs_type_name) == 0) {
                        snprintf (fs_tool_name, sizeof (fs_tool_name),
                                  "/usr/sbin/%s", fs->fs_tool_name);
                        if (sys_access (fs_tool_name, R_OK|X_OK) == 0)
                                runner_add_arg (&runner, fs_tool_name);
                        else {
                                snprintf (fs_tool_name, sizeof (fs_tool_name),
                                          "/sbin/%s", fs->fs_tool_name);
                                if (sys_access (fs_tool_name, R_OK|X_OK) == 0)
                                        runner_add_arg (&runner, fs_tool_name);
                        }
                        break;
                }
        }

        if (runner.argv[0]) {
                if (fs->fs_tool_arg)
                        runner_add_arg (&runner, fs->fs_tool_arg);
                runner_add_arg (&runner, device);
        } else {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_INODE_SIZE_GET_FAIL,
                        "could not find %s to get"
                        "inode size for %s (%s): %s package missing?",
                        fs->fs_tool_name, device, fs_name, fs->fs_tool_pkg);
                goto out;
        }

        ret = runner_start (&runner);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        GD_MSG_CMD_EXEC_FAIL, "failed to execute "
                        "\"%s\"", fs->fs_tool_name);
                /*
                 * Runner_start might return an error after the child has
                 * been forked, e.g. if the program isn't there.  In that
                 * case, we still need to call runner_end to reap the
                 * child and free resources.  Fortunately, that seems to
                 * be harmless for other kinds of failures.
                 */
                (void) runner_end (&runner);
                goto out;
        }

        for (;;) {
                if (fgets (buffer, sizeof (buffer),
                    runner_chio (&runner, STDOUT_FILENO)) == NULL)
                        break;
                trail = strrchr (buffer, '\n');
                if (trail)
                        *trail = '\0';

                cur_word =
                        glusterd_parse_inode_size (buffer, fs->fs_tool_pattern);

                if (cur_word)
                        break;
        }

        ret = runner_end (&runner);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        GD_MSG_CMD_EXEC_FAIL,
                        "%s exited with non-zero exit status",
                        fs->fs_tool_name);

                goto out;
        }
        if (!cur_word) {
                ret = -1;
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_INODE_SIZE_GET_FAIL,
                        "Unable to retrieve inode size using %s",
                        fs->fs_tool_name);
                goto out;
        }

        if (dict_set_dynstr_with_alloc (cached_fs, device, cur_word)) {
                /* not fatal if not entered into the cache */
                gf_msg_debug (THIS->name, 0,
                        "failed to cache fs inode size for %s", device);
        }

cached:
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.inode_size", count);

        ret = dict_set_dynstr_with_alloc (dict, key, cur_word);

out:
        if (ret)
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_INODE_SIZE_GET_FAIL, "failed to get inode size");
        return ret;
}

struct mntent *
glusterd_get_mnt_entry_info (char *mnt_pt, char *buff, int buflen,
                             struct mntent *entry_ptr)
{
        struct mntent  *entry   = NULL;
        FILE           *mtab    = NULL;

        GF_ASSERT (mnt_pt);
        GF_ASSERT (buff);
        GF_ASSERT (entry_ptr);

        mtab = setmntent (_PATH_MOUNTED, "r");
        if (!mtab)
                goto out;

        entry = getmntent_r (mtab, entry_ptr, buff, buflen);

        while (1) {
                if (!entry)
                        goto out;

                if (!strcmp (entry->mnt_dir, mnt_pt) &&
                    strcmp (entry->mnt_type, "rootfs"))
                        break;
                entry = getmntent_r (mtab, entry_ptr, buff, buflen);
        }

out:
        if (NULL != mtab) {
                endmntent (mtab);
        }
        return entry;
}

static int
glusterd_add_brick_mount_details (glusterd_brickinfo_t *brickinfo,
                                  dict_t *dict, int count)
{
        int             ret                  = -1;
        char            key[1024]            = {0};
        char            buff [PATH_MAX]      = {0};
        char            base_key[1024]       = {0};
        struct mntent   save_entry           = {0};
        char           *mnt_pt               = NULL;
        struct mntent  *entry                = NULL;

        snprintf (base_key, sizeof (base_key), "brick%d", count);

        ret = glusterd_get_brick_root (brickinfo->path, &mnt_pt);
        if (ret)
                goto out;

        entry = glusterd_get_mnt_entry_info (mnt_pt, buff, sizeof (buff),
                                             &save_entry);
        if (!entry) {
                ret = -1;
                goto out;
        }

        /* get device file */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.device", base_key);

        ret = dict_set_dynstr_with_alloc (dict, key, entry->mnt_fsname);
        if (ret)
                goto out;

        /* fs type */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.fs_name", base_key);

        ret = dict_set_dynstr_with_alloc (dict, key, entry->mnt_type);
        if (ret)
                goto out;

        /* mount options */
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.mnt_options", base_key);

        ret = dict_set_dynstr_with_alloc (dict, key, entry->mnt_opts);

 out:
        if (mnt_pt)
                GF_FREE (mnt_pt);

        return ret;
}

char*
glusterd_get_brick_mount_device (char *brick_path)
{
        int             ret                  = -1;
        char           *mnt_pt               = NULL;
        char           *device               = NULL;
        char            buff [PATH_MAX]      = "";
        struct mntent  *entry                = NULL;
        struct mntent   save_entry           = {0,};
        xlator_t       *this                 = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (brick_path);

        ret = glusterd_get_brick_root (brick_path, &mnt_pt);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRICKPATH_ROOT_GET_FAIL,
                        "Failed to get mount point "
                        "for %s brick", brick_path);
                goto out;
        }

        entry = glusterd_get_mnt_entry_info (mnt_pt, buff, sizeof (buff),
                                             &save_entry);
        if (NULL == entry) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MNTENTRY_GET_FAIL,
                        "Failed to get mnt entry "
                        "for %s mount path", mnt_pt);
                goto out;
        }

        /* get the fs_name/device */
        device = gf_strdup (entry->mnt_fsname);

out:
        if (mnt_pt)
                GF_FREE(mnt_pt);

        return device;
}

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

        ret = sys_statvfs (brickinfo->path, &brickstat);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_FILE_OP_FAILED, "statfs error: %s ",
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

        ret = glusterd_add_brick_mount_details (brickinfo, dict, count);
        if (ret)
                goto out;

        ret = glusterd_add_inode_size_to_dict (dict, count);
 out:
        if (ret)
                gf_msg_debug (this->name, 0, "Error adding brick"
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
        char            key[1024]             = {0};
        char            base_key[1024]        = {0};
        char            pidfile[PATH_MAX]     = {0};
        xlator_t        *this                 = NULL;
        glusterd_conf_t *priv                 = NULL;
        gf_boolean_t    brick_online          = _gf_false;

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
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.peerid", base_key);
        ret = dict_set_dynstr_with_alloc (dict, key,
                                          uuid_utoa (brickinfo->uuid));
        if (ret) {
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.port", base_key);
        ret = dict_set_int32 (dict, key, (volinfo->transport_type ==
                              GF_TRANSPORT_RDMA) ? 0 : brickinfo->port);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.rdma_port", base_key);
        if (volinfo->transport_type == GF_TRANSPORT_RDMA) {
                ret = dict_set_int32 (dict, key, brickinfo->port);
        } else if (volinfo->transport_type == GF_TRANSPORT_BOTH_TCP_RDMA) {
                ret = dict_set_int32 (dict, key, brickinfo->rdma_port);
        } else
                ret = dict_set_int32 (dict, key, 0);

        if (ret)
                goto out;

        GLUSTERD_GET_BRICK_PIDFILE (pidfile, volinfo, brickinfo, priv);

        if (glusterd_is_brick_started (brickinfo)) {
                if (gf_is_service_running (pidfile, &pid)) {
                        brick_online = _gf_true;
                }
        }

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
                gf_msg_debug (this->name, 0, "Returning %d", ret);

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

        cds_list_for_each_entry (entry, &priv->volumes, vol_list) {
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
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED, "failed to get all "
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

        cds_list_for_each_entry (volinfo, &priv->volumes, vol_list) {
                cds_list_for_each_entry (brickinfo, &volinfo->bricks,
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
        gf_msg_debug ("glusterd", 0, "returning %d", ret);
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

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        if ((!brickinfo) || (!volinfo))
                goto out;

        if (gf_uuid_is_null (brickinfo->uuid)) {
                ret = glusterd_resolve_brick (brickinfo);
                if (ret) {
                        gf_event (EVENT_BRICKPATH_RESOLVE_FAILED,
                                  "peer=%s;volume=%s;brick=%s",
                                  brickinfo->hostname, volinfo->volname,
                                  brickinfo->path);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_RESOLVE_BRICK_FAIL, FMTSTR_RESOLVE_BRICK,
                                brickinfo->hostname, brickinfo->path);
                        goto out;
                }
        }

        if (gf_uuid_compare (brickinfo->uuid, MY_UUID)) {
                ret = 0;
                if (del_brick)
                        glusterd_delete_brick (volinfo, brickinfo);
                goto out;
        }

        gf_msg_debug (this->name, 0, "About to stop glusterfs"
                " for brick %s:%s", brickinfo->hostname,
                brickinfo->path);
        ret = glusterd_volume_stop_glusterfs (volinfo, brickinfo, del_brick);
        if (ret) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        GD_MSG_BRICK_STOP_FAIL, "Unable to stop"
                        " brick: %s:%s", brickinfo->hostname,
                        brickinfo->path);
                goto out;
        }

out:
        gf_msg_debug (this->name, 0, "returning %d ", ret);
        return ret;
}

gf_boolean_t
glusterd_is_tier_daemon_running (glusterd_volinfo_t *volinfo)
{
        if (volinfo->type != GF_CLUSTER_TYPE_TIER)
                return _gf_false;

        if (volinfo->rebal.defrag &&
            volinfo->rebal.defrag_cmd == GF_DEFRAG_CMD_START_TIER) {
                return _gf_true;
        }

        return _gf_false;

}


int
glusterd_is_defrag_on (glusterd_volinfo_t *volinfo)
{
        /*
         * Do not need to consider tier daemon as a rebalance
         * daemon and with current design rebalance is not supported
         * on a tiered volume.
         */

        if (glusterd_is_tier_daemon_running (volinfo))
                return 0;

        return (volinfo->rebal.defrag != NULL);
}

int
glusterd_new_brick_validate (char *brick, glusterd_brickinfo_t *brickinfo,
                             char *op_errstr, size_t len, char *op)
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
                ret = glusterd_brickinfo_new_from_brick (brick, &newbrickinfo,
                                                         _gf_true, NULL);
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

        if (!gf_uuid_compare (MY_UUID, newbrickinfo->uuid)) {
                /* brick is local */
                if (!glusterd_is_brickpath_available (newbrickinfo->uuid,
                                                      newbrickinfo->path)) {
                        snprintf(op_errstr, len, "Brick: %s not available."
                                 " Brick may be containing or be contained "
                                 "by an existing brick.", brick);
                        if (op && (!strcmp (op, "GF_RESET_OP_COMMIT") ||
                            !strcmp (op, "GF_RESET_OP_COMMIT_FORCE")))
                                ret = 1;
                        else
                                ret = -1;
                        goto out;
                }

        } else {
                peerinfo = glusterd_peerinfo_find_by_uuid (newbrickinfo->uuid);
                if (peerinfo == NULL) {
                        ret = -1;
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRICK_VALIDATE_FAIL, "%s", op_errstr);
        gf_msg_debug (this->name, 0, "returning %d ", ret);
        return ret;
}

int
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
                gf_msg("glusterd", GF_LOG_ERROR, 0,
                       GD_MSG_RB_SRC_BRICKS_MISMATCH,
                       "Replace brick src bricks differ");
                return -1;
        }

        if (strcmp (rb->dst_brick->hostname, dst->hostname) ||
            strcmp (rb->dst_brick->path, dst->path)) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_RB_DST_BRICKS_MISMATCH,
                        "Replace brick dst bricks differ");
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

        strncpy (dir, path, (sizeof (dir) - 1));
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
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        GD_MSG_PATH_ALREADY_PART_OF_VOL, "%s", msg);
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
        if (ret == -1) {
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
        if (ret == -1) {
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
        ret = dict_set_dynstr_with_alloc (dict, key, timestr);
        if (ret)
                goto out;

out:
        gf_msg_debug ("glusterd", 0, "returning %d", ret);
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
        gf_msg_debug ("glusterd", 0, "returning %d", ret);
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
        gf_msg_debug ("glusterd", 0, "returning %d", ret);
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
        gf_msg_debug (this->name, 0, "Transitioning from '%s' to '%s' "
                "due to event '%s'", log->state_name_get (old_state),
                log->state_name_get (new_state), log->event_name_get (event));
out:
        gf_msg_debug (this->name, 0, "returning %d", ret);
        return ret;
}

int
glusterd_remove_pending_entry (struct cds_list_head *list, void *elem)
{
        glusterd_pending_node_t *pending_node = NULL;
        glusterd_pending_node_t *tmp = NULL;
        int                     ret = 0;

        cds_list_for_each_entry_safe (pending_node, tmp, list, list) {
                if (elem == pending_node->node) {
                        cds_list_del_init (&pending_node->list);
                        GF_FREE (pending_node);
                        ret = 0;
                        goto out;
                }
        }
out:
        gf_msg_debug (THIS->name, 0, "returning %d", ret);
        return ret;

}

int
glusterd_clear_pending_nodes (struct cds_list_head *list)
{
        glusterd_pending_node_t *pending_node = NULL;
        glusterd_pending_node_t *tmp = NULL;

        cds_list_for_each_entry_safe (pending_node, tmp, list, list) {
                cds_list_del_init (&pending_node->list);
                GF_FREE (pending_node);
        }

        return 0;
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
        gf_msg_debug (THIS->name, 0, "returning %d", ret);
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

        cds_list_for_each_entry_safe (brickinfo, tmp, &volinfo->bricks,
                                      brick_list) {
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
                gf_msg_debug ("glusterd", 0, "Could not allocate memory.");
                ret = -1;
                goto out;
        }

        cds_list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (gf_uuid_compare (brickinfo->uuid, MY_UUID))
                        continue;

                 pathlen = snprintf (path, sizeof(path),
                                     "--path=%s ", brickinfo->path);
                 if (pathlen < sizeof(path))
                        path[pathlen] = '\0';
                 else
                        path[sizeof(path)-1] = '\0';
                 path_tokens[count] = gf_strdup (path);
                 if (!path_tokens[count]) {
                        gf_msg_debug ("glusterd", 0,
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
                gf_msg_debug ("glusterd", 0, "Could not allocate memory.");
                ret = -1;
                goto out;
        }

        for (i = 0; i < count; i++)
                strcat (tmp_path_list, path_tokens[i]);

        if (count)
                *pathlist = tmp_path_list;

        ret = count;
out:
        if (path_tokens) {
                for (i = 0; i < count; i++) {
                        GF_FREE (path_tokens[i]);
                }
        }

        GF_FREE (path_tokens);
        path_tokens = NULL;

        if (ret == 0) {
                gf_msg_debug ("glusterd", 0, "No Local Bricks Present.");
                GF_FREE (tmp_path_list);
                tmp_path_list = NULL;
        }

        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
        return ret;
}

int
glusterd_start_gsync (glusterd_volinfo_t *master_vol, char *slave,
                      char *path_list, char *conf_path,
                      char *glusterd_uuid_str,
                      char **op_errstr, gf_boolean_t is_pause)
{
        int32_t         ret     = 0;
        int32_t         status  = 0;
        char            uuid_str [64] = {0};
        runner_t        runner = {0,};
        xlator_t        *this = NULL;
        glusterd_conf_t *priv = NULL;
        int             errcode = 0;
        gf_boolean_t    is_template_in_use = _gf_false;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        uuid_utoa_r (MY_UUID, uuid_str);

        if (!path_list) {
                ret = 0;
                gf_msg_debug ("glusterd", 0, "No Bricks in this node."
                        " Not starting gsyncd.");
                goto out;
        }

        ret = gsync_status (master_vol->volname, slave, conf_path,
                            &status, &is_template_in_use);
        if (status == 0)
                goto out;

        if (is_template_in_use == _gf_true) {
                gf_asprintf (op_errstr, GEOREP" start failed for %s %s : "
                             "pid-file entry missing in config file",
                             master_vol->volname, slave);
                ret = -1;
                goto out;
        }

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
        runner_argprintf (&runner, "--iprefix=%s", DATADIR);
        runner_argprintf (&runner, ":%s", master_vol->volname);
        runner_argprintf (&runner, "--glusterd-uuid=%s",
                          uuid_utoa (priv->uuid));
        runner_add_arg   (&runner, slave);
        if (is_pause)
                runner_add_arg   (&runner, "--pause-on-start");
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

        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_recreate_volfiles (glusterd_conf_t *conf)
{

        glusterd_volinfo_t      *volinfo = NULL;
        int                      ret = 0;
        int                      op_ret = 0;

        GF_ASSERT (conf);

        cds_list_for_each_entry (volinfo, &conf->volumes, vol_list) {
                ret = generate_brick_volfiles (volinfo);
                if (ret) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_VOLFILE_CREATE_FAIL, "Failed to "
                                "regenerate brick volfiles for %s",
                                volinfo->volname);
                        op_ret = ret;
                }
                ret = generate_client_volfiles (volinfo, GF_CLIENT_TRUSTED);
                if (ret) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_VOLFILE_CREATE_FAIL, "Failed to "
                                "regenerate trusted client volfiles for %s",
                                volinfo->volname);
                        op_ret = ret;
                }
                ret = generate_client_volfiles (volinfo, GF_CLIENT_OTHER);
                if (ret) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_VOLFILE_CREATE_FAIL, "Failed to "
                                "regenerate client volfiles for %s",
                                volinfo->volname);
                        op_ret = ret;
                }
        }
        return op_ret;
}

int32_t
glusterd_handle_upgrade_downgrade (dict_t *options, glusterd_conf_t *conf,
                                   gf_boolean_t upgrade, gf_boolean_t downgrade)
{
        int              ret                            = 0;
        gf_boolean_t     regenerate_volfiles            = _gf_false;
        gf_boolean_t     terminate                      = _gf_false;

        if (_gf_true == upgrade)
                regenerate_volfiles = _gf_true;

        if (upgrade && downgrade) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_WRONG_OPTS_SETTING, "Both upgrade and downgrade"
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

static inline int
glusterd_is_replica_volume (int type)
{
        if (type == GF_CLUSTER_TYPE_REPLICATE ||
            type == GF_CLUSTER_TYPE_STRIPE_REPLICATE)
                return 1;
        return 0;
}
gf_boolean_t
glusterd_is_volume_replicate (glusterd_volinfo_t *volinfo)
{
        gf_boolean_t    replicates = _gf_false;
        if (volinfo->type == GF_CLUSTER_TYPE_TIER) {
                replicates = glusterd_is_replica_volume
                                            (volinfo->tier_info.cold_type) |
                             glusterd_is_replica_volume
                                            (volinfo->tier_info.hot_type);
                return replicates;
       }
       return glusterd_is_replica_volume ((volinfo->type));
}

gf_boolean_t
glusterd_is_shd_compatible_type (int type)
{
        switch (type) {
        case GF_CLUSTER_TYPE_REPLICATE:
        case GF_CLUSTER_TYPE_STRIPE_REPLICATE:
        case GF_CLUSTER_TYPE_DISPERSE:
                return _gf_true;

        }
        return _gf_false;
}

gf_boolean_t
glusterd_is_shd_compatible_volume (glusterd_volinfo_t *volinfo)
{

        int     ret     = 0;

        if (volinfo->type == GF_CLUSTER_TYPE_TIER) {
                ret = glusterd_is_shd_compatible_type
                                         (volinfo->tier_info.cold_type) |
                      glusterd_is_shd_compatible_type
                                         (volinfo->tier_info.hot_type);
                return ret;
        }
        return glusterd_is_shd_compatible_type (volinfo->type);
}

int
glusterd_set_dump_options (char *dumpoptions_path, char *options,
                           int option_cnt)
{
        int              ret         = 0;
        char            *dup_options = NULL;
        char            *option      = NULL;
        char            *tmpptr      = NULL;
        FILE            *fp          = NULL;
        int              nfs_cnt     = 0;
        xlator_t        *this        = NULL;
        glusterd_conf_t *priv        = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

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
        gf_msg ("glusterd", GF_LOG_INFO, 0,
                GD_MSG_STATEDUMP_OPTS_RCVD,
                "Received following statedump options: %s",
                dup_options);
        option = strtok_r (dup_options, " ", &tmpptr);
        while (option) {
                if (!strcmp (option, priv->nfs_svc.name)) {
                        if (nfs_cnt > 0) {
                                sys_unlink (dumpoptions_path);
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


static int
glusterd_brick_signal (glusterd_volinfo_t *volinfo,
                       glusterd_brickinfo_t *brickinfo,
                       char *options, int option_cnt, char **op_errstr,
                       int sig)
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

        if (gf_uuid_is_null (brickinfo->uuid)) {
                ret = glusterd_resolve_brick (brickinfo);
                if (ret) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_RESOLVE_BRICK_FAIL,
                                "Cannot resolve brick %s:%s",
                                brickinfo->hostname, brickinfo->path);
                        goto out;
                }
        }

        if (gf_uuid_compare (brickinfo->uuid, MY_UUID)) {
                ret = 0;
                goto out;
        }

        GLUSTERD_GET_BRICK_PIDFILE (pidfile_path, volinfo, brickinfo, conf);

        /* TBD: use gf_is_service_running instead of almost-identical code? */
        pidfile = fopen (pidfile_path, "r");
        if (!pidfile) {
                gf_msg ("glusterd", GF_LOG_ERROR, errno,
                        GD_MSG_FILE_OP_FAILED, "Unable to open pidfile: %s",
                        pidfile_path);
                ret = -1;
                goto out;
        }

        ret = fscanf (pidfile, "%d", &pid);
        if (ret <= 0) {
                gf_msg ("glusterd", GF_LOG_ERROR, errno,
                        GD_MSG_FILE_OP_FAILED,
                        "Unable to get pid of brick process");
                ret = -1;
                goto out;
        }

        if (pid == 0) {
                gf_msg ("glusterd", GF_LOG_WARNING, 0,
                        GD_MSG_NO_SIG_TO_PID_ZERO,
                        "refusing to send signal %d to pid zero", sig);
                goto out;
        }

        if (sig == SIGUSR1) {
                snprintf (dumpoptions_path, sizeof (dumpoptions_path),
                          DEFAULT_VAR_RUN_DIRECTORY"/glusterdump.%d.options",
                          pid);
                ret = glusterd_set_dump_options (dumpoptions_path, options,
                                                 option_cnt);
                if (ret < 0) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                               GD_MSG_BRK_STATEDUMP_FAIL,
                               "error while parsing the statedump "
                                "options");
                        ret = -1;
                        goto out;
                }
        }

        gf_msg ("glusterd", GF_LOG_INFO, 0,
                GD_MSG_STATEDUMP_INFO,
                "sending signal %d to brick with pid %d",
                sig, pid);

        kill (pid, sig);

        sleep (1);
        ret = 0;
out:
        sys_unlink (dumpoptions_path);
        if (pidfile)
                fclose (pidfile);
        return ret;
}

int
glusterd_brick_statedump (glusterd_volinfo_t *volinfo,
                          glusterd_brickinfo_t *brickinfo,
                          char *options, int option_cnt, char **op_errstr)
{
        return glusterd_brick_signal (volinfo, brickinfo,
                                      options, option_cnt, op_errstr,
                                      SIGUSR1);
}

int
glusterd_brick_terminate (glusterd_volinfo_t *volinfo,
                          glusterd_brickinfo_t *brickinfo,
                          char *options, int option_cnt, char **op_errstr)
{
        return glusterd_brick_signal (volinfo, brickinfo,
                                      options, option_cnt, op_errstr,
                                      SIGTERM);
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
        if (strcmp (option, conf->nfs_svc.name)) {
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
                gf_msg ("glusterd", GF_LOG_ERROR, errno,
                        GD_MSG_FILE_OP_FAILED, "Unable to open pidfile: %s",
                        pidfile_path);
                ret = -1;
                goto out;
        }

        ret = fscanf (pidfile, "%d", &pid);
        if (ret <= 0) {
                gf_msg ("glusterd", GF_LOG_ERROR, errno,
                        GD_MSG_FILE_OP_FAILED,
                        "Unable to get pid of brick process");
                ret = -1;
                goto out;
        }

        snprintf (dumpoptions_path, sizeof (dumpoptions_path),
                  DEFAULT_VAR_RUN_DIRECTORY"/glusterdump.%d.options", pid);
        ret = glusterd_set_dump_options (dumpoptions_path, options, option_cnt);
        if (ret < 0) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_BRK_STATEDUMP_FAIL,
                        "error while parsing the statedump "
                        "options");
                ret = -1;
                goto out;
        }

        gf_msg ("glusterd", GF_LOG_INFO, 0,
                GD_MSG_STATEDUMP_INFO,
                "Performing statedump on nfs server with "
                "pid %d", pid);

        kill (pid, SIGUSR1);

        sleep (1);

        ret = 0;
out:
        if (pidfile)
                fclose (pidfile);
        sys_unlink (dumpoptions_path);
        GF_FREE (dup_options);
        return ret;
}

int
glusterd_client_statedump (char *volname, char *options, int option_cnt,
                           char **op_errstr)
{
        int                      ret                = 0;
        char                    *dup_options        = NULL;
        char                    *option             = NULL;
        char                    *tmpptr             = NULL;
        char                     msg[256]           = {0,};
        char                    *target_ip          = NULL;
        char                    *pid                = NULL;

        dup_options = gf_strdup (options);
        option = strtok_r (dup_options, " ", &tmpptr);
        if (strcmp (option, "client")) {
                snprintf (msg, sizeof (msg), "for gluster client statedump, options "
                          "should be after the key 'client'");
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }
        target_ip = strtok_r (NULL, " ", &tmpptr);
        if (target_ip == NULL) {
                snprintf (msg, sizeof (msg), "ip address not specified");
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }

        pid = strtok_r (NULL, " ", &tmpptr);
        if (pid == NULL) {
                snprintf (msg, sizeof (msg), "pid not specified");
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }

        ret = glusterd_client_statedump_submit_req (volname, target_ip, pid);
out:
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
        if (strcmp (option, conf->quotad_svc.name)) {
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
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_FILE_OP_FAILED, "Unable to open pidfile: %s",
                        pidfile_path);
                ret = -1;
                goto out;
        }

        ret = fscanf (pidfile, "%d", &pid);
        if (ret <= 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_FILE_OP_FAILED, "Unable to get pid of quotad "
                        "process");
                ret = -1;
                goto out;
        }

        snprintf (dumpoptions_path, sizeof (dumpoptions_path),
                  DEFAULT_VAR_RUN_DIRECTORY"/glusterdump.%d.options", pid);
        ret = glusterd_set_dump_options (dumpoptions_path, options, option_cnt);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRK_STATEDUMP_FAIL, "error while parsing "
                        "statedump options");
                ret = -1;
                goto out;
        }

        gf_msg (this->name, GF_LOG_INFO, 0,
                GD_MSG_STATEDUMP_INFO,
                "Performing statedump on quotad with "
                "pid %d", pid);

        kill (pid, SIGUSR1);

        sleep (1);

        ret = 0;
out:
        if (pidfile)
                fclose (pidfile);
        sys_unlink (dumpoptions_path);
        GF_FREE (dup_options);
        return ret;
}

/* Checks if the given peer contains bricks belonging to the given volume.
 * Returns,
 *   2 - if peer contains all the bricks
 *   1 - if peer contains at least 1 brick
 *   0 - if peer contains no bricks
 */
int
glusterd_friend_contains_vol_bricks (glusterd_volinfo_t *volinfo,
                                     uuid_t friend_uuid)
{
        int                     ret = 0;
        glusterd_brickinfo_t    *brickinfo = NULL;
        int                     count = 0;

        GF_ASSERT (volinfo);

        cds_list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (!gf_uuid_compare (brickinfo->uuid, friend_uuid)) {
                        count++;
                }
        }

        if (count) {
                if (count == volinfo->brick_count)
                        ret = 2;
                else
                        ret = 1;
        }
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);
        return ret;
}

/* Cleanup the stale volumes left behind in the cluster. The volumes which are
 * contained completely within the detached peer are stale with respect to the
 * cluster.
 */
int
glusterd_friend_remove_cleanup_vols (uuid_t uuid)
{
        int                     ret           = -1;
        glusterd_conf_t         *priv         = NULL;
        glusterd_svc_t          *svc          = NULL;
        glusterd_volinfo_t      *volinfo      = NULL;
        glusterd_volinfo_t      *tmp_volinfo  = NULL;

        priv = THIS->private;
        GF_ASSERT (priv);

        cds_list_for_each_entry_safe (volinfo, tmp_volinfo, &priv->volumes,
                                      vol_list) {
                if (!glusterd_friend_contains_vol_bricks (volinfo,
                                                          MY_UUID)) {
                        /*Stop snapd daemon service if snapd daemon is running*/
                        if (!volinfo->is_snap_volume) {
                                svc = &(volinfo->snapd.svc);
                                ret = svc->stop (svc, SIGTERM);
                                if (ret) {
                                        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                                GD_MSG_SVC_STOP_FAIL, "Failed "
                                                "to stop snapd daemon service");
                                }
                        }
                        if (volinfo->type == GF_CLUSTER_TYPE_TIER) {
                                svc = &(volinfo->tierd.svc);
                                ret = svc->stop (svc, SIGTERM);
                                if (ret) {
                                        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                                GD_MSG_SVC_STOP_FAIL, "Failed "
                                                "to stop tierd daemon service");
                                }
                        }
                }

                if (glusterd_friend_contains_vol_bricks (volinfo, uuid) == 2) {
                        gf_msg (THIS->name, GF_LOG_INFO, 0,
                                GD_MSG_STALE_VOL_DELETE_INFO,
                                "Deleting stale volume %s", volinfo->volname);
                        ret = glusterd_delete_volume (volinfo);
                        if (ret) {
                                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                        GD_MSG_STALE_VOL_REMOVE_FAIL,
                                        "Error deleting stale volume");
                                goto out;
                        }
                }

        }

        /* Reconfigure all daemon services upon peer detach */
        ret = glusterd_svcs_reconfigure ();
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_SVC_STOP_FAIL,
                        "Failed to reconfigure all daemon services.");
        }
        ret = 0;
out:
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_get_bitd_filepath (char *filepath, glusterd_volinfo_t *volinfo)
{
        int   ret             = 0;
        char  path[PATH_MAX]  = {0,};
        glusterd_conf_t *priv = NULL;

        priv = THIS->private;

        GLUSTERD_GET_VOLUME_DIR (path, volinfo, priv);

        snprintf (filepath, PATH_MAX,
                  "%s/%s-bitd.vol", path, volinfo->volname);

        return ret;
}

int
glusterd_get_client_filepath (char *filepath, glusterd_volinfo_t *volinfo,
                              gf_transport_type type)
{
        int   ret             = 0;
        char  path[PATH_MAX]  = {0,};
        glusterd_conf_t *priv = NULL;

        priv = THIS->private;

        GLUSTERD_GET_VOLUME_DIR (path, volinfo, priv);

        switch (type) {
        case GF_TRANSPORT_TCP:
                snprintf (filepath, PATH_MAX,
                          "%s/%s.tcp-fuse.vol", path, volinfo->volname);
                break;

        case GF_TRANSPORT_RDMA:
                snprintf (filepath, PATH_MAX,
                          "%s/%s.rdma-fuse.vol", path, volinfo->volname);
                break;
        default:
                ret = -1;
                break;
        }

        return ret;
}

int
glusterd_get_trusted_client_filepath (char *filepath,
                                      glusterd_volinfo_t *volinfo,
                                      gf_transport_type type)
{
        int   ret             = 0;
        char  path[PATH_MAX]  = {0,};
        glusterd_conf_t *priv = NULL;

        priv = THIS->private;

        GLUSTERD_GET_VOLUME_DIR (path, volinfo, priv);

        switch (type) {
        case GF_TRANSPORT_TCP:
                snprintf (filepath, PATH_MAX, "%s/trusted-%s.tcp-fuse.vol",
                          path, volinfo->volname);
                break;

        case GF_TRANSPORT_RDMA:
                snprintf (filepath, PATH_MAX, "%s/trusted-%s.rdma-fuse.vol",
                          path, volinfo->volname);
                break;
        default:
                ret = -1;
                break;
        }

        return ret;
}

void glusterd_update_tier_status (glusterd_volinfo_t *volinfo) {

        glusterd_rebalance_t    *rebal       = NULL;

        rebal = &volinfo->rebal;

        if (volinfo->type != GF_CLUSTER_TYPE_TIER)
                return;

        /*
         * If tier process status is stopped or failed, then
         * manually changing the status.
         */

        switch (rebal->defrag_status) {
        case GF_DEFRAG_STATUS_FAILED:
        case GF_DEFRAG_STATUS_STOPPED:
                rebal->defrag_status = GF_DEFRAG_STATUS_STARTED;
                break;
        default:
                break;
        }
        return;
}

int
glusterd_get_dummy_client_filepath (char *filepath,
                                    glusterd_volinfo_t *volinfo,
                                    gf_transport_type type)
{
        int   ret             = 0;

        switch (type) {
        case GF_TRANSPORT_TCP:
        case GF_TRANSPORT_BOTH_TCP_RDMA:
                snprintf (filepath, PATH_MAX,
                          "/tmp/%s.tcp-fuse.vol", volinfo->volname);
                break;

        case GF_TRANSPORT_RDMA:
                snprintf (filepath, PATH_MAX,
                          "/tmp/%s.rdma-fuse.vol", volinfo->volname);
                break;
        default:
                ret = -1;
                break;
        }

        return ret;
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

        /*
         * Changing the status of tier process to start the daemon
         * forcefully.
         */
        glusterd_update_tier_status (volinfo);

        switch (volinfo->rebal.defrag_status) {
        case GF_DEFRAG_STATUS_COMPLETE:
        case GF_DEFRAG_STATUS_STOPPED:
        case GF_DEFRAG_STATUS_FAILED:
                break;
        case GF_DEFRAG_STATUS_STARTED:
                GLUSTERD_GET_DEFRAG_PID_FILE(pidfile, volinfo, priv);
                if (gf_is_service_running (pidfile, &pid)) {
                        ret = glusterd_rebalance_defrag_init (volinfo, cbk);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_REBALANCE_START_FAIL,
                                        "Failed to initialize  defrag."
                                        "Not starting rebalance process for "
                                        "%s.", volinfo->volname);
                                gf_event (EVENT_REBALANCE_START_FAILED,
                                          "volume=%s", volinfo->volname);
                                goto out;
                        }
                        ret = glusterd_rebalance_rpc_create (volinfo);
                        break;
                }
        case GF_DEFRAG_STATUS_NOT_STARTED:
                ret = glusterd_handle_defrag_start (volinfo, op_errstr, len,
                                cmd, cbk, volinfo->rebal.op);
                if (ret) {
                        volinfo->rebal.defrag_status = GF_DEFRAG_STATUS_FAILED;
                        gf_event (EVENT_REBALANCE_START_FAILED,
                                  "volume=%s", volinfo->volname);
                }
                break;
        default:
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REBALANCE_START_FAIL,
                        "Unknown defrag status (%d)."
                        "Not starting rebalance process for %s.",
                        volinfo->rebal.defrag_status, volinfo->volname);
                break;
        }
out:
        return ret;

}

void
glusterd_defrag_info_set (glusterd_volinfo_t *volinfo, dict_t *dict, int cmd,
                          int status, int op)
{

        xlator_t                *this        = NULL;
        int                      ret         = -1;
        char                    *task_id_str = NULL;
        glusterd_rebalance_t    *rebal       = NULL;

        this = THIS;
        rebal = &volinfo->rebal;

        rebal->defrag_cmd = cmd;
        rebal->defrag_status = status;
        rebal->op = op;

        if (gf_uuid_is_null (rebal->rebalance_id))
                return;

        if (is_origin_glusterd (dict)) {

                ret = glusterd_generate_and_set_task_id(dict,
                                                        GF_REBALANCE_TID_KEY);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_TASKID_GEN_FAIL,
                                "Failed to generate task-id");
                        goto out;
                }
        }
        ret = dict_get_str (dict, GF_REBALANCE_TID_KEY,
                            &task_id_str);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_REBALANCE_ID_MISSING, "Missing rebalance-id");
                ret = 0;
                goto out;
        }

        gf_uuid_parse (task_id_str, rebal->rebalance_id);
out:

        if (ret) {
                gf_msg_debug (this->name, 0,
                                "Rebalance start validate failed");
        }
        return;

}


int
glusterd_restart_rebalance_for_volume (glusterd_volinfo_t *volinfo)
{
        int             ret = -1;
        char          op_errstr[PATH_MAX];

        if (!gd_should_i_start_rebalance (volinfo)) {

                /* Store the rebalance-id and rebalance command even if
                 * the peer isn't starting a rebalance process. On peers
                 * where a rebalance process is started,
                 * glusterd_handle_defrag_start performs the storing.
                 *
                 * Storing this is needed for having 'volume status'
                 * work correctly.
                 */
                volinfo->rebal.defrag_status = GF_DEFRAG_STATUS_NOT_STARTED;
                if (volinfo->type == GF_CLUSTER_TYPE_TIER)
                        glusterd_store_perform_node_state_store (volinfo);

                return 0;
        }
        if (!volinfo->rebal.defrag_cmd) {
                volinfo->rebal.defrag_status = GF_DEFRAG_STATUS_FAILED;
                return -1;
        }

        ret = glusterd_volume_defrag_restart (volinfo, op_errstr, PATH_MAX,
                                volinfo->rebal.defrag_cmd,
                                volinfo->rebal.op == GD_OP_REMOVE_BRICK ?
                                glusterd_remove_brick_migrate_cbk : NULL);
        if (!ret) {
                /* If remove brick is started then ensure that on a glusterd
                 * restart decommission_is_in_progress is set to avoid remove
                 * brick commit to happen when rebalance is not completed.
                 */
                if (volinfo->rebal.op == GD_OP_REMOVE_BRICK &&
                    volinfo->rebal.defrag_status == GF_DEFRAG_STATUS_STARTED) {
                        volinfo->decommission_in_progress = 1;
                }
        }
        return ret;
}
int
glusterd_restart_rebalance (glusterd_conf_t *conf)
{
        glusterd_volinfo_t       *volinfo = NULL;
        int                      ret = 0;

        cds_list_for_each_entry (volinfo, &conf->volumes, vol_list) {
                glusterd_restart_rebalance_for_volume (volinfo);
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

gf_boolean_t
glusterd_is_local_brick (xlator_t *this, glusterd_volinfo_t *volinfo,
                         glusterd_brickinfo_t *brickinfo)
{
        gf_boolean_t    local = _gf_false;
        int             ret = 0;

        if (gf_uuid_is_null (brickinfo->uuid)) {
                ret = glusterd_resolve_brick (brickinfo);
                if (ret)
                        goto out;
        }
        local = !gf_uuid_compare (brickinfo->uuid, MY_UUID);
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Failed to get volume id for "
                        "volume %s", volinfo->volname);
                goto out;
        }
        ret = gf_uuid_parse (volid_str, vol_uid);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_UUID_PARSE_FAIL,
                        "Failed to parse volume id "
                        "for volume %s", volinfo->volname);
                goto out;
        }

        if (gf_uuid_compare (vol_uid, volinfo->volume_id)) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOL_ID_MISMATCH, "Volume ids of volume %s - %s"
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
                                      dict_t *rsp_dict, int32_t cmd)
{
        int                             ret = 0;
        int                             ret2 = 0;
        uint64_t                        files = 0;
        uint64_t                        size = 0;
        uint64_t                        lookup = 0;
        gf_defrag_status_t              status = GF_DEFRAG_STATUS_NOT_STARTED;
        uint64_t                        failures = 0;
        uint64_t                        skipped = 0;
        xlator_t                       *this = NULL;
        double                          run_time = 0;
        uint64_t                        promoted = 0;
        uint64_t                        demoted = 0;
        uint64_t                        time_left = 0;

        this = THIS;

        ret = dict_get_uint64 (rsp_dict, "files", &files);
        if (ret)
                gf_msg_trace (this->name, 0,
                        "failed to get file count");

        ret = dict_get_uint64 (rsp_dict, "size", &size);
        if (ret)
                gf_msg_trace (this->name, 0,
                        "failed to get size of xfer");

        ret = dict_get_uint64 (rsp_dict, "lookups", &lookup);
        if (ret)
                gf_msg_trace (this->name, 0,
                        "failed to get lookedup file count");

        ret = dict_get_int32 (rsp_dict, "status", (int32_t *)&status);
        if (ret)
                gf_msg_trace (this->name, 0,
                        "failed to get status");

        ret = dict_get_uint64 (rsp_dict, "failures", &failures);
        if (ret)
                gf_msg_trace (this->name, 0,
                        "failed to get failure count");

        ret = dict_get_uint64 (rsp_dict, "skipped", &skipped);
        if (ret)
                gf_msg_trace (this->name, 0,
                        "failed to get skipped count");

        ret = dict_get_uint64 (rsp_dict, "promoted", &promoted);
        if (ret)
                gf_msg_trace (this->name, 0,
                        "failed to get promoted count");

        ret = dict_get_uint64 (rsp_dict, "demoted", &demoted);
        if (ret)
                gf_msg_trace (this->name, 0,
                        "failed to get demoted count");

        ret = dict_get_double (rsp_dict, "run-time", &run_time);
        if (ret)
                gf_msg_trace (this->name, 0,
                        "failed to get run-time");

        ret2 = dict_get_uint64 (rsp_dict, "time-left", &time_left);
        if (ret2)
                gf_msg_trace (this->name, 0,
                        "failed to get time left");

        if (cmd == GF_DEFRAG_CMD_STATUS_TIER) {
                if (files)
                        volinfo->tier.rebalance_files = files;
                if (size)
                        volinfo->tier.rebalance_data = size;
                if (lookup)
                        volinfo->tier.lookedup_files = lookup;
                if (status)
                        volinfo->tier.defrag_status = status;
                if (failures)
                        volinfo->tier.rebalance_failures = failures;
                if (skipped)
                        volinfo->tier.skipped_files = skipped;
                if (run_time)
                        volinfo->tier.rebalance_time = run_time;
        } else {
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
                if (!ret2)
                        volinfo->rebal.time_left = time_left;
        }

        if (promoted)
                volinfo->tier_info.promoted = promoted;
        if (demoted)
                volinfo->tier_info.demoted = demoted;


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
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_FILE_OP_FAILED,
                        "fopen() on file: %s failed "
                        "(%s)", filename1, strerror (errno));
                goto out;
        }

        /* fopen() the volfile2 to create the graph */
        fp2 = fopen (filename2, "r");
        if (fp2 == NULL) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_FILE_OP_FAILED,
                        "fopen() on file: %s failed "
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

        gf_msg_debug (this->name, 0, "Returning with %d", ret);
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

        ret = sys_stat (filename1, &buf1);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_FILE_OP_FAILED,
                        "stat on file: %s failed "
                        "(%s)", filename1, strerror (errno));
                goto out;
        }

        ret = sys_stat (filename2, &buf2);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_FILE_OP_FAILED,
                        "stat on file: %s failed "
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
        gf_msg_debug (this->name, 0, "Returning with %d", ret);
        return ret;
}

int
glusterd_volset_help (dict_t *dict, char **op_errstr)
{
        int                     ret = -1;
        gf_boolean_t            xml_out = _gf_false;
#if (!HAVE_LIB_XML)
        xlator_t               *this = NULL;

        this = THIS;
#endif

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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MODULE_NOT_INSTALLED,
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

        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
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
        xlator_t           *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        op_ret = arg->op_ret;
        op_errstr = arg->op_errstr;

        ret = dict_get_str (dict, "cmd-str", &cmd);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Failed to get command "
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

        if (dict) {
                dict_unref (dict);
        }
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

        ret = dict_set_dynstr_with_alloc (dst, "gsync-status", stop_msg);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_WARNING, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Unable to set the stop"
                        "message in the ctx dictionary");
                goto out;
        }

        ret = 0;
 out:
        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
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
                gf_msg_debug ("glusterd", 0, "Source brick empty");
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
                        gf_msg ("glusterd", GF_LOG_ERROR, ENOMEM,
                                GD_MSG_NO_MEMORY, "Out Of Memory");
                        goto out;
                }

                memcpy (dst_sts_val, sts_val, sizeof(gf_gsync_status_t));

                memset (sts_val_name, '\0', sizeof(sts_val_name));
                snprintf (sts_val_name, sizeof(sts_val_name), "status_value%d", i + dst_count);

                ret = dict_set_bin (dst, sts_val_name, dst_sts_val, sizeof(gf_gsync_status_t));
                if (ret) {
                        GF_FREE (dst_sts_val);
                        goto out;
                }
        }

        ret = dict_set_int32 (dst, "gsync-count", dst_count+src_count);

 out:
        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
        return ret;

}

int32_t
glusterd_aggr_brick_mount_dirs (dict_t *aggr, dict_t *rsp_dict)
{
        char                   key[PATH_MAX]   = "";
        char                  *brick_mount_dir = NULL;
        int32_t                brick_count     = -1;
        int32_t                ret             = -1;
        int32_t                i               = -1;
        xlator_t              *this            = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (aggr);
        GF_ASSERT (rsp_dict);

        ret = dict_get_int32 (rsp_dict, "brick_count", &brick_count);
        if (ret) {
                gf_msg_debug (this->name, 0, "No brick_count present");
                ret = 0;
                goto out;
        }

        for (i = 1; i <= brick_count; i++) {
                brick_mount_dir = NULL;
                snprintf (key, sizeof(key), "brick%d.mount_dir", i);
                ret = dict_get_str (rsp_dict, key, &brick_mount_dir);
                if (ret) {
                        /* Coz the info will come from a different node */
                        gf_msg_debug (this->name, 0,
                                "%s not present", key);
                        continue;
                }

                ret = dict_set_dynstr_with_alloc (aggr, key,
                                                  brick_mount_dir);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "Failed to set %s", key);
                        goto out;
                }
        }

        ret = 0;
out:
        gf_msg_trace (this->name, 0, "Returning %d ", ret);
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
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_OPCTX_GET_FAIL,
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
                        ret = dict_set_dynstr_with_alloc (ctx, "conf_path",
                                                          conf_path);
                        if (ret) {
                                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_SET_FAILED,
                                        "Unable to store conf path.");
                                goto out;
                        }
                }
        }
        if ((op_errstr) && (strcmp ("", op_errstr))) {
                ret = dict_set_dynstr_with_alloc (ctx, "errstr",
                                                  op_errstr);
                if (ret)
                        goto out;
        }

        ret = 0;
 out:
        gf_msg_debug ("glusterd", 0, "Returning %d ", ret);
        return ret;
}

int32_t
glusterd_rb_use_rsp_dict (dict_t *aggr, dict_t *rsp_dict)
{
        int32_t  src_port = 0;
        int32_t  dst_port = 0;
        int      ret      = 0;
        dict_t  *ctx      = NULL;
        xlator_t *this    = NULL;

        this = THIS;
        GF_ASSERT (this);

        if (aggr) {
                ctx = aggr;

        } else {
                ctx = glusterd_op_get_ctx ();
                if (!ctx) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_OPCTX_GET_FAIL,
                                "Operation Context is not present");
                        GF_ASSERT (0);
                }
        }

        if (rsp_dict) {
                ret = dict_get_int32 (rsp_dict, "src-brick-port", &src_port);
                if (ret == 0) {
                        gf_msg_debug ("glusterd", 0,
                                "src-brick-port=%d found", src_port);
                }

                ret = dict_get_int32 (rsp_dict, "dst-brick-port", &dst_port);
                if (ret == 0) {
                        gf_msg_debug ("glusterd", 0,
                                "dst-brick-port=%d found", dst_port);
                }

                ret = glusterd_aggr_brick_mount_dirs (ctx, rsp_dict);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_BRICK_MOUNDIRS_AGGR_FAIL,
                                "Failed to "
                                "aggregate brick mount dirs");
                        goto out;
                }
        }

        if (src_port) {
                ret = dict_set_int32 (ctx, "src-brick-port",
                                      src_port);
                if (ret) {
                        gf_msg_debug ("glusterd", 0,
                                "Could not set src-brick");
                        goto out;
                }
        }

        if (dst_port) {
                ret = dict_set_int32 (ctx, "dst-brick-port",
                                      dst_port);
                if (ret) {
                        gf_msg_debug ("glusterd", 0,
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
        xlator_t *this    = NULL;

        GF_ASSERT (rsp_dict);
        this = THIS;
        GF_ASSERT (this);

        ret = dict_get_int32 (rsp_dict, "count", &brick_count);
        if (ret) {
                ret = 0; //no bricks in the rsp
                goto out;
        }
        if (aggr) {
                ctx_dict = aggr;

        } else {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_OPCTX_GET_FAIL,
                        "Operation Context is not present");
                ret = -1;
                goto out;
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
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Unable to set key: %s in dict",
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
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
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED, "Failed to copy tasks"
                                "to ctx_dict.");
                goto out;
        }

        if (local_count != remote_count) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_TASKS_COUNT_MISMATCH,
                        "Local tasks count (%d) and "
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
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED,
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
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED,
                                "Failed to get task status from rsp dict");
                        goto out;
                }
                snprintf (key, sizeof (key), "task%d.id", i);
                ret = dict_get_str (rsp_dict, key, &remote_task_id);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED,
                                "Failed to get task id from rsp dict");
                        goto out;
                }
                for (j = 0; j < local_count; j++) {
                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "task%d.id", j);
                        ret = dict_get_str (ctx_dict, key, &local_task_id);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_GET_FAILED,
                                        "Failed to get local task-id");
                                goto out;
                        }

                        if (strncmp (remote_task_id, local_task_id,
                                     strlen (remote_task_id))) {
                                /* Quit if a matching local task is not found */
                                if (j == (local_count - 1)) {
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_TASKS_COUNT_MISMATCH,
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
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_GET_FAILED,
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
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_TASK_STATUS_UPDATE_FAIL,
                                        "Failed to "
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
        int32_t                         hot_brick_count = -1;
        int32_t                         type = -1;
        int32_t                         rsp_node_count = 0;
        int32_t                         rsp_other_count = 0;
        int                             vol_count = -1;
        int                             i = 0;
        dict_t                          *ctx_dict = NULL;
        char                            key[PATH_MAX] = {0,};
        char                            *volname = NULL;
        glusterd_volinfo_t              *volinfo       = NULL;


        GF_ASSERT (rsp_dict);

        if (aggr) {
                ctx_dict = aggr;

        } else {
                ctx_dict = glusterd_op_get_ctx (GD_OP_STATUS_VOLUME);

        }

        ret = dict_get_int32 (ctx_dict, "cmd", &cmd);
        if (ret)
                goto out;

        if (cmd & GF_CLI_STATUS_ALL && is_origin_glusterd (ctx_dict)) {
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
                } else {
                        /* Ignore the error as still the aggregation applies in
                         * case its a task sub command */
                        ret = 0;
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
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
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
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Failed to update node count");
                goto out;
        }

        ret = dict_set_int32 (ctx_dict, "other-count",
                              (other_count + rsp_other_count));
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Failed to update other-count");
                goto out;
        }

        ret = dict_get_str (ctx_dict, "volname", &volname);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Failed to get volname");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLINFO_GET_FAIL,
                        "Failed to get volinfo for volume: %s",
                        volname);
                goto out;
        }


        if (volinfo->type == GF_CLUSTER_TYPE_TIER) {
                ret = dict_get_int32 (rsp_dict, "hot_brick_count",
                                      &hot_brick_count);
                if (ret) {
                        gf_msg (THIS->name, GF_LOG_ERROR, errno,
                                GD_MSG_DICT_GET_FAILED,
                                "Failed to get hot brick count from rsp_dict");
                        goto out;
                }


                ret = dict_get_int32 (rsp_dict, "type", &type);
                if (ret) {
                        gf_msg (THIS->name, GF_LOG_ERROR, errno,
                                GD_MSG_DICT_GET_FAILED,
                                "Failed to get type from rsp_dict");
                        goto out;
                }


        }

        ret = dict_set_int32 (ctx_dict, "hot_brick_count",
                              hot_brick_count);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_SET_FAILED,
                        "Failed to update hot_brick_count");
                goto out;
        }

        ret = dict_set_int32 (ctx_dict, "type", type);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_SET_FAILED,
                        "Failed to update type");
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
glusterd_max_opversion_use_rsp_dict (dict_t *dst, dict_t *src)
{
        int ret = -1;
        int src_max_opversion = -1;
        int max_opversion = -1;

        GF_VALIDATE_OR_GOTO (THIS->name, dst, out);
        GF_VALIDATE_OR_GOTO (THIS->name, src, out);

        ret = dict_get_int32 (dst, "max-opversion", &max_opversion);
        if (ret)
                gf_msg (THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                                "Maximum supported op-version not set in destination "
                                "dictionary");

        ret = dict_get_int32 (src, "max-opversion", &src_max_opversion);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "Failed to get maximum supported op-version from source");
                goto out;
        }

        if (max_opversion == -1 || src_max_opversion < max_opversion)
                max_opversion = src_max_opversion;

        ret = dict_set_int32 (dst, "max-opversion", max_opversion);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                                "Failed to set max op-version");
                goto out;
        }
out:
        return ret;
}


int
glusterd_volume_bitrot_scrub_use_rsp_dict (dict_t *aggr, dict_t *rsp_dict)
{
        int                      ret                = -1;
        int                      j                  = 0;
        uint64_t                 value              = 0;
        char                     key[256]           = {0,};
        char                    *last_scrub_time    = NULL;
        char                    *scrub_time         = NULL;
        char                    *volname            = NULL;
        char                    *node_uuid          = NULL;
        char                    *node_uuid_str      = NULL;
        char                    *bitd_log           = NULL;
        char                    *scrub_log          = NULL;
        char                    *scrub_freq         = NULL;
        char                    *scrub_state        = NULL;
        char                    *scrub_impact       = NULL;
        char                    *bad_gfid_str       = NULL;
        xlator_t                *this               = NULL;
        glusterd_conf_t         *priv               = NULL;
        glusterd_volinfo_t      *volinfo            = NULL;
        int                      src_count          = 0;
        int                      dst_count          = 0;
        int8_t                   scrub_running      = 0;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (aggr, "volname", &volname);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "Unable to get volume name");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
                        "Unable to find volinfo for volume: %s", volname);
                goto out;
        }

        ret = dict_get_int32 (aggr, "count", &dst_count);

        ret = dict_get_int32 (rsp_dict, "count", &src_count);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "failed to get count value");
                ret = 0;
                goto out;
        }

        ret = dict_set_int32 (aggr, "count", src_count+dst_count);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                        "Failed to set count in dictonary");

        snprintf (key, 256, "node-uuid-%d", src_count);
        ret = dict_get_str (rsp_dict, key, &node_uuid);
        if (!ret) {
                node_uuid_str = gf_strdup (node_uuid);
                memset (key, 0, 256);
                snprintf (key, 256, "node-uuid-%d", src_count+dst_count);
                ret = dict_set_dynstr (aggr, key, node_uuid_str);
                if (ret) {
                        gf_msg_debug (this->name, 0, "failed to set node-uuid");
                }
        }

        memset (key, 0, 256);
        snprintf (key, 256, "scrub-running-%d", src_count);
        ret = dict_get_int8 (rsp_dict, key, &scrub_running);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "scrub-running-%d", src_count+dst_count);
                ret = dict_set_int8 (aggr, key, scrub_running);
                if (ret) {
                        gf_msg_debug (this->name, 0, "Failed to set "
                                      "scrub-running value");
                }
        }

        memset (key, 0, 256);
        snprintf (key, 256, "scrubbed-files-%d", src_count);
        ret = dict_get_uint64 (rsp_dict, key, &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "scrubbed-files-%d", src_count+dst_count);
                ret = dict_set_uint64 (aggr, key, value);
                if (ret) {
                        gf_msg_debug (this->name, 0, "Failed to set "
                                      "scrubbed-file value");
                }
        }

        memset (key, 0, 256);
        snprintf (key, 256, "unsigned-files-%d", src_count);
        ret = dict_get_uint64 (rsp_dict, key, &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "unsigned-files-%d", src_count+dst_count);
                ret = dict_set_uint64 (aggr, key, value);
                if (ret) {
                        gf_msg_debug (this->name, 0, "Failed to set "
                                      "unsigned-file value");
                }
        }

        memset (key, 0, 256);
        snprintf (key, 256, "last-scrub-time-%d", src_count);
        ret = dict_get_str (rsp_dict, key, &last_scrub_time);
        if (!ret) {
                scrub_time = gf_strdup (last_scrub_time);
                memset (key, 0, 256);
                snprintf (key, 256, "last-scrub-time-%d", src_count+dst_count);
                ret = dict_set_dynstr (aggr, key, scrub_time);
                if (ret) {
                        gf_msg_debug (this->name, 0, "Failed to set "
                                      "last scrub time value");
                }
        }

        memset (key, 0, 256);
        snprintf (key, 256, "scrub-duration-%d", src_count);
        ret = dict_get_uint64 (rsp_dict, key, &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "scrub-duration-%d", src_count+dst_count);
                ret = dict_set_uint64 (aggr, key, value);
                if (ret) {
                        gf_msg_debug (this->name, 0, "Failed to set "
                                      "scrubbed-duration value");
                }
        }

        memset (key, 0, 256);
        snprintf (key, 256, "error-count-%d", src_count);
        ret = dict_get_uint64 (rsp_dict, key, &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "error-count-%d", src_count+dst_count);
                ret = dict_set_uint64 (aggr, key, value);
                if (ret) {
                        gf_msg_debug (this->name, 0, "Failed to set error "
                                      "count value");
                }

                /* Storing all the bad files in the dictionary */
                for (j = 0; j < value; j++) {
                        memset (key, 0, 256);
                        snprintf (key, 256, "quarantine-%d-%d", j, src_count);
                        ret = dict_get_str (rsp_dict, key, &bad_gfid_str);
                        if (!ret) {
                                memset (key, 0, 256);
                                snprintf (key, 256, "quarantine-%d-%d", j,
                                          src_count+dst_count);
                                ret = dict_set_dynstr_with_alloc (aggr, key,
                                                                  bad_gfid_str);
                                if (ret) {
                                        gf_msg_debug (this->name, 0, "Failed to"
                                                      "bad file gfid ");
                                }
                        }
                }
        }

        ret = dict_get_str (rsp_dict, "bitrot_log_file", &bitd_log);
        if (!ret) {
                ret = dict_set_dynstr_with_alloc (aggr, "bitrot_log_file",
                                                  bitd_log);
                if (ret) {
                        gf_msg_debug (this->name, 0, "Failed to set "
                                      "bitrot log file location");
                        goto out;
                }
        }

        ret = dict_get_str (rsp_dict, "scrub_log_file", &scrub_log);
        if (!ret) {
                ret = dict_set_dynstr_with_alloc (aggr, "scrub_log_file",
                                                  scrub_log);
                if (ret) {
                        gf_msg_debug (this->name, 0, "Failed to set "
                                      "scrubber log file location");
                        goto out;
                }
        }

        ret = dict_get_str (rsp_dict, "features.scrub-freq", &scrub_freq);
        if (!ret) {
                ret = dict_set_dynstr_with_alloc (aggr, "features.scrub-freq",
                                                  scrub_freq);
                if (ret) {
                        gf_msg_debug (this->name, 0, "Failed to set "
                                      "scrub-frequency value to dictionary");
                        goto out;
                }
        }

        ret = dict_get_str (rsp_dict, "features.scrub-throttle", &scrub_impact);
        if (!ret) {
                ret = dict_set_dynstr_with_alloc (aggr,
                                                  "features.scrub-throttle",
                                                  scrub_impact);
                if (ret) {
                        gf_msg_debug (this->name, 0, "Failed to set "
                                      "scrub-throttle value to dictionary");
                        goto out;
                }
        }

        ret = dict_get_str (rsp_dict, "features.scrub", &scrub_state);
        if (!ret) {
                ret = dict_set_dynstr_with_alloc (aggr, "features.scrub",
                                                  scrub_state);
                if (ret) {
                        gf_msg_debug (this->name, 0, "Failed to set "
                                      "scrub state value to dictionary");
                        goto out;
                }
        }

        ret = 0;
out:
        return ret;
}

int
glusterd_bitrot_volume_node_rsp (dict_t *aggr, dict_t *rsp_dict)
{
        int                      ret                = -1;
        uint64_t                 value              = 0;
        char                     key[256]           = {0,};
        char                     buf[1024]          = {0,};
        int32_t                  i                  = 0;
        int32_t                  j                  = 0;
        char                    *last_scrub_time    = NULL;
        char                    *scrub_time         = NULL;
        char                    *volname            = NULL;
        char                    *scrub_freq         = NULL;
        char                    *scrub_state        = NULL;
        char                    *scrub_impact       = NULL;
        char                    *bad_gfid_str       = NULL;
        xlator_t                *this               = NULL;
        glusterd_conf_t         *priv               = NULL;
        glusterd_volinfo_t      *volinfo            = NULL;
        int8_t                   scrub_running      = 0;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_set_str (aggr, "bitrot_log_file",
                           (priv->bitd_svc.proc.logfile));
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                        "Failed to set bitrot log file location");
                goto out;
        }

        ret = dict_set_str (aggr, "scrub_log_file",
                           (priv->scrub_svc.proc.logfile));
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                        "Failed to set scrubber log file location");
                goto out;
        }

        ret = dict_get_str (aggr, "volname", &volname);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "Unable to get volume name");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
                        "Unable to find volinfo for volume: %s", volname);
                goto out;
        }

        ret = dict_get_int32 (aggr, "count", &i);
        i++;

        ret = dict_set_int32 (aggr, "count", i);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                        "Failed to set count");

        snprintf (buf, 1024, "%s", uuid_utoa (MY_UUID));

        snprintf (key, 256, "node-uuid-%d", i);
        ret = dict_set_dynstr_with_alloc (aggr, key, buf);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                        "failed to set node-uuid");

        ret = dict_get_str (volinfo->dict, "features.scrub-freq", &scrub_freq);
        if (!ret) {
                ret = dict_set_str (aggr, "features.scrub-freq", scrub_freq);
                if (ret) {
                        gf_msg_debug (this->name, 0, "Failed to set "
                                      "scrub-frequency value to dictionary");
                }
        } else {
                /* By Default scrub-frequency is bi-weekly. So when user
                 * enable bitrot then scrub-frequency value will not be
                 * present in volinfo->dict. Setting by-default value of
                 * scrub-frequency explicitly for presenting it to scrub
                 * status.
                 */
                 ret = dict_set_dynstr_with_alloc (aggr, "features.scrub-freq",
                                                   "biweekly");
                 if (ret) {
                         gf_msg_debug (this->name, 0, "Failed to set "
                                       "scrub-frequency value to dictionary");
                 }
        }

        ret = dict_get_str (volinfo->dict, "features.scrub-throttle",
                            &scrub_impact);
        if (!ret) {
                ret = dict_set_str (aggr, "features.scrub-throttle",
                                    scrub_impact);
                if (ret) {
                        gf_msg_debug (this->name, 0, "Failed to set "
                                      "scrub-throttle value to dictionary");
                }
        } else {
                /* By Default scrub-throttle is lazy. So when user
                 * enable bitrot then scrub-throttle value will not be
                 * present in volinfo->dict. Setting by-default value of
                 * scrub-throttle explicitly for presenting it to
                 * scrub status.
                 */
                 ret = dict_set_dynstr_with_alloc (aggr,
                                                   "features.scrub-throttle",
                                                   "lazy");
                 if (ret) {
                         gf_msg_debug (this->name, 0, "Failed to set "
                                       "scrub-throttle value to dictionary");
                 }
        }

        ret = dict_get_str (volinfo->dict, "features.scrub", &scrub_state);
        if (!ret) {
                ret = dict_set_str (aggr, "features.scrub", scrub_state);
                if (ret) {
                        gf_msg_debug (this->name, 0, "Failed to set "
                                      "scrub state value to dictionary");
                }
        }

        ret = dict_get_int8 (rsp_dict, "scrub-running", &scrub_running);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "scrub-running-%d", i);
                ret = dict_set_uint64 (aggr, key, scrub_running);
                if (ret) {
                        gf_msg_debug (this->name, 0, "Failed to set "
                                      "scrub-running value");
                }
        }

        ret = dict_get_uint64 (rsp_dict, "scrubbed-files", &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "scrubbed-files-%d", i);
                ret = dict_set_uint64 (aggr, key, value);
                if (ret) {
                        gf_msg_debug (this->name, 0, "Failed to set "
                                      "scrubbed-file value");
                }
        }

        ret = dict_get_uint64 (rsp_dict, "unsigned-files", &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "unsigned-files-%d", i);
                ret = dict_set_uint64 (aggr, key, value);
                if (ret) {
                        gf_msg_debug (this->name, 0, "Failed to set "
                                      "unsigned-file value");
                }
        }

        ret = dict_get_str (rsp_dict, "last-scrub-time", &last_scrub_time);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "last-scrub-time-%d", i);

                scrub_time = gf_strdup (last_scrub_time);
                ret = dict_set_dynstr (aggr, key, scrub_time);
                if (ret) {
                        gf_msg_debug (this->name, 0, "Failed to set "
                                      "last scrub time value");
                }
        }

        ret = dict_get_uint64 (rsp_dict, "scrub-duration", &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "scrub-duration-%d", i);
                ret = dict_set_uint64 (aggr, key, value);
                if (ret) {
                        gf_msg_debug (this->name, 0, "Failed to set "
                                      "scrubbed-duration value");
                }
        }

        ret = dict_get_uint64 (rsp_dict, "total-count", &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "error-count-%d", i);
                ret = dict_set_uint64 (aggr, key, value);
                if (ret) {
                        gf_msg_debug (this->name, 0, "Failed to set error "
                                      "count value");
                }

                /* Storing all the bad files in the dictionary */
                for (j = 0; j < value; j++) {
                        memset (key, 0, 256);
                        snprintf (key, 256, "quarantine-%d", j);
                        ret = dict_get_str (rsp_dict, key, &bad_gfid_str);
                        if (!ret) {
                                memset (key, 0, 256);
                                snprintf (key, 256, "quarantine-%d-%d", j, i);
                                ret = dict_set_dynstr_with_alloc (aggr, key,
                                                                  bad_gfid_str);
                                if (ret) {
                                        gf_msg_debug (this->name, 0, "Failed to"
                                                      "bad file gfid ");
                                }
                        }
                }
        }

        ret = 0;
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
        glusterd_peerinfo_t *peerinfo      = NULL;
        glusterd_volinfo_t  *volinfo       = NULL;
        int                  ret           = 0;
        int32_t              index         = 0;
        int32_t              count         = 0;
        int32_t              current_index = 2;
        int32_t              value32       = 0;
        uint64_t             value         = 0;
        char                *peer_uuid_str = NULL;
        xlator_t            *this           = NULL;

        GF_ASSERT (rsp_dict);
        this = THIS;
        GF_ASSERT (this);
        conf = this->private;

        if (aggr) {
                ctx_dict = aggr;

        } else {
                 gf_msg (this->name, GF_LOG_ERROR, 0,
                         GD_MSG_OPCTX_GET_FAIL,
                         "Operation Context is not present");
                goto out;
        }

        if (!ctx_dict)
                goto out;

        ret = dict_get_str (ctx_dict, "volname", &volname);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Unable to get volume name");
                goto out;
        }

        ret  = glusterd_volinfo_find (volname, &volinfo);

        if (ret)
                goto out;

        ret = dict_get_int32 (rsp_dict, "count", &index);
        if (ret)
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "failed to get index");

        memset (key, 0, 256);
        snprintf (key, 256, "node-uuid-%d", index);
        ret = dict_get_str (rsp_dict, key, &node_uuid);
        if (!ret) {
                node_uuid_str = gf_strdup (node_uuid);

                /* Finding the index of the node-uuid in the peer-list */
                rcu_read_lock ();
                cds_list_for_each_entry_rcu (peerinfo, &conf->peers,
                                             uuid_list) {
                        peer_uuid_str = gd_peer_uuid_str (peerinfo);
                        if (strcmp (peer_uuid_str, node_uuid_str) == 0)
                                break;

                        current_index++;
                }
                rcu_read_unlock ();

                /* Setting the largest index value as the total count. */
                ret = dict_get_int32 (ctx_dict, "count", &count);
                if (count < current_index) {
                        ret = dict_set_int32 (ctx_dict, "count", current_index);
                        if (ret)
                                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                            GD_MSG_DICT_SET_FAILED,
                                            "Failed to set count");
                }

                /* Setting the same index for the node, as is in the peerlist.*/
                memset (key, 0, 256);
                snprintf (key, 256, "node-uuid-%d", current_index);
                ret = dict_set_dynstr (ctx_dict, key, node_uuid_str);
                if (ret) {
                        gf_msg_debug (THIS->name, 0,
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
                        gf_msg_debug (THIS->name, 0,
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
                        gf_msg_debug (THIS->name, 0,
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
                        gf_msg_debug (THIS->name, 0,
                                "failed to set looked up file count");
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
                        gf_msg_debug (THIS->name, 0,
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
                        gf_msg_debug (THIS->name, 0,
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
                        gf_msg_debug (THIS->name, 0,
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
                        gf_msg_debug (THIS->name, 0,
                                "failed to set run-time");
                }
        }

        memset (key, 0, 256);
        snprintf (key, 256, "time-left-%d", index);
        ret = dict_get_uint64 (rsp_dict, key, &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "time-left-%d", current_index);
                ret = dict_set_uint64 (ctx_dict, key, value);
                if (ret) {
                        gf_msg_debug (THIS->name, 0,
                                "failed to set time-left");
                }
        }
        memset (key, 0, 256);
        snprintf (key, 256, "demoted-%d", index);
        ret = dict_get_uint64 (rsp_dict, key, &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "demoted-%d", current_index);
                ret = dict_set_uint64 (ctx_dict, key, value);
                if (ret) {
                        gf_msg_debug (THIS->name, 0,
                                "failed to set demoted count");
                }
        }
        memset (key, 0, 256);
        snprintf (key, 256, "promoted-%d", index);
        ret = dict_get_uint64 (rsp_dict, key, &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "promoted-%d", current_index);
                ret = dict_set_uint64 (ctx_dict, key, value);
                if (ret) {
                        gf_msg_debug (THIS->name, 0,
                                "failed to set promoted count");
                }
        }

        ret = 0;

out:
        return ret;
}

int
glusterd_volume_tier_use_rsp_dict (dict_t *aggr, dict_t *rsp_dict)
{
        char                 key[256]      = {0,};
        char                *node_uuid     = NULL;
        char                *node_uuid_str = NULL;
        char                *volname       = NULL;
        dict_t              *ctx_dict      = NULL;
        double               elapsed_time  = 0;
        glusterd_volinfo_t  *volinfo       = NULL;
        int                  ret           = 0;
        int32_t              index         = 0;
        int32_t              count         = 0;
        int32_t              value32       = 0;
        uint64_t             value         = 0;
        xlator_t            *this           = NULL;
        char                *task_id_str   = NULL;

        this = THIS;
        GF_VALIDATE_OR_GOTO (this->name, this, out);
        GF_VALIDATE_OR_GOTO (this->name, rsp_dict, out);

        if (aggr) {
                ctx_dict = aggr;

        } else {
                 gf_msg (this->name, GF_LOG_ERROR, 0,
                         GD_MSG_OPCTX_GET_FAIL,
                         "Operation Context is not present");
                goto out;
        }

        if (!ctx_dict)
                goto out;

        ret = dict_get_str (ctx_dict, "volname", &volname);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Unable to get volume name");
                goto out;
        }

        ret  = glusterd_volinfo_find (volname, &volinfo);

        if (ret)
                goto out;

        ret = dict_get_int32 (rsp_dict, "count", &index);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "failed to get index");

        memset (key, 0, 256);
        snprintf (key, 256, "node-uuid-%d", index);
        ret = dict_get_str (rsp_dict, key, &node_uuid);
        if (!ret) {
                node_uuid_str = gf_strdup (node_uuid);

        }
        ret = dict_get_int32 (ctx_dict, "count", &count);
        count++;
        ret = dict_set_int32 (ctx_dict, "count", count);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "Failed to set count");

        memset (key, 0, 256);
        snprintf (key, 256, "node-uuid-%d", count);
        ret = dict_set_dynstr (ctx_dict, key, node_uuid_str);
        if (ret) {
                gf_msg_debug (this->name, 0,
                                "failed to set node-uuid");
        }

        snprintf (key, 256, "files-%d", index);
        ret = dict_get_uint64 (rsp_dict, key, &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "files-%d", count);
                ret = dict_set_uint64 (ctx_dict, key, value);
                if (ret) {
                        gf_msg_debug (this->name, 0,
                                "failed to set the file count");
                }
        }

        memset (key, 0, 256);
        snprintf (key, 256, "size-%d", index);
        ret = dict_get_uint64 (rsp_dict, key, &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "size-%d", count);
                ret = dict_set_uint64 (ctx_dict, key, value);
                if (ret) {
                        gf_msg_debug (this->name, 0,
                                "failed to set the size of migration");
                }
        }

        memset (key, 0, 256);
        snprintf (key, 256, "lookups-%d", index);
        ret = dict_get_uint64 (rsp_dict, key, &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "lookups-%d", count);
                ret = dict_set_uint64 (ctx_dict, key, value);
                if (ret) {
                        gf_msg_debug (this->name, 0,
                                "failed to set looked up file count");
                }
        }

        memset (key, 0, 256);
        snprintf (key, 256, "status-%d", index);
        ret = dict_get_int32 (rsp_dict, key, &value32);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "status-%d", count);
                ret = dict_set_int32 (ctx_dict, key, value32);
                if (ret) {
                        gf_msg_debug (this->name, 0,
                                "failed to set status");
                }
        }

        memset (key, 0, 256);
        snprintf (key, 256, "failures-%d", index);
        ret = dict_get_uint64 (rsp_dict, key, &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "failures-%d", count);
                ret = dict_set_uint64 (ctx_dict, key, value);
                if (ret) {
                        gf_msg_debug (this->name, 0,
                                "failed to set failure count");
                }
        }

        memset (key, 0, 256);
        snprintf (key, 256, "skipped-%d", index);
        ret = dict_get_uint64 (rsp_dict, key, &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "skipped-%d", count);
                ret = dict_set_uint64 (ctx_dict, key, value);
                if (ret) {
                        gf_msg_debug (this->name, 0,
                                "failed to set skipped count");
                }
        }
        memset (key, 0, 256);
        snprintf (key, 256, "run-time-%d", index);
        ret = dict_get_double (rsp_dict, key, &elapsed_time);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "run-time-%d", count);
                ret = dict_set_double (ctx_dict, key, elapsed_time);
                if (ret) {
                        gf_msg_debug (this->name, 0,
                                "failed to set run-time");
                }
        }

        memset (key, 0, 256);
        snprintf (key, 256, "demoted-%d", index);
        ret = dict_get_uint64 (rsp_dict, key, &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "demoted-%d", count);
                ret = dict_set_uint64 (ctx_dict, key, value);
                if (ret) {
                        gf_msg_debug (this->name, 0,
                                "failed to set demoted count");
                }
        }
        memset (key, 0, 256);
        snprintf (key, 256, "promoted-%d", index);
        ret = dict_get_uint64 (rsp_dict, key, &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "promoted-%d", count);
                ret = dict_set_uint64 (ctx_dict, key, value);
                if (ret) {
                        gf_msg_debug (this->name, 0,
                                "failed to set promoted count");
                }
        }

        memset (key, 0, 256);
        snprintf (key, 256, "time-left-%d", index);
        ret = dict_get_uint64 (rsp_dict, key, &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "time-left-%d", count);
                ret = dict_set_uint64 (ctx_dict, key, value);
                if (ret) {
                        gf_msg_debug (THIS->name, 0,
                                "failed to set time-left");
                }
        }

        ret = dict_get_str (rsp_dict, GF_REMOVE_BRICK_TID_KEY,
                                &task_id_str);
        if (ret) {
                gf_msg_debug (this->name, errno,
                                "Missing remove-brick-id");
        } else
                ret = dict_set_str (ctx_dict, GF_REMOVE_BRICK_TID_KEY,
                                task_id_str);

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
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_EMPTY,
                        "Source or Destination "
                        "dict is empty.");
                goto out;
        }

        ret = dict_get_int32 (dst, "output_count", &dst_output_count);

        ret = dict_get_int32 (src, "output_count", &src_output_count);
        if (ret) {
                gf_msg_debug ("glusterd", 0, "No output from source");
                ret = 0;
                goto out;
        }

        for (i = 1; i <= src_output_count; i++) {
                len = snprintf (output_name, sizeof(output_name) - 1,
                                "output_%d", i);
                output_name[len] = '\0';
                ret = dict_get_str (src, output_name, &output);
                if (ret) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED,
                                "Unable to fetch %s",
                                output_name);
                        goto out;
                }

                len = snprintf (output_name, sizeof(output_name) - 1,
                                "output_%d", i+dst_output_count);
                output_name[len] = '\0';
                ret = dict_set_dynstr (dst, output_name, gf_strdup (output));
                if (ret) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "Unable to set %s",
                                output_name);
                        goto out;
                }
        }

        ret = dict_set_int32 (dst, "output_count",
                              dst_output_count+src_output_count);
out:
        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
        return ret;
}

int
glusterd_use_rsp_dict (dict_t *aggr, dict_t *rsp_dict)
{
        int            ret      = 0;

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
        int                 ret         = 0;
        dict_t             *ctx_dict    = NULL;
        uuid_t             *txn_id      = NULL;
        glusterd_op_info_t  txn_op_info = {{0},};
        glusterd_op_t       op          = GD_OP_NONE;

        GF_ASSERT (rsp_dict);

        ret = dict_get_bin (aggr, "transaction_id", (void **)&txn_id);
        if (ret)
                goto out;
        gf_msg_debug (THIS->name, 0, "transaction ID = %s",
                uuid_utoa (*txn_id));

        ret = glusterd_get_txn_opinfo (txn_id, &txn_op_info);
        if (ret) {
                gf_msg_callingfn (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_TRANS_OPINFO_GET_FAIL,
                        "Unable to get transaction opinfo "
                        "for transaction ID : %s",
                        uuid_utoa (*txn_id));
                goto out;
        }

        op = txn_op_info.op;
        GF_ASSERT (GD_OP_HEAL_VOLUME == op);

        if (aggr) {
                ctx_dict = aggr;

        } else {
                ctx_dict = txn_op_info.op_ctx;
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Failed to get quota opcode");
                goto out;
        }

        if ((type != GF_QUOTA_OPTION_TYPE_LIMIT_USAGE) &&
            (type != GF_QUOTA_OPTION_TYPE_LIMIT_OBJECTS) &&
            (type != GF_QUOTA_OPTION_TYPE_REMOVE) &&
            (type != GF_QUOTA_OPTION_TYPE_REMOVE_OBJECTS)) {
                dict_copy (rsp_dict, dict);
                ret = 0;
                goto out;
        }

        ret = dict_get_int32 (rsp_dict, "count", &rsp_dict_count);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Failed to get the count of "
                        "gfids from the rsp dict");
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &count);
        if (ret)
                /* The key "count" is absent in op_ctx when this function is
                 * called after self-staging on the originator. This must not
                 * be treated as error.
                 */
                gf_msg_debug (this->name, 0, "Failed to get count of gfids"
                        " from req dict. This could be because count is not yet"
                        " copied from rsp_dict into op_ctx");

        for (i = 0; i < rsp_dict_count; i++) {
                snprintf (key, sizeof(key)-1, "gfid%d", i);

                ret = dict_get_str (rsp_dict, key, &uuid_str);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED,
                                "Failed to get gfid "
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
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "Failed to set gfid "
                                "from rsp dict into req dict");
                        GF_FREE (uuid_str_dup);
                        goto out;
                }
        }

        ret = dict_set_int32 (dict, "count", rsp_dict_count + count);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Failed to set aggregated "
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
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Unable to get volume name");
                goto out;
        }

        ret = dict_get_int32 (req_dict, "heal-op", &heal_op);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Unable to get heal_op");
                goto out;
        }


        ret  = glusterd_volinfo_find (volname, &volinfo);

        if (ret)
                goto out;

        rsp_ctx.dict = op_ctx;
        rsp_ctx.volinfo = volinfo;
        rsp_ctx.this = THIS;
        if (heal_op == GF_SHD_OP_STATISTICS)
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
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Couldn't get node index");
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
glusterd_tier_or_rebalance_rsp (dict_t *op_ctx, glusterd_rebalance_t *index, int32_t i)
{
        int                             ret = 0;
        char                            key[256] = {0,};

        memset (key, 0 , 256);
        snprintf (key, 256, "files-%d", i);
        ret = dict_set_uint64 (op_ctx, key, index->rebalance_files);
        if (ret)
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "failed to set file count");

        memset (key, 0 , 256);
        snprintf (key, 256, "size-%d", i);
        ret = dict_set_uint64 (op_ctx, key, index->rebalance_data);
        if (ret)
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "failed to set size of xfer");

        memset (key, 0 , 256);
        snprintf (key, 256, "lookups-%d", i);
        ret = dict_set_uint64 (op_ctx, key, index->lookedup_files);
        if (ret)
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "failed to set lookedup file count");

        memset (key, 0 , 256);
        snprintf (key, 256, "status-%d", i);
        ret = dict_set_int32 (op_ctx, key, index->defrag_status);
        if (ret)
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "failed to set status");

        memset (key, 0 , 256);
        snprintf (key, 256, "failures-%d", i);
        ret = dict_set_uint64 (op_ctx, key, index->rebalance_failures);
        if (ret)
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "failed to set failure count");

        memset (key, 0 , 256);
        snprintf (key, 256, "skipped-%d", i);
        ret = dict_set_uint64 (op_ctx, key, index->skipped_files);
        if (ret)
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "failed to set skipped count");

        memset (key, 0, 256);
        snprintf (key, 256, "run-time-%d", i);
        ret = dict_set_double (op_ctx, key, index->rebalance_time);
        if (ret)
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "failed to set run-time");

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
        int32_t                         cmd       = 0;

        GF_ASSERT (req_dict);

        ret = dict_get_str (req_dict, "volname", &volname);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Unable to get volume name");
                goto out;
        }

        ret  = glusterd_volinfo_find (volname, &volinfo);

        ret = dict_get_int32 (req_dict, "rebalance-command", &cmd);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED, "Unable to get the cmd");
                goto out;
        }

        if (ret)
                goto out;

        if (rsp_dict) {
                ret = glusterd_defrag_volume_status_update (volinfo,
                                                            rsp_dict,
                                                            cmd);
        }

        if (!op_ctx) {
                dict_copy (rsp_dict, op_ctx);
                goto out;
        }

        ret = dict_get_int32 (op_ctx, "count", &i);
        i++;

        ret = dict_set_int32 (op_ctx, "count", i);
        if (ret)
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Failed to set count");

        snprintf (buf, 1024, "%s", uuid_utoa (MY_UUID));
        node_str = gf_strdup (buf);

        snprintf (key, 256, "node-uuid-%d",i);
        ret = dict_set_dynstr (op_ctx, key, node_str);
        if (ret)
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "failed to set node-uuid");

        if (cmd == GF_DEFRAG_CMD_STATUS_TIER)
                glusterd_tier_or_rebalance_rsp (op_ctx, &volinfo->tier, i);
        else
                glusterd_tier_or_rebalance_rsp (op_ctx, &volinfo->rebal, i);

        memset (key, 0 , 256);
        snprintf (key, 256, "time-left-%d", i);
        ret = dict_set_uint64 (op_ctx, key, volinfo->rebal.time_left);
        if (ret)
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_SET_FAILED,
                        "failed to set time left");

        memset (key, 0 , 256);
        snprintf (key, 256, "promoted-%d", i);
        ret = dict_set_uint64 (op_ctx, key, volinfo->tier_info.promoted);
        if (ret)
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_SET_FAILED,
                        "failed to set lookedup file count");

        memset (key, 0 , 256);
        snprintf (key, 256, "demoted-%d", i);
        ret = dict_set_uint64 (op_ctx, key, volinfo->tier_info.demoted);
        if (ret)
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_SET_FAILED,
                        "failed to set lookedup file count");

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
        case GD_OP_TIER_STATUS:
        case GD_OP_DETACH_TIER_STATUS:
        case GD_OP_DEFRAG_BRICK_VOLUME:
                glusterd_defrag_volume_node_rsp (req_dict,
                                                 rsp_dict, op_ctx);
                break;

        case GD_OP_HEAL_VOLUME:
                ret = glusterd_heal_volume_brick_rsp (req_dict, rsp_dict,
                                                      op_ctx, op_errstr);
                break;
        case GD_OP_SCRUB_STATUS:
                ret = glusterd_bitrot_volume_node_rsp (op_ctx, rsp_dict);

                break;
        default:
                break;
        }

        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_set_originator_uuid (dict_t *dict)
{
        int          ret              = -1;
        uuid_t      *originator_uuid  = NULL;

        GF_ASSERT (dict);

        originator_uuid = GF_CALLOC (1, sizeof(uuid_t),
                                     gf_common_mt_uuid_t);
        if (!originator_uuid) {
                ret = -1;
                goto out;
        }

        gf_uuid_copy (*originator_uuid, MY_UUID);
        ret = dict_set_bin (dict, "originator_uuid",
                            originator_uuid, sizeof (uuid_t));
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Failed to set originator_uuid.");
                goto out;
        }

out:
        if (ret && originator_uuid)
                GF_FREE (originator_uuid);

        return ret;
}

/* Should be used only when an operation is in progress, as that is the only
 * time a lock_owner is set
 */
gf_boolean_t
is_origin_glusterd (dict_t *dict)
{
        gf_boolean_t  ret              = _gf_false;
        uuid_t        lock_owner       = {0,};
        uuid_t        *originator_uuid = NULL;

        GF_ASSERT (dict);

        ret = dict_get_bin (dict, "originator_uuid",
                            (void **) &originator_uuid);
        if (ret) {
                /* If not originator_uuid has been set, then the command
                 * has been originated from a glusterd running on older version
                 * Hence fetching the lock owner */
                ret = glusterd_get_lock_owner (&lock_owner);
                if (ret) {
                        ret = _gf_false;
                        goto out;
                }
                ret = !gf_uuid_compare (MY_UUID, lock_owner);
        } else
                ret = !gf_uuid_compare (MY_UUID, *originator_uuid);

out:
        return ret;
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

        gf_uuid_generate (task_id);
        uuid_str = gf_strdup (uuid_utoa (task_id));
        if (!uuid_str) {
                ret = -1;
                goto out;
        }

        ret = dict_set_dynstr (dict, key, uuid_str);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED, "Failed to set %s in dict",
                        key);
                goto out;
        }
        gf_msg (this->name, GF_LOG_INFO, 0,
                GD_MSG_TASK_ID_INFO, "Generated task-id %s for key %s",
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

        gf_uuid_unparse (uuid, tmp_str);
        task_id_str = gf_strdup (tmp_str);
        if (!task_id_str)
                return -1;

        ret = dict_set_dynstr (dict, key, task_id_str);
        if (ret) {
                GF_FREE (task_id_str);
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
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

        if (volinfo->type == GF_CLUSTER_TYPE_DISPERSE) {
                if (volinfo->op_version < GD_OP_VERSION_3_6_0)
                        volinfo->op_version = GD_OP_VERSION_3_6_0;
                if (volinfo->client_op_version < GD_OP_VERSION_3_6_0)
                        volinfo->client_op_version = GD_OP_VERSION_3_6_0;
        }

        return;
}

int
op_version_check (xlator_t *this, int min_op_version, char *msg, int msglen)
{
       int              ret  = 0;
       glusterd_conf_t *priv = NULL;

       GF_ASSERT (this);
       GF_ASSERT (msg);

       priv = this->private;
       if (priv->op_version < min_op_version) {
                snprintf (msg, msglen, "One or more nodes do not support "
                          "the required op-version. Cluster op-version must "
                          "atleast be %d.", min_op_version);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_UNSUPPORTED_VERSION, "%s", msg);
                ret = -1;
      }
      return ret;
}


/* A task is committed/completed once the task-id for it is cleared */
gf_boolean_t
gd_is_remove_brick_committed (glusterd_volinfo_t *volinfo)
{
        GF_ASSERT (volinfo);

        if ((GD_OP_REMOVE_BRICK == volinfo->rebal.op) &&
            !gf_uuid_is_null (volinfo->rebal.rebalance_id))
                        return _gf_false;

        return _gf_true;
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
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Failed to get opcode");
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
                cds_list_for_each_entry (brick, &volinfo->bricks, brick_list) {
                        if (gf_uuid_compare (MY_UUID, brick->uuid) == 0) {
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
                                                                      &brick,
                                                                      _gf_false);
                        if (ret)
                                goto out;
                        if (gf_uuid_compare (MY_UUID, brick->uuid) == 0) {
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
glusterd_is_volume_inode_quota_enabled (glusterd_volinfo_t *volinfo)
{
        return (glusterd_volinfo_get_boolean (volinfo,
                                              VKEY_FEATURES_INODE_QUOTA));
}

int
glusterd_is_tierd_enabled (glusterd_volinfo_t *volinfo)
{
        return volinfo->is_tier_enabled;
}

int
glusterd_is_bitrot_enabled (glusterd_volinfo_t *volinfo)
{
        return glusterd_volinfo_get_boolean (volinfo, VKEY_FEATURES_BITROT);
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Failed to get quota opcode");
                goto out;
        }

        if ((op_code != GF_QUOTA_OPTION_TYPE_LIMIT_USAGE) &&
            (op_code != GF_QUOTA_OPTION_TYPE_LIMIT_OBJECTS) &&
            (op_code != GF_QUOTA_OPTION_TYPE_REMOVE) &&
            (op_code != GF_QUOTA_OPTION_TYPE_REMOVE_OBJECTS)) {
                ret = 0;
                goto out;
        }

        ret = dict_get_str (op_ctx, "path", &path);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Failed to get path");
                goto out;
        }

        ret = dict_get_int32 (op_ctx, "count", &count);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Failed to get count");
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
                ret = -ENOENT;
                goto out;
        }

        snprintf (key, sizeof (key) - 1, "gfid%d", 0);

        ret = dict_get_str (op_ctx, key, &uuid1_str);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Failed to get key '%s'",
                        key);
                goto out;
        }

        gf_uuid_parse (uuid1_str, uuid1);

        for (i = 1; i < count; i++) {
                snprintf (key, sizeof (key)-1, "gfid%d", i);

                ret = dict_get_str (op_ctx, key, &uuid2_str);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED,
                                "Failed to get key "
                                "'%s'", key);
                        goto out;
                }

                gf_uuid_parse (uuid2_str, uuid2);

                if (gf_uuid_compare (uuid1, uuid2)) {
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
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "Failed to set gfid");
                        GF_FREE (uuid1_str_dup);
                        goto out;
                }
        } else {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_ITER_FAIL,
                        "Failed to iterate through %d"
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

        sys_unlink (quota_confpath);
        sys_unlink (cksum_path);

        gf_store_handle_destroy (volinfo->quota_conf_shandle);
        volinfo->quota_conf_shandle = NULL;
        volinfo->quota_conf_version = 0;

}

int
glusterd_remove_auxiliary_mount (char *volname)
{
        int       ret                = -1;
        char      mountdir[PATH_MAX] = {0,};
        char      pidfile[PATH_MAX]  = {0,};
        xlator_t *this               = NULL;

        this = THIS;
        GF_ASSERT (this);

        GLUSTERFS_GET_AUX_MOUNT_PIDFILE (pidfile, volname);

        if (!gf_is_service_running (pidfile, NULL)) {
                gf_msg_debug (this->name, 0, "Aux mount of volume %s "
                        "absent, hence returning", volname);
                return 0;
        }

        GLUSTERD_GET_QUOTA_AUX_MOUNT_PATH (mountdir, volname, "/");
        ret = gf_umount_lazy (this->name, mountdir, 1);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_LAZY_UMOUNT_FAIL, "umount on %s failed, "
                        "reason : %s", mountdir, strerror (errno));

                /* Hide EBADF as it means the mount is already gone */
                if (errno == EBADF)
                       ret = 0;
        }

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
        (void) rpc_clnt_reconnect_cleanup (&rpc->conn);
        ret = rpc_clnt_unref (rpc);
        synclock_lock (&conf->big_lock);

        return ret;
}

int32_t
glusterd_compare_volume_name(struct cds_list_head *list1,
                             struct cds_list_head *list2)
{
        glusterd_volinfo_t *volinfo1 = NULL;
        glusterd_volinfo_t *volinfo2 = NULL;

        volinfo1 = cds_list_entry (list1, glusterd_volinfo_t, vol_list);
        volinfo2 = cds_list_entry (list2, glusterd_volinfo_t, vol_list);
        return strcmp(volinfo1->volname, volinfo2->volname);
}

static int
gd_default_synctask_cbk (int ret, call_frame_t *frame, void *opaque)
{
        glusterd_conf_t     *priv = THIS->private;
        synclock_unlock (&priv->big_lock);
        return ret;
}

void
glusterd_launch_synctask (synctask_fn_t fn, void *opaque)
{
        xlator_t        *this = NULL;
        int             ret   = -1;

        this = THIS;

        /* synclock_lock must be called from within synctask, @fn must call it before
         * it starts with its work*/
        ret = synctask_new (this->ctx->env, fn, gd_default_synctask_cbk, NULL,
                            opaque);
        if (ret)
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        GD_MSG_SPAWN_SVCS_FAIL, "Failed to spawn bricks"
                        " and other volume related services");
}

/*
 * glusterd_enable_default_options enable certain options by default on the
 * given volume based on the cluster op-version. This is called only during
 * volume create or during volume reset
 *
 * @volinfo - volume on which to enable the default options
 * @option  - option to be set to default. If NULL, all possible options will be
 *            set to default
 *
 * Returns 0 on sucess and -1 on failure. If @option is given, but doesn't match
 * any of the options that could be set, it is a success.
 */
/*
 * TODO: Make this able to parse the volume-set table to set options
 * Currently, the check and set for any option which wants to make use of this
 * 'framework' needs to be done here manually. This would mean more work for the
 * developer. This little extra work can be avoided if we make it possible to
 * parse the volume-set table to get the options which could be set and their
 * default values
 */
int
glusterd_enable_default_options (glusterd_volinfo_t *volinfo, char *option)
{
        int ret = 0;
        xlator_t *this = NULL;
        glusterd_conf_t *conf = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_VALIDATE_OR_GOTO (this->name, volinfo, out);

        conf = this->private;
        GF_ASSERT (conf);

#ifdef GD_OP_VERSION_3_8_0
        if (conf->op_version >= GD_OP_VERSION_3_8_0) {
                /* nfs.disable needs to be enabled for new volumes with
                 * >= gluster version 3.7 (for now) 3.8 later
                 */
                if (!option || !strcmp (NFS_DISABLE_MAP_KEY, option)) {
                        ret = dict_set_dynstr_with_alloc (volinfo->dict,
                                        NFS_DISABLE_MAP_KEY, "on");
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        GD_MSG_DICT_SET_FAILED,
                                        "Failed to set option '"
                                        NFS_DISABLE_MAP_KEY "' on volume "
                                        "%s", volinfo->volname);
                                goto out;
                        }
                }
        }
#endif

        if (conf->op_version >= GD_OP_VERSION_3_7_0) {
                /* Set needed volume options in volinfo->dict
                 * For ex.,
                 *
                 * if (!option || !strcmp("someoption", option) {
                 *      ret = dict_set_str(volinfo->dict, "someoption", "on");
                 *      ...
                 * }
                 * */

                /* Option 'features.quota-deem-statfs' should not be turned off
                 * with 'gluster volume reset <VOLNAME>', since quota features
                 * can be reset only with 'gluster volume quota <VOLNAME>
                 * disable'.
                 */

                if (!option || !strcmp ("features.quota-deem-statfs", option)) {
                        if (glusterd_is_volume_quota_enabled(volinfo)) {
                                ret = dict_set_dynstr_with_alloc (volinfo->dict,
                                            "features.quota-deem-statfs", "on");
                                if (ret) {
                                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                                GD_MSG_DICT_SET_FAILED,
                                                "Failed to set option "
                                                "'features.quota-deem-statfs' "
                                                "on volume %s",
                                                volinfo->volname);
                                        goto out;
                                }
                        }
                }

                if (!option || !strcmp ("features.ctr-enabled", option)) {
                        if (volinfo->type == GF_CLUSTER_TYPE_TIER) {
                                ret = dict_set_dynstr_with_alloc (volinfo->dict,
                                            "features.ctr-enabled", "on");
                                if (ret) {
                                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                                GD_MSG_DICT_SET_FAILED,
                                                "Failed to set option "
                                                "'features.ctr-enabled' "
                                                "on volume %s",
                                                volinfo->volname);
                                        goto out;
                                }
                        }
                }

        }
out:
        return ret;
}

void
glusterd_get_rebalance_volfile (glusterd_volinfo_t *volinfo,
                                char *path, int path_len)
{
        char                    workdir[PATH_MAX]      = {0,};
        glusterd_conf_t        *priv                   = THIS->private;

        GLUSTERD_GET_VOLUME_DIR (workdir, volinfo, priv);

        snprintf (path, path_len, "%s/%s-rebalance.vol", workdir,
                  volinfo->volname);
}

/* This function will update the backend file-system
 * type and the mount options in origin and snap brickinfo.
 * This will be later used to perform file-system specific operation
 * during LVM snapshot.
 *
 * @param brick_path       brickpath for which fstype to be found
 * @param brickinfo        brickinfo of snap/origin volume
 * @return 0 on success and -1 on failure
 */
int
glusterd_update_mntopts (char *brick_path, glusterd_brickinfo_t *brickinfo)
{
        int32_t               ret               = -1;
        char                 *mnt_pt            = NULL;
        char                  buff[PATH_MAX]    = "";
        struct mntent        *entry             = NULL;
        struct mntent         save_entry        = {0,};
        xlator_t             *this              = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (brick_path);
        GF_ASSERT (brickinfo);

        ret = glusterd_get_brick_root (brick_path, &mnt_pt);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRICKPATH_ROOT_GET_FAIL,
                        "getting the root "
                        "of the brick (%s) failed ", brick_path);
                goto out;
        }

        entry = glusterd_get_mnt_entry_info (mnt_pt, buff, sizeof (buff),
                                             &save_entry);
        if (!entry) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MNTENTRY_GET_FAIL,
                        "getting the mount entry for "
                        "the brick (%s) failed", brick_path);
                ret = -1;
                goto out;
        }

        strncpy (brickinfo->fstype, entry->mnt_type,
                 (sizeof (brickinfo->fstype) - 1));
        strcpy (brickinfo->mnt_opts, entry->mnt_opts);

        ret = 0;
out:
        if (mnt_pt)
                GF_FREE (mnt_pt);
        return ret;
}

int
glusterd_get_value_for_vme_entry (struct volopt_map_entry *vme, char **def_val)
{
        int                      ret = -1;
        char                    *key = NULL;
        xlator_t                *this = NULL;
        char                    *descr = NULL;
        char                    *local_def_val = NULL;
        void                    *dl_handle = NULL;
        volume_opt_list_t        vol_opt_handle = {{0},};

        this = THIS;
        GF_ASSERT (this);

        CDS_INIT_LIST_HEAD (&vol_opt_handle.list);

        if (_get_xlator_opt_key_from_vme (vme, &key)) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GET_KEY_FAILED,
                        "Failed to get %s key from "
                        "volume option entry", vme->key);
                goto out;
        }

        ret = xlator_volopt_dynload (vme->voltype, &dl_handle, &vol_opt_handle);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_XLATOR_VOLOPT_DYNLOAD_ERROR,
                        "xlator_volopt_dynload error "
                        "(%d)", ret);
                ret = -2;
                goto cont;
        }

        ret = xlator_option_info_list (&vol_opt_handle,key,
                                       &local_def_val, &descr);
        if (ret) {
                /*Swallow Error if option not found*/
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GET_KEY_FAILED,
                        "Failed to get option for %s "
                        "key", key);
                ret = -2;
                goto cont;
        }
        if (!local_def_val)
                local_def_val = "(null)";

        *def_val = gf_strdup (local_def_val);

cont:
        if (dl_handle) {
                dlclose (dl_handle);
                dl_handle = NULL;
                vol_opt_handle.given_opt = NULL;
        }
        if (key) {
                _free_xlator_opt_key (key);
                key = NULL;
        }

        if (ret)
                goto out;

out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_get_global_max_op_version (rpcsvc_request_t *req, dict_t *ctx,
                                    int count)
{
        int     ret = -1;
        char    *def_val = NULL;
        char    dict_key[50] = {0,};

        ret = glusterd_mgmt_v3_initiate_all_phases (req, GD_OP_MAX_OPVERSION,
                                                    ctx);

        ret = dict_get_str (ctx, "max-opversion", &def_val);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Failed to get max-opversion value from"
                        " dictionary");
                goto out;
        }

        sprintf (dict_key, "key%d", count);
        ret = dict_set_str (ctx, dict_key, GLUSTERD_MAX_OP_VERSION_KEY);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED, "Failed to set %s in "
                        "dictionary", GLUSTERD_MAX_OP_VERSION_KEY);
                goto out;
        }

        sprintf (dict_key, "value%d", count);
        ret = dict_set_dynstr_with_alloc (ctx, dict_key, def_val);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Failed to set %s for key %s in dictionary",
                        def_val, GLUSTERD_MAX_OP_VERSION_KEY);
                goto out;
        }

out:
        return ret;
}

int
glusterd_get_global_options_for_all_vols (rpcsvc_request_t *req, dict_t *ctx,
                                          char **op_errstr)
{
        int                     ret = -1;
        int                     count = 0;
        gf_boolean_t            all_opts = _gf_false;
        gf_boolean_t            key_found = _gf_false;
        glusterd_conf_t         *priv = NULL;
        xlator_t                *this = NULL;
        char                    *key = NULL;
        char                    *key_fixed = NULL;
        char                    dict_key[50] = {0,};
        char                    *def_val = NULL;
        char                    err_str[PATH_MAX] = {0,};
        char                    *allvolopt = NULL;
        int32_t                 i = 0;
        gf_boolean_t            exists = _gf_false;
        gf_boolean_t            need_free;

        this = THIS;
        GF_VALIDATE_OR_GOTO (THIS->name, this, out);

        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        GF_VALIDATE_OR_GOTO (this->name, ctx, out);

        ret = dict_get_str (ctx, "key", &key);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Failed to get option key from dictionary");
                goto out;
        }

        if (strcasecmp (key, "all") == 0)
                all_opts = _gf_true;
        else {
                exists = glusterd_check_option_exists (key, &key_fixed);
                if (!exists) {
                        snprintf (err_str, sizeof (err_str), "Option "
                                  "with name: %s does not exist", key);
                        gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                                GD_MSG_UNKNOWN_KEY, "%s", err_str);
                        if (key_fixed)
                                snprintf (err_str, sizeof (err_str),
                                          "Did you mean %s?", key_fixed);
                        ret = -1;
                        goto out;
                }
                if (key_fixed)
                        key = key_fixed;
        }

        ALL_VOLUME_OPTION_CHECK ("all", _gf_true, key, ret, op_errstr, out);

        for (i = 0; valid_all_vol_opts[i].option; i++) {
                allvolopt = gf_strdup (valid_all_vol_opts[i].option);

                if (!all_opts && strcmp (key, allvolopt) != 0)
                        continue;

                /* Found global option */
                if (strcmp (allvolopt, GLUSTERD_MAX_OP_VERSION_KEY) == 0) {
                        count++;
                        ret = glusterd_get_global_max_op_version (req, ctx,
                                                                  count);
                        if (ret)
                                goto out;
                        else
                                continue;
                }

                ret = dict_get_str (priv->opts, allvolopt, &def_val);

                /* If global option isn't set explicitly */

                need_free = _gf_false;
                if (!def_val) {
                        if (!strcmp (allvolopt,
                                     GLUSTERD_GLOBAL_OP_VERSION_KEY)) {
                                gf_asprintf (&def_val, "%d", priv->op_version);
                                need_free = _gf_true;
                        } else {
                                def_val = valid_all_vol_opts[i].dflt_val;
                        }
                }

                count++;
                sprintf (dict_key, "key%d", count);
                ret = dict_set_str (ctx, dict_key, allvolopt);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "Failed to set %s in dictionary", allvolopt);
                        goto out;
                }

                sprintf (dict_key, "value%d", count);
                ret = dict_set_dynstr_with_alloc (ctx, dict_key, def_val);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "Failed to set %s for key %s in dictionary",
                                def_val, allvolopt);
                        goto out;
                }

                if (need_free) {
                        GF_FREE (def_val);
                }
                def_val = NULL;
                allvolopt = NULL;

                if (!all_opts)
                        break;
        }

        ret = dict_set_int32 (ctx, "count", count);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                        "Failed to set count in dictionary");
        }

out:
        if (ret && !all_opts && !key_found) {
                if (err_str == NULL)
                        snprintf (err_str, sizeof (err_str),
                                  "option %s does not exist", key);
                if (*op_errstr == NULL)
                        *op_errstr = gf_strdup (err_str);
        }
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);

        return ret;
}

int
glusterd_get_default_val_for_volopt (dict_t *ctx, gf_boolean_t all_opts,
                                     char *input_key, char *orig_key,
                                     glusterd_volinfo_t *volinfo,
                                     char **op_errstr)
{
        struct volopt_map_entry *vme = NULL;
        int                      ret = -1;
        int                      count = 0;
        char                     err_str[PATH_MAX] = "";
        xlator_t                *this = NULL;
        char                    *def_val = NULL;
        char                     dict_key[50] = {0,};
        gf_boolean_t             key_found = _gf_false;
        glusterd_conf_t         *priv = NULL;
        dict_t                  *vol_dict = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        vol_dict = volinfo->dict;
        GF_VALIDATE_OR_GOTO (this->name, vol_dict, out);

        /* Check whether key is passed for a single option */
        if (!all_opts && !input_key) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_KEY_NULL,
                        "Key is NULL");
                goto out;
        }

        for (vme = &glusterd_volopt_map[0]; vme->key; vme++) {
                if (!all_opts && strcmp (vme->key, input_key))
                        continue;
                key_found = _gf_true;
                /* First look for the key in the priv->opts for global option
                 * and then into vol_dict, if its not present then look for
                 * translator default value */
                ret = dict_get_str (priv->opts, vme->key, &def_val);
                if (!def_val) {
                        ret = dict_get_str (vol_dict, vme->key, &def_val);
                        if (!def_val) {
                                /* For replicate volumes
                                 * performance.client-io-threads will be set to
                                 * off by default until explicitly turned on
                                 */
                                if (!strcmp (vme->key,
                                            "performance.client-io-threads")) {
                                        if (volinfo->type ==
                                            GF_CLUSTER_TYPE_REPLICATE ||
                                            volinfo->type ==
                                            GF_CLUSTER_TYPE_STRIPE_REPLICATE) {
                                                def_val = "off";
                                                goto set_count;
                                        }
                                }
                                if (vme->value) {
                                        def_val = vme->value;
                                } else {
                                        ret = glusterd_get_value_for_vme_entry
                                                 (vme, &def_val);
                                        if (!all_opts && ret)
                                                goto out;
                                        else if (ret == -2)
                                                continue;
                                }
                        }
                }
set_count:
                count++;
                sprintf (dict_key, "key%d", count);
                ret = dict_set_str(ctx, dict_key, vme->key);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "Failed to "
                                "set %s in dictionary", vme->key);
                        goto out;
                }
                sprintf (dict_key, "value%d", count);
                ret = dict_set_dynstr_with_alloc (ctx, dict_key, def_val);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "Failed to "
                                "set %s for key %s in dictionary", def_val,
                                vme->key);
                        goto out;
                }
                def_val = NULL;
                if (!all_opts)
                        break;

        }
        if (!all_opts && !key_found)
                goto out;

        ret = dict_set_int32 (ctx, "count", count);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Failed to set count "
                        "in dictionary");
        }

out:
        if (ret && !all_opts && !key_found) {
                snprintf (err_str, sizeof (err_str),
                          "option %s does not exist", orig_key);
                *op_errstr = gf_strdup (err_str);
        }
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_get_volopt_content (dict_t * ctx, gf_boolean_t xml_out)
{
        void                    *dl_handle = NULL;
        volume_opt_list_t        vol_opt_handle = {{0},};
        char                    *key = NULL;
        struct volopt_map_entry *vme = NULL;
        int                      ret = -1;
        char                    *def_val = NULL;
        char                    *descr = NULL;
        char                     output_string[51200] = {0, };
        char                    *output = NULL;
        char                     tmp_str[2048] = {0, };
#if (HAVE_LIB_XML)
        xmlTextWriterPtr         writer = NULL;
        xmlBufferPtr             buf = NULL;

        if (xml_out) {
                ret = init_sethelp_xml_doc (&writer, &buf);
                if (ret) /*logging done in init_xml_lib*/
                        goto out;
        }
#endif

        CDS_INIT_LIST_HEAD (&vol_opt_handle.list);

        for (vme = &glusterd_volopt_map[0]; vme->key; vme++) {

                if ((vme->type == NO_DOC) || (vme->type == GLOBAL_NO_DOC))
                        continue;

                if (vme->description) {
                        descr = vme->description;
                        def_val = vme->value;
                } else {
                        if (_get_xlator_opt_key_from_vme (vme, &key)) {
                                gf_msg_debug ("glusterd", 0, "Failed to "
                                        "get %s key from volume option entry",
                                        vme->key);
                                goto out; /*Some error while geting key*/
                        }

                        ret = xlator_volopt_dynload (vme->voltype,
                                                     &dl_handle,
                                                     &vol_opt_handle);

                        if (ret) {
                                gf_msg_debug ("glusterd", 0,
                                        "xlator_volopt_dynload error(%d)", ret);
                                ret = 0;
                                goto cont;
                        }

                        ret = xlator_option_info_list (&vol_opt_handle, key,
                                                       &def_val, &descr);
                        if (ret) { /*Swallow Error i.e if option not found*/
                                gf_msg_debug ("glusterd", 0,
                                        "Failed to get option for %s key", key);
                                ret = 0;
                                goto cont;
                        }
                }

                if (xml_out) {
#if (HAVE_LIB_XML)
                        if (xml_add_volset_element (writer,vme->key,
                                                    def_val, descr)) {
                                ret = -1;
                                goto cont;
                        }
#else
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_MODULE_NOT_INSTALLED,
                                "Libxml not present");
#endif
                } else {
                        snprintf (tmp_str, sizeof (tmp_str), "Option: %s\nDefault "
                                        "Value: %s\nDescription: %s\n\n",
                                        vme->key, def_val, descr);
                        strcat (output_string, tmp_str);
                }
cont:
                if (dl_handle) {
                        dlclose (dl_handle);
                        dl_handle = NULL;
                        vol_opt_handle.given_opt = NULL;
                }
                if (key) {
                        _free_xlator_opt_key (key);
                        key = NULL;
                }
                if (ret)
                        goto out;
        }

#if (HAVE_LIB_XML)
        if ((xml_out) &&
            (ret = end_sethelp_xml_doc (writer)))
                goto out;
#else
        if (xml_out)
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_MODULE_NOT_INSTALLED,
                        "Libxml not present");
#endif

        if (!xml_out)
                output = gf_strdup (output_string);
        else
#if (HAVE_LIB_XML)
                output = gf_strdup ((char *)buf->content);
#else
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_MODULE_NOT_INSTALLED,
                        "Libxml not present");
#endif

        if (NULL == output) {
                ret = -1;
                goto out;
        }

        ret = dict_set_dynstr (ctx, "help-str", output);
out:
        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
        return ret;
}

int
glusterd_check_client_op_version_support (char *volname, uint32_t op_version,
                                          char **op_errstr)
{
        int                     ret = 0;
        xlator_t                *this = NULL;
        glusterd_conf_t         *priv = NULL;
        rpc_transport_t         *xprt = NULL;

        this = THIS;
        GF_ASSERT(this);
        priv = this->private;
        GF_ASSERT(priv);

        pthread_mutex_lock (&priv->xprt_lock);
        list_for_each_entry (xprt, &priv->xprt_list, list) {
                if ((!strcmp(volname, xprt->peerinfo.volname)) &&
                    ((op_version > xprt->peerinfo.max_op_version) ||
                     (op_version < xprt->peerinfo.min_op_version))) {
                        ret = -1;
                        break;
                }
        }
        pthread_mutex_unlock (&priv->xprt_lock);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_UNSUPPORTED_VERSION,
                        "Client %s is running with min_op_version as %d and "
                        "max_op_version as %d and don't support the required "
                        "op-version %d", xprt->peerinfo.identifier,
                        xprt->peerinfo.min_op_version,
                        xprt->peerinfo.max_op_version, op_version);
                if (op_errstr)
                        ret = gf_asprintf (op_errstr, "One of the client %s is "
                                           "running with op-version %d and "
                                           "doesn't support the required "
                                           "op-version %d. This client needs to"
                                           " be upgraded or disconnected "
                                           "before running this command again",
                                           xprt->peerinfo.identifier,
                                           xprt->peerinfo.max_op_version,
                                           op_version);

                return -1;
        }
        return 0;
}

gf_boolean_t
glusterd_have_peers ()
{
        xlator_t        *this = NULL;
        glusterd_conf_t *conf = NULL;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        return !cds_list_empty (&conf->peers);
}

gf_boolean_t
glusterd_is_volume_started (glusterd_volinfo_t  *volinfo)
{
        GF_ASSERT (volinfo);
        return (volinfo->status == GLUSTERD_STATUS_STARTED);
}

int
glusterd_volume_get_type_str (glusterd_volinfo_t *volinfo, char **voltype_str)
{
        int ret = -1;
        int type = 0;

        GF_VALIDATE_OR_GOTO (THIS->name, volinfo, out);

        type = get_vol_type (volinfo->type, volinfo->dist_leaf_count,
                             volinfo->brick_count);

        *voltype_str = vol_type_str[type];

        ret = 0;
out:
        return ret;
}

int
glusterd_volume_get_status_str (glusterd_volinfo_t *volinfo, char *status_str)
{
        int ret = -1;

        GF_VALIDATE_OR_GOTO (THIS->name, volinfo, out);
        GF_VALIDATE_OR_GOTO (THIS->name, status_str, out);

        switch (volinfo->status) {
        case GLUSTERD_STATUS_NONE:
                sprintf (status_str, "%s", "Created");
                break;
        case GLUSTERD_STATUS_STARTED:
                sprintf (status_str, "%s", "Started");
                break;
        case GLUSTERD_STATUS_STOPPED:
                sprintf (status_str, "%s", "Stopped");
                break;
        default:
                goto out;

        }
        ret = 0;
out:
        return ret;
}

int
glusterd_volume_get_transport_type_str (glusterd_volinfo_t *volinfo,
                                        char *transport_type_str)
{
        int ret = -1;

        GF_VALIDATE_OR_GOTO (THIS->name, volinfo, out);
        GF_VALIDATE_OR_GOTO (THIS->name, transport_type_str, out);

        switch (volinfo->transport_type) {
        case GF_TRANSPORT_TCP:
                sprintf (transport_type_str, "%s", "tcp");
                break;
        case GF_TRANSPORT_RDMA:
                sprintf (transport_type_str, "%s", "rdma");
                break;
        case GF_TRANSPORT_BOTH_TCP_RDMA:
                sprintf (transport_type_str, "%s", "tcp_rdma_both");
                break;
        default:
                goto out;

        }
        ret = 0;
out:
        return ret;
}

int
glusterd_volume_get_quorum_status_str (glusterd_volinfo_t *volinfo,
                                       char *quorum_status_str)
{
        int ret = -1;

        GF_VALIDATE_OR_GOTO (THIS->name, volinfo, out);
        GF_VALIDATE_OR_GOTO (THIS->name, quorum_status_str, out);

        switch (volinfo->quorum_status) {
        case NOT_APPLICABLE_QUORUM:
                sprintf (quorum_status_str, "%s", "not_applicable");
                break;
        case MEETS_QUORUM:
                sprintf (quorum_status_str, "%s", "meets");
                break;
        case DOESNT_MEET_QUORUM:
                sprintf (quorum_status_str, "%s", "does_not_meet");
                break;
        default:
                goto out;

        }
        ret = 0;
out:
        return ret;
}

int
glusterd_volume_get_rebalance_status_str (glusterd_volinfo_t *volinfo,
                                          char *rebal_status_str)
{
        int ret = -1;

        GF_VALIDATE_OR_GOTO (THIS->name, volinfo, out);
        GF_VALIDATE_OR_GOTO (THIS->name, rebal_status_str, out);

        switch (volinfo->rebal.defrag_status) {
        case GF_DEFRAG_STATUS_NOT_STARTED:
                sprintf (rebal_status_str, "%s", "not_started");
                break;
        case GF_DEFRAG_STATUS_STARTED:
                sprintf (rebal_status_str, "%s", "started");
                break;
        case GF_DEFRAG_STATUS_STOPPED:
                sprintf (rebal_status_str, "%s", "stopped");
                break;
        case GF_DEFRAG_STATUS_COMPLETE:
                sprintf (rebal_status_str, "%s", "completed");
                break;
        case GF_DEFRAG_STATUS_FAILED:
                sprintf (rebal_status_str, "%s", "failed");
                break;
        case GF_DEFRAG_STATUS_LAYOUT_FIX_STARTED:
                sprintf (rebal_status_str, "%s", "layout_fix_started");
                break;
        case GF_DEFRAG_STATUS_LAYOUT_FIX_STOPPED:
                sprintf (rebal_status_str, "%s", "layout_fix_stopped");
                break;
        case GF_DEFRAG_STATUS_LAYOUT_FIX_COMPLETE:
                sprintf (rebal_status_str, "%s", "layout_fix_complete");
                break;
        case GF_DEFRAG_STATUS_LAYOUT_FIX_FAILED:
                sprintf (rebal_status_str, "%s", "layout_fix_failed");
                break;
        default:
                goto out;
        }
        ret = 0;
out:
        return ret;
}

int
glusterd_volume_get_hot_tier_type_str (glusterd_volinfo_t *volinfo,
                                       char **hot_tier_type_str)
{
        int ret = -1;
        int hot_tier_type = 0;
        int hot_dist_count = 0;

        GF_VALIDATE_OR_GOTO (THIS->name, volinfo, out);
        GF_VALIDATE_OR_GOTO (THIS->name, hot_tier_type_str, out);

        hot_dist_count = volinfo->tier_info.hot_replica_count ?
                                volinfo->tier_info.hot_replica_count : 1;

        hot_tier_type = get_vol_type (volinfo->tier_info.hot_type, hot_dist_count,
                                      volinfo->tier_info.hot_brick_count);

        *hot_tier_type_str = vol_type_str[hot_tier_type];

        ret = 0;
out:
        return ret;
}

int
glusterd_volume_get_cold_tier_type_str (glusterd_volinfo_t *volinfo,
                                        char **cold_tier_type_str)
{
        int ret = -1;
        int cold_tier_type = 0;

        GF_VALIDATE_OR_GOTO (THIS->name, volinfo, out);
        GF_VALIDATE_OR_GOTO (THIS->name, cold_tier_type_str, out);

        cold_tier_type = get_vol_type (volinfo->tier_info.cold_type,
                                       volinfo->tier_info.cold_dist_leaf_count,
                                       volinfo->tier_info.cold_brick_count);

        *cold_tier_type_str = vol_type_str[cold_tier_type];

        ret = 0;
out:
        return ret;
}

/* This function will insert the element to the list in a order.
   Order will be based on the compare function provided as a input.
   If element to be inserted in ascending order compare should return:
    0: if both the arguments are equal
   >0: if first argument is greater than second argument
   <0: if first argument is less than second argument */
void
glusterd_list_add_order (struct cds_list_head *new, struct cds_list_head *head,
                        int (*compare)(struct cds_list_head *,
                                       struct cds_list_head *))
{
        struct cds_list_head *pos = NULL;

        cds_list_for_each_rcu (pos, head) {
                if (compare (new, pos) <= 0)
                        break;
        }

        cds_list_add_rcu (new, rcu_dereference (pos->prev));
}


int
glusterd_disallow_op_for_tier (glusterd_volinfo_t *volinfo, glusterd_op_t op,
                               int cmd)
{

        xlator_t          *this       = NULL;
        int                ret        = 0;

        this = THIS;
        GF_VALIDATE_OR_GOTO (this->name, volinfo, out);

        if (volinfo->type != GF_CLUSTER_TYPE_TIER)
                goto out;

        switch (op) {
        case GD_OP_ADD_BRICK:
        case GD_OP_REPLACE_BRICK:
	case GD_OP_RESET_BRICK:
                ret = -1;
                gf_msg_debug (this->name, 0, "Operation not "
                        "permitted on tiered volume %s",
                        volinfo->volname);
                break;
        case GD_OP_REBALANCE:
                switch (cmd) {
                case GF_DEFRAG_CMD_START_TIER:
                case GF_DEFRAG_CMD_STATUS_TIER:
                case GF_DEFRAG_CMD_START_DETACH_TIER:
                case GF_DEFRAG_CMD_STOP_DETACH_TIER:
                case GF_DEFRAG_CMD_STATUS:
                case GF_DEFRAG_CMD_DETACH_STATUS:
                case GF_DEFRAG_CMD_STOP_TIER:
                case GF_DEFRAG_CMD_DETACH_START:
                case GF_DEFRAG_CMD_DETACH_COMMIT:
                case GF_DEFRAG_CMD_DETACH_COMMIT_FORCE:
                case GF_DEFRAG_CMD_DETACH_STOP:
                        ret = 0;
                        break;
                default:
                        gf_msg_debug (this->name, 0,
                             "Rebalance Operation not permitted"
                             " on tiered volume %s",
                             volinfo->volname);
                        ret = -1;
                        break;
                }
                break;
        case GD_OP_REMOVE_BRICK:
                switch (cmd) {
                case GF_DEFRAG_CMD_DETACH_START:
                case GF_OP_CMD_DETACH_COMMIT_FORCE:
                case GF_OP_CMD_DETACH_COMMIT:
                case GF_OP_CMD_DETACH_START:
                case GF_DEFRAG_CMD_STOP_DETACH_TIER:
                        ret = 0;
                        break;
                default:
                        gf_msg_debug (this->name, 0,
                             "Remove brick operation not "
                             "permitted on tiered volume %s",
                             volinfo->volname);
                        ret = -1;
                        break;
                }
                break;
        default:
                break;
        }
out:
        return ret;
}

int32_t
glusterd_count_connected_peers (int32_t *count)
{
        glusterd_peerinfo_t  *peerinfo  = NULL;
        glusterd_conf_t      *conf      = NULL;
        int32_t               ret       = -1;
        xlator_t             *this      = NULL;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", this, out);
        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, conf, out);
        GF_VALIDATE_OR_GOTO (this->name, count, out);

        *count = 1;

        rcu_read_lock ();
        cds_list_for_each_entry_rcu (peerinfo, &conf->peers, uuid_list) {
                /* Find peer who is connected and is a friend */
                if ((peerinfo->connected) &&
                     (peerinfo->state.state == GD_FRIEND_STATE_BEFRIENDED)) {
                        (*count)++;
                }
        }
        rcu_read_unlock ();

        ret = 0;
out:
        return ret;
}

char*
gd_get_shd_key (int type)
{
        char            *key               = NULL;

        switch (type) {
        case GF_CLUSTER_TYPE_REPLICATE:
        case GF_CLUSTER_TYPE_STRIPE_REPLICATE:
                key = "cluster.self-heal-daemon";
                break;
        case GF_CLUSTER_TYPE_DISPERSE:
                key = "cluster.disperse-self-heal-daemon";
                break;
        default:
                key = NULL;
                break;
        }
        return key;
}

int
glusterd_handle_replicate_brick_ops (glusterd_volinfo_t *volinfo,
                                     glusterd_brickinfo_t *brickinfo,
                                     glusterd_op_t op)
{
        int32_t                    ret               = -1;
        char                       tmpmount[]        = "/tmp/mntXXXXXX";
        char                       logfile[PATH_MAX] = {0,};
        int                        dirty[3]          = {0,};
        runner_t                   runner            = {0};
        glusterd_conf_t           *priv              = NULL;
        char                      *pid               = NULL;
        char                       vpath[PATH_MAX]   = {0,};
        char                      *volfileserver     = NULL;

        priv = THIS->private;
        GF_VALIDATE_OR_GOTO (THIS->name, priv, out);

        dirty[2] = hton32(1);

        ret = sys_lsetxattr (brickinfo->path, GF_AFR_DIRTY, dirty,
                             sizeof (dirty), 0);
        if (ret == -1) {
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        GD_MSG_SETXATTR_FAIL, "Failed to set extended"
                        " attribute %s : %s.", GF_AFR_DIRTY, strerror (errno));
                goto out;
        }

        if (mkdtemp (tmpmount) == NULL) {
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        GD_MSG_DIR_OP_FAILED,
                        "failed to create a temporary mount directory.");
                ret = -1;
                goto out;
        }

        ret = gf_asprintf (&pid, "%d", GF_CLIENT_PID_SELF_HEALD);
        if (ret < 0)
                goto out;

        switch (op) {
        case GD_OP_REPLACE_BRICK:
                if (dict_get_str (THIS->options,
                                  "transport.socket.bind-address",
                                  &volfileserver) != 0)
                        volfileserver = "localhost";

                snprintf (logfile, sizeof (logfile),
                          DEFAULT_LOG_FILE_DIRECTORY"/%s-replace-brick-mount.log",
                          volinfo->volname);
                if (!*logfile) {
                        ret = -1;
                        goto out;
                }
                runinit (&runner);
                runner_add_args (&runner, SBIN_DIR"/glusterfs",
                                 "-s", volfileserver,
                                 "--volfile-id", volinfo->volname,
                                 "--client-pid", pid,
                                 "-l", logfile, tmpmount, NULL);
                break;

        case GD_OP_ADD_BRICK:
                snprintf (logfile, sizeof (logfile),
                          DEFAULT_LOG_FILE_DIRECTORY"/%s-add-brick-mount.log",
                          volinfo->volname);
                if (!*logfile) {
                        ret = -1;
                        goto out;
                }
                ret = glusterd_get_dummy_client_filepath (vpath, volinfo,
                                                    volinfo->transport_type);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Failed to get "
                                "volfile path");
                        goto out;
                }
                runinit (&runner);
                runner_add_args (&runner, SBIN_DIR"/glusterfs",
                                 "--volfile", vpath,
                                 "--client-pid", pid,
                                 "-l", logfile, tmpmount, NULL);
                break;
        default:
                break;
        }
        synclock_unlock (&priv->big_lock);
        ret = runner_run (&runner);

        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "mount command"
                        " failed.");
                goto lock;
        }
        ret = sys_lsetxattr (tmpmount, (op == GD_OP_REPLACE_BRICK) ?
                             GF_AFR_REPLACE_BRICK : GF_AFR_ADD_BRICK,
                             brickinfo->brick_id, sizeof (brickinfo->brick_id),
                             0);
        if (ret == -1)
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        GD_MSG_SETXATTR_FAIL, "Failed to set extended"
                        " attribute %s : %s", (op == GD_OP_REPLACE_BRICK) ?
                        GF_AFR_REPLACE_BRICK : GF_AFR_ADD_BRICK,
                        strerror (errno));
        gf_umount_lazy (THIS->name, tmpmount, 1);
lock:
        synclock_lock (&priv->big_lock);
out:
        if (pid)
                GF_FREE (pid);
        gf_msg_debug ("glusterd", 0, "Returning with ret");
        return ret;
}

void
assign_brick_groups (glusterd_volinfo_t *volinfo)
{
        glusterd_brickinfo_t    *brickinfo      = NULL;
        uint16_t                group_num       = 0;
        int                     in_group        = 0;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                brickinfo->group = group_num;
                if (++in_group >= volinfo->replica_count) {
                        in_group = 0;
                        ++group_num;
                }
        }
}

glusterd_brickinfo_t*
get_last_brick_of_brick_group (glusterd_volinfo_t *volinfo,
                               glusterd_brickinfo_t *brickinfo)
{
        glusterd_brickinfo_t  *next = NULL;
        glusterd_brickinfo_t  *last = NULL;

        last = brickinfo;
        for (;;) {
                next = list_next (last, &volinfo->bricks,
                                  glusterd_brickinfo_t, brick_list);
                if (!next || (next->group != brickinfo->group)) {
                        break;
                }
                last = next;
        }

        return last;
}

int
glusterd_get_rb_dst_brickinfo (glusterd_volinfo_t *volinfo,
                               glusterd_brickinfo_t **brickinfo)
{
        int32_t                 ret = -1;

        if (!volinfo || !brickinfo)
                goto out;

        *brickinfo = volinfo->rep_brick.dst_brick;

        ret = 0;

out:
        return ret;
}

int
rb_update_dstbrick_port (glusterd_brickinfo_t *dst_brickinfo, dict_t *rsp_dict,
                         dict_t *req_dict)
{
        int     ret           = 0;
        int     dict_ret      = 0;
        int     dst_port      = 0;

        dict_ret = dict_get_int32 (req_dict, "dst-brick-port", &dst_port);
        if (!dict_ret)
                dst_brickinfo->port = dst_port;

        if (gf_is_local_addr (dst_brickinfo->hostname)) {
                gf_msg ("glusterd", GF_LOG_INFO, 0,
                        GD_MSG_BRK_PORT_NO_ADD_INDO,
                        "adding dst-brick port no %d", dst_port);

                if (rsp_dict) {
                        ret = dict_set_int32 (rsp_dict, "dst-brick-port",
                                              dst_brickinfo->port);
                        if (ret) {
                                gf_msg_debug ("glusterd", 0,
                                        "Could not set dst-brick port no in rsp dict");
                                goto out;
                        }
                }

                if (req_dict && !dict_ret) {
                        ret = dict_set_int32 (req_dict, "dst-brick-port",
                                              dst_brickinfo->port);
                        if (ret) {
                                gf_msg_debug ("glusterd", 0,
                                        "Could not set dst-brick port no");
                                goto out;
                        }
                }
        }
out:
        return ret;
}

int
glusterd_brick_op_prerequisites (dict_t *dict,
                                 char **op,
                                 glusterd_op_t *gd_op, char **volname,
                                 glusterd_volinfo_t **volinfo,
                                 char **src_brick, glusterd_brickinfo_t
                                 **src_brickinfo, char *pidfile,
                                 char **op_errstr, dict_t *rsp_dict)
{
        int                                      ret                = 0;
        char                                     msg[2048]          = {0};
        gsync_status_param_t                     param              = {0,};
        xlator_t                                *this               = NULL;
        glusterd_conf_t                         *priv               = NULL;
        glusterd_volinfo_t                      *v                  = NULL;
        glusterd_brickinfo_t                    *b                  = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "operation", op);
        if (ret) {
                gf_msg_debug (this->name, 0,
                        "dict get on operation type failed");
                goto out;
        }

        *gd_op = gd_cli_to_gd_op (*op);
        if (*gd_op < 0)
                goto out;

        ret = dict_get_str (dict, "volname", volname);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get volume name");
                goto out;
        }

        ret = glusterd_volinfo_find (*volname, volinfo);
        if (ret) {
                snprintf (msg, sizeof (msg), "volume: %s does not exist",
                          *volname);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        if (GLUSTERD_STATUS_STARTED != (*volinfo)->status) {
                ret = -1;
                snprintf (msg, sizeof (msg), "volume: %s is not started",
                          *volname);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        ret = glusterd_disallow_op_for_tier (*volinfo, *gd_op, -1);
        if (ret) {
                snprintf (msg, sizeof (msg), "%sbrick commands are not "
                          "supported on tiered volume %s",
                          (*gd_op == GD_OP_REPLACE_BRICK) ? "replace-" :
                          "reset-",
                           *volname);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        /* If geo-rep is configured, for this volume, it should be stopped. */
        param.volinfo = *volinfo;
        ret = glusterd_check_geo_rep_running (&param, op_errstr);
        if (ret || param.is_active) {
                ret = -1;
                goto out;
        }

        if (glusterd_is_defrag_on(*volinfo)) {
                snprintf (msg, sizeof(msg), "Volume name %s rebalance is in "
                          "progress. Please retry after completion", *volname);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_OIP_RETRY_LATER, "%s", msg);
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }

        if (dict) {
                if (!glusterd_is_fuse_available ()) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                (*gd_op == GD_OP_REPLACE_BRICK) ?
                                GD_MSG_RB_CMD_FAIL :
                                GD_MSG_RESET_BRICK_CMD_FAIL,
                                "Unable to open /dev/"
                                "fuse (%s), %s command failed",
                                strerror (errno), gd_rb_op_to_str (*op));
                        snprintf (msg, sizeof(msg), "Fuse unavailable\n "
                                "%s failed", gd_rb_op_to_str (*op));
                        *op_errstr = gf_strdup (msg);
                        ret = -1;
                        goto out;
                }
        }

        ret = dict_get_str (dict, "src-brick", src_brick);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get src brick");
                goto out;
        }

        gf_msg_debug (this->name, 0, "src brick=%s", *src_brick);

        ret = glusterd_volume_brickinfo_get_by_brick (*src_brick, *volinfo,
                                                      src_brickinfo,
                                                      _gf_false);
        if (ret) {
                snprintf (msg, sizeof (msg), "brick: %s does not exist in "
                          "volume: %s", *src_brick, *volname);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        if (gf_is_local_addr ((*src_brickinfo)->hostname)) {
                gf_msg_debug (this->name, 0,
                        "I AM THE SOURCE HOST");
                if ((*src_brickinfo)->port && rsp_dict) {
                        ret = dict_set_int32 (rsp_dict, "src-brick-port",
                                              (*src_brickinfo)->port);
                        if (ret) {
                                gf_msg_debug (this->name, 0,
                                        "Could not set src-brick-port=%d",
                                        (*src_brickinfo)->port);
                        }
                }

                v = *volinfo;
                b = *src_brickinfo;
                GLUSTERD_GET_BRICK_PIDFILE (pidfile, v, b,
                                            priv);
        }

        ret = 0;
out:
        return ret;
}

int
glusterd_get_dst_brick_info (char **dst_brick, char *volname, char **op_errstr,
                             glusterd_brickinfo_t **dst_brickinfo, char **host,
                             dict_t *dict, char **dup_dstbrick)
{

        char                                    *path               = NULL;
        char                                    *c                  = NULL;
        char                                     msg[2048]          = {0};
        xlator_t                                *this               = NULL;
        glusterd_conf_t                         *priv               = NULL;
        int                                      ret                = 0;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "dst-brick", dst_brick);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Unable to get dest brick.");
                goto out;
        }

        gf_msg_debug (this->name, 0, "dst brick=%s", *dst_brick);

        if (!glusterd_store_is_valid_brickpath (volname, *dst_brick) ||
                !glusterd_is_valid_volfpath (volname, *dst_brick)) {
                snprintf (msg, sizeof (msg), "brick path %s is too "
                          "long.", *dst_brick);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRKPATH_TOO_LONG, "%s", msg);
                *op_errstr = gf_strdup (msg);

                ret = -1;
                goto out;
        }

        *dup_dstbrick = gf_strdup (*dst_brick);
        if (!*dup_dstbrick) {
                ret = -1;
                goto out;
        }

        /*
         * IPv4 address contains '.' and ipv6 addresses contains ':'
         * So finding the last occurance of ':' to
         * mark the start of brick path
         */
        c = strrchr(*dup_dstbrick, ':');
        if (c != NULL) {
                c[0] = '\0';
                *host = *dup_dstbrick;
                path = c++;
        }

        if (!host || !path) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BAD_FORMAT,
                        "dst brick %s is not of "
                        "form <HOSTNAME>:<export-dir>",
                        *dst_brick);
                ret = -1;
                goto out;
        }

        ret = glusterd_brickinfo_new_from_brick (*dst_brick,
                                                 dst_brickinfo,
                                                 _gf_true, NULL);
        if (ret)
                goto out;

        ret = 0;
out:
        return ret;
}

glusterd_op_t
gd_cli_to_gd_op (char *cli_op)
{
        if (!strcmp (cli_op, "GF_RESET_OP_START") ||
            !strcmp(cli_op, "GF_RESET_OP_COMMIT") ||
            !strcmp (cli_op, "GF_RESET_OP_COMMIT_FORCE")) {
                return GD_OP_RESET_BRICK;
        }

        if (!strcmp (cli_op, "GF_REPLACE_OP_COMMIT_FORCE"))
                return GD_OP_REPLACE_BRICK;

        return -1;
}

char *
gd_rb_op_to_str (char *op)
{
        if (!strcmp (op, "GF_RESET_OP_START"))
                return "reset-brick start";
        if (!strcmp (op, "GF_RESET_OP_COMMIT"))
                return "reset-brick commit";
        if (!strcmp (op, "GF_RESET_OP_COMMIT_FORCE"))
                return "reset-brick commit force";
        if (!strcmp (op, "GF_REPLACE_OP_COMMIT_FORCE"))
                return "replace-brick commit force";
        return NULL;
}
