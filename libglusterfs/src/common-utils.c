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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_BACKTRACE
#include <execinfo.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <time.h>
#include <locale.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#include "logging.h"
#include "common-utils.h"
#include "revision.h"
#include "glusterfs.h"
#include "stack.h"

typedef int32_t (*rw_op_t)(int32_t fd, char *buf, int32_t size);
typedef int32_t (*rwv_op_t)(int32_t fd, const struct iovec *buf, int32_t size);
static glusterfs_ctx_t *gf_global_ctx;

static int32_t 
full_rw (int32_t fd, char *buf, int32_t size, 
	 rw_op_t op)
{
  int32_t bytes_xferd = 0;
  char *p = buf;

  while (bytes_xferd < size) {
    int32_t ret = op (fd, p, size - bytes_xferd);

    if (!ret || (ret < 0 && errno != EINTR)) {
      return -1;
    }
    
    bytes_xferd += ret;
    /* was: p += bytes_xferd. Took hours to find :O */
    p += ret;
  }

  return 0;
}

/*
  Make sure size bytes are read from the fd into the buf
*/
int32_t 
gf_full_read (int32_t fd, char *buf, int32_t size)
{
  return full_rw (fd, buf, size, (rw_op_t)read);
}

/*
  Make sure size bytes are written to the fd from the buf
*/
int32_t 
gf_full_write (int32_t fd, const char *buf, int32_t size)
{
  return full_rw (fd, (char *)buf, size, (rw_op_t)write);
}

static int32_t
full_rwv (int32_t fd,
	  const struct iovec *vector,
	  int count,
	  rwv_op_t fn)
{
  struct iovec *opvec = alloca (sizeof (*opvec) * count);
  int32_t i;
  int64_t total_len = 0;
  int64_t bytes_xferd = 0;
  int32_t ret;

  memcpy (opvec, vector, count * sizeof (*opvec));
  for (i = 0; i < count; i++)
    total_len += opvec[i].iov_len;

  while (bytes_xferd < total_len) {
    ret = fn (fd, opvec, count);

    if (!ret || (ret < 0 && errno != EINTR)) {
      return -1;
    }

    bytes_xferd += ret;

    if (bytes_xferd < total_len) {
      int32_t moved = 0;
      while (moved < ret) {
	if ((ret - moved) >= opvec[0].iov_len) {
	  moved += opvec[0].iov_len;
	  opvec++;
	  count--;
	} else {
	  opvec[0].iov_len -= (ret - moved);
	  opvec[0].iov_base += (ret - moved);
	  moved += (ret - moved);
	}
      }
    }
  }

  return 0;
}

int32_t
gf_full_readv (int fd,
	       const struct iovec *vector,
	       int count)
{
  return full_rwv (fd, vector, count, (rwv_op_t)readv);
}

int32_t
gf_full_writev (int fd,
		const struct iovec *vector,
		int count)
{
  return full_rwv (fd, vector, count, (rwv_op_t)writev);
}

struct dnscache {
  in_addr_t addrs[17];
  int i;
  int len;
};

#ifndef INADDR_NONE
#define INADDR_NONE 0xffffffff
#endif

in_addr_t
gf_resolve_ip (const char *hostname, void **dnscache)
{
  in_addr_t addr = INADDR_NONE;
  struct hostent *h = NULL;
  struct dnscache *cache = NULL;
  int i;

  if (!*dnscache) {
    *dnscache = calloc (1, sizeof (struct dnscache));
    gf_log ("resolver", GF_LOG_DEBUG,
	    "DNS cache not present, freshly probing hostname: %s",
	    hostname);
    h = gethostbyname (hostname);
    if (!h) {
      free (*dnscache);
      *dnscache = NULL;
      return INADDR_NONE;
    }
    cache = *dnscache;
    cache->len = h->h_length;
    {
      int j = 0;
      for (j = 0; j < 16 && h->h_addr_list[j]; j++)
	memcpy (&cache->addrs[j], h->h_addr_list[j], cache->len);
    }
  }

  cache = *dnscache;

  i = cache->i;
  if (cache->addrs[i]) {
    struct in_addr in;
    memcpy (&addr, &cache->addrs[i], cache->len);
    in.s_addr = (addr);
    gf_log ("resolver", GF_LOG_DEBUG,
	    "returning IP:%s[%d] for hostname: %s",
	    inet_ntoa (in), i, hostname);
  }

  cache->i++; i++;
  if (!cache->addrs[i]) {
    *dnscache = NULL;
    free (cache);
    gf_log ("resolver", GF_LOG_DEBUG,
	    "flushing DNS cache");
  } else {
    struct in_addr in;
    in.s_addr = cache->addrs[i];
    gf_log ("resolver", GF_LOG_DEBUG,
	    "next DNS query will return: %s", inet_ntoa (in));
  }

  return addr;
}

int64_t 
gf_str_to_long_long (const char *number)
{
  int64_t unit = 1;
  int64_t ret = 0;
  char *endptr = NULL ;
  if (!number)
    return 0;

  ret = strtoll (number, &endptr, 0);

  if (endptr) {
    switch (*endptr) {
    case 'G':
    case 'g':
      if ((* (endptr + 1) == 'B') ||(* (endptr + 1) == 'b'))
	unit = 1024 * 1024 * 1024;
      break;
    case 'M':
    case 'm':
      if ((* (endptr + 1) == 'B') ||(* (endptr + 1) == 'b'))
	unit = 1024 * 1024;
      break;
    case 'K':
    case 'k':
      if ((* (endptr + 1) == 'B') ||(* (endptr + 1) == 'b'))
	unit = 1024;
      break;
    case '%':
      unit = 1;
      break;
    default:
      unit = 1;
      break;
    }
  }
  return ret * unit;
}

void 
set_global_ctx_ptr (glusterfs_ctx_t *ctx)
{
  gf_global_ctx = ctx;
}

void 
glusterfs_stats (int32_t signum) 
{
  extern FILE *gf_log_logfile;
  extern glusterfs_ctx_t *gf_global_ctx;
  glusterfs_ctx_t *ctx = gf_global_ctx;
  int fd = fileno (gf_log_logfile);
  char msg[1024];
  char timestr[256];
  time_t utime = time (NULL);
  struct tm *tm = localtime (&utime);
  char *loglevel[] = {"NONE", 
		      "CRITICAL", 
		      "ERROR", 
		      "WARNING", 
		      "NORMAL",
		      "DEBUG"};

  /* Which TLA? What time? Which signal? */
  strftime (timestr, 256, "%Y-%m-%d %H:%M:%S", tm); 
  sprintf (msg, "\nTLA Repo Revision: %s\nTime : %s\nSignal Number : %d\n\n", 
	   GLUSTERFS_REPOSITORY_REVISION, timestr, signum);
  write (fd, msg, strlen (msg));

  /* command line options given */
  sprintf (msg, "%s", ctx->cmd); 
  write (fd, msg, strlen (msg));
  
  if (ctx->specfile) {
    sprintf (msg, " -f %s", ctx->specfile); 
    write (fd, msg, strlen (msg));
  }

  if (ctx->serverip) {
    sprintf (msg, " -s %s", ctx->serverip); 
    write (fd, msg, strlen (msg));
  }

  if (ctx->node_name) {
    sprintf (msg, " -n %s", ctx->node_name); 
    write (fd, msg, strlen (msg));
  }

  if (ctx->logfile) {
    sprintf (msg, " -l %s", ctx->logfile); 
    write (fd, msg, strlen (msg));
  }
  
  if (ctx->loglevel) {
    sprintf (msg, " -L %s", loglevel[ctx->loglevel]); 
    write (fd, msg, strlen (msg));
  }

  if (ctx->pidfile) {
    sprintf (msg, " --pidfile %s", ctx->pidfile); 
    write (fd, msg, strlen (msg));
  }

  if (ctx->mount_point) {
    sprintf (msg, " %s", ctx->mount_point); 
    write (fd, msg, strlen (msg));
  }
  write (fd, "\n", 1);

  /* Specfile layout */
  {
    xlator_t *trav_xl = ctx->graph;
    while (trav_xl) {
      xlator_list_t *child_list = trav_xl->children;
      data_pair_t *trav_opts = (trav_xl->options)?trav_xl->options->members_list:NULL;
      char subvol_str[4096] = {0,};
      char options_str[4096] = {0,};

      /* Get the 'subvolumes' list */
      if (child_list) {
	strcpy (subvol_str, "  subvolumes ");
	while (child_list) {
	  char name[256] = {0,};
	  sprintf (name, "%s ", child_list->xlator->name);
	  strcat (subvol_str, name);
	  child_list = child_list->next;
	}
	strcat (subvol_str, "\n");
      } else {
	/* Keep subvol_str as empty */
	strcpy (subvol_str, "");
      }

      /* Get the options of volume */
      if (trav_opts) {
	while (trav_opts) {
	  char option[256] = {0,};
	  sprintf (option, "  option %s %s\n", trav_opts->key, trav_opts->value->data);
	  strcat (options_str, option);
	  trav_opts = trav_opts->next;
	}
      }
      sprintf (msg, "volume %s\n  type %s%s%send-volume\n\n", 
	       trav_xl->name, trav_xl->type, options_str, subvol_str);
      write (fd, msg, strlen (msg));
      trav_xl = trav_xl->next;
    }
  }
  
  /* Pending frames, (if any), list then in order */
  {
    struct list_head *trav = ((call_pool_t *)ctx->pool)->all_frames.next;
    while (trav != (&((call_pool_t *)ctx->pool)->all_frames)) {
      call_frame_t *tmp = (call_frame_t *)(&((call_ctx_t *)trav)->frames);
      sprintf (msg,"frame : type(%d) op(%d)\n", tmp->type, tmp->op);
      write (fd, msg, strlen (msg));
      trav = trav->next;
    }
    write (fd, "\n", 1);
  }
}


#if HAVE_BACKTRACE
/* Obtain a backtrace and print it to stdout. */
/* TODO: It looks like backtrace_symbols allocates memory,
   it may be problem because mostly memory allocation/free causes 'sigsegv' */
void
gf_print_trace (int32_t signum)
{
  extern FILE *gf_log_logfile;
  void *array[200];
  size_t size;
  int fd = fileno (gf_log_logfile);
  char msg[1024];

  glusterfs_stats (signum);

  /* Print 'backtrace' */
  size = backtrace (array, 200);
  backtrace_symbols_fd (&array[1], size-1, fd);
  sprintf (msg, "---------\n");
  write (fd, msg, strlen (msg));
  
  /* Send a signal to terminate the process */
  signal (signum, SIG_DFL);
  raise (signum);
}
#endif /* HAVE_BACKTRACE */

void
trap (void)
{

}
