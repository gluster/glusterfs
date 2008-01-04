/*
   Copyright (c) 2007 Z RESEARCH, Inc. <http://www.zresearch.com>
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
fixed_id_lookup_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     inode_t *inode,
		     struct stat *buf,
		     dict_t *dict)
{
  if (op_ret >= 0)
    update_stat (buf, this->private);

  STACK_UNWIND (frame, op_ret, op_errno, inode, buf, dict);
  return 0;
}

static int32_t
fixed_id_symlink_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      inode_t *inode,
		      struct stat *buf)
{
  if (op_ret >= 0)
    update_stat (buf, this->private);

  STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
  return 0;
}


#if 0
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
#endif

static int32_t
fixed_id_lookup (call_frame_t *frame,
                 xlator_t *this,
                 loc_t *loc,
		 int32_t need_xattr)
{
  STACK_WIND (frame,
              fixed_id_lookup_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->lookup,
              loc,
	      need_xattr);
  return 0;
}

static int32_t
fixed_id_chmod (call_frame_t *frame,
                xlator_t *this,
                loc_t *loc,
                mode_t mode)
{
  STACK_WIND (frame,
              fixed_id_generic_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->chmod,
              loc,
              mode);

  return 0;
}

static int32_t
fixed_id_chown (call_frame_t *frame,
                xlator_t *this,
                loc_t *loc,
                uid_t uid,
                gid_t gid)
{
  STACK_WIND (frame,
              fixed_id_generic_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->chown,
              loc,
              uid,
              gid);

  return 0;
}

static int32_t
fixed_id_truncate (call_frame_t *frame,
                   xlator_t *this,
                   loc_t *loc,
                   off_t offset)

{
  STACK_WIND (frame,
              fixed_id_generic_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->truncate,
              loc,
              offset);

  return 0;
}

static int32_t
fixed_id_ftruncate (call_frame_t *frame,
                    xlator_t *this,
                    fd_t *fd,
                    off_t offset)

{
  STACK_WIND (frame,
              fixed_id_generic_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->ftruncate,
              fd,
              offset);
  return 0;
}

static int32_t
fixed_id_utimens (call_frame_t *frame,
                  xlator_t *this,
                  loc_t *loc,
                  struct timespec *buf)

{
  STACK_WIND (frame,
              fixed_id_generic_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->utimens,
              loc,
              buf);

  return 0;
}

static int32_t 
fixed_id_generic_inode_cbk (call_frame_t *frame,
                            void *cookie,
                            xlator_t *this,
                            int32_t op_ret,
                            int32_t op_errno,
                            inode_t *inode,
                            struct stat *buf)
{
  if (op_ret >= 0)
    update_stat (buf, this->private);

  STACK_UNWIND (frame,
                op_ret,
                op_errno,
                inode, 
                buf);
  return 0;
}

static int32_t
fixed_id_mknod (call_frame_t *frame,
                xlator_t *this,
		loc_t *loc,
                mode_t mode,
                dev_t dev)
{
  STACK_WIND (frame,
              fixed_id_generic_inode_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->mknod,
	      loc,
              mode,
              dev);

  return 0;
}

static int32_t
fixed_id_mkdir (call_frame_t *frame,
                xlator_t *this,
		loc_t *loc,
                mode_t mode)

{
  STACK_WIND (frame,
              fixed_id_generic_inode_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->mkdir,
	      loc,
              mode);

  return 0;
}

static int32_t
fixed_id_symlink (call_frame_t *frame,
                  xlator_t *this,
                  const char *oldpath,
		  loc_t *loc)

{
  STACK_WIND (frame,
              fixed_id_symlink_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->symlink,
              oldpath,
	      loc);

  return 0;
}

static int32_t
fixed_id_link (call_frame_t *frame,
               xlator_t *this,
               loc_t *oldloc,
               const char *newpath)

{
  STACK_WIND (frame,
              fixed_id_generic_inode_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->link,
              oldloc,
              newpath);

  return 0;
}

static int32_t
fixed_id_fd_cbk (call_frame_t *frame,
                 void *cookie,
                 xlator_t *this,
                 int32_t op_ret,
                 int32_t op_errno,
                 fd_t *fd)
{
  STACK_UNWIND (frame,
                op_ret,
                op_errno,
                fd);
  return 0;
}


static int32_t
fixed_id_create_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     fd_t *fd,
		     inode_t *inode,
		     struct stat *stbuf)
{
  if (op_ret >= 0)
    update_stat (stbuf, this->private);

  STACK_UNWIND (frame,
                op_ret,
                op_errno,
                fd, inode, stbuf);
  return 0;
}


static int32_t
fixed_id_create (call_frame_t *frame,
                 xlator_t *this,
		 loc_t *loc,
                 int32_t flags,
                 mode_t mode,
		 fd_t *fd)
{
  STACK_WIND (frame,
              fixed_id_create_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->create,
	      loc,
              flags,
              mode,
	      fd);
  return 0;
}

static int32_t
fixed_id_open (call_frame_t *frame,
               xlator_t *this,
               loc_t *loc,
               int32_t flags,
	       fd_t *fd)
{
  STACK_WIND (frame,
              fixed_id_fd_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->open,
              loc,
              flags,
	      fd);
  return 0;
}

#if 0
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
#endif

static int32_t
fixed_id_getdents_cbk (call_frame_t *frame,
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

/* FIXME implement readdir? (krishna renamed readdir->getdents) */

static int32_t
fixed_id_getdents (call_frame_t *frame,
		   xlator_t *this,
		   fd_t *fd,
		   size_t size,
		   off_t offset,
		   int32_t flag)
{
  STACK_WIND (frame,
              fixed_id_getdents_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->getdents,
	      fd,
              size,
              offset,
              flag);
  return 0;
}

static int32_t
fixed_id_stat (call_frame_t *frame,
               xlator_t *this,
               loc_t *loc)
{
  STACK_WIND (frame,
              fixed_id_generic_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->stat,
              loc);
  return 0;
}

static int32_t
fixed_id_fstat (call_frame_t *frame,
                xlator_t *this,
                fd_t *fd)
{
  STACK_WIND (frame,
              fixed_id_generic_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->fstat,
              fd);
  return 0;
}

static int32_t
fixed_id_rename (call_frame_t *frame,
                 xlator_t *this,
                 loc_t *oldloc,
                 loc_t *newloc)
{
  STACK_WIND (frame,
              fixed_id_generic_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->rename,
              oldloc,
              newloc);
  return 0;
}

static int32_t 
fixed_id_fchmod (call_frame_t *frame,
                 xlator_t *this,
                 fd_t *fd,
                 mode_t mode)
{
  STACK_WIND (frame,
              fixed_id_generic_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->fchmod,
              fd,
              mode);
  return 0;
}

static int32_t 
fixed_id_fchown (call_frame_t *frame,
                 xlator_t *this,
                 fd_t *fd,
                 uid_t uid,
                 gid_t gid)
{
  STACK_WIND (frame,        
              fixed_id_generic_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->fchown,
              fd,
              uid,
              gid);
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
    id->fixed_uid = data_to_uint64 (dict_get (this->options,
             "fixed-uid"));
    id->fixed_uid_set = 1;
  }

  if (dict_get (this->options, "fixed-gid")) {
    id->fixed_gid = data_to_uint64 (dict_get (this->options,
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
  .stat        = fixed_id_stat,
  .fstat       = fixed_id_fstat,
  .rename      = fixed_id_rename,
  .lookup      = fixed_id_lookup,
  .mknod       = fixed_id_mknod,
  .mkdir       = fixed_id_mkdir,
  .symlink     = fixed_id_symlink,
  .link        = fixed_id_link,
  .chmod       = fixed_id_chmod,
  .fchmod      = fixed_id_fchmod,
  .chown       = fixed_id_chown,
  .fchown      = fixed_id_fchown,
  .truncate    = fixed_id_truncate,
  .ftruncate   = fixed_id_ftruncate,
  .utimens     = fixed_id_utimens,
  .open        = fixed_id_open,
  .create      = fixed_id_create,
  .getdents    = fixed_id_getdents,
};

struct xlator_mops mops = {

};
