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


/*
 * This file contains functions to support dumping of
 * latencies of FOPs broken down by subvolumes.
 */

#include "glusterfs.h"
#include "stack.h"
#include "xlator.h"
#include "common-utils.h"
#include "statedump.h"


void
gf_set_fop_from_fn_pointer (call_frame_t *frame, struct xlator_fops *fops, void *fn)
{
        glusterfs_fop_t fop = -1;

        if (fops->stat == fn)
                fop = GF_FOP_STAT;
        else if (fops->readlink == fn)
                fop = GF_FOP_READLINK;
        else if (fops->mknod == fn)
                fop = GF_FOP_MKNOD;
        else if (fops->mkdir == fn)
                fop = GF_FOP_MKDIR;
        else if (fops->unlink == fn)
                fop = GF_FOP_UNLINK;
        else if (fops->rmdir == fn)
                fop = GF_FOP_RMDIR;
        else if (fops->symlink == fn)
                fop = GF_FOP_SYMLINK;
        else if (fops->rename == fn)
                fop = GF_FOP_RENAME;
        else if (fops->link == fn)
                fop = GF_FOP_LINK;
        else if (fops->truncate == fn)
                fop = GF_FOP_TRUNCATE;
        else if (fops->open == fn)
                fop = GF_FOP_OPEN;
        else if (fops->readv == fn)
                fop = GF_FOP_READ;
        else if (fops->writev == fn)
                fop = GF_FOP_WRITE;
        else if (fops->statfs == fn)
                fop = GF_FOP_STATFS;
        else if (fops->flush == fn)
                fop = GF_FOP_FLUSH;
        else if (fops->fsync == fn)
                fop = GF_FOP_FSYNC;
        else if (fops->setxattr == fn)
                fop = GF_FOP_SETXATTR;
        else if (fops->getxattr == fn)
                fop = GF_FOP_GETXATTR;
        else if (fops->removexattr == fn)
                fop = GF_FOP_REMOVEXATTR;
        else if (fops->opendir == fn)
                fop = GF_FOP_OPENDIR;
        else if (fops->fsyncdir == fn)
                fop = GF_FOP_FSYNCDIR;
        else if (fops->access == fn)
                fop = GF_FOP_ACCESS;
        else if (fops->create == fn)
                fop = GF_FOP_CREATE;
        else if (fops->ftruncate == fn)
                fop = GF_FOP_FTRUNCATE;
        else if (fops->fstat == fn)
                fop = GF_FOP_FSTAT;
        else if (fops->lk == fn)
                fop = GF_FOP_LK;
        else if (fops->lookup == fn)
                fop = GF_FOP_LOOKUP;
        else if (fops->readdir == fn)
                fop = GF_FOP_READDIR;
        else if (fops->inodelk == fn)
                fop = GF_FOP_INODELK;
        else if (fops->finodelk == fn)
                fop = GF_FOP_FINODELK;
        else if (fops->entrylk == fn)
                fop = GF_FOP_ENTRYLK;
        else if (fops->fentrylk == fn)
                fop = GF_FOP_FENTRYLK;
        else if (fops->xattrop == fn)
                fop = GF_FOP_XATTROP;
        else if (fops->fxattrop == fn)
                fop = GF_FOP_FXATTROP;
        else if (fops->fgetxattr == fn)
                fop = GF_FOP_FGETXATTR;
        else if (fops->fsetxattr == fn)
                fop = GF_FOP_FSETXATTR;
        else if (fops->rchecksum == fn)
                fop = GF_FOP_RCHECKSUM;
        else if (fops->setattr == fn)
                fop = GF_FOP_SETATTR;
        else if (fops->fsetattr == fn)
                fop = GF_FOP_FSETATTR;
        else if (fops->readdirp == fn)
                fop = GF_FOP_READDIRP;
        else if (fops->getspec == fn)
                fop = GF_FOP_GETSPEC;
        else
                fop = -1;

        frame->op   = fop;
}


void
gf_update_latency (call_frame_t *frame)
{
        double elapsed;
        struct timeval *begin, *end;

        fop_latency_t *lat;

        begin = &frame->begin;
        end   = &frame->end;

        elapsed = (end->tv_sec - begin->tv_sec) * 1e6
                + (end->tv_usec - begin->tv_usec);

        lat = &frame->this->latencies[frame->op];

        lat->total += elapsed;
        lat->count++;
        lat->mean = lat->mean + (elapsed - lat->mean) / lat->count;
}


void
gf_proc_dump_latency_info (xlator_t *xl)
{
        char key_prefix[GF_DUMP_MAX_BUF_LEN];
        char key[GF_DUMP_MAX_BUF_LEN];
        int i;

        snprintf (key_prefix, GF_DUMP_MAX_BUF_LEN, "%s.latency", xl->name);
        gf_proc_dump_add_section (key_prefix);

        for (i = 0; i < GF_FOP_MAXVALUE; i++) {
                gf_proc_dump_build_key (key, key_prefix, gf_fop_list[i]);

                gf_proc_dump_write (key, "%.03f,%"PRId64",%.03f",
                                    xl->latencies[i].mean,
                                    xl->latencies[i].count,
                                    xl->latencies[i].total);
        }
}


void
gf_latency_toggle (int signum)
{
        glusterfs_ctx_t *ctx = NULL;

        ctx = glusterfs_ctx_get ();

        if (ctx) {
                ctx->measure_latency = !ctx->measure_latency;
                gf_log ("[core]", GF_LOG_NORMAL,
                        "Latency measurement turned %s",
                        ctx->measure_latency ? "on" : "off");
        }
}
