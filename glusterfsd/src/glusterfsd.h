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
          char readbuf[80] = {0,}; \
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
  char *path;
};

struct held_locks {
  struct held_locks *next;
  char *path;
};

struct sock_private {
  struct file_ctx_list *fctxl;
  struct held_locks *locks;
  struct xlator *xl;
  int fd;
  void *private;
};

struct gfsd_fns {
  int (*function) (struct sock_private *sock_priv);
};

struct confd {
  char *chroot_dir;
  char *scratch_dir;
  char *inet_prot;
  int key_len;
  int port;
  char *bind_ip_address;
  // add few more things if needed
};

typedef struct gfsd_fns glusterfsd_fn_t;

int glusterfsd_getattr (struct sock_private *sock_priv);
int glusterfsd_readlink (struct sock_private *sock_priv);
int glusterfsd_mknod (struct sock_private *sock_priv);
int glusterfsd_mkdir (struct sock_private *sock_priv);
int glusterfsd_unlink (struct sock_private *sock_priv);
int glusterfsd_rmdir (struct sock_private *sock_priv);
int glusterfsd_symlink (struct sock_private *sock_priv);
int glusterfsd_rename (struct sock_private *sock_priv);
int glusterfsd_link (struct sock_private *sock_priv);
int glusterfsd_chmod (struct sock_private *sock_priv);
int glusterfsd_chown (struct sock_private *sock_priv);
int glusterfsd_truncate (struct sock_private *sock_priv);
int glusterfsd_utime (struct sock_private *sock_priv);
int glusterfsd_open (struct sock_private *sock_priv);
int glusterfsd_read (struct sock_private *sock_priv);
int glusterfsd_write (struct sock_private *sock_priv);
int glusterfsd_statfs (struct sock_private *sock_priv);
int glusterfsd_flush (struct sock_private *sock_priv);
int glusterfsd_release (struct sock_private *sock_priv);
int glusterfsd_fsync (struct sock_private *sock_priv);
int glusterfsd_setxattr (struct sock_private *sock_priv);
int glusterfsd_getxattr (struct sock_private *sock_priv);
int glusterfsd_listxattr (struct sock_private *sock_priv);
int glusterfsd_removexattr (struct sock_private *sock_priv);
int glusterfsd_opendir (struct sock_private *sock_priv);
int glusterfsd_readdir (struct sock_private *sock_priv);
int glusterfsd_releasedir (struct sock_private *sock_priv);
int glusterfsd_fsyncdir (struct sock_private *sock_priv);
int glusterfsd_init (struct sock_private *sock_priv);
int glusterfsd_destroy (struct sock_private *sock_priv);
int glusterfsd_access (struct sock_private *sock_priv);
int glusterfsd_create (struct sock_private *sock_priv);
int glusterfsd_ftruncate (struct sock_private *sock_priv);
int glusterfsd_fgetattr (struct sock_private *sock_priv);
int glusterfsd_stats (struct sock_private *sock_priv);
int glusterfsd_bulk_getattr (struct sock_private *sock_priv);

int glusterfsd_getvolume (struct sock_private *sock_priv);
int glusterfsd_setvolume (struct sock_private *sock_priv);
int glusterfsd_lock (struct sock_private *sock_priv);
int glusterfsd_unlock (struct sock_private *sock_priv);
int glusterfsd_listlocks (struct sock_private *sock_priv);
int glusterfsd_nslookup (struct sock_private *sock_priv);
int glusterfsd_nsupdate (struct sock_private *sock_priv);

int glusterfsd_getspec (struct sock_private *sock_priv);
int glusterfsd_setspec (struct sock_private *sock_priv);
int handle_fops (glusterfsd_fn_t *gfopsd, struct sock_private *sock_priv);
int handle_mgmt (glusterfsd_fn_t *gmgmtd, struct sock_private *sock_priv);
struct xlator *gf_get_xlator_tree_node (void);

