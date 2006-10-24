/*
  (C) 2006 Gluster core team <http://www.gluster.org/>
  
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

#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"

#define DEFAULT_LOG_FILE   DATADIR"/log/glusterfs/glusterfsd.log"

#define GLUSTERFSD_SPEC_DIR    "/var/state/glusterfs"
#define GLUSTERFSD_SPEC_PATH   "/var/state/glusterfs/client-volume.spec"

#define GF_YES 1
#define GF_NO  0

#define CHECK_ENDMGMT() do {\
	  fgets (readbuf, 80, fp); \
	  if (strcasecmp (readbuf, "EndMgmt\n") != 0) { \
	    fprintf (stderr, "Protocol Error: 'EndMgmt' not found\n");\
            if (dict) \
              dict_destroy (dict);\
	    return -1;\
	  }\
        } while (0)

#define CHECK_ENDFOPS() do {\
          int8_t readbuf[80] = {0,}; \
	  fgets (readbuf, 80, fp); \
	  if (strcasecmp (readbuf, "EndFops\n") != 0) { \
	    fprintf (stderr, "Protocol Error: 'EndFops' not found\n");\
            if (dict) \
              dict_destroy (dict);\
	    return -1;\
	  }\
        } while (0)


struct file_ctx_list {
  struct file_ctx_list *next;
  struct file_context *ctx;
  int8_t *path;
};

struct held_locks {
  struct held_locks *next;
  int8_t *path;
};

struct write_list {
  struct write_list *next;
  int8_t *buf;
  int32_t len;
};

struct sock_private {
  struct file_ctx_list *fctxl;
  struct held_locks *locks;
  struct write_list *send_list;
  struct xlator *xl;
  int32_t send_buf_count;
  int32_t fd;
  void *private;
};

struct gfsd_fns {
  int32_t (*function) (struct sock_private *sock_priv);
};

struct confd {
  int8_t *chroot_dir;
  int8_t *scratch_dir;
  int8_t *inet_prot;
  int32_t key_len;
  int32_t port;
  int8_t *bind_ip_address;
  // add few more things if needed
};

typedef struct gfsd_fns glusterfsd_fn_t;

int32_t glusterfsd_getattr (struct sock_private *sock_priv);
int32_t glusterfsd_readlink (struct sock_private *sock_priv);
int32_t glusterfsd_mknod (struct sock_private *sock_priv);
int32_t glusterfsd_mkdir (struct sock_private *sock_priv);
int32_t glusterfsd_unlink (struct sock_private *sock_priv);
int32_t glusterfsd_rmdir (struct sock_private *sock_priv);
int32_t glusterfsd_symlink (struct sock_private *sock_priv);
int32_t glusterfsd_rename (struct sock_private *sock_priv);
int32_t glusterfsd_link (struct sock_private *sock_priv);
int32_t glusterfsd_chmod (struct sock_private *sock_priv);
int32_t glusterfsd_chown (struct sock_private *sock_priv);
int32_t glusterfsd_truncate (struct sock_private *sock_priv);
int32_t glusterfsd_utime (struct sock_private *sock_priv);
int32_t glusterfsd_open (struct sock_private *sock_priv);
int32_t glusterfsd_read (struct sock_private *sock_priv);
int32_t glusterfsd_write (struct sock_private *sock_priv);
int32_t glusterfsd_statfs (struct sock_private *sock_priv);
int32_t glusterfsd_flush (struct sock_private *sock_priv);
int32_t glusterfsd_release (struct sock_private *sock_priv);
int32_t glusterfsd_fsync (struct sock_private *sock_priv);
int32_t glusterfsd_setxattr (struct sock_private *sock_priv);
int32_t glusterfsd_getxattr (struct sock_private *sock_priv);
int32_t glusterfsd_listxattr (struct sock_private *sock_priv);
int32_t glusterfsd_removexattr (struct sock_private *sock_priv);
int32_t glusterfsd_opendir (struct sock_private *sock_priv);
int32_t glusterfsd_readdir (struct sock_private *sock_priv);
int32_t glusterfsd_releasedir (struct sock_private *sock_priv);
int32_t glusterfsd_fsyncdir (struct sock_private *sock_priv);
int32_t glusterfsd_init (struct sock_private *sock_priv);
int32_t glusterfsd_destroy (struct sock_private *sock_priv);
int32_t glusterfsd_access (struct sock_private *sock_priv);
int32_t glusterfsd_create (struct sock_private *sock_priv);
int32_t glusterfsd_ftruncate (struct sock_private *sock_priv);
int32_t glusterfsd_fgetattr (struct sock_private *sock_priv);
int32_t glusterfsd_stats (struct sock_private *sock_priv);
int32_t glusterfsd_bulk_getattr (struct sock_private *sock_priv);

int32_t glusterfsd_getvolume (struct sock_private *sock_priv);
int32_t glusterfsd_setvolume (struct sock_private *sock_priv);
int32_t glusterfsd_lock (struct sock_private *sock_priv);
int32_t glusterfsd_unlock (struct sock_private *sock_priv);
int32_t glusterfsd_listlocks (struct sock_private *sock_priv);
int32_t glusterfsd_nslookup (struct sock_private *sock_priv);
int32_t glusterfsd_nsupdate (struct sock_private *sock_priv);

int32_t glusterfsd_getspec (struct sock_private *sock_priv);
int32_t glusterfsd_setspec (struct sock_private *sock_priv);
int32_t handle_fops (glusterfsd_fn_t *gfopsd, struct sock_private *sock_priv);
int32_t handle_mgmt (glusterfsd_fn_t *gmgmtd, struct sock_private *sock_priv);
struct xlator *gf_get_xlator_tree_node (void);

