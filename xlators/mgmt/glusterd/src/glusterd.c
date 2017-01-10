/*
   Copyright (c) 2006-2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <time.h>
#include <grp.h>
#include <sys/uio.h>
#include <sys/resource.h>

#include <libgen.h>
#include "compat-uuid.h"

#include "glusterd.h"
#include "rpcsvc.h"
#include "fnmatch.h"
#include "xlator.h"
#include "call-stub.h"
#include "defaults.h"
#include "list.h"
#include "dict.h"
#include "options.h"
#include "compat.h"
#include "compat-errno.h"
#include "syscall.h"
#include "glusterd-statedump.h"
#include "glusterd-sm.h"
#include "glusterd-op-sm.h"
#include "glusterd-store.h"
#include "glusterd-hooks.h"
#include "glusterd-utils.h"
#include "glusterd-locks.h"
#include "glusterd-svc-mgmt.h"
#include "glusterd-shd-svc.h"
#include "glusterd-nfs-svc.h"
#include "glusterd-bitd-svc.h"
#include "glusterd-scrub-svc.h"
#include "glusterd-quotad-svc.h"
#include "glusterd-snapd-svc.h"
#include "glusterd-messages.h"
#include "common-utils.h"
#include "glusterd-geo-rep.h"
#include "run.h"
#include "rpc-clnt-ping.h"
#include "rpc-common-xdr.h"

#include "syncop.h"

#include "glusterd-mountbroker.h"

extern struct rpcsvc_program gluster_handshake_prog;
extern struct rpcsvc_program gluster_cli_getspec_prog;
extern struct rpcsvc_program gluster_pmap_prog;
extern glusterd_op_info_t opinfo;
extern struct rpcsvc_program gd_svc_mgmt_prog;
extern struct rpcsvc_program gd_svc_mgmt_v3_prog;
extern struct rpcsvc_program gd_svc_peer_prog;
extern struct rpcsvc_program gd_svc_cli_prog;
extern struct rpcsvc_program gd_svc_cli_trusted_progs;
extern struct rpc_clnt_program gd_brick_prog;
extern struct rpcsvc_program glusterd_mgmt_hndsk_prog;

extern char snap_mount_dir[PATH_MAX];

rpcsvc_cbk_program_t glusterd_cbk_prog = {
        .progname  = "Gluster Callback",
        .prognum   = GLUSTER_CBK_PROGRAM,
        .progver   = GLUSTER_CBK_VERSION,
};

struct rpcsvc_program *gd_inet_programs[] = {
        &gd_svc_peer_prog,
        &gd_svc_cli_trusted_progs, /* Must be index 1 for secure_mgmt! */
        &gd_svc_mgmt_prog,
        &gd_svc_mgmt_v3_prog,
        &gluster_pmap_prog,
        &gluster_handshake_prog,
        &glusterd_mgmt_hndsk_prog,
};
int gd_inet_programs_count = (sizeof (gd_inet_programs) /
                              sizeof (gd_inet_programs[0]));

struct rpcsvc_program *gd_uds_programs[] = {
        &gd_svc_cli_prog,
        &gluster_cli_getspec_prog,
};
int gd_uds_programs_count = (sizeof (gd_uds_programs) /
                             sizeof (gd_uds_programs[0]));

const char *gd_op_list[GD_OP_MAX + 1] = {
        [GD_OP_NONE]                    = "Invalid op",
        [GD_OP_CREATE_VOLUME]           = "Create",
        [GD_OP_START_BRICK]             = "Start Brick",
        [GD_OP_STOP_BRICK]              = "Stop Brick",
        [GD_OP_DELETE_VOLUME]           = "Delete",
        [GD_OP_START_VOLUME]            = "Start",
        [GD_OP_STOP_VOLUME]             = "Stop",
        [GD_OP_DEFRAG_VOLUME]           = "Rebalance",
        [GD_OP_ADD_BRICK]               = "Add brick",
        [GD_OP_DETACH_TIER]             = "Detach tier",
        [GD_OP_TIER_MIGRATE]            = "Tier migration",
        [GD_OP_REMOVE_BRICK]            = "Remove brick",
        [GD_OP_REPLACE_BRICK]           = "Replace brick",
        [GD_OP_SET_VOLUME]              = "Set",
        [GD_OP_RESET_VOLUME]            = "Reset",
        [GD_OP_SYNC_VOLUME]             = "Sync",
        [GD_OP_LOG_ROTATE]              = "Log rotate",
        [GD_OP_GSYNC_SET]               = "Geo-replication",
        [GD_OP_PROFILE_VOLUME]          = "Profile",
        [GD_OP_QUOTA]                   = "Quota",
        [GD_OP_STATUS_VOLUME]           = "Status",
        [GD_OP_REBALANCE]               = "Rebalance",
        [GD_OP_HEAL_VOLUME]             = "Heal",
        [GD_OP_STATEDUMP_VOLUME]        = "Statedump",
        [GD_OP_LIST_VOLUME]             = "Lists",
        [GD_OP_CLEARLOCKS_VOLUME]       = "Clear locks",
        [GD_OP_DEFRAG_BRICK_VOLUME]     = "Rebalance",
        [GD_OP_COPY_FILE]               = "Copy File",
        [GD_OP_SYS_EXEC]                = "Execute system commands",
        [GD_OP_GSYNC_CREATE]            = "Geo-replication Create",
        [GD_OP_SNAP]                    = "Snapshot",
        [GD_OP_RESET_BRICK]             = "Reset Brick",
        [GD_OP_MAX_OPVERSION]           = "Maximum supported op-version",
        [GD_OP_MAX]                     = "Invalid op"
};

static int
glusterd_opinfo_init ()
{
        int32_t ret = -1;

        opinfo.op = GD_OP_NONE;

        return ret;
}


int
glusterd_uuid_init ()
{
        int             ret = -1;
        xlator_t        *this = NULL;
        glusterd_conf_t *priv = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;

        ret = glusterd_retrieve_uuid ();
        if (ret == 0) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        GD_MSG_RETRIEVED_UUID,
                        "retrieved UUID: %s", uuid_utoa (priv->uuid));
                return 0;
        }

        ret = glusterd_uuid_generate_save ();

        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_UUID_GEN_STORE_FAIL,
                          "Unable to generate and save new UUID");
                return ret;
        }

        return 0;
}

int
glusterd_uuid_generate_save ()
{
        int               ret = -1;
        glusterd_conf_t   *priv = NULL;
        xlator_t          *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        gf_uuid_generate (priv->uuid);

        gf_msg (this->name, GF_LOG_INFO, 0,
                GD_MSG_GENERATED_UUID, "generated UUID: %s",
                uuid_utoa (priv->uuid));

        ret = glusterd_store_global_info (this);

        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_UUID_STORE_FAIL,
                        "Unable to store the generated uuid %s",
                        uuid_utoa (priv->uuid));

        return ret;
}

int
glusterd_options_init (xlator_t *this)
{
        int             ret = -1;
        glusterd_conf_t *priv = NULL;
        char            *initial_version = "0";

        priv = this->private;

        priv->opts = dict_new ();
        if (!priv->opts)
                goto out;

        ret = glusterd_store_retrieve_options (this);
        if (ret == 0) {
                goto out;
        }

        ret = dict_set_str (priv->opts, GLUSTERD_GLOBAL_OPT_VERSION,
                            initial_version);
        if (ret)
                goto out;

        ret = glusterd_store_options (this, priv->opts);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VERS_STORE_FAIL, "Unable to store version");
                return ret;
        }
out:
        return 0;
}

int
glusterd_client_statedump_submit_req (char *volname, char *target_ip,
                                      char *pid)
{
        gf_statedump             statedump_req      = {0, };
        glusterd_conf_t         *conf               = NULL;
        int                      ret                = 0;
        char                    *end_ptr            = NULL;
        rpc_transport_t         *trans              = NULL;
        char                    *ip_addr            = NULL;
        xlator_t                *this               = NULL;
        char                     tmp[UNIX_PATH_MAX] = {0, };

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        if (target_ip == NULL || pid == NULL) {
                ret = -1;
                goto out;
        }

        statedump_req.pid = strtol (pid, &end_ptr, 10);

        gf_msg_debug (this->name, 0, "Performing statedump on volume %s "
                      "client with pid:%d host:%s", volname, statedump_req.pid,
                      target_ip);

        pthread_mutex_lock (&conf->xprt_lock);
        {
                list_for_each_entry (trans, &conf->xprt_list, list) {
                        /* check if this connection matches "all" or the
                         * volname */
                        if (strncmp (volname, "all", NAME_MAX) &&
                            strncmp (trans->peerinfo.volname, volname,
                                     NAME_MAX)) {
                                /* no match, try next trans */
                                continue;
                        }

                        strcpy (tmp, trans->peerinfo.identifier);
                        ip_addr = strtok (tmp, ":");
                        if (gf_is_same_address (ip_addr, target_ip)) {
                                /* Every gluster client would have
                                 * connected to glusterd(volfile server). This
                                 * connection is used to send the statedump
                                 * request rpc to the application.
                                 */
                                gf_msg_trace (this->name, 0, "Submitting "
                                        "statedump rpc request for %s",
                                        trans->peerinfo.identifier);
                                rpcsvc_request_submit (conf->rpc, trans,
                                                       &glusterd_cbk_prog,
                                                       GF_CBK_STATEDUMP,
                                                       &statedump_req, this->ctx,
                                                       (xdrproc_t)xdr_gf_statedump);
                        }
                }
        }
        pthread_mutex_unlock (&conf->xprt_lock);
out:
        return ret;

}

int
glusterd_fetchspec_notify (xlator_t *this)
{
        int              ret   = -1;
        glusterd_conf_t *priv  = NULL;
        rpc_transport_t *trans = NULL;

        priv = this->private;

        pthread_mutex_lock (&priv->xprt_lock);
        {
                list_for_each_entry (trans, &priv->xprt_list, list) {
                        rpcsvc_callback_submit (priv->rpc, trans,
                                                &glusterd_cbk_prog,
                                                GF_CBK_FETCHSPEC, NULL, 0,
                                                NULL);
                }
        }
        pthread_mutex_unlock (&priv->xprt_lock);

        ret = 0;

        return ret;
}

int
glusterd_fetchsnap_notify (xlator_t *this)
{
        int              ret   = -1;
        glusterd_conf_t *priv  = NULL;
        rpc_transport_t *trans = NULL;

        priv = this->private;

        /*
         * TODO: As of now, the identification of the rpc clients in the
         * handshake protocol is not there. So among so many glusterfs processes
         * registered with glusterd, it is hard to identify one particular
         * process (in this particular case, the snap daemon). So the callback
         * notification is sent to all the transports from the transport list.
         * Only those processes which have a rpc client registered for this
         * callback will respond to the notification. Once the identification
         * of the rpc clients becomes possible, the below section can be changed
         * to send callback notification to only those rpc clients, which have
         * registered.
         */
        pthread_mutex_lock (&priv->xprt_lock);
        {
                list_for_each_entry (trans, &priv->xprt_list, list) {
                        rpcsvc_callback_submit (priv->rpc, trans,
                                                &glusterd_cbk_prog,
                                                GF_CBK_GET_SNAPS, NULL, 0,
                                                NULL);
                }
        }
        pthread_mutex_unlock (&priv->xprt_lock);

        ret = 0;

        return ret;
}


int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_gld_mt_end + 1);

        if (ret != 0) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        GD_MSG_NO_MEMORY, "Memory accounting init"
                        " failed");
                return ret;
        }

        return ret;
}

int
glusterd_rpcsvc_notify (rpcsvc_t *rpc, void *xl, rpcsvc_event_t event,
                     void *data)
{
        xlator_t            *this = NULL;
        rpc_transport_t     *xprt = NULL;
        glusterd_conf_t     *priv = NULL;

        if (!xl || !data) {
                gf_msg ("glusterd", GF_LOG_WARNING, 0,
                        GD_MSG_NO_INIT,
                        "Calling rpc_notify without initializing");
                goto out;
        }

        this = xl;
        xprt = data;

        priv = this->private;

        switch (event) {
        case RPCSVC_EVENT_ACCEPT:
        {

                pthread_mutex_lock (&priv->xprt_lock);
                list_add_tail (&xprt->list, &priv->xprt_list);
                pthread_mutex_unlock (&priv->xprt_lock);
                break;
        }
        case RPCSVC_EVENT_DISCONNECT:
        {
                /* A DISCONNECT event could come without an ACCEPT event
                 * happening for this transport. This happens when the server is
                 * expecting encrypted connections by the client tries to
                 * connect unecnrypted
                 */
                if (list_empty (&xprt->list))
                        break;

                pthread_mutex_lock (&priv->xprt_lock);
                list_del (&xprt->list);
                pthread_mutex_unlock (&priv->xprt_lock);
                pmap_registry_remove (this, 0, NULL, GF_PMAP_PORT_NONE, xprt);
                break;
        }

        default:
                break;
        }

out:
        return 0;
}


static int32_t
glusterd_program_register (xlator_t *this, rpcsvc_t *svc,
                           rpcsvc_program_t *prog)
{
        int32_t ret = -1;

        ret = rpcsvc_program_register (svc, prog);
        if (ret) {
                gf_msg_debug (this->name, 0,
                        "cannot register program (name: %s, prognum:%d, "
                        "progver:%d)", prog->progname, prog->prognum,
                        prog->progver);
                goto out;
        }

out:
        return ret;
}

int
glusterd_rpcsvc_options_build (dict_t *options)
{
        int             ret = 0;
        uint32_t        backlog = 0;

        ret = dict_get_uint32 (options, "transport.socket.listen-backlog",
                               &backlog);

        if (ret) {
                backlog = GLUSTERD_SOCKET_LISTEN_BACKLOG;
                ret = dict_set_uint32 (options,
                                      "transport.socket.listen-backlog",
                                      backlog);
                if (ret)
                        goto out;
        }

        gf_msg_debug ("glusterd", 0, "listen-backlog value: %d", backlog);

out:
        return ret;
}

#if SYNCDAEMON_COMPILE
static int
glusterd_check_gsync_present (int *valid_state)
{
        char                buff[PATH_MAX] = {0, };
        runner_t            runner = {0,};
        char               *ptr = NULL;
        int                 ret = 0;

        runinit (&runner);
        runner_add_args (&runner, GSYNCD_PREFIX"/gsyncd", "--version", NULL);
        runner_redir (&runner, STDOUT_FILENO, RUN_PIPE);
        ret = runner_start (&runner);
        if (ret == -1) {
                if (errno == ENOENT) {
                        gf_msg ("glusterd", GF_LOG_INFO, errno,
                                GD_MSG_MODULE_NOT_INSTALLED, GEOREP
                                 " module not installed in the system");
                        *valid_state = 0;
                }
                else {
                        gf_msg ("glusterd", GF_LOG_ERROR, errno,
                                GD_MSG_MODULE_NOT_WORKING, GEOREP
                                  " module not working as desired");
                        *valid_state = -1;
                }
                goto out;
        }

        ptr = fgets(buff, sizeof(buff), runner_chio (&runner, STDOUT_FILENO));
        if (ptr) {
                if (!strstr (buff, "gsyncd")) {
                        ret = -1;
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_MODULE_NOT_WORKING, GEOREP" module not "
                                 "working as desired");
                        *valid_state = -1;
                        goto out;
                }
        } else {
                ret = -1;
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_MODULE_NOT_WORKING, GEOREP" module not "
                         "working as desired");
                *valid_state = -1;
                goto out;
        }

        ret = 0;
 out:

        runner_end (&runner);

        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
        return ret;

}

static int
group_write_allow (char *path, gid_t gid)
{
        struct stat st = {0,};
        int ret        = 0;

        ret = sys_stat (path, &st);
        if (ret == -1)
                goto out;
        GF_ASSERT (S_ISDIR (st.st_mode));

        ret = sys_chown (path, -1, gid);
        if (ret == -1)
                goto out;

        ret = sys_chmod (path, (st.st_mode & ~S_IFMT) | S_IWGRP|S_IXGRP|S_ISVTX);

 out:
        if (ret == -1)
                gf_msg ("glusterd", GF_LOG_CRITICAL, errno,
                        GD_MSG_WRITE_ACCESS_GRANT_FAIL,
                        "failed to set up write access to %s for group %d (%s)",
                        path, gid, strerror (errno));
        return ret;
}

static int
glusterd_crt_georep_folders (char *georepdir, glusterd_conf_t *conf)
{
        char *greplg_s   = NULL;
        struct group *gr = NULL;
        int ret          = 0;

        GF_ASSERT (georepdir);
        GF_ASSERT (conf);

        if (strlen (conf->workdir)+2 > PATH_MAX-strlen(GEOREP)) {
                ret = -1;
                gf_msg ("glusterd", GF_LOG_CRITICAL, 0,
                        GD_MSG_DIRPATH_TOO_LONG,
                        "directory path %s/"GEOREP" is longer than PATH_MAX",
                        conf->workdir);
                goto out;
        }

        snprintf (georepdir, PATH_MAX, "%s/"GEOREP, conf->workdir);
        ret = mkdir_p (georepdir, 0777, _gf_true);
        if (-1 == ret) {
                gf_msg ("glusterd", GF_LOG_CRITICAL, errno,
                        GD_MSG_CREATE_DIR_FAILED,
                        "Unable to create "GEOREP" directory %s",
                        georepdir);
                goto out;
        }

        if (strlen (DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP) >= PATH_MAX) {
                ret = -1;
                gf_msg ("glusterd", GF_LOG_CRITICAL, 0,
                        GD_MSG_DIRPATH_TOO_LONG,
                        "directory path "DEFAULT_LOG_FILE_DIRECTORY"/"
                        GEOREP" is longer than PATH_MAX");
                goto out;
        }
        ret = mkdir_p (DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP, 0777, _gf_true);
        if (-1 == ret) {
                gf_msg ("glusterd", GF_LOG_CRITICAL, errno,
                        GD_MSG_CREATE_DIR_FAILED,
                        "Unable to create "GEOREP" log directory");
                goto out;
        }

        /* Slave log file directory */
        if (strlen(DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"-slaves") >= PATH_MAX) {
                ret = -1;
                gf_msg ("glusterd", GF_LOG_CRITICAL, 0,
                        GD_MSG_DIRPATH_TOO_LONG,
                        "directory path "DEFAULT_LOG_FILE_DIRECTORY"/"
                        GEOREP"-slaves"" is longer than PATH_MAX");
                goto out;
        }
        ret = mkdir_p (DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"-slaves", 0777,
                      _gf_true);
        if (-1 == ret) {
                gf_msg ("glusterd", GF_LOG_CRITICAL, errno,
                        GD_MSG_CREATE_DIR_FAILED,
                        "Unable to create "GEOREP" slave log directory");
                goto out;
        }

        /* MountBroker log file directory */
        if (strlen(DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"-slaves/mbr") >= PATH_MAX) {
                ret = -1;
                gf_msg ("glusterd", GF_LOG_CRITICAL, 0,
                        GD_MSG_DIRPATH_TOO_LONG,
                        "directory path "DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP
                        "-slaves/mbr"" is longer than PATH_MAX");
                goto out;
        }
        ret = mkdir_p (DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"-slaves/mbr", 0777,
                      _gf_true);
        if (-1 == ret) {
                gf_msg ("glusterd", GF_LOG_CRITICAL, errno,
                        GD_MSG_CREATE_DIR_FAILED,
                        "Unable to create "GEOREP" mountbroker slave log directory");
                goto out;
        }

        ret = dict_get_str (THIS->options, GEOREP"-log-group", &greplg_s);
        if (ret)
                ret = 0;
        else {
                gr = getgrnam (greplg_s);
                if (!gr) {
                        gf_msg ("glusterd", GF_LOG_CRITICAL, 0,
                                GD_MSG_LOGGROUP_INVALID,
                                "group "GEOREP"-log-group %s does not exist", greplg_s);
                        ret = -1;
                        goto out;
                }

                ret = group_write_allow (DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP,
                                         gr->gr_gid);
                if (ret == 0)
                        ret = group_write_allow (DEFAULT_LOG_FILE_DIRECTORY"/"
                                                 GEOREP"-slaves", gr->gr_gid);
                if (ret == 0)
                        ret = group_write_allow (DEFAULT_LOG_FILE_DIRECTORY"/"
                                                 GEOREP"-slaves/mbr", gr->gr_gid);
        }

 out:
        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
        return ret;
}

static void
runinit_gsyncd_setrx (runner_t *runner, glusterd_conf_t *conf)
{
        runinit (runner);
        runner_add_args (runner, GSYNCD_PREFIX"/gsyncd", "-c", NULL);
        runner_argprintf (runner, "%s/"GSYNC_CONF_TEMPLATE, conf->workdir);
        runner_add_arg (runner, "--config-set-rx");
}

static int
configure_syncdaemon (glusterd_conf_t *conf)
#define RUN_GSYNCD_CMD do {                                                          \
        ret = runner_run_reuse (&runner);                                            \
        if (ret == -1) {                                                             \
                runner_log (&runner, "glusterd", GF_LOG_ERROR, "command failed");    \
                runner_end (&runner);                                                \
                goto out;                                                            \
        }                                                                            \
        runner_end (&runner);                                                        \
} while (0)
{
        int ret = 0;
        runner_t runner = {0,};
        char georepdir[PATH_MAX] = {0,};
        int valid_state = 0;

        ret = setenv ("_GLUSTERD_CALLED_", "1", 1);
        if (ret < 0) {
                ret = 0;
                goto out;
        }
        valid_state = -1;
        ret = glusterd_check_gsync_present (&valid_state);
        if (-1 == ret) {
                ret = valid_state;
                goto out;
        }

        glusterd_crt_georep_folders (georepdir, conf);
        if (ret) {
                ret = 0;
                goto out;
        }

        /************
         * master pre-configuration
         ************/

        /* remote-gsyncd */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args (&runner, "remote-gsyncd", GSYNCD_PREFIX"/gsyncd", ".", ".", NULL);
        RUN_GSYNCD_CMD;

        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args (&runner, "remote-gsyncd", "/nonexistent/gsyncd",
                         ".", "^ssh:", NULL);
        RUN_GSYNCD_CMD;

        /* gluster-command-dir */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args (&runner, "gluster-command-dir", SBIN_DIR"/",
                         ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* gluster-params */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args (&runner, "gluster-params",
                         "aux-gfid-mount acl",
                         ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* ssh-command */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_arg (&runner, "ssh-command");
        runner_argprintf (&runner,
                          "ssh -oPasswordAuthentication=no "
                           "-oStrictHostKeyChecking=no "
                           "-i %s/secret.pem", georepdir);
        runner_add_args (&runner, ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* ssh-command tar */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_arg (&runner, "ssh-command-tar");
        runner_argprintf (&runner,
                          "ssh -oPasswordAuthentication=no "
                           "-oStrictHostKeyChecking=no "
                           "-i %s/tar_ssh.pem", georepdir);
        runner_add_args (&runner, ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* pid-file */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_arg (&runner, "pid-file");
        runner_argprintf (&runner, "%s/${mastervol}_${remotehost}_${slavevol}/monitor.pid", georepdir);
        runner_add_args (&runner, ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* geo-rep working dir */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_arg (&runner, "georep-session-working-dir");
        runner_argprintf (&runner, "%s/${mastervol}_${remotehost}_${slavevol}/", georepdir);
        runner_add_args (&runner, ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* state-file */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_arg (&runner, "state-file");
        runner_argprintf (&runner, "%s/${mastervol}_${remotehost}_${slavevol}/monitor.status", georepdir);
        runner_add_args (&runner, ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* state-detail-file */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_arg (&runner, "state-detail-file");
        runner_argprintf (&runner, "%s/${mastervol}_${remotehost}_${slavevol}/${eSlave}-detail.status",
                          georepdir);
        runner_add_args (&runner, ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* state-detail-file */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_arg (&runner, "state-detail-file");
        runner_argprintf (&runner, "%s/${mastervol}_${remotehost}_${slavevol}/${eSlave}-detail.status",
                          georepdir);
        runner_add_args (&runner, ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* state-socket */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_arg (&runner, "state-socket-unencoded");
        runner_argprintf (&runner, "%s/${mastervol}/${eSlave}.socket", georepdir);
        runner_add_args (&runner, ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* socketdir */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args (&runner, "socketdir", GLUSTERD_SOCK_DIR, ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* log-file */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args (&runner,
                         "log-file",
                         DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"/${mastervol}/${eSlave}.log",
                         ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* gluster-log-file */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args (&runner,
                         "gluster-log-file",
                         DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"/${mastervol}/${eSlave}${local_id}.gluster.log",
                         ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* ignore-deletes */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args (&runner, "ignore-deletes", "true", ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* special-sync-mode */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args (&runner, "special-sync-mode", "partial", ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* change-detector == changelog */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args(&runner, "change-detector", "changelog", ".", ".", NULL);
        RUN_GSYNCD_CMD;

        runinit_gsyncd_setrx (&runner, conf);
        runner_add_arg(&runner, "working-dir");
        runner_argprintf(&runner, "%s/${mastervol}/${eSlave}",
                         DEFAULT_VAR_RUN_DIRECTORY);
        runner_add_args (&runner, ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /************
         * slave pre-configuration
         ************/

        /* gluster-command-dir */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args (&runner, "gluster-command-dir", SBIN_DIR"/",
                         ".", NULL);
        RUN_GSYNCD_CMD;

        /* gluster-params */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args (&runner, "gluster-params",
                         "aux-gfid-mount acl",
                         ".", NULL);
        RUN_GSYNCD_CMD;

        /* log-file */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args (&runner,
                         "log-file",
                         DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"-slaves/${session_owner}:${local_node}${local_id}.${slavevol}.log",
                         ".", NULL);
        RUN_GSYNCD_CMD;

        /* MountBroker log-file */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args (&runner,
                         "log-file-mbr",
                         DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"-slaves/mbr/${session_owner}:${local_node}${local_id}.${slavevol}.log",
                         ".", NULL);
        RUN_GSYNCD_CMD;

        /* gluster-log-file */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args (&runner,
                         "gluster-log-file",
                         DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"-slaves/${session_owner}:${local_node}${local_id}.${slavevol}.gluster.log",
                         ".", NULL);
        RUN_GSYNCD_CMD;

 out:
        return ret ? -1 : 0;
}
#undef RUN_GSYNCD_CMD
#else /* SYNCDAEMON_COMPILE */
static int
configure_syncdaemon (glusterd_conf_t *conf)
{
        return 0;
}
#endif /* !SYNCDAEMON_COMPILE */


static int
check_prepare_mountbroker_root (char *mountbroker_root)
{
        int dfd0        = -1;
        int dfd         = -1;
        int dfd2        = -1;
        struct stat st  = {0,};
        struct stat st2 = {0,};
        int ret         = 0;

        ret = open (mountbroker_root, O_RDONLY);
        if (ret != -1) {
                dfd = ret;
                ret = sys_fstat (dfd, &st);
        }
        if (ret == -1 || !S_ISDIR (st.st_mode)) {
                gf_msg ("glusterd", GF_LOG_ERROR, errno,
                        GD_MSG_DIR_OP_FAILED,
                        "cannot access mountbroker-root directory %s",
                        mountbroker_root);
                ret = -1;
                goto out;
        }
        if (st.st_uid != 0 ||
            (st.st_mode & (S_IWGRP|S_IWOTH))) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DIR_PERM_LIBERAL,
                        "permissions on mountbroker-root directory %s are "
                        "too liberal", mountbroker_root);
                ret = -1;
                goto out;
        }
        if (!(st.st_mode & (S_IXGRP|S_IXOTH))) {
                gf_msg ("glusterd", GF_LOG_WARNING, 0,
                        GD_MSG_DIR_PERM_STRICT,
                        "permissions on mountbroker-root directory %s are "
                        "probably too strict", mountbroker_root);
        }

        dfd0 = dup (dfd);

        for (;;) {
                ret = sys_openat (dfd, "..", O_RDONLY, 0);
                if (ret != -1) {
                        dfd2 = ret;
                        ret = sys_fstat (dfd2, &st2);
                }
                if (ret == -1) {
                        gf_msg ("glusterd", GF_LOG_ERROR, errno,
                                GD_MSG_DIR_OP_FAILED,
                                "error while checking mountbroker-root ancestors "
                                "%d (%s)", errno, strerror (errno));
                        goto out;
                }

                if (st2.st_ino == st.st_ino)
                        break; /* arrived to root */

                if (st2.st_uid != 0 ||
                    ((st2.st_mode & (S_IWGRP|S_IWOTH)) &&
                     !(st2.st_mode & S_ISVTX))) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_DIR_PERM_LIBERAL,
                                "permissions on ancestors of mountbroker-root "
                                "directory are too liberal");
                        ret = -1;
                        goto out;
                }
                if (!(st.st_mode & (S_IXGRP|S_IXOTH))) {
                        gf_msg ("glusterd", GF_LOG_WARNING, 0,
                                GD_MSG_DIR_PERM_STRICT,
                                "permissions on ancestors of mountbroker-root "
                                "directory are probably too strict");
                }

                sys_close (dfd);
                dfd = dfd2;
                st = st2;
        }

        ret = sys_mkdirat (dfd0, MB_HIVE, 0711);
        if (ret == -1 && errno == EEXIST)
                ret = 0;
        if (ret != -1)
                ret = sys_fstatat (dfd0, MB_HIVE, &st, AT_SYMLINK_NOFOLLOW);
        if (ret == -1 || st.st_mode != (S_IFDIR|0711)) {
                gf_msg ("glusterd", GF_LOG_ERROR, errno,
                        GD_MSG_CREATE_DIR_FAILED,
                        "failed to set up mountbroker-root directory %s",
                        mountbroker_root);
                ret = -1;
                goto out;
        }

        ret = 0;

 out:
        if (dfd0 != -1)
                sys_close (dfd0);
        if (dfd != -1)
                sys_close (dfd);
        if (dfd2 != -1)
                sys_close (dfd2);

        return ret;
}

static int
_install_mount_spec (dict_t *opts, char *key, data_t *value, void *data)
{
        glusterd_conf_t *priv           = THIS->private;
        char            *label          = NULL;
        gf_boolean_t     georep         = _gf_false;
        gf_boolean_t     ghadoop        = _gf_false;
        char            *pdesc          = value->data;
        char            *volname        = NULL;
        int              rv             = 0;
        gf_mount_spec_t *mspec          = NULL;
        char            *user           = NULL;
        char            *volfile_server = NULL;

        label = strtail (key, "mountbroker.");

        /* check for presence of geo-rep/hadoop label */
        if (!label) {
                label = strtail (key, "mountbroker-"GEOREP".");
                if (label)
                        georep = _gf_true;
                else {
                        label = strtail (key, "mountbroker-"GHADOOP".");
                        if (label)
                                ghadoop = _gf_true;
                }
        }

        if (!label)
                return 0;

        mspec = GF_CALLOC (1, sizeof (*mspec), gf_gld_mt_mount_spec);
        if (!mspec)
                goto err;
        mspec->label = label;

        if (georep || ghadoop) {
                volname = gf_strdup (pdesc);
                if (!volname)
                        goto err;
                user = strchr (volname, ':');
                if (user) {
                        *user = '\0';
                        user++;
                } else
                        user = label;

                if (georep)
                        rv = make_georep_mountspec (mspec, volname, user);

                if (ghadoop) {
                        volfile_server = strchr (user, ':');
                        if (volfile_server)
                                *volfile_server++ = '\0';
                        else
                                volfile_server = "localhost";

                        rv = make_ghadoop_mountspec (mspec, volname, user, volfile_server);
                }

                GF_FREE (volname);
                if (rv != 0)
                        goto err;
        } else if (parse_mount_pattern_desc (mspec, pdesc) != 0)
                goto err;

        cds_list_add_tail (&mspec->speclist, &priv->mount_specs);

        return 0;
 err:

        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                GD_MSG_MOUNT_SPEC_INSTALL_FAIL,
                "adding %smount spec failed: label: %s desc: %s",
                georep ? GEOREP" " : "", label, pdesc);

        if (mspec) {
                if (mspec->patterns) {
                        GF_FREE (mspec->patterns->components);
                        GF_FREE (mspec->patterns);
                }
                GF_FREE (mspec);
        }

        return -1;
}


/* The glusterd unix domain socket listener only listens for cli */
rpcsvc_t *
glusterd_init_uds_listener (xlator_t *this)
{
        int             ret = -1;
        dict_t          *options = NULL;
        rpcsvc_t        *rpc = NULL;
        data_t          *sock_data = NULL;
        char            sockfile[UNIX_PATH_MAX+1] = {0,};
        int             i = 0;


        GF_ASSERT (this);

        sock_data = dict_get (this->options, "glusterd-sockfile");
        if (!sock_data) {
                strncpy (sockfile, DEFAULT_GLUSTERD_SOCKFILE, UNIX_PATH_MAX);
        } else {
                strncpy (sockfile, sock_data->data, UNIX_PATH_MAX);
        }

        options = dict_new ();
        if (!options)
                goto out;

        ret = rpcsvc_transport_unix_options_build (&options, sockfile);
        if (ret)
                goto out;

        rpc = rpcsvc_init (this, this->ctx, options, 8);
        if (rpc == NULL) {
                ret = -1;
                goto out;
        }

        ret = rpcsvc_register_notify (rpc, glusterd_rpcsvc_notify, this);
        if (ret) {
                gf_msg_debug (this->name, 0,
                        "Failed to register notify function");
                goto out;
        }

        ret = rpcsvc_create_listeners (rpc, options, this->name);
        if (ret != 1) {
                gf_msg_debug (this->name, 0, "Failed to create listener");
                goto out;
        }
        ret = 0;

        for (i = 0; i < gd_uds_programs_count; i++) {
                ret = glusterd_program_register (this, rpc, gd_uds_programs[i]);
                if (ret) {
                        i--;
                        for (; i >= 0; i--)
                                rpcsvc_program_unregister (rpc,
                                                           gd_uds_programs[i]);

                        goto out;
                }
        }

out:
        if (options)
                dict_unref (options);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GLUSTERD_SOCK_LISTENER_START_FAIL,
                        "Failed to start glusterd "
                        "unix domain socket listener.");
                if (rpc) {
                        GF_FREE (rpc);
                        rpc = NULL;
                }
        }
        return rpc;
}

void
glusterd_stop_uds_listener (xlator_t *this)
{
        glusterd_conf_t         *conf = NULL;
        rpcsvc_listener_t       *listener = NULL;
        rpcsvc_listener_t       *next = NULL;
        data_t                  *sock_data = NULL;
        char                     sockfile[UNIX_PATH_MAX+1] = {0,};

        GF_ASSERT (this);
        conf = this->private;

        (void) rpcsvc_program_unregister (conf->uds_rpc, &gd_svc_cli_prog);
        (void) rpcsvc_program_unregister (conf->uds_rpc, &gluster_handshake_prog);

        list_for_each_entry_safe (listener, next, &conf->uds_rpc->listeners,
                                  list) {
                rpcsvc_listener_destroy (listener);
        }

        (void) rpcsvc_unregister_notify (conf->uds_rpc,
                                         glusterd_rpcsvc_notify, this);

        sock_data = dict_get (this->options, "glusterd-sockfile");
        if (!sock_data) {
                strncpy (sockfile, DEFAULT_GLUSTERD_SOCKFILE, UNIX_PATH_MAX);
        } else {
                strncpy (sockfile, sock_data->data, UNIX_PATH_MAX);
        }
        sys_unlink (sockfile);

        return;
}


void
glusterd_stop_listener (xlator_t *this)
{
        glusterd_conf_t         *conf = NULL;
        rpcsvc_listener_t       *listener = NULL;
        rpcsvc_listener_t       *next = NULL;
        int                      i = 0;

        GF_VALIDATE_OR_GOTO ("glusterd", this, out);
        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, conf, out);

        gf_msg_debug (this->name, 0,
                "%s function called ", __func__);

        for (i = 0; i < gd_inet_programs_count; i++) {
              rpcsvc_program_unregister (conf->rpc, gd_inet_programs[i]);
        }

        list_for_each_entry_safe (listener, next, &conf->rpc->listeners, list) {
                rpcsvc_listener_destroy (listener);
        }

        (void) rpcsvc_unregister_notify (conf->rpc,
                                         glusterd_rpcsvc_notify,
                                         this);

out:

        return;
}

static int
glusterd_find_correct_var_run_dir (xlator_t *this, char *var_run_dir)
{
        int             ret = -1;
        struct stat     buf = {0,};

        GF_VALIDATE_OR_GOTO ("glusterd", this, out);
        GF_VALIDATE_OR_GOTO (this->name, var_run_dir, out);

        /* /var/run is normally a symbolic link to /run dir, which
         * creates problems as the entry point in the mtab for the mount point
         * and glusterd maintained entry point will be different. Therefore
         * identify the correct run dir and use it
         */
        ret = sys_lstat (GLUSTERD_VAR_RUN_DIR, &buf);
        if (ret != 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_FILE_OP_FAILED,
                        "stat fails on %s, exiting. (errno = %d)",
                        GLUSTERD_VAR_RUN_DIR, errno);
                goto out;
        }

        /* If /var/run is symlink then use /run dir */
        if (S_ISLNK (buf.st_mode)) {
                strcpy (var_run_dir, GLUSTERD_RUN_DIR);
        } else {
                strcpy (var_run_dir, GLUSTERD_VAR_RUN_DIR);
        }

        ret = 0;
out:
        return ret;
}

static int
glusterd_init_var_run_dirs (xlator_t *this, char *var_run_dir,
                            char *dir_to_be_created)
{
        int             ret                = -1;
        struct stat     buf                = {0,};
        char            abs_path[PATH_MAX] = {0, };

        GF_VALIDATE_OR_GOTO ("glusterd", this, out);
        GF_VALIDATE_OR_GOTO (this->name, var_run_dir, out);
        GF_VALIDATE_OR_GOTO (this->name, dir_to_be_created, out);

        snprintf (abs_path, sizeof(abs_path), "%s%s",
                  var_run_dir, dir_to_be_created);

        ret = sys_stat (abs_path, &buf);
        if ((ret != 0) && (ENOENT != errno)) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_FILE_OP_FAILED,
                        "stat fails on %s, exiting. (errno = %d)",
                        abs_path, errno);
                ret = -1;
                goto out;
        }

        if ((!ret) && (!S_ISDIR(buf.st_mode))) {
                gf_msg (this->name, GF_LOG_CRITICAL, ENOENT,
                        GD_MSG_DIR_NOT_FOUND,
                        "Provided snap path %s is not a directory,"
                        "exiting", abs_path);
                ret = -1;
                goto out;
        }

        if ((-1 == ret) && (ENOENT == errno)) {
                /* Create missing dirs */
                ret = mkdir_p (abs_path, 0777, _gf_true);

                if (-1 == ret) {
                        gf_msg (this->name, GF_LOG_CRITICAL, errno,
                                GD_MSG_CREATE_DIR_FAILED,
                                "Unable to create directory %s"
                                " ,errno = %d", abs_path, errno);
                        goto out;
                }
        }

out:
        return ret;
}

static void
glusterd_svcs_build ()
{
        xlator_t           *this    = NULL;
        glusterd_conf_t    *priv    = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        priv->shd_svc.build = glusterd_shdsvc_build;
        priv->shd_svc.build (&(priv->shd_svc));

        priv->nfs_svc.build = glusterd_nfssvc_build;
        priv->nfs_svc.build (&(priv->nfs_svc));

        priv->quotad_svc.build = glusterd_quotadsvc_build;
        priv->quotad_svc.build (&(priv->quotad_svc));

        priv->bitd_svc.build = glusterd_bitdsvc_build;
        priv->bitd_svc.build (&(priv->bitd_svc));

        priv->scrub_svc.build = glusterd_scrubsvc_build;
        priv->scrub_svc.build (&(priv->scrub_svc));
}

static int
is_upgrade (dict_t *options, gf_boolean_t *upgrade)
{
        int              ret              = 0;
        char            *type             = NULL;

        ret = dict_get_str (options, "upgrade", &type);
        if (!ret) {
                ret = gf_string2boolean (type, upgrade);
                if (ret) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_STR_TO_BOOL_FAIL, "upgrade option "
                                "%s is not a valid boolean type", type);
                        ret = -1;
                        goto out;
                }
        }
        ret = 0;
out:
        return ret;
}

static int
is_downgrade (dict_t *options, gf_boolean_t *downgrade)
{
        int              ret              = 0;
        char            *type             = NULL;

        ret = dict_get_str (options, "downgrade", &type);
        if (!ret) {
                ret = gf_string2boolean (type, downgrade);
                if (ret) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_STR_TO_BOOL_FAIL, "downgrade option "
                                "%s is not a valid boolean type", type);
                        ret = -1;
                        goto out;
                }
        }
        ret = 0;
out:
        return ret;
}

/*
 * init - called during glusterd initialization
 *
 * @this:
 *
 */
int
init (xlator_t *this)
{
        int32_t            ret                        = -1;
        rpcsvc_t          *rpc                        = NULL;
        rpcsvc_t          *uds_rpc                    = NULL;
        glusterd_conf_t   *conf                       = NULL;
        data_t            *dir_data                   = NULL;
        struct stat        buf                        = {0,};
        char               storedir[PATH_MAX]         = {0,};
        char               workdir[PATH_MAX]          = {0,};
        char               cmd_log_filename[PATH_MAX] = {0,};
        char              *mountbroker_root           = NULL;
        int                i                          = 0;
        int                total_transport            = 0;
        char              *valgrind_str               = NULL;
        char              *transport_type             = NULL;
        char               var_run_dir[PATH_MAX]      = {0,};
        int32_t            workers                    = 0;
        gf_boolean_t       upgrade                    = _gf_false;
        gf_boolean_t       downgrade                  = _gf_false;

#ifndef GF_DARWIN_HOST_OS
        {
                struct rlimit lim;
                lim.rlim_cur = 65536;
                lim.rlim_max = 65536;

                if (setrlimit (RLIMIT_NOFILE, &lim) == -1) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_SETXATTR_FAIL,
                                "Failed to set 'ulimit -n "
                                " 65536'");
                } else {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                GD_MSG_FILE_DESC_LIMIT_SET,
                                "Maximum allowed open file descriptors "
                                "set to 65536");
                }
        }
#endif

        dir_data = dict_get (this->options, "working-directory");

        if (!dir_data) {
                //Use default working dir
                strncpy (workdir, GLUSTERD_DEFAULT_WORKDIR, PATH_MAX);
        } else {
                strncpy (workdir, dir_data->data, PATH_MAX);
        }

        ret = sys_stat (workdir, &buf);
        if ((ret != 0) && (ENOENT != errno)) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DIR_OP_FAILED,
                        "stat fails on %s, exiting. (errno = %d)",
                        workdir, errno);
                exit (1);
        }

        if ((!ret) && (!S_ISDIR(buf.st_mode))) {
                gf_msg (this->name, GF_LOG_CRITICAL, ENOENT,
                        GD_MSG_DIR_NOT_FOUND,
                        "Provided working area %s is not a directory,"
                        "exiting", workdir);
                exit (1);
        }


        if ((-1 == ret) && (ENOENT == errno)) {
                ret = mkdir_p (workdir, 0777, _gf_true);

                if (-1 == ret) {
                        gf_msg (this->name, GF_LOG_CRITICAL, errno,
                                GD_MSG_CREATE_DIR_FAILED,
                                "Unable to create directory %s"
                                " ,errno = %d", workdir, errno);
                        exit (1);
                }
        }

        setenv ("GLUSTERD_WORKDIR", workdir, 1);
        gf_msg (this->name, GF_LOG_INFO, 0,
                GD_MSG_CURR_WORK_DIR_INFO, "Using %s as working directory",
                workdir);

        ret = glusterd_find_correct_var_run_dir (this, var_run_dir);
        if (ret) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        GD_MSG_VAR_RUN_DIR_FIND_FAIL, "Unable to find "
                        "the correct var run dir");
                exit (1);
        }

        ret = glusterd_init_var_run_dirs (this, var_run_dir,
                                      GLUSTERD_DEFAULT_SNAPS_BRICK_DIR);
        if (ret) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        GD_MSG_CREATE_DIR_FAILED, "Unable to create "
                        "snap backend folder");
                exit (1);
        }

        snprintf (snap_mount_dir, sizeof(snap_mount_dir), "%s%s",
                  var_run_dir, GLUSTERD_DEFAULT_SNAPS_BRICK_DIR);

        ret = mkdir_p (GLUSTER_SHARED_STORAGE_BRICK_DIR, 0777,
                       _gf_true);
        if (ret) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        GD_MSG_DIR_OP_FAILED, "Unable to create "
                        "shared storage brick");
                exit (1);
        }

        snprintf (cmd_log_filename, PATH_MAX, "%s/cmd_history.log",
                  DEFAULT_LOG_FILE_DIRECTORY);
        ret = gf_cmd_log_init (cmd_log_filename);

        if (ret == -1) {
                gf_msg ("this->name", GF_LOG_CRITICAL, errno,
                        GD_MSG_FILE_OP_FAILED,
                        "Unable to create cmd log file %s", cmd_log_filename);
                exit (1);
        }

        snprintf (storedir, PATH_MAX, "%s/vols", workdir);

        ret = sys_mkdir (storedir, 0777);

        if ((-1 == ret) && (errno != EEXIST)) {
                gf_msg (this->name, GF_LOG_CRITICAL, errno,
                        GD_MSG_CREATE_DIR_FAILED,
                        "Unable to create volume directory %s"
                        " ,errno = %d", storedir, errno);
                exit (1);
        }

        snprintf (storedir, PATH_MAX, "%s/snaps", workdir);

        ret = sys_mkdir (storedir, 0777);

        if ((-1 == ret) && (errno != EEXIST)) {
                gf_msg (this->name, GF_LOG_CRITICAL, errno,
                        GD_MSG_CREATE_DIR_FAILED,
                        "Unable to create snaps directory %s"
                        " ,errno = %d", storedir, errno);
                exit (1);
        }

        snprintf (storedir, PATH_MAX, "%s/peers", workdir);

        ret = sys_mkdir (storedir, 0777);

        if ((-1 == ret) && (errno != EEXIST)) {
                gf_msg (this->name, GF_LOG_CRITICAL, errno,
                        GD_MSG_CREATE_DIR_FAILED,
                        "Unable to create peers directory %s"
                        " ,errno = %d", storedir, errno);
                exit (1);
        }

        snprintf (storedir, PATH_MAX, "%s/bricks", DEFAULT_LOG_FILE_DIRECTORY);
        ret = sys_mkdir (storedir, 0777);
        if ((-1 == ret) && (errno != EEXIST)) {
                gf_msg (this->name, GF_LOG_CRITICAL, errno,
                        GD_MSG_CREATE_DIR_FAILED,
                        "Unable to create logs directory %s"
                        " ,errno = %d", storedir, errno);
                exit (1);
        }

        snprintf (storedir, PATH_MAX, "%s/nfs", workdir);
        ret = sys_mkdir (storedir, 0777);
        if ((-1 == ret) && (errno != EEXIST)) {
                gf_msg (this->name, GF_LOG_CRITICAL, errno,
                        GD_MSG_CREATE_DIR_FAILED,
                        "Unable to create nfs directory %s"
                        " ,errno = %d", storedir, errno);
                exit (1);
        }

        snprintf (storedir, PATH_MAX, "%s/bitd", workdir);
        ret = sys_mkdir (storedir, 0777);
        if ((-1 == ret) && (errno != EEXIST)) {
                gf_msg (this->name, GF_LOG_CRITICAL, errno,
                        GD_MSG_CREATE_DIR_FAILED,
                        "Unable to create bitrot directory %s",
                        storedir);
                exit (1);
        }

        snprintf (storedir, PATH_MAX, "%s/scrub", workdir);
        ret = sys_mkdir (storedir, 0777);
        if ((-1 == ret) && (errno != EEXIST)) {
                gf_msg (this->name, GF_LOG_CRITICAL, errno,
                        GD_MSG_CREATE_DIR_FAILED,
                        "Unable to create scrub directory %s",
                        storedir);
                exit (1);
        }

        snprintf (storedir, PATH_MAX, "%s/glustershd", workdir);
        ret = sys_mkdir (storedir, 0777);
        if ((-1 == ret) && (errno != EEXIST)) {
                gf_msg (this->name, GF_LOG_CRITICAL, errno,
                        GD_MSG_CREATE_DIR_FAILED,
                        "Unable to create glustershd directory %s"
                        " ,errno = %d", storedir, errno);
                exit (1);
        }

        snprintf (storedir, PATH_MAX, "%s/quotad", workdir);
        ret = sys_mkdir (storedir, 0777);
        if ((-1 == ret) && (errno != EEXIST)) {
                gf_msg (this->name, GF_LOG_CRITICAL, errno,
                        GD_MSG_CREATE_DIR_FAILED,
                        "Unable to create quotad directory %s"
                        " ,errno = %d", storedir, errno);
                exit (1);
        }

        snprintf (storedir, PATH_MAX, "%s/groups", workdir);
        ret = sys_mkdir (storedir, 0777);
        if ((-1 == ret) && (errno != EEXIST)) {
                gf_msg (this->name, GF_LOG_CRITICAL, errno,
                        GD_MSG_CREATE_DIR_FAILED,
                        "Unable to create glustershd directory %s"
                        " ,errno = %d", storedir, errno);
                exit (1);
        }

        ret = glusterd_rpcsvc_options_build (this->options);
        if (ret)
                goto out;
        rpc = rpcsvc_init (this, this->ctx, this->options, 64);
        if (rpc == NULL) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RPC_INIT_FAIL,
                        "failed to init rpc");
                goto out;
        }

        ret = rpcsvc_register_notify (rpc, glusterd_rpcsvc_notify, this);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RPCSVC_REG_NOTIFY_RETURNED,
                        "rpcsvc_register_notify returned %d", ret);
                goto out;
        }

        /* Enable encryption for the TCP listener is management encryption is
         * enabled
         */
        if (this->ctx->secure_mgmt) {
                ret = dict_set_str (this->options,
                                    "transport.socket.ssl-enabled", "on");
                if (ret != 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "failed to set ssl-enabled in dict");
                        goto out;
                }
                /*
                 * With strong authentication, we can afford to allow
                 * privileged operations over TCP.
                 */
                gd_inet_programs[1] = &gd_svc_cli_prog;
                /*
                 * This is the only place where we want secure_srvr to reflect
                 * the management-plane setting.
                 */
                this->ctx->secure_srvr = MGMT_SSL_ALWAYS;
        }

        /*
         * only one (at most a pair - rdma and socket) listener for
         * glusterd1_mop_prog, gluster_pmap_prog and gluster_handshake_prog.
         */

        ret = dict_get_str (this->options, "transport-type", &transport_type);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Failed to get transport type");
                ret = -1;
                goto out;
        }

        total_transport = rpc_transport_count (transport_type);
        if (total_transport <= 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RPC_TRANSPORT_COUNT_GET_FAIL,
                        "failed to get total number of available tranpsorts");
                ret = -1;
                goto out;
        }

        ret = rpcsvc_create_listeners (rpc, this->options, this->name);
        if (ret < 1) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RPC_LISTENER_CREATE_FAIL,
                        "creation of listener failed");
                ret = -1;
                goto out;
        } else if (ret < total_transport) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RPC_LISTENER_CREATE_FAIL,
                        "creation of %d listeners failed, continuing with "
                        "succeeded transport", (total_transport - ret));
        }

        for (i = 0; i < gd_inet_programs_count; i++) {
                ret = glusterd_program_register (this, rpc,
                                                 gd_inet_programs[i]);
                if (ret) {
                        i--;
                        for (; i >= 0; i--)
                                rpcsvc_program_unregister (rpc,
                                                           gd_inet_programs[i]);

                        goto out;
                }
        }

        /*
         * Start a unix domain socket listener just for cli commands This
         * should prevent ports from being wasted by being in TIMED_WAIT when
         * cli commands are done continuously
         */
        uds_rpc = glusterd_init_uds_listener (this);
        if (uds_rpc == NULL) {
                ret = -1;
                goto out;
        }

        conf = GF_CALLOC (1, sizeof (glusterd_conf_t),
                          gf_gld_mt_glusterd_conf_t);
        GF_VALIDATE_OR_GOTO(this->name, conf, out);

        CDS_INIT_LIST_HEAD (&conf->peers);
        CDS_INIT_LIST_HEAD (&conf->volumes);
        CDS_INIT_LIST_HEAD (&conf->snapshots);
        CDS_INIT_LIST_HEAD (&conf->missed_snaps_list);

        pthread_mutex_init (&conf->mutex, NULL);
        conf->rpc = rpc;
        conf->uds_rpc = uds_rpc;
        conf->gfs_mgmt = &gd_brick_prog;
        strncpy (conf->workdir, workdir, PATH_MAX);

        synclock_init (&conf->big_lock, SYNC_LOCK_RECURSIVE);
        pthread_mutex_init (&conf->xprt_lock, NULL);
        INIT_LIST_HEAD (&conf->xprt_list);

        glusterd_friend_sm_init ();
        glusterd_op_sm_init ();
        glusterd_opinfo_init ();
        ret = glusterd_sm_tr_log_init (&conf->op_sm_log,
                                       glusterd_op_sm_state_name_get,
                                       glusterd_op_sm_event_name_get,
                                       GLUSTERD_TR_LOG_SIZE);
        if (ret)
                goto out;

         conf->base_port = GF_IANA_PRIV_PORTS_START;
         if (dict_get_uint32(this->options, "base-port", &conf->base_port) == 0) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "base-port override: %d", conf->base_port);
         }

        /* Set option to run bricks on valgrind if enabled in glusterd.vol */
        conf->valgrind = _gf_false;
        ret = dict_get_str (this->options, "run-with-valgrind", &valgrind_str);
        if (ret < 0) {
                gf_msg_debug (this->name, 0,
                        "cannot get run-with-valgrind value");
        }
        if (valgrind_str) {
                if (gf_string2boolean (valgrind_str, &(conf->valgrind))) {
                        gf_msg (this->name, GF_LOG_WARNING, EINVAL,
                                GD_MSG_INVALID_ENTRY,
                                "run-with-valgrind value not a boolean string");
                }
        }

        /* Store ping-timeout in conf */
        ret = dict_get_int32 (this->options, "ping-timeout",
                              &conf->ping_timeout);
        /* Not failing here since ping-timeout can be optional as well */

        this->private = conf;
        glusterd_mgmt_v3_lock_init ();
        glusterd_txn_opinfo_dict_init ();
        glusterd_svcs_build ();

        /* Make install copies few of the hook-scripts by creating hooks
         * directory. Hence purposefully not doing the check for the presence of
         * hooks directory. Doing so avoids creation of complete hooks directory
         * tree.
         */
        ret = glusterd_hooks_create_hooks_directory (conf->workdir);
        if (-1 == ret) {
                gf_msg (this->name, GF_LOG_CRITICAL, errno,
                        GD_MSG_DIR_OP_FAILED,
                        "Unable to create hooks directory ");
                exit (1);
        }

        CDS_INIT_LIST_HEAD (&conf->mount_specs);

        ret = dict_foreach (this->options, _install_mount_spec, NULL);
        if (ret)
                goto out;
        ret = dict_get_str (this->options, "mountbroker-root",
                            &mountbroker_root);
        if (ret)
                ret = 0;
        else
                ret = check_prepare_mountbroker_root (mountbroker_root);
        if (ret)
                goto out;

        ret = is_upgrade (this->options, &upgrade);
        if (ret)
                goto out;

        ret = is_downgrade (this->options, &downgrade);
        if (ret)
                goto out;

        if (!upgrade && !downgrade) {
                ret = configure_syncdaemon (conf);
                if (ret)
                        goto out;
        }

        /* Restoring op-version needs to be done before initializing the
         * services as glusterd_svc_init_common () invokes
         * glusterd_conn_build_socket_filepath () which uses MY_UUID macro.
         * MY_UUID generates a new uuid if its not been generated and writes it
         * in the info file, Since the op-version is not read yet
         * the default value i.e. 0 will be written for op-version and restore
         * will fail. This is why restoring op-version needs to happen before
         * service initialization
         * */
        ret = glusterd_restore_op_version (this);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_OP_VERS_RESTORE_FAIL,
                        "Failed to restore op_version");
                goto out;
        }

        ret = glusterd_restore ();
        if (ret < 0)
                goto out;

        /* If the peer count is less than 2 then this would be the best time to
         * spawn process/bricks that may need (re)starting since last time
         * (this) glusterd was up. */
        if (glusterd_get_peers_count () < 2)
                glusterd_launch_synctask (glusterd_spawn_daemons, NULL);

        ret = glusterd_options_init (this);
        if (ret < 0)
                goto out;

        ret = glusterd_handle_upgrade_downgrade (this->options, conf, upgrade,
                                                 downgrade);
        if (ret)
                goto out;

        ret = glusterd_hooks_spawn_worker (this);
        if (ret)
                goto out;

        GF_OPTION_INIT ("event-threads", workers, int32, out);
        if (workers > 0 && workers != conf->workers) {
                conf->workers = workers;
                ret = event_reconfigure_threads (this->ctx->event_pool,
                                                 workers);
                if (ret)
                        goto out;
        }

        ret = 0;
out:
        if (ret < 0) {
                if (this->private != NULL) {
                        GF_FREE (this->private);
                        this->private = NULL;
                }

        }

        return ret;
}




/*
 * fini - finish function for glusterd, called before
 *        unloading gluster.
 *
 * @this:
 *
 */
void
fini (xlator_t *this)
{
        if (!this || !this->private)
                goto out;

        glusterd_stop_uds_listener (this); /*stop unix socket rpc*/
        glusterd_stop_listener (this);     /*stop tcp/ip socket rpc*/

#if 0
       /* Running threads might be using these resourses, we have to cancel/stop
        * running threads before deallocating the memeory, but we don't have
        * control over the running threads to do pthread_cancel().
        * So memeory freeing handover to kernel.
        */
        /*TODO: cancel/stop the running threads*/

        GF_FREE (conf->uds_rpc);
        GF_FREE (conf->rpc);
        FREE (conf->pmap);
        if (conf->handle)
                gf_store_handle_destroy (conf->handle);
        glusterd_sm_tr_log_delete (&conf->op_sm_log);
        glusterd_mgmt_v3_lock_fini ();
        glusterd_txn_opinfo_dict_fini ();
        GF_FREE (conf);

        this->private = NULL;
#endif
out:
        return;
}

/*
 * notify - notify function for glusterd
 * @this:
 * @trans:
 * @event:
 *
 */
int
notify (xlator_t *this, int32_t event, void *data, ...)
{
        int          ret = 0;

        switch (event) {
                case GF_EVENT_POLLIN:
                        break;

                case GF_EVENT_POLLERR:
                        break;

                case GF_EVENT_TRANSPORT_CLEANUP:
                        break;

                default:
                        default_notify (this, event, data);
                        break;

        }

        return ret;
}


struct xlator_fops fops;

struct xlator_cbks cbks;

struct xlator_dumpops dumpops = {
        .priv  = glusterd_dump_priv,
};


struct volume_options options[] = {
        { .key   = {"working-directory"},
          .type  = GF_OPTION_TYPE_PATH,
        },
        { .key   = {"transport-type"},
          .type  = GF_OPTION_TYPE_ANY,
        },
        { .key   = {"transport.*"},
          .type  = GF_OPTION_TYPE_ANY,
        },
        { .key   = {"rpc-auth.*"},
          .type  = GF_OPTION_TYPE_ANY,
        },
        { .key   = {"rpc-auth-allow-insecure"},
          .type  = GF_OPTION_TYPE_BOOL,
        },
        { .key  = {"upgrade"},
          .type = GF_OPTION_TYPE_BOOL,
        },
        { .key  = {"downgrade"},
          .type = GF_OPTION_TYPE_BOOL,
        },
        { .key = {"bind-insecure"},
          .type = GF_OPTION_TYPE_BOOL,
        },
        { .key  = {"mountbroker-root"},
          .type = GF_OPTION_TYPE_PATH,
        },
        { .key  = {"mountbroker.*"},
          .type = GF_OPTION_TYPE_ANY,
        },
        { .key  = {"mountbroker-"GEOREP".*"},
          .type = GF_OPTION_TYPE_ANY,
        },
        { .key  = {"mountbroker-"GHADOOP".*"},
          .type = GF_OPTION_TYPE_ANY,
        },
        { .key = {GEOREP"-log-group"},
          .type = GF_OPTION_TYPE_ANY,
        },
        { .key = {"run-with-valgrind"},
          .type = GF_OPTION_TYPE_BOOL,
        },
        { .key = {"server-quorum-type"},
          .type = GF_OPTION_TYPE_STR,
          .value = { "none", "server"},
          .description = "This feature is on the server-side i.e. in glusterd."
                         " Whenever the glusterd on a machine observes that "
                         "the quorum is not met, it brings down the bricks to "
                         "prevent data split-brains. When the network "
                         "connections are brought back up and the quorum is "
                         "restored the bricks in the volume are brought back "
                         "up."
        },
        { .key = {"server-quorum-ratio"},
          .type = GF_OPTION_TYPE_PERCENT,
          .description = "Sets the quorum percentage for the trusted "
          "storage pool."
        },
        { .key = {"glusterd-sockfile"},
          .type = GF_OPTION_TYPE_PATH,
          .description = "The socket file on which glusterd should listen for "
                        "cli requests. Default is "DEFAULT_GLUSTERD_SOCKFILE "."
        },
        { .key = {"base-port"},
          .type = GF_OPTION_TYPE_INT,
          .description = "Sets the base port for portmap query"
        },
        { .key = {"snap-brick-path"},
          .type = GF_OPTION_TYPE_STR,
          .description = "directory where the bricks for the snapshots will be created"
        },
        { .key  = {"ping-timeout"},
          .type = GF_OPTION_TYPE_TIME,
          .min  = 0,
          .max  = 300,
          .default_value = TOSTRING(RPC_DEFAULT_PING_TIMEOUT),
        },
        { .key   = {"event-threads"},
          .type  = GF_OPTION_TYPE_INT,
          .min   = 1,
          .max   = 32,
          .default_value = "2",
          .description = "Specifies the number of event threads to execute "
                         "in parallel. Larger values would help process"
                         " responses faster, depending on available processing"
                         " power. Range 1-32 threads."
        },
        { .key   = {NULL} },
};
