/*
  Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <locale.h>
#include <string.h>
#include <stdlib.h>

#include "xlator.h"
#include "logging.h"
#include "defaults.h"

static pthread_mutex_t  logfile_mutex;
static char            *filename = NULL;
static uint8_t          logrotate = 0;

static FILE            *logfile = NULL;
static gf_loglevel_t    loglevel = GF_LOG_MAX;

gf_loglevel_t           gf_log_loglevel; /* extern'd */
FILE                   *gf_log_logfile;


void 
gf_log_logrotate (int signum)
{
	logrotate = 1;
}


gf_loglevel_t 
gf_log_get_loglevel (void)
{
	return loglevel;
}


void
gf_log_set_loglevel (gf_loglevel_t level)
{
	gf_log_loglevel = loglevel = level;
}


void 
gf_log_fini (void)
{
	pthread_mutex_destroy (&logfile_mutex);
}


int
gf_log_init (const char *file)
{
	if (!file){
		fprintf (stderr, "gf_log_init: no filename specified\n");
		return -1;
	}

	pthread_mutex_init (&logfile_mutex, NULL);

	filename = strdup (file);
	if (!filename) {
		fprintf (stderr, "gf_log_init: strdup error\n");
		return -1;
	}

	logfile = fopen (file, "a");
	if (!logfile){
		fprintf (stderr,
			 "gf_log_init: failed to open logfile \"%s\" (%s)\n",
			 file,
			 strerror (errno));
		return -1;
	}

	gf_log_logfile = logfile;

	return 0;
}


static int
dummy_init (xlator_t *xl)
{
	return 0;
}


static int
gf_log_notify (xlator_t *this_xl, int event, void *data, ...)
{
	int ret = 0;
	
	switch (event) {
	case GF_EVENT_CHILD_UP:
		break;
                
	case GF_EVENT_CHILD_DOWN:
		break;
                
	default:
		ret = default_notify (this_xl, event, data);
		break;
	}
	
	return ret;
}


/*
 * Get a dummy xlator for the purpose of central logging.
 * An xlator is needed because a transport cannot exist without
 * an xlator.
 */

static xlator_t *
__get_dummy_xlator (glusterfs_ctx_t *ctx, const char *remote_host,
                    const char *transport, uint32_t remote_port)
{
        volume_opt_list_t *vol_opt = NULL;
	xlator_t *         trav    = NULL;
        
	int ret = 0;
        
	xlator_t *      top    = NULL;
	xlator_t *      trans  = NULL;
	xlator_list_t * parent = NULL;
        xlator_list_t * tmp    = NULL;

	top = CALLOC (1, sizeof (*top));
        if (!top)
                goto out;

	trans = CALLOC (1, sizeof (*trans));
        if (!trans)
                goto out;
	
        INIT_LIST_HEAD (&top->volume_options);
        INIT_LIST_HEAD (&trans->volume_options);

	top->name     = "log-dummy";
	top->ctx      = ctx;
	top->next     = trans;
	top->init     = dummy_init;
	top->notify   = gf_log_notify;
	top->children = (void *) CALLOC (1, sizeof (*top->children));
        
	if (!top->children)
                goto out;

	top->children->xlator = trans;
	
	trans->name    = "log-transport";
	trans->ctx     = ctx;
	trans->prev    = top;
	trans->init    = dummy_init;
	trans->notify  = default_notify;
	trans->options = get_new_dict ();
	
	parent = CALLOC (1, sizeof(*parent));

        if (!parent)
                goto out;

	parent->xlator = top;
        
	if (trans->parents == NULL)
		trans->parents = parent;
	else {
		tmp = trans->parents;
		while (tmp->next)
			tmp = tmp->next;
		tmp->next = parent;
	}

	/* TODO: log on failure to set dict */
	if (remote_host) {
                ret = dict_set (trans->options, "remote-host",
                                str_to_data ((char *)remote_host));
        }

	if (remote_port)
		ret = dict_set_uint32 (trans->options, "remote-port", 
				       remote_port);

	/* 
         * 'option remote-subvolume <x>' is needed here even though 
	 * its not used 
	 */
	ret = dict_set_static_ptr (trans->options, "remote-subvolume", 
				   "brick");
	ret = dict_set_static_ptr (trans->options, "disable-handshake", "on");
	ret = dict_set_static_ptr (trans->options, "non-blocking-io", "off");
	
	if (transport) {
		char *transport_type = CALLOC (1, strlen (transport) + 10);
		ERR_ABORT (transport_type);
		strcpy(transport_type, transport);

		if (strchr (transport_type, ':'))
			*(strchr (transport_type, ':')) = '\0';

		ret = dict_set_dynstr (trans->options, "transport-type", 
				       transport_type);
	}
	
	xlator_set_type (trans, "protocol/client");

        trav = top;
	while (trav) {
		/* Get the first volume_option */
                if (!list_empty (&trav->volume_options)) {
                        list_for_each_entry (vol_opt, 
                                             &trav->volume_options, list) 
                                break;
                        if ((ret = 
                             validate_xlator_volume_options (trav, 
                                                             vol_opt->given_opt)) < 0) {
                                gf_log (trav->name, GF_LOG_ERROR, 
                                        "validating translator failed");
                                return NULL;
                        }
                }
		trav = trav->next;
	}

	if (xlator_tree_init (top) != 0)
		return NULL;

out:	
	return top;
}


/*
 * Initialize logging to a central server.
 * If successful, log messages will be written both to
 * the local file and to the remote server.
 */

static xlator_t * logging_xl = NULL;
static int __central_log_enabled = 0;

static pthread_t logging_thread;

struct _msg_queue {
        struct list_head msgs;
};

static struct _msg_queue msg_queue;

static pthread_cond_t msg_cond;
static pthread_mutex_t msg_cond_mutex;
static pthread_mutex_t msg_queue_mutex;

struct _log_msg {
        const char *msg;
        struct list_head queue;
};


int32_t
gf_log_central_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno)
{
        struct _log_msg *msg = NULL;

        msg = (struct _log_msg *) cookie;

        FREE (msg->msg);

        STACK_DESTROY (frame->root);

        return 0;
}


void *
logging_thread_loop (void *arg)
{
        struct _log_msg *msg;

        call_frame_t *frame = NULL;

        while (1) {
                pthread_mutex_lock (&msg_cond_mutex);
                {
                        pthread_cond_wait (&msg_cond, &msg_cond_mutex);

                        while (!list_empty (&msg_queue.msgs)) {
                                pthread_mutex_lock (&msg_queue_mutex);
                                {
                                        msg = list_entry (msg_queue.msgs.next,
                                                          struct _log_msg,
                                                          queue);
                                
                                        list_del_init (&msg->queue);
                                }
                                pthread_mutex_unlock (&msg_queue_mutex);

                                frame = create_frame (logging_xl, 
                                                      logging_xl->ctx->pool);
                                
                                frame->local = logging_xl->private;
                                
                                STACK_WIND_COOKIE (frame, (void *) msg,
                                                   gf_log_central_cbk,
                                                   logging_xl->children->xlator,
                                                   logging_xl->children->xlator->mops->log,
                                                   msg->msg);
                        }

                }
                pthread_mutex_unlock (&msg_cond_mutex);
        }

        return NULL;
}


int
gf_log_central_init (glusterfs_ctx_t *ctx, const char *remote_host,
                     const char *transport, uint32_t remote_port)
{
        logging_xl = __get_dummy_xlator (ctx, remote_host, transport, 
                                         remote_port);
        
        if (!logging_xl) {
                goto out;
        }

        __central_log_enabled = 1;

        INIT_LIST_HEAD (&msg_queue.msgs);

        pthread_cond_init (&msg_cond, NULL);
        pthread_mutex_init (&msg_cond_mutex, NULL);
        pthread_mutex_init (&msg_queue_mutex, NULL);

        pthread_create (&logging_thread, NULL, logging_thread_loop, NULL);

out:
        return 0;
}


int
gf_log_central (const char *msg)
{
        struct _log_msg *lm = NULL;

        lm = CALLOC (1, sizeof (*lm));
        
        if (!lm)
                goto out;

        INIT_LIST_HEAD (&lm->queue);

        lm->msg = strdup (msg);

        pthread_mutex_lock (&msg_queue_mutex);
        {
                list_add_tail (&lm->queue, &msg_queue.msgs);
        }
        pthread_mutex_unlock (&msg_queue_mutex);
                
        pthread_cond_signal (&msg_cond);

out:
        return 0;
}


void 
gf_log_lock (void)
{
	pthread_mutex_lock (&logfile_mutex);
}


void 
gf_log_unlock (void)
{
	pthread_mutex_unlock (&logfile_mutex);
}


void
gf_log_cleanup (void)
{
	pthread_mutex_destroy (&logfile_mutex);
}


int
_gf_log (const char *domain, const char *file, const char *function, int line,
	 gf_loglevel_t level, const char *fmt, ...)
{
	const char  *basename = NULL;
	FILE        *new_logfile = NULL;
	va_list      ap;
	time_t       utime = 0;
	struct tm   *tm = NULL;
	char         timestr[256];

        char        *str1 = NULL;
        char        *str2 = NULL;
        char        *msg  = NULL;
        size_t       len  = 0;
        int          ret  = 0;

	static char *level_strings[] = {"",  /* NONE */
					"C", /* CRITICAL */
					"E", /* ERROR */
					"W", /* WARNING */
					"N", /* NORMAL */
					"D", /* DEBUG */
                                        "T", /* TRACE */
					""};
  
	if (!domain || !file || !function || !fmt) {
		fprintf (stderr, 
			 "logging: %s:%s():%d: invalid argument\n", 
			 __FILE__, __PRETTY_FUNCTION__, __LINE__);
		return -1;
	}
  
	if (!logfile) {
		fprintf (stderr, "no logfile set\n");
		return (-1);
	}

	if (logrotate) {
		logrotate = 0;

		new_logfile = fopen (filename, "a");
		if (!new_logfile) {
			gf_log ("logrotate", GF_LOG_CRITICAL,
				"failed to open logfile %s (%s)",
				filename, strerror (errno));
			goto log;
		}

		fclose (logfile);
		gf_log_logfile = logfile = new_logfile;
	}

log:
	utime = time (NULL);
	tm    = localtime (&utime);

	if (level > loglevel) {
		goto out;
	}

	pthread_mutex_lock (&logfile_mutex);
	{
		va_start (ap, fmt);

		strftime (timestr, 256, "%Y-%m-%d %H:%M:%S", tm); 

		basename = strrchr (file, '/');
		if (basename)
			basename++;
		else
			basename = file;

                ret = asprintf (&str1, "[%s] %s [%s:%d:%s] %s: ",
                                timestr, level_strings[level],
                                basename, line, function,
                                domain);
                if (-1 == ret) {
                        goto unlock;
                }

                ret = vasprintf (&str2, fmt, ap);
                if (-1 == ret) {
                        goto unlock;
                }

		va_end (ap);

                len = strlen (str1);
                msg = malloc (len + strlen (str2) + 1);
                
                strcpy (msg, str1);
                strcpy (msg + len, str2);

		fprintf (logfile, "%s\n", msg);
		fflush (logfile);
	}
unlock:
	pthread_mutex_unlock (&logfile_mutex);

        if (msg) {
                if ((ret != -1) && __central_log_enabled &&
                    ((glusterfs_central_log_flag_get ()) == 0)) {

                        glusterfs_central_log_flag_set ();
                        {
                                gf_log_central (msg);
                        }
                        glusterfs_central_log_flag_unset ();
                }
                FREE (msg);
        }

        if (str1)
                FREE (str1);

        if (str2)
                FREE (str2);

out:
	return (0);
}


struct _client_log {
        char *identifier;
        FILE *file;
        struct list_head list;
};

struct _client_log *client_logs = NULL;


static void
client_log_init (struct _client_log *cl, char *identifier)
{
        int   ret = 0;
        char *path = NULL;

        cl->identifier = identifier;

        ret = asprintf (&path, "%s.client-%s", filename, identifier);
        if (-1 == ret) {
                return;
        }
        cl->file = fopen (path, "a");
        FREE (path);
        
        INIT_LIST_HEAD (&cl->list);
}


static FILE *
__logfile_for_client (char *identifier)
{
        struct _client_log *client = NULL;

        if (!client_logs) {
                client = CALLOC (1, sizeof (*client));
                client_log_init (client, identifier);

                client_logs = client;
        }

        list_for_each_entry (client, &client_logs->list, list) {
                if (!strcmp (client->identifier, identifier))
                        break;
        }

        if (!client) {
                client = CALLOC (1, sizeof (*client));

                client_log_init (client, identifier);

                list_add_tail (&client->list, &client_logs->list);
        }

        return client->file;
}


int
gf_log_from_client (const char *msg, char *identifier)
{
        FILE *client_log = NULL;

        client_log = __logfile_for_client (identifier);

        fprintf (client_log, "%s\n", msg);
        fflush (client_log);

        return 0;
}
