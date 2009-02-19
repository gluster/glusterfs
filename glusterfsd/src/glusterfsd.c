/*
   Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <netdb.h>
#include <signal.h>
#include <libgen.h>

#include <sys/utsname.h>

#include <stdint.h>
#include <pthread.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#ifdef HAVE_MALLOC_STATS
#ifdef DEBUG
#include <mcheck.h>
#endif
#endif

#include "xlator.h"
#include "glusterfs.h"
#include "compat.h"
#include "logging.h"
#include "dict.h"
#include "protocol.h"
#include "list.h"
#include "timer.h"
#include "glusterfsd.h"
#include "stack.h"
#include "revision.h"
#include "common-utils.h"
#include "event.h"

#include <fnmatch.h>

/* using argp for command line parsing */
static char gf_doc[] = "";
static char argp_doc[] = "--volfile-server=SERVER [MOUNT-POINT]\n"       \
                         "--volfile=VOLFILE [MOUNT-POINT]";
const char *argp_program_version = "" \
            PACKAGE_NAME" "PACKAGE_VERSION" built on "__DATE__" "__TIME__ \
            "\nRepository revision: " GLUSTERFS_REPOSITORY_REVISION "\n"  \
            "Copyright (c) 2006, 2007, 2008 Z RESEARCH Inc. "             \
            "<http://www.zresearch.com>\n"                                \
            "GlusterFS comes with ABSOLUTELY NO WARRANTY.\n"              \
            "You may redistribute copies of GlusterFS under the terms of "\
            "the GNU General Public License.";
const char *argp_program_bug_address = "<" PACKAGE_BUGREPORT ">";

error_t parse_opts (int32_t key, char *arg, struct argp_state *_state);

static struct argp_option gf_options[] = {
 	{0, 0, 0, 0, "Basic options:"},
 	{"volfile-server", ARGP_VOLFILE_SERVER_KEY, "SERVER", 0, 
 	 "Server to get the volume file from.  This option overrides "
	 "--volfile option"},
 	{"volfile", ARGP_VOLUME_FILE_KEY, "VOLFILE", 0, 
 	 "File to use as VOLUME_FILE [default: "DEFAULT_CLIENT_VOLUME_FILE" or "
	 DEFAULT_SERVER_VOLUME_FILE"]"},
	{"spec-file", ARGP_VOLUME_FILE_KEY, "VOLFILE", OPTION_HIDDEN, 
	 "File to use as VOLFILE [default : "DEFAULT_CLIENT_VOLUME_FILE" or "
	 DEFAULT_SERVER_VOLUME_FILE"]"},
 	{"log-level", ARGP_LOG_LEVEL_KEY, "LOGLEVEL", 0, 
 	 "Logging severity.  Valid options are DEBUG, NORMAL, WARNING, ERROR, "
	 "CRITICAL and NONE [default: NORMAL]"},
 	{"log-file", ARGP_LOG_FILE_KEY, "LOGFILE", 0, 
 	 "File to use for logging [default: " 
	 DEFAULT_LOG_FILE_DIRECTORY "/" PACKAGE_NAME ".log" "]"},
 	
 	{0, 0, 0, 0, "Advanced Options:"},
 	{"volfile-server-port", ARGP_VOLFILE_SERVER_PORT_KEY, "PORT", 0, 
 	 "Listening port number of volfile server"},
 	{"volfile-server-transport", ARGP_VOLFILE_SERVER_TRANSPORT_KEY, 
	 "TRANSPORT", 0, 
 	 "Transport type to get volfile from server [default: socket]"},
 	{"volfile-id", ARGP_VOLFILE_ID_KEY, "KEY", 0, 
 	 "'key' of the volfile to be fetched from server"},
 	{"pid-file", ARGP_PID_FILE_KEY, "PIDFILE", 0, 
 	 "File to use as pid file"},
 	{"no-daemon", ARGP_NO_DAEMON_KEY, 0, 0,
 	 "Run in foreground"},
 	{"run-id", ARGP_RUN_ID_KEY, "RUN-ID", OPTION_HIDDEN,
 	 "Run ID for the process, used by scripts to keep track of process "
	 "they started, defaults to none"},
 	{"debug", ARGP_DEBUG_KEY, 0, 0, 
 	 "Run in debug mode.  This option sets --no-daemon, --log-level "
	 "to DEBUG and --log-file to console"},
 	{"volume-name", ARGP_VOLUME_NAME_KEY, "VOLUME-NAME", 0,
 	 "Volume name to be used for MOUNT-POINT [default: top most volume "
	 "in VOLFILE]"},
 	{"xlator-option", ARGP_XLATOR_OPTION_KEY,"VOLUME-NAME.OPTION=VALUE", 0,
	 "Add/override a translator option for a volume with specified value"},
	
 	{0, 0, 0, 0, "Fuse options:"},
 	{"disable-direct-io-mode", ARGP_DISABLE_DIRECT_IO_MODE_KEY, 0, 0, 
 	 "Disable direct I/O mode in fuse kernel module"},
 	{"entry-timeout", ARGP_ENTRY_TIMEOUT_KEY, "SECONDS", 0, 
 	 "Set entry timeout to SECONDS in fuse kernel module [default: 1]"},
 	{"attribute-timeout", ARGP_ATTRIBUTE_TIMEOUT_KEY, "SECONDS", 0, 
 	 "Set attribute timeout to SECONDS for inodes in fuse kernel module "
	 "[default: 1]"},
#ifdef GF_DARWIN_HOST_OS
 	{"non-local", ARGP_NON_LOCAL_KEY, 0, 0, 
 	 "Mount the macfuse volume without '-o local' option"},
#endif
 	{0, 0, 0, 0, "Miscellaneous Options:"},
 	{0, }
};


static struct argp argp = { gf_options, parse_opts, argp_doc, gf_doc };


static void 
_gf_dump_details (int argc, char **argv)
{
        extern FILE *gf_log_logfile;
        int          i = 0;
        char         timestr[256];
        time_t       utime = 0;
        struct tm   *tm = NULL;
	pid_t        mypid = 0;
	struct utsname uname_buf = {{0, }, };
	int            uname_ret = -1;

	utime = time (NULL);
	tm    = localtime (&utime);
	mypid = getpid ();
	uname_ret   = uname (&uname_buf);

        /* Which TLA? What time? */
        strftime (timestr, 256, "%Y-%m-%d %H:%M:%S", tm); 
	fprintf (gf_log_logfile, 
		 "========================================"
		 "========================================\n");
        fprintf (gf_log_logfile, "Version      : %s %s built on %s %s\n", 
                 PACKAGE_NAME, PACKAGE_VERSION, __DATE__, __TIME__);
        fprintf (gf_log_logfile, "TLA Revision : %s\n", 
                 GLUSTERFS_REPOSITORY_REVISION);
        fprintf (gf_log_logfile, "Starting Time: %s\n", timestr);
        fprintf (gf_log_logfile, "Command line : ");
        for (i = 0; i < argc; i++) {
                fprintf (gf_log_logfile, "%s ", argv[i]);
        }

	fprintf (gf_log_logfile, "\nPID          : %d\n", mypid);
	
	if (uname_ret == 0) {
		fprintf (gf_log_logfile, "System name  : %s\n", uname_buf.sysname);
		fprintf (gf_log_logfile, "Nodename     : %s\n", uname_buf.nodename);
		fprintf (gf_log_logfile, "Kernel Release : %s\n", uname_buf.release);
		fprintf (gf_log_logfile, "Hardware Identifier: %s\n", uname_buf.machine);
	}


        fprintf (gf_log_logfile, "\n");
        fflush (gf_log_logfile);
}



static xlator_t *
_add_fuse_mount (xlator_t *graph)
{
	int              ret = 0;
	cmd_args_t      *cmd_args = NULL;
	xlator_t        *top = NULL;
	glusterfs_ctx_t *ctx = NULL;
	xlator_list_t   *xlchild = NULL;
	
	ctx = graph->ctx;
	cmd_args = &ctx->cmd_args;
	
	xlchild = CALLOC (sizeof (*xlchild), 1);
	ERR_ABORT (xlchild);
	xlchild->xlator = graph;
	
	top = CALLOC (1, sizeof (*top));
	top->name = strdup ("fuse");
	if (xlator_set_type (top, ZR_XLATOR_FUSE) == -1) {
		fprintf (stderr, 
			 "MOUNT-POINT %s initialization failed", 
			 cmd_args->mount_point);
		gf_log ("glusterfs", GF_LOG_ERROR, 
			"MOUNT-POINT %s initialization failed", 
			cmd_args->mount_point);
		return NULL;
	}
	top->children = xlchild;
	top->ctx      = graph->ctx;
	top->next     = graph;
	top->options  = get_new_dict ();

	ret = dict_set_static_ptr (top->options, ZR_MOUNTPOINT_OPT,
				   cmd_args->mount_point);
	if (ret < 0) {
		gf_log ("glusterfs", GF_LOG_DEBUG,
			"failed to set mount-point to options dictionary");
	} 

	if (cmd_args->fuse_attribute_timeout)
		ret = dict_set_double (top->options, ZR_ATTR_TIMEOUT_OPT, 
				       cmd_args->fuse_attribute_timeout);
	if (cmd_args->fuse_entry_timeout)
		ret = dict_set_double (top->options, ZR_ENTRY_TIMEOUT_OPT, 
				       cmd_args->fuse_entry_timeout);

#ifdef GF_DARWIN_HOST_OS 
	/* On Darwin machines, O_APPEND is not handled, 
	 * which may corrupt the data 
	 */
	if (cmd_args->fuse_direct_io_mode_flag == _gf_true) {
		gf_log ("glusterfs", GF_LOG_DEBUG, 
			 "'direct-io-mode' in fuse causes data corruption "
			"if O_APPEND is used. disabling 'direct-io-mode'");
	}
	ret = dict_set_static_ptr (top->options, ZR_DIRECT_IO_OPT, "disable");

 	if (cmd_args->non_local)
 		ret = dict_set_uint32 (top->options, "macfuse-local", 
				       cmd_args->non_local);
	
#else /* ! DARWIN HOST OS */
	if (cmd_args->fuse_direct_io_mode_flag == _gf_true) {
		ret = dict_set_static_ptr (top->options, ZR_DIRECT_IO_OPT,
					   "enable");
	} else  {
		ret = dict_set_static_ptr (top->options, ZR_DIRECT_IO_OPT,
					   "disable");
	}

#endif /* GF_DARWIN_HOST_OS */
	
	graph->parents = CALLOC (1, sizeof (xlator_list_t));
	graph->parents->xlator = top;
	
	return top;
}


static FILE *
_get_specfp (glusterfs_ctx_t *ctx)
{
	int          ret = 0;
	cmd_args_t  *cmd_args = NULL;
	FILE        *specfp = NULL;
	struct stat  statbuf;

	cmd_args = &ctx->cmd_args;
	
	if (cmd_args->volfile_server) {
		specfp = fetch_spec (ctx);
		
		if (specfp == NULL) {
			fprintf (stderr, 
				 "error while getting volume file from "
				 "server %s\n", cmd_args->volfile_server);
			gf_log ("glusterfs", GF_LOG_ERROR, 
				"error while getting volume file from "
				"server %s", cmd_args->volfile_server);
		}
		else {
			gf_log ("glusterfs", GF_LOG_DEBUG, 
				"loading volume file from server %s", 
				cmd_args->volfile_server);
		}
		
		return specfp;
	}
	
	ret = stat (cmd_args->volume_file, &statbuf);
	if (ret == -1) {
		fprintf (stderr, "%s: %s\n", 
			 cmd_args->volume_file, strerror (errno));
		gf_log ("glusterfs", GF_LOG_ERROR, 
			"%s: %s", cmd_args->volume_file, strerror (errno));
		return NULL;		
	}
	if (!(S_ISREG (statbuf.st_mode) || S_ISLNK (statbuf.st_mode))) {
		fprintf (stderr, 
			 "provide a valid volume file\n");
		gf_log ("glusterfs", GF_LOG_ERROR, 
			"provide a valid volume file");
		return NULL;
	}
	if ((specfp = fopen (cmd_args->volume_file, "r")) == NULL) {
		fprintf (stderr, "volume file %s: %s\n", 
			 cmd_args->volume_file, 
			 strerror (errno));
		gf_log ("glusterfs", GF_LOG_ERROR, 
			"volume file %s: %s", 
			cmd_args->volume_file, 
			strerror (errno));
		return NULL;
	}
	
	gf_log ("glusterfs", GF_LOG_DEBUG, 
		"loading volume file %s", cmd_args->volume_file);
	
	return specfp;
}

static xlator_t *
_parse_specfp (glusterfs_ctx_t *ctx, 
	       FILE *specfp)
{
	cmd_args_t *cmd_args = NULL;
	xlator_t *tree = NULL, *trav = NULL, *new_tree = NULL;
	
	cmd_args = &ctx->cmd_args;
	
	fseek (specfp, 0L, SEEK_SET);
	
	tree = file_to_xlator_tree (ctx, specfp);
	trav = tree;
	
	if (tree == NULL) {
		if (cmd_args->volfile_server) {
			fprintf (stderr, 
				 "error in parsing volume file given by "
				 "server %s\n", cmd_args->volfile_server);
			gf_log ("glusterfs", GF_LOG_ERROR, 
				"error in parsing volume file given by "
				"server %s", cmd_args->volfile_server);
		}
		else {
			fprintf (stderr, 
				 "error in parsing volume file %s\n", 
				 cmd_args->volume_file);
			gf_log ("glusterfs", GF_LOG_ERROR, 
				"error in parsing volume file %s", 
				cmd_args->volume_file);
		}
		return NULL;
	}
	
	/* if volume_name is given, then we attach to it */
	if (cmd_args->volume_name) {
		while (trav) {
			if (strcmp (trav->name, cmd_args->volume_name) == 0) {
				new_tree = trav;
				break;
			}
			trav = trav->next;
		}
		
		if (!trav) {
			if (cmd_args->volfile_server) {
				fprintf (stderr, 
					 "volume %s not found in volume "
					 "file given by server %s\n", 
					 cmd_args->volume_name, 
					 cmd_args->volfile_server);
				gf_log ("glusterfs", GF_LOG_ERROR, 
					"volume %s not found in volume "
					"file given by server %s", 
					cmd_args->volume_name, 
					cmd_args->volfile_server);
			} else {
				fprintf (stderr, 
					 "volume %s not found in volume "
					 "file %s\n", 
					 cmd_args->volume_name, 
					 cmd_args->volume_file);
				gf_log ("glusterfs", GF_LOG_ERROR, 
					"volume %s not found in volume "
					"file %s", cmd_args->volume_name, 
					cmd_args->volume_file);
			}
			return NULL;
		}
		tree = trav;
	}
	return tree;
}

static int
_log_if_option_is_invalid (xlator_t *xl, data_pair_t *pair)
{
	volume_opt_list_t *vol_opt = NULL;
	volume_option_t   *opt     = NULL;
	int i     = 0;
	int index = 0;
	int found = 0;

	/* Get the first volume_option */
	list_for_each_entry (vol_opt, &xl->volume_options, list) {
		/* Warn for extra option */
		if (!vol_opt->given_opt)
			break;

		opt = vol_opt->given_opt;
		for (index = 0; 
		     ((index < ZR_OPTION_MAX_ARRAY_SIZE) && 
		      (opt[index].key && opt[index].key[0]));  index++)
			for (i = 0; (i < ZR_VOLUME_MAX_NUM_KEY) &&
				     opt[index].key[i]; i++) {
				if (fnmatch (opt[index].key[i],
					     pair->key, 
					     FNM_NOESCAPE) == 0) {
					found = 1;
					break;
				}
			}
	}

	if (!found) {
		gf_log (xl->name, GF_LOG_WARNING,
			"option '%s' is not recognized",
			pair->key);
	}
	return 0;
}

static int 
_xlator_graph_init (xlator_t *xl)
{
	volume_opt_list_t *vol_opt = NULL;
	data_pair_t *pair = NULL;
	xlator_t *trav = NULL;
	int ret = -1;
	
	trav = xl;
	
	while (trav->prev)
		trav = trav->prev;

	/* Validate phase */
	while (trav) {
		/* Get the first volume_option */
		list_for_each_entry (vol_opt, 
				     &trav->volume_options, list) 
			break;
		if ((ret = 
		     validate_xlator_volume_options (trav, 
				     vol_opt->given_opt)) < 0) {
			gf_log (trav->name, GF_LOG_ERROR, 
				"validating translator failed");
			return ret;
		}
		trav = trav->next;
	}

	
	trav = xl;
	while (trav->prev)
		trav = trav->prev;
	/* Initialization phase */
	while (trav) {
		if (!trav->ready) {
			if ((ret = xlator_tree_init (trav)) < 0) {
				gf_log ("glusterfs", GF_LOG_ERROR, 
					"initializing translator failed");
				return ret;
			}
		}
		trav = trav->next;
	}
	
	/* No error in this phase, just bunch of warning if at all */
	trav = xl;
	
	while (trav->prev)
		trav = trav->prev;
	
	/* Validate again phase */
	while (trav) {
		pair = trav->options->members_list;
		while (pair) {
			_log_if_option_is_invalid (trav, pair);
			pair = pair->next;
		}
		trav = trav->next;
	}

	return ret;
}

int 
glusterfs_graph_init (xlator_t *graph, int fuse) 
{ 
	volume_opt_list_t *vol_opt = NULL;

	if (fuse) {
		/* FUSE needs to be initialized earlier than the 
		   other translators */
		list_for_each_entry (vol_opt, 
				     &graph->volume_options, list) 
			break;
		if (validate_xlator_volume_options (graph, 
					   vol_opt->given_opt) == -1) {
			gf_log (graph->name, GF_LOG_ERROR, 
				"validating translator failed");
			return -1;
		}
		if (graph->init (graph) != 0)
			return -1;
		
		graph->ready = 1;
	}
	if (_xlator_graph_init (graph) == -1)
		return -1;

	/* check server or fuse is given */
	if (graph->ctx->top == NULL) {
		fprintf (stderr, "no valid translator loaded at the top, or"
			 "no mount point given. exiting\n");
		gf_log ("glusterfs", GF_LOG_ERROR, 
			"no valid translator loaded at the top or "
			"no mount point given. exiting");
		return -1;
	}

	return 0;
}

static int
gf_remember_xlator_option (struct list_head *options, char *arg)
{
	glusterfs_ctx_t         *ctx = NULL;
	cmd_args_t              *cmd_args  = NULL;
	xlator_cmdline_option_t *option = NULL;
	int                      ret = -1;
	char                    *dot = NULL;
	char                    *equals = NULL;

	ctx = get_global_ctx_ptr ();
	cmd_args = &ctx->cmd_args;

	option = CALLOC (1, sizeof (xlator_cmdline_option_t));
	INIT_LIST_HEAD (&option->cmd_args);

	dot = strchr (arg, '.');
	if (!dot)
		goto out;

	option->volume = CALLOC ((dot - arg), sizeof (char));
	strncpy (option->volume, arg, (dot - arg));

	equals = strchr (arg, '=');
	if (!equals)
		goto out;

	option->key = CALLOC ((equals - dot), sizeof (char));
	strncpy (option->key, dot + 1, (equals - dot - 1));

	if (!*(equals + 1))
		goto out;

	option->value = strdup (equals + 1);
	
	list_add (&option->cmd_args, &cmd_args->xlator_options);

	ret = 0;
out:
	if (ret == -1) {
		if (option) {
			if (option->volume)
				FREE (option->volume);
			if (option->key)
				FREE (option->key);
			if (option->value)
				FREE (option->value);

			FREE (option);
		}
	}

	return ret;
}


static void
gf_add_cmdline_options (xlator_t *graph, cmd_args_t *cmd_args)
{
	int                      ret = 0;
	xlator_t                *trav = graph;
	xlator_cmdline_option_t *cmd_option = NULL;

	while (trav) {
		list_for_each_entry (cmd_option, 
				     &cmd_args->xlator_options, cmd_args) {
			if (!fnmatch (cmd_option->volume, 
				      trav->name, FNM_NOESCAPE)) {
				ret = dict_set_str (trav->options, 
						    cmd_option->key, 
						    cmd_option->value);
				if (ret == 0) {
					gf_log ("glusterfs", GF_LOG_WARNING,
						"adding option '%s' for "
						"volume '%s' with value '%s'", 
						cmd_option->key, trav->name, 
						cmd_option->value);
				} else {
					gf_log ("glusterfs", GF_LOG_WARNING,
						"adding option '%s' for "
						"volume '%s' failed: %s", 
						cmd_option->key, trav->name, 
						strerror (-ret));
				}
			}
		}
		trav = trav->next;
	}
}


error_t 
parse_opts (int key, char *arg, struct argp_state *state)
{
	cmd_args_t *cmd_args = NULL;
	uint32_t    n = 0;
	double      d = 0.0;

	cmd_args = state->input;
	
	switch (key) {
	case ARGP_VOLFILE_SERVER_KEY:
		cmd_args->volfile_server = strdup (arg);
		break;
		
	case ARGP_VOLUME_FILE_KEY:
		cmd_args->volume_file = strdup (arg);
		break;
		
	case ARGP_LOG_LEVEL_KEY:
		if (strcasecmp (arg, ARGP_LOG_LEVEL_NONE_OPTION) == 0) {
			cmd_args->log_level = GF_LOG_NONE;
			break;
		}
		if (strcasecmp (arg, ARGP_LOG_LEVEL_TRACE_OPTION) == 0) {
			cmd_args->log_level = GF_LOG_TRACE;
			break;
		}
		if (strcasecmp (arg, ARGP_LOG_LEVEL_CRITICAL_OPTION) == 0) {
			cmd_args->log_level = GF_LOG_CRITICAL;
			break;
		}
		if (strcasecmp (arg, ARGP_LOG_LEVEL_ERROR_OPTION) == 0) {
			cmd_args->log_level = GF_LOG_ERROR;
			break;
		}
		if (strcasecmp (arg, ARGP_LOG_LEVEL_WARNING_OPTION) == 0) {
			cmd_args->log_level = GF_LOG_WARNING;
			break;
		}
		if (strcasecmp (arg, ARGP_LOG_LEVEL_NORMAL_OPTION) == 0) {
			cmd_args->log_level = GF_LOG_NORMAL;
			break;
		}
		if (strcasecmp (arg, ARGP_LOG_LEVEL_DEBUG_OPTION) == 0) {
			cmd_args->log_level = GF_LOG_DEBUG;
			break;
		}
		
		argp_failure (state, -1, 0, "unknown log level %s", arg);
		break;
		
	case ARGP_LOG_FILE_KEY:
		cmd_args->log_file = strdup (arg);
		break;
		
	case ARGP_VOLFILE_SERVER_PORT_KEY:
		n = 0;
		
		if (gf_string2uint_base10 (arg, &n) == 0) {
			cmd_args->volfile_server_port = n;
			break;
		}
		
		argp_failure (state, -1, 0, 
			      "unknown volfile server port %s", arg);
		break;
	
	case ARGP_VOLFILE_SERVER_TRANSPORT_KEY:
		cmd_args->volfile_server_transport = strdup (arg);
		break;
		
	case ARGP_VOLFILE_ID_KEY:
		cmd_args->volfile_id = strdup (arg);
		break;

	case ARGP_PID_FILE_KEY:
		cmd_args->pid_file = strdup (arg);
		break;
		
	case ARGP_NO_DAEMON_KEY:
		cmd_args->no_daemon_mode = ENABLE_NO_DAEMON_MODE;
		break;
		
	case ARGP_RUN_ID_KEY:
		cmd_args->run_id = strdup (arg);
		break;
		
	case ARGP_DEBUG_KEY:
		cmd_args->debug_mode = ENABLE_DEBUG_MODE;
		break;
		
	case ARGP_DISABLE_DIRECT_IO_MODE_KEY:
		cmd_args->fuse_direct_io_mode_flag = _gf_false;
		break;
		
	case ARGP_ENTRY_TIMEOUT_KEY:
		d = 0.0;
		
		gf_string2double (arg, &d);
		if (!(d < 0.0)) {
			cmd_args->fuse_entry_timeout = d;
			break;
		}
		
		argp_failure (state, -1, 0, "unknown entry timeout %s", arg);
		break;
	
	case ARGP_ATTRIBUTE_TIMEOUT_KEY:
		d = 0.0;

		gf_string2double (arg, &d);
		if (!(d < 0.0)) {
			cmd_args->fuse_attribute_timeout = d;
			break;
		}
		
		argp_failure (state, -1, 0, 
			      "unknown attribute timeout %s", arg);
		break;
	
	case ARGP_VOLUME_NAME_KEY:
		cmd_args->volume_name = strdup (arg);
		break;

	case ARGP_XLATOR_OPTION_KEY:
		gf_remember_xlator_option (&cmd_args->xlator_options, arg);
		break;

#ifdef GF_DARWIN_HOST_OS		
	case ARGP_NON_LOCAL_KEY:
		cmd_args->non_local = _gf_true;
		break;

#endif /* DARWIN */

	case ARGP_KEY_NO_ARGS:
		break;
		
	case ARGP_KEY_ARG:
		if (state->arg_num >= 1)
			argp_usage (state);
		
		cmd_args->mount_point = strdup (arg);
		break;
	}

	return 0;
}


void 
cleanup_and_exit (int signum)
{
	glusterfs_ctx_t *ctx = NULL;
	xlator_t        *trav = NULL;

	ctx = get_global_ctx_ptr ();
	
	gf_log ("glusterfs", GF_LOG_WARNING, "shutting down");

	if (ctx->pidfp) {
		gf_unlockfd (fileno (ctx->pidfp));
		fclose (ctx->pidfp);
		ctx->pidfp = NULL;
	}

	if (ctx->specfp) {
		fclose (ctx->specfp);
		ctx->specfp = NULL;
	}

	if (ctx->cmd_args.pid_file) {
		unlink (ctx->cmd_args.pid_file);
		ctx->cmd_args.pid_file = NULL;
	}
	
	if (ctx->graph) {
		trav = ctx->graph;
		ctx->graph = NULL;
		while (trav) {
			trav->fini (trav);
			trav = trav->next;
		}
		exit (0);
	} else {
		gf_log ("glusterfs", GF_LOG_DEBUG, "no graph present");
	}
}


static char *
zr_build_process_uuid ()
{
	char           tmp_str[1024] = {0,};
	char           hostname[256] = {0,};
	struct timeval tv = {0,};
	struct tm      now = {0, };
	char           now_str[32];

	if (-1 == gettimeofday(&tv, NULL)) {
		gf_log ("", GF_LOG_ERROR, 
			"gettimeofday: failed %s",
			strerror (errno));		
	}

	if (-1 == gethostname (hostname, 256)) {
		gf_log ("", GF_LOG_ERROR, 
			"gethostname: failed %s",
			strerror (errno));
	}

	localtime_r (&tv.tv_sec, &now);
	strftime (now_str, 32, "%Y/%m/%d-%H:%M:%S", &now);
	snprintf (tmp_str, 1024, "%s-%d-%s:%ld", 
		  hostname, getpid(), now_str, tv.tv_usec);
	
	return strdup (tmp_str);
}

#define GF_SERVER_PROCESS 0
#define GF_CLIENT_PROCESS 1

static uint8_t
gf_get_process_mode (char *exec_name)
{
	char *dup_execname = NULL, *base = NULL;
	uint8_t ret = 0;

	dup_execname = strdup (exec_name);
	base = basename (dup_execname);
	
	if (!strncmp (base, "glusterfsd", 10)) {
		ret = GF_SERVER_PROCESS;
	} else {
		ret = GF_CLIENT_PROCESS;
	}
	
	free (dup_execname);

	return ret;
}

int 
main (int argc, char *argv[])
{
	glusterfs_ctx_t  *ctx = NULL;
	cmd_args_t       *cmd_args = NULL;
	call_pool_t      *pool = NULL;
	struct stat       stbuf;
	char              tmp_logfile[1024] = { 0 };
	char              timestr[256] = { 0 };
	char             *base_exec_name = NULL;
	time_t            utime;
	struct tm        *tm = NULL;
	int               ret = 0;
	struct rlimit     lim;
	FILE             *specfp = NULL;
	xlator_t         *graph = NULL;
	xlator_t         *trav = NULL;
	int               fuse_volume_found = 0;
	int               xl_count = 0;
	uint8_t           process_mode = 0;

	utime = time (NULL);
	ctx = CALLOC (1, sizeof (glusterfs_ctx_t));
	ERR_ABORT (ctx);
	base_exec_name = strdup (argv[0]);
	process_mode = gf_get_process_mode (base_exec_name);
	set_global_ctx_ptr (ctx);
	ctx->process_uuid = zr_build_process_uuid ();
	cmd_args = &ctx->cmd_args;

	/* parsing command line arguments */
	cmd_args->log_level = DEFAULT_LOG_LEVEL;
	cmd_args->fuse_direct_io_mode_flag = _gf_true;
	
	INIT_LIST_HEAD (&cmd_args->xlator_options);

	argp_parse (&argp, argc, argv, ARGP_IN_ORDER, NULL, cmd_args);

	if (ENABLE_DEBUG_MODE == cmd_args->debug_mode) {
		cmd_args->log_level = GF_LOG_DEBUG;
		cmd_args->log_file = "/dev/stdout";
		cmd_args->no_daemon_mode = ENABLE_NO_DAEMON_MODE;
	}

	if ((cmd_args->volfile_server == NULL) 
	    && (cmd_args->volume_file == NULL)) {
		if (process_mode == GF_SERVER_PROCESS)
			cmd_args->volume_file = strdup (DEFAULT_SERVER_VOLUME_FILE);
		else
			cmd_args->volume_file = strdup (DEFAULT_CLIENT_VOLUME_FILE);
	}

	if (cmd_args->log_file == NULL)
		asprintf (&cmd_args->log_file, 
			  DEFAULT_LOG_FILE_DIRECTORY "/%s.log", 
			  basename (base_exec_name));
	
	free (base_exec_name);

	ctx->event_pool = event_pool_new (DEFAULT_EVENT_POOL_SIZE);
	pthread_mutex_init (&(ctx->lock), NULL);
	pool = ctx->pool = CALLOC (1, sizeof (call_pool_t));
	ERR_ABORT (ctx->pool);
	LOCK_INIT (&pool->lock);
	INIT_LIST_HEAD (&pool->all_frames);
	
 	if (cmd_args->pid_file != NULL) {
 		ctx->pidfp = fopen (cmd_args->pid_file, "a+");
 		if (ctx->pidfp == NULL) {
 			fprintf (stderr, 
				 "unable to open pid file %s. %s. exiting\n", 
				 cmd_args->pid_file, strerror (errno));
 			/* do cleanup and exit ?! */
 			return -1;
 		}
 		ret = gf_lockfd (fileno (ctx->pidfp));
 		if (ret == -1) {
 			fprintf (stderr, "unable to lock pid file %s. %s. "
				 "Is another instance of %s running?!\n"
 				 "exiting\n", cmd_args->pid_file, 
				 strerror (errno), argv[0]);
 			fclose (ctx->pidfp);
 			return -1;
 		}
 		ret = ftruncate (fileno (ctx->pidfp), 0);
 		if (ret == -1) {
 			fprintf (stderr, 
				 "unable to truncate file %s. %s. exiting\n", 
				 cmd_args->pid_file, strerror (errno));
 			gf_unlockfd (fileno (ctx->pidfp));
 			fclose (ctx->pidfp);
 			return -1;
 		}
 	}
	
	/* initializing logs */
	if (cmd_args->run_id) {
		ret = stat (cmd_args->log_file, &stbuf);
		/* If its /dev/null, or /dev/stdout, /dev/stderr, 
		 * let it use the same, no need to alter 
		 */
		if (((ret == 0) && 
		     (S_ISREG (stbuf.st_mode) || S_ISLNK (stbuf.st_mode))) || 
		    (ret == -1)) {
			/* Have seperate logfile per run */
			tm = localtime (&utime);
			strftime (timestr, 256, "%Y%m%d.%H%M%S", tm); 
			sprintf (tmp_logfile, "%s.%s.%d", 
				 cmd_args->log_file, timestr, getpid ());
			
			/* Create symlink to actual log file */
			unlink (cmd_args->log_file);
			symlink (tmp_logfile, cmd_args->log_file);
			
			FREE (cmd_args->log_file);
			cmd_args->log_file = strdup (tmp_logfile);
		}
	}
	
	gf_global_variable_init ();

	if (gf_log_init (cmd_args->log_file) == -1) {
		fprintf (stderr, 
			 "failed to open logfile %s.  exiting\n", 
			 cmd_args->log_file);
		return -1;
	}
	gf_log_set_loglevel (cmd_args->log_level);
	
	/* setting up environment  */
	lim.rlim_cur = RLIM_INFINITY;
	lim.rlim_max = RLIM_INFINITY;
	if (setrlimit (RLIMIT_CORE, &lim) == -1) {
		fprintf (stderr, "ignoring %s\n", 
			 strerror (errno));
	}
#ifdef HAVE_MALLOC_STATS
#ifdef DEBUG
	mtrace ();
#endif
	signal (SIGUSR1, (sighandler_t) malloc_stats);
#endif
	signal (SIGSEGV, gf_print_trace);
	signal (SIGABRT, gf_print_trace);
	signal (SIGPIPE, SIG_IGN);
	signal (SIGHUP, gf_log_logrotate);
	signal (SIGTERM, cleanup_and_exit);
	/* This is used to dump details */
	/* signal (SIGUSR2, (sighandler_t) glusterfs_stats); */
	
	/* getting and parsing volume file */
	if ((specfp = _get_specfp (ctx)) == NULL) {
		/* _get_specfp() prints necessary error message  */
		gf_log ("glusterfs", GF_LOG_ERROR, "exiting\n");
		argp_help (&argp, stderr, ARGP_HELP_SEE, (char *) argv[0]);
		return -1;
	}
	_gf_dump_details (argc, argv);
	gf_log_volume_file (specfp); 
	if ((graph = _parse_specfp (ctx, specfp)) == NULL) {
		/* _parse_specfp() prints necessary error message */
		fprintf (stderr, "exiting\n");
		gf_log ("glusterfs", GF_LOG_ERROR, "exiting");
		return -1;
	}
	ctx->specfp = specfp;
	
	/* check whether MOUNT-POINT argument and fuse volume are given
	 * at same time or not. If not, add argument MOUNT-POINT to graph 
	 * as top volume if given 
	 */
	trav = graph;
	fuse_volume_found = 0;
	
	while (trav) {
		if (strcmp (trav->type, ZR_XLATOR_FUSE) == 0) {
			if (dict_get (trav->options, 
				      ZR_MOUNTPOINT_OPT) != NULL) {
				trav->ctx = graph->ctx;
				fuse_volume_found = 1;
			}
		}

		xl_count++;  /* Getting this value right is very important */
		trav = trav->next;
	}
	
	ctx->xl_count = xl_count + 1;

	if (!fuse_volume_found && (cmd_args->mount_point != NULL)) {
		if ((graph = _add_fuse_mount (graph)) == NULL) {
			/* _add_fuse_mount() prints necessary 
			 * error message 
			 */
			fprintf (stderr, "exiting\n");
			gf_log ("glusterfs", GF_LOG_ERROR, "exiting");
			return -1;
		}
	}
		
	/* daemonize now */
	if (!cmd_args->no_daemon_mode) {
		if (daemon (0, 0) == -1) {
			fprintf (stderr, "unable to run in daemon mode: %s",
				 strerror (errno));
			gf_log ("glusterfs", GF_LOG_ERROR, 
				"unable to run in daemon mode: %s",
				strerror (errno));
			return -1;
		}
		
		/* we are daemon now */
 		/* update pid file, if given */
 		if (cmd_args->pid_file != NULL) {
 			fprintf (ctx->pidfp, "%d\n", getpid ());
 			fflush (ctx->pidfp);
 			/* we close pid file on exit */
 		}
	}
	
	gf_log ("glusterfs", GF_LOG_DEBUG, 
		"running in pid %d", getpid ());
	
	gf_timer_registry_init (ctx);

	/* override xlator options with command line options
	 * where applicable 
	 */
	gf_add_cmdline_options (graph, cmd_args);

	ctx->graph = graph;
	if (glusterfs_graph_init (graph, fuse_volume_found) != 0) {
		gf_log ("glusterfs", GF_LOG_ERROR, 
			"translator initialization failed.  exiting");
		return -1;
	}

	/* Send PARENT_UP notify to all the translators now */
	graph->notify (graph, GF_EVENT_PARENT_UP, ctx->graph);

	gf_log ("glusterfs", GF_LOG_NORMAL, "Successfully started");
	
	event_dispatch (ctx->event_pool);

	return 0;
}
