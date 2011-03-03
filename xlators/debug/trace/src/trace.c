/*
  Copyright (c) 2006-2010 Gluster, Inc. <http://www.gluster.com>
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

/**
 * xlators/debug/trace :
 *    This translator logs all the arguments to the fops/mops and also
 *    their _cbk functions, which later passes the call to next layer.
 *    Very helpful translator for debugging.
 */

#include <time.h>
#include <errno.h>
#include "glusterfs.h"
#include "xlator.h"
#include "common-utils.h"


struct {
        char *name;
        int enabled;
} trace_fop_names[GF_FOP_MAXVALUE];

int trace_log_level = GF_LOG_NORMAL;

static char *
trace_stat_to_str (struct iatt *buf)
{
        char    *statstr           = NULL;
        char     atime_buf[256]    = {0,};
        char     mtime_buf[256]    = {0,};
        char     ctime_buf[256]    = {0,};
        int      asprint_ret_value = 0;
        uint64_t ia_time           = 0;

        if (!buf) {
                statstr = NULL;
                goto out;
        }

        ia_time = buf->ia_atime;
        strftime (atime_buf, 256, "[%b %d %H:%M:%S]",
                  localtime ((time_t *)&ia_time));

        ia_time = buf->ia_mtime;
        strftime (mtime_buf, 256, "[%b %d %H:%M:%S]",
                  localtime ((time_t *)&ia_time));

        ia_time = buf->ia_ctime;
        strftime (ctime_buf, 256, "[%b %d %H:%M:%S]",
                  localtime ((time_t *)&ia_time));

        asprint_ret_value = gf_asprintf (&statstr,
                                         "gfid=%s ino=%"PRIu64", mode=%o, "
                                         "nlink=%"GF_PRI_NLINK", uid=%u, "
                                         "gid=%u, size=%"PRIu64", "
                                         "blocks=%"PRIu64", atime=%s, "
                                         "mtime=%s, ctime=%s",
                                         uuid_utoa (buf->ia_gfid), buf->ia_ino,
                                         st_mode_from_ia (buf->ia_prot,
                                                          buf->ia_type),
                                         buf->ia_nlink, buf->ia_uid,
                                         buf->ia_gid, buf->ia_size,
                                         buf->ia_blocks, atime_buf,
                                         mtime_buf, ctime_buf);

        if (asprint_ret_value < 0)
                statstr = NULL;

out:
        return statstr;
}


int
trace_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, fd_t *fd,
                  inode_t *inode, struct iatt *buf,
                  struct iatt *preparent, struct iatt *postparent)
{
        char  *statstr = NULL;
        char  *preparentstr = NULL;
        char  *postparentstr = NULL;

        if (trace_fop_names[GF_FOP_CREATE].enabled) {
                if (op_ret >= 0) {
                        statstr = trace_stat_to_str (buf);
                        preparentstr = trace_stat_to_str (preparent);
                        postparentstr = trace_stat_to_str (postparent);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s (op_ret=%d, fd=%p"
                                "*stbuf {%s}, *preparent {%s}, *postparent = "
                                "{%s})", frame->root->unique,
                                uuid_utoa (inode->gfid), op_ret, fd,
                                statstr, preparentstr, postparentstr);

                        if (statstr)
                                GF_FREE (statstr);
                        if (preparentstr)
                                GF_FREE (preparentstr);
                        if (postparentstr)
                                GF_FREE (postparentstr);

                        /* for 'release' log */
                        fd_ctx_set (fd, this, 0);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d)",
                                frame->root->unique, op_ret, op_errno);
                }
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (create, frame, op_ret, op_errno, fd, inode, buf,
                             preparent, postparent);
        return 0;
}


int
trace_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, fd_t *fd)
{

        if (trace_fop_names[GF_FOP_OPEN].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s op_ret=%d, op_errno=%d, *fd=%p",
                        frame->root->unique, uuid_utoa (frame->local), op_ret, op_errno, fd);
        }

        /* for 'release' log */
        if (op_ret >= 0)
                fd_ctx_set (fd, this, 0);

        frame->local = NULL;
        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd);
        return 0;
}


int
trace_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        char *statstr = NULL;
        if (trace_fop_names[GF_FOP_STAT].enabled) {
                if (op_ret >= 0) {
                        statstr = trace_stat_to_str (buf);
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s op_ret=%d buf=%s",
                                frame->root->unique, uuid_utoa (frame->local),
                                op_ret, statstr);

                        if (statstr)
                                GF_FREE (statstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s op_ret=%d, op_errno=%d)",
                                frame->root->unique, uuid_utoa (frame->local), op_ret, op_errno);
                }
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (stat, frame, op_ret, op_errno, buf);
        return 0;
}


int
trace_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iovec *vector,
                 int32_t count, struct iatt *buf, struct iobref *iobref)
{
        char  *statstr = NULL;

        if (trace_fop_names[GF_FOP_READ].enabled) {
                if (op_ret >= 0) {
                        statstr = trace_stat_to_str (buf);
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s op_ret=%d buf=%s",
                                frame->root->unique, uuid_utoa (frame->local),
                                op_ret, statstr);

                        if (statstr)
                                GF_FREE (statstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s op_ret=%d, op_errno=%d)",
                                frame->root->unique, uuid_utoa (frame->local),
                                op_ret, op_errno);
                }
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno, vector, count,
                             buf, iobref);
        return 0;
}


int
trace_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  struct iatt *prebuf, struct iatt *postbuf)
{
        char  *preopstr = NULL;
        char  *postopstr = NULL;

        if (trace_fop_names[GF_FOP_WRITE].enabled) {
                if (op_ret >= 0) {
                        preopstr = trace_stat_to_str (prebuf);
                        postopstr = trace_stat_to_str (postbuf);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, *prebuf = {%s}, "
                                "*postbuf = {%s})",
                                frame->root->unique, op_ret,
                                preopstr, postopstr);

                        if (preopstr)
                                GF_FREE (preopstr);

                        if (postopstr)
                                GF_FREE (postopstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                                frame->root->unique, uuid_utoa (frame->local),
                                op_ret, op_errno);
                }
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf);
        return 0;
}



int
trace_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, gf_dirent_t *buf)
{
        if (trace_fop_names[GF_FOP_READDIR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64" : gfid=%s op_ret=%d, op_errno=%d",
                        frame->root->unique, uuid_utoa (frame->local), op_ret, op_errno);
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (readdir, frame, op_ret, op_errno, buf);

        return 0;
}


int
trace_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, gf_dirent_t *buf)
{
        if (trace_fop_names[GF_FOP_READDIRP].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64" : gfid=%s op_ret=%d, op_errno=%d",
                        frame->root->unique, uuid_utoa (frame->local), op_ret, op_errno);
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (readdirp, frame, op_ret, op_errno, buf);

        return 0;
}


int
trace_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 struct iatt *prebuf, struct iatt *postbuf)
{
        char  *preopstr = NULL;
        char  *postopstr = NULL;

        if (trace_fop_names[GF_FOP_FSYNC].enabled) {
                if (op_ret >= 0) {
                        preopstr = trace_stat_to_str (prebuf);
                        postopstr = trace_stat_to_str (postbuf);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, *prebuf = {%s}, "
                                "*postbuf = {%s}",
                                frame->root->unique, op_ret,
                                preopstr, postopstr);

                        if (preopstr)
                                GF_FREE (preopstr);

                        if (postopstr)
                                GF_FREE (postopstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                                frame->root->unique, uuid_utoa (frame->local),
                                op_ret, op_errno);
                }
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (fsync, frame, op_ret, op_errno, prebuf, postbuf);

        return 0;
}


int
trace_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno,
                   struct iatt *statpre, struct iatt *statpost)
{
        char  *preopstr = NULL;
        char  *postopstr = NULL;

        if (trace_fop_names[GF_FOP_SETATTR].enabled) {
                if (op_ret >= 0) {
                        preopstr = trace_stat_to_str (statpre);
                        postopstr = trace_stat_to_str (statpost);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, *prebuf = {%s}, "
                                "*postbuf = {%s})",
                                frame->root->unique, op_ret,
                                preopstr, postopstr);

                        if (preopstr)
                                GF_FREE (preopstr);

                        if (postopstr)
                                GF_FREE (postopstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s op_ret=%d, op_errno=%d)",
                                frame->root->unique, uuid_utoa (frame->local), op_ret, op_errno);
                }
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (setattr, frame, op_ret, op_errno, statpre, statpost);
        return 0;
}


int
trace_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    struct iatt *statpre, struct iatt *statpost)
{
        char  *preopstr = NULL;
        char  *postopstr = NULL;

        if (trace_fop_names[GF_FOP_FSETATTR].enabled) {
                if (op_ret >= 0) {
                        preopstr = trace_stat_to_str (statpre);
                        postopstr = trace_stat_to_str (statpost);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, *prebuf = {%s}, "
                                "*postbuf = {%s})",
                                frame->root->unique, op_ret,
                                preopstr, postopstr);

                        if (preopstr)
                                GF_FREE (preopstr);

                        if (postopstr)
                                GF_FREE (postopstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s op_ret=%d, op_errno=%d)",
                                frame->root->unique, uuid_utoa (frame->local), op_ret, op_errno);
                }
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (fsetattr, frame, op_ret, op_errno,
                             statpre, statpost);
        return 0;
}


int
trace_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  struct iatt *preparent, struct iatt *postparent)
{
        char *preparentstr = NULL;
        char *postparentstr = NULL;

        if (trace_fop_names[GF_FOP_UNLINK].enabled) {
                if (op_ret >= 0) {
                        preparentstr = trace_stat_to_str (preparent);
                        postparentstr = trace_stat_to_str (postparent);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s op_ret=%d, *preparent = {%s}, "
                                "*postparent = {%s})",
                                frame->root->unique, uuid_utoa (frame->local), op_ret, preparentstr,
                                postparentstr);

                        if (preparentstr)
                                GF_FREE (preparentstr);

                        if (postparentstr)
                                GF_FREE (postparentstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s op_ret=%d, op_errno=%d)",
                                frame->root->unique, uuid_utoa (frame->local), op_ret, op_errno);
                }
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (unlink, frame, op_ret, op_errno,
                             preparent, postparent);
        return 0;
}


int
trace_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *buf,
                  struct iatt *preoldparent, struct iatt *postoldparent,
                  struct iatt *prenewparent, struct iatt *postnewparent)
{
        char  *statstr = NULL;
        char  *preoldparentstr = NULL;
        char  *postoldparentstr = NULL;
        char  *prenewparentstr = NULL;
        char  *postnewparentstr = NULL;

        if (trace_fop_names[GF_FOP_RENAME].enabled) {
                if (op_ret >= 0) {
                        statstr = trace_stat_to_str (buf);
                        preoldparentstr = trace_stat_to_str (preoldparent);
                        postoldparentstr = trace_stat_to_str (postoldparent);

                        prenewparentstr = trace_stat_to_str (prenewparent);
                        postnewparentstr = trace_stat_to_str (postnewparent);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, *stbuf = {%s}, "
                                "*preoldparent = {%s}, *postoldparent = {%s}"
                                " *prenewparent = {%s}, *postnewparent = {%s})",
                                frame->root->unique, op_ret, statstr,
                                preoldparentstr, postoldparentstr,
                                prenewparentstr, postnewparentstr);

                        if (preoldparentstr)
                                GF_FREE (preoldparentstr);

                        if (postoldparentstr)
                                GF_FREE (postoldparentstr);

                        if (prenewparentstr)
                                GF_FREE (prenewparentstr);

                        if (postnewparentstr)
                                GF_FREE (postnewparentstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                                frame->root->unique, uuid_utoa (frame->local),
                                op_ret, op_errno);
                }
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (rename, frame, op_ret, op_errno, buf,
                             preoldparent, postoldparent,
                             prenewparent, postnewparent);
        return 0;
}


int
trace_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    const char *buf, struct iatt *stbuf)
{
        char *statstr = NULL;

        if (trace_fop_names[GF_FOP_READLINK].enabled) {

                if (op_ret == 0) {
                        statstr = trace_stat_to_str (stbuf);
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d, buf=%s, "
                                "stbuf = { %s })",
                                frame->root->unique, op_ret, op_errno, buf,
                                statstr);
                } else
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                                frame->root->unique, uuid_utoa (frame->local),
                                op_ret, op_errno);

                if (statstr)
                        GF_FREE (statstr);
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (readlink, frame, op_ret, op_errno, buf, stbuf);
        return 0;
}


int
trace_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  inode_t *inode, struct iatt *buf,
                  dict_t *xattr, struct iatt *postparent)
{
        char  *statstr = NULL;
        char  *postparentstr = NULL;

        if (trace_fop_names[GF_FOP_LOOKUP].enabled) {
                if (op_ret >= 0) {
                        statstr = trace_stat_to_str (buf);
                        postparentstr = trace_stat_to_str (postparent);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s (op_ret=%d "
                                "*buf {%s}, *postparent {%s}",
                                frame->root->unique, uuid_utoa (inode->gfid),
                                op_ret, statstr, postparentstr);

                        if (statstr)
                                GF_FREE (statstr);
                        if (postparentstr)
                                GF_FREE (postparentstr);

                        /* For 'forget' */
                        inode_ctx_put (inode, this, 0);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s op_ret=%d, op_errno=%d)",
                                frame->root->unique, uuid_utoa (frame->local),
                                op_ret, op_errno);
                }
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, inode, buf,
                             xattr, postparent);
        return 0;
}


int
trace_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno,
                   inode_t *inode, struct iatt *buf,
                   struct iatt *preparent, struct iatt *postparent)
{
        char  *statstr = NULL;
        char  *preparentstr = NULL;
        char  *postparentstr = NULL;

        if (trace_fop_names[GF_FOP_SYMLINK].enabled) {
                if (op_ret >= 0) {
                        statstr = trace_stat_to_str (buf);
                        preparentstr = trace_stat_to_str (preparent);
                        postparentstr = trace_stat_to_str (postparent);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s (op_ret=%d "
                                "*stbuf = {%s}, *preparent = {%s}, "
                                "*postparent = {%s})",
                                frame->root->unique, uuid_utoa (inode->gfid),
                                op_ret, statstr, preparentstr, postparentstr);

                        if (statstr)
                                GF_FREE (statstr);

                        if (preparentstr)
                                GF_FREE (preparentstr);

                        if (postparentstr)
                                GF_FREE (postparentstr);

                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": op_ret=%d, op_errno=%d",
                                frame->root->unique, op_ret, op_errno);
                }
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (symlink, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
        return 0;
}


int
trace_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 inode_t *inode, struct iatt *buf,
                 struct iatt *preparent, struct iatt *postparent)
{
        char *statstr = NULL;
        char *preparentstr = NULL;
        char *postparentstr = NULL;

        if (trace_fop_names[GF_FOP_MKNOD].enabled) {
                if (op_ret >= 0) {
                        statstr = trace_stat_to_str (buf);
                        preparentstr = trace_stat_to_str (preparent);
                        postparentstr = trace_stat_to_str (postparent);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s (op_ret=%d "
                                "*stbuf = {%s}, *preparent = {%s}, "
                                "*postparent = {%s})",
                                frame->root->unique, uuid_utoa (inode->gfid),
                                op_ret, statstr, preparentstr, postparentstr);

                        if (statstr)
                                GF_FREE (statstr);

                        if (preparentstr)
                                GF_FREE (preparentstr);

                        if (postparentstr)
                                GF_FREE (postparentstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d)",
                                frame->root->unique, op_ret, op_errno);
                }
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (mknod, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
        return 0;
}


int
trace_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 inode_t *inode, struct iatt *buf,
                 struct iatt *preparent, struct iatt *postparent)
{
        char  *statstr = NULL;
        char  *preparentstr = NULL;
        char  *postparentstr = NULL;

        if (trace_fop_names[GF_FOP_MKDIR].enabled) {
                if (op_ret >= 0) {
                        statstr = trace_stat_to_str (buf);
                        preparentstr = trace_stat_to_str (preparent);
                        postparentstr = trace_stat_to_str (postparent);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s (op_ret=%d "
                                ", *stbuf = {%s}, *prebuf = {%s}, "
                                "*postbuf = {%s} )",
                                frame->root->unique, uuid_utoa (inode->gfid),
                                op_ret, statstr, preparentstr, postparentstr);

                        if (statstr)
                                GF_FREE (statstr);

                        if (preparentstr)
                                GF_FREE (preparentstr);

                        if (postparentstr)
                                GF_FREE (postparentstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d)",
                                frame->root->unique, op_ret, op_errno);
                }
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (mkdir, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
        return 0;
}


int
trace_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno,
                inode_t *inode, struct iatt *buf,
                struct iatt *preparent, struct iatt *postparent)
{
        char  *statstr = NULL;
        char  *preparentstr = NULL;
        char  *postparentstr = NULL;

        if (trace_fop_names[GF_FOP_LINK].enabled) {
                if (op_ret >= 0) {
                        statstr = trace_stat_to_str (buf);
                        preparentstr = trace_stat_to_str (preparent);
                        postparentstr = trace_stat_to_str (postparent);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, *stbuf = {%s}, "
                                " *prebuf = {%s}, *postbuf = {%s})",
                                frame->root->unique, op_ret,
                                statstr, preparentstr, postparentstr);

                        if (statstr)
                                GF_FREE (statstr);

                        if (preparentstr)
                                GF_FREE (preparentstr);

                        if (postparentstr)
                                GF_FREE (postparentstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                                frame->root->unique, uuid_utoa (frame->local),
                                op_ret, op_errno);
                }
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (link, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
        return 0;
}


int
trace_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno)
{
        if (trace_fop_names[GF_FOP_FLUSH].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                        frame->root->unique, uuid_utoa (frame->local),
                        op_ret, op_errno);
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno);
        return 0;
}


int
trace_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        if (trace_fop_names[GF_FOP_OPENDIR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s op_ret=%d, op_errno=%d, fd=%p",
                        frame->root->unique, uuid_utoa (frame->local),
                        op_ret, op_errno, fd);
        }

        /* for 'releasedir' log */
        if (op_ret >= 0)
                fd_ctx_set (fd, this, 0);

        frame->local = NULL;
        STACK_UNWIND_STRICT (opendir, frame, op_ret, op_errno, fd);
        return 0;
}


int
trace_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 struct iatt *preparent, struct iatt *postparent)
{
        char  *preparentstr = NULL;
        char  *postparentstr = NULL;

        if (trace_fop_names[GF_FOP_RMDIR].enabled) {
                if (op_ret >= 0) {
                        preparentstr = trace_stat_to_str (preparent);
                        postparentstr = trace_stat_to_str (postparent);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s op_ret=%d, *prebuf = {%s}, "
                                "*postbuf = {%s}",
                                frame->root->unique, uuid_utoa (frame->local),
                                op_ret, preparentstr,  postparentstr);

                        if (preparentstr)
                                GF_FREE (preparentstr);

                        if (postparentstr)
                                GF_FREE (postparentstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                                frame->root->unique, uuid_utoa (frame->local),
                                op_ret, op_errno);
                }
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (rmdir, frame, op_ret, op_errno,
                             preparent, postparent);
        return 0;
}


int
trace_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    struct iatt *prebuf, struct iatt *postbuf)
{
        char  *preopstr = NULL;
        char  *postopstr = NULL;

        if (trace_fop_names[GF_FOP_TRUNCATE].enabled) {
                if (op_ret >= 0) {
                        preopstr = trace_stat_to_str (prebuf);
                        postopstr = trace_stat_to_str (postbuf);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, *prebuf = {%s}, "
                                "*postbuf = {%s} )",
                                frame->root->unique, op_ret, preopstr,
                                postopstr);

                        if (preopstr)
                                GF_FREE (preopstr);

                        if (postopstr)
                                GF_FREE (postopstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                                frame->root->unique, uuid_utoa (frame->local),
                                op_ret, op_errno);
                }
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno, prebuf, postbuf);
        return 0;
}


int
trace_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct statvfs *buf)
{
        if (trace_fop_names[GF_FOP_STATFS].enabled) {
                if (op_ret >= 0) {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": ({f_bsize=%lu, f_frsize=%lu, f_blocks=%"GF_PRI_FSBLK
                                ", f_bfree=%"GF_PRI_FSBLK", f_bavail=%"GF_PRI_FSBLK", "
                                "f_files=%"GF_PRI_FSBLK", f_ffree=%"GF_PRI_FSBLK", f_favail=%"
                                GF_PRI_FSBLK", f_fsid=%lu, f_flag=%lu, f_namemax=%lu}) => ret=%d",
                                frame->root->unique, buf->f_bsize, buf->f_frsize, buf->f_blocks,
                                buf->f_bfree, buf->f_bavail, buf->f_files, buf->f_ffree,
                                buf->f_favail, buf->f_fsid, buf->f_flag, buf->f_namemax, op_ret);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": (op_ret=%d, op_errno=%d)",
                                frame->root->unique, op_ret, op_errno);
                }
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (statfs, frame, op_ret, op_errno, buf);
        return 0;
}


int
trace_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno)
{
        if (trace_fop_names[GF_FOP_SETXATTR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                        frame->root->unique, uuid_utoa (frame->local),
                        op_ret, op_errno);
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno);
        return 0;
}


int
trace_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        if (trace_fop_names[GF_FOP_GETXATTR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s op_ret=%d, op_errno=%d, dict=%p",
                        frame->root->unique, uuid_utoa (frame->local), op_ret,
                        op_errno, dict);
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict);

        return 0;
}

int
trace_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno)
{
        if (trace_fop_names[GF_FOP_FSETXATTR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                        frame->root->unique, uuid_utoa (frame->local), op_ret,
                        op_errno);
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno);
        return 0;
}


int
trace_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        if (trace_fop_names[GF_FOP_FGETXATTR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s op_ret=%d, op_errno=%d, dict=%p",
                        frame->root->unique, uuid_utoa (frame->local), op_ret,
                        op_errno, dict);
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (fgetxattr, frame, op_ret, op_errno, dict);

        return 0;
}

int
trace_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno)
{
        if (trace_fop_names[GF_FOP_REMOVEXATTR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                        frame->root->unique, uuid_utoa (frame->local),
                        op_ret, op_errno);
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (removexattr, frame, op_ret, op_errno);

        return 0;
}


int
trace_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno)
{
        if (trace_fop_names[GF_FOP_FSYNCDIR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                        frame->root->unique, uuid_utoa (frame->local),
                        op_ret, op_errno);
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (fsyncdir, frame, op_ret, op_errno);
        return 0;
}


int
trace_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno)
{
        if (trace_fop_names[GF_FOP_ACCESS].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s op_ret=%d, op_errno=%d)",
                        frame->root->unique, uuid_utoa (frame->local),
                        op_ret, op_errno);
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (access, frame, op_ret, op_errno);
        return 0;
}


int
trace_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *prebuf, struct iatt *postbuf)
{
        char  *prebufstr = NULL;
        char  *postbufstr = NULL;

        if (trace_fop_names[GF_FOP_FTRUNCATE].enabled) {
                if (op_ret >= 0) {
                        prebufstr = trace_stat_to_str (prebuf);
                        postbufstr = trace_stat_to_str (postbuf);

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": op_ret=%d, *prebuf = {%s}, "
                                "*postbuf = {%s} )",
                                frame->root->unique, op_ret,
                                prebufstr, postbufstr);

                        if (prebufstr)
                                GF_FREE (prebufstr);

                        if (postbufstr)
                                GF_FREE (postbufstr);

                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                                frame->root->unique, uuid_utoa (frame->local),
                                op_ret, op_errno);
                }
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno, prebuf, postbuf);
        return 0;
}


int
trace_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        char *statstr = NULL;

        if (trace_fop_names[GF_FOP_FSTAT].enabled) {
                if (op_ret >= 0) {
                        statstr = trace_stat_to_str (buf);
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s op_ret=%d buf=%s",
                                frame->root->unique, uuid_utoa (frame->local),
                                op_ret, statstr);

                        if (statstr)
                                GF_FREE (statstr);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                                frame->root->unique, uuid_utoa (frame->local),
                                op_ret, op_errno);
                }
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (fstat, frame, op_ret, op_errno, buf);
        return 0;
}


int
trace_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, struct gf_flock *lock)
{
        if (trace_fop_names[GF_FOP_LK].enabled) {
                if (op_ret >= 0) {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s op_ret=%d, {l_type=%d, l_whence=%d, "
                                "l_start=%"PRId64", l_len=%"PRId64", l_pid=%u})",
                                frame->root->unique, uuid_utoa (frame->local),
                                op_ret, lock->l_type, lock->l_whence,
                                lock->l_start, lock->l_len, lock->l_pid);
                } else {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s op_ret=%d, op_errno=%d)",
                                frame->root->unique, uuid_utoa (frame->local),
                                op_ret, op_errno);
                }
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (lk, frame, op_ret, op_errno, lock);
        return 0;
}



int
trace_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno)
{
        if (trace_fop_names[GF_FOP_ENTRYLK].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                        frame->root->unique, uuid_utoa (frame->local),
                        op_ret, op_errno);
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (entrylk, frame, op_ret, op_errno);
        return 0;
}

int
trace_fentrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno)
{
        if (trace_fop_names[GF_FOP_FENTRYLK].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                        frame->root->unique, uuid_utoa (frame->local),
                        op_ret, op_errno);
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (fentrylk, frame, op_ret, op_errno);
        return 0;
}


int
trace_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        if (trace_fop_names[GF_FOP_XATTROP].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                        frame->root->unique, uuid_utoa (frame->local),
                        op_ret, op_errno);
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (xattrop, frame, op_ret, op_errno, dict);
        return 0;
}


int
trace_fxattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        if (trace_fop_names[GF_FOP_FXATTROP].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                        frame->root->unique, uuid_utoa (frame->local),
                        op_ret, op_errno);
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (fxattrop, frame, op_ret, op_errno, dict);
        return 0;
}


int
trace_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno)
{
        if (trace_fop_names[GF_FOP_INODELK].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                        frame->root->unique, uuid_utoa (frame->local),
                        op_ret, op_errno);
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (inodelk, frame, op_ret, op_errno);
        return 0;
}

int
trace_finodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno)
{
        if (trace_fop_names[GF_FOP_FINODELK].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                        frame->root->unique, uuid_utoa (frame->local), op_ret, op_errno);
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (finodelk, frame, op_ret, op_errno);
        return 0;
}


int
trace_rchecksum_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     uint32_t weak_checksum, uint8_t *strong_checksum)
{
        if (trace_fop_names[GF_FOP_RCHECKSUM].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s op_ret=%d op_errno=%d",
                        frame->root->unique, uuid_utoa (frame->local), op_ret, op_errno);
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (rchecksum, frame, op_ret, op_errno, weak_checksum,
                             strong_checksum);

        return 0;
}

/* *_cbk section over <----------> fop section start */

int
trace_entrylk (call_frame_t *frame, xlator_t *this,
               const char *volume, loc_t *loc, const char *basename,
               entrylk_cmd cmd, entrylk_type type)
{
        if (trace_fop_names[GF_FOP_ENTRYLK].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s volume=%s, (path=%s basename=%s, "
                        "cmd=%s, type=%s)",
                        frame->root->unique, uuid_utoa (loc->inode->gfid),
                        volume, loc->path, basename,
                        ((cmd == ENTRYLK_LOCK) ? "ENTRYLK_LOCK" : "ENTRYLK_UNLOCK"),
                        ((type == ENTRYLK_RDLCK) ? "ENTRYLK_RDLCK" : "ENTRYLK_WRLCK"));
                frame->local = loc->inode->gfid;
        }

        STACK_WIND (frame, trace_entrylk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->entrylk,
                    volume, loc, basename, cmd, type);
        return 0;
}


int
trace_inodelk (call_frame_t *frame, xlator_t *this, const char *volume,
               loc_t *loc, int32_t cmd, struct gf_flock *flock)
{
        char *cmd_str = NULL;
        char *type_str = NULL;

        if (trace_fop_names[GF_FOP_INODELK].enabled) {
                switch (cmd) {
#if F_GETLK != F_GETLK64
                case F_GETLK64:
#endif
                case F_GETLK:
                        cmd_str = "GETLK";
                        break;

#if F_SETLK != F_SETLK64
                case F_SETLK64:
#endif
                case F_SETLK:
                        cmd_str = "SETLK";
                        break;

#if F_SETLKW != F_SETLKW64
                case F_SETLKW64:
#endif
                case F_SETLKW:
                        cmd_str = "SETLKW";
                        break;

                default:
                        cmd_str = "UNKNOWN";
                        break;
                }

                switch (flock->l_type) {
                case F_RDLCK:
                        type_str = "READ";
                        break;
                case F_WRLCK:
                        type_str = "WRITE";
                        break;
                case F_UNLCK:
                        type_str = "UNLOCK";
                        break;
                default:
                        type_str = "UNKNOWN";
                        break;
                }

                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s volume=%s, (path=%s "
                        "cmd=%s, type=%s, start=%llu, len=%llu, pid=%llu)",
                        frame->root->unique, uuid_utoa (loc->inode->gfid),
                        volume, loc->path,
                        cmd_str, type_str, (unsigned long long) flock->l_start,
                        (unsigned long long) flock->l_len,
                        (unsigned long long) flock->l_pid);
                frame->local = loc->inode->gfid;
        }

        STACK_WIND (frame, trace_inodelk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->inodelk,
                    volume, loc, cmd, flock);
        return 0;
}


int
trace_finodelk (call_frame_t *frame, xlator_t *this, const char *volume,
                fd_t *fd, int32_t cmd, struct gf_flock *flock)
{
        char *cmd_str = NULL, *type_str = NULL;

        if (trace_fop_names[GF_FOP_FINODELK].enabled) {
                switch (cmd) {
#if F_GETLK != F_GETLK64
                case F_GETLK64:
#endif
                case F_GETLK:
                        cmd_str = "GETLK";
                        break;

#if F_SETLK != F_SETLK64
                case F_SETLK64:
#endif
                case F_SETLK:
                        cmd_str = "SETLK";
                        break;

#if F_SETLKW != F_SETLKW64
                case F_SETLKW64:
#endif
                case F_SETLKW:
                        cmd_str = "SETLKW";
                        break;

                default:
                        cmd_str = "UNKNOWN";
                        break;
                }

                switch (flock->l_type) {
                case F_RDLCK:
                        type_str = "READ";
                        break;
                case F_WRLCK:
                        type_str = "WRITE";
                        break;
                case F_UNLCK:
                        type_str = "UNLOCK";
                        break;
                default:
                        type_str = "UNKNOWN";
                        break;
                }

                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s volume=%s, (fd =%p "
                        "cmd=%s, type=%s, start=%llu, len=%llu, pid=%llu)",
                        frame->root->unique, uuid_utoa (fd->inode->gfid), volume, fd,
                        cmd_str, type_str, (unsigned long long) flock->l_start,
                        (unsigned long long) flock->l_len,
                        (unsigned long long) flock->l_pid);
                frame->local = fd->inode->gfid;
        }

        STACK_WIND (frame, trace_finodelk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->finodelk,
                    volume, fd, cmd, flock);
        return 0;
}


int
trace_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
               gf_xattrop_flags_t flags, dict_t *dict)
{
        if (trace_fop_names[GF_FOP_XATTROP].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s (path=%s flags=%d)",
                        frame->root->unique, uuid_utoa (loc->inode->gfid),
                        loc->path, flags);
                frame->local = loc->inode->gfid;
        }

        STACK_WIND (frame, trace_xattrop_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->xattrop,
                    loc, flags, dict);

        return 0;
}


int
trace_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
                gf_xattrop_flags_t flags, dict_t *dict)
{
        if (trace_fop_names[GF_FOP_FXATTROP].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s fd=%p, flags=%d",
                        frame->root->unique, uuid_utoa (fd->inode->gfid),
                        fd, flags);
                frame->local = fd->inode->gfid;
        }

        STACK_WIND (frame, trace_fxattrop_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fxattrop,
                    fd, flags, dict);

        return 0;
}


int
trace_lookup (call_frame_t *frame, xlator_t *this,
              loc_t *loc, dict_t *xattr_req)
{
        if (trace_fop_names[GF_FOP_LOOKUP].enabled) {
                /* TODO: print all the keys mentioned in xattr_req */
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s path=%s",
                        frame->root->unique, uuid_utoa (loc->inode->gfid),
                        loc->path);
                frame->local = loc->inode->gfid;
        }

        STACK_WIND (frame, trace_lookup_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup,
                    loc, xattr_req);

        return 0;
}


int
trace_stat (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        if (trace_fop_names[GF_FOP_STAT].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s path=%s",
                        frame->root->unique, uuid_utoa (loc->inode->gfid),
                        loc->path);
                frame->local = loc->inode->gfid;
        }

        STACK_WIND (frame, trace_stat_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->stat,
                    loc);

        return 0;
}


int
trace_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size)
{
        if (trace_fop_names[GF_FOP_READLINK].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s path=%s, size=%"GF_PRI_SIZET")",
                        frame->root->unique, uuid_utoa (loc->inode->gfid),
                        loc->path, size);
                frame->local = loc->inode->gfid;
        }

        STACK_WIND (frame, trace_readlink_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readlink,
                    loc, size);

        return 0;
}


int
trace_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc,
             mode_t mode, dev_t dev, dict_t *params)
{
        if (trace_fop_names[GF_FOP_MKNOD].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s path=%s mode=%d dev=%"GF_PRI_DEV")",
                        frame->root->unique, uuid_utoa (loc->inode->gfid),
                        loc->path, mode, dev);
        }

        STACK_WIND (frame, trace_mknod_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mknod,
                    loc, mode, dev, params);

        return 0;
}


int
trace_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
             dict_t *params)
{
        if (trace_fop_names[GF_FOP_MKDIR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s path=%s mode=%d",
                        frame->root->unique, uuid_utoa (loc->inode->gfid),
                        loc->path, mode);
        }

        STACK_WIND (frame, trace_mkdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mkdir,
                    loc, mode, params);
        return 0;
}


int
trace_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        if (trace_fop_names[GF_FOP_UNLINK].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s path=%s",
                        frame->root->unique, uuid_utoa (loc->inode->gfid),
                        loc->path);
                frame->local = loc->inode->gfid;
        }

        STACK_WIND (frame, trace_unlink_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink,
                    loc);
        return 0;
}


int
trace_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags)
{
        if (trace_fop_names[GF_FOP_RMDIR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s path=%s flags=%d",
                        frame->root->unique, uuid_utoa (loc->inode->gfid),
                        loc->path, flags);
                frame->local = loc->inode->gfid;
        }

        STACK_WIND (frame, trace_rmdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rmdir,
                    loc, flags);

        return 0;
}


int
trace_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
               loc_t *loc, dict_t *params)
{
        if (trace_fop_names[GF_FOP_SYMLINK].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s linkpath=%s, path=%s",
                        frame->root->unique, uuid_utoa (loc->inode->gfid),
                        linkpath, loc->path);
        }

        STACK_WIND (frame, trace_symlink_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->symlink,
                    linkpath, loc, params);

        return 0;
}


int
trace_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc)
{
        char oldgfid[50] = {0,};
        char newgfid[50] = {0,};

        if (trace_fop_names[GF_FOP_RENAME].enabled) {
                if (newloc->inode)
                        uuid_utoa_r (newloc->inode->gfid, newgfid);
                else
                        strcpy (newgfid, "0");

                uuid_utoa_r (oldloc->inode->gfid, oldgfid);

                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": oldgfid=%s oldpath=%s --> newgfid=%s newpath=%s",
                        frame->root->unique, oldgfid, oldloc->path, newgfid, newloc->path);

                frame->local = oldloc->inode->gfid;
        }

        STACK_WIND (frame, trace_rename_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rename,
                    oldloc, newloc);

        return 0;
}


int
trace_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc)
{
        char oldgfid[50] = {0,};
        char newgfid[50] = {0,};

        if (trace_fop_names[GF_FOP_LINK].enabled) {
                if (newloc->inode)
                        uuid_utoa_r (newloc->inode->gfid, newgfid);
                else
                        strcpy (newgfid, "0");

                uuid_utoa_r (oldloc->inode->gfid, oldgfid);

                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": oldgfid=%s oldpath=%s --> newgfid=%s newpath=%s",
                        frame->root->unique, oldgfid, oldloc->path,
                        newgfid, newloc->path);
                frame->local = oldloc->inode->gfid;
        }

        STACK_WIND (frame, trace_link_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->link,
                    oldloc, newloc);
        return 0;
}


int
trace_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
               struct iatt *stbuf, int32_t valid)
{
        uint64_t ia_time          = 0;
        char     actime_str[256]  = {0,};
        char     modtime_str[256] = {0,};

        if (trace_fop_names[GF_FOP_SETATTR].enabled) {
                if (valid & GF_SET_ATTR_MODE) {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s path=%s mode=%o)",
                                frame->root->unique, uuid_utoa (loc->inode->gfid),
                                loc->path, st_mode_from_ia (stbuf->ia_prot, stbuf->ia_type));
                }

                if (valid & (GF_SET_ATTR_UID | GF_SET_ATTR_GID)) {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s path=%s uid=%o, gid=%o",
                                frame->root->unique,  uuid_utoa (loc->inode->gfid),
                                loc->path, stbuf->ia_uid, stbuf->ia_gid);
                }

                if (valid & (GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME)) {
                        ia_time = stbuf->ia_atime;
                        strftime (actime_str, 256, "[%b %d %H:%M:%S]",
                                  localtime ((time_t *)&ia_time));

                        ia_time = stbuf->ia_mtime;
                        strftime (modtime_str, 256, "[%b %d %H:%M:%S]",
                                  localtime ((time_t *)&ia_time));

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s path=%s ia_atime=%s, ia_mtime=%s",
                                frame->root->unique, uuid_utoa (loc->inode->gfid),
                                loc->path, actime_str, modtime_str);
                }
                frame->local = loc->inode->gfid;
        }

        STACK_WIND (frame, trace_setattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setattr,
                    loc, stbuf, valid);

        return 0;
}


int
trace_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                struct iatt *stbuf, int32_t valid)
{
        uint64_t ia_time          = 0;
        char     actime_str[256]  = {0,};
        char     modtime_str[256] = {0,};

        if (trace_fop_names[GF_FOP_FSETATTR].enabled) {
                if (valid & GF_SET_ATTR_MODE) {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s fd=%p, mode=%o",
                                frame->root->unique, uuid_utoa (fd->inode->gfid), fd,
                                st_mode_from_ia (stbuf->ia_prot, stbuf->ia_type));
                }

                if (valid & (GF_SET_ATTR_UID | GF_SET_ATTR_GID)) {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s fd=%p, uid=%o, gid=%o",
                                frame->root->unique, uuid_utoa (fd->inode->gfid),
                                fd, stbuf->ia_uid, stbuf->ia_gid);
                }

                if (valid & (GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME)) {
                        ia_time = stbuf->ia_atime;
                        strftime (actime_str, 256, "[%b %d %H:%M:%S]",
                                  localtime ((time_t *)&ia_time));

                        ia_time = stbuf->ia_mtime;
                        strftime (modtime_str, 256, "[%b %d %H:%M:%S]",
                                  localtime ((time_t *)&ia_time));

                        gf_log (this->name, GF_LOG_NORMAL,
                                "%"PRId64": gfid=%s fd=%p ia_atime=%s, ia_mtime=%s",
                                frame->root->unique, uuid_utoa (fd->inode->gfid),
                                fd, actime_str, modtime_str);
                }
                frame->local = fd->inode->gfid;
        }

        STACK_WIND (frame, trace_fsetattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsetattr,
                    fd, stbuf, valid);

        return 0;
}


int
trace_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc,
                off_t offset)
{
        if (trace_fop_names[GF_FOP_TRUNCATE].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s path=%s, offset=%"PRId64"",
                        frame->root->unique, uuid_utoa (loc->inode->gfid),
                        loc->path, offset);
                frame->local = loc->inode->gfid;
        }

        STACK_WIND (frame, trace_truncate_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->truncate,
                    loc, offset);

        return 0;
}


int
trace_open (call_frame_t *frame, xlator_t *this, loc_t *loc,
            int32_t flags, fd_t *fd, int32_t wbflags)
{
        if (trace_fop_names[GF_FOP_OPEN].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s path=%s flags=%d fd=%p wbflags=%d",
                        frame->root->unique, uuid_utoa (loc->inode->gfid),
                        loc->path, flags, fd, wbflags);
                frame->local = loc->inode->gfid;
        }

        STACK_WIND (frame, trace_open_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open,
                    loc, flags, fd, wbflags);
        return 0;
}


int
trace_create (call_frame_t *frame, xlator_t *this, loc_t *loc,
              int32_t flags, mode_t mode, fd_t *fd, dict_t *params)
{
        if (trace_fop_names[GF_FOP_CREATE].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s path=%s, fd=%p, flags=0%o mode=0%o",
                        frame->root->unique, uuid_utoa (loc->inode->gfid),
                        loc->path, fd, flags, mode);
        }

        STACK_WIND (frame, trace_create_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->create,
                    loc, flags, mode, fd, params);
        return 0;
}


int
trace_readv (call_frame_t *frame, xlator_t *this, fd_t *fd,
             size_t size, off_t offset)
{
        if (trace_fop_names[GF_FOP_READ].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s fd=%p, size=%"GF_PRI_SIZET", offset=%"PRId64")",
                        frame->root->unique, uuid_utoa (fd->inode->gfid), fd, size, offset);
                frame->local = fd->inode->gfid;
        }

        STACK_WIND (frame, trace_readv_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readv,
                    fd, size, offset);
        return 0;
}


int
trace_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
              struct iovec *vector, int32_t count,
              off_t offset, struct iobref *iobref)
{
        if (trace_fop_names[GF_FOP_WRITE].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s fd=%p, count=%d, offset=%"PRId64")",
                        frame->root->unique, uuid_utoa (fd->inode->gfid),
                        fd, count, offset);
                frame->local = fd->inode->gfid;
        }

        STACK_WIND (frame, trace_writev_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev,
                    fd, vector, count, offset, iobref);
        return 0;
}


int
trace_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        if (trace_fop_names[GF_FOP_STATFS].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s path=%s",
                        frame->root->unique, uuid_utoa (loc->inode->gfid),
                        loc->path);
        }

        STACK_WIND (frame, trace_statfs_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->statfs,
                    loc);
        return 0;
}


int
trace_flush (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        if (trace_fop_names[GF_FOP_FLUSH].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s fd=%p",
                        frame->root->unique, uuid_utoa (fd->inode->gfid), fd);
                frame->local = fd->inode->gfid;
        }

        STACK_WIND (frame, trace_flush_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->flush,
                    fd);
        return 0;
}


int
trace_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags)
{
        if (trace_fop_names[GF_FOP_FSYNC].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s flags=%d fd=%p",
                        frame->root->unique, uuid_utoa (fd->inode->gfid), flags, fd);
                frame->local = fd->inode->gfid;
        }

        STACK_WIND (frame, trace_fsync_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsync,
                    fd, flags);
        return 0;
}


int
trace_setxattr (call_frame_t *frame, xlator_t *this,
                loc_t *loc, dict_t *dict, int32_t flags)
{
        if (trace_fop_names[GF_FOP_SETXATTR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s path=%s flags=%d",
                        frame->root->unique, uuid_utoa (loc->inode->gfid),
                        loc->path, flags);
                frame->local = loc->inode->gfid;
        }

        STACK_WIND (frame, trace_setxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr,
                    loc, dict, flags);
        return 0;
}


int
trace_getxattr (call_frame_t *frame, xlator_t *this,
                loc_t *loc, const char *name)
{
        if (trace_fop_names[GF_FOP_GETXATTR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s path=%s name=%s",
                        frame->root->unique, uuid_utoa (loc->inode->gfid),
                        loc->path, name);
                frame->local = loc->inode->gfid;
        }

        STACK_WIND (frame, trace_getxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->getxattr,
                    loc, name);
        return 0;
}


int
trace_removexattr (call_frame_t *frame, xlator_t *this,
                   loc_t *loc, const char *name)
{
        if (trace_fop_names[GF_FOP_REMOVEXATTR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s path=%s name=%s",
                        frame->root->unique, uuid_utoa (loc->inode->gfid),
                        loc->path, name);
                frame->local = loc->inode->gfid;
        }

        STACK_WIND (frame, trace_removexattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->removexattr,
                    loc, name);

        return 0;
}


int
trace_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd)
{
        if (trace_fop_names[GF_FOP_OPENDIR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s path=%s fd=%p",
                        frame->root->unique, uuid_utoa (loc->inode->gfid),
                        loc->path, fd);
                frame->local = loc->inode->gfid;
        }

        STACK_WIND (frame, trace_opendir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->opendir,
                    loc, fd);
        return 0;
}

int
trace_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                off_t offset)
{
        if (trace_fop_names[GF_FOP_READDIRP].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s fd=%p, size=%"GF_PRI_SIZET", offset=%"PRId64,
                        frame->root->unique, uuid_utoa (fd->inode->gfid),
                        fd, size, offset);
                frame->local = fd->inode->gfid;
        }

        STACK_WIND (frame, trace_readdirp_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdirp,
                    fd, size, offset);

        return 0;
}


int
trace_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd,
               size_t size, off_t offset)
{
        if (trace_fop_names[GF_FOP_READDIR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s fd=%p, size=%"GF_PRI_SIZET", offset=%"PRId64,
                        frame->root->unique, uuid_utoa (fd->inode->gfid),
                        fd, size, offset);
                frame->local = fd->inode->gfid;
        }

        STACK_WIND (frame, trace_readdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdir,
                    fd, size, offset);

        return 0;
}


int
trace_fsyncdir (call_frame_t *frame, xlator_t *this,
                fd_t *fd, int32_t datasync)
{
        if (trace_fop_names[GF_FOP_FSYNCDIR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s datasync=%d fd=%p",
                        frame->root->unique, uuid_utoa (fd->inode->gfid),
                        datasync, fd);
                frame->local = fd->inode->gfid;
        }

        STACK_WIND (frame, trace_fsyncdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsyncdir,
                    fd, datasync);
        return 0;
}


int
trace_access (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask)
{
        if (trace_fop_names[GF_FOP_ACCESS].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s path=%s mask=0%o",
                        frame->root->unique, uuid_utoa (loc->inode->gfid),
                        loc->path, mask);
                frame->local = loc->inode->gfid;
        }

        STACK_WIND (frame, trace_access_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->access,
                    loc, mask);
        return 0;
}


int32_t
trace_rchecksum (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                 int32_t len)
{
        if (trace_fop_names[GF_FOP_RCHECKSUM].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s offset=%"PRId64" len=%u fd=%p",
                        frame->root->unique, uuid_utoa (fd->inode->gfid),
                        offset, len, fd);
                frame->local = fd->inode->gfid;
        }

        STACK_WIND (frame, trace_rchecksum_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rchecksum,
                    fd, offset, len);

        return 0;

}

int32_t
trace_fentrylk (call_frame_t *frame, xlator_t *this, const char *volume,
                fd_t *fd, const char *basename, entrylk_cmd cmd,
                entrylk_type type)
{
        if (trace_fop_names[GF_FOP_FENTRYLK].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s volume=%s, (fd=%p basename=%s, "
                        "cmd=%s, type=%s)",
                        frame->root->unique, uuid_utoa (fd->inode->gfid),
                        volume, fd, basename,
                        ((cmd == ENTRYLK_LOCK) ? "ENTRYLK_LOCK" : "ENTRYLK_UNLOCK"),
                        ((type == ENTRYLK_RDLCK) ? "ENTRYLK_RDLCK" : "ENTRYLK_WRLCK"));
                frame->local = fd->inode->gfid;
        }

        STACK_WIND (frame, trace_fentrylk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fentrylk,
                    volume, fd, basename, cmd, type);
        return 0;

}

int32_t
trace_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 const char *name)
{
        if (trace_fop_names[GF_FOP_FGETXATTR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s fd=%p name=%s",
                        frame->root->unique, uuid_utoa (fd->inode->gfid),
                        fd, name);
                frame->local = fd->inode->gfid;
        }

        STACK_WIND (frame, trace_fgetxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fgetxattr,
                    fd, name);
        return 0;
}

int32_t
trace_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 dict_t *dict, int32_t flags)
{
        if (trace_fop_names[GF_FOP_FSETXATTR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s fd=%p flags=%d",
                        frame->root->unique, uuid_utoa (fd->inode->gfid),
                        fd, flags);
                frame->local = fd->inode->gfid;
        }

        STACK_WIND (frame, trace_fsetxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsetxattr,
                    fd, dict, flags);
        return 0;
}

int
trace_ftruncate (call_frame_t *frame, xlator_t *this,
                 fd_t *fd, off_t offset)
{
        if (trace_fop_names[GF_FOP_FTRUNCATE].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s offset=%"PRId64" fd=%p",
                        frame->root->unique, uuid_utoa (fd->inode->gfid),
                        offset, fd);
                frame->local = fd->inode->gfid;
        }

        STACK_WIND (frame, trace_ftruncate_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->ftruncate,
                    fd, offset);

        return 0;
}


int
trace_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        if (trace_fop_names[GF_FOP_FSTAT].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s fd=%p",
                        frame->root->unique, uuid_utoa (fd->inode->gfid), fd);
                frame->local = fd->inode->gfid;
        }

        STACK_WIND (frame, trace_fstat_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fstat,
                    fd);
        return 0;
}


int
trace_lk (call_frame_t *frame, xlator_t *this, fd_t *fd,
          int32_t cmd, struct gf_flock *lock)
{
        if (trace_fop_names[GF_FOP_LK].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "%"PRId64": gfid=%s fd=%p, cmd=%d, lock {l_type=%d, l_whence=%d, "
                        "l_start=%"PRId64", l_len=%"PRId64", l_pid=%u})",
                        frame->root->unique, uuid_utoa (fd->inode->gfid), fd,
                        cmd, lock->l_type, lock->l_whence,
                        lock->l_start, lock->l_len, lock->l_pid);
                frame->local = fd->inode->gfid;
        }

        STACK_WIND (frame, trace_lk_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lk,
                    fd, cmd, lock);
        return 0;
}

int32_t
trace_forget (xlator_t *this, inode_t *inode)
{
        /* If user want to understand when a lookup happens,
           he should know about 'forget' too */
        if (trace_fop_names[GF_FOP_LOOKUP].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "gfid=%s ino=%"PRIu64,
                        uuid_utoa (inode->gfid), inode->ino);
        }
        return 0;
}


int32_t
trace_releasedir (xlator_t *this, fd_t *fd)
{
        if (trace_fop_names[GF_FOP_OPENDIR].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "gfid=%s fd=%p", uuid_utoa (fd->inode->gfid), fd);
        }

        return 0;
}

int32_t
trace_release (xlator_t *this, fd_t *fd)
{
        if (trace_fop_names[GF_FOP_OPEN].enabled ||
            trace_fop_names[GF_FOP_CREATE].enabled) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "gfid=%s fd=%p", uuid_utoa (fd->inode->gfid), fd);
        }
        return 0;
}



void
enable_all_calls (int enabled)
{
        int i;

        for (i = 0; i < GF_FOP_MAXVALUE; i++)
                trace_fop_names[i].enabled = enabled;
}


void
enable_call (const char *name, int enabled)
{
        int i;
        for (i = 0; i < GF_FOP_MAXVALUE; i++)
                if (!strcasecmp(trace_fop_names[i].name, name))
                        trace_fop_names[i].enabled = enabled;
}


/*
  include = 1 for "include-ops"
  = 0 for "exclude-ops"
*/
void
process_call_list (const char *list, int include)
{
        enable_all_calls (include ? 0 : 1);

        char *call = strsep ((char **)&list, ",");

        while (call) {
                enable_call (call, include);
                call = strsep ((char **)&list, ",");
        }
}


int32_t
init (xlator_t *this)
{
        dict_t *options = NULL;
        char *includes = NULL, *excludes = NULL;
        char *forced_loglevel = NULL;

        if (!this)
                return -1;

        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "trace translator requires one subvolume");
                return -1;
        }
        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dangling volume. check volfile ");
        }


        options = this->options;
        includes = data_to_str (dict_get (options, "include-ops"));
        excludes = data_to_str (dict_get (options, "exclude-ops"));

        {
                int i;
                for (i = 0; i < GF_FOP_MAXVALUE; i++) {
                        trace_fop_names[i].name = (gf_fop_list[i] ?
                                                   gf_fop_list[i] : ":O");
                        trace_fop_names[i].enabled = 1;
                }
        }

        if (includes && excludes) {
                gf_log (this->name,
                        GF_LOG_ERROR,
                        "must specify only one of 'include-ops' and 'exclude-ops'");
                return -1;
        }
        if (includes)
                process_call_list (includes, 1);
        if (excludes)
                process_call_list (excludes, 0);

        if (dict_get (options, "force-log-level")) {
                forced_loglevel = data_to_str (dict_get (options,
                                                         "force-log-level"));
                if (!forced_loglevel)
                        goto setloglevel;

                if (strcmp (forced_loglevel, "NORMAL") == 0)
                        trace_log_level = GF_LOG_NORMAL;
                else if (strcmp (forced_loglevel, "TRACE") == 0)
                        trace_log_level = GF_LOG_TRACE;
                else if (strcmp (forced_loglevel, "ERROR") == 0)
                        trace_log_level = GF_LOG_ERROR;
                else if (strcmp (forced_loglevel, "DEBUG") == 0)
                        trace_log_level = GF_LOG_DEBUG;
                else if (strcmp (forced_loglevel, "WARNING") == 0)
                        trace_log_level = GF_LOG_WARNING;
                else if (strcmp (forced_loglevel, "CRITICAL") == 0)
                        trace_log_level = GF_LOG_CRITICAL;
                else if (strcmp (forced_loglevel, "NONE") == 0)
                        trace_log_level = GF_LOG_NONE;
        }

setloglevel:
        gf_log_set_loglevel (trace_log_level);

        return 0;
}

void
fini (xlator_t *this)
{
        if (!this)
                return;

        gf_log (this->name, GF_LOG_NORMAL,
                "trace translator unloaded");
        return;
}

struct xlator_fops fops = {
        .stat        = trace_stat,
        .readlink    = trace_readlink,
        .mknod       = trace_mknod,
        .mkdir       = trace_mkdir,
        .unlink      = trace_unlink,
        .rmdir       = trace_rmdir,
        .symlink     = trace_symlink,
        .rename      = trace_rename,
        .link        = trace_link,
        .truncate    = trace_truncate,
        .open        = trace_open,
        .readv       = trace_readv,
        .writev      = trace_writev,
        .statfs      = trace_statfs,
        .flush       = trace_flush,
        .fsync       = trace_fsync,
        .setxattr    = trace_setxattr,
        .getxattr    = trace_getxattr,
        .fsetxattr   = trace_fsetxattr,
        .fgetxattr   = trace_fgetxattr,
        .removexattr = trace_removexattr,
        .opendir     = trace_opendir,
        .readdir     = trace_readdir,
        .readdirp    = trace_readdirp,
        .fsyncdir    = trace_fsyncdir,
        .access      = trace_access,
        .ftruncate   = trace_ftruncate,
        .fstat       = trace_fstat,
        .create      = trace_create,
        .lk          = trace_lk,
        .inodelk     = trace_inodelk,
        .finodelk    = trace_finodelk,
        .entrylk     = trace_entrylk,
        .fentrylk    = trace_fentrylk,
        .lookup      = trace_lookup,
        .rchecksum   = trace_rchecksum,
        .xattrop     = trace_xattrop,
        .fxattrop    = trace_fxattrop,
        .setattr     = trace_setattr,
        .fsetattr    = trace_fsetattr,
};


struct xlator_cbks cbks = {
        .release     = trace_release,
        .releasedir  = trace_releasedir,
        .forget      = trace_forget,
};

struct volume_options options[] = {
        { .key  = {"include-ops", "include"},
          .type = GF_OPTION_TYPE_STR,
          /*.value = { ""} */
        },
        { .key  = {"exclude-ops", "exclude"},
          .type = GF_OPTION_TYPE_STR
          /*.value = { ""} */
        },
        { .key  = {NULL} },
};
