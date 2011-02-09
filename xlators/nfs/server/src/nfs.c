/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
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

/* This is the primary translator source for NFS.
 * Every other protocol version gets initialized from here.
 */


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "defaults.h"
#include "rpcsvc.h"
#include "dict.h"
#include "xlator.h"
#include "nfs.h"
#include "mem-pool.h"
#include "logging.h"
#include "nfs-fops.h"
#include "inode.h"
#include "mount3.h"
#include "nfs3.h"
#include "nfs-mem-types.h"

/* Every NFS version must call this function with the init function
 * for its particular version.
 */
int
nfs_add_initer (struct list_head *list, nfs_version_initer_t init)
{
        struct nfs_initer_list  *new = NULL;
        if ((!list) || (!init))
                return -1;

        new = GF_CALLOC (1, sizeof (*new), gf_nfs_mt_nfs_initer_list);
        if (!new) {
                gf_log (GF_NFS, GF_LOG_ERROR, "Memory allocation failed");
                return -1;
        }

        new->init = init;
        list_add_tail (&new->list, list);
        return 0;
}


int
nfs_deinit_versions (struct list_head *versions, xlator_t *this)
{
        struct nfs_initer_list          *version = NULL;
        struct nfs_initer_list          *tmp = NULL;
        struct nfs_state                *nfs = NULL;

        if ((!versions) || (!this))
                return -1;

        nfs = (struct nfs_state *)this->private;
        list_for_each_entry_safe (version, tmp, versions, list) {
                /* TODO: Add version specific destructor.
                 * if (!version->deinit)
                        goto err;

                   version->deinit (this);
                */
                if (version->program)
                        nfs_rpcsvc_program_unregister (nfs->rpcsvc,
                                                  *(version->program));

                list_del (&version->list);
                GF_FREE (version);
        }

        return 0;
}


int
nfs_init_versions (struct nfs_state *nfs, xlator_t *this)
{
        struct nfs_initer_list          *version = NULL;
        struct nfs_initer_list          *tmp = NULL;
        rpcsvc_program_t                *prog = NULL;
        int                             ret = -1;
        struct list_head                *versions = NULL;

        if ((!nfs) || (!this))
                return -1;

        gf_log (GF_NFS, GF_LOG_DEBUG, "Initing protocol versions");
        versions = &nfs->versions;
        list_for_each_entry_safe (version, tmp, versions, list) {
                if (!version->init) {
                        ret = -1;
                        goto err;
                }

                prog = version->init (this);
                if (!prog) {
                        ret = -1;
                        goto err;
                }
                prog->actorxl = this;
                version->program = prog;
                if (nfs->override_portnum)
                        prog->progport = nfs->override_portnum;
                gf_log (GF_NFS, GF_LOG_DEBUG, "Starting program: %s",
                        prog->progname);
                ret = nfs_rpcsvc_program_register (nfs->rpcsvc, *prog);
                if (ret == -1) {
                        gf_log (GF_NFS, GF_LOG_ERROR, "Program init failed");
                        goto err;
                }
        }

        ret = 0;
err:
        return ret;
}


int
nfs_add_all_initiators (struct nfs_state *nfs)
{
        int     ret = 0;

        /* Add the initializers for all versions. */
        ret = nfs_add_initer (&nfs->versions, mnt3svc_init);
        if (ret == -1) {
                gf_log (GF_NFS, GF_LOG_ERROR, "Failed to add protocol"
                        " initializer");
                goto ret;
        }

        ret = nfs_add_initer (&nfs->versions, mnt1svc_init);
        if (ret == -1) {
                gf_log (GF_NFS, GF_LOG_ERROR, "Failed to add protocol"
                        " initializer");
                goto ret;
        }

        ret = nfs_add_initer (&nfs->versions, nfs3svc_init);
        if (ret == -1) {
                gf_log (GF_NFS, GF_LOG_ERROR, "Failed to add protocol"
                        " initializer");
                goto ret;
        }

        ret = 0;
ret:
        return ret;
}


int
nfs_subvolume_started (struct nfs_state *nfs, xlator_t *xl)
{
        int     x = 0;
        int     started = 0;

        if ((!nfs) || (!xl))
                return 1;

        LOCK (&nfs->svinitlock);
        {
                for (;x < nfs->allsubvols; ++x) {
                        if (nfs->initedxl[x] == xl) {
                                started = 1;
                                goto unlock;
                       }
               }
        }
unlock:
        UNLOCK (&nfs->svinitlock);

        return started;
}


int
nfs_subvolume_set_started (struct nfs_state *nfs, xlator_t *xl)
{
        int     x = 0;

        if ((!nfs) || (!xl))
                return 1;

        LOCK (&nfs->svinitlock);
        {
                for (;x < nfs->allsubvols; ++x) {
                        if (nfs->initedxl[x] == NULL) {
                                nfs->initedxl[x] = xl;
                                ++nfs->upsubvols;
                                gf_log (GF_NFS, GF_LOG_DEBUG, "Starting up: %s "
                                        ", vols started till now: %d", xl->name,
                                        nfs->upsubvols);
                                goto unlock;
                        }
               }
        }
unlock:
        UNLOCK (&nfs->svinitlock);

        return 0;
}


int32_t
nfs_start_subvol_lookup_cbk (call_frame_t *frame, void *cookie,
                             xlator_t *this, int32_t op_ret, int32_t op_errno,
                             inode_t *inode, struct iatt *buf, dict_t *xattr,
                             struct iatt *postparent)
{
        if (op_ret == -1) {
                gf_log (GF_NFS, GF_LOG_CRITICAL, "Failed to lookup root: %s",
                        strerror (op_errno));
                goto err;
        }

        nfs_subvolume_set_started (this->private, ((xlator_t *)cookie));
        gf_log (GF_NFS, GF_LOG_TRACE, "Started %s", ((xlator_t *)cookie)->name);
err:
        return 0;
}


int
nfs_startup_subvolume (xlator_t *nfsx, xlator_t *xl)
{
        int             ret = -1;
        loc_t           rootloc = {0, };
        nfs_user_t      nfu = {0, };

        if ((!nfsx) || (!xl))
                return -1;

        if (nfs_subvolume_started (nfsx->private, xl)) {
                gf_log (GF_NFS,GF_LOG_TRACE, "Subvolume already started: %s",
                        xl->name);
                ret = 0;
                goto err;
        }

        ret = nfs_inode_loc_fill (xl->itable->root, &rootloc);
        if (ret == -1) {
                gf_log (GF_NFS, GF_LOG_CRITICAL, "Failed to init root loc");
                goto err;
        }

        nfs_user_root_create (&nfu);
        ret = nfs_fop_lookup (nfsx, xl, &nfu, &rootloc,
                              nfs_start_subvol_lookup_cbk,
                              (void *)nfsx->private);
        if (ret < 0) {
                gf_log (GF_NFS, GF_LOG_CRITICAL, "Failed to lookup root: %s",
                        strerror (-ret));
                goto err;
        }

        nfs_loc_wipe (&rootloc);

err:
        return ret;
}

int
nfs_startup_subvolumes (xlator_t *nfsx)
{
        int                     ret = -1;
        xlator_list_t           *cl = NULL;
        struct nfs_state        *nfs = NULL;

        if (!nfsx)
                return -1;

        nfs = nfsx->private;
        cl = nfs->subvols;
        while (cl) {
                gf_log (GF_NFS, GF_LOG_DEBUG, "Starting subvolume: %s",
                        cl->xlator->name);
                ret = nfs_startup_subvolume (nfsx, cl->xlator);
                if (ret == -1) {
                        gf_log (GF_NFS, GF_LOG_CRITICAL, "Failed to start-up "
                                "xlator: %s", cl->xlator->name);
                        goto err;
                }
                cl = cl->next;
        }

        ret = 0;
err:
        return ret;
}


int
nfs_init_subvolume (struct nfs_state *nfs, xlator_t *xl)
{
        unsigned int    lrusize = 0;
        int             ret = -1;

        if ((!nfs) || (!xl))
                return -1;

        lrusize = nfs->memfactor * GF_NFS_INODE_LRU_MULT;
        xl->itable = inode_table_new (lrusize, xl);
        if (!xl->itable) {
                gf_log (GF_NFS, GF_LOG_CRITICAL, "Failed to allocate "
                        "inode table");
                goto err;
        }
        ret = 0;
err:
        return ret;
}

int
nfs_init_subvolumes (struct nfs_state *nfs, xlator_list_t *cl)
{
        int             ret = -1;
        unsigned int    lrusize = 0;
        int             svcount = 0;

        if ((!nfs) || (!cl))
                return -1;

        lrusize = nfs->memfactor * GF_NFS_INODE_LRU_MULT;
        nfs->subvols = cl;
        gf_log (GF_NFS, GF_LOG_TRACE, "inode table lru: %d", lrusize);

        while (cl) {
                gf_log (GF_NFS, GF_LOG_DEBUG, "Initing subvolume: %s",
                        cl->xlator->name);
                ret = nfs_init_subvolume (nfs, cl->xlator);
                if (ret == -1) {
                        gf_log (GF_NFS, GF_LOG_CRITICAL, "Failed to init "
                                "xlator: %s", cl->xlator->name);
                        goto err;
                }
                ++svcount;
                cl = cl->next;
        }

        LOCK_INIT (&nfs->svinitlock);
        nfs->initedxl = GF_CALLOC (svcount, sizeof (xlator_t *), 
                                   gf_nfs_mt_xlator_t );
        if (!nfs->initedxl) {
                gf_log (GF_NFS, GF_LOG_ERROR, "Failed to allocated inited xls");
                ret = -1;
                goto err;
        }

        gf_log (GF_NFS, GF_LOG_TRACE, "Inited volumes: %d", svcount);
        nfs->allsubvols = svcount;
        ret = 0;
err:
        return ret;
}


int
nfs_user_root_create (nfs_user_t *newnfu)
{
        if (!newnfu)
                return -1;

        newnfu->uid = 0;
        newnfu->gids[0] = 0;
        newnfu->ngrps = 1;

        return 0;
}


int
nfs_user_create (nfs_user_t *newnfu, uid_t uid, gid_t gid, gid_t *auxgids,
                 int auxcount)
{
        int     x = 1;
        int     y = 0;

        /* We test for GF_REQUEST_MAXGROUPS instead of  NFS_FOP_NGROUPS because
         * the latter accounts for the @gid being in @auxgids, which is not the
         * case here.
         */
        if ((!newnfu) || (auxcount > GF_REQUEST_MAXGROUPS))
                return -1;

        newnfu->uid = uid;
        newnfu->gids[0] = gid;
        newnfu->ngrps = 1;

        gf_log (GF_NFS, GF_LOG_TRACE, "uid: %d, gid %d, gids: %d", uid, gid,
                auxcount);

        if (!auxgids)
                return 0;

        for (; y < auxcount; ++x,++y) {
                newnfu->gids[x] = auxgids[y];
                ++newnfu->ngrps;
                gf_log (GF_NFS, GF_LOG_TRACE, "gid: %d", auxgids[y]);
        }

        return 0;
}


void
nfs_request_user_init (nfs_user_t *nfu, rpcsvc_request_t *req)
{
        gid_t           *gidarr = NULL;
        int             gids = 0;

        if ((!req) || (!nfu))
                return;

        gidarr = nfs_rpcsvc_auth_unix_auxgids (req, &gids);
        nfs_user_create (nfu, nfs_rpcsvc_request_uid (req),
                         nfs_rpcsvc_request_gid (req), gidarr, gids);

        return;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_nfs_mt_end + 1);
        
        if (ret != 0) {
                gf_log(this->name, GF_LOG_ERROR, "Memory accounting init"
                                "failed");
                return ret;
        }

        return ret;
}


struct nfs_state *
nfs_init_state (xlator_t *this)
{
        struct nfs_state        *nfs = NULL;
        int                     ret = -1;
        unsigned int            fopspoolsize = 0;
        char                    *optstr = NULL;
        gf_boolean_t            boolt = _gf_false;

        if (!this)
                return NULL;

        if ((!this->children) || (!this->children->xlator)) {
                gf_log (GF_NFS, GF_LOG_ERROR, "nfs must have at least one"
                        " child subvolume");
                return NULL;
        }

        nfs = GF_CALLOC (1, sizeof (*nfs), gf_nfs_mt_nfs_state);
        if (!nfs) {
                gf_log (GF_NFS, GF_LOG_ERROR, "memory allocation failed");
                return NULL;
        }

        /* RPC service needs to be started before NFS versions can be
         * inited. */
        nfs->rpcsvc =  nfs_rpcsvc_init (this->ctx, this->options);
        if (!nfs->rpcsvc) {
                gf_log (GF_NFS, GF_LOG_ERROR, "RPC service init failed");
                goto free_nfs;
        }

        nfs->memfactor = GF_NFS_DEFAULT_MEMFACTOR;
        if (dict_get (this->options, "nfs.mem-factor")) {
                ret = dict_get_str (this->options, "nfs.mem-factor",
                                    &optstr);
                if (ret < 0) {
                        gf_log (GF_NFS, GF_LOG_ERROR, "Failed to parse dict");
                        goto free_foppool;
                }

                ret = gf_string2uint (optstr, &nfs->memfactor);
                if (ret < 0) {
                        gf_log (GF_NFS, GF_LOG_ERROR, "Failed to parse uint "
                                "string");
                        goto free_foppool;
                }
        }

        fopspoolsize = nfs->memfactor * GF_NFS_CONCURRENT_OPS_MULT;
        /* FIXME: Really saddens me to see this as xlator wide. */
        nfs->foppool = mem_pool_new (struct nfs_fop_local, fopspoolsize);
        if (!nfs->foppool) {
                gf_log (GF_NFS, GF_LOG_CRITICAL, "Failed to allocate fops "
                        "local pool");
                goto free_rpcsvc;
        }

        nfs->dynamicvolumes = GF_NFS_DVM_OFF;
        if (dict_get (this->options, "nfs.dynamic-volumes")) {
                ret = dict_get_str (this->options, "nfs.dynamic-volumes",
                                    &optstr);
                if (ret < 0) {
                        gf_log (GF_NFS, GF_LOG_ERROR, "Failed to parse dict");
                        goto free_foppool;
                }

                ret = gf_string2boolean (optstr, &boolt);
                if (ret < 0) {
                        gf_log (GF_NFS, GF_LOG_ERROR, "Failed to parse bool "
                                "string");
                        goto free_foppool;
                }

                if (boolt == _gf_true)
                        nfs->dynamicvolumes = GF_NFS_DVM_ON;
        }

        nfs->enable_ino32 = 0;
        if (dict_get (this->options, "nfs.enable-ino32")) {
                ret = dict_get_str (this->options, "nfs.enable-ino32",
                                    &optstr);
                if (ret < 0) {
                        gf_log (GF_NFS, GF_LOG_ERROR, "Failed to parse dict");
                        goto free_foppool;
                }

                ret = gf_string2boolean (optstr, &boolt);
                if (ret < 0) {
                        gf_log (GF_NFS, GF_LOG_ERROR, "Failed to parse bool "
                                "string");
                        goto free_foppool;
                }

                if (boolt == _gf_true)
                        nfs->enable_ino32 = 1;
        }

        nfs->override_portnum = 0;
        if (dict_get (this->options, "nfs.port")) {
                ret = dict_get_str (this->options, "nfs.port",
                                    &optstr);
                if (ret < 0) {
                        gf_log (GF_NFS, GF_LOG_ERROR, "Failed to parse dict");
                        goto free_foppool;
                }

                ret = gf_string2uint (optstr, &nfs->override_portnum);
                if (ret < 0) {
                        gf_log (GF_NFS, GF_LOG_ERROR, "Failed to parse uint "
                                "string");
                        goto free_foppool;
                }
        }

        this->private = (void *)nfs;
        INIT_LIST_HEAD (&nfs->versions);

        ret = 0;

free_foppool:
        if (ret < 0)
                mem_pool_destroy (nfs->foppool);

free_rpcsvc:
        /*
         * rpcsvc_deinit */
free_nfs:
        if (ret < 0) {
                GF_FREE (nfs);
                nfs = NULL;
        }

        return nfs;
}

int
validate_options (xlator_t *this, dict_t *options, char **op_errstr)
{
        char         *str=NULL;
        gf_boolean_t  nfs_ino32;
        data_t       *data = NULL;
        long long     lng = 0;
        int           ret = 0;



        ret = dict_get_str (options, "nfs.enable-ino32", 
                            &str);
        if (ret == 0) {
                ret = gf_string2boolean (str, 
                                         &nfs_ino32);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "'nfs.enable-ino32' takes only boolean"
                                " arguments");
                        *op_errstr = gf_strdup ("Error, should be boolean");
                        ret = -1;
                        goto out;
                }
        }

        data = dict_get (options, "nfs.mem-factor");
        if (data) {
                if (gf_string2longlong (data->data, &lng) != 0) {
                        gf_log (this->name, GF_LOG_ERROR, "invalid number format"
                                                        "\"%s\" in option "
                                                        "\"nfs.mem-factor\" ",
                                data->data );
                        *op_errstr = gf_strdup ("Error, Invalid number format");
                        ret = -1;
                        goto out;
                }
        }

        ret =0;
out:
                return ret;

}


int
init (xlator_t *this) {

        struct nfs_state        *nfs = NULL;
        int                     ret = -1;

        if (!this)
                return -1;

        nfs = nfs_init_state (this);
        if (!nfs) {
                gf_log (GF_NFS, GF_LOG_ERROR, "Failed to init nfs option");
                return -1;
        }

        ret = nfs_add_all_initiators (nfs);
        if (ret == -1) {
                gf_log (GF_NFS, GF_LOG_ERROR, "Failed to add initiators");
                goto err;
        }

        ret = nfs_init_subvolumes (nfs, this->children);
        if (ret == -1) {
                gf_log (GF_NFS, GF_LOG_CRITICAL, "Failed to init NFS "
                        "exports");
                goto err;
        }

        ret = nfs_init_versions (nfs, this);
        if (ret == -1) {
                gf_log (GF_NFS, GF_LOG_ERROR, "Failed to initialize "
                        "protocols");
                /* Do not return an error on this. If we dont return
                 * an error, the process keeps running and it helps
                 * to point out where the log is by doing ps ax|grep gluster.
                 */
                ret = 0;
                goto err;
        }

        gf_log (GF_NFS, GF_LOG_INFO, "NFS service started");
err:

        return ret;
}


int
notify (xlator_t *this, int32_t event, void *data, ...)
{
        struct nfs_state        *nfs = NULL;
        xlator_t                *subvol = NULL;

        nfs = (struct nfs_state *)this->private;
        subvol = (xlator_t *)data;

        gf_log (GF_NFS, GF_LOG_TRACE, "Notification received: %d",
                event);
        switch (event)
        {
                case GF_EVENT_CHILD_UP:
                {
                        nfs_startup_subvolume (this, subvol);
                        break;
                }

                case GF_EVENT_PARENT_UP:
                {
                        default_notify (this, GF_EVENT_PARENT_UP, data);
                        break;
                }
        }

        return 0;
}


int
fini (xlator_t *this)
{

        struct nfs_state        *nfs = NULL;

        nfs = (struct nfs_state *)this->private;
        gf_log (GF_NFS, GF_LOG_DEBUG, "NFS service going down");
        nfs_deinit_versions (&nfs->versions, this);
        return 0;
}

struct xlator_cbks cbks = { };
struct xlator_fops fops = { };

/* TODO: If needed, per-volume options below can be extended to be export
+ * specific also because after export-dir is introduced, a volume is not
+ * neccessarily an export whereas different subdirectories within that volume
+ * can be and may need these options to be specified separately.
+ */
struct volume_options options[] = {
        { .key  = {"nfs3.read-size"},
          .type = GF_OPTION_TYPE_SIZET,
          .description = "Size in which the client should issue read requests"
                         " to the Gluster NFSv3 server. Must be a multiple of"
                         " 4KiB."
        },
        { .key  = {"nfs3.write-size"},
          .type = GF_OPTION_TYPE_SIZET,
          .description = "Size in which the client should issue write requests"
                         " to the Gluster NFSv3 server. Must be a multiple of"
                         " 4KiB."
        },
        { .key  = {"nfs3.readdir-size"},
          .type = GF_OPTION_TYPE_SIZET,
          .description = "Size in which the client should issue directory "
                         " reading requests."
        },
        { .key  = {"nfs3.*.volume-access"},
          .type = GF_OPTION_TYPE_STR,
          .description = "Type of access desired for this subvolume: "
                         " read-only, read-write(default)"
        },
        { .key  = {"nfs3.*.trusted-write"},
          .type = GF_OPTION_TYPE_BOOL,
          .description = "On an UNSTABLE write from client, return STABLE flag"
                         " to force client to not send a COMMIT request. In "
                         "some environments, combined with a replicated "
                         "GlusterFS setup, this option can improve write "
                         "performance. This flag allows user to trust Gluster"
                         " replication logic to sync data to the disks and "
                         "recover when required. COMMIT requests if received "
                         "will be handled in a default manner by fsyncing."
                         " STABLE writes are still handled in a sync manner. "
                         "Off by default."

        },
        { .key  = {"nfs3.*.trusted-sync"},
          .type = GF_OPTION_TYPE_BOOL,
          .description = "All writes and COMMIT requests are treated as async."
                         " This implies that no write requests are guaranteed"
                         " to be on server disks when the write reply is "
                         "received at the NFS client. Trusted sync includes "
                         " trusted-write behaviour. Off by default."

        },
        { .key  = {"nfs3.*.export-dir"},
          .type = GF_OPTION_TYPE_STR,
          .description = "By default, all subvolumes of nfs are exported as "
                         "individual exports. There are cases where a "
                         "subdirectory or subdirectories in the volume need to "
                         "be exported separately. This option can also be used "
                         "in conjunction with nfs3.export-volumes option to "
                         "restrict exports only to the subdirectories specified"
                         " through this option. Must be an absolute path."
        },
        { .key  = {"nfs3.export-dirs"},
          .type = GF_OPTION_TYPE_BOOL,
          .description = "By default, all subvolumes of nfs are exported as "
                         "individual exports. There are cases where a "
                         "subdirectory or subdirectories in the volume need to "
                         "be exported separately. Enabling this option allows "
                         "any directory on a volumes to be exported separately."
                         " Directory exports are enabled by default."
        },
        { .key  = {"nfs3.export-volumes"},
          .type = GF_OPTION_TYPE_BOOL,
          .description = "Enable or disable exporting whole volumes, instead "
                         "if used in conjunction with nfs3.export-dir, can "
                         "allow setting up only subdirectories as exports. On "
                         "by default."
        },
        { .key  = {"rpc-auth.auth-unix"},
          .type = GF_OPTION_TYPE_BOOL,
          .description = "Disable or enable the AUTH_UNIX authentication type."
                         "Must always be enabled for better interoperability."
                         "However, can be disabled if needed. Enabled by"
                         "default"
        },
        { .key  = {"rpc-auth.auth-null"},
          .type = GF_OPTION_TYPE_BOOL,
          .description = "Disable or enable the AUTH_NULL authentication type."
                         "Must always be enabled. This option is here only to"
                         " avoid unrecognized option warnings"
        },
        { .key  = {"rpc-auth.auth-unix.*"},
          .type = GF_OPTION_TYPE_BOOL,
          .description = "Disable or enable the AUTH_UNIX authentication type "
                         "for a particular exported volume over-riding defaults"
                         " and general setting for AUTH_UNIX scheme. Must "
                         "always be enabled for better interoperability."
                         "However, can be disabled if needed. Enabled by"
                         "default."
        },
        { .key  = {"rpc-auth.auth-null.*"},
          .type = GF_OPTION_TYPE_BOOL,
          .description = "Disable or enable the AUTH_NULL authentication type "
                         "for a particular exported volume over-riding defaults"
                         " and general setting for AUTH_NULL. Must always be "
                         "enabled. This option is here only to avoid "
                         "unrecognized option warnings."
        },
        { .key  = {"rpc-auth.addr.allow"},
          .type = GF_OPTION_TYPE_STR,
          .description = "Allow a comma separated list of addresses and/or"
                         " hostnames to connect to the server. By default, all"
                         " connections are disallowed. This allows users to "
                         "define a general rule for all exported volumes."
        },
        { .key  = {"rpc-auth.addr.reject"},
          .type = GF_OPTION_TYPE_STR,
          .description = "Reject a comma separated list of addresses and/or"
                         " hostnames from connecting to the server. By default,"
                         " all connections are disallowed. This allows users to"
                         "define a general rule for all exported volumes."
        },
        { .key  = {"rpc-auth.addr.*.allow"},
          .type = GF_OPTION_TYPE_STR,
          .description = "Allow a comma separated list of addresses and/or"
                         " hostnames to connect to the server. By default, all"
                         " connections are disallowed. This allows users to "
                         "define a rule for a specific exported volume."
        },
        { .key  = {"rpc-auth.addr.*.reject"},
          .type = GF_OPTION_TYPE_STR,
          .description = "Reject a comma separated list of addresses and/or"
                         " hostnames from connecting to the server. By default,"
                         " all connections are disallowed. This allows users to"
                         "define a rule for a specific exported volume."
        },
        { .key  = {"rpc-auth.ports.insecure"},
          .type = GF_OPTION_TYPE_BOOL,
          .description = "Allow client connections from unprivileged ports. By "
                         "default only privileged ports are allowed. This is a"
                         "global setting in case insecure ports are to be "
                         "enabled for all exports using a single option."
        },
        { .key  = {"rpc-auth.ports.*.insecure"},
          .type = GF_OPTION_TYPE_BOOL,
          .description = "Allow client connections from unprivileged ports. By "
                         "default only privileged ports are allowed. Use this"
                         " option to set enable or disable insecure ports for "
                         "a specific subvolume and to over-ride global setting "
                         " set by the previous option."
        },
        { .key  = {"rpc-auth.addr.namelookup"},
          .type = GF_OPTION_TYPE_BOOL,
          .description = "Users have the option of turning off name lookup for"
                  " incoming client connections using this option. In some "
                  "setups, the name server can take too long to reply to DNS "
                  "queries resulting in timeouts of mount requests. Use this "
                  "option to turn off name lookups during address "
                  "authentication. Note, turning this off will prevent you from"
                  " using hostnames in rpc-auth.addr.* filters. By default, "
                  " name lookup is on."
        },
        { .key  = {"nfs.dynamic-volumes"},
          .type = GF_OPTION_TYPE_BOOL,
          .description = "Internal option set to tell gnfs to use a different"
                         " scheme for encoding file handles when DVM is being"
                         " used."
        },
        { .key  = {"nfs3.*.volume-id"},
          .type = GF_OPTION_TYPE_STR,
          .description = "When nfs.dynamic-volumes is set, gnfs expects every "
                         "subvolume to have this option set for it, so that "
                         "gnfs can use this option to identify the volume. "
                         "If all subvolumes do not have this option set, an "
                         "error is reported."
        },
        { .key  = {"nfs.enable-ino32"},
          .type = GF_OPTION_TYPE_BOOL,
          .description = "For nfs clients or apps that do not support 64-bit "
                         "inode numbers, use this option to make NFS return "
                         "32-bit inode numbers instead. Disabled by default so "
                         "NFS returns 64-bit inode numbers by default."
        },
        { .key  = {"rpc.register-with-portmap"},
          .type = GF_OPTION_TYPE_BOOL,
          .description = "For systems that need to run multiple nfs servers, we"
                         "need to prevent more than one from registering with "
                         "portmap service. Use this option to turn off portmap "
                         "registration for Gluster NFS. On by default"
        },
        { .key  = {"nfs.port"},
          .type = GF_OPTION_TYPE_INT,
          .description = "Use this option on systems that need Gluster NFS to "
                         "be associated with a non-default port number."
        },
        { .key  = {"nfs.mem-factor"},
          .type = GF_OPTION_TYPE_INT,
          .description = "Use this option to make NFS be faster on systems by "
                         "using more memory. This option specifies a multiple "
                         "that determines the total amount of memory used. "
                         "Default value is 15. Increase to use more memory in "
                         "order to improve performance for certain use cases."
                         "Please consult gluster-users list before using this "
                         "option."
        },
        { .key  = {NULL} },
};

