/*
  (C) 2007 Z RESEARCH Inc. <http://www.zresearch.com>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include "glusterfs.h"
#include "dict.h"

static xlator_t *
get_shrub (glusterfs_ctx_t *ctx,
	   const char *remote_host,
	   const char *remote_port,
	   const char *transport)
{

  xlator_t *top = calloc (1, sizeof (*top));
  xlator_t *trans = calloc (1, sizeof (*trans));

  top->name = "top";
  top->ctx = ctx;
  top->next = trans;
  top->children = (void *) calloc (1, sizeof (*top->children));
  top->children->xlator = trans;

  trans->name = "trans";
  trans->ctx = ctx;
  trans->prev = top;
  trans->parent = top;
  trans->options = get_new_dict ();

  if (remote_host)
    dict_set (trans->options, "remote-host",
	      str_to_data (remote_host));

  if (remote_port)
    dict_set (trans->options, "remote-port",
	      str_to_data (remote_port));

  if (transport) {
    char *transport_type = strdup (transport);

    if (strchr (transport_type, ':'))
      *(strchr (transport_type, ':')) = '\0';

    dict_set (trans->options, "transport-type",
	      str_to_data (transport_type));
  }

  xlator_set_type (trans, "protocol/client");

  if (trans->init (trans) != 0)
    return NULL;
}


static int32_t
fetch (glusterfs_ctx_t *ctx,
       FILE *spec_fp,
       const char *remote_host,
       const char *remote_port,
       const char *transport)
{
  xlator_t *this = get_shrub (ctx, remote_host, remote_port, transport);
  //  call_frame_t *frame = get_frame ();
  if (!this)
    return -1;
}


static int32_t
fork_and_fetch (glusterfs_ctx_t *ctx,
		FILE *spec_fp,
		const char *remote_host,
		const char *remote_port,
		const char *transport)
{
  int32_t ret;

  ret = fork ();
  switch (ret) {
  case -1:
    perror ("fork()");
    break;
  case 0:
    /* child */
    ret = fetch (ctx, spec_fp, remote_host, remote_port, transport);
    exit (ret);
  default:
 /* parent */
    wait (&ret);
    ret = WEXITSTATUS (ret);
  }
  return ret;
}

FILE *
fetch_spec (glusterfs_ctx_t *ctx,
	    const char *remote_host,
	    const char *remote_port,
	    const char *transport)
{
  FILE *spec_fp;
  int32_t ret;

  spec_fp = tmpfile ();

  if (!spec_fp) {
    perror (spec_fp);
    return NULL;
  }

  ret = fork_and_fetch (ctx, spec_fp, remote_host, remote_port, transport);

  if (!ret) {
    fseek (spec_fp, 0, SEEK_SET);
  } else {
    fclose (spec_fp);
    spec_fp = NULL;
  }

  return spec_fp;
}
