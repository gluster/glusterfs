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


#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"

/*
  option fixed-uid <uid>
   - makes all files/dirs 'owned' by <uid>
  option fixed-gid <gid>
   - nakes all files/dirs 'owned' by <gid>
*/

/* Calls which return at this level */

struct fixed_id {
  uid_t fixed_uid;
  char fixed_uid_set;
  gid_t fixed_gid;
  char fixed_gid_set;
};

static void
update_stat (struct stat *buf,
	     struct fixed_id *id)
{

  if (id->fixed_uid_set)
    buf->st_uid = id->fixed_uid;

  if (id->fixed_gid_set)
    buf->st_gid = id->fixed_gid;

}

static int32_t
fixed_id_generic_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      struct stat *buf)
{
  if (op_ret >= 0)
    update_stat (buf, this->private);

  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t 
fixed_id_getattr (call_frame_t *frame,
		  xlator_t *this,
		  const char *path)
{
  STACK_WIND (frame,
	      fixed_id_generic_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->getattr,
	      path);

  return 0;
}

static int32_t
fixed_id_chmod (call_frame_t *frame,
		xlator_t *this,
		const char *path,
		mode_t mode)
{
  STACK_WIND (frame,
	      fixed_id_generic_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->chmod,
	      path,
	      mode);

  return 0;
}


static int32_t
fixed_id_chown (call_frame_t *frame,
		xlator_t *this,
		const char *path,
		uid_t uid,
		gid_t gid)
{
  STACK_WIND (frame,
	      fixed_id_generic_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->chown,
	      path,
	      uid,
	      gid);

  return 0;
}


static int32_t
fixed_id_truncate (call_frame_t *frame,
		   xlator_t *this,
		   const char *path,
		   off_t offset)

{
  STACK_WIND (frame,
	      fixed_id_generic_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->truncate,
	      path,
	      offset);

  return 0;
}


static int32_t
fixed_id_ftruncate (call_frame_t *frame,
		    xlator_t *this,
		    dict_t *fd_ctx,
		    off_t offset)

{
  STACK_WIND (frame,
	      fixed_id_generic_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->ftruncate,
	      fd_ctx,
	      offset);

  return 0;
}


static int32_t
fixed_id_utimes (call_frame_t *frame,
		 xlator_t *this,
		 const char *path,
		 struct timespec *buf)

{
  STACK_WIND (frame,
	      fixed_id_generic_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->utimes,
	      path,
	      buf);

  return 0;
}


static int32_t
fixed_id_mknod (call_frame_t *frame,
		xlator_t *this,
		const char *path,
		mode_t mode,
		dev_t dev)

{
  STACK_WIND (frame,
	      fixed_id_generic_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->mknod,
	      path,
	      mode,
	      dev);

  return 0;
}


static int32_t
fixed_id_mkdir (call_frame_t *frame,
		xlator_t *this,
		const char *path,
		mode_t mode)

{
  STACK_WIND (frame,
	      fixed_id_generic_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->mkdir,
	      path,
	      mode);

  return 0;
}


static int32_t
fixed_id_symlink (call_frame_t *frame,
		  xlator_t *this,
		  const char *oldpath,
		  const char *newpath)

{
  STACK_WIND (frame,
	      fixed_id_generic_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->symlink,
	      oldpath,
	      newpath);

  return 0;
}


static int32_t
fixed_id_link (call_frame_t *frame,
	       xlator_t *this,
	       const char *oldpath,
	       const char *newpath)

{
  STACK_WIND (frame,
	      fixed_id_generic_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->link,
	      oldpath,
	      newpath);

  return 0;
}


static int32_t
fixed_id_fd_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 dict_t *ctx,
		 struct stat *buf)
{
  if (op_ret >= 0)
    update_stat (buf, this->private);

  STACK_UNWIND (frame,
                op_ret,
                op_errno,
                ctx,
                buf);
  return 0;
}


static int32_t
fixed_id_create (call_frame_t *frame,
		 xlator_t *this,
		 const char *path,
		 int32_t flags,
		 mode_t mode)
{
  STACK_WIND (frame,
              fixed_id_fd_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->create,
              path,
	      flags,
              mode);
  return 0;
}

static int32_t
fixed_id_open (call_frame_t *frame,
	       xlator_t *this,
	       const char *path,
	       int32_t flags,
	       mode_t mode)
{
  STACK_WIND (frame,
              fixed_id_fd_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->open,
              path,
	      flags,
              mode);
  return 0;
}


static int32_t
fixed_id_fgetattr (call_frame_t *frame,
		   xlator_t *this,
		   dict_t *fd)

{
  STACK_WIND (frame,
	      fixed_id_generic_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->fgetattr,
	      fd);

  return 0;
}

static int32_t
fixed_id_readdir_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      dir_entry_t *entries,
		      int32_t count)
{
  if (op_ret >= 0) {
    dir_entry_t *trav = entries->next;

    while (trav) {
      update_stat (&trav->buf, this->private);
      trav = trav->next;
    }
  }

  STACK_UNWIND (frame,
                op_ret,
                op_errno,
                entries,
                count);
  return 0;
}

static int32_t
fixed_id_readdir (call_frame_t *frame,
		  xlator_t *this,
		  const char *path)
{
  STACK_WIND (frame,
              fixed_id_readdir_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->readdir,
              path);
  return 0;
}



int32_t 
init (xlator_t *this)
{
  struct fixed_id *id;
  if (!this->children || this->children->next) {
    gf_log ("fixed-id",
	    GF_LOG_ERROR,
	    "FATAL: xlator (%s) not configured with exactly one child",
	    this->name);
    return -1;
  }

  id = (void *) calloc (sizeof (*id), 1);

  if (dict_get (this->options, "fixed-uid")) {
    id->fixed_uid = data_to_int (dict_get (this->options,
					   "fixed-uid"));
    id->fixed_uid_set = 1;
  }

  if (dict_get (this->options, "fixed-gid")) {
    id->fixed_gid = data_to_int (dict_get (this->options,
					   "fixed-gid"));
    id->fixed_gid_set = 1;
  }

  this->private = id;
  return 0;
}

void
fini (xlator_t *xl)
{

  return;
}


struct xlator_fops fops = {
  .getattr     = fixed_id_getattr,
  .mknod       = fixed_id_mknod,
  .mkdir       = fixed_id_mkdir,
  .symlink     = fixed_id_symlink,
  .link        = fixed_id_link,
  .chmod       = fixed_id_chmod,
  .chown       = fixed_id_chown,
  .truncate    = fixed_id_truncate,
  .utimes      = fixed_id_utimes,
  .open        = fixed_id_open,
  .create      = fixed_id_create,
  .ftruncate   = fixed_id_ftruncate,
  .readdir     = fixed_id_readdir,
  .fgetattr    = fixed_id_fgetattr
};

struct xlator_mops mops = {

};
