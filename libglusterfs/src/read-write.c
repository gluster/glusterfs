#include "glusterfs.h"

int
full_write_sock (int fd,
		 const void *data,
		 size_t len)
{
  int total_wrote = 0;
  int ret = 0;
  int to_write = len;

  while (total_wrote < len) {
    ret = write (fd, &data[total_wrote], to_write);
    if (ret < 0) {
      if (errno == EINTR)
	continue;
      break;
    }
    if (ret == 0)
      break;
    if (ret > 0) {
      total_wrote += ret;
      to_write -= ret;
    }
  }
  return total_wrote;
}


int
full_write (struct glusterfs_private *priv,
	    const void *data,
	    size_t len)
{
  int ret;

  ret = full_write_sock (priv->sock, data, len);

  if (ret != len) {
    close (priv->sock);
    priv->sock = -1;
    priv->connected = 0;
    pthread_mutex_destroy (&priv->mutex);
    return -1;
  }

  return ret;
}

int
full_read_sock (int fd,
		char *data,
		size_t len)
{
  int total_read = 0;
  int ret = 0;
  int to_read = len;

  while (total_read < len) {
    ret = read (fd, &data[total_read], to_read);
    if (ret < 0) {
      if (errno == EINTR || errno == EAGAIN)
	continue;
      break;
    }
    if (ret == 0)
      break;
    if (ret > 0) {
      total_read += ret;
      to_read -= ret;
    }
  }
  return total_read;
}

int
full_read (struct glusterfs_private *priv,
	   void *data,
	   size_t len)
{
  int ret = 0;
  char local_buf[512];

  if (!data) {
    while (ret < len) {
      int to_read = (len < 512? len : 512);
      if (full_read_sock (priv->sock, local_buf, to_read) != to_read)
	return (ret + to_read);
      ret += to_read;
    }
  } else {
    ret = full_read_sock (priv->sock, data, len);
  }

  if (ret != len) {
    close (priv->sock);
    priv->sock = -1;
    priv->connected = 0;
    pthread_mutex_destroy (&priv->mutex);
    return -1;
  }
    
  return ret;
}

