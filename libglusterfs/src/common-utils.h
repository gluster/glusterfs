/*
  (C) 2006,2007 Z RESEARCH Inc. <http://www.zresearch.com>
  
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

#ifndef _COMMON_UTILS_H
#define _COMMON_UTILS_H

#include <stdint.h>
#include <sys/uio.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>


#define EEEEKS (void *)0xeeeeeeee

#ifdef DEBUG

#define freee(ptr) do { assert (ptr != EEEEKS); free ((void *)ptr); ptr = EEEEKS; } while(0)
#define TRAP_ON(cond) if (cond) { gf_log ("trapper", GF_LOG_CRITICAL, "condition `%s' failed", #cond); trap (); }

#else /* DEBUG */

#define freee(ptr) free((void *)ptr)
#define TRAP_ON(cond)

#endif /* DEBUG */

int32_t gf_full_read (int32_t fd, char *buf, int32_t size);
int32_t gf_full_write (int32_t fd, const char *buf, int32_t size);

int32_t gf_full_readv (int32_t fd, const struct iovec *vector, int32_t count);
int32_t gf_full_writev (int32_t fd, const struct iovec *vector, int32_t count);
in_addr_t gf_resolve_ip (const char *hostname);
int64_t gf_str_to_long_long (const char *number);

#if HAVE_BACKTRACE
void gf_print_trace (int32_t signal);
#endif /* HAVE_BACKTRACE */

#define VECTORSIZE(count) (count * (sizeof (struct iovec)))

#define LOCK_INIT(x)    pthread_mutex_init (x, NULL);
#define LOCK(x)         pthread_mutex_lock (x);
#define UNLOCK(x)       pthread_mutex_unlock (x);
#define LOCK_DESTROY(x) pthread_mutex_destroy (x);


static inline void
iov_free (struct iovec *vector,
	  int32_t count)
{
  int i;

  for (i=0; i<count; i++)
    freee (vector[i].iov_base);

  freee (vector);
}

static inline int32_t
iov_length (const struct iovec *vector,
	    int32_t count)
{
  int32_t i;
  size_t size = 0;

  for (i=0; i<count; i++)
    size += vector[i].iov_len;

  return size;
}

static inline struct iovec *
iov_dup (struct iovec *vector,
	 int32_t count)
{
  int32_t bytecount = (count * sizeof (struct iovec));
  int32_t i;
  struct iovec *newvec = malloc (bytecount);

  for (i=0;i<count;i++) {
    newvec[i].iov_len = vector[i].iov_len;
    newvec[i].iov_base = vector[i].iov_base;
  }

  return newvec;
}

static inline int32_t
iov_subset (struct iovec *orig,
	    int32_t orig_count,
	    off_t src_offset,
	    off_t dst_offset,
	    struct iovec *new)
{
  int32_t new_count = 0;
  int32_t i;
  off_t offset = 0;

  for (i=0; i<orig_count; i++) {
    if ((offset + orig[i].iov_len >= src_offset) && 
	(offset <= dst_offset)) {
      if (new) {
	size_t start_offset = 0, end_offset = orig[i].iov_len;
	if (src_offset >= offset) {
	  start_offset = (src_offset - offset);
	}
	if (dst_offset <= (offset + orig[i].iov_len)) {
	  end_offset = (dst_offset - offset);
	}
	new[new_count].iov_base = orig[i].iov_base + start_offset;
	new[new_count].iov_len = end_offset - start_offset;
      }
      new_count++;
    }
    offset += orig[i].iov_len;
  }

  return new_count;
}

static inline void
iov_unload (char *buf,
	    const struct iovec *vector,
	    int32_t count)
{
  int32_t i;
  int32_t copied = 0;

  for (i=0; i<count; i++) {
    memcpy (buf + copied, vector[i].iov_base, vector[i].iov_len);
    copied += vector[i].iov_len;
  }
}

#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#define roof(a,b) ((((a)+(b)-1)/((b)?(b):1))*(b))
#define floor(a,b) (((a)/((b)?(b):1))*(b))


void trap (void);

static inline void *
memdup (const void *ptr, size_t size)
{
  void *newptr = malloc (size);
  memcpy (newptr, ptr, size);
  return newptr;
}

#endif /* _COMMON_UTILS_H */

