/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "quiesce.h"
#include "defaults.h"
#include "call-stub.h"

/* Quiesce Specific Functions */
call_stub_t *
gf_quiesce_dequeue (quiesce_priv_t *priv)
{
        call_stub_t  *stub = NULL;

        if (list_empty (&priv->req))
                return NULL;

        LOCK (&priv->lock);
        {
                stub = list_entry (priv->req.next, call_stub_t, list);
                list_del_init (&stub->list);
                priv->queue_size--;
        }
        UNLOCK (&priv->lock);

        return stub;
}


void
gf_quiesce_enqueue (quiesce_priv_t *priv, call_stub_t *stub)
{
        LOCK (&priv->lock);
        {
                list_add_tail (&stub->list, &priv->req);
                priv->queue_size++;
        }
        UNLOCK (&priv->lock);

        return;
}


void *
gf_quiesce_dequeue_start (void *data)
{
        xlator_t       *this = NULL;
        quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        this = data;
        priv = this->private;
        THIS = this;

        while (!list_empty (&priv->req)) {
                stub = gf_quiesce_dequeue (priv);
                if (stub) {
                        call_resume (stub);
                }
        }

        return 0;
}


void
gf_quiesce_timeout (void *data)
{
        xlator_t       *this = NULL;
        quiesce_priv_t *priv = NULL;
        int             need_dequeue = 0;

        this = data;
        priv = this->private;
        THIS = this;

        LOCK (&priv->lock);
        {
                priv->pass_through = _gf_true;
                need_dequeue = (priv->queue_size)? 1:0;
        }
        UNLOCK (&priv->lock);

        gf_quiesce_dequeue_start (this);

        return;
}

/* FOP */

int32_t
quiesce_fgetxattr (call_frame_t *frame,
                   xlator_t *this,
                   fd_t *fd,
                   const char *name)
{
        quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_fgetxattr_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fgetxattr,
                            fd,
                            name);
                return 0;
        }

        stub = fop_fgetxattr_stub (frame, default_fgetxattr_resume, fd, name);
        if (!stub) {
                STACK_UNWIND_STRICT (fgetxattr, frame, -1, ENOMEM, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_fsetxattr (call_frame_t *frame,
                   xlator_t *this,
                   fd_t *fd,
                   dict_t *dict,
                   int32_t flags)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_fsetxattr_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fsetxattr,
                            fd,
                            dict,
                            flags);
	        return 0;
        }

        stub = fop_fsetxattr_stub (frame, default_fsetxattr_resume,
                                   fd, dict, flags);
        if (!stub) {
                STACK_UNWIND_STRICT (fsetxattr, frame, -1, ENOMEM);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_setxattr (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  dict_t *dict,
		  int32_t flags)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_setxattr_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->setxattr,
                            loc,
                            dict,
                            flags);
	        return 0;
        }

        stub = fop_setxattr_stub (frame, default_setxattr_resume,
                                  loc, dict, flags);
        if (!stub) {
                STACK_UNWIND_STRICT (setxattr, frame, -1, ENOMEM);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_statfs (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_statfs_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->statfs,
                            loc);
	        return 0;
        }

        stub = fop_statfs_stub (frame, default_statfs_resume, loc);
        if (!stub) {
                STACK_UNWIND_STRICT (statfs, frame, -1, ENOMEM, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_fsyncdir (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd,
		  int32_t flags)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_fsyncdir_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fsyncdir,
                            fd,
                            flags);
	        return 0;
        }

        stub = fop_fsyncdir_stub (frame, default_fsyncdir_resume, fd, flags);
        if (!stub) {
                STACK_UNWIND_STRICT (fsyncdir, frame, -1, ENOMEM);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_opendir (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc, fd_t *fd)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_opendir_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->opendir,
                            loc, fd);
	        return 0;
        }

        stub = fop_opendir_stub (frame, default_opendir_resume, loc, fd);
        if (!stub) {
                STACK_UNWIND_STRICT (opendir, frame, -1, ENOMEM, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_fstat (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_fstat_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fstat,
                            fd);
	        return 0;
        }

        stub = fop_fstat_stub (frame, default_fstat_resume, fd);
        if (!stub) {
                STACK_UNWIND_STRICT (fstat, frame, -1, ENOMEM, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_fsync (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       int32_t flags)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_fsync_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fsync,
                            fd,
                            flags);
	        return 0;
        }

        stub = fop_fsync_stub (frame, default_fsync_resume, fd, flags);
        if (!stub) {
                STACK_UNWIND_STRICT (fsync, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_flush (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_flush_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->flush,
                            fd);
	        return 0;
        }

        stub = fop_flush_stub (frame, default_flush_resume, fd);
        if (!stub) {
                STACK_UNWIND_STRICT (flush, frame, -1, ENOMEM);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_writev (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd,
		struct iovec *vector,
		int32_t count,
		off_t off,
                struct iobref *iobref)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_writev_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->writev,
                            fd,
                            vector,
                            count,
                            off,
                            iobref);
	        return 0;
        }

        stub = fop_writev_stub (frame, default_writev_resume,
                                fd, vector, count, off, iobref);
        if (!stub) {
                STACK_UNWIND_STRICT (writev, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_readv (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       size_t size,
	       off_t offset)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_readv_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readv,
                            fd,
                            size,
                            offset);
	        return 0;
        }

        stub = fop_readv_stub (frame, default_readv_resume, fd, size, offset);
        if (!stub) {
                STACK_UNWIND_STRICT (readv, frame, -1, ENOMEM,
                                     NULL, 0, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}


int32_t
quiesce_open (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      int32_t flags, fd_t *fd,
              int32_t wbflags)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_open_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open,
                            loc, flags, fd, wbflags);
	        return 0;
        }

        stub = fop_open_stub (frame, default_open_resume, loc,
                              flags, fd, wbflags);
        if (!stub) {
                STACK_UNWIND_STRICT (open, frame, -1, ENOMEM, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_create (call_frame_t *frame, xlator_t *this,
		loc_t *loc, int32_t flags, mode_t mode,
                fd_t *fd, dict_t *params)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame, default_create_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->create,
                            loc, flags, mode, fd, params);
	        return 0;
        }

        stub = fop_create_stub (frame, default_create_resume,
                                loc, flags, mode, fd, params);
        if (!stub) {
                STACK_UNWIND_STRICT (create, frame, -1, ENOMEM,
                                     NULL, NULL, NULL, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_link (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *oldloc,
	      loc_t *newloc)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_link_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->link,
                            oldloc, newloc);
	        return 0;
        }

        stub = fop_link_stub (frame, default_link_resume, oldloc, newloc);
        if (!stub) {
                STACK_UNWIND_STRICT (link, frame, -1, ENOMEM,
                                     NULL, NULL, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_rename (call_frame_t *frame,
		xlator_t *this,
		loc_t *oldloc,
		loc_t *newloc)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_rename_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->rename,
                            oldloc, newloc);
	        return 0;
        }

        stub = fop_rename_stub (frame, default_rename_resume, oldloc, newloc);
        if (!stub) {
                STACK_UNWIND_STRICT (rename, frame, -1, ENOMEM,
                                     NULL, NULL, NULL, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}


int
quiesce_symlink (call_frame_t *frame, xlator_t *this,
		 const char *linkpath, loc_t *loc, dict_t *params)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame, default_symlink_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->symlink,
                            linkpath, loc, params);
	        return 0;
        }

        stub = fop_symlink_stub (frame, default_symlink_resume,
                                 linkpath, loc, params);
        if (!stub) {
                STACK_UNWIND_STRICT (symlink, frame, -1, ENOMEM,
                                     NULL, NULL, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}


int
quiesce_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame, default_rmdir_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->rmdir,
                            loc, flags);
	        return 0;
        }

        stub = fop_rmdir_stub (frame, default_rmdir_resume, loc, flags);
        if (!stub) {
                STACK_UNWIND_STRICT (rmdir, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_unlink (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_unlink_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->unlink,
                            loc);
	        return 0;
        }

        stub = fop_unlink_stub (frame, default_unlink_resume, loc);
        if (!stub) {
                STACK_UNWIND_STRICT (unlink, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int
quiesce_mkdir (call_frame_t *frame, xlator_t *this,
	       loc_t *loc, mode_t mode, dict_t *params)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame, default_mkdir_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->mkdir,
                            loc, mode, params);
	        return 0;
        }

        stub = fop_mkdir_stub (frame, default_mkdir_resume,
                               loc, mode, params);
        if (!stub) {
                STACK_UNWIND_STRICT (mkdir, frame, -1, ENOMEM,
                                     NULL, NULL, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}


int
quiesce_mknod (call_frame_t *frame, xlator_t *this,
	       loc_t *loc, mode_t mode, dev_t rdev, dict_t *parms)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame, default_mknod_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->mknod,
                            loc, mode, rdev, parms);
	        return 0;
        }

        stub = fop_mknod_stub (frame, default_mknod_resume,
                               loc, mode, rdev, parms);
        if (!stub) {
                STACK_UNWIND_STRICT (mknod, frame, -1, ENOMEM,
                                     NULL, NULL, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_readlink (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  size_t size)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_readlink_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readlink,
                            loc,
                            size);
	        return 0;
        }

        stub = fop_readlink_stub (frame, default_readlink_resume, loc, size);
        if (!stub) {
                STACK_UNWIND_STRICT (readlink, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}


int32_t
quiesce_access (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		int32_t mask)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_access_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->access,
                            loc,
                            mask);
	        return 0;
        }

        stub = fop_access_stub (frame, default_access_resume, loc, mask);
        if (!stub) {
                STACK_UNWIND_STRICT (access, frame, -1, ENOMEM);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_ftruncate (call_frame_t *frame,
		   xlator_t *this,
		   fd_t *fd,
		   off_t offset)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_ftruncate_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->ftruncate,
                            fd,
                            offset);
	        return 0;
        }

        stub = fop_ftruncate_stub (frame, default_ftruncate_resume, fd, offset);
        if (!stub) {
                STACK_UNWIND_STRICT (ftruncate, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_getxattr (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  const char *name)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_getxattr_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->getxattr,
                            loc,
                            name);
	        return 0;
        }

        stub = fop_getxattr_stub (frame, default_getxattr_resume, loc, name);
        if (!stub) {
                STACK_UNWIND_STRICT (getxattr, frame, -1, ENOMEM, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}


int32_t
quiesce_xattrop (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 gf_xattrop_flags_t flags,
		 dict_t *dict)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_xattrop_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->xattrop,
                            loc,
                            flags,
                            dict);
	        return 0;
        }

        stub = fop_xattrop_stub (frame, default_xattrop_resume,
                                 loc, flags, dict);
        if (!stub) {
                STACK_UNWIND_STRICT (xattrop, frame, -1, ENOMEM, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_fxattrop (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd,
		  gf_xattrop_flags_t flags,
		  dict_t *dict)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_fxattrop_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fxattrop,
                            fd,
                            flags,
                            dict);
	        return 0;
        }

        stub = fop_fxattrop_stub (frame, default_fxattrop_resume,
                                  fd, flags, dict);
        if (!stub) {
                STACK_UNWIND_STRICT (fxattrop, frame, -1, ENOMEM, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_removexattr (call_frame_t *frame,
		     xlator_t *this,
		     loc_t *loc,
		     const char *name)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_removexattr_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->removexattr,
                            loc,
                            name);
	        return 0;
        }

        stub = fop_removexattr_stub (frame, default_removexattr_resume,
                                     loc, name);
        if (!stub) {
                STACK_UNWIND_STRICT (removexattr, frame, -1, ENOMEM);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_lk (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    int32_t cmd,
	    struct gf_flock *lock)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_lk_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lk,
                            fd,
                            cmd,
                            lock);
	        return 0;
        }

        stub = fop_lk_stub (frame, default_lk_resume, fd, cmd, lock);
        if (!stub) {
                STACK_UNWIND_STRICT (lk, frame, -1, ENOMEM, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}


int32_t
quiesce_inodelk (call_frame_t *frame, xlator_t *this,
		 const char *volume, loc_t *loc, int32_t cmd,
                 struct gf_flock *lock)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_inodelk_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->inodelk,
                            volume, loc, cmd, lock);
	        return 0;
        }

        stub = fop_inodelk_stub (frame, default_inodelk_resume,
                                 volume, loc, cmd, lock);
        if (!stub) {
                STACK_UNWIND_STRICT (inodelk, frame, -1, ENOMEM);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_finodelk (call_frame_t *frame, xlator_t *this,
		  const char *volume, fd_t *fd, int32_t cmd, struct gf_flock *lock)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_finodelk_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->finodelk,
                            volume, fd, cmd, lock);
	        return 0;
        }

        stub = fop_finodelk_stub (frame, default_finodelk_resume,
                                  volume, fd, cmd, lock);
        if (!stub) {
                STACK_UNWIND_STRICT (finodelk, frame, -1, ENOMEM);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_entrylk (call_frame_t *frame, xlator_t *this,
		 const char *volume, loc_t *loc, const char *basename,
		 entrylk_cmd cmd, entrylk_type type)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame, default_entrylk_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->entrylk,
                            volume, loc, basename, cmd, type);
	        return 0;
        }

        stub = fop_entrylk_stub (frame, default_entrylk_resume,
                                 volume, loc, basename, cmd, type);
        if (!stub) {
                STACK_UNWIND_STRICT (entrylk, frame, -1, ENOMEM);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_fentrylk (call_frame_t *frame, xlator_t *this,
		  const char *volume, fd_t *fd, const char *basename,
		  entrylk_cmd cmd, entrylk_type type)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame, default_fentrylk_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fentrylk,
                            volume, fd, basename, cmd, type);
	        return 0;
        }

        stub = fop_fentrylk_stub (frame, default_fentrylk_resume,
                                  volume, fd, basename, cmd, type);
        if (!stub) {
                STACK_UNWIND_STRICT (fentrylk, frame, -1, ENOMEM);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_rchecksum (call_frame_t *frame,
                   xlator_t *this,
                   fd_t *fd, off_t offset,
                   int32_t len)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_rchecksum_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->rchecksum,
                            fd, offset, len);
	        return 0;
        }

        stub = fop_rchecksum_stub (frame, default_rchecksum_resume,
                                   fd, offset, len);
        if (!stub) {
                STACK_UNWIND_STRICT (rchecksum, frame, -1, ENOMEM, 0, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}


int32_t
quiesce_readdir (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 size_t size,
		 off_t off)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_readdir_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readdir,
                            fd, size, off);
	        return 0;
        }

        stub = fop_readdir_stub (frame, default_readdir_resume, fd, size, off);
        if (!stub) {
                STACK_UNWIND_STRICT (readdir, frame, -1, ENOMEM, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}


int32_t
quiesce_readdirp (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd,
		  size_t size,
		  off_t off)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_readdirp_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readdirp,
                            fd, size, off);
	        return 0;
        }

        stub = fop_readdirp_stub (frame, default_readdirp_resume, fd, size, off);
        if (!stub) {
                STACK_UNWIND_STRICT (readdirp, frame, -1, ENOMEM, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_setattr (call_frame_t *frame,
                 xlator_t *this,
                 loc_t *loc,
                 struct iatt *stbuf,
                 int32_t valid)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_setattr_cbk,
                            FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->setattr,
                            loc, stbuf, valid);
	        return 0;
        }

        stub = fop_setattr_stub (frame, default_setattr_resume,
                                   loc, stbuf, valid);
        if (!stub) {
                STACK_UNWIND_STRICT (setattr, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_truncate (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  off_t offset)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_truncate_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->truncate,
                            loc,
                            offset);
	        return 0;
        }

        stub = fop_truncate_stub (frame, default_truncate_resume, loc, offset);
        if (!stub) {
                STACK_UNWIND_STRICT (truncate, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_stat (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_stat_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->stat,
                            loc);
	        return 0;
        }

        stub = fop_stat_stub (frame, default_stat_resume, loc);
        if (!stub) {
                STACK_UNWIND_STRICT (stat, frame, -1, ENOMEM, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_lookup (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		dict_t *xattr_req)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_lookup_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup,
                            loc,
                            xattr_req);
	        return 0;
        }

        stub = fop_lookup_stub (frame, default_lookup_resume, loc, xattr_req);
        if (!stub) {
                STACK_UNWIND_STRICT (lookup, frame, -1, ENOMEM,
                                     NULL, NULL, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
quiesce_fsetattr (call_frame_t *frame,
                  xlator_t *this,
                  fd_t *fd,
                  struct iatt *stbuf,
                  int32_t valid)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_fsetattr_cbk,
                            FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fsetattr,
                            fd, stbuf, valid);
	        return 0;
        }

        stub = fop_fsetattr_stub (frame, default_fsetattr_resume,
                                  fd, stbuf, valid);
        if (!stub) {
                STACK_UNWIND_STRICT (fsetattr, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (priv, stub);

        return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        ret = xlator_mem_acct_init (this, gf_quiesce_mt_end + 1);

        return ret;
}

int
init (xlator_t *this)
{
        int ret = -1;
        quiesce_priv_t *priv = NULL;

	if (!this->children || this->children->next) {
		gf_log (this->name, GF_LOG_ERROR,
			"'quiesce' not configured with exactly one child");
                goto out;
	}

	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"dangling volume. check volfile ");
	}

        priv = GF_CALLOC (1, sizeof (*priv), gf_quiesce_mt_priv_t);
        if (!priv)
                goto out;

        LOCK_INIT (&priv->lock);
        priv->pass_through = _gf_false;

        INIT_LIST_HEAD (&priv->req);

        this->private = priv;
        ret = 0;
out:
        return ret;
}

void
fini (xlator_t *this)
{
        quiesce_priv_t *priv = NULL;

        priv = this->private;
        if (!priv)
                goto out;
        this->private = NULL;

        LOCK_DESTROY (&priv->lock);
        GF_FREE (priv);
out:
        return;
}

int
notify (xlator_t *this, int event, void *data, ...)
{
        int             ret     = 0;
        quiesce_priv_t *priv    = NULL;
        struct timeval  timeout = {0,};

        priv = this->private;
        if (!priv)
                goto out;

        switch (event) {
        case GF_EVENT_CHILD_UP:
        {
                ret = pthread_create (&priv->thr, NULL, gf_quiesce_dequeue_start,
                                      this);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to create the quiesce-dequeue thread");
                }

                LOCK (&priv->lock);
                {
                        priv->pass_through = _gf_true;
                }
                UNLOCK (&priv->lock);
                break;
        }
        case GF_EVENT_CHILD_DOWN:
                LOCK (&priv->lock);
                {
                        priv->pass_through = _gf_false;
                }
                UNLOCK (&priv->lock);

                if (!priv->timer) {
                        timeout.tv_sec = 20;
                        timeout.tv_usec = 0;

                        gf_timer_call_cancel (this->ctx, priv->timer);
                        priv->timer = gf_timer_call_after (this->ctx,
                                                           timeout,
                                                           gf_quiesce_timeout,
                                                           (void *) this);

                        if (priv->timer == NULL) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Cannot create timer");
                        }
                }
                break;
        default:
                break;
        }

        ret = default_notify (this, event, data);
out:
        return ret;
}


struct xlator_fops fops = {
	.lookup      = quiesce_lookup,
	.mknod       = quiesce_mknod,
	.create      = quiesce_create,
	.stat        = quiesce_stat,
	.fstat       = quiesce_fstat,
	.truncate    = quiesce_truncate,
	.ftruncate   = quiesce_ftruncate,
	.access      = quiesce_access,
	.readlink    = quiesce_readlink,
	.setxattr    = quiesce_setxattr,
	.getxattr    = quiesce_getxattr,
	.removexattr = quiesce_removexattr,
	.open        = quiesce_open,
	.readv       = quiesce_readv,
	.writev      = quiesce_writev,
	.flush       = quiesce_flush,
	.fsync       = quiesce_fsync,
	.statfs      = quiesce_statfs,
	.lk          = quiesce_lk,
	.opendir     = quiesce_opendir,
	.readdir     = quiesce_readdir,
	.readdirp    = quiesce_readdirp,
	.fsyncdir    = quiesce_fsyncdir,
	.symlink     = quiesce_symlink,
	.unlink      = quiesce_unlink,
	.link        = quiesce_link,
	.mkdir       = quiesce_mkdir,
	.rmdir       = quiesce_rmdir,
	.rename      = quiesce_rename,
	.inodelk     = quiesce_inodelk,
	.finodelk    = quiesce_finodelk,
	.entrylk     = quiesce_entrylk,
	.fentrylk    = quiesce_fentrylk,
	.xattrop     = quiesce_xattrop,
	.fxattrop    = quiesce_fxattrop,
        .setattr     = quiesce_setattr,
        .fsetattr    = quiesce_fsetattr,
};

struct xlator_dumpops dumpops = {
};


struct xlator_cbks cbks = {
};


struct volume_options options[] = {
	{ .key  = {NULL} },
};
