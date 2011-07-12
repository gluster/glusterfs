/*
  Copyright (c) 2006-2010 Gluster, Inc. <http://www.gluster.com>
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
#include <time.h>
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
#include "glusterd-utils.h"
#include "common-utils.h"
#include "run.h"

static uuid_t glusterd_uuid;
extern struct rpcsvc_program glusterd1_mop_prog;
extern struct rpcsvc_program gd_svc_mgmt_prog;
extern struct rpcsvc_program gd_svc_cli_prog;
extern struct rpcsvc_program gluster_handshake_prog;
extern struct rpcsvc_program gluster_pmap_prog;
extern glusterd_op_info_t opinfo;
extern struct rpc_clnt_program glusterd_glusterfs_3_1_mgmt_prog;

rpcsvc_cbk_program_t glusterd_cbk_prog = {
        .progname  = "Gluster Callback",
        .prognum   = GLUSTER_CBK_PROGRAM,
        .progver   = GLUSTER_CBK_VERSION,
};


static int
glusterd_opinfo_init ()
{
        int32_t ret = -1;

        ret = pthread_mutex_init (&opinfo.lock, NULL);

        return ret;
}

static int
glusterd_uuid_init (int flag)
{
        int             ret = -1;
        glusterd_conf_t *priv = NULL;

        priv = THIS->private;

        if (!flag) {
                ret = glusterd_retrieve_uuid ();
                if (!ret) {
                        uuid_copy (glusterd_uuid, priv->uuid);
                        gf_log ("glusterd", GF_LOG_INFO,
                                "retrieved UUID: %s", uuid_utoa (priv->uuid));
                        return 0;
                }
        }

        uuid_generate (glusterd_uuid);

        gf_log ("glusterd", GF_LOG_INFO,
                        "generated UUID: %s", uuid_utoa (glusterd_uuid));
        uuid_copy (priv->uuid, glusterd_uuid);

        ret = glusterd_store_uuid ();

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR,
                          "Unable to store generated UUID");
                return ret;
        }

        return 0;
}

int
glusterd_fetchspec_notify (xlator_t *this)
{
        int              ret   = -1;
        glusterd_conf_t *priv  = NULL;
        rpc_transport_t *trans = NULL;

        priv = this->private;

        list_for_each_entry (trans, &priv->xprt_list, list) {
                rpcsvc_callback_submit (priv->rpc, trans, &glusterd_cbk_prog,
                                        GF_CBK_FETCHSPEC, NULL, 0);
        }

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

                list_add_tail (&xprt->list, &priv->xprt_list);
                break;
        }
        case RPCSVC_EVENT_DISCONNECT:
        {
                list_del (&xprt->list);
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

        if (!dict_get (options, "rpc-auth-allow-insecure")) {
                ret = dict_set_str (options, "rpc-auth-allow-insecure", "on");
                if (ret)
                        goto out;
        }

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

/* defined in usterd-utils.c -- no
 * glusterd header where it would be
 * appropriate to put to, and too
 * accidental routine to place in
 * libglusterfs.
 *
 * (Indeed, XXX: we'd rather need a general
 * "mkdir -p" like routine in
 * libglusterfs)
 */
extern int mkdir_if_missing (char *path);

#if SYNCDAEMON_COMPILE
static int
glusterd_check_gsync_present ()
{
        char                buff[PATH_MAX] = {0, };
        runner_t            runner = {0,};
        char               *ptr = NULL;
        int                 ret = 0;

        runinit (&runner);
        runner_add_args (&runner, GSYNCD_PREFIX"/gsyncd", "--version", NULL);
        runner_redir (&runner, STDOUT_FILENO, RUN_PIPE);
        ret = runner_start (&runner);
        if (ret == -1)
                goto out;

        ptr = fgets(buff, sizeof(buff), runner_chio (&runner, STDOUT_FILENO));
        if (ptr) {
                if (!strstr (buff, "gsyncd")) {
                        ret = -1;
                        goto out;
                }
        } else {
                ret = -1;
                goto out;
        }
        ret = 0;
 out:

        if (ret == 0)
                ret = runner_end (&runner);
        if (ret == -1)
                        gf_log ("", GF_LOG_INFO, "geo-replication module not"
                                " installed in the system");

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;

}

int
glusterd_crt_georep_folders (char *georepdir, glusterd_conf_t *conf)
{
        int ret = 0;

        GF_ASSERT (georepdir);
        GF_ASSERT (conf);

        if (strlen (conf->workdir)+2 > PATH_MAX-strlen(GEOREP)) {
                ret = -1;
                gf_log ("glusterd", GF_LOG_CRITICAL,
                        "Unable to create "GEOREP" directory %s",
                        georepdir);
                goto out;
        }

        snprintf (georepdir, PATH_MAX, "%s/"GEOREP, conf->workdir);
        ret = mkdir_if_missing (georepdir);
        if (-1 == ret) {
                gf_log ("glusterd", GF_LOG_CRITICAL,
                        "Unable to create "GEOREP" directory %s",
                        georepdir);
                goto out;
        }

        if (strlen (DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP) >= PATH_MAX) {
                ret = -1;
                gf_log ("glusterd", GF_LOG_CRITICAL,
                        "Unable to create "GEOREP" directory %s",
                        georepdir);
                goto out;
        }
        ret = mkdir_if_missing (DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP);
        if (-1 == ret) {
                gf_log ("glusterd", GF_LOG_CRITICAL,
                        "Unable to create "GEOREP" log directory");
                goto out;
        }

        if (strlen(DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"-slaves") >= PATH_MAX) {
                ret = -1;
                gf_log ("glusterd", GF_LOG_CRITICAL,
                        "Unable to create "GEOREP" directory %s",
                        georepdir);
                goto out;
        }
        ret = mkdir_if_missing (DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"-slaves");
        if (-1 == ret) {
                gf_log ("glusterd", GF_LOG_CRITICAL,
                        "Unable to create "GEOREP" slave log directory");
                goto out;
        }
        ret = 0;
 out:
        gf_log("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;

}
#endif

static void
runinit_gsyncd_setrx (runner_t *runner, glusterd_conf_t *conf)
{
        runinit (runner);
        runner_add_args (runner, GSYNCD_PREFIX"/gsyncd", "-c", NULL);
        runner_argprintf (runner, "%s/"GSYNC_CONF,conf->workdir);
        runner_add_arg (runner, "--config-set-rx");
}

static int
configure_syncdaemon (glusterd_conf_t *conf)
{
        int ret = 0;
#if SYNCDAEMON_COMPILE
        runner_t runner = {0,};
        char georepdir[PATH_MAX] = {0,};

        ret = setenv ("_GLUSTERD_CALLED_", "1", 1);
        if (ret < 0) {
                ret = 0;
                goto out;
        }

        ret = glusterd_check_gsync_present ();
        if (-1 == ret) {
                ret = 0;
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
        ret = runner_run (&runner);
        if (ret)
                goto out;

        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args (&runner, "remote-gsyncd",
                         "/usr/local/libexec/glusterfs/gsyncd", ".", "^ssh:", NULL);
        ret = runner_run (&runner);
        if (ret)
                goto out;

        /* gluster-command */
        /* XXX $sbindir should be used (throughout the codebase) */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args (&runner, "gluster-command",
                         GFS_PREFIX"/sbin/glusterfs "
                          "--xlator-option *-dht.assert-no-child-down=true",
                         ".", ".", NULL);
        ret = runner_run (&runner);
        if (ret)
                goto out;

        /* ssh-command */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_arg (&runner, "ssh-command");
        runner_argprintf (&runner,
                          "ssh -oPasswordAuthentication=no "
                           "-oStrictHostKeyChecking=no "
                           "-i %s/secret.pem", georepdir);
        runner_add_args (&runner, ".", ".", NULL);
        ret = runner_run (&runner);
        if (ret)
                goto out;

        /* pid-file */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_arg (&runner, "pid-file");
        runner_argprintf (&runner, "%s/${mastervol}/${eSlave}.pid", georepdir);
        runner_add_args (&runner, ".", ".", NULL);
        ret = runner_run (&runner);
        if (ret)
                goto out;

        /* state-file */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_arg (&runner, "state-file");
        runner_argprintf (&runner, "%s/${mastervol}/${eSlave}.status", georepdir);
        runner_add_args (&runner, ".", ".", NULL);
        ret = runner_run (&runner);
        if (ret)
                goto out;

        /* log-file */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args (&runner,
                         "log-file",
                         DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"/${mastervol}/${eSlave}.log",
                         ".", ".", NULL);
        ret = runner_run (&runner);
        if (ret)
                goto out;

        /* gluster-log-file */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args (&runner,
                         "gluster-log-file",
                         DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"/${mastervol}/${eSlave}.gluster.log",
                         ".", ".", NULL);
        ret = runner_run (&runner);
        if (ret)
                goto out;

        /************
         * slave pre-configuration
         ************/

        /* gluster-command */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args (&runner, "gluster-command",
                         GFS_PREFIX"/sbin/glusterfs "
                          "--xlator-option *-dht.assert-no-child-down=true",
                         ".", NULL);
        ret = runner_run (&runner);
        if (ret)
                goto out;

        /* log-file */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args (&runner,
                         "log-file",
                         DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"-slaves/${session_owner}:${eSlave}.log",
                         ".", NULL);
        ret = runner_run (&runner);
        if (ret)
                goto out;

        /* gluster-log-file */
        runinit_gsyncd_setrx (&runner, conf);
        runner_add_args (&runner,
                         "gluster-log-file",
                         DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"-slaves/${session_owner}:${eSlave}.gluster.log",
                         ".", NULL);
        ret = runner_run (&runner);
        if (ret)
                goto out;

 out:
#else
        (void)conf;
#endif
        return ret ? -1 : 0;
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
        glusterd_conf_t   *conf              = NULL;
        data_t            *dir_data          = NULL;
        struct stat        buf               = {0,};
        char               voldir [PATH_MAX] = {0,};
        char               dirname [PATH_MAX];
        char               cmd_log_filename [PATH_MAX] = {0,};
        int                first_time        = 0;

        dir_data = dict_get (this->options, "working-directory");

        if (!dir_data) {
                //Use default working dir
                strncpy (dirname, GLUSTERD_DEFAULT_WORKDIR, PATH_MAX);
        } else {
                strncpy (dirname, dir_data->data, PATH_MAX);
        }

        ret = stat (dirname, &buf);
        if ((ret != 0) && (ENOENT != errno)) {
                gf_log (this->name, GF_LOG_ERROR,
                        "stat fails on %s, exiting. (errno = %d)",
			dirname, errno);
                exit (1);
        }

        if ((!ret) && (!S_ISDIR(buf.st_mode))) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "Provided working area %s is not a directory,"
                        "exiting", dirname);
                exit (1);
        }


        if ((-1 == ret) && (ENOENT == errno)) {
                ret = mkdir (dirname, 0777);

                if (-1 == ret) {
                        gf_log (this->name, GF_LOG_CRITICAL,
                                "Unable to create directory %s"
                                " ,errno = %d", dirname, errno);
                        exit (1);
                }
                first_time = 1;
        }

        gf_log (this->name, GF_LOG_INFO, "Using %s as working directory",
                dirname);

        snprintf (cmd_log_filename, PATH_MAX,"%s/.cmd_log_history",
                  DEFAULT_LOG_FILE_DIRECTORY);
        ret = gf_cmd_log_init (cmd_log_filename);

        if (ret == -1) {
                gf_log ("this->name", GF_LOG_CRITICAL,
                        "Unable to create cmd log file %s", cmd_log_filename);
                exit (1);
        }

        snprintf (voldir, PATH_MAX, "%s/vols", dirname);

        ret = mkdir (voldir, 0777);

        if ((-1 == ret) && (errno != EEXIST)) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "Unable to create volume directory %s"
                        " ,errno = %d", voldir, errno);
                exit (1);
        }

        snprintf (voldir, PATH_MAX, "%s/peers", dirname);

        ret = mkdir (voldir, 0777);

        if ((-1 == ret) && (errno != EEXIST)) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "Unable to create peers directory %s"
                        " ,errno = %d", voldir, errno);
                exit (1);
        }

        snprintf (voldir, PATH_MAX, "%s/bricks", DEFAULT_LOG_FILE_DIRECTORY);
        ret = mkdir (voldir, 0777);
        if ((-1 == ret) && (errno != EEXIST)) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "Unable to create logs directory %s"
                        " ,errno = %d", voldir, errno);
                exit (1);
        }

        snprintf (voldir, PATH_MAX, "%s/nfs", dirname);
        ret = mkdir (voldir, 0777);
        if ((-1 == ret) && (errno != EEXIST)) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "Unable to create nfs directory %s"
                        " ,errno = %d", voldir, errno);
                exit (1);
        }

        ret = glusterd_rpcsvc_options_build (this->options);
        if (ret)
                goto out;
        rpc = rpcsvc_init (this->ctx, this->options);
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

        ret = glusterd_program_register (this, rpc, &glusterd1_mop_prog);
        if (ret) {
                goto out;
        }

        ret = glusterd_program_register (this, rpc, &gd_svc_cli_prog);
        if (ret) {
                rpcsvc_program_unregister (rpc, &glusterd1_mop_prog);
                goto out;
        }

        ret = glusterd_program_register (this, rpc, &gd_svc_mgmt_prog);
        if (ret) {
                rpcsvc_program_unregister (rpc, &glusterd1_mop_prog);
                rpcsvc_program_unregister (rpc, &gd_svc_cli_prog);
                goto out;
        }

        ret = glusterd_program_register (this, rpc, &gluster_pmap_prog);
        if (ret) {
                rpcsvc_program_unregister (rpc, &glusterd1_mop_prog);
                rpcsvc_program_unregister (rpc, &gd_svc_cli_prog);
                rpcsvc_program_unregister (rpc, &gd_svc_mgmt_prog);
                goto out;
        }

        ret = glusterd_program_register (this, rpc, &gluster_handshake_prog);
        if (ret) {
                rpcsvc_program_unregister (rpc, &glusterd1_mop_prog);
                rpcsvc_program_unregister (rpc, &gluster_pmap_prog);
                rpcsvc_program_unregister (rpc, &gd_svc_cli_prog);
                rpcsvc_program_unregister (rpc, &gd_svc_mgmt_prog);
                goto out;
        }

        conf = GF_CALLOC (1, sizeof (glusterd_conf_t),
                          gf_gld_mt_glusterd_conf_t);
        GF_VALIDATE_OR_GOTO(this->name, conf, out);
        INIT_LIST_HEAD (&conf->peers);
        INIT_LIST_HEAD (&conf->volumes);
        pthread_mutex_init (&conf->mutex, NULL);
        conf->rpc = rpc;
        conf->gfs_mgmt = &glusterd_glusterfs_3_1_mgmt_prog;
        strncpy (conf->workdir, dirname, PATH_MAX);

        INIT_LIST_HEAD (&conf->xprt_list);
        ret = glusterd_sm_tr_log_init (&conf->op_sm_log,
                                       glusterd_op_sm_state_name_get,
                                       glusterd_op_sm_event_name_get,
                                       GLUSTERD_TR_LOG_SIZE);
        if (ret)
                goto out;

        this->private = conf;
        //this->ctx->top = this;

        ret = glusterd_uuid_init (first_time);
        if (ret < 0)
                goto out;

        ret = configure_syncdaemon (conf);
        if (ret)
                goto out;

        ret = glusterd_restore ();
        if (ret < 0)
                goto out;

        glusterd_friend_sm_init ();
        glusterd_op_sm_init ();
        glusterd_opinfo_init ();

        ret = glusterd_handle_upgrade_downgrade (this->options, conf);
        if (ret)
                goto out;

        glusterd_restart_bricks (conf);
        ret = glusterd_restart_gsyncds (conf);
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
        if (conf->pmap)
                FREE (conf->pmap);
        if (conf->handle)
                glusterd_store_handle_destroy (conf->handle);
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


struct xlator_fops fops = {
};

struct xlator_cbks cbks = {
};

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
        { .key   = {NULL} },
};
