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

#include <stdint.h>

#include "glusterfs.h"
#include "logging.h"
#include "xlator.h"
#include "glusterfs.h"
#include "transport.h"

#include <fuse.h>
#include <fuse/fuse_lowlevel.h>

#include "fuse-internals.h"

static int32_t
fuse_transport_send (transport_t *this)
{

  return 0;
}

static int32_t
fuse_transport_receive (transport_t *this)
{


  return 0;
}

static int32_t
fuse_transport_submit (transport_t *this)
{


  return 0;
}

static int32_t
fuse_transport_except (transport_t *this)
{

  return 0;
}

static int32_t
fuse_transport_init (transport_t *this)
{

  return 0;
}

static int32_t
fuse_transport_fini (transport_t *this)
{


  return 0;
}


static int32_t
fuse_transport_notify (xlator_t *xl, transport_t *trans)
{


  return 0;
}

static struct transport_ops fuse_transport_ops = {
  .send = NULL,
  .recieve = NULL,
  .submit = NULL,
  .except = NULL
};

static transport_t fuse_transport = {
  .ops = &fuse_transport_ops,
  .private = NULL,
  .xl = NULL,
  .init = fuse_transport_init,
  .fini = fuse_transport_fini,
  .notify = fuse_transport_notify
};




/*
static void fuse_destroy(struct fuse *f)
{
    size_t i;
    for (i = 0; i < f->id_table_size; i++) {
        struct node *node;

        for (node = f->id_table[i]; node != NULL; node = node->id_next) {
            if (node->is_hidden) {
                char *path = get_path(f, node->nodeid);
                if (path) {
                    f->op.unlink(path);
                    free(path);
                }
            }
        }
    }
    for (i = 0; i < f->id_table_size; i++) {
        struct node *node;
        struct node *next;

        for (node = f->id_table[i]; node != NULL; node = next) {
            next = node->id_next;
            free_node(node);
        }
    }
    free(f->id_table);
    free(f->name_table);
    pthread_mutex_destroy(&f->lock);
    fuse_session_destroy(f->se);
    free(f);
}

*/



struct fuse *
glusterfs_fuse_setup (int argc, 
		      char *argv[],
		      char *mountpoint,
		      int *fd)

{
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  struct fuse *fuse;
  int res;

    /* set mountpoint */
    /* set multithreaded */

    *fd = fuse_mount(mountpoint, &args);
    if (*fd == -1) {
        fuse_opt_free_args(&args);
        goto err_free;
    }

    fuse = glusterfs_fuse_new_common(*fd, &args);
    fuse_opt_free_args(&args);
    if (fuse == NULL)
        goto err_unmount;

    res = fuse_set_signal_handlers(fuse->se);
    if (res == -1)
        goto err_destroy;

    return fuse;

 err_destroy:
    fuse_destroy(fuse);
 err_unmount:
    fuse_unmount(mountpoint);
 err_free:
    free(mountpoint);
    return NULL;
}


int
fuse_loop_wrapper (struct fuse *fuse)
{
  int res = 0;
  struct fuse_session *se = fuse->se;
  struct fuse_chan *ch = fuse_session_next_chan(se, NULL);
  size_t bufsize = fuse_chan_bufsize(ch);
  char *buf = (char *) malloc(bufsize);
  if (!buf) {
    fprintf(stderr, "fuse: failed to allocate read buffer\n");
    return -1;
  }

  while (!fuse_session_exited(se)) {
    res = fuse_chan_receive(ch, buf, bufsize);
    if (!res)
      continue;
    if (res == -1)
      break;
    fuse_session_process(se, buf, res, ch);
    res = 0;
  }

  free(buf);
  fuse_session_reset(se);
  return res;
}


int32_t
glusterfs_mount (xlator_t *graph,
		 const char *mount_point)
{
  struct fuse *fuse;
  int res;
  int fd;
  char *mountpoint = strdup (mount_point);
  int argc = 7;
  char *argv[] = { "glusterfs",
                   "-o",
                   "nonempty",
                   "-o",
                   "allow_other",
                   "-o",
                   "default_permissions",
                   NULL };



  fuse = glusterfs_fuse_setup(argc, argv, mountpoint, &fd);

  if (fuse == NULL)
    return 1;


  fuse_transport.private = (void *) fuse;
  fuse_transport.xl = graph;

  res = fuse_loop_wrapper (fuse);

  fuse_teardown(fuse, fd, mountpoint);
  if (res == -1)
    return 1;

  return 0;
}

