/*
   Copyright (c) 2006-2013 Red Hat, Inc. <http://www.redhat.com>
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
#include <time.h>
#include <grp.h>
#include <sys/uio.h>
#include <sys/resource.h>

#include <libgen.h>
#include "uuid.h"

#include "glusterd.h"
#include "rpcsvc.h"
#include "fnmatch.h"
#include "xlator.h"
#include "call-stub.h"
#include "defaults.h"
#include "list.h"
#include "dict.h"
#include "compat.h"
#include "compat-errno.h"
#include "statedump.h"
#include "glusterd-sm.h"
#include "glusterd-op-sm.h"
#include "glusterd-store.h"
#include "glusterd-hooks.h"
#include "glusterd-utils.h"
#include "common-utils.h"
#include "run.h"

#include "syncop.h"

#include "glusterd-mountbroker.h"

extern struct rpcsvc_program gluster_handshake_prog;
extern struct rpcsvc_program gluster_cli_getspec_prog;
extern struct rpcsvc_program gluster_pmap_prog;
extern glusterd_op_info_t opinfo;
extern struct rpcsvc_program gd_svc_mgmt_prog;
extern struct rpcsvc_program gd_svc_peer_prog;
extern struct rpcsvc_program gd_svc_cli_prog;
extern struct rpcsvc_program gd_svc_cli_prog_ro;
extern struct rpc_clnt_program gd_brick_prog;
extern struct rpcsvc_program glusterd_mgmt_hndsk_prog;

rpcsvc_cbk_program_t glusterd_cbk_prog = {
        .progname  = "Gluster Callback",
        .prognum   = GLUSTER_CBK_PROGRAM,
        .progver   = GLUSTER_CBK_VERSION,
};

struct rpcsvc_program *gd_inet_programs[] = {
        &gd_svc_peer_prog,
        &gd_svc_cli_prog_ro,
        &gd_svc_mgmt_prog,
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
                gf_log (this->name, GF_LOG_INFO,
                        "retrieved UUID: %s", uuid_utoa (priv->uuid));
                return 0;
        }

        ret = glusterd_uuid_generate_save ();

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR,
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

        uuid_generate (priv->uuid);

        gf_log (this->name, GF_LOG_INFO, "generated UUID: %s",
                uuid_utoa (priv->uuid));

        ret = glusterd_store_global_info (this);

        if (ret)
                gf_log (this->name, GF_LOG_ERROR,
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
        if (ret == 0)
                goto out;

        ret = dict_set_str (priv->opts, GLUSTERD_GLOBAL_OPT_VERSION,
                            initial_version);
        if (ret)
                goto out;
        ret = glusterd_store_options (this, priv->opts);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to store version");
                return ret;
        }
out:

        return 0;
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
                                                GF_CBK_FETCHSPEC, NULL, 0);
                }
        }
        pthread_mutex_unlock (&priv->xprt_lock);

        ret = 0;

        return ret;
}

int
glusterd_priv (xlator_t *this)
{
        return 0;
}



int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_gld_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
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
                gf_log ("glusterd", GF_LOG_WARNING,
                        "Calling rpc_notify without initializing");
                goto out;
        }

        this = xl;
        xprt = data;

        priv = this->private;

        switch (event) {
        case RPCSVC_EVENT_ACCEPT:
        {
                INIT_LIST_HEAD (&xprt->list);

                pthread_mutex_lock (&priv->xprt_lock);
                list_add_tail (&xprt->list, &priv->xprt_list);
                pthread_mutex_unlock (&priv->xprt_lock);
                break;
        }
        case RPCSVC_EVENT_DISCONNECT:
        {
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


inline int32_t
glusterd_program_register (xlator_t *this, rpcsvc_t *svc,
                           rpcsvc_program_t *prog)
{
        int32_t ret = -1;

        ret = rpcsvc_program_register (svc, prog);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
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

        gf_log ("", GF_LOG_DEBUG, "listen-backlog value: %d", backlog);

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
                        gf_log ("glusterd", GF_LOG_INFO, GEOREP
                                 " module not installed in the system");
                        *valid_state = 0;
                }
                else {
                        gf_log ("glusterd", GF_LOG_ERROR, GEOREP
                                  " module not working as desired");
                        *valid_state = -1;
                }
                goto out;
        }

        ptr = fgets(buff, sizeof(buff), runner_chio (&runner, STDOUT_FILENO));
        if (ptr) {
                if (!strstr (buff, "gsyncd")) {
                        ret = -1;
                        gf_log ("glusterd", GF_LOG_ERROR, GEOREP" module not "
                                 "working as desired");
                        *valid_state = -1;
                        goto out;
                }
        } else {
                ret = -1;
                gf_log ("glusterd", GF_LOG_ERROR, GEOREP" module not "
                         "working as desired");
                *valid_state = -1;
                goto out;
        }

        ret = 0;
 out:

        runner_end (&runner);

        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;

}

static int
group_write_allow (char *path, gid_t gid)
{
        struct stat st = {0,};
        int ret        = 0;

        ret = stat (path, &st);
        if (ret == -1)
                goto out;
        GF_ASSERT (S_ISDIR (st.st_mode));

        ret = chown (path, -1, gid);
        if (ret == -1)
                goto out;

        ret = chmod (path, (st.st_mode & ~S_IFMT) | S_IWGRP|S_IXGRP|S_ISVTX);

 out:
        if (ret == -1)
                gf_log ("", GF_LOG_CRITICAL,
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
                gf_log ("glusterd", GF_LOG_CRITICAL,
                        "directory path %s/"GEOREP" is longer than PATH_MAX",
                        conf->workdir);
                goto out;
        }

        snprintf (georepdir, PATH_MAX, "%s/"GEOREP, conf->workdir);
        ret = mkdir_p (georepdir, 0777, _gf_true);
        if (-1 == ret) {
                gf_log ("glusterd", GF_LOG_CRITICAL,
                        "Unable to create "GEOREP" directory %s",
                        georepdir);
                goto out;
        }

        if (strlen (DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP) >= PATH_MAX) {
                ret = -1;
                gf_log ("glusterd", GF_LOG_CRITICAL,
                        "directory path "DEFAULT_LOG_FILE_DIRECTORY"/"
                        GEOREP" is longer than PATH_MAX");
                goto out;
        }
        ret = mkdir_p (DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP, 0777, _gf_true);
        if (-1 == ret) {
                gf_log ("glusterd", GF_LOG_CRITICAL,
                        "Unable to create "GEOREP" log directory");
                goto out;
        }

        /* Slave log file directory */
        if (strlen(DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"-slaves") >= PATH_MAX) {
                ret = -1;
                gf_log ("glusterd", GF_LOG_CRITICAL,
                        "directory path "DEFAULT_LOG_FILE_DIRECTORY"/"
                        GEOREP"-slaves"" is longer than PATH_MAX");
                goto out;
        }
        ret = mkdir_p (DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"-slaves", 0777,
                      _gf_true);
        if (-1 == ret) {
                gf_log ("glusterd", GF_LOG_CRITICAL,
                        "Unable to create "GEOREP" slave log directory");
                goto out;
        }

        /* MountBroker log file directory */
        if (strlen(DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"-slaves/mbr") >= PATH_MAX) {
                ret = -1;
                gf_log ("glusterd", GF_LOG_CRITICAL,
                        "directory path "DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP
                        "-slaves/mbr"" is longer than PATH_MAX");
                goto out;
        }
        ret = mkdir_p (DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"-slaves/mbr", 0777,
                      _gf_true);
        if (-1 == ret) {
                gf_log ("glusterd", GF_LOG_CRITICAL,
                        "Unable to create "GEOREP" mountbroker slave log directory");
                goto out;
        }

        ret = dict_get_str (THIS->options, GEOREP"-log-group", &greplg_s);
        if (ret)
                ret = 0;
        else {
                gr = getgrnam (greplg_s);
                if (!gr) {
                        gf_log ("glusterd", GF_LOG_CRITICAL,
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
        gf_log("", GF_LOG_DEBUG, "Returning %d", ret);
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
                         "aux-gfid-mount",
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
        runner_argprintf (&runner, "%s/${mastervol}_${remotehost}_${slavevol}/${eSlave}.pid", georepdir);
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
        runner_argprintf (&runner, "%s/${mastervol}_${remotehost}_${slavevol}/${eSlave}.status", georepdir);
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
                         "aux-gfid-mount",
                         ".", NULL);
        RUN_GSYNCD_CMD;

        /* log-file */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args (&runner,
                         "log-file",
                         DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"-slaves/${session_owner}:${eSlave}.log",
                         ".", NULL);
        RUN_GSYNCD_CMD;

        /* MountBroker log-file */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args (&runner,
                         "log-file-mbr",
                         DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"-slaves/mbr/${session_owner}:${eSlave}.log",
                         ".", NULL);
        RUN_GSYNCD_CMD;

        /* gluster-log-file */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args (&runner,
                         "gluster-log-file",
                         DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"-slaves/${session_owner}:${eSlave}.gluster.log",
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
                ret = fstat (dfd, &st);
        }
        if (ret == -1 || !S_ISDIR (st.st_mode)) {
                gf_log ("", GF_LOG_ERROR,
                        "cannot access mountbroker-root directory %s",
                        mountbroker_root);
                ret = -1;
                goto out;
        }
        if (st.st_uid != 0 ||
            (st.st_mode & (S_IWGRP|S_IWOTH))) {
                gf_log ("", GF_LOG_ERROR,
                        "permissions on mountbroker-root directory %s are "
                        "too liberal", mountbroker_root);
                ret = -1;
                goto out;
        }
        if (!(st.st_mode & (S_IXGRP|S_IXOTH))) {
                gf_log ("", GF_LOG_WARNING,
                        "permissions on mountbroker-root directory %s are "
                        "probably too strict", mountbroker_root);
        }

        dfd0 = dup (dfd);

        for (;;) {
                ret = openat (dfd, "..", O_RDONLY);
                if (ret != -1) {
                        dfd2 = ret;
                        ret = fstat (dfd2, &st2);
                }
                if (ret == -1) {
                        gf_log ("", GF_LOG_ERROR,
                                "error while checking mountbroker-root ancestors "
                                "%d (%s)", errno, strerror (errno));
                        goto out;
                }

                if (st2.st_ino == st.st_ino)
                        break; /* arrived to root */

                if (st2.st_uid != 0 ||
                    ((st2.st_mode & (S_IWGRP|S_IWOTH)) &&
                     !(st2.st_mode & S_ISVTX))) {
                        gf_log ("", GF_LOG_ERROR,
                                "permissions on ancestors of mountbroker-root "
                                "directory are too liberal");
                        ret = -1;
                        goto out;
                }
                if (!(st.st_mode & (S_IXGRP|S_IXOTH))) {
                        gf_log ("", GF_LOG_WARNING,
                                "permissions on ancestors of mountbroker-root "
                                "directory are probably too strict");
                }

                close (dfd);
                dfd = dfd2;
                st = st2;
        }

        ret = mkdirat (dfd0, MB_HIVE, 0711);
        if (ret == -1 && errno == EEXIST)
                ret = 0;
        if (ret != -1)
                ret = fstatat (dfd0, MB_HIVE, &st, AT_SYMLINK_NOFOLLOW);
        if (ret == -1 || st.st_mode != (S_IFDIR|0711)) {
                gf_log ("", GF_LOG_ERROR,
                        "failed to set up mountbroker-root directory %s",
                        mountbroker_root);
                ret = -1;
                goto out;
        }

        ret = 0;

 out:
        if (dfd0 != -1)
                close (dfd0);
        if (dfd != -1)
                close (dfd);
        if (dfd2 != -1)
                close (dfd2);

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

        list_add_tail (&mspec->speclist, &priv->mount_specs);

        return 0;
 err:

        gf_log ("", GF_LOG_ERROR,
                "adding %smount spec failed: label: %s desc: %s",
                georep ? GEOREP" " : "", label, pdesc);

        return -1;
}


static int
gd_default_synctask_cbk (int ret, call_frame_t *frame, void *opaque)
{
        glusterd_conf_t     *priv = THIS->private;
        synclock_unlock (&priv->big_lock);
        return ret;
}

static void
glusterd_launch_synctask (synctask_fn_t fn, void *opaque)
{
        xlator_t        *this = NULL;
        glusterd_conf_t *priv = NULL;
        int             ret   = -1;

        this = THIS;
        priv = this->private;

        synclock_lock (&priv->big_lock);
        ret = synctask_new (this->ctx->env, fn, gd_default_synctask_cbk, NULL,
                            opaque);
        if (ret)
                gf_log (this->name, GF_LOG_CRITICAL, "Failed to spawn bricks"
                        " and other volume related services");
}

int
glusterd_uds_rpcsvc_notify (rpcsvc_t *rpc, void *xl, rpcsvc_event_t event,
                            void *data)
{
        /* glusterd_rpcsvc_notify() does stuff that calls coming in from the
         * unix domain socket don't need. This is just an empty function to be
         * used for the uds listener. This will be used later if required.
         */
        return 0;
}

/* The glusterd unix domain socket listener only listens for cli */
rpcsvc_t *
glusterd_init_uds_listener (xlator_t *this)
{
        int             ret = -1;
        dict_t          *options = NULL;
        rpcsvc_t        *rpc = NULL;
        data_t          *sock_data = NULL;
        char            sockfile[PATH_MAX+1] = {0,};
        int             i = 0;


        GF_ASSERT (this);

        sock_data = dict_get (this->options, "glusterd-sockfile");
        if (!sock_data) {
                strncpy (sockfile, DEFAULT_GLUSTERD_SOCKFILE, PATH_MAX);
        } else {
                strncpy (sockfile, sock_data->data, PATH_MAX);
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

        ret = rpcsvc_register_notify (rpc, glusterd_uds_rpcsvc_notify,
                                      this);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Failed to register notify function");
                goto out;
        }

        ret = rpcsvc_create_listeners (rpc, options, this->name);
        if (ret != 1) {
                gf_log (this->name, GF_LOG_DEBUG, "Failed to create listener");
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
                gf_log (this->name, GF_LOG_ERROR, "Failed to start glusterd "
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

        GF_ASSERT (this);
        conf = this->private;

        (void) rpcsvc_program_unregister (conf->uds_rpc, &gd_svc_cli_prog);
        (void) rpcsvc_program_unregister (conf->uds_rpc, &gluster_handshake_prog);

        list_for_each_entry_safe (listener, next, &conf->uds_rpc->listeners,
                                  list) {
                rpcsvc_listener_destroy (listener);
        }

        (void) rpcsvc_unregister_notify (conf->uds_rpc, glusterd_rpcsvc_notify,
                                         this);

        unlink (DEFAULT_GLUSTERD_SOCKFILE);

        GF_FREE (conf->uds_rpc);
        conf->uds_rpc = NULL;

        return;
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
        int32_t            ret               = -1;
        rpcsvc_t          *rpc               = NULL;
        rpcsvc_t          *uds_rpc           = NULL;
        glusterd_conf_t   *conf              = NULL;
        data_t            *dir_data          = NULL;
        struct stat        buf               = {0,};
        char               storedir [PATH_MAX] = {0,};
        char               workdir [PATH_MAX] = {0,};
        char               hooks_dir [PATH_MAX] = {0,};
        char               cmd_log_filename [PATH_MAX] = {0,};
        int                first_time        = 0;
        char              *mountbroker_root  = NULL;
        int                i                 = 0;
        char              *valgrind_str      = NULL;

        dir_data = dict_get (this->options, "working-directory");

        if (!dir_data) {
                //Use default working dir
                strncpy (workdir, GLUSTERD_DEFAULT_WORKDIR, PATH_MAX);
        } else {
                strncpy (workdir, dir_data->data, PATH_MAX);
        }

        ret = stat (workdir, &buf);
        if ((ret != 0) && (ENOENT != errno)) {
                gf_log (this->name, GF_LOG_ERROR,
                        "stat fails on %s, exiting. (errno = %d)",
                        workdir, errno);
                exit (1);
        }

        if ((!ret) && (!S_ISDIR(buf.st_mode))) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "Provided working area %s is not a directory,"
                        "exiting", workdir);
                exit (1);
        }


        if ((-1 == ret) && (ENOENT == errno)) {
                ret = mkdir (workdir, 0777);

                if (-1 == ret) {
                        gf_log (this->name, GF_LOG_CRITICAL,
                                "Unable to create directory %s"
                                " ,errno = %d", workdir, errno);
                        exit (1);
                }

                first_time = 1;
        }

        setenv ("GLUSTERD_WORKING_DIR", workdir, 1);
        gf_log (this->name, GF_LOG_INFO, "Using %s as working directory",
                workdir);

        snprintf (cmd_log_filename, PATH_MAX,"%s/.cmd_log_history",
                  DEFAULT_LOG_FILE_DIRECTORY);
        ret = gf_cmd_log_init (cmd_log_filename);

        if (ret == -1) {
                gf_log ("this->name", GF_LOG_CRITICAL,
                        "Unable to create cmd log file %s", cmd_log_filename);
                exit (1);
        }

        snprintf (storedir, PATH_MAX, "%s/vols", workdir);

        ret = mkdir (storedir, 0777);

        if ((-1 == ret) && (errno != EEXIST)) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "Unable to create volume directory %s"
                        " ,errno = %d", storedir, errno);
                exit (1);
        }

        snprintf (storedir, PATH_MAX, "%s/peers", workdir);

        ret = mkdir (storedir, 0777);

        if ((-1 == ret) && (errno != EEXIST)) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "Unable to create peers directory %s"
                        " ,errno = %d", storedir, errno);
                exit (1);
        }

        snprintf (storedir, PATH_MAX, "%s/bricks", DEFAULT_LOG_FILE_DIRECTORY);
        ret = mkdir (storedir, 0777);
        if ((-1 == ret) && (errno != EEXIST)) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "Unable to create logs directory %s"
                        " ,errno = %d", storedir, errno);
                exit (1);
        }

        snprintf (storedir, PATH_MAX, "%s/nfs", workdir);
        ret = mkdir (storedir, 0777);
        if ((-1 == ret) && (errno != EEXIST)) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "Unable to create nfs directory %s"
                        " ,errno = %d", storedir, errno);
                exit (1);
        }

        snprintf (storedir, PATH_MAX, "%s/glustershd", workdir);
        ret = mkdir (storedir, 0777);
        if ((-1 == ret) && (errno != EEXIST)) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "Unable to create glustershd directory %s"
                        " ,errno = %d", storedir, errno);
                exit (1);
        }

        snprintf (storedir, PATH_MAX, "%s/quotad", workdir);
        ret = mkdir (storedir, 0777);
        if ((-1 == ret) && (errno != EEXIST)) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "Unable to create quotad directory %s"
                        " ,errno = %d", storedir, errno);
                exit (1);
        }

        snprintf (storedir, PATH_MAX, "%s/groups", workdir);
        ret = mkdir (storedir, 0777);
        if ((-1 == ret) && (errno != EEXIST)) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "Unable to create glustershd directory %s"
                        " ,errno = %d", storedir, errno);
                exit (1);
        }

        ret = glusterd_rpcsvc_options_build (this->options);
        if (ret)
                goto out;
        rpc = rpcsvc_init (this, this->ctx, this->options, 64);
        if (rpc == NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to init rpc");
                goto out;
        }

        ret = rpcsvc_register_notify (rpc, glusterd_rpcsvc_notify, this);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpcsvc_register_notify returned %d", ret);
                goto out;
        }

        /*
         * only one (atmost a pair - rdma and socket) listener for
         * glusterd1_mop_prog, gluster_pmap_prog and gluster_handshake_prog.
         */
        ret = rpcsvc_create_listeners (rpc, this->options, this->name);
        if (ret < 1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "creation of listener failed");
                ret = -1;
                goto out;
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

        /* Start a unix domain socket listener just for cli commands
         * This should prevent ports from being wasted by being in TIMED_WAIT
         * when cli commands are done continuously
         */
        uds_rpc = glusterd_init_uds_listener (this);
        if (uds_rpc == NULL) {
                ret = -1;
                goto out;
        }

        conf = GF_CALLOC (1, sizeof (glusterd_conf_t),
                          gf_gld_mt_glusterd_conf_t);
        GF_VALIDATE_OR_GOTO(this->name, conf, out);

        conf->shd = GF_CALLOC (1, sizeof (nodesrv_t), gf_gld_mt_nodesrv_t);
        GF_VALIDATE_OR_GOTO(this->name, conf->shd, out);
        conf->nfs = GF_CALLOC (1, sizeof (nodesrv_t), gf_gld_mt_nodesrv_t);
        GF_VALIDATE_OR_GOTO(this->name, conf->nfs, out);
        conf->quotad = GF_CALLOC (1, sizeof (nodesrv_t),
                               gf_gld_mt_nodesrv_t);
        GF_VALIDATE_OR_GOTO(this->name, conf->quotad, out);

        INIT_LIST_HEAD (&conf->peers);
        INIT_LIST_HEAD (&conf->volumes);
        pthread_mutex_init (&conf->mutex, NULL);
        conf->rpc = rpc;
        conf->uds_rpc = uds_rpc;
        conf->gfs_mgmt = &gd_brick_prog;
        strncpy (conf->workdir, workdir, PATH_MAX);

        synclock_init (&conf->big_lock);
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
                 gf_log (this->name, GF_LOG_INFO,
                         "base-port override: %d", conf->base_port);
         }

        /* Set option to run bricks on valgrind if enabled in glusterd.vol */
        conf->valgrind = _gf_false;
        ret = dict_get_str (this->options, "run-with-valgrind", &valgrind_str);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "cannot get run-with-valgrind value");
        }
        if (valgrind_str) {
                if (gf_string2boolean (valgrind_str, &(conf->valgrind))) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "run-with-valgrind value not a boolean string");
                }
        }

        this->private = conf;
        (void) glusterd_nodesvc_set_online_status ("glustershd", _gf_false);

        GLUSTERD_GET_HOOKS_DIR (hooks_dir, GLUSTERD_HOOK_VER, conf);
        if (stat (hooks_dir, &buf)) {
                ret = glusterd_hooks_create_hooks_directory (conf->workdir);
                if (-1 == ret) {
                        gf_log (this->name, GF_LOG_CRITICAL,
                                "Unable to create hooks directory ");
                        exit (1);
                }
        }

        INIT_LIST_HEAD (&conf->mount_specs);

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

        ret = configure_syncdaemon (conf);
        if (ret)
                goto out;

        ret = glusterd_restore ();
        if (ret < 0)
                goto out;

        /* If there are no 'friends', this would be the best time to
         * spawn process/bricks that may need (re)starting since last
         * time (this) glusterd was up.*/

        if (list_empty (&conf->peers)) {
                glusterd_launch_synctask (glusterd_spawn_daemons, NULL);
        }
        ret = glusterd_options_init (this);
        if (ret < 0)
                goto out;

        ret = glusterd_handle_upgrade_downgrade (this->options, conf);
        if (ret)
                goto out;

        ret = glusterd_hooks_spawn_worker (this);
        if (ret)
                goto out;

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
        glusterd_conf_t *conf = NULL;
        if (!this || !this->private)
                goto out;

        conf = this->private;

        glusterd_stop_uds_listener (this);

        FREE (conf->pmap);
        if (conf->handle)
                gf_store_handle_destroy (conf->handle);
        glusterd_sm_tr_log_delete (&conf->op_sm_log);
        GF_FREE (conf);

        this->private = NULL;
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
        .priv  = glusterd_priv,
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
        { .key   = {NULL} },
};
