/*
  (C) 2006 Z RESEARCH Inc. <http://www.zresearch.com>
  
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
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "logging.h"
#include "common-utils.h"

char *
stripwhite (char *string)
{
  register char *s, *t;

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

char *
get_token (char **line)
{
  char *command;
  while (1)
    {
      command = (char *) strsep ((char **)line, " ,");
      if (!command)
        break;
      if (*(command))
        break;
    }
  
  if (command)
    return strdup (stripwhite (command));
  return command;
}

int32_t 
str2long (char *str, int32_t base, int64_t *l)
{
  int64_t value;
  char *tail = NULL;
  int32_t errnum;
  
  errno = 0;
  value = strtol (str, &tail, base);
  errnum = errno;
  
  if (errnum)
    return (-1); // overflow
  
  if (tail[0] != '\0')
    return (-1); // invalid integer format
  
  *l = value;
  
  return 0;
}

int32_t 
str2ulong (char *str, int32_t base, uint64_t *ul)
{
  int64_t l;
  uint64_t value;
  char *tail = NULL;
  int32_t errnum;
  
  errno = 0;
  l = strtol (str, &tail, base);
  errnum = errno;
  
  if (tail[0] != '\0')
    return (-1); // invalid integer format
  
  if (!errnum)
    {
      if (l < 0)
	return (-1); // minus sign is present
    }
  
  errno = 0;
  value = strtoul (str, &tail, base);
  errnum = errno;
  
  if (errnum)
    return (-1); // overflow
  
  if (tail[0] != '\0')
    return (-1); // invalid integer format
  
  *ul = value;
  
  return 0;
}

int32_t 
str2int (char *str, int32_t base, int32_t *i)
{
  int64_t l;
  
  if (!str2long (str, base, &l))
    {
      if (l >= INT_MIN && l <= INT_MAX)
	{
	  *i = l;
	  return 0;
	}
    }
  
  return (-1);
}

int32_t 
str2uint (char *str, int32_t base, uint32_t *ui)
{
  uint64_t ul;
  
  if (!str2ulong (str, base, &ul))
    {
      if (ul <= UINT_MAX)
	{
	  *ui = ul;
	  return 0;
	}
    }
  
  return (-1);
}

int32_t 
str2double (char *str, double *d)
{
  double value;
  char *tail = NULL;
  int32_t errnum;
  
  if (!(str && d && str[0]))
    return (-1);
  
  errno = 0;
  value = strtod (str, &tail);
  errnum = errno;
  
  if (errnum)
    return (-1);
  
  if (tail[0] != '\0')
    return (-1); // invalid format
  
  *d = value;
  
  return 0;
}

int32_t 
validate_ip_address (char *ip_address)
{
  struct in_addr inp;
  int32_t i;
  int32_t c;
  
  if (!ip_address)
    return (-1);
  
  for (i = 0, c = 0; ip_address[i]; i++)
    {
      if (ip_address[i] == '.')
	c++;
    }
  
  if (c != 3)
    return (-1);
  
  return (!inet_aton (ip_address, &inp));
}

typedef int32_t (*rw_op_t)(int32_t fd, char *buf, int32_t size);
typedef int32_t (*rwv_op_t)(int32_t fd, const struct iovec *buf, int32_t size);

static int32_t 
full_rw (int32_t fd, char *buf, int32_t size, 
	 rw_op_t op)
{
  int32_t bytes_xferd = 0;
  char *p = buf;

  while (bytes_xferd < size) {
    int32_t ret = op (fd, p, size - bytes_xferd);

    if (!ret || (ret < 0 && errno != EINTR)) {
      gf_log ("libglusterfs", GF_LOG_DEBUG, "full_rw: %d bytes r/w instead of %d", bytes_xferd, size);
      gf_log ("libglusterfs", GF_LOG_DEBUG, "full_rw: %s, error string '%s'", buf, strerror (errno));
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
full_read (int32_t fd, char *buf, int32_t size)
{
  return full_rw (fd, buf, size, (rw_op_t)read);
}

/*
  Make sure size bytes are written to the fd from the buf
*/
int32_t 
full_write (int32_t fd, const char *buf, int32_t size)
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
      gf_log ("libglusterfs",
	      GF_LOG_DEBUG,
	      "full_rwv: %d bytes r/w instead of %d",
	      bytes_xferd, 
	      total_len);
      return -1;
    }

    bytes_xferd += ret;

    if (bytes_xferd < total_len) {
      int32_t moved = 0;
      while (moved < ret) {
	if ((ret - moved) >= opvec[0].iov_len) {
	  moved += opvec[0].iov_len;
	  opvec++;
	} else {
	  opvec[0].iov_len -= (ret - moved);
	  opvec[0].iov_base += (ret - moved);
	}
      }
    }
  }

  return 0;
}

int32_t
full_readv (int fd,
	    const struct iovec *vector,
	    int count)
{
  return full_rwv (fd, vector, count, readv);
}

int32_t
full_writev (int fd,
	     const struct iovec *vector,
	     int count)
{
  return full_rwv (fd, vector, count, writev);
}
