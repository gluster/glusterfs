/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
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
extern struct rpcclnt_cb_program gluster_cbk_prog;

int client_handshake (xlator_t *this, struct rpc_clnt *rpc);
void client_start_ping (void *data);
int client_init_rpc (xlator_t *this);
int client_destroy_rpc (xlator_t *this);
int client_mark_fd_bad (xlator_t *this);

int32_t
client_type_to_gf_type (short l_type)
{
        int32_t  gf_type;

        switch (l_type) {
        case F_RDLCK:
                gf_type = GF_LK_F_RDLCK;
                break;
        case F_WRLCK:
                gf_type = GF_LK_F_WRLCK;
                break;
        case F_UNLCK:
                gf_type = GF_LK_F_UNLCK;
                break;
        }

        return gf_type;
}

uint32_t
client_get_lk_ver (clnt_conf_t *conf)
{
        uint32_t  lk_ver = 0;

        GF_VALIDATE_OR_GOTO ("client", conf, out);

        pthread_mutex_lock (&conf->lock);
        {
                lk_ver = conf->lk_version;
        }
        pthread_mutex_unlock (&conf->lock);
out:
        return lk_ver;
}

void
client_grace_timeout (void *data)
{
        int               ver  = 0;
        xlator_t         *this = NULL;
        struct clnt_conf *conf = NULL;
        struct rpc_clnt  *rpc  = NULL;

        GF_VALIDATE_OR_GOTO ("client", data, out);

        this = THIS;

        rpc = (struct rpc_clnt *) data;

        conf = (struct clnt_conf *) this->private;

        pthread_mutex_lock (&conf->lock);
        {
                ver = ++conf->lk_version;
                /* ver == 0 is a special value used by server
                   to notify client that this is a fresh connect.*/
                if (ver == 0)
                        ver = ++conf->lk_version;

                gf_timer_call_cancel (this->ctx, conf->grace_timer);
                conf->grace_timer = NULL;
        }
        pthread_mutex_unlock (&conf->lock);

        gf_log (this->name, GF_LOG_WARNING,
                "client grace timer expired, updating "
                "the lk-version to %d", ver);

        client_mark_fd_bad (this);
out:
        return;
}

int
client_submit_request (xlator_t *this, void *req, call_frame_t *frame,
                       rpc_clnt_prog_t *prog, int procnum, fop_cbk_fn_t cbk,
                       struct iobref *iobref,  struct iovec *rsphdr,
                       int rsphdr_count, struct iovec *rsp_payload,
                       int rsp_payload_count, struct iobref *rsp_iobref,
                       xdrproc_t xdrproc)
{
        int            ret         = -1;
        clnt_conf_t   *conf        = NULL;
        struct iovec   iov         = {0, };
        struct iobuf  *iobuf       = NULL;
        int            count       = 0;
        char           start_ping  = 0;
        struct iobref *new_iobref  = NULL;
        ssize_t        xdr_size    = 0;

        GF_VALIDATE_OR_GOTO ("client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, prog, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);

        conf = this->private;

        /* If 'setvolume' is not successful, we should not send frames to
           server, mean time we should be able to send 'DUMP' and 'SETVOLUME'
           call itself even if its not connected */
        if (!(conf->connected ||
              ((prog->prognum == GLUSTER_DUMP_PROGRAM) ||
               (prog->prognum == GLUSTER_PMAP_PROGRAM) ||
               ((prog->prognum == GLUSTER_HNDSK_PROGRAM) &&
                (procnum == GF_HNDSK_SETVOLUME))))) {
                /* This particular error captured/logged in
                   functions calling this */
                gf_log (this->name, GF_LOG_DEBUG,
                        "connection in disconnected state");
                goto out;
       }

        if (req && xdrproc) {
                xdr_size = xdr_sizeof (xdrproc, req);
                iobuf = iobuf_get2 (this->ctx->iobuf_pool, xdr_size);
                if (!iobuf) {
                        goto out;
                };

                new_iobref = iobref_new ();
                if (!new_iobref) {
                        goto out;
                }

                if (iobref != NULL) {
                        ret = iobref_merge (new_iobref, iobref);
                        if (ret != 0) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "cannot merge iobref passed from caller "
                                        "into new_iobref");
                        }
                }

                ret = iobref_add (new_iobref, iobuf);
                if (ret != 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "cannot add iobuf into iobref");
                        goto out;
                }

                iov.iov_base = iobuf->ptr;
                iov.iov_len  = iobuf_size (iobuf);

                /* Create the xdr payload */
                ret = xdr_serialize_generic (iov, req, xdrproc);
                if (ret == -1) {
                        /* callingfn so that, we can get to know which xdr
                           function was called */
                        gf_log_callingfn (this->name, GF_LOG_WARNING,
                                          "XDR payload creation failed");
                        goto out;
                }
                iov.iov_len = ret;
                count = 1;
        }

        /* Send the msg */
        ret = rpc_clnt_submit (conf->rpc, prog, procnum, cbk, &iov, count, NULL,
                               0, new_iobref, frame, rsphdr, rsphdr_count,
                               rsp_payload, rsp_payload_count, rsp_iobref);

        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG, "rpc_clnt_submit failed");
        }

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
        if (new_iobref != NULL)
                iobref_unref (new_iobref);

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
        if (!conf || !conf->fops)
                goto out;

        args.fd = fd;

        proc = &conf->fops->proctable[GF_FOP_RELEASEDIR];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_RELEASEDIR]);
                goto out;
        }
        if (proc->fn) {
                frame = create_frame (this, this->ctx->pool);
                if (!frame) {
                        goto out;
                }
                ret = proc->fn (frame, this, &args);
        }
out:
        if (ret)
                gf_log (this->name, GF_LOG_WARNING,
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
        if (!conf || !conf->fops)
                goto out;

        args.fd = fd;
        proc = &conf->fops->proctable[GF_FOP_RELEASE];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_RELEASE]);
                goto out;
        }
        if (proc->fn) {
                frame = create_frame (this, this->ctx->pool);
                if (!frame) {
                        goto out;
                }
                ret = proc->fn (frame, this, &args);
        }
out:
        if (ret)
                gf_log (this->name, GF_LOG_WARNING,
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
        if (!conf || !conf->fops)
                goto out;

        args.loc = loc;
        args.dict = xattr_req;

        proc = &conf->fops->proctable[GF_FOP_LOOKUP];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_LOOKUP]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.loc = loc;

        proc = &conf->fops->proctable[GF_FOP_STAT];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_STAT]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.loc    = loc;
        args.offset = offset;

        proc = &conf->fops->proctable[GF_FOP_TRUNCATE];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_TRUNCATE]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.fd     = fd;
        args.offset = offset;

        proc = &conf->fops->proctable[GF_FOP_FTRUNCATE];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_FTRUNCATE]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.loc  = loc;
        args.mask = mask;

        proc = &conf->fops->proctable[GF_FOP_ACCESS];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_ACCESS]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.loc  = loc;
        args.size = size;

        proc = &conf->fops->proctable[GF_FOP_READLINK];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_READLINK]);
                goto out;
        }
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (readlink, frame, -1, ENOTCONN, NULL, NULL);

	return 0;
}


int
client_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
              dev_t rdev, dict_t *params)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf || !conf->fops)
                goto out;

        args.loc  = loc;
        args.mode = mode;
        args.rdev = rdev;
        args.dict = params;

        proc = &conf->fops->proctable[GF_FOP_MKNOD];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_MKNOD]);
                goto out;
        }
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (mknod, frame, -1, ENOTCONN,
                                     NULL, NULL, NULL, NULL);

	return 0;
}


int
client_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc,
              mode_t mode, dict_t *params)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf || !conf->fops)
                goto out;

        args.loc  = loc;
        args.mode = mode;
        args.dict = params;

        proc = &conf->fops->proctable[GF_FOP_MKDIR];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_MKDIR]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.loc = loc;

        proc = &conf->fops->proctable[GF_FOP_UNLINK];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_UNLINK]);
                goto out;
        }
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (unlink, frame, -1, ENOTCONN,
                                     NULL, NULL);

	return 0;
}

int32_t
client_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf || !conf->fops)
                goto out;

        args.loc = loc;
        args.flags = flags;

        proc = &conf->fops->proctable[GF_FOP_RMDIR];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_RMDIR]);
                goto out;
        }
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        /* think of avoiding a missing frame */
        if (ret)
                STACK_UNWIND_STRICT (rmdir, frame, -1, ENOTCONN,
                                     NULL, NULL);

	return 0;
}


int
client_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
                loc_t *loc, dict_t *params)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf || !conf->fops)
                goto out;

        args.linkname = linkpath;
        args.loc      = loc;
        args.dict     = params;

        proc = &conf->fops->proctable[GF_FOP_SYMLINK];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_SYMLINK]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.oldloc = oldloc;
        args.newloc = newloc;
        proc = &conf->fops->proctable[GF_FOP_RENAME];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_RENAME]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.oldloc = oldloc;
        args.newloc = newloc;

        proc = &conf->fops->proctable[GF_FOP_LINK];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_LINK]);
                goto out;
        }
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
               int32_t flags, mode_t mode, fd_t *fd, dict_t *params)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf || !conf->fops)
                goto out;

        args.loc = loc;
        args.flags = flags;
        args.mode = mode;
        args.fd = fd;
        args.dict = params;

        proc = &conf->fops->proctable[GF_FOP_CREATE];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_CREATE]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.loc = loc;
        args.flags = flags;
        args.fd = fd;
        args.wbflags = wbflags;

        proc = &conf->fops->proctable[GF_FOP_OPEN];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_OPEN]);
                goto out;
        }
        if (proc->fn)
                ret = proc->fn (frame, this, &args);

out:
        if (ret)
                STACK_UNWIND_STRICT (open, frame, -1, ENOTCONN, NULL);

	return 0;
}



int32_t
client_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
              off_t offset, uint32_t flags)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf || !conf->fops)
                goto out;

        args.fd     = fd;
        args.size   = size;
        args.offset = offset;
        args.flags  = flags;

        proc = &conf->fops->proctable[GF_FOP_READ];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_READ]);
                goto out;
        }
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
               uint32_t flags, struct iobref *iobref)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf || !conf->fops)
                goto out;

        args.fd     = fd;
        args.vector = vector;
        args.count  = count;
        args.offset = off;
        args.flags  = flags;
        args.iobref = iobref;

        proc = &conf->fops->proctable[GF_FOP_WRITE];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_WRITE]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.fd = fd;

        proc = &conf->fops->proctable[GF_FOP_FLUSH];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_FLUSH]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.fd    = fd;
        args.flags = flags;

        proc = &conf->fops->proctable[GF_FOP_FSYNC];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_FSYNC]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.fd = fd;

        proc = &conf->fops->proctable[GF_FOP_FSTAT];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_FSTAT]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.loc = loc;
        args.fd  = fd;

        proc = &conf->fops->proctable[GF_FOP_OPENDIR];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_OPENDIR]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.fd    = fd;
        args.flags = flags;

        proc = &conf->fops->proctable[GF_FOP_FSYNCDIR];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_FSYNCDIR]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.loc = loc;

        proc = &conf->fops->proctable[GF_FOP_STATFS];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_STATFS]);
                goto out;
        }
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (statfs, frame, -1, ENOTCONN, NULL);

	return 0;
}

static gf_boolean_t
is_client_rpc_init_command (dict_t *dict, xlator_t *this,
                            char **value)
{
        gf_boolean_t ret      = _gf_false;
        int          dict_ret = -1;

        if (!strstr (this->name, "replace-brick")) {
                gf_log (this->name, GF_LOG_TRACE, "name is !replace-brick");
                goto out;
        }
        dict_ret = dict_get_str (dict, CLIENT_CMD_CONNECT, value);
        if (dict_ret) {
                gf_log (this->name, GF_LOG_TRACE, "key %s not present",
                        CLIENT_CMD_CONNECT);
                goto out;
        }

        ret = _gf_true;

out:
        return ret;

}

static gf_boolean_t
is_client_rpc_destroy_command (dict_t *dict, xlator_t *this)
{
        gf_boolean_t ret      = _gf_false;
        int          dict_ret = -1;
        char        *dummy    = NULL;

        if (strncmp (this->name, "replace-brick", 13)) {
                gf_log (this->name, GF_LOG_TRACE, "name is !replace-brick");
                goto out;
        }

        dict_ret = dict_get_str (dict, CLIENT_CMD_DISCONNECT, &dummy);
        if (dict_ret) {
                gf_log (this->name, GF_LOG_TRACE, "key %s not present",
                        CLIENT_CMD_DISCONNECT);
                goto out;
        }

        ret = _gf_true;

out:
        return ret;

}

static gf_boolean_t
client_set_remote_options (char *value, xlator_t *this)
{
        char         *dup_value       = NULL;
        char         *host            = NULL;
        char         *subvol          = NULL;
        char         *host_dup        = NULL;
        char         *subvol_dup      = NULL;
        char         *remote_port_str = NULL;
        char         *tmp             = NULL;
        int           remote_port     = 0;
        gf_boolean_t  ret             = _gf_false;

        dup_value = gf_strdup (value);
        host = strtok_r (dup_value, ":", &tmp);
        subvol = strtok_r (NULL, ":", &tmp);
        remote_port_str = strtok_r (NULL, ":", &tmp);

        if (!subvol) {
                gf_log (this->name, GF_LOG_WARNING,
                        "proper value not passed as subvolume");
                goto out;
        }

        host_dup = gf_strdup (host);
        if (!host_dup) {
                goto out;
        }

        ret = dict_set_dynstr (this->options, "remote-host", host_dup);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to set remote-host with %s", host);
                goto out;
        }

        subvol_dup = gf_strdup (subvol);
        if (!subvol_dup) {
                goto out;
        }

        ret = dict_set_dynstr (this->options, "remote-subvolume", subvol_dup);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to set remote-host with %s", host);
                goto out;
        }

        remote_port = atoi (remote_port_str);
        GF_ASSERT (remote_port);

        ret = dict_set_int32 (this->options, "remote-port",
                              remote_port);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to set remote-port to %d", remote_port);
                goto out;
        }

        ret = _gf_true;
out:
        if (dup_value)
                GF_FREE (dup_value);

        return ret;
}


int32_t
client_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
                 int32_t flags)
{
        int                   ret         = -1;
        int                   op_ret      = -1;
        int                   op_errno    = ENOTCONN;
        int                   need_unwind = 0;
        clnt_conf_t          *conf        = NULL;
        rpc_clnt_procedure_t *proc        = NULL;
        clnt_args_t           args        = {0,};
        char                 *value       = NULL;


        if (is_client_rpc_init_command (dict, this, &value) == _gf_true) {
                GF_ASSERT (value);
                gf_log (this->name, GF_LOG_INFO, "client rpc init command");
                ret = client_set_remote_options (value, this);
                if (ret) {
                        (void) client_destroy_rpc (this);
                        ret = client_init_rpc (this);
                }

                if (!ret) {
                        op_ret      = 0;
                        op_errno    = 0;
                }
                need_unwind = 1;
                goto out;
        }

        if (is_client_rpc_destroy_command (dict, this) == _gf_true) {
                gf_log (this->name, GF_LOG_INFO, "client rpc destroy command");
                ret = client_destroy_rpc (this);
                if (ret) {
                        op_ret      = 0;
                        op_errno    = 0;
                }
                need_unwind = 1;
                goto out;
        }

        conf = this->private;
        if (!conf || !conf->fops) {
                op_errno = ENOTCONN;
                need_unwind = 1;
                goto out;
        }

        args.loc   = loc;
        args.dict  = dict;
        args.flags = flags;

        proc = &conf->fops->proctable[GF_FOP_SETXATTR];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_SETXATTR]);
                goto out;
        }
        if (proc->fn) {
                ret = proc->fn (frame, this, &args);
                if (ret) {
                        need_unwind = 1;
                }
        }
out:
        if (need_unwind)
                STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno);

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
        if (!conf || !conf->fops)
                goto out;

        args.fd = fd;
        args.dict = dict;
        args.flags = flags;

        proc = &conf->fops->proctable[GF_FOP_FSETXATTR];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_FSETXATTR]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.fd = fd;
        args.name = name;

        proc = &conf->fops->proctable[GF_FOP_FGETXATTR];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_FGETXATTR]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.name = name;
        args.loc  = loc;

        proc = &conf->fops->proctable[GF_FOP_GETXATTR];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_GETXATTR]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.loc = loc;
        args.flags = flags;
        args.dict = dict;

        proc = &conf->fops->proctable[GF_FOP_XATTROP];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_XATTROP]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.fd = fd;
        args.flags = flags;
        args.dict = dict;

        proc = &conf->fops->proctable[GF_FOP_FXATTROP];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_FXATTROP]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.name = name;
        args.loc  = loc;

        proc = &conf->fops->proctable[GF_FOP_REMOVEXATTR];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_REMOVEXATTR]);
                goto out;
        }
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (removexattr, frame, -1, ENOTCONN);

	return 0;
}

int32_t
client_fremovexattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                     const char *name)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf || !conf->fops)
                goto out;

        args.name = name;
        args.fd  = fd;

        proc = &conf->fops->proctable[GF_FOP_FREMOVEXATTR];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_FREMOVEXATTR]);
                goto out;
        }
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (fremovexattr, frame, -1, ENOTCONN);

	return 0;
}

int32_t
client_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
           struct gf_flock *lock)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf || !conf->fops)
                goto out;

        args.fd    = fd;
        args.cmd   = cmd;
        args.flock = lock;

        proc = &conf->fops->proctable[GF_FOP_LK];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_LK]);
                goto out;
        }
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (lk, frame, -1, ENOTCONN, NULL);

	return 0;
}


int32_t
client_inodelk (call_frame_t *frame, xlator_t *this, const char *volume,
                loc_t *loc, int32_t cmd, struct gf_flock *lock)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf || !conf->fops)
                goto out;

        args.loc    = loc;
        args.cmd    = cmd;
        args.flock  = lock;
        args.volume = volume;

        proc = &conf->fops->proctable[GF_FOP_INODELK];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_INODELK]);
                goto out;
        }
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (inodelk, frame, -1, ENOTCONN);

	return 0;
}



int32_t
client_finodelk (call_frame_t *frame, xlator_t *this, const char *volume,
                 fd_t *fd, int32_t cmd, struct gf_flock *lock)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf || !conf->fops)
                goto out;

        args.fd     = fd;
        args.cmd    = cmd;
        args.flock  = lock;
        args.volume = volume;

        proc = &conf->fops->proctable[GF_FOP_FINODELK];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_FINODELK]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.loc          = loc;
        args.basename     = basename;
        args.type         = type;
        args.volume       = volume;
        args.cmd_entrylk  = cmd;

        proc = &conf->fops->proctable[GF_FOP_ENTRYLK];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_ENTRYLK]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.fd           = fd;
        args.basename     = basename;
        args.type         = type;
        args.volume       = volume;
        args.cmd_entrylk  = cmd;

        proc = &conf->fops->proctable[GF_FOP_FENTRYLK];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_FENTRYLK]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.fd = fd;
        args.offset = offset;
        args.len = len;

        proc = &conf->fops->proctable[GF_FOP_RCHECKSUM];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_RCHECKSUM]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.fd = fd;
        args.size = size;
        args.offset = off;

        proc = &conf->fops->proctable[GF_FOP_READDIR];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_READDIR]);
                goto out;
        }
        if (proc->fn)
                ret = proc->fn (frame, this, &args);
out:
        if (ret)
                STACK_UNWIND_STRICT (readdir, frame, -1, ENOTCONN, NULL);

	return 0;
}


int32_t
client_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 size_t size, off_t off, dict_t *dict)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;
        rpc_clnt_procedure_t *proc = NULL;
        clnt_args_t  args = {0,};

        conf = this->private;
        if (!conf || !conf->fops)
                goto out;

        args.fd = fd;
        args.size = size;
        args.offset = off;
        args.xattr_req = dict;

        proc = &conf->fops->proctable[GF_FOP_READDIRP];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_READDIRP]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.loc = loc;
        args.stbuf = stbuf;
        args.valid = valid;

        proc = &conf->fops->proctable[GF_FOP_SETATTR];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_SETATTR]);
                goto out;
        }
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
        if (!conf || !conf->fops)
                goto out;

        args.fd = fd;
        args.stbuf = stbuf;
        args.valid = valid;

        proc = &conf->fops->proctable[GF_FOP_FSETATTR];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_FSETATTR]);
                goto out;
        }
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
        if (!conf || !conf->fops || !conf->handshake)
                goto out;

        args.name = key;
        args.flags = flags;

        /* For all other xlators, getspec is an fop, hence its in fops table */
        proc = &conf->fops->proctable[GF_FOP_GETSPEC];
        if (!proc) {
                gf_log (this->name, GF_LOG_ERROR,
                        "rpc procedure not found for %s",
                        gf_fop_list[GF_FOP_GETSPEC]);
                goto out;
        }
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
        if (!this || !this->private) {
                gf_log ("client", GF_LOG_ERROR,
                        (this != NULL) ?
                        "private structure of the xlator is NULL":
                        "xlator is NULL");
                goto out;
        }

        conf = this->private;

        switch (event) {
        case RPC_CLNT_CONNECT:
        {
                conf->connected = 1;
                // connect happened, send 'get_supported_versions' mop
                ret = dict_get_str (this->options, "disable-handshake",
                                    &handshake);

                gf_log (this->name, GF_LOG_DEBUG, "got RPC_CLNT_CONNECT");

                if ((ret < 0) || (strcasecmp (handshake, "on"))) {
                        ret = client_handshake (this, conf->rpc);
                        if (ret)
                                gf_log (this->name, GF_LOG_WARNING,
                                        "handshake msg returned %d", ret);
                } else {
                        //conf->rpc->connected = 1;
                        if (conf->last_sent_event != GF_EVENT_CHILD_UP) {
                                ret = default_notify (this, GF_EVENT_CHILD_UP,
                                                      NULL);
                                if (ret)
                                        gf_log (this->name, GF_LOG_INFO,
                                                "CHILD_UP notify failed");
                                conf->last_sent_event = GF_EVENT_CHILD_UP;
                        }
                }

                /* Cancel grace timer if set */
                pthread_mutex_lock (&conf->lock);
                {
                        if (conf->grace_timer) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "Cancelling the grace timer");

                                gf_timer_call_cancel (this->ctx,
                                                      conf->grace_timer);
                                conf->grace_timer = NULL;
                        }
                }
                pthread_mutex_unlock (&conf->lock);

                break;
        }
        case RPC_CLNT_DISCONNECT:
                /* client_mark_fd_bad (this); */

                pthread_mutex_lock (&conf->lock);
                {
                        if (conf->grace_timer) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "Client grace timer is already set");
                        } else {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "Registering a grace timer");
                                conf->grace_timer =
                                        gf_timer_call_after (this->ctx,
                                                             conf->grace_tv,
                                                             client_grace_timeout,
                                                             conf->rpc);
                        }
                }
                pthread_mutex_unlock (&conf->lock);

                if (!conf->skip_notify) {
                        if (conf->connected)
                                gf_log (this->name, GF_LOG_INFO,
                                        "disconnected");

                        /* If the CHILD_DOWN event goes to parent xlator
                           multiple times, the logic of parent xlator notify
                           may get screwed up.. (eg. CHILD_MODIFIED event in
                           replicate), hence make sure events which are passed
                           to parent are genuine */
                        if (conf->last_sent_event != GF_EVENT_CHILD_DOWN) {
                                ret = default_notify (this, GF_EVENT_CHILD_DOWN,
                                                      NULL);
                                if (ret)
                                        gf_log (this->name, GF_LOG_INFO,
                                                "CHILD_DOWN notify failed");
                                conf->last_sent_event = GF_EVENT_CHILD_DOWN;
                        }
                } else {
                        if (conf->connected)
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "disconnected (skipped notify)");
                }

                conf->connected = 0;
                conf->skip_notify = 0;

                break;

        default:
                gf_log (this->name, GF_LOG_TRACE,
                        "got some other RPC event %d", event);

                break;
        }

out:
        return 0;
}


int
notify (xlator_t *this, int32_t event, void *data, ...)
{
        clnt_conf_t     *conf  = NULL;

        conf = this->private;
        if (!conf)
                return 0;

        switch (event) {
        case GF_EVENT_PARENT_UP:
        {
                gf_log (this->name, GF_LOG_INFO,
                        "parent translators are ready, attempting connect "
                        "on transport");

                rpc_clnt_start (conf->rpc);
                break;
        }

        case GF_EVENT_PARENT_DOWN:
                gf_log (this->name, GF_LOG_INFO,
                        "current graph is no longer active, destroying "
                        "rpc_client ");

                pthread_mutex_lock (&conf->lock);
                {
                        conf->parent_down = 1;
                }
                pthread_mutex_unlock (&conf->lock);

                rpc_clnt_disable (conf->rpc);
                break;

        default:
                gf_log (this->name, GF_LOG_DEBUG,
                        "got %d, calling default_notify ()", event);

                default_notify (this, event, data);
                conf->last_sent_event = event;
                break;
        }

        return 0;
}

int
build_client_config (xlator_t *this, clnt_conf_t *conf)
{
        int                     ret = -1;

        if (!conf)
                goto out;

        GF_OPTION_INIT ("frame-timeout", conf->rpc_conf.rpc_timeout,
                        int32, out);

        GF_OPTION_INIT ("remote-port", conf->rpc_conf.remote_port,
                        int32, out);

        GF_OPTION_INIT ("ping-timeout", conf->opt.ping_timeout,
                        int32, out);

        GF_OPTION_INIT ("remote-subvolume", conf->opt.remote_subvolume,
                        path, out);
        if (!conf->opt.remote_subvolume)
                gf_log (this->name, GF_LOG_WARNING,
                        "option 'remote-subvolume' not given");

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
client_destroy_rpc (xlator_t *this)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;

        conf = this->private;
        if (!conf)
                goto out;

        if (conf->rpc) {
                conf->rpc = rpc_clnt_unref (conf->rpc);
                ret = 0;
                gf_log (this->name, GF_LOG_DEBUG,
                        "Client rpc conn destroyed");
                goto out;
        }

        gf_log (this->name, GF_LOG_WARNING,
                "RPC destroy called on already destroyed "
                "connection");

out:
        return ret;
}

int
client_init_rpc (xlator_t *this)
{
        int          ret  = -1;
        clnt_conf_t *conf = NULL;

        conf = this->private;

        if (conf->rpc) {
                gf_log (this->name, GF_LOG_WARNING,
                        "client rpc already init'ed");
                ret = -1;
                goto out;
        }

        conf->rpc = rpc_clnt_new (this->options, this->ctx, this->name);
        if (!conf->rpc) {
                gf_log (this->name, GF_LOG_ERROR, "failed to initialize RPC");
                goto out;
        }

        ret = rpc_clnt_register_notify (conf->rpc, client_rpc_notify, this);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to register notify");
                goto out;
        }

        conf->handshake = &clnt_handshake_prog;
        conf->dump      = &clnt_dump_prog;

        ret = rpcclnt_cbk_program_register (conf->rpc, &gluster_cbk_prog);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to register callback program");
                goto out;
        }

        ret = 0;

        gf_log (this->name, GF_LOG_DEBUG, "client init successful");
out:
        return ret;
}


int
client_init_grace_timer (xlator_t *this, dict_t *options,
                         clnt_conf_t *conf)
{
        char     *lk_heal        = NULL;
        int32_t   ret            = -1;
        int32_t   grace_timeout  = -1;

        GF_VALIDATE_OR_GOTO ("client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, options, out);
        GF_VALIDATE_OR_GOTO (this->name, conf, out);

        conf->lk_heal = _gf_true;

        ret = dict_get_str (options, "lk-heal", &lk_heal);
        if (!ret)
                gf_string2boolean (lk_heal, &conf->lk_heal);

        ret = dict_get_int32 (options, "grace-timeout", &grace_timeout);
        if (!ret)
                conf->grace_tv.tv_sec = grace_timeout;
        else
                conf->grace_tv.tv_sec = 10;

        conf->grace_tv.tv_usec  = 0;

        gf_log (this->name, GF_LOG_INFO, "lk-heal = %s",
                (conf->lk_heal) ? "on" : "off");

        ret = 0;
out:
        return ret;
}

int
reconfigure (xlator_t *this, dict_t *options)
{
	clnt_conf_t *conf              = NULL;
	int          ret               = -1;
        int          subvol_ret        = 0;
        char        *old_remote_subvol = NULL;
        char        *new_remote_subvol = NULL;
        char        *old_remote_host   = NULL;
        char        *new_remote_host   = NULL;

	conf = this->private;

        GF_OPTION_RECONF ("frame-timeout", conf->rpc_conf.rpc_timeout,
                          options, int32, out);

        GF_OPTION_RECONF ("ping-timeout", conf->opt.ping_timeout,
                          options, int32, out);

        subvol_ret = dict_get_str (this->options, "remote-host",
                                   &old_remote_host);

        if (subvol_ret == 0) {
                subvol_ret = dict_get_str (options, "remote-host",
                                           &new_remote_host);
                if (subvol_ret == 0) {
                        if (strcmp (old_remote_host, new_remote_host)) {
                                ret = 1;
                                goto out;
                        }
                }
        }

        subvol_ret = dict_get_str (this->options, "remote-subvolume",
                                   &old_remote_subvol);

        if (subvol_ret == 0) {
                subvol_ret = dict_get_str (options, "remote-subvolume",
                                           &new_remote_subvol);
                if (subvol_ret == 0) {
                        if (strcmp (old_remote_subvol, new_remote_subvol)) {
                                ret = 1;
                                goto out;
                        }
                }
        }

        ret = client_init_grace_timer (this, options, conf);
        if (ret)
                goto out;

        ret = 0;
out:
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

        /* Initialize parameters for lock self healing*/
        conf->lk_version  = 1;
        conf->grace_timer = NULL;

        ret = client_init_grace_timer (this, this->options, conf);
        if (ret)
                goto out;

        LOCK_INIT (&conf->rec_lock);

        conf->last_sent_event = -1; /* To start with we don't have any events */

        this->private = conf;

        /* If it returns -1, then its a failure, if it returns +1 we need
           have to understand that 'this' is subvolume of a xlator which,
           will set the remote host and remote subvolume in a setxattr
           call.
        */

        ret = build_client_config (this, conf);
        if (ret == -1)
                goto out;

        if (ret) {
                ret = 0;
                goto out;
        }

        this->local_pool = mem_pool_new (clnt_local_t, 1024);
        if (!this->local_pool) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to create local_t's memory pool");
                goto out;
        }

        ret = client_init_rpc (this);
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
                       rpc_clnt_unref (conf->rpc);

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
                gf_log(this->name, GF_LOG_WARNING, "Unable to lock client %s"
                       " errno: %d", this->name, errno);
                return -1;
        }

        gf_proc_dump_build_key(key_prefix, "xlator.protocol.client",
                               "%s.priv", this->name);

        gf_proc_dump_add_section(key_prefix);

        list_for_each_entry(tmp, &conf->saved_fds, sfd_pos) {
                sprintf (key, "fd.%d.remote_fd", ++i);
                gf_proc_dump_write(key, "%d", tmp->remote_fd);
        }

        gf_proc_dump_write("connecting", "%d", conf->connecting);
        gf_proc_dump_write("last_sent", "%s", ctime(&conf->last_sent.tv_sec));
        gf_proc_dump_write("last_received", "%s", ctime(&conf->last_received.tv_sec));

        if (conf->rpc) {
                gf_proc_dump_write("total_bytes_read", "%"PRIu64,
                                   conf->rpc->conn.trans->total_bytes_read);

                gf_proc_dump_write("total_bytes_written", "%"PRIu64,
                                   conf->rpc->conn.trans->total_bytes_write);
        }
        pthread_mutex_unlock(&conf->lock);

        return 0;

}

int32_t
client_inodectx_dump (xlator_t *this, inode_t *inode)
{
        if (!inode)
                return -1;

        if (!this)
                return -1;

        /*TODO*/

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
        .fremovexattr = client_fremovexattr,
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
                    "tcp/client", "ib-verbs/client", "rdma"},
          .type  = GF_OPTION_TYPE_STR
        },
        { .key   = {"remote-host"},
          .type  = GF_OPTION_TYPE_INTERNET_ADDRESS
        },
        { .key   = {"remote-port"},
          .type  = GF_OPTION_TYPE_INT,
        },
        { .key   = {"remote-subvolume"},
          .type  = GF_OPTION_TYPE_ANY
        },
        { .key   = {"frame-timeout",
                    "rpc-timeout" },
          .type  = GF_OPTION_TYPE_TIME,
          .min   = 0,
          .max   = 86400,
          .default_value = "1800",
          .description = "Time frame after which the (file) operation would be "
                         "declared as dead, if the server does not respond for "
                         "a particular (file) operation."
        },
        { .key   = {"ping-timeout"},
          .type  = GF_OPTION_TYPE_TIME,
          .min   = 1,
          .max   = 1013,
          .default_value = "42",
          .description = "Time duration for which the client waits to "
                         "check if the server is responsive."
        },
        { .key   = {"client-bind-insecure"},
          .type  = GF_OPTION_TYPE_BOOL
        },
        { .key   = {"lk-heal"},
          .type  = GF_OPTION_TYPE_STR
        },
        { .key   = {"grace-timeout"},
          .type  = GF_OPTION_TYPE_INT
        },
        { .key   = {NULL} },
};
