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
int64_t total_bytes_xferd;
int64_t total_bytes_rcvd;

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
  if ((void *)op == (void *)write)
    total_bytes_xferd += bytes_xferd;
  else 
    total_bytes_rcvd += bytes_xferd;
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
  struct iovec *opvec = NULL;
  int32_t i;
  int64_t total_len = 0;
  int64_t bytes_xferd = 0;
  int32_t ret;

  opvec = alloca (sizeof (*opvec) * count);
  ERR_ABORT (opvec);


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
  if ((void *)fn == (void *)writev)
    total_bytes_xferd += bytes_xferd;
  else 
    total_bytes_rcvd += bytes_xferd;
  return 0;

  return 0;
}

void
gf_print_bytes ()
{
  gf_log ("", 1, "xfer == %"PRId64", rcvd == %"PRId64"", 
	  total_bytes_xferd, total_bytes_rcvd);
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

struct dnscache6 {
  struct addrinfo *first;
  struct addrinfo *next;
};

int32_t
gf_resolve_ip6 (const char *hostname, 
		uint16_t port, 
		int family, 
		void **dnscache, 
		struct addrinfo **addr_info)
{
  int32_t ret = 0;
  struct addrinfo hints;
  struct dnscache6 *cache = NULL;
  char service[NI_MAXSERV], host[NI_MAXHOST];

  if (!hostname) {
    gf_log ("resolver", GF_LOG_WARNING, "hostname is NULL");
    return -1;
  }

  if (!*dnscache) {
    *dnscache = calloc (1, sizeof (**dnscache));
  }

  cache = *dnscache;
  if (cache->first && !cache->next) {
      freeaddrinfo(cache->first);
      cache->first = cache->next = NULL;
      gf_log ("resolver", GF_LOG_DEBUG,
	      "flushing DNS cache");
    }

  if (!cache->first) {
    char *port_str = NULL;
    gf_log ("resolver", GF_LOG_DEBUG,
	    "DNS cache not present, freshly probing hostname: %s",
	    hostname);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = family;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_ADDRCONFIG;

    asprintf (&port_str, "%d", port);
    if ((ret = getaddrinfo(hostname, port_str, &hints, &cache->first)) != 0) {
      gf_log ("resolver", GF_LOG_ERROR,
	      "getaddrinfo failed (%s)", gai_strerror (ret));

      free (*dnscache);
      *dnscache = NULL;
      free (port_str);
      return -1;
    }
    free (port_str);

    cache->next = cache->first;
  }

  if (cache->next) {
    ret = getnameinfo((struct sockaddr *)cache->next->ai_addr,
		      cache->next->ai_addrlen,
		      host, sizeof (host),
		      service, sizeof (service),
		      NI_NUMERICHOST);
    if (ret != 0) {
      gf_log ("resolver",
	      GF_LOG_ERROR,
	      "getnameinfo failed (%s)", gai_strerror (ret));
      goto err;
    }

    gf_log ("resolver", GF_LOG_DEBUG,
	    "returning ip-%s (port-%s) for hostname: %s and port: %d",
	    host, service, hostname, port);

    *addr_info = cache->next;
  }

  cache->next = cache->next->ai_next;
  if (cache->next) {
    ret = getnameinfo((struct sockaddr *)cache->next->ai_addr,
		      cache->next->ai_addrlen,
		      host, sizeof (host),
		      service, sizeof (service),
		      NI_NUMERICHOST);
    if (ret != 0) {
      gf_log ("resolver",
	      GF_LOG_ERROR,
	      "getnameinfo failed (%s)", gai_strerror (ret));
      goto err;
    }

    gf_log ("resolver", GF_LOG_DEBUG,
	    "next DNS query will return: ip-%s port-%s", host, service);
  }

  return 0;

 err:
  freeaddrinfo (cache->first);
  cache->first = cache->next = NULL;
  free (cache);
  *dnscache = NULL;
  return -1;
}

void 
set_global_ctx_ptr (glusterfs_ctx_t *ctx)
{
  gf_global_ctx = ctx;
}

glusterfs_ctx_t *
get_global_ctx_ptr (void)
{
  return gf_global_ctx;
}

void
gf_dump_spec_file (FILE *specfp)
{
  extern FILE *gf_log_logfile;
  int fd = fileno (gf_log_logfile);
  int specfd = fileno (specfp);
  char msg[1024];
  int ret = -1;
  
  /* hopefully two breaks should break this loop :p */
  write (fd, "\n", 1);
  while (1) 
    {
      ret = read (specfd, msg, 1024);
      if (ret == -1)
	break;
      if (ret == 0)
	break;
      write (fd, msg, ret);
    }
  
  write (fd, "\n", 1);
  fseek (specfp, 0L, SEEK_SET);

  return;
}

void 
gf_dump_config_flags (int fd)
{

  write (fd, "configuration details:\n", 22);

/* have argp */
#ifdef HAVE_ARGP
  write (fd, "argp 1\n", 7);
#endif

/* ifdef if found backtrace */
#ifdef HAVE_BACKTRACE 
  write (fd, "backtrace 1\n", 12);
#endif

/* Berkeley-DB version has cursor->get() */
#ifdef HAVE_BDB_CURSOR_GET 
  write (fd, "bdb->cursor->get 1\n", 19);
#endif

/* Define to 1 if you have the <db.h> header file. */
#ifdef HAVE_DB_H 
  write (fd, "db.h 1\n", 7);
#endif

/* Define to 1 if you have the <dlfcn.h> header file. */
#ifdef HAVE_DLFCN_H 
  write (fd, "dlfcn 1\n", 8);
#endif

/* define if fdatasync exists */
#ifdef HAVE_FDATASYNC 
  write (fd, "fdatasync 1\n", 12);
#endif

/* Define to 1 if you have the `pthread' library (-lpthread). */
#ifdef HAVE_LIBPTHREAD 
  write (fd, "libpthread 1\n", 13);
#endif

/* define if llistxattr exists */
#ifdef HAVE_LLISTXATTR 
  write (fd, "llistxattr 1\n", 13);
#endif

/* define if found setfsuid setfsgid */
#ifdef HAVE_SET_FSID 
  write (fd, "setfsid 1\n", 10);
#endif

/* define if found spinlock */
#ifdef HAVE_SPINLOCK 
  write (fd, "spinlock 1\n", 11);
#endif

/* Define to 1 if you have the <sys/epoll.h> header file. */
#ifdef HAVE_SYS_EPOLL_H 
  write (fd, "epoll.h 1\n", 10);
#endif

/* Define to 1 if you have the <sys/extattr.h> header file. */
#ifdef HAVE_SYS_EXTATTR_H 
  write (fd, "extattr.h 1\n", 12);
#endif

/* Define to 1 if you have the <sys/xattr.h> header file. */
#ifdef HAVE_SYS_XATTR_H 
  write (fd, "xattr.h 1\n", 10);
#endif

/* define if found tv_nsec */
#ifdef HAVE_TV_NSEC 
  write (fd, "tv_nsec 1\n", 10);
#endif

/* Define to the full name and version of this package. */
#ifdef PACKAGE_STRING 
  {
    char msg[128];
    sprintf (msg, "package-string: %s\n", PACKAGE_STRING); 
    write (fd, msg, strlen (msg));
  }
#endif

  return;
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
		      "TRACE",
		      "CRITICAL", 
		      "ERROR", 
		      "WARNING", 
		      "NORMAL",
		      "DEBUG"};

  if (ctx->run_id)
    {
      sprintf (msg, "RunID: %s", ctx->run_id); 
      write (fd, msg, strlen (msg));
    }

#ifdef DEBUG
  gf_dump_config_flags (fd);
#endif

  /* Which TLA? What time? */
  strftime (timestr, 256, "%Y-%m-%d %H:%M:%S", tm); 
  sprintf (msg, "\nVersion: %s %s\nTLA Repo Revision: %s\nTime : %s\n\n", 
	   PACKAGE_NAME, PACKAGE_VERSION, GLUSTERFS_REPOSITORY_REVISION, timestr);
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
}


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

  //glusterfs_stats (signum);
  /* Print the total number of bytes got transfered */
  gf_print_bytes ();

  /* Pending frames, (if any), list them in order */
  write (fd, "pending frames:\n", 16);
  {
    extern glusterfs_ctx_t *gf_global_ctx;
    glusterfs_ctx_t *ctx = gf_global_ctx;
    struct list_head *trav = ((call_pool_t *)ctx->pool)->all_frames.next;
    while (trav != (&((call_pool_t *)ctx->pool)->all_frames)) {
      call_frame_t *tmp = (call_frame_t *)(&((call_ctx_t *)trav)->frames);
      sprintf (msg,"frame : type(%d) op(%d)\n", tmp->type, tmp->op);
      write (fd, msg, strlen (msg));
      trav = trav->next;
    }
    write (fd, "\n", 1);
  }

  sprintf (msg, "Signal received: %d", signum); 
  write (fd, msg, strlen (msg));

#if HAVE_BACKTRACE
  /* Print 'backtrace' */
  size = backtrace (array, 200);
  backtrace_symbols_fd (&array[1], size-1, fd);
  sprintf (msg, "---------\n");
  write (fd, msg, strlen (msg));
#endif /* HAVE_BACKTRACE */
  
  /* Send a signal to terminate the process */
  signal (signum, SIG_DFL);
  raise (signum);
}

void
trap (void)
{

}

char *
gf_trim (char *string)
{
  register char *s, *t;
  
  if (string == NULL)
    {
      return NULL;
    }
  
  for (s = string; isspace (*s); s++)
    ;
  
  if (*s == 0)
    return s;
  
  t = s + strlen (s) - 1;
  while (t > s && isspace (*t))
    t--;
  *++t = '\0';
  
  return s;
}

int 
gf_strsplit (const char *str, const char *delim, 
	     char ***tokens, int *token_count)
{
  char *_running = NULL;
  char *running = NULL;
  char *token = NULL;
  char **token_list = NULL;
  int count = 0;
  int i = 0;
  int j = 0;
  
  if (str == NULL || delim == NULL || tokens == NULL || token_count == NULL)
    {
      return -1;
    }
  
  if ((_running = strdup (str)) == NULL)
    {
      return -1;
    }
  running = _running;
  
  while ((token = strsep (&running, delim)) != NULL)
    {
      if (token[0] != '\0')
	count++;
    }
  free (_running);
  
  if ((_running = strdup (str)) == NULL)
    {
      return -1;
    }
  running = _running;
  
  if ((token_list = calloc (count, sizeof (char *))) == NULL)
    {
      free (_running);
      return -1;
    }
  
  while ((token = strsep (&running, delim)) != NULL)
    {
      if (token[0] == '\0')
	continue;
      
      if ((token_list[i++] = strdup (token)) == NULL)
	goto free_exit;
    }
  
  free (_running);
  
  *tokens = token_list;
  *token_count = count;
  return 0;
  
 free_exit:
  free (_running);
  for (j = 0; j < i; j++)
    {
      free (token_list[j]);
    }
  free (token_list);
  return -1;
}

int 
gf_volume_name_validate (const char *volume_name)
{
  const char *vname = NULL;
  
  if (volume_name == NULL)
    {
      return -1;
    }
  
  if (!isalpha (volume_name[0]))
    {
      return 1;
    }
  
  for (vname = &volume_name[1]; *vname != '\0'; vname++)
    {
      if (!(isalnum (*vname) || *vname == '_'))
	return 1;
    }
  
  return 0;
}

static int 
_gf_string2long (const char *str, long *n, int base)
{
  long value = 0;
  char *tail = NULL;
  int old_errno = 0;
  
  if (str == NULL || n == NULL)
    {
      errno = EINVAL;
      return -1;
    }
  
  old_errno = errno;
  errno = 0;
  value = strtol (str, &tail, base);
  
  if (errno == ERANGE || errno == EINVAL)
    {
      return -1;
    }
  
  if (errno == 0)
    {
      errno = old_errno;
    }
  
  if (tail[0] != '\0')
    {
      /* bala: invalid integer format */
      return -1;
    }
  
  *n = value;
  
  return 0;
}

static int 
_gf_string2ulong (const char *str, unsigned long *n, int base)
{
  unsigned long value = 0;
  char *tail = NULL;
  int old_errno = 0;
  const char *s = NULL;
  
  if (str == NULL || n == NULL)
    {
      errno = EINVAL;
      return -1;
    }
  
  for (s = str; *s != '\0'; s++)
    {
      if (isspace (*s))
	{
	  continue;
	}
      if (*s == '-')
	{
	  /* bala: we do not support suffixed (-) sign and 
	     invalid integer format */
	  return -1;
	}
      break;
    }
  
  old_errno = errno;
  errno = 0;
  value = strtoul (str, &tail, base);
  
  if (errno == ERANGE || errno == EINVAL)
    {
      return -1;
    }
  
  if (errno == 0)
    {
      errno = old_errno;
    }
  
  if (tail[0] != '\0')
    {
      /* bala: invalid integer format */
      return -1;
    }
  
  *n = value;
  
  return 0;
}

static int 
_gf_string2longlong (const char *str, long long *n, int base)
{
  long long value = 0;
  char *tail = NULL;
  int old_errno = 0;
  
  if (str == NULL || n == NULL)
    {
      errno = EINVAL;
      return -1;
    }
  
  old_errno = errno;
  errno = 0;
  value = strtoll (str, &tail, base);
  
  if (errno == ERANGE || errno == EINVAL)
    {
      return -1;
    }
  
  if (errno == 0)
    {
      errno = old_errno;
    }
  
  if (tail[0] != '\0')
    {
      /* bala: invalid integer format */
      return -1;
    }
  
  *n = value;
  
  return 0;
}

static int 
_gf_string2ulonglong (const char *str, unsigned long long *n, int base)
{
  unsigned long long value = 0;
  char *tail = NULL;
  int old_errno = 0;
  const char *s = NULL;
  
  if (str == NULL || n == NULL)
    {
      errno = EINVAL;
      return -1;
    }
  
  for (s = str; *s != '\0'; s++)
    {
      if (isspace (*s))
	{
	  continue;
	}
      if (*s == '-')
	{
	  /* bala: we do not support suffixed (-) sign and 
	     invalid integer format */
	  return -1;
	}
      break;
    }
  
  old_errno = errno;
  errno = 0;
  value = strtoull (str, &tail, base);
  
  if (errno == ERANGE || errno == EINVAL)
    {
      return -1;
    }
  
  if (errno == 0)
    {
      errno = old_errno;
    }
  
  if (tail[0] != '\0')
    {
      /* bala: invalid integer format */
      return -1;
    }
  
  *n = value;
  
  return 0;
}

int 
gf_string2long (const char *str, long *n)
{
  return _gf_string2long (str, n, 0);
}

int 
gf_string2ulong (const char *str, unsigned long *n)
{
  return _gf_string2ulong (str, n, 0);
}

int 
gf_string2int (const char *str, int *n)
{
  return _gf_string2long (str, (long *) n, 0);
}

int 
gf_string2uint (const char *str, unsigned int *n)
{
  return _gf_string2ulong (str, (unsigned long *) n, 0);
}

int 
gf_string2longlong (const char *str, long long *n)
{
  return _gf_string2longlong (str, n, 0);
}

int 
gf_string2ulonglong (const char *str, unsigned long long *n)
{
  return _gf_string2ulonglong (str, n, 0);
}

int 
gf_string2int8 (const char *str, int8_t *n)
{
  long l = 0L;
  int rv = 0;
  
  rv = _gf_string2long (str, &l, 0);
  if (rv != 0)
    return rv;
  
  if (l >= INT8_MIN && l <= INT8_MAX)
    {
      *n = (int8_t) l;
      return 0;
    }
  
  errno = ERANGE;
  return -1;
}

int 
gf_string2int16 (const char *str, int16_t *n)
{
  long l = 0L;
  int rv = 0;
  
  rv = _gf_string2long (str, &l, 0);
  if (rv != 0)
    return rv;
  
  if (l >= INT16_MIN && l <= INT16_MAX)
    {
      *n = (int16_t) l;
      return 0;
    }
  
  errno = ERANGE;
  return -1;
}

int 
gf_string2int32 (const char *str, int32_t *n)
{
  long l = 0L;
  int rv = 0;
  
  rv = _gf_string2long (str, &l, 0);
  if (rv != 0)
    return rv;
  
  if (l >= INT32_MIN && l <= INT32_MAX)
    {
      *n = (int32_t) l;
      return 0;
    }
  
  errno = ERANGE;
  return -1;
}

int 
gf_string2int64 (const char *str, int64_t *n)
{
  long long l = 0LL;
  int rv = 0;
  
  rv = _gf_string2longlong (str, &l, 0);
  if (rv != 0)
    return rv;
  
  if (l >= INT64_MIN && l <= INT64_MAX)
    {
      *n = (int64_t) l;
      return 0;
    }
  
  errno = ERANGE;
  return -1;
}

int 
gf_string2uint8 (const char *str, uint8_t *n)
{
  unsigned long l = 0L;
  int rv = 0;
  
  rv = _gf_string2ulong (str, &l, 0);
  if (rv != 0)
    return rv;
  
  if (l >= 0 && l <= UINT8_MAX)
    {
      *n = (uint8_t) l;
      return 0;
    }
  
  errno = ERANGE;
  return -1;
}

int 
gf_string2uint16 (const char *str, uint16_t *n)
{
  unsigned long l = 0L;
  int rv = 0;
  
  rv = _gf_string2ulong (str, &l, 0);
  if (rv != 0)
    return rv;
  
  if (l >= 0 && l <= UINT16_MAX)
    {
      *n = (uint16_t) l;
      return 0;
    }
  
  errno = ERANGE;
  return -1;
}

int 
gf_string2uint32 (const char *str, uint32_t *n)
{
  unsigned long l = 0L;
  int rv = 0;
  
  rv = _gf_string2ulong (str, &l, 0);
  if (rv != 0)
    return rv;
  
  if (l >= 0 && l <= UINT32_MAX)
    {
      *n = (uint32_t) l;
      return 0;
    }
  
  errno = ERANGE;
  return -1;
}

int 
gf_string2uint64 (const char *str, uint64_t *n)
{
  unsigned long long l = 0ULL;
  int rv = 0;
  
  rv = _gf_string2ulonglong (str, &l, 0);
  if (rv != 0)
    return rv;
  
  if (l >= 0 && l <= UINT64_MAX)
    {
      *n = (uint64_t) l;
      return 0;
    }
  
  errno = ERANGE;
  return -1;
}

int 
gf_string2ulong_base10 (const char *str, unsigned long *n)
{
  return _gf_string2ulong (str, n, 10);
}

int 
gf_string2uint_base10 (const char *str, unsigned int *n)
{
  return _gf_string2ulong (str, (unsigned long *) n, 10);
}

int 
gf_string2uint8_base10 (const char *str, uint8_t *n)
{
  unsigned long l = 0L;
  int rv = 0;
  
  rv = _gf_string2ulong (str, &l, 10);
  if (rv != 0)
    return rv;
  
  if (l >= 0 && l <= UINT8_MAX)
    {
      *n = (uint8_t) l;
      return 0;
    }
  
  errno = ERANGE;
  return -1;
}

int 
gf_string2uint16_base10 (const char *str, uint16_t *n)
{
  unsigned long l = 0L;
  int rv = 0;
  
  rv = _gf_string2ulong (str, &l, 10);
  if (rv != 0)
    return rv;
  
  if (l >= 0 && l <= UINT16_MAX)
    {
      *n = (uint16_t) l;
      return 0;
    }
  
  errno = ERANGE;
  return -1;
}

int 
gf_string2uint32_base10 (const char *str, uint32_t *n)
{
  unsigned long l = 0L;
  int rv = 0;
  
  rv = _gf_string2ulong (str, &l, 10);
  if (rv != 0)
    return rv;
  
  if (l >= 0 && l <= UINT32_MAX)
    {
      *n = (uint32_t) l;
      return 0;
    }
  
  errno = ERANGE;
  return -1;
}

int 
gf_string2uint64_base10 (const char *str, uint64_t *n)
{
  unsigned long long l = 0ULL;
  int rv = 0;
  
  rv = _gf_string2ulonglong (str, &l, 10);
  if (rv != 0)
    return rv;
  
  if (l >= 0 && l <= UINT64_MAX)
    {
      *n = (uint64_t) l;
      return 0;
    }
  
  errno = ERANGE;
  return -1;
}

int 
gf_string2bytesize (const char *str, size_t *n)
{
  uint64_t value = 0ULL;
  char *tail = NULL;
  int old_errno = 0;
  const char *s = NULL;
  
  if (str == NULL || n == NULL)
    {
      errno = EINVAL;
      return -1;
    }
  
  for (s = str; *s != '\0'; s++)
    {
      if (isspace (*s))
	{
	  continue;
	}
      if (*s == '-')
	{
	  /* bala: we do not support suffixed (-) sign and 
	     invalid integer format */
	  return -1;
	}
      break;
    }
  
  old_errno = errno;
  errno = 0;
  value = strtoull (str, &tail, 10);
  
  if (errno == ERANGE || errno == EINVAL)
    {
      return -1;
    }
  
  if (errno == 0)
    {
      errno = old_errno;
    }
  
  if (tail[0] != '\0')
    {
      if (strcasecmp (tail, GF_UNIT_KB_STRING) == 0)
	{
	  value *= GF_UNIT_KB;
	}
      else if (strcasecmp (tail, GF_UNIT_MB_STRING) == 0)
	{
	  value *= GF_UNIT_MB;
	}
      else if (strcasecmp (tail, GF_UNIT_GB_STRING) == 0)
	{
	  value *= GF_UNIT_GB;
	}
      else if (strcasecmp (tail, GF_UNIT_TB_STRING) == 0)
	{
	  value *= GF_UNIT_TB;
	}
      else if (strcasecmp (tail, GF_UNIT_PB_STRING) == 0)
	{
	  value *= GF_UNIT_PB;
	}
      else 
	{
	  /* bala: invalid integer format */
	  return -1;
	}
    }
  
  *n = value;
  
  return 0;
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
