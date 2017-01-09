/*
  Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "trace.h"
#include "trace-mem-types.h"

/**
 * xlators/debug/trace :
 *    This translator logs all the arguments to the fops/mops and also
 *    their _cbk functions, which later passes the call to next layer.
 *    Very helpful translator for debugging.
 */
#define TRACE_STAT_TO_STR(buf, str) trace_stat_to_str (buf, str, sizeof (str))

static void
trace_stat_to_str(struct iatt *buf, char *str, size_t len)
{
        char     atime_buf[256]    = {0,};
        char     mtime_buf[256]    = {0,};
        char     ctime_buf[256]    = {0,};

        if (!buf)
                return;

        gf_time_fmt (atime_buf, sizeof atime_buf, buf->ia_atime,
                     gf_timefmt_dirent);

        gf_time_fmt (mtime_buf, sizeof mtime_buf, buf->ia_mtime,
                     gf_timefmt_dirent);

        gf_time_fmt (ctime_buf, sizeof ctime_buf, buf->ia_ctime,
                     gf_timefmt_dirent);

        snprintf (str, len, "gfid=%s ino=%"PRIu64", mode=%o, "
                  "nlink=%"GF_PRI_NLINK", uid=%u, gid=%u, size=%"PRIu64", "
                  "blocks=%"PRIu64", atime=%s mtime=%s ctime=%s "
                  "atime_sec=%"PRIu32", atime_nsec=%"PRIu32","
                  " mtime_sec=%"PRIu32", mtime_nsec=%"PRIu32", "
                  "ctime_sec=%"PRIu32", ctime_nsec=%"PRIu32"",
                  uuid_utoa (buf->ia_gfid), buf->ia_ino,
                  st_mode_from_ia (buf->ia_prot, buf->ia_type), buf->ia_nlink,
                  buf->ia_uid, buf->ia_gid, buf->ia_size, buf->ia_blocks,
                  atime_buf, mtime_buf, ctime_buf,
                  buf->ia_atime, buf->ia_atime_nsec,
                  buf->ia_mtime, buf->ia_mtime_nsec,
                  buf->ia_ctime, buf->ia_ctime_nsec);
}


int
dump_history_trace (circular_buffer_t *cb, void *data)
{
        char     timestr[256] = {0,};

        /* Since we are continuing with adding entries to the buffer even when
           gettimeofday () fails, it's safe to check tm and then dump the time
           at which the entry was added to the buffer */

        gf_time_fmt (timestr, sizeof timestr, cb->tv.tv_sec, gf_timefmt_Ymd_T);
        snprintf (timestr + strlen (timestr), 256 - strlen (timestr),
                  ".%"GF_PRI_SUSECONDS, cb->tv.tv_usec);
        gf_proc_dump_write ("TIME", "%s", timestr);

        gf_proc_dump_write ("FOP", "%s\n", cb->data);

        return 0;
}

int
trace_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, fd_t *fd,
                  inode_t *inode, struct iatt *buf,
                  struct iatt *preparent, struct iatt *postparent,
                  dict_t *xdata)
{
        char          statstr[4096]       = {0, };
        char          preparentstr[4096]  = {0, };
        char          postparentstr[4096] = {0, };
        trace_conf_t  *conf               = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_CREATE].enabled) {
                char  string[4096] = {0,};
                if (op_ret >= 0) {
                        TRACE_STAT_TO_STR (buf, statstr);
                        TRACE_STAT_TO_STR (preparent, preparentstr);
                        TRACE_STAT_TO_STR (postparent, postparentstr);

                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s (op_ret=%d, fd=%p"
                                  "*stbuf {%s}, *preparent {%s}, "
                                  "*postparent = {%s})",
                                  frame->root->unique,
                                  uuid_utoa (inode->gfid), op_ret, fd,
                                  statstr, preparentstr, postparentstr);

                        /* for 'release' log */
                        fd_ctx_set (fd, this, 0);
                } else {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": (op_ret=%d, op_errno=%d)",
                                  frame->root->unique, op_ret,
                                  op_errno);
                }
                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (create, frame, op_ret, op_errno, fd, inode, buf,
                            preparent, postparent, xdata);
        return 0;
}

int
trace_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        trace_conf_t      *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_OPEN].enabled) {
                char     string[4096] = {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s op_ret=%d, op_errno=%d, "
                          "*fd=%p", frame->root->unique,
                          uuid_utoa (frame->local), op_ret, op_errno,
                          fd);

                LOG_ELEMENT (conf, string);
        }

out:
        /* for 'release' log */
        if (op_ret >= 0)
                fd_ctx_set (fd, this, 0);

        TRACE_STACK_UNWIND (open, frame, op_ret, op_errno, fd, xdata);
        return 0;
}

int
trace_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *buf,
                dict_t *xdata)
{
        char          statstr[4096] = {0, };
        trace_conf_t  *conf         = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_STAT].enabled) {
                char string[4096] = {0,};
                if (op_ret == 0) {
                        TRACE_STAT_TO_STR (buf, statstr);
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s op_ret=%d buf=%s",
                                  frame->root->unique,
                                  uuid_utoa (frame->local), op_ret,
                                  statstr);
                } else {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s op_ret=%d, "
                                  "op_errno=%d)",
                                  frame->root->unique,
                                  uuid_utoa (frame->local), op_ret,
                                  op_errno);
                }
                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (stat, frame, op_ret, op_errno, buf, xdata);
        return 0;
}

int
trace_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iovec *vector,
                 int32_t count, struct iatt *buf, struct iobref *iobref,
                 dict_t *xdata)
{
        char          statstr[4096] = {0, };
        trace_conf_t  *conf         = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_READ].enabled) {
                char  string[4096] = {0,};
                if (op_ret >= 0) {
                        TRACE_STAT_TO_STR (buf, statstr);
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s op_ret=%d buf=%s",
                                  frame->root->unique,
                                  uuid_utoa (frame->local), op_ret,
                                  statstr);
                } else {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s op_ret=%d, "
                                  "op_errno=%d)",
                                  frame->root->unique,
                                  uuid_utoa (frame->local), op_ret,
                                  op_errno);
                }
                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (readv, frame, op_ret, op_errno, vector, count,
                            buf, iobref, xdata);
        return 0;
}

int
trace_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        char         preopstr[4096]  = {0, };
        char         postopstr[4096] = {0, };
        trace_conf_t *conf           = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_WRITE].enabled) {
                char  string[4096] = {0,};
                if (op_ret >= 0) {
                        TRACE_STAT_TO_STR (prebuf, preopstr);
                        TRACE_STAT_TO_STR (postbuf, postopstr);

                        snprintf (string, sizeof (string),
                                  "%"PRId64": (op_ret=%d, "
                                  "*prebuf = {%s}, *postbuf = {%s})",
                                  frame->root->unique, op_ret,
                                  preopstr, postopstr);
                } else {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s op_ret=%d, "
                                  "op_errno=%d", frame->root->unique,
                                  uuid_utoa (frame->local), op_ret,
                                  op_errno);
                }
                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (writev, frame, op_ret, op_errno, prebuf, postbuf,
                            xdata);
        return 0;
}

int
trace_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, gf_dirent_t *buf,
                   dict_t *xdata)
{
        trace_conf_t  *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_READDIR].enabled) {
                char    string[4096]  = {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64" : gfid=%s op_ret=%d, op_errno=%d",
                          frame->root->unique, uuid_utoa (frame->local),
                          op_ret, op_errno);

                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (readdir, frame, op_ret, op_errno, buf, xdata);

        return 0;
}

int
trace_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, gf_dirent_t *buf,
                    dict_t *xdata)
{
        int             count         = 0;
        char            statstr[4096] = {0,};
        char            string[4096]  = {0,};
        trace_conf_t   *conf          = NULL;
        gf_dirent_t    *entry         = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_READDIRP].enabled) {
                snprintf (string, sizeof (string),
                          "%"PRId64" : gfid=%s op_ret=%d, op_errno=%d",
                          frame->root->unique, uuid_utoa (frame->local),
                          op_ret, op_errno);

                LOG_ELEMENT (conf, string);
        }
        if (op_ret < 0)
                goto out;

        list_for_each_entry (entry, &buf->list, list) {
                count++;
                TRACE_STAT_TO_STR (&entry->d_stat, statstr);
                snprintf (string, sizeof (string), "entry no. %d, pargfid=%s, "
                          "bname=%s *buf {%s}", count, uuid_utoa (frame->local),
                          entry->d_name, statstr);
                LOG_ELEMENT (conf, string);
        }

out:
        TRACE_STACK_UNWIND (readdirp, frame, op_ret, op_errno, buf, xdata);
        return 0;
}

int
trace_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        char          preopstr[4096]  = {0, };
        char          postopstr[4096] = {0, };
        trace_conf_t  *conf           = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_FSYNC].enabled) {
                char  string[4096] = {0,};
                if (op_ret == 0) {
                        TRACE_STAT_TO_STR (prebuf, preopstr);
                        TRACE_STAT_TO_STR (postbuf, postopstr);

                        snprintf (string, sizeof (string),
                                  "%"PRId64": (op_ret=%d, "
                                  "*prebuf = {%s}, *postbuf = {%s}",
                                  frame->root->unique, op_ret,
                                  preopstr, postopstr);
                } else {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s op_ret=%d, "
                                  "op_errno=%d", frame->root->unique,
                                  uuid_utoa (frame->local), op_ret,
                                  op_errno);

                }
                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (fsync, frame, op_ret, op_errno, prebuf, postbuf,
                            xdata);

        return 0;
}

int
trace_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno,
                   struct iatt *statpre, struct iatt *statpost, dict_t *xdata)
{
        char          preopstr[4096]  = {0, };
        char          postopstr[4096] = {0, };
        trace_conf_t  *conf           = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_SETATTR].enabled) {
                char  string[4096]  = {0,};
                if (op_ret == 0) {
                        TRACE_STAT_TO_STR (statpre, preopstr);
                        TRACE_STAT_TO_STR (statpost, postopstr);

                        snprintf (string, sizeof (string),
                                  "%"PRId64": (op_ret=%d, "
                                  "*prebuf = {%s}, *postbuf = {%s})",
                                  frame->root->unique, op_ret,
                                  preopstr, postopstr);
                } else {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s op_ret=%d, "
                                  "op_errno=%d)", frame->root->unique,
                                  uuid_utoa (frame->local), op_ret,
                                  op_errno);
                }
                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (setattr, frame, op_ret, op_errno, statpre,
                            statpost, xdata);
        return 0;
}

int
trace_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    struct iatt *statpre, struct iatt *statpost, dict_t *xdata)
{
        char          preopstr[4096]  = {0, };
        char          postopstr[4096] = {0, };
        trace_conf_t  *conf           = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_FSETATTR].enabled) {
                char  string[4096] = {0,};
                if (op_ret == 0) {
                        TRACE_STAT_TO_STR (statpre, preopstr);
                        TRACE_STAT_TO_STR (statpost, postopstr);

                        snprintf (string, sizeof (string),
                                  "%"PRId64": (op_ret=%d, "
                                  "*prebuf = {%s}, *postbuf = {%s})",
                                  frame->root->unique, op_ret,
                                  preopstr, postopstr);
                } else {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s op_ret=%d, op_errno=%d)",
                                  frame->root->unique, uuid_utoa (frame->local),
                                  op_ret, op_errno);
                }
                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (fsetattr, frame, op_ret, op_errno,
                            statpre, statpost, xdata);
        return 0;
}

int
trace_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  struct iatt *preparent, struct iatt *postparent,
                  dict_t *xdata)
{
        char          preparentstr[4096]  = {0, };
        char          postparentstr[4096] = {0, };
        trace_conf_t  *conf               = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_UNLINK].enabled) {
                char string[4096] = {0,};
                if (op_ret == 0) {
                        TRACE_STAT_TO_STR (preparent, preparentstr);
                        TRACE_STAT_TO_STR (postparent, postparentstr);

                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s op_ret=%d, "
                                  " *preparent = {%s}, "
                                  "*postparent = {%s})",
                                  frame->root->unique,
                                  uuid_utoa (frame->local),
                                  op_ret, preparentstr,
                                  postparentstr);
                } else {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s op_ret=%d, "
                                  "op_errno=%d)",
                                  frame->root->unique,
                                  uuid_utoa (frame->local), op_ret,
                                  op_errno);
                }
                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (unlink, frame, op_ret, op_errno,
                            preparent, postparent, xdata);
        return 0;
}

int
trace_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *buf,
                  struct iatt *preoldparent, struct iatt *postoldparent,
                  struct iatt *prenewparent, struct iatt *postnewparent,
                  dict_t *xdata)
{
        char           statstr[4096]          = {0, };
        char           preoldparentstr[4096]  = {0, };
        char           postoldparentstr[4096] = {0, };
        char           prenewparentstr[4096]  = {0, };
        char           postnewparentstr[4096] = {0, };
        trace_conf_t   *conf                  = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_RENAME].enabled) {
                char  string[4096] = {0,};
                if (op_ret == 0) {
                        TRACE_STAT_TO_STR (buf, statstr);
                        TRACE_STAT_TO_STR (preoldparent, preoldparentstr);
                        TRACE_STAT_TO_STR (postoldparent, postoldparentstr);
                        TRACE_STAT_TO_STR (prenewparent, prenewparentstr);
                        TRACE_STAT_TO_STR (postnewparent, postnewparentstr);

                        snprintf (string, sizeof (string),
                                  "%"PRId64": (op_ret=%d, "
                                  "*stbuf = {%s}, *preoldparent = {%s},"
                                  " *postoldparent = {%s}"
                                  " *prenewparent = {%s}, "
                                  "*postnewparent = {%s})",
                                  frame->root->unique, op_ret, statstr,
                                  preoldparentstr, postoldparentstr,
                                  prenewparentstr, postnewparentstr);
                } else {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s op_ret=%d, "
                                  "op_errno=%d", frame->root->unique,
                                  uuid_utoa (frame->local),
                                  op_ret, op_errno);

                }
                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (rename, frame, op_ret, op_errno, buf,
                            preoldparent, postoldparent,
                            prenewparent, postnewparent, xdata);
        return 0;
}

int
trace_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    const char *buf, struct iatt *stbuf, dict_t *xdata)
{
        char          statstr[4096] = {0, };
        trace_conf_t  *conf         = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_READLINK].enabled) {
                char string[4096] = {0,};
                if (op_ret == 0) {
                        TRACE_STAT_TO_STR (stbuf, statstr);
                        snprintf (string, sizeof (string),
                                  "%"PRId64": (op_ret=%d, op_errno=%d,"
                                  "buf=%s, stbuf = { %s })",
                                  frame->root->unique, op_ret, op_errno,
                                  buf, statstr);
                } else {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s op_ret=%d, "
                                  "op_errno=%d",
                                  frame->root->unique,
                                  uuid_utoa (frame->local), op_ret,
                                  op_errno);
                }

                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (readlink, frame, op_ret, op_errno, buf, stbuf,
                            xdata);
        return 0;
}

int
trace_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  inode_t *inode, struct iatt *buf,
                  dict_t *xdata, struct iatt *postparent)
{
        char          statstr[4096]       = {0, };
        char          postparentstr[4096] = {0, };
        trace_conf_t  *conf               = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_LOOKUP].enabled) {
                char  string[4096] = {0,};
                if (op_ret == 0) {
                        TRACE_STAT_TO_STR (buf, statstr);
                        TRACE_STAT_TO_STR (postparent, postparentstr);
                        /* print buf->ia_gfid instead of inode->gfid,
                         * since if the inode is not yet linked to the
                         * inode table (fresh lookup) then null gfid
                         * will be printed.
                         */
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s (op_ret=%d "
                                  "*buf {%s}, *postparent {%s}",
                                  frame->root->unique,
                                  uuid_utoa (buf->ia_gfid),
                                  op_ret, statstr, postparentstr);

                        /* For 'forget' */
                        inode_ctx_put (inode, this, 0);
                } else {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s op_ret=%d, "
                                  "op_errno=%d)",
                                  frame->root->unique,
                                  uuid_utoa (frame->local), op_ret,
                                  op_errno);
                }
                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (lookup, frame, op_ret, op_errno, inode, buf,
                            xdata, postparent);
        return 0;
}

int
trace_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno,
                   inode_t *inode, struct iatt *buf,
                   struct iatt *preparent, struct iatt *postparent,
                   dict_t *xdata)
{
        char          statstr[4096]       = {0, };
        char          preparentstr[4096]  = {0, };
        char          postparentstr[4096] = {0, };
        trace_conf_t  *conf               = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_SYMLINK].enabled) {
                char  string[4096] = {0,};
                if (op_ret == 0) {
                        TRACE_STAT_TO_STR (buf, statstr);
                        TRACE_STAT_TO_STR (preparent, preparentstr);
                        TRACE_STAT_TO_STR (postparent, postparentstr);

                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s (op_ret=%d "
                                  "*stbuf = {%s}, *preparent = {%s}, "
                                  "*postparent = {%s})",
                                  frame->root->unique,
                                  uuid_utoa (inode->gfid),
                                  op_ret, statstr, preparentstr,
                                  postparentstr);
                } else {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": op_ret=%d, op_errno=%d",
                                  frame->root->unique, op_ret,
                                  op_errno);
                }
                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (symlink, frame, op_ret, op_errno, inode, buf,
                            preparent, postparent, xdata);
        return 0;
}

int
trace_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 inode_t *inode, struct iatt *buf,
                 struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
        char          statstr[4096]       = {0, };
        char          preparentstr[4096]  = {0, };
        char          postparentstr[4096] = {0, };
        trace_conf_t  *conf               = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        char string[4096]  = {0,};
        if (trace_fop_names[GF_FOP_MKNOD].enabled) {
                if (op_ret == 0) {
                        TRACE_STAT_TO_STR (buf, statstr);
                        TRACE_STAT_TO_STR (preparent, preparentstr);
                        TRACE_STAT_TO_STR (postparent, postparentstr);

                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s (op_ret=%d "
                                  "*stbuf = {%s}, *preparent = {%s}, "
                                  "*postparent = {%s})",
                                  frame->root->unique,
                                  uuid_utoa (inode->gfid),
                                  op_ret, statstr, preparentstr,
                                  postparentstr);
                } else {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": (op_ret=%d, op_errno=%d)",
                                  frame->root->unique, op_ret,
                                  op_errno);
                }
                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (mknod, frame, op_ret, op_errno, inode, buf,
                            preparent, postparent, xdata);
        return 0;
}

int
trace_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 inode_t *inode, struct iatt *buf,
                 struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
        char          statstr[4096]       = {0, };
        char          preparentstr[4096]  = {0, };
        char          postparentstr[4096] = {0, };
        trace_conf_t  *conf               = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_MKDIR].enabled) {
                char  string[4096]  = {0,};
                if (op_ret == 0) {
                        TRACE_STAT_TO_STR (buf, statstr);
                        TRACE_STAT_TO_STR (preparent, preparentstr);
                        TRACE_STAT_TO_STR (postparent, postparentstr);

                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s (op_ret=%d "
                                  ", *stbuf = {%s}, *prebuf = {%s}, "
                                  "*postbuf = {%s} )",
                                  frame->root->unique,
                                  uuid_utoa (inode->gfid),
                                  op_ret, statstr, preparentstr,
                                  postparentstr);
                } else {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": (op_ret=%d, op_errno=%d)",
                                  frame->root->unique, op_ret,
                                  op_errno);
                }
                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (mkdir, frame, op_ret, op_errno, inode, buf,
                            preparent, postparent, xdata);
        return 0;
}

int
trace_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno,
                inode_t *inode, struct iatt *buf,
                struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
        char          statstr[4096]       = {0, };
        char          preparentstr[4096]  = {0, };
        char          postparentstr[4096] = {0, };
        trace_conf_t  *conf               = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        char  string[4096]  = {0,};
        if (trace_fop_names[GF_FOP_LINK].enabled) {
                if (op_ret == 0) {
                        TRACE_STAT_TO_STR (buf, statstr);
                        TRACE_STAT_TO_STR (preparent, preparentstr);
                        TRACE_STAT_TO_STR (postparent, postparentstr);

                        snprintf (string, sizeof (string),
                                  "%"PRId64": (op_ret=%d, "
                                  "*stbuf = {%s},  *prebuf = {%s},"
                                  " *postbuf = {%s})",
                                  frame->root->unique, op_ret,
                                  statstr, preparentstr, postparentstr);
                } else {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s op_ret=%d, "
                                  "op_errno=%d",
                                  frame->root->unique,
                                  uuid_utoa (frame->local),
                                  op_ret, op_errno);
                }
                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (link, frame, op_ret, op_errno, inode, buf,
                            preparent, postparent, xdata);
        return 0;
}

int
trace_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        char      string[4096] = {0,};
        if (trace_fop_names[GF_FOP_FLUSH].enabled) {
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                          frame->root->unique, uuid_utoa (frame->local),
                          op_ret, op_errno);

                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (flush, frame, op_ret, op_errno, xdata);
        return 0;
}

int
trace_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        char    string[4096] = {0,};
        if (trace_fop_names[GF_FOP_OPENDIR].enabled) {
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s op_ret=%d, op_errno=%d,"
                          " fd=%p",
                          frame->root->unique, uuid_utoa (frame->local),
                          op_ret, op_errno, fd);

                LOG_ELEMENT (conf, string);
        }
out:
        /* for 'releasedir' log */
        if (op_ret >= 0)
                fd_ctx_set (fd, this, 0);

        TRACE_STACK_UNWIND (opendir, frame, op_ret, op_errno, fd, xdata);
        return 0;
}

int
trace_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
        char           preparentstr[4096]  = {0, };
        char           postparentstr[4096] = {0, };
        trace_conf_t   *conf               = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_RMDIR].enabled) {
                char  string[4096] = {0,};
                if (op_ret == 0) {
                        TRACE_STAT_TO_STR (preparent, preparentstr);
                        TRACE_STAT_TO_STR (postparent, postparentstr);

                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s op_ret=%d, "
                                  "*prebuf={%s},  *postbuf={%s}",
                                  frame->root->unique,
                                  uuid_utoa (frame->local),
                                  op_ret, preparentstr, postparentstr);
                } else {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s op_ret=%d, "
                                  "op_errno=%d", frame->root->unique,
                                  uuid_utoa (frame->local),
                                  op_ret, op_errno);
                }
                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (rmdir, frame, op_ret, op_errno,
                            preparent, postparent, xdata);
        return 0;
}

int
trace_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        char           preopstr[4096]  = {0, };
        char           postopstr[4096] = {0, };
        trace_conf_t   *conf           = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_TRUNCATE].enabled) {
                char   string[4096] = {0,};
                if (op_ret == 0) {
                        TRACE_STAT_TO_STR (prebuf, preopstr);
                        TRACE_STAT_TO_STR (postbuf, postopstr);

                        snprintf (string, sizeof (string),
                                  "%"PRId64": (op_ret=%d, "
                                  "*prebuf = {%s}, *postbuf = {%s} )",
                                  frame->root->unique, op_ret,
                                  preopstr, postopstr);
                } else {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s op_ret=%d, "
                                  "op_errno=%d", frame->root->unique,
                                  uuid_utoa (frame->local), op_ret,
                                  op_errno);
                }
                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (truncate, frame, op_ret, op_errno, prebuf,
                            postbuf, xdata);
        return 0;
}

int
trace_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct statvfs *buf,
                  dict_t *xdata)
{
        trace_conf_t  *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_STATFS].enabled) {
                char   string[4096] = {0,};
                if (op_ret == 0) {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": ({f_bsize=%lu, "
                                  "f_frsize=%lu, "
                                  "f_blocks=%"GF_PRI_FSBLK
                                  ", f_bfree=%"GF_PRI_FSBLK", "
                                  "f_bavail=%"GF_PRI_FSBLK", "
                                  "f_files=%"GF_PRI_FSBLK", "
                                  "f_ffree=%"GF_PRI_FSBLK", "
                                  "f_favail=%"GF_PRI_FSBLK", "
                                  "f_fsid=%lu, f_flag=%lu, "
                                  "f_namemax=%lu}) => ret=%d",
                                  frame->root->unique, buf->f_bsize,
                                  buf->f_frsize, buf->f_blocks,
                                  buf->f_bfree, buf->f_bavail,
                                  buf->f_files, buf->f_ffree,
                                  buf->f_favail, buf->f_fsid,
                                  buf->f_flag, buf->f_namemax, op_ret);
                } else {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": (op_ret=%d, "
                                  "op_errno=%d)",
                                  frame->root->unique, op_ret,
                                  op_errno);
                }
                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (statfs, frame, op_ret, op_errno, buf, xdata);
        return 0;
}

int
trace_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        trace_conf_t     *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_SETXATTR].enabled) {
                char      string[4096] = {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                          frame->root->unique,
                          uuid_utoa (frame->local), op_ret,
                          op_errno);

                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (setxattr, frame, op_ret, op_errno, xdata);
        return 0;
}

int
trace_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *dict,
                    dict_t *xdata)
{
        trace_conf_t    *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_GETXATTR].enabled) {
                char       string[4096] = {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s op_ret=%d, op_errno=%d,"
                          " dict=%p", frame->root->unique,
                          uuid_utoa (frame->local), op_ret, op_errno,
                          dict);

                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (getxattr, frame, op_ret, op_errno, dict, xdata);

        return 0;
}

int
trace_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
                goto out;
        if (trace_fop_names[GF_FOP_FSETXATTR].enabled) {
                char    string[4096] = {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                          frame->root->unique,
                          uuid_utoa (frame->local), op_ret, op_errno);

                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (fsetxattr, frame, op_ret, op_errno, xdata);
        return 0;
}

int
trace_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *dict,
                     dict_t *xdata)
{
        trace_conf_t    *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_FGETXATTR].enabled) {
                char      string[4096]  = {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s op_ret=%d, op_errno=%d,"
                          " dict=%p", frame->root->unique,
                          uuid_utoa (frame->local), op_ret, op_errno,
                          dict);

                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (fgetxattr, frame, op_ret, op_errno, dict, xdata);

        return 0;
}

int
trace_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        trace_conf_t  *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_REMOVEXATTR].enabled) {
                char       string[4096] = {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                          frame->root->unique,
                          uuid_utoa (frame->local), op_ret, op_errno);

                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (removexattr, frame, op_ret, op_errno, xdata);

        return 0;
}

int
trace_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        trace_conf_t     *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_FSYNCDIR].enabled) {
                char       string[4096]   =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                          frame->root->unique,
                          uuid_utoa (frame->local), op_ret, op_errno);

                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (fsyncdir, frame, op_ret, op_errno, xdata);
        return 0;
}

int
trace_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_ACCESS].enabled) {
                char      string[4096] = {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s op_ret=%d, "
                          "op_errno=%d)", frame->root->unique,
                          uuid_utoa (frame->local), op_ret, op_errno);

                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (access, frame, op_ret, op_errno, xdata);
        return 0;
}

int
trace_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        char          prebufstr[4096]  = {0, };
        char          postbufstr[4096] = {0, };
        trace_conf_t  *conf            = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_FTRUNCATE].enabled) {
                char  string[4096]  = {0,};
                if (op_ret == 0) {
                        TRACE_STAT_TO_STR (prebuf, prebufstr);
                        TRACE_STAT_TO_STR (postbuf, postbufstr);

                        snprintf (string, sizeof (string),
                                  "%"PRId64": op_ret=%d, "
                                  "*prebuf = {%s}, *postbuf = {%s} )",
                                  frame->root->unique, op_ret,
                                  prebufstr, postbufstr);
                } else {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s op_ret=%d, "
                                  "op_errno=%d", frame->root->unique,
                                  uuid_utoa (frame->local), op_ret,
                                  op_errno);
                }
                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (ftruncate, frame, op_ret, op_errno, prebuf, postbuf,
                            xdata);
        return 0;
}

int
trace_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *buf, dict_t *xdata)
{
        char          statstr[4096] = {0, };
        trace_conf_t  *conf         = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_FSTAT].enabled) {
                char string[4096]  = {0.};
                if (op_ret == 0) {
                        TRACE_STAT_TO_STR (buf, statstr);
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s op_ret=%d "
                                  "buf=%s", frame->root->unique,
                                  uuid_utoa (frame->local), op_ret,
                                  statstr);
                } else {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s op_ret=%d, "
                                  "op_errno=%d", frame->root->unique,
                                  uuid_utoa (frame->local), op_ret,
                                  op_errno);
                }
                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (fstat, frame, op_ret, op_errno, buf, xdata);
        return 0;
}

int
trace_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, struct gf_flock *lock,
              dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_LK].enabled) {
                char      string[4096] = {0,};
                if (op_ret == 0) {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s op_ret=%d, "
                                  "{l_type=%d, l_whence=%d, "
                                  "l_start=%"PRId64", "
                                  "l_len=%"PRId64", l_pid=%u})",
                                  frame->root->unique,
                                  uuid_utoa (frame->local),
                                  op_ret, lock->l_type, lock->l_whence,
                                  lock->l_start, lock->l_len,
                                  lock->l_pid);
                } else {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s op_ret=%d, "
                                  "op_errno=%d)", frame->root->unique,
                                  uuid_utoa (frame->local), op_ret,
                                  op_errno);
                }

                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (lk, frame, op_ret, op_errno, lock, xdata);
        return 0;
}

int
trace_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        trace_conf_t  *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_ENTRYLK].enabled) {
                char   string[4096] = {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                          frame->root->unique,
                          uuid_utoa (frame->local), op_ret, op_errno);

                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (entrylk, frame, op_ret, op_errno, xdata);
        return 0;
}

int
trace_fentrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        trace_conf_t    *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_FENTRYLK].enabled) {
                char      string[4096] = {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                          frame->root->unique,
                          uuid_utoa (frame->local), op_ret, op_errno);

                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (fentrylk, frame, op_ret, op_errno, xdata);
        return 0;
}

int
trace_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, dict_t *dict,
                   dict_t *xdata)
{
        trace_conf_t  *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_XATTROP].enabled) {
                char    string[4096] = {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                          frame->root->unique,
                          uuid_utoa (frame->local), op_ret, op_errno);

                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (xattrop, frame, op_ret, op_errno, dict, xdata);
        return 0;
}

int
trace_fxattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *dict,
                    dict_t *xdata)
{
        trace_conf_t    *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_FXATTROP].enabled) {
                char      string[4096] = {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                          frame->root->unique,
                          uuid_utoa (frame->local), op_ret, op_errno);

                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (fxattrop, frame, op_ret, op_errno, dict, xdata);
        return 0;
}

int
trace_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_INODELK].enabled) {
                char       string[4096] = {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                          frame->root->unique,
                          uuid_utoa (frame->local),op_ret, op_errno);

                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (inodelk, frame, op_ret, op_errno, xdata);
        return 0;
}

int
trace_finodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_FINODELK].enabled) {
                char      string[4096] = {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s op_ret=%d, op_errno=%d",
                          frame->root->unique,
                          uuid_utoa (frame->local), op_ret, op_errno);

                LOG_ELEMENT (conf, string);
        }
out:
        TRACE_STACK_UNWIND (finodelk, frame, op_ret, op_errno, xdata);
        return 0;
}

int
trace_rchecksum_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     uint32_t weak_checksum, uint8_t *strong_checksum,
                     dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
                goto out;
        if (trace_fop_names[GF_FOP_RCHECKSUM].enabled) {
                char      string[4096] = {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s op_ret=%d op_errno=%d",
                          frame->root->unique,
                          uuid_utoa (frame->local), op_ret, op_errno);

                LOG_ELEMENT (conf, string);
        }

out:
        TRACE_STACK_UNWIND (rchecksum, frame, op_ret, op_errno, weak_checksum,
                            strong_checksum, xdata);

        return 0;
}

/* *_cbk section over <----------> fop section start */

int
trace_entrylk (call_frame_t *frame, xlator_t *this,
               const char *volume, loc_t *loc, const char *basename,
               entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_ENTRYLK].enabled) {
                char     string[4096] = {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s volume=%s, (path=%s "
                          "basename=%s, cmd=%s, type=%s)",
                          frame->root->unique,
                          uuid_utoa (loc->inode->gfid),
                          volume, loc->path, basename,
                          ((cmd == ENTRYLK_LOCK) ? "ENTRYLK_LOCK" :
                           "ENTRYLK_UNLOCK"),
                          ((type == ENTRYLK_RDLCK) ? "ENTRYLK_RDLCK" :
                           "ENTRYLK_WRLCK"));

                frame->local = loc->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_entrylk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->entrylk,
                    volume, loc, basename, cmd, type, xdata);
        return 0;
}

int
trace_inodelk (call_frame_t *frame, xlator_t *this, const char *volume,
               loc_t *loc, int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
        char         *cmd_str  = NULL;
        char         *type_str = NULL;
        trace_conf_t *conf     = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_INODELK].enabled) {
                char string[4096]  = {0,};
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

                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s volume=%s, (path=%s "
                          "cmd=%s, type=%s, start=%llu, len=%llu, "
                          "pid=%llu)", frame->root->unique,
                          uuid_utoa (loc->inode->gfid), volume,
                          loc->path, cmd_str, type_str,
                          (unsigned long long)flock->l_start,
                          (unsigned long long) flock->l_len,
                          (unsigned long long) flock->l_pid);

                frame->local = loc->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_inodelk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->inodelk,
                    volume, loc, cmd, flock, xdata);
        return 0;
}

int
trace_finodelk (call_frame_t *frame, xlator_t *this, const char *volume,
                fd_t *fd, int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
        char          *cmd_str  = NULL;
        char          *type_str = NULL;
        trace_conf_t  *conf     = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_FINODELK].enabled) {
                char  string[4096] = {0,};
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

                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s volume=%s, (fd =%p "
                          "cmd=%s, type=%s, start=%llu, len=%llu, "
                          "pid=%llu)", frame->root->unique,
                          uuid_utoa (fd->inode->gfid), volume, fd,
                          cmd_str, type_str,
                          (unsigned long long) flock->l_start,
                          (unsigned long long) flock->l_len,
                          (unsigned long long) flock->l_pid);

                frame->local = fd->inode->gfid;

                LOG_ELEMENT (conf, string);
        }
out:
        STACK_WIND (frame, trace_finodelk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->finodelk,
                    volume, fd, cmd, flock, xdata);
        return 0;
}

int
trace_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
               gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_XATTROP].enabled) {
                char    string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s (path=%s flags=%d)",
                          frame->root->unique,
                          uuid_utoa (loc->inode->gfid), loc->path,
                          flags);

                frame->local = loc->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_xattrop_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->xattrop,
                    loc, flags, dict, xdata);

        return 0;
}

int
trace_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
                gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
        trace_conf_t    *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_FXATTROP].enabled) {
                char    string[4096]  = {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s fd=%p, flags=%d",
                          frame->root->unique,
                          uuid_utoa (fd->inode->gfid), fd, flags);

                frame->local = fd->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_fxattrop_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fxattrop,
                    fd, flags, dict, xdata);

        return 0;
}

int
trace_lookup (call_frame_t *frame, xlator_t *this,
              loc_t *loc, dict_t *xdata)
{
        trace_conf_t *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_LOOKUP].enabled) {
                char string[4096] = {0,};
                /* TODO: print all the keys mentioned in xattr_req */
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s path=%s",
                          frame->root->unique,
                          uuid_utoa (loc->inode->gfid), loc->path);

                frame->local = loc->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_lookup_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup,
                    loc, xdata);

        return 0;
}

int
trace_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        trace_conf_t  *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
                goto out;
        if (trace_fop_names[GF_FOP_STAT].enabled) {
                char  string[4096]  = {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s path=%s",
                          frame->root->unique,
                          uuid_utoa (loc->inode->gfid), loc->path);

                frame->local = loc->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_stat_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->stat,
                    loc, xdata);

        return 0;
}

int
trace_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size,
                dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_READLINK].enabled) {
                char      string[4096] = {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s path=%s, "
                          "size=%"GF_PRI_SIZET")", frame->root->unique,
                          uuid_utoa (loc->inode->gfid), loc->path,
                          size);

                frame->local = loc->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_readlink_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readlink,
                    loc, size, xdata);

        return 0;
}

int
trace_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc,
             mode_t mode, dev_t dev, mode_t umask, dict_t *xdata)
{
        trace_conf_t  *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
                goto out;
        if (trace_fop_names[GF_FOP_MKNOD].enabled) {
                char     string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s path=%s mode=%d "
                          "umask=0%o, dev=%"GF_PRI_DEV")",
                          frame->root->unique,
                          uuid_utoa (loc->inode->gfid), loc->path,
                          mode, umask, dev);

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_mknod_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mknod,
                    loc, mode, dev, umask, xdata);

        return 0;
}

int
trace_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
             mode_t umask, dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_MKDIR].enabled) {
                char     string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s path=%s mode=%d"
                          " umask=0%o", frame->root->unique,
                          uuid_utoa (loc->inode->gfid), loc->path,
                          mode, umask);

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_mkdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mkdir,
                    loc, mode, umask, xdata);
        return 0;
}

int
trace_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
              dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_UNLINK].enabled) {
                char     string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s path=%s flag=%d",
                          frame->root->unique,
                          uuid_utoa (loc->inode->gfid), loc->path,
                          xflag);

                frame->local = loc->inode->gfid;

                LOG_ELEMENT (conf, string);
        }
out:
        STACK_WIND (frame, trace_unlink_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink,
                    loc, xflag, xdata);
        return 0;
}

int
trace_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
             dict_t *xdata)
{
        trace_conf_t    *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_RMDIR].enabled) {
                char     string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s path=%s flags=%d",
                          frame->root->unique,
                          uuid_utoa (loc->inode->gfid), loc->path,
                          flags);

                frame->local = loc->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_rmdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rmdir,
                    loc, flags, xdata);

        return 0;
}

int
trace_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
               loc_t *loc, mode_t umask, dict_t *xdata)
{
        trace_conf_t     *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_SYMLINK].enabled) {
                char     string[4096]   =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s linkpath=%s, path=%s"
                          " umask=0%o", frame->root->unique,
                          uuid_utoa (loc->inode->gfid), linkpath,
                          loc->path, umask);

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_symlink_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->symlink,
                    linkpath, loc, umask, xdata);

        return 0;
}

int
trace_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
              dict_t *xdata)
{
        char         oldgfid[50] = {0,};
        char         newgfid[50] = {0,};
        trace_conf_t *conf       = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_RENAME].enabled) {
                char string[4096] = {0,};
                if (newloc->inode)
                        uuid_utoa_r (newloc->inode->gfid, newgfid);
                else
                        strcpy (newgfid, "0");

                uuid_utoa_r (oldloc->inode->gfid, oldgfid);

                snprintf (string, sizeof (string),
                          "%"PRId64": oldgfid=%s oldpath=%s --> "
                          "newgfid=%s newpath=%s",
                          frame->root->unique, oldgfid,
                          oldloc->path, newgfid, newloc->path);

                frame->local = oldloc->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_rename_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rename,
                    oldloc, newloc, xdata);

        return 0;
}

int
trace_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
            dict_t *xdata)
{
        char         oldgfid[50] = {0,};
        char         newgfid[50] = {0,};
        trace_conf_t *conf       = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_LINK].enabled) {
                char string[4096]  = {0,};
                if (newloc->inode)
                        uuid_utoa_r (newloc->inode->gfid, newgfid);
                else
                        strcpy (newgfid, "0");

                uuid_utoa_r (oldloc->inode->gfid, oldgfid);

                snprintf (string, sizeof (string),
                          "%"PRId64": oldgfid=%s oldpath=%s --> "
                          "newgfid=%s newpath=%s", frame->root->unique,
                          oldgfid, oldloc->path, newgfid,
                          newloc->path);

                frame->local = oldloc->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_link_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->link,
                    oldloc, newloc, xdata);
        return 0;
}

int
trace_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
               struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        char         actime_str[256]  = {0,};
        char         modtime_str[256] = {0,};
        trace_conf_t *conf            = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_SETATTR].enabled) {
                char     string[4096]  =  {0,};
                if (valid & GF_SET_ATTR_MODE) {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s path=%s mode=%o)",
                                  frame->root->unique,
                                  uuid_utoa (loc->inode->gfid),
                                  loc->path,
                                  st_mode_from_ia (stbuf->ia_prot,
                                                   stbuf->ia_type));

                        LOG_ELEMENT (conf, string);
                        memset (string, 0 , sizeof (string));
                }

                if (valid & (GF_SET_ATTR_UID | GF_SET_ATTR_GID)) {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s path=%s uid=%o,"
                                  " gid=%o", frame->root->unique,
                                  uuid_utoa (loc->inode->gfid),
                                  loc->path, stbuf->ia_uid,
                                  stbuf->ia_gid);

                        LOG_ELEMENT (conf, string);
                        memset (string, 0 , sizeof (string));
                }

                if (valid & (GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME)) {
                        gf_time_fmt (actime_str, sizeof actime_str,
                                     stbuf->ia_atime, gf_timefmt_bdT);

                        gf_time_fmt (modtime_str, sizeof modtime_str,
                                     stbuf->ia_mtime, gf_timefmt_bdT);

                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s path=%s "
                                  "ia_atime=%s, ia_mtime=%s",
                                  frame->root->unique,
                                  uuid_utoa (loc->inode->gfid),
                                  loc->path, actime_str, modtime_str);

                        LOG_ELEMENT (conf, string);
                        memset (string, 0 , sizeof (string));
                }
                frame->local = loc->inode->gfid;
        }

out:
        STACK_WIND (frame, trace_setattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setattr,
                    loc, stbuf, valid, xdata);

        return 0;
}

int
trace_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        char           actime_str[256]  = {0,};
        char           modtime_str[256] = {0,};
        trace_conf_t   *conf            = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_FSETATTR].enabled) {
                char     string[4096]  =  {0,};
                if (valid & GF_SET_ATTR_MODE) {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s fd=%p, mode=%o",
                                  frame->root->unique,
                                  uuid_utoa (fd->inode->gfid), fd,
                                  st_mode_from_ia (stbuf->ia_prot,
                                                   stbuf->ia_type));

                        LOG_ELEMENT (conf, string);
                        memset (string, 0, sizeof (string));
                }

                if (valid & (GF_SET_ATTR_UID | GF_SET_ATTR_GID)) {
                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s fd=%p, uid=%o, "
                                  "gid=%o", frame->root->unique,
                                  uuid_utoa (fd->inode->gfid),
                                  fd, stbuf->ia_uid, stbuf->ia_gid);

                        LOG_ELEMENT (conf, string);
                        memset (string, 0, sizeof (string));
                }

                if (valid & (GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME)) {
                        gf_time_fmt (actime_str, sizeof actime_str,
                                     stbuf->ia_atime, gf_timefmt_bdT);

                        gf_time_fmt (modtime_str, sizeof modtime_str,
                                     stbuf->ia_mtime, gf_timefmt_bdT);

                        snprintf (string, sizeof (string),
                                  "%"PRId64": gfid=%s fd=%p "
                                  "ia_atime=%s, ia_mtime=%s",
                                  frame->root->unique,
                                  uuid_utoa (fd->inode->gfid),
                                  fd, actime_str, modtime_str);

                        LOG_ELEMENT (conf, string);
                        memset (string, 0, sizeof (string));
                }
                frame->local = fd->inode->gfid;
        }

out:
        STACK_WIND (frame, trace_fsetattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsetattr,
                    fd, stbuf, valid, xdata);

        return 0;
}

int
trace_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc,
                off_t offset, dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_TRUNCATE].enabled) {
                char     string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s path=%s, "
                          "offset=%"PRId64"", frame->root->unique,
                          uuid_utoa (loc->inode->gfid), loc->path,
                          offset);

                frame->local = loc->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_truncate_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->truncate,
                    loc, offset, xdata);

        return 0;
}

int
trace_open (call_frame_t *frame, xlator_t *this, loc_t *loc,
            int32_t flags, fd_t *fd, dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_OPEN].enabled) {
                char      string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s path=%s flags=%d fd=%p",
                          frame->root->unique,
                          uuid_utoa (loc->inode->gfid), loc->path,
                          flags, fd);

                frame->local = loc->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_open_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open,
                    loc, flags, fd, xdata);
        return 0;
}

int
trace_create (call_frame_t *frame, xlator_t *this, loc_t *loc,
              int32_t flags, mode_t mode, mode_t umask, fd_t *fd,
              dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_CREATE].enabled) {
                char    string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s path=%s, fd=%p, "
                          "flags=0%o mode=0%o umask=0%o",
                          frame->root->unique,
                          uuid_utoa (loc->inode->gfid), loc->path,
                          fd, flags, mode, umask);

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_create_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->create,
                    loc, flags, mode, umask, fd, xdata);
        return 0;
}

int
trace_readv (call_frame_t *frame, xlator_t *this, fd_t *fd,
             size_t size, off_t offset, uint32_t flags, dict_t *xdata)
{
        trace_conf_t  *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_READ].enabled) {
                char     string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s fd=%p, size=%"
                          GF_PRI_SIZET"offset=%"PRId64" flags=0%x)",
                          frame->root->unique,
                          uuid_utoa (fd->inode->gfid), fd, size,
                          offset, flags);

                frame->local = fd->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_readv_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readv,
                    fd, size, offset, flags, xdata);
        return 0;
}

int
trace_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
              struct iovec *vector, int32_t count,
              off_t offset, uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
        trace_conf_t    *conf = NULL;
        int i = 0;
        size_t total_size = 0;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_WRITE].enabled) {
                char     string[4096]  =  {0,};
                for (i = 0; i < count; i++)
                        total_size += vector[i].iov_len;

                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s fd=%p, count=%d, "
                          " offset=%"PRId64" flags=0%x write_size=%zu",
                          frame->root->unique,
                          uuid_utoa (fd->inode->gfid), fd, count,
                          offset, flags, total_size);

                frame->local = fd->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_writev_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev,
                    fd, vector, count, offset, flags, iobref, xdata);
        return 0;
}

int
trace_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        trace_conf_t  *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_STATFS].enabled) {
                char  string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s path=%s",
                          frame->root->unique, (loc->inode)?
                          uuid_utoa (loc->inode->gfid):"0", loc->path);

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_statfs_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->statfs,
                    loc, xdata);
        return 0;
}

int
trace_flush (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        trace_conf_t  *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_FLUSH].enabled) {
                char    string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s fd=%p",
                          frame->root->unique,
                          uuid_utoa (fd->inode->gfid), fd);

                frame->local = fd->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_flush_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->flush,
                    fd, xdata);
        return 0;
}

int
trace_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags,
             dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_FSYNC].enabled) {
                char     string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s flags=%d fd=%p",
                          frame->root->unique,
                          uuid_utoa (fd->inode->gfid), flags, fd);

                frame->local = fd->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_fsync_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsync,
                    fd, flags, xdata);
        return 0;
}

int
trace_setxattr (call_frame_t *frame, xlator_t *this,
                loc_t *loc, dict_t *dict, int32_t flags, dict_t *xdata)
{
        trace_conf_t    *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_SETXATTR].enabled) {
                char     string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s path=%s flags=%d",
                          frame->root->unique,
                          uuid_utoa (loc->inode->gfid), loc->path,
                          flags);

                frame->local = loc->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_setxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr,
                    loc, dict, flags, xdata);
        return 0;
}

int
trace_getxattr (call_frame_t *frame, xlator_t *this,
                loc_t *loc, const char *name, dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_GETXATTR].enabled) {
                char    string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s path=%s name=%s",
                          frame->root->unique,
                          uuid_utoa (loc->inode->gfid), loc->path,
                          name);

                frame->local = loc->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_getxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->getxattr,
                    loc, name, xdata);
        return 0;
}

int
trace_removexattr (call_frame_t *frame, xlator_t *this,
                   loc_t *loc, const char *name, dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_REMOVEXATTR].enabled) {
                char   string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s path=%s name=%s",
                          frame->root->unique,
                          uuid_utoa (loc->inode->gfid), loc->path,
                          name);

                frame->local = loc->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_removexattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->removexattr,
                    loc, name, xdata);

        return 0;
}

int
trace_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
               dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_OPENDIR].enabled) {
                char   string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s path=%s fd=%p",
                          frame->root->unique,
                          uuid_utoa (loc->inode->gfid), loc->path, fd);

                frame->local = loc->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_opendir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->opendir,
                    loc, fd, xdata);
        return 0;
}

int
trace_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                off_t offset, dict_t *dict)
{
        trace_conf_t *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_READDIRP].enabled) {
                char   string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s fd=%p, size=%"GF_PRI_SIZET
                          ", offset=%"PRId64" dict=%p",
                          frame->root->unique,
                          uuid_utoa (fd->inode->gfid), fd, size,
                          offset, dict);

                frame->local = fd->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_readdirp_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdirp,
                    fd, size, offset, dict);

        return 0;
}

int
trace_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd,
               size_t size, off_t offset, dict_t *xdata)
{
        trace_conf_t  *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_READDIR].enabled) {
                char   string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s fd=%p, size=%"GF_PRI_SIZET
                          ", offset=%"PRId64,
                          frame->root->unique,
                          uuid_utoa (fd->inode->gfid), fd, size,
                          offset);

                frame->local = fd->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_readdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdir,
                    fd, size, offset, xdata);

        return 0;
}

int
trace_fsyncdir (call_frame_t *frame, xlator_t *this,
                fd_t *fd, int32_t datasync, dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_FSYNCDIR].enabled) {
                char     string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s datasync=%d fd=%p",
                          frame->root->unique,
                          uuid_utoa (fd->inode->gfid), datasync, fd);

                frame->local = fd->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_fsyncdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsyncdir,
                    fd, datasync, xdata);
        return 0;
}

int
trace_access (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask,
              dict_t *xdata)
{
        trace_conf_t  *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_ACCESS].enabled) {
                char     string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s path=%s mask=0%o",
                          frame->root->unique,
                          uuid_utoa (loc->inode->gfid),
                          loc->path, mask);

                frame->local = loc->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_access_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->access,
                    loc, mask, xdata);
        return 0;
}

int32_t
trace_rchecksum (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                 int32_t len, dict_t *xdata)
{

        trace_conf_t *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_RCHECKSUM].enabled) {
                char    string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s offset=%"PRId64
                          "len=%u fd=%p", frame->root->unique,
                          uuid_utoa (fd->inode->gfid), offset, len, fd);

                frame->local = fd->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_rchecksum_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rchecksum,
                    fd, offset, len, xdata);

        return 0;

}

int32_t
trace_fentrylk (call_frame_t *frame, xlator_t *this, const char *volume,
                fd_t *fd, const char *basename, entrylk_cmd cmd,
                entrylk_type type, dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_FENTRYLK].enabled) {
                char      string[4096]   =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s volume=%s, (fd=%p "
                          "basename=%s, cmd=%s, type=%s)",
                          frame->root->unique,
                          uuid_utoa (fd->inode->gfid), volume, fd,
                          basename,
                          ((cmd == ENTRYLK_LOCK) ? "ENTRYLK_LOCK" :
                           "ENTRYLK_UNLOCK"),
                          ((type == ENTRYLK_RDLCK) ? "ENTRYLK_RDLCK" :
                           "ENTRYLK_WRLCK"));

                frame->local = fd->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_fentrylk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fentrylk,
                    volume, fd, basename, cmd, type, xdata);
        return 0;

}

int32_t
trace_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 const char *name, dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
                goto out;
        if (trace_fop_names[GF_FOP_FGETXATTR].enabled) {
                char     string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s fd=%p name=%s",
                          frame->root->unique,
                          uuid_utoa (fd->inode->gfid), fd, name);

                frame->local = fd->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_fgetxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fgetxattr,
                    fd, name, xdata);
        return 0;
}

int32_t
trace_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 dict_t *dict, int32_t flags, dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_FSETXATTR].enabled) {
                char   string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s fd=%p flags=%d",
                          frame->root->unique,
                          uuid_utoa (fd->inode->gfid), fd, flags);

                frame->local = fd->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_fsetxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsetxattr,
                    fd, dict, flags, xdata);
        return 0;
}

int
trace_ftruncate (call_frame_t *frame, xlator_t *this,
                 fd_t *fd, off_t offset, dict_t *xdata)
{
        trace_conf_t  *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_FTRUNCATE].enabled) {
                char    string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s offset=%"PRId64" fd=%p",
                          frame->root->unique,
                          uuid_utoa (fd->inode->gfid), offset, fd);

                frame->local = fd->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_ftruncate_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->ftruncate,
                    fd, offset, xdata);

        return 0;
}

int
trace_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_FSTAT].enabled) {
                char   string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s fd=%p",
                          frame->root->unique,
                          uuid_utoa (fd->inode->gfid), fd);

                frame->local = fd->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_fstat_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fstat,
                    fd, xdata);
        return 0;
}

int
trace_lk (call_frame_t *frame, xlator_t *this, fd_t *fd,
          int32_t cmd, struct gf_flock *lock, dict_t *xdata)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_LK].enabled) {
                char     string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "%"PRId64": gfid=%s fd=%p, cmd=%d, "
                          "lock {l_type=%d, "
                          "l_whence=%d, l_start=%"PRId64", "
                          "l_len=%"PRId64", l_pid=%u})",
                          frame->root->unique,
                          uuid_utoa (fd->inode->gfid), fd, cmd,
                          lock->l_type, lock->l_whence,
                          lock->l_start, lock->l_len, lock->l_pid);

                frame->local = fd->inode->gfid;

                LOG_ELEMENT (conf, string);
        }

out:
        STACK_WIND (frame, trace_lk_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lk,
                    fd, cmd, lock, xdata);
        return 0;
}

int32_t
trace_forget (xlator_t *this, inode_t *inode)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;
        /* If user want to understand when a lookup happens,
           he should know about 'forget' too */
        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_LOOKUP].enabled) {
                char   string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "gfid=%s", uuid_utoa (inode->gfid));

                LOG_ELEMENT (conf, string);
        }

out:
        return 0;
}

int32_t
trace_releasedir (xlator_t *this, fd_t *fd)
{
        trace_conf_t  *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_OPENDIR].enabled) {
                char   string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "gfid=%s fd=%p",
                          uuid_utoa (fd->inode->gfid), fd);

                LOG_ELEMENT (conf, string);
        }

out:
        return 0;
}

int32_t
trace_release (xlator_t *this, fd_t *fd)
{
        trace_conf_t   *conf = NULL;

        conf = this->private;

        if (!conf->log_file && !conf->log_history)
		goto out;
        if (trace_fop_names[GF_FOP_OPEN].enabled ||
            trace_fop_names[GF_FOP_CREATE].enabled) {
                char   string[4096]  =  {0,};
                snprintf (string, sizeof (string),
                          "gfid=%s fd=%p",
                          uuid_utoa (fd->inode->gfid), fd);

                LOG_ELEMENT (conf, string);
        }

out:
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
trace_dump_history (xlator_t *this)
{
        int ret = -1;
        char key_prefix[GF_DUMP_MAX_BUF_LEN] = {0,};
        trace_conf_t *conf = NULL;

        GF_VALIDATE_OR_GOTO ("trace", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->history, out);

        conf = this->private;
        // Is it ok to return silently if log-history option his off?
        if (conf && conf->log_history == _gf_true) {
                gf_proc_dump_build_key (key_prefix, "xlator.debug.trace",
                                        "history");
                gf_proc_dump_add_section (key_prefix);
                eh_dump (this->history, NULL, dump_history_trace);
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

        ret = xlator_mem_acct_init (this, gf_trace_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
                        " failed");
                return ret;
        }

        return ret;
}

int
reconfigure (xlator_t *this, dict_t *options)
{
        int32_t       ret            = -1;
        trace_conf_t    *conf           = NULL;
        char            *includes = NULL, *excludes = NULL;

        GF_VALIDATE_OR_GOTO ("quick-read", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);
        GF_VALIDATE_OR_GOTO (this->name, options, out);

        conf = this->private;

        includes = data_to_str (dict_get (options, "include-ops"));
        excludes = data_to_str (dict_get (options, "exclude-ops"));

        {
                int i;
                for (i = 0; i < GF_FOP_MAXVALUE; i++) {
                        if (gf_fop_list[i])
                                strncpy (trace_fop_names[i].name,
                                         gf_fop_list[i],
                                         strlen (gf_fop_list[i]));
                        else
                                strncpy (trace_fop_names[i].name, ":O",
                                         strlen (":O"));
                        trace_fop_names[i].enabled = 1;
                }
        }

        if (includes && excludes) {
                gf_log (this->name,
                        GF_LOG_ERROR,
                        "must specify only one of 'include-ops' and "
                        "'exclude-ops'");
                goto out;
        }

        if (includes)
                process_call_list (includes, 1);
        if (excludes)
                process_call_list (excludes, 0);

        /* Should resizing of the event-history be allowed in reconfigure?
         * for which a new event_history might have to be allocated and the
         * older history has to be freed.
         */
        GF_OPTION_RECONF ("log-file", conf->log_file, options, bool, out);

        GF_OPTION_RECONF ("log-history", conf->log_history, options, bool, out);

        ret = 0;

out:
        return ret;
}

int32_t
init (xlator_t *this)
{
        dict_t *options = NULL;
        char *includes = NULL, *excludes = NULL;
        char *forced_loglevel = NULL;
        eh_t *history = NULL;
        int  ret = -1;
        size_t  history_size = TRACE_DEFAULT_HISTORY_SIZE;
        trace_conf_t    *conf = NULL;

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

        conf = GF_CALLOC (1, sizeof (trace_conf_t), gf_trace_mt_trace_conf_t);
        if (!conf) {
                gf_log (this->name, GF_LOG_ERROR, "cannot allocate "
                        "xl->private");
                return -1;
        }

        options = this->options;
        includes = data_to_str (dict_get (options, "include-ops"));
        excludes = data_to_str (dict_get (options, "exclude-ops"));

        {
                int i;
                for (i = 0; i < GF_FOP_MAXVALUE; i++) {
                        if (gf_fop_list[i])
                                strncpy (trace_fop_names[i].name,
                                         gf_fop_list[i],
                                         strlen (gf_fop_list[i]));
                        else
                                strncpy (trace_fop_names[i].name, ":O",
                                         strlen (":O"));
                        trace_fop_names[i].enabled = 1;
                }
        }

        if (includes && excludes) {
                gf_log (this->name,
                        GF_LOG_ERROR,
                        "must specify only one of 'include-ops' and "
                        "'exclude-ops'");
                return -1;
        }

        if (includes)
                process_call_list (includes, 1);
        if (excludes)
                process_call_list (excludes, 0);


        GF_OPTION_INIT ("history-size", conf->history_size, size, out);

        gf_log (this->name, GF_LOG_INFO, "history size %"GF_PRI_SIZET,
                history_size);

        GF_OPTION_INIT ("log-file", conf->log_file, bool, out);

        gf_log (this->name, GF_LOG_INFO, "logging to file %s",
                (conf->log_file == _gf_true)?"enabled":"disabled");

        GF_OPTION_INIT ("log-history", conf->log_history, bool, out);

        gf_log (this->name, GF_LOG_DEBUG, "logging to history %s",
                (conf->log_history == _gf_true)?"enabled":"disabled");

        history = eh_new (history_size, _gf_false, NULL);
        if (!history) {
                gf_log (this->name, GF_LOG_ERROR, "event history cannot be "
                        "initialized");
                return -1;
        }

        this->history = history;

        conf->trace_log_level = GF_LOG_INFO;

        if (dict_get (options, "force-log-level")) {
                forced_loglevel = data_to_str (dict_get (options,
                                                         "force-log-level"));
                if (!forced_loglevel)
                        goto setloglevel;

                if (strcmp (forced_loglevel, "INFO") == 0)
                        conf->trace_log_level = GF_LOG_INFO;
                else if (strcmp (forced_loglevel, "TRACE") == 0)
                        conf->trace_log_level = GF_LOG_TRACE;
                else if (strcmp (forced_loglevel, "ERROR") == 0)
                        conf->trace_log_level = GF_LOG_ERROR;
                else if (strcmp (forced_loglevel, "DEBUG") == 0)
                        conf->trace_log_level = GF_LOG_DEBUG;
                else if (strcmp (forced_loglevel, "WARNING") == 0)
                        conf->trace_log_level = GF_LOG_WARNING;
                else if (strcmp (forced_loglevel, "CRITICAL") == 0)
                        conf->trace_log_level = GF_LOG_CRITICAL;
                else if (strcmp (forced_loglevel, "NONE") == 0)
                        conf->trace_log_level = GF_LOG_NONE;
        }

setloglevel:
        gf_log_set_loglevel (conf->trace_log_level);
        this->private = conf;
        ret = 0;
out:
        if (ret == -1) {
                if (history)
                        GF_FREE (history);
                if (conf)
                        GF_FREE (conf);
        }

        return ret;
}

void
fini (xlator_t *this)
{
        if (!this)
                return;

        if (this->history)
                eh_destroy (this->history);

        gf_log (this->name, GF_LOG_INFO,
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
        { .key  = {"history-size"},
          .type = GF_OPTION_TYPE_SIZET,
          .default_value = "1024",
        },
        { .key  = {"log-file"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "no",
        },
        { .key  = {"log-history"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "no",
        },
        { .key  = {NULL} },
};

struct xlator_dumpops dumpops = {
        .history = trace_dump_history
};
