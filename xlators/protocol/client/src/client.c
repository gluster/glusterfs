/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
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


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "client.h"
#include "xlator.h"
#include "defaults.h"
#include "glusterfs.h"
#include "statedump.h"
#include "compat-errno.h"

#include "glusterfs3.h"

extern rpc_clnt_prog_t clnt_handshake_prog;
extern rpc_clnt_prog_t clnt_dump_prog;

int
client_handshake (xlator_t *this, struct rpc_clnt *rpc);

void
client_start_ping (void *data);

int
client_submit_request (xlator_t *this, void *req, call_frame_t *frame,
                       rpc_clnt_prog_t *prog, int procnum, fop_cbk_fn_t cbk,
                       struct iobref *iobref, gfs_serialize_t sfunc)
{
        int          ret         = -1;
        clnt_conf_t *conf        = NULL;
        struct iovec iov         = {0, };
        struct iobuf *iobuf      = NULL;
        int           count      = 0;
        char          new_iobref = 0, start_ping = 0;

        if (!this || !prog || !frame)
                goto out;

        conf = this->private;

        /* If 'setvolume' is not successful, we should not send frames to
           server, mean time we should be able to send 'DUMP' and 'SETVOLUME'
           call itself even if its not connected */
        if (!(conf->connected ||
              ((prog->prognum == GLUSTER_DUMP_PROGRAM) ||
               ((prog->prognum == GLUSTER_HNDSK_PROGRAM) && (procnum == GF_HNDSK_SETVOLUME)))))
                goto out;

        iobuf = iobuf_get (this->ctx->iobuf_pool);
        if (!iobuf) {
                goto out;
        };

        if (!iobref) {
                iobref = iobref_new ();
                if (!iobref) {
                        goto out;
                }

                new_iobref = 1;
        }

        iobref_add (iobref, iobuf);

        iov.iov_base = iobuf->ptr;
        iov.iov_len  = 128 * GF_UNIT_KB;

        /* Create the xdr payload */
        if (req && sfunc) {
                ret = sfunc (iov, req);
                if (ret == -1) {
                        goto out;
                }
                iov.iov_len = ret;
                count = 1;
        }
        /* Send the msg */
        ret = rpc_clnt_submit (conf->rpc, prog, procnum, cbk, &iov, count, NULL, 0,
                               iobref, frame, NULL, 0, NULL, 0, NULL);

        if (ret == 0) {
                pthread_mutex_lock (&conf->rpc->conn.lock);
                {
                        if (!conf->rpc->conn.ping_started) {
                                start_ping = 1;
                        }
                }
                pthread_mutex_unlock (&conf->rpc->conn.lock);
        }

        if (start_ping)
                client_start_ping ((void *) this);

        ret = 0;
out:
        if (new_iobref)
                iobref_unref (iobref);

        if (iobuf)
                iobuf_unref (iobuf);

        return ret;
}


int32_t
client_forget (xlator_t *this, inode_t *inode)
{
        /* Nothing here */
	return 0;
}

int32_t
client_releasedir (xlator_t *this, fd_t *fd)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};
        call_frame_t *frame = NULL;

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.fd = fd;

        proc = &conf->fops->proctable[GF_FOP_RELEASEDIR];
        if (proc->fn) {
                frame = create_frame (this, this->ctx->pool);
                if (!frame) {
                        goto out;
                }
                ret = proc->fn (frame, this, &args);
        }
out:
        if (ret)
                gf_log (this->name, GF_LOG_TRACE,
                        "releasedir fop failed");
	return 0;
}

int32_t
client_release (xlator_t *this, fd_t *fd)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};
        call_frame_t *frame = NULL;

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.fd = fd;
        proc = &conf->fops->proctable[GF_FOP_RELEASE];
        if (proc->fn) {
                frame = create_frame (this, this->ctx->pool);
                if (!frame) {
                        goto out;
                }
                ret = proc->fn (frame, this, &args);
        }
out:
        if (ret)
                gf_log (this->name, GF_LOG_TRACE,
                        "release fop failed");
	return 0;
}


int32_t
client_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc,
               dict_t *xattr_req)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.loc = loc;
        args.dict = xattr_req;

        proc = &conf->fops->proctable[GF_FOP_LOOKUP];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        /* think of avoiding a missing frame */
        if (ret)
                STACK_UNWIND_STRICT (lookup, frame, -1, ENOTCONN,
                                     NULL, NULL, NULL, NULL);

	return 0;
}


int32_t
client_stat (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.loc = loc;

        proc = &conf->fops->proctable[GF_FOP_STAT];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (stat, frame, -1, ENOTCONN, NULL);


	return 0;
}


int32_t
client_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.loc    = loc;
        args.offset = offset;

        proc = &conf->fops->proctable[GF_FOP_TRUNCATE];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (truncate, frame, -1, ENOTCONN, NULL, NULL);


	return 0;
}


int32_t
client_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.fd     = fd;
        args.offset = offset;

        proc = &conf->fops->proctable[GF_FOP_FTRUNCATE];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (ftruncate, frame, -1, ENOTCONN, NULL, NULL);

	return 0;
}



int32_t
client_access (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.loc  = loc;
        args.mask = mask;

        proc = &conf->fops->proctable[GF_FOP_ACCESS];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (access, frame, -1, ENOTCONN);

	return 0;
}




int32_t
client_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.loc  = loc;
        args.size = size;

        proc = &conf->fops->proctable[GF_FOP_READLINK];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (readlink, frame, -1, ENOTCONN, NULL, NULL);

	return 0;
}



int32_t
client_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
              dev_t rdev)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.loc  = loc;
        args.mode = mode;
        args.rdev = rdev;

        proc = &conf->fops->proctable[GF_FOP_MKNOD];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (mknod, frame, -1, ENOTCONN,
                                     NULL, NULL, NULL, NULL);

	return 0;
}



int32_t
client_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc,
              mode_t mode)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.loc = loc;
        args.mode = mode;

        proc = &conf->fops->proctable[GF_FOP_MKDIR];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (mkdir, frame, -1, ENOTCONN,
                                     NULL, NULL, NULL, NULL);

	return 0;
}



int32_t
client_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.loc = loc;

        proc = &conf->fops->proctable[GF_FOP_UNLINK];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (unlink, frame, -1, ENOTCONN,
                                     NULL, NULL);

	return 0;
}

int32_t
client_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.loc = loc;

        proc = &conf->fops->proctable[GF_FOP_RMDIR];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        /* think of avoiding a missing frame */
        if (ret)
                STACK_UNWIND_STRICT (rmdir, frame, -1, ENOTCONN,
                                     NULL, NULL);

	return 0;
}



int32_t
client_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
                loc_t *loc)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.linkname = linkpath;
        args.loc      = loc;

        proc = &conf->fops->proctable[GF_FOP_SYMLINK];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (symlink, frame, -1, ENOTCONN,
                                     NULL, NULL, NULL, NULL);

	return 0;
}



int32_t
client_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
               loc_t *newloc)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.oldloc = oldloc;
        args.newloc = newloc;
        proc = &conf->fops->proctable[GF_FOP_RENAME];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (rename, frame, -1, ENOTCONN,
                                     NULL, NULL, NULL, NULL, NULL);

	return 0;
}



int32_t
client_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
             loc_t *newloc)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.oldloc = oldloc;
        args.newloc = newloc;

        proc = &conf->fops->proctable[GF_FOP_LINK];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (link, frame, -1, ENOTCONN,
                                     NULL, NULL, NULL, NULL);

	return 0;
}



int32_t
client_create (call_frame_t *frame, xlator_t *this, loc_t *loc,
               int32_t flags, mode_t mode, fd_t *fd)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.loc = loc;
        args.flags = flags;
        args.mode = mode;
        args.fd = fd;

        proc = &conf->fops->proctable[GF_FOP_CREATE];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (create, frame, -1, ENOTCONN,
                                     NULL, NULL, NULL, NULL, NULL);

	return 0;
}



int32_t
client_open (call_frame_t *frame, xlator_t *this, loc_t *loc,
             int32_t flags, fd_t *fd, int32_t wbflags)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.loc = loc;
        args.flags = flags;
        args.fd = fd;
        args.wbflags = wbflags;

        proc = &conf->fops->proctable[GF_FOP_OPEN];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);

out:
        if (ret)
                STACK_UNWIND_STRICT (open, frame, -1, ENOTCONN, NULL);

	return 0;
}



int32_t
client_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
              off_t offset)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.fd     = fd;
        args.size   = size;
        args.offset = offset;

        proc = &conf->fops->proctable[GF_FOP_READ];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);

out:
        if (ret)
                STACK_UNWIND_STRICT (readv, frame, -1, ENOTCONN,
                                     NULL, 0, NULL, NULL);

	return 0;
}




int32_t
client_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
               struct iovec *vector, int32_t count, off_t off,
               struct iobref *iobref)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.fd     = fd;
        args.vector = vector;
        args.count  = count;
        args.offset = off;
        args.iobref = iobref;

        proc = &conf->fops->proctable[GF_FOP_WRITE];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (writev, frame, -1, ENOTCONN, NULL, NULL);

	return 0;
}



int32_t
client_flush (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.fd = fd;

        proc = &conf->fops->proctable[GF_FOP_FLUSH];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (flush, frame, -1, ENOTCONN);

	return 0;
}



int32_t
client_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd,
              int32_t flags)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.fd    = fd;
        args.flags = flags;

        proc = &conf->fops->proctable[GF_FOP_FSYNC];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (fsync, frame, -1, ENOTCONN, NULL, NULL);

	return 0;
}



int32_t
client_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.fd = fd;

        proc = &conf->fops->proctable[GF_FOP_FSTAT];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (fstat, frame, -1, ENOTCONN, NULL);

	return 0;
}



int32_t
client_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.loc = loc;
        args.fd  = fd;

        proc = &conf->fops->proctable[GF_FOP_OPENDIR];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (opendir, frame, -1, ENOTCONN, NULL);

	return 0;
}



int32_t
client_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.fd    = fd;
        args.flags = flags;

        proc = &conf->fops->proctable[GF_FOP_FSYNCDIR];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (fsyncdir, frame, -1, ENOTCONN);

	return 0;
}



int32_t
client_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.loc = loc;

        proc = &conf->fops->proctable[GF_FOP_STATFS];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (statfs, frame, -1, ENOTCONN, NULL);

	return 0;
}



int32_t
client_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
                 int32_t flags)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.loc   = loc;
        args.dict  = dict;
        args.flags = flags;

        proc = &conf->fops->proctable[GF_FOP_SETXATTR];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (setxattr, frame, -1, ENOTCONN);

	return 0;
}



int32_t
client_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                  dict_t *dict, int32_t flags)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.fd = fd;
        args.dict = dict;
        args.flags = flags;

        proc = &conf->fops->proctable[GF_FOP_FSETXATTR];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (fsetxattr, frame, -1, ENOTCONN);

	return 0;
}




int32_t
client_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                  const char *name)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.fd = fd;
        args.name = name;

        proc = &conf->fops->proctable[GF_FOP_FGETXATTR];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (fgetxattr, frame, -1, ENOTCONN, NULL);

	return 0;
}



int32_t
client_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                 const char *name)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.name = name;
        args.loc  = loc;

        proc = &conf->fops->proctable[GF_FOP_GETXATTR];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (getxattr, frame, -1, ENOTCONN, NULL);

	return 0;
}



int32_t
client_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
                gf_xattrop_flags_t flags, dict_t *dict)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.loc = loc;
        args.flags = flags;
        args.dict = dict;

        proc = &conf->fops->proctable[GF_FOP_XATTROP];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (xattrop, frame, -1, ENOTCONN, NULL);

	return 0;
}



int32_t
client_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 gf_xattrop_flags_t flags, dict_t *dict)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.fd = fd;
        args.flags = flags;
        args.dict = dict;

        proc = &conf->fops->proctable[GF_FOP_FXATTROP];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (fxattrop, frame, -1, ENOTCONN, NULL);

	return 0;
}



int32_t
client_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    const char *name)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.name = name;
        args.loc  = loc;

        proc = &conf->fops->proctable[GF_FOP_REMOVEXATTR];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (removexattr, frame, -1, ENOTCONN);

	return 0;
}


int32_t
client_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
           struct flock *lock)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.fd    = fd;
        args.cmd   = cmd;
        args.flock = lock;

        proc = &conf->fops->proctable[GF_FOP_LK];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (lk, frame, -1, ENOTCONN, NULL);

	return 0;
}


int32_t
client_inodelk (call_frame_t *frame, xlator_t *this, const char *volume,
                loc_t *loc, int32_t cmd, struct flock *lock)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.loc    = loc;
        args.cmd    = cmd;
        args.flock  = lock;
        args.volume = volume;

        proc = &conf->fops->proctable[GF_FOP_INODELK];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (inodelk, frame, -1, ENOTCONN);

	return 0;
}



int32_t
client_finodelk (call_frame_t *frame, xlator_t *this, const char *volume,
                 fd_t *fd, int32_t cmd, struct flock *lock)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.fd     = fd;
        args.cmd    = cmd;
        args.flock  = lock;
        args.volume = volume;

        proc = &conf->fops->proctable[GF_FOP_FINODELK];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (finodelk, frame, -1, ENOTCONN);

	return 0;
}


int32_t
client_entrylk (call_frame_t *frame, xlator_t *this, const char *volume,
                loc_t *loc, const char *basename, entrylk_cmd cmd,
                entrylk_type type)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.loc          = loc;
        args.basename     = basename;
        args.type         = type;
        args.volume       = volume;
        args.cmd_entrylk  = cmd;

        proc = &conf->fops->proctable[GF_FOP_ENTRYLK];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (entrylk, frame, -1, ENOTCONN);

	return 0;
}



int32_t
client_fentrylk (call_frame_t *frame, xlator_t *this, const char *volume,
                 fd_t *fd, const char *basename, entrylk_cmd cmd,
                 entrylk_type type)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.fd           = fd;
        args.basename     = basename;
        args.type         = type;
        args.volume       = volume;
        args.cmd_entrylk  = cmd;

        proc = &conf->fops->proctable[GF_FOP_FENTRYLK];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (fentrylk, frame, -1, ENOTCONN);

	return 0;
}


int32_t
client_rchecksum (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                  int32_t len)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.fd = fd;
        args.offset = offset;
        args.len = len;

        proc = &conf->fops->proctable[GF_FOP_RCHECKSUM];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (rchecksum, frame, -1, ENOTCONN, 0, NULL);

	return 0;
}

int32_t
client_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd,
                size_t size, off_t off)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.fd = fd;
        args.size = size;
        args.offset = off;

        proc = &conf->fops->proctable[GF_FOP_READDIR];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (readdir, frame, -1, ENOTCONN, NULL);

	return 0;
}


int32_t
client_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 size_t size, off_t off)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.fd = fd;
        args.size = size;
        args.offset = off;

        proc = &conf->fops->proctable[GF_FOP_READDIRP];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (readdirp, frame, -1, ENOTCONN, NULL);

	return 0;
}


int32_t
client_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                struct iatt *stbuf, int32_t valid)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.loc = loc;
        args.stbuf = stbuf;
        args.valid = valid;

        proc = &conf->fops->proctable[GF_FOP_SETATTR];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (setattr, frame, -1, ENOTCONN, NULL, NULL);

	return 0;
}

int32_t
client_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 struct iatt *stbuf, int32_t valid)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops)
                goto out;

        args.fd = fd;
        args.stbuf = stbuf;
        args.valid = valid;

        proc = &conf->fops->proctable[GF_FOP_FSETATTR];
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (fsetattr, frame, -1, ENOTCONN, NULL, NULL);

	return 0;
}


int32_t
client_getspec (call_frame_t *frame, xlator_t *this, const char *key,
                int32_t flags)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf->fops || !conf->handshake)
                goto out;

        args.name = key;
        args.flags = flags;

        /* For all other xlators, getspec is an fop, hence its in fops table */
        proc = &conf->fops->proctable[GF_FOP_GETSPEC];
        if (proc->fn) {
                /* But at protocol level, this is handshake */
                ret = proc->fn (frame, this, &args);
        }
out:
        if (ret)
                STACK_UNWIND_STRICT (getspec, frame, -1, EINVAL, NULL);

	return 0;
}


 int
client_mark_fd_bad (xlator_t *this)
{
        clnt_conf_t            *conf = NULL;
        clnt_fd_ctx_t          *tmp = NULL, *fdctx = NULL;

        conf = this->private;

        pthread_mutex_lock (&conf->lock);
        {
                list_for_each_entry_safe (fdctx, tmp, &conf->saved_fds,
                                          sfd_pos) {
                        fdctx->remote_fd = -1;
                }
        }
        pthread_mutex_unlock (&conf->lock);

        return 0;
}


int
client_rpc_notify (struct rpc_clnt *rpc, void *mydata, rpc_clnt_event_t event,
                   void *data)
{
        xlator_t *this = NULL;
        char *handshake = NULL;
        clnt_conf_t  *conf = NULL;
        int ret = 0;

        this = mydata;
        conf = this->private;

        switch (event) {
        case RPC_CLNT_CONNECT:
        {
                // connect happened, send 'get_supported_versions' mop
                ret = dict_get_str (this->options, "disable-handshake",
                                    &handshake);

                gf_log (this->name, GF_LOG_TRACE, "got RPC_CLNT_CONNECT");

                if ((ret < 0) || (strcasecmp (handshake, "on"))) {
                        ret = client_handshake (this, conf->rpc);
                        if (ret)
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "handshake msg returned %d", ret);
                } else {
                        //conf->rpc->connected = 1;
                        ret = default_notify (this, GF_EVENT_CHILD_UP, NULL);
                        if (ret)
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "default notify failed");
                }
                break;
        }
        case RPC_CLNT_DISCONNECT:

                client_mark_fd_bad (this);

                gf_log (this->name, GF_LOG_TRACE, "got RPC_CLNT_DISCONNECT");

                default_notify (this, GF_EVENT_CHILD_DOWN, NULL);
                break;

        default:
                gf_log (this->name, GF_LOG_TRACE,
                        "got some other RPC event %d", event);

                break;
        }

        return 0;
}


int
notify (xlator_t *this, int32_t event, void *data, ...)
{
        clnt_conf_t *conf  = NULL;
        void        *trans = NULL;

        conf = this->private;

        switch (event) {
        case GF_EVENT_PARENT_UP:
        {
                if (conf->rpc)
                        trans = conf->rpc->conn.trans;

                if (!trans) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "transport init failed");
                        return 0;
                }

                gf_log (this->name, GF_LOG_DEBUG,
                        "got GF_EVENT_PARENT_UP, attempting connect "
                        "on transport");

                rpc_clnt_reconnect (trans);
        }
        break;

        default:
                gf_log (this->name, GF_LOG_DEBUG,
                        "got %d, calling default_notify ()", event);

                default_notify (this, event, data);
                break;
        }

        return 0;
}

int
build_client_config (xlator_t *this, clnt_conf_t *conf)
{
        int ret = 0;

        ret = dict_get_str (this->options, "remote-subvolume",
                            &conf->opt.remote_subvolume);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "option 'remote-subvolume' not given");
                goto out;
        }

        ret = dict_get_int32 (this->options, "frame-timeout",
                              &conf->rpc_conf.rpc_timeout);
        if (ret >= 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "setting frame-timeout to %d",
                        conf->rpc_conf.rpc_timeout);
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "defaulting frame-timeout to 30mins");
                conf->rpc_conf.rpc_timeout = 1800;
        }

        ret = dict_get_int32 (this->options, "remote-port",
                              &conf->rpc_conf.remote_port);
        if (ret >= 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "remote-port is %d", conf->rpc_conf.remote_port);
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "defaulting remote-port to 'auto'");
        }

        ret = dict_get_int32 (this->options, "ping-timeout",
                              &conf->opt.ping_timeout);
        if (ret >= 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "setting ping-timeout to %d", conf->opt.ping_timeout);
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "defaulting ping-timeout to 42");
                conf->opt.ping_timeout = GF_UNIVERSAL_ANSWER;
        }

        ret = 0;
out:
        return ret;
}


int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_client_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
                                "failed");
                return ret;
        }

        return ret;
}


int
init (xlator_t *this)
{
        int          ret = -1;
        clnt_conf_t *conf = NULL;

        /* */
        if (this->children) {
                gf_log (this->name, GF_LOG_ERROR,
                        "FATAL: client protocol translator cannot have any "
                        "subvolumes");
                goto out;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Volume is dangling. ");
        }

        conf = GF_CALLOC (1, sizeof (*conf), gf_client_mt_clnt_conf_t);
        if (!conf)
                goto out;

        pthread_mutex_init (&conf->lock, NULL);
        INIT_LIST_HEAD (&conf->saved_fds);

        ret = build_client_config (this, conf);
        if (ret)
                goto out;

        conf->rpc = rpc_clnt_init (&conf->rpc_conf, this->options, this->ctx,
                                   this->name);
        if (!conf->rpc)
                goto out;
        conf->rpc->xid = 42; /* It should be enough random everytime :O */
        ret = rpc_clnt_register_notify (conf->rpc, client_rpc_notify, this);
        if (ret)
                goto out;

        conf->handshake = &clnt_handshake_prog;
        conf->dump      = &clnt_dump_prog;
        this->private = conf;

        ret = 0;
out:
        if (ret)
                this->fini (this);

        return ret;
}

void
fini (xlator_t *this)
{
        clnt_conf_t *conf = NULL;

        conf = this->private;
        this->private = NULL;

        if (conf) {
                if (conf->rpc)
                        rpc_clnt_destroy (conf->rpc);

                /* Saved Fds */
                /* TODO: */

                pthread_mutex_destroy (&conf->lock);

                GF_FREE (conf);
        }
        return;
}

int
client_priv_dump (xlator_t *this)
{
        clnt_conf_t    *conf = NULL;
        int             ret   = -1;
        clnt_fd_ctx_t  *tmp = NULL;
        int             i = 0;
        char            key[GF_DUMP_MAX_BUF_LEN];
        char            key_prefix[GF_DUMP_MAX_BUF_LEN];

        if (!this)
                return -1;

        conf = this->private;
        if (!conf) {
                gf_log (this->name, GF_LOG_WARNING,
                        "conf null in xlator");
                return -1;
        }

        ret = pthread_mutex_trylock(&conf->lock);
        if (ret) {
                gf_log("", GF_LOG_WARNING, "Unable to lock client %s"
                       " errno: %d", this->name, errno);
                return -1;
        }

        gf_proc_dump_build_key(key_prefix, "xlator.protocol.client",
                               "%s.priv", this->name);

        gf_proc_dump_add_section(key_prefix);

        list_for_each_entry(tmp, &conf->saved_fds, sfd_pos) {
                gf_proc_dump_build_key(key, key_prefix,
                                       "fd.%d.remote_fd", ++i);
                gf_proc_dump_write(key, "%d", tmp->remote_fd);
        }

        gf_proc_dump_build_key(key, key_prefix, "connecting");
        gf_proc_dump_write(key, "%d", conf->connecting);
        gf_proc_dump_build_key(key, key_prefix, "last_sent");
        gf_proc_dump_write(key, "%s", ctime(&conf->last_sent.tv_sec));
        gf_proc_dump_build_key(key, key_prefix, "last_received");
        gf_proc_dump_write(key, "%s", ctime(&conf->last_received.tv_sec));

        pthread_mutex_unlock(&conf->lock);

        return 0;

}

int32_t
client_inodectx_dump (xlator_t *this, inode_t *inode)
{
        ino_t    par = 0;
        uint64_t gen = 0;
        int      ret = -1;
        char     key[GF_DUMP_MAX_BUF_LEN];

        if (!inode)
                return -1;

        if (!this)
                return -1;

        ret = inode_ctx_get2 (inode, this, &par, &gen);

        if (ret != 0)
                return ret;

        gf_proc_dump_build_key(key, "xlator.protocol.client",
                               "%s.inode.%ld.par",
                               this->name,inode->ino);
        gf_proc_dump_write(key, "%ld, %ld", par, gen);

        return 0;
}




struct xlator_cbks cbks = {
        .forget     = client_forget,
        .release    = client_release,
        .releasedir = client_releasedir
};

struct xlator_fops fops = {
        .stat        = client_stat,
        .readlink    = client_readlink,
        .mknod       = client_mknod,
        .mkdir       = client_mkdir,
        .unlink      = client_unlink,
        .rmdir       = client_rmdir,
        .symlink     = client_symlink,
        .rename      = client_rename,
        .link        = client_link,
        .truncate    = client_truncate,
        .open        = client_open,
        .readv       = client_readv,
        .writev      = client_writev,
        .statfs      = client_statfs,
        .flush       = client_flush,
        .fsync       = client_fsync,
        .setxattr    = client_setxattr,
        .getxattr    = client_getxattr,
        .fsetxattr   = client_fsetxattr,
        .fgetxattr   = client_fgetxattr,
        .removexattr = client_removexattr,
        .opendir     = client_opendir,
        .readdir     = client_readdir,
        .readdirp    = client_readdirp,
        .fsyncdir    = client_fsyncdir,
        .access      = client_access,
        .ftruncate   = client_ftruncate,
        .fstat       = client_fstat,
        .create      = client_create,
        .lk          = client_lk,
        .inodelk     = client_inodelk,
        .finodelk    = client_finodelk,
        .entrylk     = client_entrylk,
        .fentrylk    = client_fentrylk,
        .lookup      = client_lookup,
        .rchecksum   = client_rchecksum,
        .xattrop     = client_xattrop,
        .fxattrop    = client_fxattrop,
        .setattr     = client_setattr,
        .fsetattr    = client_fsetattr,
        .getspec     = client_getspec,
};


struct xlator_dumpops dumpops = {
        .priv      =  client_priv_dump,
        .inodectx  =  client_inodectx_dump,
};


struct volume_options options[] = {
        { .key   = {"username"},
          .type  = GF_OPTION_TYPE_ANY
        },
        { .key   = {"password"},
          .type  = GF_OPTION_TYPE_ANY
        },
        { .key   = {"transport-type"},
          .value = {"tcp", "socket", "ib-verbs", "unix", "ib-sdp",
                    "tcp/client", "ib-verbs/client"},
          .type  = GF_OPTION_TYPE_STR
        },
        { .key   = {"remote-host"},
          .type  = GF_OPTION_TYPE_INTERNET_ADDRESS
        },
        { .key   = {"remote-subvolume"},
          .type  = GF_OPTION_TYPE_ANY
        },
        { .key   = {"frame-timeout",
                    "rpc-timeout" },
          .type  = GF_OPTION_TYPE_TIME,
          .min   = 0,
          .max   = 86400,
        },
        { .key   = {"ping-timeout"},
          .type  = GF_OPTION_TYPE_TIME,
          .min   = 1,
          .max   = 1013,
        },
        { .key   = {NULL} },
};
