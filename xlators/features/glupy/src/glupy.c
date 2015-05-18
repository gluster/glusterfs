/*
   Copyright (c) 2006-2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <ctype.h>
#include <sys/uio.h>
#include <Python.h>

#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "defaults.h"

#include "glupy.h"

/* UTILITY FUNCTIONS FOR FOP-SPECIFIC CODE */

pthread_key_t gil_init_key;

PyGILState_STATE
glupy_enter (void)
{
        if (!pthread_getspecific(gil_init_key)) {
                PyEval_ReleaseLock();
                (void)pthread_setspecific(gil_init_key,(void *)1);
        }

        return PyGILState_Ensure();
}

void
glupy_leave (PyGILState_STATE gstate)
{
        PyGILState_Release(gstate);
}

/* FOP: LOOKUP */

int32_t
glupy_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *buf, dict_t *xdata, struct iatt *postparent)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;

        if (!priv->cbks[GLUPY_LOOKUP]) {
                goto unwind;
        }

        gstate = glupy_enter();
        ret = ((fop_lookup_cbk_t)(priv->cbks[GLUPY_LOOKUP]))(
                frame, cookie, this, op_ret, op_errno,
                inode, buf, xdata, postparent);
        glupy_leave(gstate);

        return ret;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, inode, buf,
                             xdata, postparent);
        return 0;
}

int32_t
glupy_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc,
              dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;
        static long next_id = 0;

        if (!priv->fops[GLUPY_LOOKUP]) {
                goto wind;
        }

        gstate = glupy_enter();
        frame->local = (void *)++next_id;
        ret = ((fop_lookup_t)(priv->fops[GLUPY_LOOKUP]))(
                frame, this, loc, xdata);
        glupy_leave(gstate);

        return ret;

wind:
        STACK_WIND (frame, glupy_lookup_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup, loc, xdata);
        return 0;
}

void
wind_lookup (call_frame_t *frame, xlator_t *xl, loc_t *loc, dict_t *xdata)
{
        xlator_t        *this = THIS;

        if (!xl || (xl == this)) {
                xl = FIRST_CHILD(this);
        }

        STACK_WIND(frame,glupy_lookup_cbk,xl,xl->fops->lookup,loc,xdata);
}

void
unwind_lookup (call_frame_t *frame, long cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode,
               struct iatt *buf, dict_t *xdata, struct iatt *postparent)
{
        frame->local = NULL;
        STACK_UNWIND_STRICT(lookup,frame,op_ret,op_errno,
                            inode,buf,xdata,postparent);
}

void
set_lookup_fop (long py_this, fop_lookup_t fop)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->fops[GLUPY_LOOKUP] = (long)fop;
}

void
set_lookup_cbk (long py_this, fop_lookup_cbk_t cbk)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->cbks[GLUPY_LOOKUP] = (long)cbk;
}

/* FOP: CREATE */

int32_t
glupy_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
                  struct iatt *buf, struct iatt *preparent,
                  struct iatt *postparent, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;

        if (!priv->cbks[GLUPY_CREATE]) {
                goto unwind;
        }

        gstate = glupy_enter();
        ret = ((fop_create_cbk_t)(priv->cbks[GLUPY_CREATE]))(
                frame, cookie, this, op_ret, op_errno,
                fd, inode, buf, preparent, postparent, xdata);
        glupy_leave(gstate);

        return ret;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (create, frame, op_ret, op_errno, fd, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}

int32_t
glupy_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
              mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;
        static long next_id = 0;

        if (!priv->fops[GLUPY_CREATE]) {
                goto wind;
        }

        gstate = glupy_enter();
        frame->local = (void *)++next_id;
        ret = ((fop_create_t)(priv->fops[GLUPY_CREATE]))(
                frame, this, loc, flags, mode, umask, fd, xdata);
        glupy_leave(gstate);

        return ret;

wind:
        STACK_WIND (frame, glupy_create_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->create, loc, flags, mode, umask,
                    fd, xdata);
        return 0;
}

void
wind_create (call_frame_t *frame, xlator_t *xl, loc_t *loc, int32_t flags,
             mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        xlator_t        *this = THIS;

        if (!xl || (xl == this)) {
                xl = FIRST_CHILD(this);
        }

        STACK_WIND (frame, glupy_create_cbk,xl, xl->fops->create,
                    loc, flags, mode, umask, fd, xdata);
}

void
unwind_create (call_frame_t *frame, long cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
               struct iatt *buf, struct iatt *preparent,
               struct iatt *postparent, dict_t *xdata)
{
        frame->local = NULL;
        STACK_UNWIND_STRICT (create, frame, op_ret, op_errno, fd, inode, buf,
                             preparent, postparent, xdata);
}

void
set_create_fop (long py_this, fop_create_t fop)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->fops[GLUPY_CREATE] = (long)fop;
}

void
set_create_cbk (long py_this, fop_create_cbk_t cbk)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->cbks[GLUPY_CREATE] = (long)cbk;
}

/* FOP: OPEN */

int32_t
glupy_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;

        if (!priv->cbks[GLUPY_OPEN]) {
                goto unwind;
        }

        gstate = glupy_enter();
        ret = ((fop_open_cbk_t)(priv->cbks[GLUPY_OPEN]))(
                frame, cookie, this, op_ret, op_errno,
                fd, xdata);
        glupy_leave(gstate);

        return ret;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd, xdata);
        return 0;
}

int32_t
glupy_open (call_frame_t *frame, xlator_t *this, loc_t *loc,
            int32_t flags, fd_t *fd, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;
        static long next_id = 0;

        if (!priv->fops[GLUPY_OPEN]) {
                goto wind;
        }

        gstate = glupy_enter();
        frame->local = (void *)++next_id;
        ret = ((fop_open_t)(priv->fops[GLUPY_OPEN]))(
                frame, this, loc, flags, fd, xdata);
        glupy_leave(gstate);

        return ret;

wind:
        STACK_WIND (frame, glupy_open_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open, loc, flags, fd, xdata);
        return 0;
}

void
wind_open (call_frame_t *frame, xlator_t *xl, loc_t *loc, int32_t flags,
           fd_t *fd, dict_t *xdata)
{
        xlator_t        *this = THIS;

        if (!xl || (xl == this)) {
                xl = FIRST_CHILD(this);
        }

        STACK_WIND (frame, glupy_open_cbk, xl, xl->fops->open, loc, flags,
                    fd, xdata);
}

void
unwind_open (call_frame_t *frame, long cookie, xlator_t *this,
             int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        frame->local = NULL;
        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd, xdata);
}

void
set_open_fop (long py_this, fop_open_t fop)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;
        priv->fops[GLUPY_OPEN] = (long)fop;
}

void
set_open_cbk (long py_this, fop_open_cbk_t cbk)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;
        priv->cbks[GLUPY_OPEN] = (long)cbk;
}

/* FOP: READV */

int32_t
glupy_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iovec *vector,
                 int32_t count, struct iatt *stbuf, struct iobref *iobref,
                 dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;

        if (!priv->cbks[GLUPY_READV]) {
                goto unwind;
        }

        gstate = glupy_enter();
        ret = ((fop_readv_cbk_t)(priv->cbks[GLUPY_READV]))(
                frame, cookie, this, op_ret, op_errno,
                vector, count, stbuf, iobref, xdata);
        glupy_leave(gstate);

        return ret;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno, vector,
                             count, stbuf, iobref, xdata);
        return 0;
}

int32_t
glupy_readv (call_frame_t *frame, xlator_t *this, fd_t *fd,
             size_t size, off_t offset, uint32_t flags, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;
        static long next_id = 0;

        if (!priv->fops[GLUPY_READV]) {
                goto wind;
        }

        gstate = glupy_enter();
        frame->local = (void *)++next_id;
        ret = ((fop_readv_t)(priv->fops[GLUPY_READV]))(
                frame, this, fd, size, offset, flags, xdata);
        glupy_leave(gstate);

        return ret;

wind:
        STACK_WIND (frame, glupy_readv_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readv, fd, size, offset,
                    flags, xdata);
        return 0;
}

void
wind_readv (call_frame_t *frame, xlator_t *xl, fd_t *fd, size_t size,
            off_t offset, uint32_t flags, dict_t *xdata)
{
        xlator_t        *this = THIS;

        if (!xl || (xl == this)) {
                xl = FIRST_CHILD(this);
        }

        STACK_WIND (frame, glupy_readv_cbk, xl, xl->fops->readv, fd, size,
                    offset, flags, xdata);
}

void
unwind_readv (call_frame_t *frame, long cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, struct iovec *vector,
              int32_t count, struct iatt *stbuf, struct iobref *iobref,
              dict_t *xdata)
{
        frame->local = NULL;
        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno, vector,
                             count, stbuf, iobref, xdata);
}

void
set_readv_fop (long py_this, fop_readv_t fop)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;
        priv->fops[GLUPY_READV] = (long)fop;
}

void
set_readv_cbk (long py_this, fop_readv_cbk_t cbk)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;
        priv->cbks[GLUPY_READV] = (long)cbk;
}

/* FOP: WRITEV */

int32_t
glupy_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                  struct iatt *postbuf, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;

        if (!priv->cbks[GLUPY_WRITEV]) {
                goto unwind;
        }

        gstate = glupy_enter();
        ret = ((fop_writev_cbk_t)(priv->cbks[GLUPY_WRITEV]))(
                frame, cookie, this, op_ret, op_errno,
                prebuf, postbuf, xdata);
        glupy_leave(gstate);

        return ret;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf,
                             postbuf, xdata);
        return 0;
}

int32_t
glupy_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
              struct iovec *vector, int32_t count, off_t offset,
              uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;
        static long next_id = 0;

        if (!priv->fops[GLUPY_WRITEV]) {
                goto wind;
        }

        gstate = glupy_enter();
        frame->local = (void *)++next_id;
        ret = ((fop_writev_t)(priv->fops[GLUPY_WRITEV]))(
                frame, this, fd, vector, count, offset, flags,
                iobref, xdata);
        glupy_leave(gstate);

        return ret;

wind:
        STACK_WIND (frame, glupy_writev_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev, fd, vector, count,
                    offset, flags, iobref, xdata);
        return 0;
}

void
wind_writev (call_frame_t *frame, xlator_t *xl, fd_t *fd, struct iovec *vector,
             int32_t count, off_t offset, uint32_t flags, struct iobref *iobref,
             dict_t *xdata)
{
        xlator_t        *this = THIS;

        if (!xl || (xl == this)) {
                xl = FIRST_CHILD(this);
        }

        STACK_WIND (frame, glupy_writev_cbk, xl, xl->fops->writev, fd, vector,
                    count, offset, flags, iobref, xdata);
}

void
unwind_writev (call_frame_t *frame, long cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
               struct iatt *postbuf, dict_t *xdata)
{
        frame->local = NULL;
        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf,
                             postbuf, xdata);
}

void
set_writev_fop (long py_this, fop_writev_t fop)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;
        priv->fops[GLUPY_WRITEV] = (long)fop;
}

void
set_writev_cbk (long py_this, fop_writev_cbk_t cbk)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;
        priv->cbks[GLUPY_WRITEV] = (long)cbk;
}


/* FOP: OPENDIR */

int32_t
glupy_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, fd_t *fd,
                   dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;

        if (!priv->cbks[GLUPY_OPENDIR]) {
                goto unwind;
        }

        gstate = glupy_enter();
        ret = ((fop_opendir_cbk_t)(priv->cbks[GLUPY_OPENDIR]))(
                frame, cookie, this, op_ret, op_errno,
                fd, xdata);
        glupy_leave(gstate);

        return ret;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (opendir, frame, op_ret, op_errno, fd, xdata);
        return 0;
}

int32_t
glupy_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc,
               fd_t *fd, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;
        static long next_id = 0;

        if (!priv->fops[GLUPY_OPENDIR]) {
                goto wind;
        }

        gstate = glupy_enter();
        frame->local = (void *)++next_id;
        ret = ((fop_opendir_t)(priv->fops[GLUPY_OPENDIR]))(
                frame, this, loc, fd, xdata);
        glupy_leave(gstate);

        return ret;

wind:
        STACK_WIND (frame, glupy_opendir_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->opendir, loc, fd, xdata);
        return 0;
}

void
wind_opendir (call_frame_t *frame, xlator_t *xl, loc_t *loc, fd_t *fd, dict_t *xdata)
{
        xlator_t        *this = THIS;

        if (!xl || (xl == this)) {
                xl = FIRST_CHILD(this);
        }

        STACK_WIND(frame,glupy_opendir_cbk,xl,xl->fops->opendir,loc,fd,xdata);
}

void
unwind_opendir (call_frame_t *frame, long cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        frame->local = NULL;
        STACK_UNWIND_STRICT(opendir,frame,op_ret,op_errno,
                            fd,xdata);
}

void
set_opendir_fop (long py_this, fop_opendir_t fop)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->fops[GLUPY_OPENDIR] = (long)fop;
}

void
set_opendir_cbk (long py_this, fop_opendir_cbk_t cbk)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->cbks[GLUPY_OPENDIR] = (long)cbk;
}

/* FOP: READDIR */

int32_t
glupy_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                   dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;

        if (!priv->cbks[GLUPY_READDIR]) {
                goto unwind;
        }

        gstate = glupy_enter();
        ret = ((fop_readdir_cbk_t)(priv->cbks[GLUPY_READDIR]))(
                frame, cookie, this, op_ret, op_errno,
                entries,  xdata);
        glupy_leave(gstate);

        return ret;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (readdir, frame, op_ret, op_errno, entries,
                             xdata);
        return 0;
}

int32_t
glupy_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd,
               size_t size, off_t offset, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;
        static long next_id = 0;

        if (!priv->fops[GLUPY_READDIR]) {
                goto wind;
        }

        gstate = glupy_enter();
        frame->local = (void *)++next_id;
        ret = ((fop_readdir_t)(priv->fops[GLUPY_READDIR]))(
                frame, this, fd, size, offset, xdata);
        glupy_leave(gstate);

        return ret;

wind:
        STACK_WIND (frame, glupy_readdir_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdir,fd, size, offset, xdata);
        return 0;
}

void
wind_readdir(call_frame_t *frame, xlator_t *xl, fd_t *fd, size_t size,
             off_t offset, dict_t *xdata)
{
        xlator_t        *this = THIS;

        if (!xl || (xl == this)) {
                xl = FIRST_CHILD(this);
        }

        STACK_WIND(frame,glupy_readdir_cbk,xl,xl->fops->readdir,fd,size,offset,xdata);
}

void
unwind_readdir (call_frame_t *frame, long cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                dict_t *xdata)
{
        frame->local = NULL;
        STACK_UNWIND_STRICT(readdir,frame,op_ret,op_errno,
                            entries, xdata);
}

void
set_readdir_fop (long py_this, fop_readdir_t fop)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->fops[GLUPY_READDIR] = (long)fop;
}

void
set_readdir_cbk (long py_this, fop_readdir_cbk_t cbk)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->cbks[GLUPY_READDIR] = (long)cbk;
}


/* FOP: READDIRP */

int32_t
glupy_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                    dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;

        if (!priv->cbks[GLUPY_READDIRP]) {
                goto unwind;
        }

        gstate = glupy_enter();
        ret = ((fop_readdirp_cbk_t)(priv->cbks[GLUPY_READDIRP]))(
                frame, cookie, this, op_ret, op_errno,
                entries,  xdata);
        glupy_leave(gstate);

        return ret;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (readdirp, frame, op_ret, op_errno, entries,
                             xdata);
        return 0;
}

int32_t
glupy_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd,
                size_t size, off_t offset, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;
        static long next_id = 0;

        if (!priv->fops[GLUPY_READDIRP]) {
                goto wind;
        }

        gstate = glupy_enter();
        frame->local = (void *)++next_id;
        ret = ((fop_readdirp_t)(priv->fops[GLUPY_READDIRP]))(
                frame, this, fd, size, offset, xdata);
        glupy_leave(gstate);

        return ret;

wind:
        STACK_WIND (frame, glupy_readdirp_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdirp,fd, size, offset, xdata);
        return 0;
}

void
wind_readdirp (call_frame_t *frame, xlator_t *xl, fd_t *fd, size_t size,
               off_t offset, dict_t *xdata)
{
        xlator_t        *this = THIS;

        if (!xl || (xl == this)) {
                xl = FIRST_CHILD(this);
        }

        STACK_WIND(frame,glupy_readdirp_cbk,xl,xl->fops->readdirp,fd,size,offset,xdata);
}

void
unwind_readdirp (call_frame_t *frame, long cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                 dict_t *xdata)
{
        frame->local = NULL;
        STACK_UNWIND_STRICT(readdirp,frame,op_ret,op_errno,
                            entries, xdata);
}

void
set_readdirp_fop (long py_this, fop_readdirp_t fop)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->fops[GLUPY_READDIRP] = (long)fop;
}

void
set_readdirp_cbk (long py_this, fop_readdirp_cbk_t cbk)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->cbks[GLUPY_READDIRP] = (long)cbk;
}


/* FOP:STAT */

int32_t
glupy_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *buf, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;

        if (!priv->cbks[GLUPY_STAT]) {
                goto unwind;
        }

        gstate = glupy_enter();
        ret = ((fop_stat_cbk_t)(priv->cbks[GLUPY_STAT]))(
                frame, cookie, this, op_ret, op_errno,
                buf, xdata);
        glupy_leave(gstate);

        return ret;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (stat, frame, op_ret, op_errno, buf, xdata);
        return 0;
}

int32_t
glupy_stat (call_frame_t *frame, xlator_t *this, loc_t *loc,
            dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;
        static long next_id = 0;

        if (!priv->fops[GLUPY_STAT]) {
                goto wind;
        }

        gstate = glupy_enter();
        frame->local = (void *)++next_id;
        ret = ((fop_stat_t)(priv->fops[GLUPY_STAT]))(
                frame, this, loc, xdata);
        glupy_leave(gstate);

        return ret;

wind:
        STACK_WIND (frame, glupy_stat_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->stat, loc, xdata);
        return 0;
}

void
wind_stat (call_frame_t *frame, xlator_t *xl, loc_t *loc, dict_t *xdata)
{
        xlator_t        *this = THIS;

        if (!xl || (xl == this)) {
                xl = FIRST_CHILD(this);
        }

        STACK_WIND(frame,glupy_stat_cbk,xl,xl->fops->stat,loc,xdata);
}

void
unwind_stat (call_frame_t *frame, long cookie, xlator_t *this,
             int32_t op_ret, int32_t op_errno, struct iatt *buf,
             dict_t *xdata)
{
        frame->local = NULL;
        STACK_UNWIND_STRICT(stat,frame,op_ret,op_errno,
                            buf,xdata);
}

void
set_stat_fop (long py_this, fop_stat_t fop)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->fops[GLUPY_STAT] = (long)fop;
}

void
set_stat_cbk (long py_this, fop_stat_cbk_t cbk)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->cbks[GLUPY_STAT] = (long)cbk;
}


/* FOP: FSTAT */

int32_t
glupy_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *buf, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;

        if (!priv->cbks[GLUPY_FSTAT]) {
                goto unwind;
        }

        gstate = glupy_enter();
        ret = ((fop_fstat_cbk_t)(priv->cbks[GLUPY_FSTAT]))(
                frame, cookie, this, op_ret, op_errno,
                buf, xdata);
        glupy_leave(gstate);

        return ret;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (fstat, frame, op_ret, op_errno, buf, xdata);
        return 0;
}

int32_t
glupy_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd,
             dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;
        static long next_id = 0;

        if (!priv->fops[GLUPY_FSTAT]) {
                goto wind;
        }

        gstate = glupy_enter();
        frame->local = (void *)++next_id;
        ret = ((fop_fstat_t)(priv->fops[GLUPY_FSTAT]))(
                frame, this, fd, xdata);
        glupy_leave(gstate);

        return ret;

wind:
        STACK_WIND (frame, glupy_fstat_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fstat, fd, xdata);
        return 0;
}

void
wind_fstat (call_frame_t *frame, xlator_t *xl, fd_t *fd, dict_t *xdata)
{
        xlator_t        *this = THIS;

        if (!xl || (xl == this)) {
                xl = FIRST_CHILD(this);
        }

        STACK_WIND(frame,glupy_fstat_cbk,xl,xl->fops->fstat,fd,xdata);
}

void
unwind_fstat (call_frame_t *frame, long cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, struct iatt *buf,
              dict_t *xdata)
{
        frame->local = NULL;
        STACK_UNWIND_STRICT(fstat,frame,op_ret,op_errno,
                            buf,xdata);
}

void
set_fstat_fop (long py_this, fop_fstat_t fop)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->fops[GLUPY_FSTAT] = (long)fop;
}

void
set_fstat_cbk (long py_this, fop_fstat_cbk_t cbk)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->cbks[GLUPY_FSTAT] = (long)cbk;
}

/* FOP:STATFS */

int32_t
glupy_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct statvfs *buf, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;

        if (!priv->cbks[GLUPY_STATFS]) {
                goto unwind;
        }

        gstate = glupy_enter();
        ret = ((fop_statfs_cbk_t)(priv->cbks[GLUPY_STATFS]))(
                frame, cookie, this, op_ret, op_errno,
                buf, xdata);
        glupy_leave(gstate);

        return ret;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (statfs, frame, op_ret, op_errno, buf, xdata);
        return 0;
}

int32_t
glupy_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc,
              dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;
        static long next_id = 0;

        if (!priv->fops[GLUPY_STATFS]) {
                goto wind;
        }

        gstate = glupy_enter();
        frame->local = (void *)++next_id;
        ret = ((fop_statfs_t)(priv->fops[GLUPY_STATFS]))(
                frame, this, loc, xdata);
        glupy_leave(gstate);

        return ret;

wind:
        STACK_WIND (frame, glupy_statfs_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->statfs, loc, xdata);
        return 0;
}

void
wind_statfs (call_frame_t *frame, xlator_t *xl, loc_t *loc, dict_t *xdata)
{
        xlator_t        *this = THIS;

        if (!xl || (xl == this)) {
                xl = FIRST_CHILD(this);
        }

        STACK_WIND(frame,glupy_statfs_cbk,xl,xl->fops->statfs,loc,xdata);
}

void
unwind_statfs (call_frame_t *frame, long cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct statvfs *buf,
               dict_t *xdata)
{
        frame->local = NULL;
        STACK_UNWIND_STRICT(statfs,frame,op_ret,op_errno,
                            buf,xdata);
}

void
set_statfs_fop (long py_this, fop_statfs_t fop)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->fops[GLUPY_STATFS] = (long)fop;
}

void
set_statfs_cbk (long py_this, fop_statfs_cbk_t cbk)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->cbks[GLUPY_STATFS] = (long)cbk;
}


/* FOP: SETXATTR */

int32_t
glupy_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;

        if (!priv->cbks[GLUPY_SETXATTR]) {
                goto unwind;
        }

        gstate = glupy_enter();
        ret = ((fop_setxattr_cbk_t)(priv->cbks[GLUPY_SETXATTR]))(
                frame, cookie, this, op_ret, op_errno,
                xdata);
        glupy_leave(gstate);

        return ret;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno, xdata);
        return 0;
}

int32_t
glupy_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                dict_t *dict, int32_t flags, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;
        static long next_id = 0;

        if (!priv->fops[GLUPY_SETXATTR]) {
                goto wind;
        }

        gstate = glupy_enter();
        frame->local = (void *)++next_id;
        ret = ((fop_setxattr_t)(priv->fops[GLUPY_SETXATTR]))(
                frame, this, loc, dict, flags, xdata);
        glupy_leave(gstate);

        return ret;

wind:
        STACK_WIND (frame, glupy_setxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr, loc, dict,
                    flags, xdata);
        return 0;
}

void
wind_setxattr (call_frame_t *frame, xlator_t *xl, loc_t *loc,
               dict_t *dict, int32_t flags, dict_t *xdata)
{
        xlator_t        *this = THIS;

        if (!xl || (xl == this)) {
                xl = FIRST_CHILD(this);
        }

        STACK_WIND (frame, glupy_setxattr_cbk, xl, xl->fops->setxattr,
                    loc, dict, flags, xdata);
}


void
unwind_setxattr (call_frame_t *frame, long cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        frame->local = NULL;
        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno, xdata);

}

void
set_setxattr_fop (long py_this, fop_setxattr_t fop)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->fops[GLUPY_SETXATTR] = (long)fop;
}

void
set_setxattr_cbk (long py_this, fop_setxattr_cbk_t cbk)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->cbks[GLUPY_SETXATTR] = (long)cbk;
}

/* FOP: GETXATTR */

int32_t
glupy_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *dict,
                    dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;

        if (!priv->cbks[GLUPY_GETXATTR]) {
                goto unwind;
        }

        gstate = glupy_enter();
        ret = ((fop_getxattr_cbk_t)(priv->cbks[GLUPY_GETXATTR]))(
                frame, cookie, this, op_ret, op_errno, dict,
                xdata);
        glupy_leave(gstate);

        return ret;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict,
                             xdata);
        return 0;
}

int32_t
glupy_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                const char *name, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;
        static long next_id = 0;

        if (!priv->fops[GLUPY_GETXATTR]) {
                goto wind;
        }

        gstate = glupy_enter();
        frame->local = (void *)++next_id;
        ret = ((fop_getxattr_t)(priv->fops[GLUPY_GETXATTR]))(
                frame, this, loc, name, xdata);
        glupy_leave(gstate);

        return ret;

wind:
        STACK_WIND (frame, glupy_getxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->getxattr, loc, name,
                    xdata);
        return 0;
}

void
wind_getxattr (call_frame_t *frame, xlator_t *xl, loc_t *loc,
               const char *name, dict_t *xdata)
{
        xlator_t        *this = THIS;

        if (!xl || (xl == this)) {
                xl = FIRST_CHILD(this);
        }

        STACK_WIND (frame, glupy_getxattr_cbk, xl, xl->fops->getxattr,
                    loc, name, xdata);
}


void
unwind_getxattr (call_frame_t *frame, long cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *dict,
                 dict_t *xdata)
{
        frame->local = NULL;
        STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict,
                             xdata);

}


void
set_getxattr_fop (long py_this, fop_getxattr_t fop)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->fops[GLUPY_GETXATTR] = (long)fop;
}


void
set_getxattr_cbk (long py_this, fop_getxattr_cbk_t cbk)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->cbks[GLUPY_GETXATTR] = (long)cbk;
}

/* FOP: FSETXATTR */

int32_t
glupy_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;

        if (!priv->cbks[GLUPY_FSETXATTR]) {
                goto unwind;
        }

        gstate = glupy_enter();
        ret = ((fop_fsetxattr_cbk_t)(priv->cbks[GLUPY_FSETXATTR]))(
                frame, cookie, this, op_ret, op_errno,
                xdata);
        glupy_leave(gstate);

        return ret;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno, xdata);
        return 0;
}

int32_t
glupy_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 dict_t *dict, int32_t flags, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;
        static long next_id = 0;

        if (!priv->fops[GLUPY_FSETXATTR]) {
                goto wind;
        }

        gstate = glupy_enter();
        frame->local = (void *)++next_id;
        ret = ((fop_fsetxattr_t)(priv->fops[GLUPY_FSETXATTR]))(
                frame, this, fd, dict, flags, xdata);
        glupy_leave(gstate);

        return ret;

wind:
        STACK_WIND (frame, glupy_fsetxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsetxattr, fd, dict,
                    flags, xdata);
        return 0;
}

void
wind_fsetxattr (call_frame_t *frame, xlator_t *xl, fd_t *fd,
                dict_t *dict, int32_t flags, dict_t *xdata)
{
        xlator_t        *this = THIS;

        if (!xl || (xl == this)) {
                xl = FIRST_CHILD(this);
        }

        STACK_WIND (frame, glupy_fsetxattr_cbk, xl, xl->fops->fsetxattr,
                    fd, dict, flags, xdata);
}


void
unwind_fsetxattr (call_frame_t *frame, long cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        frame->local = NULL;
        STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno, xdata);

}

void
set_fsetxattr_fop (long py_this, fop_fsetxattr_t fop)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->fops[GLUPY_FSETXATTR] = (long)fop;
}

void
set_fsetxattr_cbk (long py_this, fop_fsetxattr_cbk_t cbk)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->cbks[GLUPY_FSETXATTR] = (long)cbk;
}

/* FOP: FGETXATTR */

int32_t
glupy_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *dict,
                     dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;

        if (!priv->cbks[GLUPY_FGETXATTR]) {
                goto unwind;
        }

        gstate = glupy_enter();
        ret = ((fop_fgetxattr_cbk_t)(priv->cbks[GLUPY_FGETXATTR]))(
                frame, cookie, this, op_ret, op_errno, dict,
                xdata);
        glupy_leave(gstate);

        return ret;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (fgetxattr, frame, op_ret, op_errno, dict,
                             xdata);
        return 0;
}

int32_t
glupy_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 const char *name, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;
        static long next_id = 0;

        if (!priv->fops[GLUPY_FGETXATTR]) {
                goto wind;
        }

        gstate = glupy_enter();
        frame->local = (void *)++next_id;
        ret = ((fop_fgetxattr_t)(priv->fops[GLUPY_FGETXATTR]))(
                frame, this, fd, name, xdata);
        glupy_leave(gstate);

        return ret;

wind:
        STACK_WIND (frame, glupy_fgetxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fgetxattr, fd, name,
                    xdata);
        return 0;
}

void
wind_fgetxattr (call_frame_t *frame, xlator_t *xl, fd_t *fd,
                const char *name, dict_t *xdata)
{
        xlator_t        *this = THIS;

        if (!xl || (xl == this)) {
                xl = FIRST_CHILD(this);
        }

        STACK_WIND (frame, glupy_fgetxattr_cbk, xl, xl->fops->fgetxattr,
                    fd, name, xdata);
}


void
unwind_fgetxattr (call_frame_t *frame, long cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *dict,
                  dict_t *xdata)
{
        frame->local = NULL;
        STACK_UNWIND_STRICT (fgetxattr, frame, op_ret, op_errno, dict,
                             xdata);

}


void
set_fgetxattr_fop (long py_this, fop_fgetxattr_t fop)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->fops[GLUPY_FGETXATTR] = (long)fop;
}


void
set_fgetxattr_cbk (long py_this, fop_fgetxattr_cbk_t cbk)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->cbks[GLUPY_FGETXATTR] = (long)cbk;
}

/* FOP:REMOVEXATTR */

int32_t
glupy_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;

        if (!priv->cbks[GLUPY_REMOVEXATTR]) {
                goto unwind;
        }

        gstate = glupy_enter();
        ret = ((fop_removexattr_cbk_t)(priv->cbks[GLUPY_REMOVEXATTR]))(
                frame, cookie, this, op_ret, op_errno, xdata);
        glupy_leave(gstate);

        return ret;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (removexattr, frame, op_ret, op_errno, xdata);
        return 0;
}

int32_t
glupy_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                   const char *name, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;
        static long next_id = 0;

        if (!priv->fops[GLUPY_REMOVEXATTR]) {
                goto wind;
        }

        gstate = glupy_enter();
        frame->local = (void *)++next_id;
        ret = ((fop_removexattr_t)(priv->fops[GLUPY_REMOVEXATTR]))(
                frame, this, loc, name, xdata);
        glupy_leave(gstate);

        return ret;

wind:
        STACK_WIND (frame, glupy_removexattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->removexattr, loc, name,
                    xdata);
        return 0;
}

void
wind_removexattr (call_frame_t *frame, xlator_t *xl, loc_t *loc,
                  const char *name, dict_t *xdata)
{
        xlator_t        *this = THIS;

        if (!xl || (xl == this)) {
                xl = FIRST_CHILD(this);
        }

        STACK_WIND (frame, glupy_removexattr_cbk, xl, xl->fops->removexattr,
                    loc, name, xdata);
}


void
unwind_removexattr (call_frame_t *frame, long cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        frame->local = NULL;
        STACK_UNWIND_STRICT (removexattr, frame, op_ret, op_errno, xdata);

}

void
set_removexattr_fop (long py_this, fop_removexattr_t fop)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->fops[GLUPY_REMOVEXATTR] = (long)fop;
}

void
set_removexattr_cbk (long py_this, fop_removexattr_cbk_t cbk)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->cbks[GLUPY_REMOVEXATTR] = (long)cbk;
}


/* FOP:FREMOVEXATTR */

int32_t
glupy_fremovexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;

        if (!priv->cbks[GLUPY_FREMOVEXATTR]) {
                goto unwind;
        }

        gstate = glupy_enter();
        ret = ((fop_fremovexattr_cbk_t)(priv->cbks[GLUPY_FREMOVEXATTR]))(
                frame, cookie, this, op_ret, op_errno, xdata);
        glupy_leave(gstate);

        return ret;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (fremovexattr, frame, op_ret, op_errno, xdata);
        return 0;
}

int32_t
glupy_fremovexattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                   const char *name, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;
        static long next_id = 0;

        if (!priv->fops[GLUPY_FREMOVEXATTR]) {
                goto wind;
        }

        gstate = glupy_enter();
        frame->local = (void *)++next_id;
        ret = ((fop_fremovexattr_t)(priv->fops[GLUPY_FREMOVEXATTR]))(
                frame, this, fd, name, xdata);
        glupy_leave(gstate);

        return ret;

wind:
        STACK_WIND (frame, glupy_fremovexattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fremovexattr, fd, name,
                    xdata);
        return 0;
}

void
wind_fremovexattr (call_frame_t *frame, xlator_t *xl, fd_t *fd,
                   const char *name, dict_t *xdata)
{
        xlator_t        *this = THIS;

        if (!xl || (xl == this)) {
                xl = FIRST_CHILD(this);
        }

        STACK_WIND (frame, glupy_fremovexattr_cbk, xl, xl->fops->fremovexattr,
                    fd, name, xdata);
}


void
unwind_fremovexattr (call_frame_t *frame, long cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        frame->local = NULL;
        STACK_UNWIND_STRICT (fremovexattr, frame, op_ret, op_errno, xdata);

}

void
set_fremovexattr_fop (long py_this, fop_fremovexattr_t fop)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->fops[GLUPY_FREMOVEXATTR] = (long)fop;
}

void
set_fremovexattr_cbk (long py_this, fop_fremovexattr_cbk_t cbk)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->cbks[GLUPY_FREMOVEXATTR] = (long)cbk;
}


/* FOP: LINK*/
int32_t
glupy_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, inode_t *inode,
                struct iatt *buf, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;

        if (!priv->cbks[GLUPY_LINK]) {
                goto unwind;
        }

        gstate = glupy_enter();
        ret = ((fop_link_cbk_t)(priv->cbks[GLUPY_LINK]))(
                frame, cookie, this, op_ret, op_errno,
                inode, buf, preparent, postparent, xdata);
        glupy_leave(gstate);

        return ret;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (link, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}

int32_t
glupy_link(call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
           dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;
        static long next_id = 0;

        if (!priv->fops[GLUPY_LINK]) {
                goto wind;
        }

        gstate = glupy_enter();
        frame->local = (void *)++next_id;
        ret = ((fop_link_t)(priv->fops[GLUPY_LINK]))(
                frame, this, oldloc, newloc, xdata);
        glupy_leave(gstate);

        return ret;

wind:
        STACK_WIND (frame, glupy_link_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->link, oldloc, newloc,
                    xdata);
        return 0;
}

void
wind_link (call_frame_t *frame, xlator_t *xl, loc_t *oldloc, loc_t *newloc,
           dict_t *xdata)
{
        xlator_t        *this = THIS;

        if (!xl || (xl == this)) {
                xl = FIRST_CHILD(this);
        }

        STACK_WIND (frame, glupy_link_cbk, xl, xl->fops->link,
                    oldloc, newloc, xdata);
}

void
unwind_link (call_frame_t *frame, long cookie, xlator_t *this,
             int32_t op_ret, int32_t op_errno, inode_t *inode,
             struct iatt *buf, struct iatt *preparent,
             struct iatt *postparent, dict_t *xdata)
{
        frame->local = NULL;
        STACK_UNWIND_STRICT (link, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
}

void
set_link_fop (long py_this, fop_link_t fop)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->fops[GLUPY_LINK] = (long)fop;
}

void
set_link_cbk (long py_this, fop_link_cbk_t cbk)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->cbks[GLUPY_LINK] = (long)cbk;
}

/* FOP: SYMLINK*/
int32_t
glupy_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;

        if (!priv->cbks[GLUPY_SYMLINK]) {
                goto unwind;
        }

        gstate = glupy_enter();
        ret = ((fop_symlink_cbk_t)(priv->cbks[GLUPY_SYMLINK]))(
                frame, cookie, this, op_ret, op_errno,
                inode, buf, preparent, postparent, xdata);
        glupy_leave(gstate);

        return ret;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (symlink, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}

int32_t
glupy_symlink(call_frame_t *frame, xlator_t *this, const char *linkname,
              loc_t *loc, mode_t umask, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;
        static long next_id = 0;

        if (!priv->fops[GLUPY_SYMLINK]) {
                goto wind;
        }

        gstate = glupy_enter();
        frame->local = (void *)++next_id;
        ret = ((fop_symlink_t)(priv->fops[GLUPY_SYMLINK]))(
                frame, this, linkname, loc, umask, xdata);
        glupy_leave(gstate);

        return ret;

wind:
        STACK_WIND (frame, glupy_symlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->symlink, linkname, loc,
                    umask, xdata);
        return 0;
}

void
wind_symlink (call_frame_t *frame, xlator_t *xl, const char *linkname,
              loc_t *loc, mode_t umask, dict_t *xdata)
{
        xlator_t        *this = THIS;

        if (!xl || (xl == this)) {
                xl = FIRST_CHILD(this);
        }

        STACK_WIND (frame, glupy_symlink_cbk, xl, xl->fops->symlink,
                    linkname, loc, umask, xdata);
}

void
unwind_symlink (call_frame_t *frame, long cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, inode_t *inode,
                struct iatt *buf, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
        frame->local = NULL;
        STACK_UNWIND_STRICT (symlink, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
}

void
set_symlink_fop (long py_this, fop_symlink_t fop)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->fops[GLUPY_SYMLINK] = (long)fop;
}

void
set_symlink_cbk (long py_this, fop_symlink_cbk_t cbk)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->cbks[GLUPY_SYMLINK] = (long)cbk;
}


/* FOP: READLINK */
int32_t
glupy_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, const char *path,
                    struct iatt *buf, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;

        if (!priv->cbks[GLUPY_READLINK]) {
                goto unwind;
        }

        gstate = glupy_enter();
        ret = ((fop_readlink_cbk_t)(priv->cbks[GLUPY_READLINK]))(
                frame, cookie, this, op_ret, op_errno,
                path, buf, xdata);
        glupy_leave(gstate);

        return ret;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (readlink, frame, op_ret, op_errno, path,
                             buf, xdata);
        return 0;
}

int32_t
glupy_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc,
                size_t size, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;
        static long next_id = 0;

        if (!priv->fops[GLUPY_READLINK]) {
                goto wind;
        }

        gstate = glupy_enter();
        frame->local = (void *)++next_id;
        ret = ((fop_readlink_t)(priv->fops[GLUPY_READLINK]))(
                frame, this, loc, size, xdata);
        glupy_leave(gstate);

        return ret;

wind:
        STACK_WIND (frame, glupy_readlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readlink, loc,
                    size, xdata);
        return 0;
}

void
wind_readlink (call_frame_t *frame, xlator_t *xl, loc_t *loc,
               size_t size, dict_t *xdata)
{
        xlator_t        *this = THIS;

        if (!xl || (xl == this)) {
                xl = FIRST_CHILD(this);
        }

        STACK_WIND (frame, glupy_readlink_cbk, xl, xl->fops->readlink,
                    loc, size, xdata);
}

void
unwind_readlink (call_frame_t *frame, long cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, const char *path,
                 struct iatt *buf, dict_t *xdata)
{
        frame->local = NULL;
        STACK_UNWIND_STRICT (readlink, frame, op_ret, op_errno, path, buf,
                             xdata);
}

void
set_readlink_fop (long py_this, fop_readlink_t fop)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->fops[GLUPY_READLINK] = (long)fop;
}

void
set_readlink_cbk (long py_this, fop_readlink_cbk_t cbk)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->cbks[GLUPY_READLINK] = (long)cbk;
}


/* FOP: UNLINK */

int32_t
glupy_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  struct iatt *preparent, struct iatt *postparent,
                  dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;

        if (!priv->cbks[GLUPY_UNLINK]) {
                goto unwind;
        }

        gstate = glupy_enter();
        ret = ((fop_unlink_cbk_t)(priv->cbks[GLUPY_UNLINK]))(
                frame, cookie, this, op_ret, op_errno,
                preparent, postparent, xdata);
        glupy_leave(gstate);

        return ret;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (unlink, frame, op_ret, op_errno, preparent,
                             postparent, xdata);
        return 0;
}

int32_t
glupy_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc,
              int xflags, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;
        static long next_id = 0;

        if (!priv->fops[GLUPY_UNLINK]) {
                goto wind;
        }

        gstate = glupy_enter();
        frame->local = (void *)++next_id;
        ret = ((fop_unlink_t)(priv->fops[GLUPY_UNLINK]))(
                frame, this, loc, xflags, xdata);
        glupy_leave(gstate);

        return ret;

wind:
        STACK_WIND (frame, glupy_unlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink, loc,
                    xflags, xdata);
        return 0;
}

void
wind_unlink (call_frame_t *frame, xlator_t *xl, loc_t *loc,
             int xflags, dict_t *xdata)
{
        xlator_t        *this = THIS;

        if (!xl || (xl == this)) {
                xl = FIRST_CHILD(this);
        }

        STACK_WIND (frame, glupy_unlink_cbk, xl, xl->fops->unlink,
                    loc, xflags, xdata);
}

void
unwind_unlink (call_frame_t *frame, long cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno,
               struct iatt *preparent, struct iatt *postparent,
               dict_t *xdata)
{
        frame->local = NULL;
        STACK_UNWIND_STRICT (unlink, frame, op_ret, op_errno,
                             preparent, postparent, xdata);
}

void
set_unlink_fop (long py_this, fop_unlink_t fop)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->fops[GLUPY_UNLINK] = (long)fop;
}

void
set_unlink_cbk (long py_this, fop_unlink_cbk_t cbk)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->cbks[GLUPY_UNLINK] = (long)cbk;
}


/* FOP: MKDIR */

int32_t
glupy_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, inode_t *inode,
                 struct iatt *buf, struct iatt *preparent,
                 struct iatt *postparent, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;

        if (!priv->cbks[GLUPY_MKDIR]) {
                goto unwind;
        }

        gstate = glupy_enter();
        ret = ((fop_mkdir_cbk_t)(priv->cbks[GLUPY_MKDIR]))(
                frame, cookie, this, op_ret, op_errno,
                inode, buf, preparent, postparent, xdata);
        glupy_leave(gstate);

        return ret;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (mkdir, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}

int32_t
glupy_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
             mode_t umask, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;
        static long next_id = 0;

        if (!priv->fops[GLUPY_MKDIR]) {
                goto wind;
        }

        gstate = glupy_enter();
        frame->local = (void *)++next_id;
        ret = ((fop_mkdir_t)(priv->fops[GLUPY_MKDIR]))(
                frame, this, loc, mode, umask, xdata);
        glupy_leave(gstate);

        return ret;

wind:
        STACK_WIND (frame, glupy_mkdir_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mkdir, loc, mode, umask,
                    xdata);
        return 0;
}

void
wind_mkdir (call_frame_t *frame, xlator_t *xl, loc_t *loc, mode_t mode,
            mode_t umask, dict_t *xdata)
{

        xlator_t        *this = THIS;

        if (!xl || (xl == this)) {
                xl = FIRST_CHILD(this);
        }

        STACK_WIND (frame, glupy_mkdir_cbk, xl, xl->fops->mkdir,
                    loc, mode, umask, xdata);
}

void
unwind_mkdir (call_frame_t *frame, long cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, inode_t *inode,
              struct iatt *buf, struct iatt *preparent,
              struct iatt *postparent, dict_t *xdata)
{
        frame->local = NULL;
        STACK_UNWIND_STRICT (mkdir, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
}

void
set_mkdir_fop (long py_this, fop_mkdir_t fop)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->fops[GLUPY_MKDIR] = (long)fop;
}

void
set_mkdir_cbk (long py_this, fop_mkdir_cbk_t cbk)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->cbks[GLUPY_MKDIR] = (long)cbk;
}


/* FOP: RMDIR */

int32_t
glupy_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 struct iatt *preparent, struct iatt *postparent,
                 dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;

        if (!priv->cbks[GLUPY_RMDIR]) {
                goto unwind;
        }

        gstate = glupy_enter();
        ret = ((fop_rmdir_cbk_t)(priv->cbks[GLUPY_RMDIR]))(
                frame, cookie, this, op_ret, op_errno,
                preparent, postparent, xdata);
        glupy_leave(gstate);

        return ret;

unwind:
        frame->local = NULL;
        STACK_UNWIND_STRICT (rmdir, frame, op_ret, op_errno, preparent,
                             postparent, xdata);
        return 0;
}

int32_t
glupy_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc,
             int xflags, dict_t *xdata)
{
        glupy_private_t *priv = this->private;
        PyGILState_STATE gstate;
        int32_t ret;
        static long next_id = 0;

        if (!priv->fops[GLUPY_RMDIR]) {
                goto wind;
        }

        gstate = glupy_enter();
        frame->local = (void *)++next_id;
        ret = ((fop_rmdir_t)(priv->fops[GLUPY_RMDIR]))(
                frame, this, loc, xflags, xdata);
        glupy_leave(gstate);

        return ret;

wind:
        STACK_WIND (frame, glupy_rmdir_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rmdir, loc,
                    xflags, xdata);
        return 0;
}

void
wind_rmdir (call_frame_t *frame, xlator_t *xl, loc_t *loc,
            int xflags, dict_t *xdata)
{

        xlator_t        *this = THIS;

        if (!xl || (xl == this)) {
                xl = FIRST_CHILD(this);
        }

        STACK_WIND (frame, glupy_rmdir_cbk, xl, xl->fops->rmdir,
                    loc, xflags, xdata);
}

void
unwind_rmdir (call_frame_t *frame, long cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno,
              struct iatt *preparent, struct iatt *postparent,
              dict_t *xdata)
{
        frame->local = NULL;
        STACK_UNWIND_STRICT (rmdir, frame, op_ret, op_errno,
                             preparent, postparent, xdata);
}

void
set_rmdir_fop (long py_this, fop_rmdir_t fop)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->fops[GLUPY_RMDIR] = (long)fop;
}

void
set_rmdir_cbk (long py_this, fop_rmdir_cbk_t cbk)
{
        glupy_private_t *priv   = ((xlator_t *)py_this)->private;

        priv->cbks[GLUPY_RMDIR] = (long)cbk;
}


/* NON-FOP-SPECIFIC CODE */


long
get_id (call_frame_t *frame)
{
        return (long)(frame->local);
}

uint64_t
get_rootunique (call_frame_t *frame)
{
        return frame->root->unique;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_glupy_mt_end);

        if (ret != 0) {
                gf_log(this->name, GF_LOG_ERROR, "Memory accounting init"
                       " failed");
                return ret;
        }

        return ret;
}

int32_t
init (xlator_t *this)
{
        glupy_private_t         *priv           = NULL;
        char                    *module_name    = NULL;
        PyObject                *py_mod_name    = NULL;
        PyObject                *py_init_func   = NULL;
        PyObject                *py_args        = NULL;
        PyObject                *syspath        = NULL;
	PyObject                *path           = NULL;
	PyObject                *error_type     = NULL;
	PyObject                *error_msg      = NULL;
	PyObject                *error_bt       = NULL;
	static gf_boolean_t      py_inited      = _gf_false;
        void *                   err_cleanup    = &&err_return;

        if (dict_get_str(this->options,"module-name",&module_name) != 0) {
                gf_log (this->name, GF_LOG_ERROR, "missing module-name");
                return -1;
        }

	priv = GF_CALLOC (1, sizeof (glupy_private_t), gf_glupy_mt_priv);
        if (!priv) {
                goto *err_cleanup;
        }
        this->private = priv;
        err_cleanup = &&err_free_priv;

	if (!py_inited) {
	        /* 
                 * This must be done before Py_Initialize(),
                 * because it will duplicate the environment,
                 * and fail to see later environment updates.
                 */
	        setenv("PATH_GLUSTERFS_GLUPY_MODULE",
                       PATH_GLUSTERFS_GLUPY_MODULE, 1);

                Py_Initialize();
                PyEval_InitThreads();

                (void)pthread_key_create(&gil_init_key,NULL);
                (void)pthread_setspecific(gil_init_key,(void *)1);

                /* PyEval_InitThreads takes this "for" us.  No thanks. */
                PyEval_ReleaseLock();
                py_inited = _gf_true;
        }

	/* Adjust python's path */
	syspath = PySys_GetObject("path");
	path = PyString_FromString(GLUSTER_PYTHON_PATH);
	PyList_Append(syspath, path);
	Py_DECREF(path);

	py_mod_name = PyString_FromString(module_name);
        if (!py_mod_name) {
                gf_log (this->name, GF_LOG_ERROR, "could not create name");
                if (PyErr_Occurred()) {
                        PyErr_Fetch (&error_type, &error_msg, &error_bt);
                        gf_log (this->name, GF_LOG_ERROR, "Python error: %s",
                                PyString_AsString(error_msg));
                }
                goto *err_cleanup;
        }

        gf_log (this->name, GF_LOG_DEBUG, "py_mod_name = %s", module_name);
        priv->py_module = PyImport_Import(py_mod_name);
        Py_DECREF(py_mod_name);
        if (!priv->py_module) {
                gf_log (this->name, GF_LOG_ERROR, "Python import of %s failed",
                        module_name);
                if (PyErr_Occurred()) {
                        PyErr_Fetch (&error_type, &error_msg, &error_bt);
                        gf_log (this->name, GF_LOG_ERROR, "Python error: %s",
                                PyString_AsString(error_msg));
                }
                goto *err_cleanup;
        }
        gf_log (this->name, GF_LOG_INFO, "Import of %s succeeded", module_name);
        err_cleanup = &&err_deref_module;

        py_init_func = PyObject_GetAttrString(priv->py_module, "xlator");
        if (!py_init_func || !PyCallable_Check(py_init_func)) {
                gf_log (this->name, GF_LOG_ERROR, "missing init func");
                if (PyErr_Occurred()) {
                        PyErr_Fetch (&error_type, &error_msg, &error_bt);
                        gf_log (this->name, GF_LOG_ERROR, "Python error: %s",
                                PyString_AsString(error_msg));
                }
                goto *err_cleanup;
        }
        err_cleanup = &&err_deref_init;

        py_args = PyTuple_New(1);
        if (!py_args) {
                gf_log (this->name, GF_LOG_ERROR, "could not create args");
                if (PyErr_Occurred()) {
                        PyErr_Fetch (&error_type, &error_msg, &error_bt);
                        gf_log (this->name, GF_LOG_ERROR, "Python error: %s",
                                PyString_AsString(error_msg));
                }
                goto *err_cleanup;
        }
        PyTuple_SetItem(py_args,0,PyLong_FromLong((long)this));

        /* TBD: pass in list of children */
        priv->py_xlator = PyObject_CallObject(py_init_func, py_args);
        Py_DECREF(py_args);
        if (!priv->py_xlator) {
                gf_log (this->name, GF_LOG_ERROR, "Python init failed");
                if (PyErr_Occurred()) {
                        PyErr_Fetch (&error_type, &error_msg, &error_bt);
                        gf_log (this->name, GF_LOG_ERROR, "Python error: %s",
                                PyString_AsString(error_msg));
                }
                goto *err_cleanup;
        }
        gf_log (this->name, GF_LOG_DEBUG, "init returned %p", priv->py_xlator);

        return 0;

err_deref_init:
        Py_DECREF(py_init_func);
err_deref_module:
        Py_DECREF(priv->py_module);
err_free_priv:
        GF_FREE(priv);
err_return:
        return -1;
}

void
fini (xlator_t *this)
{
        glupy_private_t *priv = this->private;

        if (!priv)
                return;
        Py_DECREF(priv->py_xlator);
        Py_DECREF(priv->py_module);
        this->private = NULL;
        GF_FREE (priv);

        return;
}

struct xlator_fops fops = {
        .lookup       = glupy_lookup,
        .create       = glupy_create,
        .open         = glupy_open,
        .readv        = glupy_readv,
        .writev       = glupy_writev,
        .opendir      = glupy_opendir,
        .readdir      = glupy_readdir,
        .stat         = glupy_stat,
        .fstat        = glupy_fstat,
        .setxattr     = glupy_setxattr,
        .getxattr     = glupy_getxattr,
        .fsetxattr    = glupy_fsetxattr,
        .fgetxattr    = glupy_fgetxattr,
        .removexattr  = glupy_removexattr,
        .fremovexattr = glupy_fremovexattr,
        .link         = glupy_link,
        .unlink       = glupy_unlink,
        .readlink     = glupy_readlink,
        .symlink      = glupy_symlink,
        .mkdir        = glupy_mkdir,
        .rmdir        = glupy_rmdir,
        .statfs       = glupy_statfs,
        .readdirp     = glupy_readdirp
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
        { .key  = {NULL} },
};
