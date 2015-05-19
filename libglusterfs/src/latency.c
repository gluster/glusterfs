/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
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
#include "libglusterfs-messages.h"

void
gf_set_fop_from_fn_pointer (call_frame_t *frame, struct xlator_fops *fops, void *fn)
{
        glusterfs_fop_t fop = -1;

        if (fops->stat == *(fop_stat_t *)&fn)
                fop = GF_FOP_STAT;
        else if (fops->readlink == *(fop_readlink_t *)&fn)
                fop = GF_FOP_READLINK;
        else if (fops->mknod == *(fop_mknod_t *)&fn)
                fop = GF_FOP_MKNOD;
        else if (fops->mkdir == *(fop_mkdir_t *)&fn)
                fop = GF_FOP_MKDIR;
        else if (fops->unlink == *(fop_unlink_t *)&fn)
                fop = GF_FOP_UNLINK;
        else if (fops->rmdir == *(fop_rmdir_t *)&fn)
                fop = GF_FOP_RMDIR;
        else if (fops->symlink == *(fop_symlink_t *)&fn)
                fop = GF_FOP_SYMLINK;
        else if (fops->rename == *(fop_rename_t *)&fn)
                fop = GF_FOP_RENAME;
        else if (fops->link == *(fop_link_t *)&fn)
                fop = GF_FOP_LINK;
        else if (fops->truncate == *(fop_truncate_t *)&fn)
                fop = GF_FOP_TRUNCATE;
        else if (fops->open == *(fop_open_t *)&fn)
                fop = GF_FOP_OPEN;
        else if (fops->readv == *(fop_readv_t *)&fn)
                fop = GF_FOP_READ;
        else if (fops->writev == *(fop_writev_t *)&fn)
                fop = GF_FOP_WRITE;
        else if (fops->statfs == *(fop_statfs_t *)&fn)
                fop = GF_FOP_STATFS;
        else if (fops->flush == *(fop_flush_t *)&fn)
                fop = GF_FOP_FLUSH;
        else if (fops->fsync == *(fop_fsync_t *)&fn)
                fop = GF_FOP_FSYNC;
        else if (fops->setxattr == *(fop_setxattr_t *)&fn)
                fop = GF_FOP_SETXATTR;
        else if (fops->getxattr == *(fop_getxattr_t *)&fn)
                fop = GF_FOP_GETXATTR;
        else if (fops->removexattr == *(fop_removexattr_t *)&fn)
                fop = GF_FOP_REMOVEXATTR;
        else if (fops->opendir == *(fop_opendir_t *)&fn)
                fop = GF_FOP_OPENDIR;
        else if (fops->fsyncdir == *(fop_fsyncdir_t *)&fn)
                fop = GF_FOP_FSYNCDIR;
        else if (fops->access == *(fop_access_t *)&fn)
                fop = GF_FOP_ACCESS;
        else if (fops->create == *(fop_create_t *)&fn)
                fop = GF_FOP_CREATE;
        else if (fops->ftruncate == *(fop_ftruncate_t *)&fn)
                fop = GF_FOP_FTRUNCATE;
        else if (fops->fstat == *(fop_fstat_t *)&fn)
                fop = GF_FOP_FSTAT;
        else if (fops->lk == *(fop_lk_t *)&fn)
                fop = GF_FOP_LK;
        else if (fops->lookup == *(fop_lookup_t *)&fn)
                fop = GF_FOP_LOOKUP;
        else if (fops->readdir == *(fop_readdir_t *)&fn)
                fop = GF_FOP_READDIR;
        else if (fops->inodelk == *(fop_inodelk_t *)&fn)
                fop = GF_FOP_INODELK;
        else if (fops->finodelk == *(fop_finodelk_t *)&fn)
                fop = GF_FOP_FINODELK;
        else if (fops->entrylk == *(fop_entrylk_t *)&fn)
                fop = GF_FOP_ENTRYLK;
        else if (fops->fentrylk == *(fop_fentrylk_t *)&fn)
                fop = GF_FOP_FENTRYLK;
        else if (fops->xattrop == *(fop_xattrop_t *)&fn)
                fop = GF_FOP_XATTROP;
        else if (fops->fxattrop == *(fop_fxattrop_t *)&fn)
                fop = GF_FOP_FXATTROP;
        else if (fops->fgetxattr == *(fop_fgetxattr_t *)&fn)
                fop = GF_FOP_FGETXATTR;
        else if (fops->fsetxattr == *(fop_fsetxattr_t *)&fn)
                fop = GF_FOP_FSETXATTR;
        else if (fops->rchecksum == *(fop_rchecksum_t *)&fn)
                fop = GF_FOP_RCHECKSUM;
        else if (fops->setattr == *(fop_setattr_t *)&fn)
                fop = GF_FOP_SETATTR;
        else if (fops->fsetattr == *(fop_fsetattr_t *)&fn)
                fop = GF_FOP_FSETATTR;
        else if (fops->readdirp == *(fop_readdirp_t *)&fn)
                fop = GF_FOP_READDIRP;
        else if (fops->getspec == *(fop_getspec_t *)&fn)
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
gf_latency_begin (call_frame_t *frame, void *fn)
{
        gf_set_fop_from_fn_pointer (frame, frame->this->fops, fn);

        gettimeofday (&frame->begin, NULL);
}


void
gf_latency_end (call_frame_t *frame)
{
        gettimeofday (&frame->end, NULL);

        gf_update_latency (frame);
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
                gf_proc_dump_build_key (key, key_prefix,
                                        (char *)gf_fop_list[i]);

                gf_proc_dump_write (key, "%.03f,%"PRId64",%.03f",
                                    xl->latencies[i].mean,
                                    xl->latencies[i].count,
                                    xl->latencies[i].total);
        }

        memset (xl->latencies, 0, sizeof (xl->latencies));
}


void
gf_latency_toggle (int signum, glusterfs_ctx_t *ctx)
{
        if (ctx) {
                ctx->measure_latency = !ctx->measure_latency;
                gf_msg ("[core]", GF_LOG_INFO, 0,
                        LG_MSG_LATENCY_MEASUREMENT_STATE,
                        "Latency measurement turned %s",
                        ctx->measure_latency ? "on" : "off");
        }
}
