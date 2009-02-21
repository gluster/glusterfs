/*
  Copyright (c) 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif /* _CONFIG_H */

#include "glusterfs.h"
#include "stack.h"
#include "dict.h"
#include "transport.h"
#include "event.h"
#include "defaults.h"


static int 
fetch_cbk (call_frame_t *frame,
	   void *cookie,
	   xlator_t *this,
	   int32_t op_ret,
	   int32_t op_errno,
	   char *spec_data)
{
	FILE *spec_fp = NULL;
	
	spec_fp = frame->local;
	
	if (op_ret >= 0) {
		fwrite (spec_data, strlen (spec_data), 1, spec_fp);
		fflush (spec_fp);
		fclose (spec_fp);
	}
	else {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"GETSPEC from server returned -1 (%s)", 
			strerror (op_errno));
	}
	
	frame->local = NULL;
	STACK_DESTROY (frame->root);
	
	/* exit the child process */
	exit (op_ret);
}


static int
fetch_notify (xlator_t *this_xl, int event, void *data, ...)
{
	int ret = 0;
	call_frame_t *frame = NULL;
	
	switch (event)
	{
	case GF_EVENT_CHILD_UP:
		frame = create_frame (this_xl, this_xl->ctx->pool);
		frame->local = this_xl->private;
		
		STACK_WIND (frame, fetch_cbk,
			    this_xl->children->xlator,
			    this_xl->children->xlator->mops->getspec,
			    this_xl->ctx->cmd_args.volfile_id,
			    0);
		break;
	case GF_EVENT_CHILD_DOWN:
		break;
	default:
		ret = default_notify (this_xl, event, data);
		break;
	}
	
	return ret;
}


static int
fetch_init (xlator_t *xl)
{
	return 0;
}

static xlator_t *
get_shrub (glusterfs_ctx_t *ctx,
	   const char *remote_host,
	   const char *transport,
	   uint32_t remote_port)
{
	int ret = 0;
	xlator_t *top = NULL;
	xlator_t *trans = NULL;
	xlator_list_t *parent = NULL, *tmp = NULL;
	
	top = CALLOC (1, sizeof (*top));
	ERR_ABORT (top);
	trans = CALLOC (1, sizeof (*trans));
	ERR_ABORT (trans);
	
	top->name = "top";
	top->ctx = ctx;
	top->next = trans;
	top->init = fetch_init;
	top->notify = fetch_notify;
	top->children = (void *) CALLOC (1, sizeof (*top->children));
	ERR_ABORT (top->children);
	top->children->xlator = trans;
	
	trans->name = "trans";
	trans->ctx = ctx;
	trans->prev = top;
	trans->init = fetch_init;
	trans->notify = default_notify;
	trans->options = get_new_dict ();
	
	parent = CALLOC (1, sizeof(*parent));
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
	if (remote_host)
		ret = dict_set_static_ptr (trans->options, "remote-host",
					   (char *)remote_host);

	if (remote_port)
		ret = dict_set_uint32 (trans->options, "remote-port", 
				       remote_port);

	/* 'option remote-subvolume <x>' is needed here even though 
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
	
	if (xlator_tree_init (top) != 0)
		return NULL;
	
	return top;
}


static int 
_fetch (glusterfs_ctx_t *ctx,
	FILE *spec_fp,
	const char *remote_host,
	const char *transport,
	uint32_t remote_port)
{
	xlator_t *this = NULL;
	
	this = get_shrub (ctx, remote_host, transport, remote_port);
	if (this == NULL)
		return -1;
	
	this->private = spec_fp;
	
	event_dispatch (ctx->event_pool);
	
	return 0;
}


static int 
_fork_and_fetch (glusterfs_ctx_t *ctx,
		 FILE *spec_fp,
		 const char *remote_host,
		 const char *transport,
		 uint32_t remote_port)
{
	int ret;
	
	ret = fork ();
	switch (ret) {
	case -1:
		perror ("fork()");
		break;
	case 0:
		/* child */
		ret = _fetch (ctx, spec_fp, remote_host, 
			      transport, remote_port);
		if (ret == -1)
			exit (ret);
	default:
		/* parent */
		wait (&ret);
		ret = WEXITSTATUS (ret);
	}
	return ret;
}

FILE *
fetch_spec (glusterfs_ctx_t *ctx)
{
	char *remote_host = NULL;
	char *transport = NULL;
	FILE *spec_fp;
	int32_t ret;
	
	spec_fp = tmpfile ();
	
	if (!spec_fp) {
		perror ("tmpfile ()");
		return NULL;
	}
	
	remote_host = ctx->cmd_args.volfile_server;
	transport = ctx->cmd_args.volfile_server_transport;
	if (!transport)
		transport = "tcp";

	ret = _fork_and_fetch (ctx, spec_fp, remote_host, transport,
			       ctx->cmd_args.volfile_server_port);
	
	if (!ret) {
		fseek (spec_fp, 0, SEEK_SET);
	}
	else {
		fclose (spec_fp);
		spec_fp = NULL;
	}
	
	return spec_fp;
}
