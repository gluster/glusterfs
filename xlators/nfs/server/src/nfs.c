/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* This is the primary translator source for NFS.
 * Every other protocol version gets initialized from here.
 */


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
#include "nfs3-helpers.h"
#include "nlm4.h"
#include "options.h"
#include "acl3.h"
#include "rpc-drc.h"
#include "syscall.h"
#include "rpcsvc.h"
#include "nfs-messages.h"

#define OPT_SERVER_AUX_GIDS             "nfs.server-aux-gids"
#define OPT_SERVER_GID_CACHE_TIMEOUT    "nfs.server.aux-gid-timeout"
#define OPT_SERVER_RPC_STATD             "nfs.rpc-statd"
#define OPT_SERVER_RPC_STATD_PIDFILE     "nfs.rpc-statd-pidfile"
#define OPT_SERVER_RPC_STATD_NOTIFY_PIDFILE "nfs.rpc-statd-notify-pidfile"

#define NFS_DATADIR                     GLUSTERD_DEFAULT_WORKDIR "/nfs"

/* Forward declaration */
static int nfs_add_initer (struct list_head *list, nfs_version_initer_t init,
                           gf_boolean_t required);

static int
nfs_init_version (xlator_t *this, nfs_version_initer_t init,
                  gf_boolean_t required)
{
        int                       ret       = -1;
        struct nfs_initer_list    *version  = NULL;
        struct nfs_initer_list    *tmp      = NULL;
        rpcsvc_program_t          *prog     = NULL;
        struct list_head          *versions = NULL;
        struct nfs_state          *nfs      = NULL;
        gf_boolean_t              found     = _gf_false;

        if ((!this) || (!this->private) || (!init))
                return (-1);

        nfs = (struct nfs_state *)this->private;

        ret = nfs_add_initer (&nfs->versions, init, required);
        if (ret == -1) {
                gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_PROT_INIT_ADD_FAIL,
                        "Failed to add protocol initializer");
                goto err;
        }

        versions = &nfs->versions;
        list_for_each_entry_safe (version, tmp, versions, list) {
                prog = version->program;
                if (version->init == init) {
                        prog = init(this);
                        if (!prog) {
                                ret = -1;
                                goto err;
                        }
                        version->program = prog;
                        found = _gf_true;
                        break;
                }
        }

        /* program not added */
        if (!found) {
                gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_PGM_NOT_FOUND,
                        "Program: %s NOT found", prog->progname);
                goto err;
        }

        /* Check if nfs.port is configured */
        if (nfs->override_portnum)
                prog->progport = nfs->override_portnum;

        gf_msg_debug (GF_NFS, 0, "Starting program: %s", prog->progname);

        ret = rpcsvc_program_register (nfs->rpcsvc, prog);
        if (ret == -1) {
                gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_PGM_INIT_FAIL,
                        "Program: %s init failed", prog->progname);
                goto err;
        }

        /* Registration with portmapper is disabled, Nothing to do */
        if (!nfs->register_portmap)
                goto err;

        ret = rpcsvc_program_register_portmap (prog, prog->progport);
        if (ret == -1) {
                gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_PGM_REG_FAIL,
                        "Program  %s registration failed", prog->progname);
                goto err;
        }
        ret = 0; /* All well */
err:
        return ret;
}

static int
nfs_deinit_version (struct nfs_state *nfs, nfs_version_initer_t init)
{
        int                       ret       = -1;
        struct nfs_initer_list    *version  = NULL;
        struct nfs_initer_list    *tmp      = NULL;
        rpcsvc_program_t          *prog     = NULL;
        struct list_head          *versions = NULL;

        if ((!nfs) || (!init))
                return (-1);

        versions = &nfs->versions;
        list_for_each_entry_safe (version, tmp, versions, list) {
                prog = version->program;
                if (version->init == init) {
                        prog = version->program;
                        ret = rpcsvc_program_unregister (nfs->rpcsvc, prog);
                        if (ret != 0)
                                return (-1);
                        list_del (&version->list);
                        GF_FREE (version);
                        return (0);
                }
        }

        return (-1);
}

static int
nfs_reconfigure_acl3 (xlator_t *this)
{
        struct nfs_state          *nfs     = NULL;

        if ((!this) || (!this->private))
                return (-1);

        nfs = (struct nfs_state *)this->private;

        /* ACL is enabled */
        if (nfs->enable_acl)
                return nfs_init_version (this, acl3svc_init, _gf_false);

        /* ACL is disabled */
        return nfs_deinit_version (nfs, acl3svc_init);
}

static int
nfs_reconfigure_nlm4 (xlator_t *this)
{
        struct nfs_state          *nfs     = NULL;

        if ((!this) || (!this->private))
                return (-1);

        nfs = (struct nfs_state *)this->private;

        /* NLM is enabled */
        if (nfs->enable_nlm)
                return nfs_init_version (this, nlm4svc_init, _gf_false);

        /* NLM is disabled */
        return nfs_deinit_version (nfs, nlm4svc_init);
}

static int
nfs_program_register_portmap_all (struct nfs_state *nfs)
{
        struct list_head                *versions = NULL;
        struct nfs_initer_list          *version = NULL;
        struct nfs_initer_list          *tmp = NULL;
        rpcsvc_program_t                *prog = NULL;

        if (nfs == NULL)
                return (-1);

        versions = &nfs->versions;
        list_for_each_entry_safe (version, tmp, versions, list) {
                prog = version->program;
                if (prog == NULL)
                        continue;
                if (nfs->override_portnum)
                        prog->progport = nfs->override_portnum;
                (void) rpcsvc_program_register_portmap (prog, prog->progport);
        }

        return (0);
}

static int
nfs_program_unregister_portmap_all (struct nfs_state *nfs)
{
        struct list_head                *versions = NULL;
        struct nfs_initer_list          *version = NULL;
        struct nfs_initer_list          *tmp = NULL;
        rpcsvc_program_t                *prog = NULL;

        if (nfs == NULL)
                return (-1);

        versions = &nfs->versions;
        list_for_each_entry_safe (version, tmp, versions, list) {
                prog = version->program;
                if (prog == NULL)
                        continue;
                (void) rpcsvc_program_unregister_portmap (prog);
        }

        return (0);
}

/* Every NFS version must call this function with the init function
 * for its particular version.
 */
static int
nfs_add_initer (struct list_head *list, nfs_version_initer_t init,
                gf_boolean_t required)
{
        struct nfs_initer_list  *new = NULL;
        if ((!list) || (!init))
                return -1;

        new = GF_CALLOC (1, sizeof (*new), gf_nfs_mt_nfs_initer_list);
        if (!new) {
                gf_msg (GF_NFS, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Memory allocation failed");
                return -1;
        }

        new->init = init;
        new->required = required;
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
                        rpcsvc_program_unregister (nfs->rpcsvc,
                                                   (version->program));

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

        gf_msg_debug (GF_NFS, 0, "Initing protocol versions");
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

                version->program = prog;
                if (nfs->override_portnum)
                        prog->progport = nfs->override_portnum;
                gf_msg_debug (GF_NFS, 0, "Starting program: %s",
                        prog->progname);

                ret = rpcsvc_program_register (nfs->rpcsvc, prog);
                if (ret == -1) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, 0,
                                NFS_MSG_PGM_INIT_FAIL,
                                "Program: %s init failed", prog->progname);
                        goto err;
                }
                if (nfs->register_portmap) {
                        ret = rpcsvc_program_register_portmap (prog,
                                                               prog->progport);
                        if (ret == -1) {
                                gf_msg (GF_NFS, GF_LOG_ERROR, 0,
                                        NFS_MSG_PGM_REG_FAIL,
                                        "%s program  %s registration failed",
                                        version->required ?
                                        "Required" : "Optional",
                                        prog->progname);

                                /* fatal error if the program is required */
                                if (version->required)
                                        goto err;
                        }
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
        ret = nfs_add_initer (&nfs->versions, mnt3svc_init, _gf_true);
        if (ret == -1) {
                gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_PROT_INIT_ADD_FAIL,
                        "Failed to add MOUNT3 protocol initializer");
                goto ret;
        }

        ret = nfs_add_initer (&nfs->versions, mnt1svc_init, _gf_true);
        if (ret == -1) {
                gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_PROT_INIT_ADD_FAIL,
                        "Failed to add MOUNT1 protocol initializer");
                goto ret;
        }

        ret = nfs_add_initer (&nfs->versions, nfs3svc_init, _gf_true);
        if (ret == -1) {
                gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_PROT_INIT_ADD_FAIL,
                        "Failed to add NFS3 protocol initializer");
                goto ret;
        }

        if (nfs->enable_nlm == _gf_true) {
                ret = nfs_add_initer (&nfs->versions, nlm4svc_init, _gf_false);
                if (ret == -1) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, 0,
                                NFS_MSG_PROT_INIT_ADD_FAIL,
                                "Failed to add protocol initializer");
                        goto ret;
                }
        }

        if (nfs->enable_acl == _gf_true) {
                ret = nfs_add_initer (&nfs->versions, acl3svc_init, _gf_false);
                if (ret == -1) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, 0,
                                NFS_MSG_PROT_INIT_ADD_FAIL,
                                "Failed to add ACL protocol initializer");
                        goto ret;
                }
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
                        if (nfs->initedxl[x] == xl) {
                                gf_msg_debug (GF_NFS, 0,
                                        "Volume already started %s",
                                        xl->name);
                                break;
                        }

                        if (nfs->initedxl[x] == NULL) {
                                nfs->initedxl[x] = xl;
                                ++nfs->upsubvols;
                                gf_msg_debug (GF_NFS, 0, "Starting up: %s "
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
                gf_msg (GF_NFS, GF_LOG_CRITICAL, op_errno,
                        NFS_MSG_LOOKUP_ROOT_FAIL,
                        "Failed to lookup root: %s",
                        strerror (op_errno));
                goto err;
        }

        nfs_subvolume_set_started (this->private, ((xlator_t *)cookie));
        gf_msg_trace (GF_NFS, 0, "Started %s", ((xlator_t *)cookie)->name);
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
                gf_msg_trace (GF_NFS, 0, "Subvolume already started: %s",
                        xl->name);
                ret = 0;
                goto err;
        }

        ret = nfs_root_loc_fill (xl->itable, &rootloc);
        if (ret == -1) {
                gf_msg (GF_NFS, GF_LOG_CRITICAL, 0,
                        NFS_MSG_ROOT_LOC_INIT_FAIL,
                        "Failed to init root loc");
                goto err;
        }

        nfs_user_root_create (&nfu);
        ret = nfs_fop_lookup (nfsx, xl, &nfu, &rootloc,
                              nfs_start_subvol_lookup_cbk,
                              (void *)nfsx->private);
        if (ret < 0) {
                gf_msg (GF_NFS, GF_LOG_CRITICAL, -ret,
                        NFS_MSG_LOOKUP_ROOT_FAIL,
                        "Failed to lookup root: %s", strerror (-ret));
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
                gf_msg_debug (GF_NFS, 0, "Starting subvolume: %s",
                        cl->xlator->name);
                ret = nfs_startup_subvolume (nfsx, cl->xlator);
                if (ret == -1) {
                        gf_msg (GF_NFS, GF_LOG_CRITICAL, 0,
                                NFS_MSG_STARTUP_FAIL,
                                "Failed to start-up xlator: %s",
                                cl->xlator->name);
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
                gf_msg (GF_NFS, GF_LOG_CRITICAL, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to allocate inode table");
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
        gf_msg_trace (GF_NFS, 0, "inode table lru: %d", lrusize);

        while (cl) {
                gf_msg_debug (GF_NFS, 0, "Initing subvolume: %s",
                        cl->xlator->name);
                ret = nfs_init_subvolume (nfs, cl->xlator);
                if (ret == -1) {
                        gf_msg (GF_NFS, GF_LOG_CRITICAL, 0,
                                NFS_MSG_XLATOR_INIT_FAIL, "Failed to init "
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
                gf_msg (GF_NFS, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to allocated inited xls");
                ret = -1;
                goto err;
        }

        gf_msg_trace (GF_NFS, 0, "Inited volumes: %d", svcount);
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
nfs_user_create (nfs_user_t *newnfu, uid_t uid, gid_t gid,
                 rpc_transport_t *trans, gid_t *auxgids, int auxcount)
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
        if (trans) {
                memcpy (&newnfu->identifier, trans->peerinfo.identifier,
                       UNIX_PATH_MAX);
        }

        gf_msg_trace (GF_NFS, 0, "uid: %d, gid %d, gids: %d", uid, gid,
                auxcount);

        if (!auxgids)
                return 0;

        for (; y < auxcount; ++x,++y) {
                newnfu->gids[x] = auxgids[y];
                ++newnfu->ngrps;
                gf_msg_trace (GF_NFS, 0, "gid: %d", auxgids[y]);
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

        gidarr = rpcsvc_auth_unix_auxgids (req, &gids);
        nfs_user_create (nfu, rpcsvc_request_uid (req),
                         rpcsvc_request_gid (req),
                         rpcsvc_request_transport (req),
                         gidarr, gids);

        return;
}

void
nfs_request_primary_user_init (nfs_user_t *nfu, rpcsvc_request_t *req,
                               uid_t uid, gid_t gid)
{
        gid_t           *gidarr = NULL;
        int             gids = 0;

        if ((!req) || (!nfu))
                return;

        gidarr = rpcsvc_auth_unix_auxgids (req, &gids);
        nfs_user_create (nfu, uid, gid, rpcsvc_request_transport (req),
                         gidarr, gids);

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
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Memory accounting init failed");
                return ret;
        }

        return ret;
}


struct nfs_state *
nfs_init_state (xlator_t *this)
{
        struct nfs_state        *nfs = NULL;
        int                     i = 0, ret = -1;
        unsigned int            fopspoolsize = 0;
        char                    *optstr = NULL;
        gf_boolean_t            boolt = _gf_false;
        struct stat             stbuf = {0,};

        if (!this)
                return NULL;

        if (!this->children) {
                gf_msg (GF_NFS, GF_LOG_INFO, 0, NFS_MSG_NFS_MAN_DISABLE,
                        "NFS is manually disabled: Exiting");
                /* Nothing for nfs process to do, exit cleanly */
                kill (getpid (), SIGTERM);
        }

        nfs = GF_CALLOC (1, sizeof (*nfs), gf_nfs_mt_nfs_state);
        if (!nfs) {
                gf_msg (GF_NFS, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "memory allocation failed");
                return NULL;
        }

        nfs->memfactor = GF_NFS_DEFAULT_MEMFACTOR;
        if (dict_get (this->options, "nfs.mem-factor")) {
                ret = dict_get_str (this->options, "nfs.mem-factor",
                                    &optstr);
                if (ret < 0) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, -ret,
                                NFS_MSG_DICT_GET_FAILED,
                                "Failed to parse dict");
                        goto free_rpcsvc;
                }

                ret = gf_string2uint (optstr, &nfs->memfactor);
                if (ret < 0) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, errno, NFS_MSG_PARSE_FAIL,
                                "Failed to parse uint string");
                        goto free_rpcsvc;
                }
        }

        fopspoolsize = nfs->memfactor * GF_NFS_CONCURRENT_OPS_MULT;
        /* FIXME: Really saddens me to see this as xlator wide. */
        nfs->foppool = mem_pool_new (struct nfs_fop_local, fopspoolsize);
        if (!nfs->foppool) {
                gf_msg (GF_NFS, GF_LOG_CRITICAL, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to allocate fops local pool");
                goto free_rpcsvc;
        }

        nfs->dynamicvolumes = GF_NFS_DVM_OFF;
        if (dict_get (this->options, "nfs.dynamic-volumes")) {
                ret = dict_get_str (this->options, "nfs.dynamic-volumes",
                                    &optstr);
                if (ret < 0) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, -ret,
                                NFS_MSG_DICT_GET_FAILED,
                                "Failed to parse dict");
                        goto free_foppool;
                }

                ret = gf_string2boolean (optstr, &boolt);
                if (ret < 0) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, errno, NFS_MSG_PARSE_FAIL,
                                "Failed to parse bool string");
                        goto free_foppool;
                }

                if (boolt == _gf_true)
                        nfs->dynamicvolumes = GF_NFS_DVM_ON;
        }

        nfs->enable_nlm = _gf_true;
        ret = dict_get_str_boolean (this->options, "nfs.nlm", _gf_true);
        if (ret == _gf_false) {
                gf_msg (GF_NFS, GF_LOG_INFO, 0, NFS_MSG_NLM_MAN_DISABLE,
                        "NLM is manually disabled");
                nfs->enable_nlm = _gf_false;
        }

        nfs->enable_acl = _gf_true;
        ret = dict_get_str_boolean (this->options, "nfs.acl", _gf_true);
        if (ret == _gf_false) {
                gf_msg (GF_NFS, GF_LOG_INFO, 0, NFS_MSG_ACL_MAN_DISABLE,
                        "ACL is manually disabled");
                nfs->enable_acl = _gf_false;
        }

        nfs->enable_ino32 = 0;
        if (dict_get (this->options, "nfs.enable-ino32")) {
                ret = dict_get_str (this->options, "nfs.enable-ino32",
                                    &optstr);
                if (ret < 0) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, -ret, NFS_MSG_PARSE_FAIL,
                                "Failed to parse dict");
                        goto free_foppool;
                }

                ret = gf_string2boolean (optstr, &boolt);
                if (ret < 0) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, errno, NFS_MSG_PARSE_FAIL,
                                "Failed to parse bool string");
                        goto free_foppool;
                }

                if (boolt == _gf_true)
                        nfs->enable_ino32 = 1;
        }

        if (dict_get (this->options, "nfs.port")) {
                ret = dict_get_str (this->options, "nfs.port",
                                    &optstr);
                if (ret < 0) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, -ret, NFS_MSG_PARSE_FAIL,
                                "Failed to parse dict");
                        goto free_foppool;
                }

                ret = gf_string2uint (optstr, &nfs->override_portnum);
                if (ret < 0) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, errno, NFS_MSG_PARSE_FAIL,
                                "Failed to parse uint string");
                        goto free_foppool;
                }
        }

        if (dict_get (this->options, "transport.socket.bind-address")) {
                ret = dict_get_str (this->options,
                        "transport.socket.bind-address",
                        &optstr);
                if (ret < 0) {
                        gf_log (GF_NFS, GF_LOG_ERROR, "Failed to parse "
                                "transport.socket.bind-address string");
                } else {
                        this->instance_name = gf_strdup (optstr);
                        for (i = 0; i < strlen (this->instance_name); i++) {
                                if (this->instance_name[i] == '.' ||
                                    this->instance_name[i] == ':')
                                        this->instance_name[i] = '_';
                        }
                }
        }

        if (dict_get(this->options, "transport.socket.listen-port") == NULL) {
                if (nfs->override_portnum)
                        ret = gf_asprintf (&optstr, "%d",
                                           nfs->override_portnum);
                else
                        ret = gf_asprintf (&optstr, "%d", GF_NFS3_PORT);
                if (ret == -1) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                                "failed mem-allocation");
                        goto free_foppool;
                }
                ret = dict_set_dynstr (this->options,
                                       "transport.socket.listen-port", optstr);
                if (ret == -1) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, 0,
                                NFS_MSG_DICT_SET_FAILED,
                                "dict_set_dynstr error");
                        goto free_foppool;
                }
        }

        /* Right only socket support exists between nfs client and
         * gluster nfs, so we can set default value as socket
         */
        ret = dict_set_str (this->options, "transport-type", "socket");
        if (ret == -1) {
                gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_DICT_SET_FAILED,
                        "dict_set_str error");
                goto free_foppool;
        }

        nfs->mount_udp = 0;
        if (dict_get(this->options, "nfs.mount-udp")) {
                ret = dict_get_str (this->options, "nfs.mount-udp", &optstr);
                if (ret == -1) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, -ret, NFS_MSG_PARSE_FAIL,
                                "Failed to parse dict");
                        goto free_foppool;
                }

                ret = gf_string2boolean (optstr, &boolt);
                if (ret < 0) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, errno, NFS_MSG_PARSE_FAIL,
                                "Failed to parse bool "
                                "string");
                        goto free_foppool;
                }

                if (boolt == _gf_true)
                        nfs->mount_udp = 1;
        }

        nfs->exports_auth = GF_NFS_DEFAULT_EXPORT_AUTH;
        if (dict_get(this->options, "nfs.exports-auth-enable")) {
                ret = dict_get_str (this->options, "nfs.exports-auth-enable",
                                    &optstr);
                if (ret == -1) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, -ret, NFS_MSG_PARSE_FAIL,
                                "Failed to parse dict");
                        goto free_foppool;
                }

                ret = gf_string2boolean (optstr, &boolt);
                if (ret < 0) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, errno, NFS_MSG_PARSE_FAIL,
                                "Failed to parse bool string");
                        goto free_foppool;
                }

                if (boolt == _gf_true)
                        nfs->exports_auth = 1;
        }

        nfs->auth_refresh_time_secs = GF_NFS_DEFAULT_AUTH_REFRESH_INTERVAL_SEC;
        if (dict_get (this->options, "nfs.auth-refresh-interval-sec")) {
                ret = dict_get_str (this->options,
                                    "nfs.auth-refresh-interval-sec", &optstr);
                if (ret < 0) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, -ret, NFS_MSG_PARSE_FAIL,
                                "Failed to parse dict");
                        goto free_foppool;
                }

                ret = gf_string2uint (optstr, &nfs->auth_refresh_time_secs);
                if (ret < 0) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, errno, NFS_MSG_PARSE_FAIL,
                                "Failed to parse uint string");
                        goto free_foppool;
                }
        }

        nfs->auth_cache_ttl_sec = GF_NFS_DEFAULT_AUTH_CACHE_TTL_SEC;
        if (dict_get (this->options, "nfs.auth-cache-ttl-sec")) {
                ret = dict_get_str (this->options,
                                    "nfs.auth-cache-ttl-sec", &optstr);
                if (ret < 0) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, -ret, NFS_MSG_PARSE_FAIL,
                                "Failed to parse dict");
                        goto free_foppool;
                }

                ret = gf_string2uint (optstr, &nfs->auth_cache_ttl_sec);
                if (ret < 0) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, errno, NFS_MSG_PARSE_FAIL,
                                "Failed to parse uint string");
                        goto free_foppool;
                }
        }

        /* TODO: Make this a configurable option in case we don't want to read
         * exports/netgroup files off disk when they change. */
        nfs->refresh_auth = 1;

        nfs->rmtab = gf_strdup (NFS_DATADIR "/rmtab");
        if (dict_get(this->options, "nfs.mount-rmtab")) {
                ret = dict_get_str (this->options, "nfs.mount-rmtab", &nfs->rmtab);
                if (ret == -1) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, -ret, NFS_MSG_PARSE_FAIL,
                                "Failed to parse dict");
                        goto free_foppool;
                }

                /* check if writing the rmtab is disabled*/
                if (nfs->rmtab && strcmp ("/-", nfs->rmtab) == 0) {
                        GF_FREE (nfs->rmtab);
                        nfs->rmtab = NULL;
                }
        }

        /* support both options rpc-auth.ports.insecure and
         * rpc-auth-allow-insecure for backward compatibility
         */
        nfs->allow_insecure = 1;
        if (dict_get(this->options, "rpc-auth.ports.insecure")) {
                ret = dict_get_str (this->options, "rpc-auth.ports.insecure",
                                    &optstr);
                if (ret < 0) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, -ret, NFS_MSG_PARSE_FAIL,
                                "Failed to parse dict");
                        goto free_foppool;
                }

                ret = gf_string2boolean (optstr, &boolt);
                if (ret < 0) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, errno, NFS_MSG_PARSE_FAIL,
                                "Failed to parse bool "
                                "string");
                        goto free_foppool;
                }

                if (boolt == _gf_false)
                        nfs->allow_insecure = 0;
        }

        if (dict_get(this->options, "rpc-auth-allow-insecure")) {
                ret = dict_get_str (this->options, "rpc-auth-allow-insecure",
                                    &optstr);
                if (ret < 0) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, -ret, NFS_MSG_PARSE_FAIL,
                                "Failed to parse dict");
                        goto free_foppool;
                }

                ret = gf_string2boolean (optstr, &boolt);
                if (ret < 0) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, errno, NFS_MSG_PARSE_FAIL,
                                "Failed to parse bool string");
                        goto free_foppool;
                }

                if (boolt == _gf_false)
                        nfs->allow_insecure = 0;
        }

        if (nfs->allow_insecure) {
                /* blindly set both the options */
                dict_del (this->options, "rpc-auth-allow-insecure");
                ret = dict_set_str (this->options,
                                    "rpc-auth-allow-insecure", "on");
                if (ret == -1) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, 0,
                                NFS_MSG_DICT_SET_FAILED, "dict_set_str error");
                        goto free_foppool;
                }
                dict_del (this->options, "rpc-auth.ports.insecure");
                ret = dict_set_str (this->options,
                                    "rpc-auth.ports.insecure", "on");
                if (ret == -1) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, 0,
                                NFS_MSG_DICT_SET_FAILED, "dict_set_str error");
                        goto free_foppool;
                }
        }

        GF_OPTION_INIT ("nfs.rdirplus", nfs->rdirplus, bool, free_foppool);

	GF_OPTION_INIT (OPT_SERVER_RPC_STATD, nfs->rpc_statd, path, free_foppool);

	GF_OPTION_INIT (OPT_SERVER_RPC_STATD_PIDFILE, nfs->rpc_statd_pid_file, path, free_foppool);

        GF_OPTION_INIT (OPT_SERVER_AUX_GIDS, nfs->server_aux_gids,
                        bool, free_foppool);
        GF_OPTION_INIT (OPT_SERVER_GID_CACHE_TIMEOUT,
                        nfs->server_aux_gids_max_age,
                        uint32, free_foppool);

        if (gid_cache_init(&nfs->gid_cache, nfs->server_aux_gids_max_age) < 0) {
                gf_msg (GF_NFS, GF_LOG_ERROR, 0,
                        NFS_MSG_INIT_GRP_CACHE_FAIL,
                        "Failed to initialize group cache.");
                goto free_foppool;
        }

        ret = sys_access (nfs->rpc_statd, X_OK);
        if (ret) {
                gf_msg (GF_NFS, GF_LOG_WARNING, EPERM, NFS_MSG_NO_PERM,
                        "%s not enough permissions to access. Disabling NLM",
                        nfs->rpc_statd);
                nfs->enable_nlm = _gf_false;
        }

        ret = sys_stat (nfs->rpc_statd, &stbuf);
        if (ret || !S_ISREG (stbuf.st_mode)) {
                gf_msg (GF_NFS, GF_LOG_WARNING, 0, NFS_MSG_REG_FILE_ERROR,
                        "%s not a regular file. Disabling NLM", nfs->rpc_statd);
                nfs->enable_nlm = _gf_false;
        }

        nfs->rpcsvc =  rpcsvc_init (this, this->ctx,
                                    this->options, fopspoolsize);
        if (!nfs->rpcsvc) {
                ret = -1;
                gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_RPC_INIT_FAIL,
                        "RPC service init failed");
                goto free_foppool;
        }

        ret = rpcsvc_set_throttle_on (nfs->rpcsvc);
        if (ret) {
                gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_ENABLE_THROTTLE_FAIL,
                        "Enabling throttle failed");
                goto free_foppool;
        }

        ret = rpcsvc_set_outstanding_rpc_limit (nfs->rpcsvc,
                                                this->options,
                                                RPCSVC_DEF_NFS_OUTSTANDING_RPC_LIMIT);
        if (ret < 0) {
                gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_RPC_CONFIG_FAIL,
                        "Failed to configure outstanding-rpc-limit");
                goto free_foppool;
        }

        nfs->register_portmap = rpcsvc_register_portmap_enabled (nfs->rpcsvc);

        this->private = (void *)nfs;
        INIT_LIST_HEAD (&nfs->versions);
        nfs->generation = 1965;

        ret = 0;

free_foppool:
        if (ret < 0)
                mem_pool_destroy (nfs->foppool);

free_rpcsvc:
        /*
         * rpcsvc_deinit */
        if (ret < 0) {
                GF_FREE (nfs);
                nfs = NULL;
        }

        return nfs;
}

int
nfs_drc_init (xlator_t *this)
{
        int       ret     = -1;
        rpcsvc_t *svc     = NULL;

        GF_VALIDATE_OR_GOTO (GF_NFS, this, out);
        GF_VALIDATE_OR_GOTO (GF_NFS, this->private, out);

        svc = ((struct nfs_state *)(this->private))->rpcsvc;
        if (!svc)
                goto out;

        ret = rpcsvc_drc_init (svc, this->options);

 out:
        return ret;
}

int
nfs_reconfigure_state (xlator_t *this, dict_t *options)
{
        int                 ret = 0;
        int                 keyindx = 0;
        char                *rmtab = NULL;
        char                *rpc_statd = NULL;
        gf_boolean_t        optbool;
        uint32_t            optuint32;
        struct nfs_state    *nfs = NULL;
        char                *blacklist_keys[] = {
                                        "nfs.port",
                                        "nfs.transport-type",
                                        "nfs.mem-factor",
                                        NULL};

        GF_VALIDATE_OR_GOTO (GF_NFS, this, out);
        GF_VALIDATE_OR_GOTO (GF_NFS, this->private, out);
        GF_VALIDATE_OR_GOTO (GF_NFS, options, out);

        nfs = (struct nfs_state *)this->private;

        /* Black listed options can't be reconfigured, they need
         * NFS to be restarted. There are two cases 1. SET 2. UNSET.
         * 1. SET */
        while (blacklist_keys[keyindx]) {
                if (dict_get (options, blacklist_keys[keyindx])) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, 0,
                                NFS_MSG_RECONFIG_FAIL,
                                "Reconfiguring %s needs NFS restart",
                                blacklist_keys[keyindx]);
                        goto out;
                }
                keyindx ++;
        }

        /* UNSET for nfs.mem-factor */
        if ((!dict_get (options, "nfs.mem-factor")) &&
            (nfs->memfactor != GF_NFS_DEFAULT_MEMFACTOR)) {
                gf_msg (GF_NFS, GF_LOG_INFO, 0, NFS_MSG_RECONFIG_FAIL,
                        "Reconfiguring nfs.mem-factor needs NFS restart");
                goto out;
        }

        /* UNSET for nfs.port */
        if ((!dict_get (options, "nfs.port")) &&
            (nfs->override_portnum)) {
                gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_RECONFIG_FAIL,
                        "Reconfiguring nfs.port needs NFS restart");
                goto out;
        }

        /* reconfig nfs.rpc-statd...  */
        rpc_statd = GF_RPC_STATD_PROG;
        if (dict_get (options, OPT_SERVER_RPC_STATD_PIDFILE)) {
                ret = dict_get_str (options, "nfs.rpc-statd", &rpc_statd);
                if (ret < 0) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_READ_FAIL,
                                "Failed to read reconfigured option: "
                                "nfs.rpc-statd");
                        goto out;
                }
        }

        if (strcmp(nfs->rpc_statd, rpc_statd) != 0) {
                gf_msg (GF_NFS, GF_LOG_INFO, 0, NFS_MSG_RECONFIG_FAIL,
                        "Reconfiguring nfs.rpc-statd needs NFS restart");
                goto out;
        }

        /* reconfig nfs.mount-rmtab */
        rmtab = NFS_DATADIR "/rmtab";
        if (dict_get (options, "nfs.mount-rmtab")) {
                ret = dict_get_str (options, "nfs.mount-rmtab", &rmtab);
                if (ret < 0) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_READ_FAIL,
                                "Failed to read reconfigured option:"
                                " nfs.mount-rmtab");
                        goto out;
                }
                gf_path_strip_trailing_slashes (rmtab);
        }
        /* check if writing the rmtab is disabled*/
        if (strcmp ("/-", rmtab) == 0) {
                GF_FREE (nfs->rmtab);
                nfs->rmtab = NULL;
                gf_msg (GF_NFS, GF_LOG_INFO, 0, NFS_MSG_WRITE_FAIL,
                        "Disabled writing of nfs.mount-rmtab");
        } else if (!nfs->rmtab || strcmp (nfs->rmtab, rmtab) != 0) {
                mount_rewrite_rmtab (nfs->mstate, rmtab);
                gf_msg (GF_NFS, GF_LOG_INFO, 0, NFS_MSG_RECONFIG_PATH,
                        "Reconfigured nfs.mount-rmtab path: %s", nfs->rmtab);
        }

        GF_OPTION_RECONF (OPT_SERVER_AUX_GIDS, optbool,
                                               options, bool, out);
        if (nfs->server_aux_gids != optbool) {
                nfs->server_aux_gids = optbool;
                gf_msg (GF_NFS, GF_LOG_INFO, 0, NFS_MSG_RECONFIG_VALUE,
                        "Reconfigured %s with value %d", OPT_SERVER_AUX_GIDS,
                        optbool);
        }

        GF_OPTION_RECONF (OPT_SERVER_GID_CACHE_TIMEOUT, optuint32,
                                               options, uint32, out);
        if (nfs->server_aux_gids_max_age != optuint32) {
                nfs->server_aux_gids_max_age = optuint32;
                gid_cache_reconf (&nfs->gid_cache, optuint32);
                gf_msg (GF_NFS, GF_LOG_INFO, 0, NFS_MSG_RECONFIG_VALUE,
                        "Reconfigured %s with value %d",
                        OPT_SERVER_GID_CACHE_TIMEOUT, optuint32);
        }

        GF_OPTION_RECONF ("nfs.rdirplus", optbool,
                                          options, bool, out);
        if (nfs->rdirplus != optbool) {
                nfs->rdirplus = optbool;
                gf_msg (GF_NFS, GF_LOG_INFO, 0, NFS_MSG_RECONFIG_VALUE,
                        "Reconfigured nfs.rdirplus with value %d", optbool);
        }

        /* reconfig nfs.dynamic-volumes */
        ret = dict_get_str_boolean (options, "nfs.dynamic-volumes",
                                             GF_NFS_DVM_OFF);
        switch (ret) {
        case GF_NFS_DVM_ON:
        case GF_NFS_DVM_OFF:
                optbool = ret;
                break;
        default:
                optbool = GF_NFS_DVM_OFF;
                break;
        }
        if (nfs->dynamicvolumes != optbool) {
                nfs->dynamicvolumes = optbool;
                gf_msg (GF_NFS, GF_LOG_INFO, 0, NFS_MSG_RECONFIG_VOL,
                        "Reconfigured nfs.dynamic-volumes with value %d",
                        optbool);
        }

        optbool = _gf_false;
        if (dict_get (options, "nfs.enable-ino32")) {
                ret = dict_get_str_boolean (options, "nfs.enable-ino32",
                                                      _gf_false);
                if (ret < 0) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_READ_FAIL,
                                "Failed to read reconfigured option: "
                                "nfs.enable-ino32");
                        goto out;
                }
                optbool = ret;
        }
        if (nfs->enable_ino32 != optbool) {
                nfs->enable_ino32 = optbool;
                gf_msg (GF_NFS, GF_LOG_INFO, 0, NFS_MSG_RECONFIG_ENABLE,
                        "Reconfigured nfs.enable-ino32 with value %d", optbool);
        }

        /* nfs.nlm is enabled by default */
        ret = dict_get_str_boolean (options, "nfs.nlm", _gf_true);
        if (ret < 0) {
                optbool = _gf_true;
        } else {
                optbool = ret;
        }
        if (nfs->enable_nlm != optbool) {
                gf_msg (GF_NFS, GF_LOG_INFO, 0, NFS_MSG_NLM_INFO, "NLM is"
                        " manually %s", (optbool ? "enabled":"disabled"));
                nfs->enable_nlm = optbool;
                nfs_reconfigure_nlm4 (this);
        }

        /* nfs.acl is enabled by default */
        ret = dict_get_str_boolean (options, "nfs.acl", _gf_true);
        if (ret < 0) {
                optbool = _gf_true;
        } else {
                optbool = ret;
        }
        if (nfs->enable_acl != optbool) {
                gf_msg (GF_NFS, GF_LOG_INFO, 0, NFS_MSG_ACL_INFO, "ACL is "
                        "manually %s", (optbool ? "enabled":"disabled"));
                nfs->enable_acl = optbool;
                nfs_reconfigure_acl3 (this);
        }

        ret = 0;
out:
        return ret;
}


/*
 * reconfigure() for NFS server xlator.
 */
int
reconfigure (xlator_t *this, dict_t *options)
{
        int                      ret = 0;
        struct nfs_state         *nfs = NULL;
        gf_boolean_t             regpmap = _gf_true;

        if ((!this) || (!this->private) || (!options))
                return (-1);

        nfs = (struct nfs_state *)this->private;

        /* Reconfigure nfs options */
        ret = nfs_reconfigure_state(this, options);
        if (ret) {
                gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_RECONFIG_FAIL,
                        "nfs reconfigure state failed");
                return (-1);
        }

        /* Reconfigure nfs3 options */
        ret = nfs3_reconfigure_state(this, options);
        if (ret) {
                gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_RECONFIG_FAIL,
                        "nfs3 reconfigure state failed");
                return (-1);
        }

        /* Reconfigure mount options */
        ret = mount_reconfigure_state(this, options);
        if (ret) {
                gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_RECONFIG_FAIL,
                        "mount reconfigure state failed");
                return (-1);
        }

        /* Reconfigure rpc layer */
        ret = rpcsvc_reconfigure_options (nfs->rpcsvc, options);
        if (ret) {
                gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_RECONFIG_FAIL,
                        "rpcsvc reconfigure options failed");
                return (-1);
        }

        /* Reconfigure rpc.outstanding-rpc-limit */
        ret = rpcsvc_set_outstanding_rpc_limit (nfs->rpcsvc,
                                       options,
                                       RPCSVC_DEF_NFS_OUTSTANDING_RPC_LIMIT);
        if (ret < 0) {
                gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_RECONFIG_FAIL,
                        "Failed to reconfigure outstanding-rpc-limit");
                return (-1);
        }

        regpmap = rpcsvc_register_portmap_enabled(nfs->rpcsvc);
        if (nfs->register_portmap != regpmap) {
                nfs->register_portmap = regpmap;
                if (regpmap) {
                        (void) nfs_program_register_portmap_all (nfs);
                } else {
                        (void) nfs_program_unregister_portmap_all (nfs);
                }
        }

        /* Reconfigure drc */
        ret = rpcsvc_drc_reconfigure (nfs->rpcsvc, options);
        if (ret) {
                gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_RECONFIG_FAIL,
                        "rpcsvc DRC reconfigure failed");
                return (-1);
        }

        return (0);
}

/* Main init() routine for NFS server xlator. It inits NFS v3 protocol
 * and its dependent protocols e.g. ACL, MOUNT v3 (mount3), NLM and
 * DRC.
 *
 * Usage: glusterfsd:
 *            glusterfs_process_volfp() =>
 *              glusterfs_graph_activate() =>
 *                glusterfs_graph_init() =>
 *                  xlator_init () => NFS init() routine
 *
 * If init() routine fails, the glusterfsd cleans up the NFS process
 * by invoking cleanup_and_exit().
 *
 * RETURN:
 *       0 (SUCCESS) if all protocol specific inits PASS.
 *      -1 (FAILURE) if any of them FAILS.
 */
int
init (xlator_t *this) {

        struct nfs_state        *nfs = NULL;
        int                     ret = -1;

        if (!this)
                return (-1);

        nfs = nfs_init_state (this);
        if (!nfs) {
                gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_OPT_INIT_FAIL,
                        "Failed to init nfs option");
                return (-1);
        }

        ret = nfs_add_all_initiators (nfs);
        if (ret) {
                gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_INIT_FAIL,
                        "Failed to add initiators");
                return (-1);
        }

        ret = nfs_init_subvolumes (nfs, this->children);
        if (ret) {
                gf_msg (GF_NFS, GF_LOG_CRITICAL, 0, NFS_MSG_INIT_FAIL,
                        "Failed to init NFS exports");
                return (-1);
        }

        ret = mount_init_state (this);
        if (ret) {
                gf_msg (GF_NFS, GF_LOG_CRITICAL, 0, NFS_MSG_INIT_FAIL,
                        "Failed to init Mount state");
                return (-1);
        }

        ret = nlm4_init_state (this);
        if (ret) {
                gf_msg (GF_NFS, GF_LOG_CRITICAL, 0, NFS_MSG_INIT_FAIL,
                        "Failed to init NLM state");
                return (-1);
        }

        ret = nfs_init_versions (nfs, this);
        if (ret) {
                gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_INIT_FAIL,
                        "Failed to initialize protocols");
                return (-1);
        }

        ret = nfs_drc_init (this);
        if (ret) {
                gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_INIT_FAIL,
                        "Failed to initialize DRC");
                return (-1);
        }

        gf_msg (GF_NFS, GF_LOG_INFO, 0, NFS_MSG_STARTED,
                "NFS service started");
        return (0); /* SUCCESS */
}


int
notify (xlator_t *this, int32_t event, void *data, ...)
{
        xlator_t                *subvol = NULL;
        struct nfs_state        *priv   = NULL;

        subvol = (xlator_t *)data;

        gf_msg_trace (GF_NFS, 0, "Notification received: %d",
                event);

        switch (event) {
        case GF_EVENT_CHILD_UP:
                nfs_startup_subvolume (this, subvol);
                break;

        case GF_EVENT_SOME_DESCENDENT_DOWN:
        case GF_EVENT_SOME_DESCENDENT_UP:
                priv = this->private;
                ++(priv->generation);
                break;

        case GF_EVENT_PARENT_UP:
                default_notify (this, GF_EVENT_PARENT_UP, data);
                break;
        }

        return 0;
}


int
fini (xlator_t *this)
{

        struct nfs_state        *nfs = NULL;

        mnt3svc_deinit (this);
        nfs = (struct nfs_state *)this->private;
        gf_msg_debug (GF_NFS, 0, "NFS service going down");
        nfs_deinit_versions (&nfs->versions, this);
        GF_FREE (this->instance_name);
        return 0;
}

int32_t
nfs_forget (xlator_t *this, inode_t *inode)
{
        uint64_t                 ctx    = 0;
        struct nfs_inode_ctx    *ictx   = NULL;

        if (inode_ctx_del (inode, this, &ctx))
                return -1;

        ictx = (struct nfs_inode_ctx *)ctx;
        GF_FREE (ictx);

        return 0;
}

gf_boolean_t
_nfs_export_is_for_vol (char *exname, char *volname)
{
        gf_boolean_t    ret = _gf_false;
        char            *tmp = NULL;

        tmp = exname;
        if (tmp[0] == '/')
                tmp++;

        if (!strcmp (tmp, volname))
                ret = _gf_true;

        return ret;
}

int
nfs_priv_to_dict (xlator_t *this, dict_t *dict)
{
        int                     ret = -1;
        struct nfs_state        *priv = NULL;
        struct mountentry       *mentry = NULL;
        char                    *volname = NULL;
        char                    key[1024] = {0,};
        int                     count = 0;

        GF_VALIDATE_OR_GOTO (THIS->name, this, out);
        GF_VALIDATE_OR_GOTO (THIS->name, dict, out);

        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, -ret, NFS_MSG_VOL_NOT_FOUND,
                        "Could not get volname");
                goto out;
        }

        list_for_each_entry (mentry, &priv->mstate->mountlist, mlist) {
                if (!_nfs_export_is_for_vol (mentry->exname, volname))
                        continue;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "client%d.hostname", count);
                ret = dict_set_str (dict, key, mentry->hostname);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                NFS_MSG_WRITE_FAIL,
                                "Error writing hostname to dict");
                        goto out;
                }

                /* No connection data available yet in nfs server.
                 * Hence, setting to 0 to prevent cli failing
                 */
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "client%d.bytesread", count);
                ret = dict_set_uint64 (dict, key, 0);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                NFS_MSG_WRITE_FAIL,
                                "Error writing bytes read to dict");
                        goto out;
                }

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "client%d.byteswrite", count);
                ret = dict_set_uint64 (dict, key, 0);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                NFS_MSG_WRITE_FAIL,
                                "Error writing bytes write to dict");
                        goto out;
                }

                count++;
        }

        ret = dict_set_int32 (dict, "clientcount", count);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0, NFS_MSG_WRITE_FAIL,
                        "Error writing client count to dict");

out:
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);
        return ret;
}

extern int32_t
nlm_priv (xlator_t *this);

int32_t
nfs_priv (xlator_t *this)
{
        int32_t ret = -1;

        /* DRC needs the global drc structure, xl is of no use to it. */
        ret = rpcsvc_drc_priv (((struct nfs_state *)(this->private))->rpcsvc->drc);
        if (ret) {
                gf_msg_debug (this->name, 0, "Statedump of DRC failed");
                goto out;
        }

        ret = nlm_priv (this);
        if (ret) {
                gf_msg_debug (this->name, 0, "Statedump of NLM failed");
                goto out;
        }
 out:
        return ret;
}


struct xlator_cbks cbks = {
        .forget      = nfs_forget,
};

struct xlator_fops fops;

struct xlator_dumpops dumpops = {
        .priv           = nfs_priv,
        .priv_to_dict   = nfs_priv_to_dict,
};

/* TODO: If needed, per-volume options below can be extended to be export
 * specific also because after export-dir is introduced, a volume is not
 * necessarily an export whereas different subdirectories within that volume
 * can be and may need these options to be specified separately.
*/
struct volume_options options[] = {
        { .key  = {"nfs3.read-size"},
          .type = GF_OPTION_TYPE_SIZET,
          .min  = GF_NFS3_RTMIN,
          .max  = GF_NFS3_RTMAX,
          .default_value = TOSTRING(GF_NFS3_RTPREF),
          .description = "Size in which the client should issue read requests "
                         "to the Gluster NFSv3 server. Must be a multiple of "
                         "4KB (4096). Min and Max supported values are 4KB "
                         "(4096) and 1MB (1048576) respectively. If the "
                         "specified value is within the supported range but "
                         "not a multiple of 4096, it is rounded up to the "
                         "nearest multiple of 4096."
        },
        { .key  = {"nfs3.write-size"},
          .type = GF_OPTION_TYPE_SIZET,
          .min  = GF_NFS3_WTMIN,
          .max  = GF_NFS3_WTMAX,
          .default_value = TOSTRING(GF_NFS3_WTPREF),
          .description = "Size in which the client should issue write requests "
                         "to the Gluster NFSv3 server. Must be a multiple of "
                         "1KB (1024). Min and Max supported values are "
                         "4KB (4096) and 1MB(1048576) respectively. If the "
                         "specified value is within the supported range but "
                         "not a multiple of 4096, it is rounded up to the "
                         "nearest multiple of 4096."
        },
        { .key  = {"nfs3.readdir-size"},
          .type = GF_OPTION_TYPE_SIZET,
          .min  = GF_NFS3_DTMIN,
          .max  = GF_NFS3_DTMAX,
          .default_value = TOSTRING(GF_NFS3_DTPREF),
          .description = "Size in which the client should issue directory "
                         "reading requests to the Gluster NFSv3 server. Must "
                         "be a multiple of 1KB (1024). Min and Max supported "
                         "values are 4KB (4096) and 1MB (1048576) respectively."
                         "If the specified value is within the supported range "
                         "but not a multiple of 4096, it is rounded up to the "
                         "nearest multiple of 4096."
        },
        { .key  = {"nfs3.*.volume-access"},
          .type = GF_OPTION_TYPE_STR,
          .value = {"read-only", "read-write"},
          .default_value = "read-write",
          .description = "Type of access desired for this subvolume: "
                         " read-only, read-write(default)"
        },
        { .key  = {"nfs3.*.trusted-write"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
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
          .default_value = "off",
          .description = "All writes and COMMIT requests are treated as async."
                         " This implies that no write requests are guaranteed"
                         " to be on server disks when the write reply is "
                         "received at the NFS client. Trusted sync includes "
                         " trusted-write behaviour. Off by default."

        },
        { .key  = {"nfs3.*.export-dir"},
          .type = GF_OPTION_TYPE_PATH,
          .default_value = "",
          .description = "By default, all subvolumes of nfs are exported as "
                         "individual exports. There are cases where a "
                         "subdirectory or subdirectories in the volume need to "
                         "be exported separately. This option can also be used "
                         "in conjunction with nfs3.export-volumes option to "
                         "restrict exports only to the subdirectories specified"
                         " through this option. Must be an absolute path. Along"
                         " with path allowed list of IPs/hostname can be "
                         "associated with each subdirectory. If provided "
                         "connection will allowed only from these IPs. By "
                         "default connections from all IPs are allowed. "
                         "Format: <dir>[(hostspec[|hostspec|...])][,...]. Where"
                         " hostspec can be an IP address, hostname or an IP "
                         "range in CIDR notation. "
                         "e.g. /foo(192.168.1.0/24|host1|10.1.1.8),/host2."
                         " NOTE: Care must be taken while configuring this "
                         "option as invalid entries and/or unreachable DNS "
                         "servers can introduce unwanted delay in all the mount"
                         " calls."
        },
        { .key  = {"nfs3.export-dirs"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "By default, all subvolumes of nfs are exported as "
                         "individual exports. There are cases where a "
                         "subdirectory or subdirectories in the volume need to "
                         "be exported separately. Enabling this option allows "
                         "any directory on a volumes to be exported separately."
                         "Directory exports are enabled by default."
        },
        { .key  = {"nfs3.export-volumes"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "Enable or disable exporting whole volumes, instead "
                         "if used in conjunction with nfs3.export-dir, can "
                         "allow setting up only subdirectories as exports. On "
                         "by default."
        },
        { .key  = {"rpc-auth.auth-unix"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "Disable or enable the AUTH_UNIX authentication type."
                         "Must always be enabled for better interoperability. "
                         "However, can be disabled if needed. Enabled by "
                         "default"
        },
        { .key  = {"rpc-auth.auth-null"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "Disable or enable the AUTH_NULL authentication type."
                         "Must always be enabled. This option is here only to"
                         " avoid unrecognized option warnings"
        },
        { .key  = {"rpc-auth.auth-unix.*"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "Disable or enable the AUTH_UNIX authentication type "
                         "for a particular exported volume overriding defaults"
                         " and general setting for AUTH_UNIX scheme. Must "
                         "always be enabled for better interoperability. "
                         "However, can be disabled if needed. Enabled by "
                         "default."
        },
        { .key  = {"rpc-auth.auth-unix.*.allow"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = "on",
          .description = "Disable or enable the AUTH_UNIX authentication type "
                         "for a particular exported volume overriding defaults"
                         " and general setting for AUTH_UNIX scheme. Must "
                         "always be enabled for better interoperability. "
                         "However, can be disabled if needed. Enabled by "
                         "default."
        },
        { .key  = {"rpc-auth.auth-null.*"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "Disable or enable the AUTH_NULL authentication type "
                         "for a particular exported volume overriding defaults"
                         " and general setting for AUTH_NULL. Must always be "
                         "enabled. This option is here only to avoid "
                         "unrecognized option warnings."
        },
        { .key  = {"rpc-auth.addr.allow"},
          .type = GF_OPTION_TYPE_CLIENT_AUTH_ADDR,
          .default_value = "all",
          .description = "Allow a comma separated list of addresses and/or"
                         " hostnames to connect to the server. By default, all"
                         " connections are allowed. This allows users to "
                         "define a general rule for all exported volumes."
        },
        { .key  = {"rpc-auth.addr.reject"},
          .type = GF_OPTION_TYPE_CLIENT_AUTH_ADDR,
          .default_value = "none",
          .description = "Reject a comma separated list of addresses and/or"
                         " hostnames from connecting to the server. By default,"
                         " all connections are allowed. This allows users to "
                         "define a general rule for all exported volumes."
        },
        { .key  = {"rpc-auth.addr.*.allow"},
          .type = GF_OPTION_TYPE_CLIENT_AUTH_ADDR,
          .default_value = "all",
          .description = "Allow a comma separated list of addresses and/or"
                         " hostnames to connect to the server. By default, all"
                         " connections are allowed. This allows users to "
                         "define a rule for a specific exported volume."
        },
        { .key  = {"rpc-auth.addr.*.reject"},
          .type = GF_OPTION_TYPE_CLIENT_AUTH_ADDR,
          .default_value = "none",
          .description = "Reject a comma separated list of addresses and/or"
                         " hostnames from connecting to the server. By default,"
                         " all connections are allowed. This allows users to "
                         "define a rule for a specific exported volume."
        },
        { .key  = {"rpc-auth.ports.insecure"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "Allow client connections from unprivileged ports. By "
                         "default only privileged ports are allowed. This is a"
                         "global setting in case insecure ports are to be "
                         "enabled for all exports using a single option."
        },
        { .key  = {"rpc-auth.ports.*.insecure"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "Allow client connections from unprivileged ports. By "
                         "default only privileged ports are allowed. Use this"
                         " option to enable or disable insecure ports for "
                         "a specific subvolume and to override the global "
                         "setting set by the previous option."
        },
        { .key  = {"rpc-auth.addr.namelookup"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "Users have the option of turning on name lookup for"
                  " incoming client connections using this option. Use this "
                  "option to turn on name lookups during address-based "
                  "authentication. Turning this on will enable you to"
                  " use hostnames in nfs.rpc-auth-* filters. In some "
                  "setups, the name server can take too long to reply to DNS "
                  "queries resulting in timeouts of mount requests. By "
                  "default, name lookup is off"
        },
        { .key  = {"nfs.dynamic-volumes"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "Internal option set to tell gnfs to use a different"
                         " scheme for encoding file handles when DVM is being"
                         " used."
        },
        { .key  = {"nfs3.*.volume-id"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = "",
          .description = "When nfs.dynamic-volumes is set, gnfs expects every "
                         "subvolume to have this option set for it, so that "
                         "gnfs can use this option to identify the volume. "
                         "If all subvolumes do not have this option set, an "
                         "error is reported."
        },
        { .key  = {"nfs.enable-ino32"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "no",
          .description = "For nfs clients or apps that do not support 64-bit "
                         "inode numbers, use this option to make NFS return "
                         "32-bit inode numbers instead. Disabled by default, so"
                         " NFS returns 64-bit inode numbers."
        },
        { .key  = {"rpc.register-with-portmap"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "For systems that need to run multiple nfs servers, "
                         "only one registration is possible with "
                         "portmap service. Use this option to turn off portmap "
                         "registration for Gluster NFS. On by default"
        },
        { .key  = {"rpc.outstanding-rpc-limit"},
          .type = GF_OPTION_TYPE_INT,
          .min  = RPCSVC_MIN_OUTSTANDING_RPC_LIMIT,
          .max  = RPCSVC_MAX_OUTSTANDING_RPC_LIMIT,
          .default_value = TOSTRING(RPCSVC_DEF_NFS_OUTSTANDING_RPC_LIMIT),
          .description = "Parameter to throttle the number of incoming RPC "
                         "requests from a client. 0 means no limit (can "
                         "potentially run out of memory)"
        },
        { .key  = {"nfs.port"},
          .type = GF_OPTION_TYPE_INT,
          .min  = 1,
          .max  = 0xffff,
          .default_value = TOSTRING(GF_NFS3_PORT),
          .description = "Use this option on systems that need Gluster NFS to "
                         "be associated with a non-default port number."
        },
        { .key  = {"nfs.mem-factor"},
          .type = GF_OPTION_TYPE_INT,
          .min  = 1,
          .max  = 1024,
          .default_value = TOSTRING(GF_NFS_DEFAULT_MEMFACTOR),
          .description = "Use this option to make NFS be faster on systems by "
                         "using more memory. This option specifies a multiple "
                         "that determines the total amount of memory used. "
                         "Default value is 15. Increase to use more memory in "
                         "order to improve performance for certain use cases."
                         "Please consult gluster-users list before using this "
                         "option."
        },
        { .key  = {"nfs.*.disable"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "false",
          .description = "This option is used to start or stop the NFS server "
                         "for individual volumes."
        },
        { .key  = {"nfs.nlm"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "This option, if set to 'off', disables NLM server "
                         "by not registering the service with the portmapper."
                         " Set it to 'on' to re-enable it. Default value: 'on'"
        },

        { .key = {"nfs.mount-udp"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "set the option to 'on' to enable mountd on UDP. "
                         "Required for some Solaris and AIX NFS clients. "
                         "The need for enabling this option often depends "
                         "on the usage of NLM."
        },
        { .key = {"nfs.mount-rmtab"},
          .type = GF_OPTION_TYPE_PATH,
          .default_value = NFS_DATADIR "/rmtab",
          .description = "Set the location of the cache file that is used to "
                         "list all the NFS-clients that have connected "
                         "through the MOUNT protocol. If this is on shared "
                         "storage, all GlusterFS servers will update and "
                         "output (with 'showmount') the same list. Set to "
                         "\"/-\" to disable."
        },
        { .key = {OPT_SERVER_RPC_STATD},
          .type = GF_OPTION_TYPE_PATH,
          .default_value = GF_RPC_STATD_PROG,
          .description = "The executable of RPC statd utility. "
                         "Defaults to " GF_RPC_STATD_PROG
        },
        { .key = {OPT_SERVER_RPC_STATD_PIDFILE},
          .type = GF_OPTION_TYPE_PATH,
          .default_value = GF_RPC_STATD_PIDFILE,
          .description = "The pid file of RPC statd utility. "
                         "Defaults to " GF_RPC_STATD_PIDFILE
        },
        { .key = {OPT_SERVER_AUX_GIDS},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "Let the server look up which groups a user belongs "
                         "to, overwriting the list passed from the client. "
                         "This enables support for group lists longer than "
                         "can be passed through the NFS protocol, but is not "
                         "secure unless users and groups are well synchronized "
                         "between clients and servers."
        },
        { .key = {OPT_SERVER_GID_CACHE_TIMEOUT},
          .type = GF_OPTION_TYPE_INT,
          .min = 0,
          .max = 3600,
          .default_value = "300",
          .description = "Number of seconds to cache auxiliary-GID data, when "
                         OPT_SERVER_AUX_GIDS " is set."
        },
        { .key = {"nfs.acl"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "This option is used to control ACL support for NFS."
        },
        { .key  = {"nfs.drc"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = "off",
          .description = "Enable Duplicate Request Cache in gNFS server to "
                         "improve correctness of non-idempotent operations like "
                         "write, delete, link, et al"
        },
        { .key  = {"nfs.drc-size"},
          .type = GF_OPTION_TYPE_INT,
          .default_value = "0x20000",
          .description = "Sets the number of non-idempotent "
                         "requests to cache in drc"
        },
        { .key = {"nfs.exports-auth-enable"},
          .type = GF_OPTION_TYPE_BOOL,
          .description = "Set the option to 'on' to enable exports/netgroup "
                         "authentication in the NFS server and mount daemon."
        },

        { .key = {"nfs.auth-refresh-interval-sec"},
          .type = GF_OPTION_TYPE_INT,
          .description = "Frequency in seconds that the daemon should check for"
                         " changes in the exports/netgroups file."
        },

       { .key = {"nfs.auth-cache-ttl-sec"},
          .type = GF_OPTION_TYPE_INT,
          .description = "Sets the TTL of an entry in the auth cache. Value is "
                         "in seconds."
        },
        { .key  = {"nfs.rdirplus"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "When this option is set to off NFS falls back to "
                         "standard readdir instead of readdirp"
        },

        { .key  = {NULL} },
};
