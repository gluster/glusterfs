/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <libgen.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <stdlib.h>
#include <signal.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "afr.h"
#include "dict.h"
#include "xlator.h"
#include "hashfn.h"
#include "logging.h"
#include "stack.h"
#include "list.h"
#include "call-stub.h"
#include "defaults.h"
#include "common-utils.h"
#include "compat-errno.h"
#include "compat.h"
#include "byte-order.h"

#include "afr-transaction.h"
#include "afr-self-heal.h"
#include "afr-self-heal-common.h"
#include "afr-self-heal-algorithm.h"

int
afr_sh_data_fail (call_frame_t *frame, xlator_t *this);

static inline gf_boolean_t
afr_sh_data_proceed (unsigned int success_count)
{
        return (success_count >= AFR_SH_MIN_PARTICIPANTS);
}

extern int
sh_loop_finish (call_frame_t *loop_frame, xlator_t *this);

int
afr_post_sh_big_lock_success (call_frame_t *frame, xlator_t *this);

int
afr_post_sh_big_lock_failure (call_frame_t *frame, xlator_t *this);

int
afr_sh_data_finish (call_frame_t *frame, xlator_t *this);

int
afr_sh_data_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;

        local = frame->local;
        sh = &local->self_heal;

        sh->completion_cbk (frame, this);

        return 0;
}


int
afr_sh_data_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        afr_local_t   *local       = NULL;
        afr_private_t *priv        = NULL;
        int            call_count  = 0;
        int            child_index = (long) cookie;

        local = frame->local;
        priv = this->private;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "flush failed on %s on subvolume %s: %s",
                                local->loc.path, priv->children[child_index]->name,
                                strerror (op_errno));
                }
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                afr_sh_data_done (frame, this);
        }

        return 0;
}

int
afr_sh_data_close (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local      = NULL;
        afr_private_t   *priv       = NULL;
        afr_self_heal_t *sh         = NULL;
        int              i          = 0;
        int              call_count = 0;

        local = frame->local;
        sh    = &local->self_heal;
        priv  = this->private;

        if (!sh->healing_fd) {
                //This happens when file is non-reg
                afr_sh_data_done (frame, this);
                return 0;
        }
        call_count        = afr_set_elem_count_get (sh->success,
                                                    priv->child_count);
        local->call_count = call_count;

        if (call_count == 0) {
                afr_sh_data_done (frame, this);
                return 0;
        }

        for (i = 0; i < priv->child_count; i++) {
                if (!sh->success[i])
                        continue;
                gf_log (this->name, GF_LOG_DEBUG,
                        "closing fd of %s on %s",
                        local->loc.path, priv->children[i]->name);

                STACK_WIND_COOKIE (frame, afr_sh_data_flush_cbk,
                                   (void *) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->flush,
                                   sh->healing_fd, NULL);

                if (!--call_count)
                        break;
        }

        return 0;
}

int
afr_sh_dom_unlock (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh    = NULL;
        afr_private_t   *priv  = NULL;

        local = frame->local;
        sh    = &local->self_heal;
        priv  = this->private;

        if (sh->sh_dom_lock_held)
                afr_sh_data_unlock (frame, this, priv->sh_domain,
                                    afr_sh_data_close);
        else
                afr_sh_data_close (frame, this);
        return 0;
}

int
afr_sh_data_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                         struct iatt *statpost, dict_t *xdata)
{

        afr_local_t   *local       = NULL;
        afr_private_t *priv        = NULL;
        int            call_count  = 0;
        int            child_index = (long) cookie;

        local = frame->local;
        priv = this->private;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_INFO,
                                "setattr failed on %s on subvolume %s: %s",
                                local->loc.path, priv->children[child_index]->name,
                                strerror (op_errno));
                }
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                afr_sh_data_finish (frame, this);
        }

        return 0;
}

int
afr_sh_data_setattr (call_frame_t *frame, xlator_t *this, struct iatt* stbuf)
{
        afr_local_t     *local      = NULL;
        afr_private_t   *priv       = NULL;
        afr_self_heal_t *sh         = NULL;
        int              i          = 0;
        int              call_count = 0;
        int32_t          valid      = 0;

        local = frame->local;
        sh    = &local->self_heal;
        priv  = this->private;

        valid = (GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME);

        call_count        = afr_set_elem_count_get (sh->success,
                                                    priv->child_count);
        local->call_count = call_count;

        if (call_count == 0) {
                GF_ASSERT (0);
                afr_sh_data_finish (frame, this);
                return 0;
        }

        for (i = 0; i < priv->child_count; i++) {
                if (!sh->success[i])
                        continue;

                STACK_WIND_COOKIE (frame, afr_sh_data_setattr_cbk,
                                   (void *) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->setattr,
                                   &local->loc, stbuf, valid, NULL);

                if (!--call_count)
                        break;
        }

        return 0;
}

int
afr_sh_data_setattr_fstat_cbk (call_frame_t *frame, void *cookie,
                               xlator_t *this, int32_t op_ret, int32_t op_errno,
                               struct iatt *buf, dict_t *xdata)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        int child_index = (long) cookie;

        local = frame->local;
        sh = &local->self_heal;

        GF_ASSERT (sh->source == child_index);
        if (op_ret != -1) {
                sh->buf[child_index] = *buf;
                afr_sh_data_setattr (frame, this, buf);
        } else {
                gf_log (this->name, GF_LOG_ERROR, "%s: Failed to set "
                        "time-stamps after self-heal", local->loc.path);
                afr_sh_data_fail (frame, this);
        }

        return 0;
}

/*
 * If there are any writes after the self-heal is triggered then the
 * stbuf stored in local->self_heal.buf[] will be invalid so we do one more
 * stat on the source and then set the [am]times
 */
int
afr_sh_set_timestamps (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local      = NULL;
        afr_private_t   *priv       = NULL;
        afr_self_heal_t *sh         = NULL;

        local = frame->local;
        sh    = &local->self_heal;
        priv  = this->private;

        STACK_WIND_COOKIE (frame, afr_sh_data_setattr_fstat_cbk,
                           (void *) (long) sh->source,
                           priv->children[sh->source],
                           priv->children[sh->source]->fops->fstat,
                           sh->healing_fd, NULL);
        return 0;
}

//Fun fact, lock_cbk is being used for both lock & unlock
int
afr_sh_data_unlock (call_frame_t *frame, xlator_t *this, char *dom,
                    afr_lock_cbk_t lock_cbk)
{
        afr_local_t         *local    = NULL;
        afr_internal_lock_t *int_lock = NULL;
        afr_self_heal_t     *sh       = NULL;
        afr_private_t       *priv     = NULL;
        int                 ret       = 0;

        local    = frame->local;
        int_lock = &local->internal_lock;
        sh       = &local->self_heal;
        priv     = this->private;

        if (strcmp (dom, this->name) == 0) {
                sh->data_lock_held = _gf_false;
        } else if (strcmp (dom, priv->sh_domain) == 0) {
                sh->sh_dom_lock_held = _gf_false;
        } else {
                ret = -1;
                goto out;
        }
        int_lock->lock_cbk = lock_cbk;
        int_lock->domain = dom;
        afr_unlock (frame, this);

out:
        if (ret) {
                int_lock->lock_op_ret = -1;
                int_lock->lock_cbk (frame, this);
        }
        return 0;
}

int
afr_sh_data_finish (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh    = NULL;

        local = frame->local;
        sh = &local->self_heal;

        gf_log (this->name, GF_LOG_DEBUG,
                "finishing data selfheal of %s", local->loc.path);

        if (sh->data_lock_held)
                afr_sh_data_unlock (frame, this, this->name, afr_sh_dom_unlock);
        else
                afr_sh_dom_unlock (frame, this);

        return 0;
}

int
afr_sh_data_fail (call_frame_t *frame, xlator_t *this)
{
        afr_local_t   *local = NULL;
        afr_self_heal_t *sh = NULL;

        local = frame->local;
        sh = &local->self_heal;

        gf_log (this->name, GF_LOG_DEBUG,
                "finishing failed data selfheal of %s", local->loc.path);

        afr_set_self_heal_status (sh, AFR_SELF_HEAL_FAILED);
        afr_sh_data_finish (frame, this);
        return 0;
}

int
afr_sh_data_erase_pending_cbk (call_frame_t *frame, void *cookie,
                               xlator_t *this, int32_t op_ret,
                               int32_t op_errno, dict_t *xattr, dict_t *xdata)
{
        int             call_count = 0;
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;
        int32_t         child_index = (long) cookie;

        priv  = this->private;
        local = frame->local;
        sh    = &local->self_heal;
        if (op_ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "Erasing of pending change "
                        "log failed on %s for subvol %s, reason: %s",
                        local->loc.path, priv->children[child_index]->name,
                        strerror (op_errno));
                afr_set_self_heal_status (sh, AFR_SELF_HEAL_FAILED);
        }

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                if (is_self_heal_failed (sh, AFR_CHECK_SPECIFIC)) {
                        if (sh->old_loop_frame)
                                sh_loop_finish (sh->old_loop_frame, this);
                        sh->old_loop_frame = NULL;
                        afr_sh_data_fail (frame, this);
                        goto out;
                }
                if (!IA_ISREG (sh->type)) {
                        afr_sh_data_finish (frame, this);
                        goto out;
                }
                GF_ASSERT (sh->old_loop_frame);
                afr_sh_data_lock (frame, this, 0, 0, _gf_true, this->name,
                                  afr_post_sh_big_lock_success,
                                  afr_post_sh_big_lock_failure);
        }
out:
        return 0;
}

int
afr_sh_data_erase_pending (call_frame_t *frame, xlator_t *this)
{
        afr_sh_erase_pending (frame, this, AFR_DATA_TRANSACTION,
                              afr_sh_data_erase_pending_cbk,
                              afr_sh_data_finish);
        return 0;
}

int
afr_sh_data_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int op_ret, int op_errno, struct iatt *pre,
                       struct iatt *post, dict_t *xdata)
{
        afr_local_t     *local      = NULL;
        afr_private_t   *priv       = NULL;
        afr_self_heal_t *sh         = NULL;
        int             call_count  = 0;
        int             child_index = (long) cookie;

        local = frame->local;
        priv = this->private;
        sh   = &local->self_heal;

        if (op_ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "%s: Failed to fsync on "
                        "%s - %s", local->loc.path,
                        priv->children[child_index]->name, strerror (op_errno));
                LOCK (&frame->lock);
                {
                        afr_set_self_heal_status (sh, AFR_SELF_HEAL_FAILED);
                }
                UNLOCK (&frame->lock);
                if (sh->old_loop_frame)
                        sh_loop_finish (sh->old_loop_frame, this);
                sh->old_loop_frame = NULL;
        }

        call_count = afr_frame_return (frame);
        if (call_count == 0) {
                if (is_self_heal_failed (sh, AFR_CHECK_SPECIFIC))
                        afr_sh_data_fail (frame, this);
                else
                        afr_sh_data_erase_pending (frame, this);
        }
        return 0;
}

/*
 * Before erasing xattrs, make sure the data is written to disk
 */
int
afr_sh_data_fsync (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local     = NULL;
        afr_private_t   *priv      = NULL;
        afr_self_heal_t *sh        = NULL;
        int             i          = 0;
        int             call_count = 0;

        local = frame->local;
        priv = this->private;
        sh   = &local->self_heal;

        call_count        = sh->active_sinks;
        if (call_count == 0) {
                afr_sh_data_erase_pending (frame, this);
                return 0;
        }

        local->call_count = call_count;
        for (i = 0; i < priv->child_count; i++) {
                if (!sh->success[i] || sh->sources[i])
                        continue;

                STACK_WIND_COOKIE (frame, afr_sh_data_fsync_cbk,
                                   (void *) (long) i, priv->children[i],
                                   priv->children[i]->fops->fsync,
                                   sh->healing_fd, 1, NULL);
        }

        return 0;
}

static struct afr_sh_algorithm *
sh_algo_from_name (xlator_t *this, char *name)
{
        int i = 0;

        if (name == NULL)
                goto out;

        while (afr_self_heal_algorithms[i].name) {
                if (!strcmp (name, afr_self_heal_algorithms[i].name)) {
                        return &afr_self_heal_algorithms[i];
                }

                i++;
        }

out:
        return NULL;
}


static int
sh_zero_byte_files_exist (afr_local_t *local, int child_count)
{
        int             i = 0;
        int             ret = 0;
        afr_self_heal_t *sh = NULL;

        sh = &local->self_heal;
        for (i = 0; i < child_count; i++) {
                if (!local->child_up[i] || sh->child_errno[i])
                        continue;
                if (sh->buf[i].ia_size == 0) {
                        ret = 1;
                        break;
                }
        }

        return ret;
}


struct afr_sh_algorithm *
afr_sh_data_pick_algo (call_frame_t *frame, xlator_t *this)
{
        afr_private_t *           priv  = NULL;
        struct afr_sh_algorithm * algo  = NULL;
        afr_local_t *             local = NULL;
        afr_self_heal_t *         sh    = NULL;

        priv  = this->private;
        local = frame->local;
        sh    = &local->self_heal;
        algo  = sh_algo_from_name (this, priv->data_self_heal_algorithm);

        if (algo == NULL) {
                /* option not set, so fall back on heuristics */

                if (sh_zero_byte_files_exist (local, priv->child_count)
                    || (sh->file_size <= (priv->data_self_heal_window_size *
                                          this->ctx->page_size))) {

                        /*
                         * If the file does not exist on one of the subvolumes,
                         * or a zero-byte file exists (created by entry self-heal)
                         * the entire content has to be copied anyway, so there
                         * is no benefit from using the "diff" algorithm.
                         *
                         * If the file size is about the same as page size,
                         * the entire file can be read and written with a few
                         * (pipelined) STACK_WINDs, which will be faster
                         * than "diff" which has to read checksums and then
                         * read and write.
                         */

                        algo = sh_algo_from_name (this, "full");

                } else {
                        algo = sh_algo_from_name (this, "diff");
                }
        }

        return algo;
}


int
afr_sh_data_sync_prepare (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        struct afr_sh_algorithm *sh_algo = NULL;

        local = frame->local;
        sh = &local->self_heal;

        sh->algo_completion_cbk = afr_sh_data_fsync;
        sh->algo_abort_cbk      = afr_sh_data_fail;

        sh_algo = afr_sh_data_pick_algo (frame, this);

        sh->algo = sh_algo;
        sh_algo->fn (frame, this);

        return 0;
}

int
afr_sh_data_trim_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                      struct iatt *postbuf, dict_t *xdata)
{
        int              call_count = 0;
        int              child_index = 0;
        afr_private_t    *priv = NULL;
        afr_local_t      *local  = NULL;
        afr_self_heal_t  *sh = NULL;

        priv  = this->private;
        local = frame->local;
        sh    = &local->self_heal;

        child_index = (long) cookie;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "ftruncate of %s on subvolume %s failed (%s)",
                                local->loc.path,
                                priv->children[child_index]->name,
                                strerror (op_errno));
                        afr_set_self_heal_status (sh, AFR_SELF_HEAL_FAILED);
                } else {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "ftruncate of %s on subvolume %s completed",
                                local->loc.path,
                                priv->children[child_index]->name);
                }
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                if (is_self_heal_failed (sh, AFR_CHECK_SPECIFIC))
                        afr_sh_data_fail (frame, this);
                else
                        afr_sh_data_sync_prepare (frame, this);
        }

        return 0;
}


int
afr_sh_data_trim_sinks (call_frame_t *frame, xlator_t *this)
{
        afr_private_t * priv = NULL;
        afr_local_t * local  = NULL;
        afr_self_heal_t *sh  = NULL;
        int             *sources = NULL;
        int              call_count = 0;
        int              i = 0;


        priv = this->private;
        local = frame->local;
        sh = &local->self_heal;

        sources = sh->sources;
        call_count = sh->active_sinks;

        local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (sources[i] || !local->child_up[i])
                        continue;

                STACK_WIND_COOKIE (frame, afr_sh_data_trim_cbk,
                                   (void *) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->ftruncate,
                                   sh->healing_fd, sh->file_size,
                                   NULL);

                if (!--call_count)
                        break;
        }

        return 0;
}

int
afr_sh_inode_set_read_ctx (afr_self_heal_t *sh, xlator_t *this)
{
        afr_private_t   *priv = NULL;
        int             ret = 0;
        int             i = 0;

        priv = this->private;
        sh->source = afr_sh_select_source (sh->sources, priv->child_count);
        if (sh->source < 0) {
                ret = -1;
                goto out;
        }

        /* detect changes not visible through pending flags -- JIC */
        for (i = 0; i < priv->child_count; i++) {
                if (i == sh->source || sh->child_errno[i])
                        continue;

                if (SIZE_DIFFERS (&sh->buf[i], &sh->buf[sh->source]))
                        sh->sources[i] = 0;
        }

        afr_reset_children (sh->fresh_children, priv->child_count);
        afr_get_fresh_children (sh->success_children, sh->sources,
                                sh->fresh_children, priv->child_count);
        afr_inode_set_read_ctx (this, sh->inode, sh->source,
                                sh->fresh_children);
out:
        return ret;
}

char*
afr_get_sizes_str (afr_local_t *local, struct iatt *bufs, xlator_t *this)
{
        afr_private_t *priv = NULL;
        int           i     = 0;
        char          num[1024] = {0};
        size_t        len = 0;
        char          *sizes_str = NULL;
        size_t        off = 0;
        char          *fmt_str = "%llu bytes on %s, ";
        char          *child_down =  " %s,";
        char          *child_unknown = " %s,";
        int           down_child_present = 0;
        int           down_count = 0;
        int           unknown_count = 0;
        int           unknown_child_present = 0;
        char          *down_subvol_1 = " down subvolume is ";
        char          *unknown_subvol_1 = " unknown subvolume is ";
        char          *down_subvol_2 = " down subvolumes are ";
        char          *unknown_subvol_2 = " unknown subvolumes are ";

        priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i] == 1) {
                        len += snprintf (num, sizeof (num), fmt_str,
                                         (unsigned long long) bufs[i].ia_size,
                                         priv->children[i]->name);
                } else if (local->child_up[i] == 0) {
                        len += snprintf (num, sizeof (num), child_down,
                                         priv->children[i]->name);
                        if (!down_child_present)
                                down_child_present = 1;
                        down_count ++;
                } else if (local->child_up[i] == -1) {
                        len += snprintf (num, sizeof (num), child_unknown,
                                         priv->children[i]->name);
                        if (!unknown_child_present)
                                unknown_child_present = 1;
                        unknown_count++;
                }

        }

        if (down_child_present) {
                if (down_count > 1)
                        len += snprintf (num, sizeof (num), "%s",
                                         down_subvol_2);
                else
                        len += snprintf (num, sizeof (num), "%s",
                                        down_subvol_1);
        }
        if (unknown_child_present) {
                if (unknown_count > 1)
                        len += snprintf (num, sizeof (num), "%s",
                                         unknown_subvol_2);
                else
                        len += snprintf (num, sizeof (num), "%s",
                                         unknown_subvol_1);
        }

        len++;//for '\0'

        sizes_str = GF_CALLOC (len, sizeof (char), gf_common_mt_char);

        if (!sizes_str)
                return NULL;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i] == 1) {
                        off += snprintf (sizes_str + off, len - off, fmt_str,
                                         (unsigned long long) bufs[i].ia_size,
                                         priv->children[i]->name);
                }
        }

        if (down_child_present) {
                if (down_count > 1) {
                        off += snprintf (sizes_str + off, len - off, "%s",
                                         down_subvol_2);
                } else {
                        off += snprintf (sizes_str + off, len - off, "%s",
                                         down_subvol_1);
                }
        }

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i] == 0) {
                        off += snprintf (sizes_str + off, len - off, child_down,
                                         priv->children[i]->name);
                }
        }

        if (unknown_child_present) {
                if (unknown_count > 1) {
                        off += snprintf (sizes_str + off, len - off, "%s",
                                        unknown_subvol_2);
                } else {
                        off += snprintf (sizes_str + off, len - off, "%s",
                                         unknown_subvol_1);
                }
        }

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i] == -1) {
                        off += snprintf (sizes_str + off, len - off,
                                         child_unknown,
                                         priv->children[i]->name);

                }
        }

        return sizes_str;
}

char*
afr_get_sinks_str (xlator_t *this, afr_local_t *local, afr_self_heal_t *sh)
{
        afr_private_t   *priv = NULL;
        int             i = 0;
        char            num[1024] = {0};
        size_t          len = 0;
        char            *sinks_str = NULL;
        char            *temp_str = " to sinks ";
        char            *str_format = " %s,";
        char            off = 0;

        priv = this->private;

        len += snprintf (num, sizeof (num), "%s", temp_str);
        for (i = 0; i < priv->child_count; i++) {
                if ((sh->sources[i] == 0) && (local->child_up[i] == 1)) {
                        len += snprintf (num, sizeof (num), str_format,
                                         priv->children[i]->name);
                }
        }

        len ++;

        sinks_str = GF_CALLOC (len, sizeof (char), gf_common_mt_char);

        if (!sinks_str)
                return NULL;

        off += snprintf (sinks_str + off, len - off, "%s", temp_str);

        for (i = 0; i < priv->child_count; i++) {
                if ((sh->sources[i] == 0) && (local->child_up[i] == 1)) {
                        off += snprintf (sinks_str + off, len - off,
                                         str_format,
                                         priv->children[i]->name);
                }
        }

        return sinks_str;

}


void
afr_set_data_sh_info_str (afr_local_t *local, afr_self_heal_t *sh, xlator_t *this)
{
        char            *pending_matrix_str = NULL;
        char            *sizes_str = NULL;
        char            *sinks_str = NULL;
        afr_private_t   *priv = NULL;

        priv = this->private;

        pending_matrix_str = afr_get_pending_matrix_str (sh->pending_matrix,
                                                         this);
        if (!pending_matrix_str)
                pending_matrix_str = "";

        sizes_str = afr_get_sizes_str (local, sh->buf, this);
        if (!sizes_str)
                sizes_str = "";

        sinks_str = afr_get_sinks_str (this, local, sh);
        if (!sinks_str)
                sinks_str = "";

        gf_asprintf (&sh->data_sh_info, " data self heal from %s %s with "
                     "%s data %s", priv->children[sh->source]->name, sinks_str,
                     sizes_str, pending_matrix_str);

        if (pending_matrix_str && strcmp (pending_matrix_str, ""))
                GF_FREE (pending_matrix_str);

        if (sizes_str && strcmp (sizes_str, ""))
                GF_FREE (sizes_str);

        if (sinks_str && strcmp (sinks_str, ""))
                GF_FREE (sinks_str);
}

void
afr_sh_data_fix (call_frame_t *frame, xlator_t *this)
{
        int              source = 0;
        afr_local_t     *local      = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        source     = sh->source;
        sh->block_size = this->ctx->page_size;
        sh->file_size  = sh->buf[source].ia_size;

        if (FILE_HAS_HOLES (&sh->buf[source]))
                sh->file_has_holes = 1;

        if (sh->background && sh->unwind && !sh->unwound) {
                sh->unwind (sh->orig_frame, this, sh->op_ret, sh->op_errno,
                            is_self_heal_failed (sh, AFR_CHECK_SPECIFIC));
                sh->unwound = _gf_true;
        }

        afr_sh_mark_source_sinks (frame, this);
        if (sh->active_sinks == 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no active sinks for performing self-heal on file %s",
                        local->loc.path);
                afr_sh_data_finish (frame, this);
                return;
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "self-healing file %s from subvolume %s to %d other",
                local->loc.path, priv->children[sh->source]->name,
                sh->active_sinks);

        sh->actual_sh_started = _gf_true;
        afr_set_self_heal_status (sh, AFR_SELF_HEAL_SYNC_BEGIN);
        afr_sh_data_trim_sinks (frame, this);
}

int
afr_sh_data_fxattrop_fstat_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local      = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;
        int              nsources = 0;
        int              ret = 0;
        int             *old_sources = NULL;
        int             tstamp_source = 0;
        int             i = 0;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        gf_log (this->name, GF_LOG_DEBUG, "Pending matrix for: %s",
                lkowner_utoa (&frame->root->lk_owner));
        if (sh->sync_done) {
                //store sources before sync so that mtime can be set using the
                //iatt buf from one of them.
                old_sources = alloca (priv->child_count*sizeof (*old_sources));
                memcpy (old_sources, sh->sources,
                        priv->child_count * sizeof (*old_sources));
        }

        nsources = afr_build_sources (this, sh->xattr, sh->buf, sh->pending_matrix,
                                      sh->sources, sh->success_children,
                                      AFR_DATA_TRANSACTION, NULL, _gf_true);
        if ((nsources == -1)
            && (priv->favorite_child != -1)
            && (sh->child_errno[priv->favorite_child] == 0)) {

                gf_log (this->name, GF_LOG_DEBUG,
                        "Picking favorite child %s as authentic source to "
                        "resolve conflicting data of %s",
                        priv->children[priv->favorite_child]->name,
                        local->loc.path);

                sh->sources[priv->favorite_child] = 1;

                nsources = afr_sh_source_count (sh->sources,
                                                priv->child_count);
        }

        if (nsources == -1) {
                afr_sh_print_split_brain_log (sh->pending_matrix, this,
                                              local->loc.path);
                afr_set_split_brain (this, sh->inode, DONT_KNOW, SPB);

                afr_sh_data_fail (frame, this);
                return 0;
        }

        afr_set_split_brain (this, sh->inode, DONT_KNOW, NO_SPB);

        ret = afr_sh_inode_set_read_ctx (sh, this);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "No active sources found.");

                afr_sh_data_fail (frame, this);
                return 0;
        }

        if (sh->sync_done) {
                /* Perform setattr from one of the old_sources if possible
                 * Because only they have the correct mtime, the new sources
                 * (i.e. old sinks) have mtime from last writev in sync.
                 */
                tstamp_source = sh->source;
                for (i = 0; i < priv->child_count; i++) {
                        if (old_sources[i] && sh->sources[i])
                                tstamp_source = i;
                }
                afr_sh_data_setattr (frame, this, &sh->buf[tstamp_source]);
        } else {
                afr_set_data_sh_info_str (local, sh, this);

                if (nsources == 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "No self-heal needed for %s",
                                local->loc.path);

                        afr_sh_data_finish (frame, this);
                        return 0;
                }

                sh->data_sh_pending = _gf_true;

                if (!sh->dry_run && sh->do_data_self_heal &&
                    afr_data_self_heal_enabled (priv->data_self_heal))
                        afr_sh_data_fix (frame, this);
                else
                        afr_sh_data_finish (frame, this);
        }
        return 0;
}

int
afr_lookup_select_read_child_by_txn_type (xlator_t *this, afr_local_t *local,
                                          dict_t **xattr,
                                          afr_transaction_type txn_type,
                                          uuid_t gfid)
{
        afr_private_t            *priv      = NULL;
        int                      read_child = -1;
        int32_t                  **pending_matrix = NULL;
        int32_t                  *sources         = NULL;
        int32_t                  *success_children   = NULL;
        struct iatt              *bufs            = NULL;
        int32_t                  nsources         = 0;
        int32_t                  prev_read_child  = -1;
        int32_t                  config_read_child = -1;
        int32_t                  subvol_status = 0;

        priv = this->private;
        bufs = local->cont.lookup.bufs;
        success_children = local->cont.lookup.success_children;

        pending_matrix = local->cont.lookup.pending_matrix;
        sources = local->cont.lookup.sources;
        memset (sources, 0, sizeof (*sources) * priv->child_count);

        nsources = afr_build_sources (this, xattr, bufs, pending_matrix,
                                      sources, success_children, txn_type,
                                      &subvol_status, _gf_false);
        if (subvol_status & SPLIT_BRAIN) {
                gf_log (this->name, GF_LOG_DEBUG, "%s: Possible split-brain",
                        local->loc.path);
                switch (txn_type) {
                case AFR_DATA_TRANSACTION:
                        local->cont.lookup.possible_spb = _gf_true;
                        nsources = 1;
                        sources[success_children[0]] = 1;
                        break;
                case AFR_ENTRY_TRANSACTION:
                        read_child = afr_get_no_xattr_dir_read_child (this,
                                                             success_children,
                                                             bufs);
                        sources[read_child] = 1;
                        nsources = 1;
                        break;
                default:
                        break;
                }
        }
        if (nsources < 0)
                goto out;

        prev_read_child = local->read_child_index;
        config_read_child = priv->read_child;
        read_child = afr_select_read_child_from_policy (success_children,
                                                        priv->child_count,
                                                        prev_read_child,
                                                        config_read_child,
                                                        sources,
                                                        priv->hash_mode, gfid);
out:
        gf_log (this->name, GF_LOG_DEBUG, "returning read_child: %d",
                read_child);
        return read_child;
}

int
afr_sh_data_fstat_cbk (call_frame_t *frame, void *cookie,
                       xlator_t *this, int32_t op_ret, int32_t op_errno,
                       struct iatt *buf, dict_t *xdata)
{
        afr_private_t   *priv  = NULL;
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        int call_count  = -1;
        int child_index = (long) cookie;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        LOCK (&frame->lock);
        {
                if (op_ret != -1) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "fstat of %s on %s succeeded",
                                local->loc.path,
                                priv->children[child_index]->name);

                        sh->buf[child_index] = *buf;
                        sh->success_children[sh->success_count] = child_index;
                        sh->success_count++;
                } else {
                        gf_log (this->name, GF_LOG_ERROR, "%s: fstat failed "
                                "on %s, reason %s", local->loc.path,
                                priv->children[child_index]->name,
                                strerror (op_errno));
                        sh->child_errno[child_index] = op_errno;
                }
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                /* Previous versions of glusterfs might have set
                 * the pending data xattrs which need to be erased
                 */
                if (!afr_sh_data_proceed (sh->success_count)) {
                        gf_log (this->name, GF_LOG_DEBUG, "inspecting metadata "
                                "succeeded on < %d children, aborting "
                                "self-heal for %s", AFR_SH_MIN_PARTICIPANTS,
                                local->loc.path);
                        afr_sh_data_fail (frame, this);
                        goto out;
                }
                afr_sh_data_fxattrop_fstat_done (frame, this);
        }
out:
        return 0;
}


int
afr_sh_data_fstat (call_frame_t *frame, xlator_t *this)
{
        afr_self_heal_t *sh    = NULL;
        afr_local_t     *local = NULL;
        afr_private_t   *priv  = NULL;
        int             call_count = 0;
        int             i = 0;
        int             child = 0;
        int32_t         *fstat_children = NULL;

        priv  = this->private;
        local = frame->local;
        sh    = &local->self_heal;

        fstat_children = memdup (sh->success_children,
                                 sizeof (*fstat_children) * priv->child_count);
        if (!fstat_children) {
                afr_sh_data_fail (frame, this);
                goto out;
        }
        call_count = sh->success_count;
        local->call_count = call_count;

        memset (sh->buf, 0, sizeof (*sh->buf) * priv->child_count);
        afr_reset_children (sh->success_children, priv->child_count);
        sh->success_count = 0;
        for (i = 0; i < priv->child_count; i++) {
                child = fstat_children[i];
                if (child == -1)
                        break;
                STACK_WIND_COOKIE (frame, afr_sh_data_fstat_cbk,
                                   (void *) (long) child,
                                   priv->children[child],
                                   priv->children[child]->fops->fstat,
                                   sh->healing_fd, NULL);
                --call_count;
        }
        GF_ASSERT (!call_count);
out:
        GF_FREE (fstat_children);
        return 0;
}

void
afr_sh_common_fxattrop_resp_handler (call_frame_t *frame, void *cookie,
                                     xlator_t *this, int32_t op_ret,
                                     int32_t op_errno, dict_t *xattr)
{
        afr_private_t   *priv  = NULL;
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        int child_index = (long) cookie;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        LOCK (&frame->lock);
        {
                if (op_ret != -1) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "fxattrop of %s on %s succeeded",
                                local->loc.path,
                                priv->children[child_index]->name);

                        sh->xattr[child_index] = dict_ref (xattr);
                        sh->success_children[sh->success_count] = child_index;
                        sh->success_count++;
                } else {
                        gf_log (this->name, GF_LOG_ERROR, "fxattrop of %s "
                                "failed on %s, reason %s", local->loc.path,
                                priv->children[child_index]->name,
                                strerror (op_errno));
                        sh->child_errno[child_index] = op_errno;
                }
        }
        UNLOCK (&frame->lock);
}

int
afr_sh_data_fxattrop_cbk (call_frame_t *frame, void *cookie,
                          xlator_t *this, int32_t op_ret, int32_t op_errno,
                          dict_t *xattr, dict_t *xdata)
{
        int             call_count  = -1;
        afr_local_t     *local  = NULL;
        afr_self_heal_t *sh     = NULL;

        local = frame->local;
        sh    = &local->self_heal;

        afr_sh_common_fxattrop_resp_handler (frame, cookie, this, op_ret,
                                             op_errno, xattr);

        call_count = afr_frame_return (frame);
        if (call_count == 0) {
                if (!afr_sh_data_proceed (sh->success_count)) {
                        gf_log (this->name, GF_LOG_DEBUG, "%s, inspecting "
                                "change log succeeded on < %d children",
                                local->loc.path, AFR_SH_MIN_PARTICIPANTS);
                        afr_sh_data_fail (frame, this);
                        goto out;
                }
                afr_sh_data_fstat (frame, this);
        }
out:
        return 0;
}


int
afr_sh_data_fxattrop (call_frame_t *frame, xlator_t *this)
{
        afr_self_heal_t *sh    = NULL;
        afr_local_t     *local = NULL;
        afr_private_t   *priv  = NULL;
        dict_t          **xattr_req;
        int32_t         *zero_pending = NULL;
        int call_count = 0;
        int i = 0;
        int ret = 0;
	int j;

        priv  = this->private;
        local = frame->local;
        sh    = &local->self_heal;

        call_count = afr_up_children_count (local->child_up,
                                            priv->child_count);

        local->call_count = call_count;

	xattr_req = GF_CALLOC(priv->child_count, sizeof(struct dict_t *),
			      gf_afr_mt_dict_t);
	if (!xattr_req)
		goto out;

	for (i = 0; i < priv->child_count; i++) {
		xattr_req[i] = dict_new();
		if (!xattr_req[i]) {
			ret = -1;
			goto out;
		}
	}

	for (i = 0; i < priv->child_count; i++) {
		for (j = 0; j < priv->child_count; j++) {
			zero_pending = GF_CALLOC (3, sizeof (*zero_pending),
						  gf_afr_mt_int32_t);
			if (!zero_pending) {
				ret = -1;
				goto out;
			}
			ret = dict_set_dynptr (xattr_req[i], priv->pending_key[j],
					       zero_pending,
					       3 * sizeof (*zero_pending));
			if (ret < 0) {
				gf_log (this->name, GF_LOG_WARNING,
					"Unable to set dict value");
				goto out;
			} else {
				zero_pending = NULL;
			}
		}
	}

        afr_reset_xattr (sh->xattr, priv->child_count);
        afr_reset_children (sh->success_children, priv->child_count);
        memset (sh->child_errno, 0,
                sizeof (*sh->child_errno) * priv->child_count);
        sh->success_count = 0;
        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND_COOKIE (frame, afr_sh_data_fxattrop_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->fxattrop,
                                           sh->healing_fd, GF_XATTROP_ADD_ARRAY,
                                           xattr_req[i], NULL);

                        if (!--call_count)
                                break;
                }
        }

out:
	if (xattr_req) {
		for (i = 0; i < priv->child_count; i++)
			if (xattr_req[i])
				dict_unref(xattr_req[i]);
		GF_FREE(xattr_req);
	}

        if (ret) {
                GF_FREE (zero_pending);
                afr_sh_data_fail (frame, this);
        }

        return 0;
}

int
afr_sh_data_big_lock_success (call_frame_t *frame, xlator_t *this)
{
        afr_local_t   *local = NULL;
        afr_self_heal_t *sh = NULL;

        local = frame->local;
        sh = &local->self_heal;

        sh->data_lock_held = _gf_true;
        afr_sh_data_fxattrop (frame, this);
        return 0;
}

int
afr_sh_dom_lock_success (call_frame_t *frame, xlator_t *this)
{
        afr_local_t   *local = NULL;
        afr_self_heal_t *sh = NULL;

        local = frame->local;
        sh = &local->self_heal;

        sh->sh_dom_lock_held = _gf_true;
        afr_sh_data_lock (frame, this, 0, 0, _gf_true, this->name,
                          afr_sh_data_big_lock_success,
                          afr_sh_data_fail);
        return 0;
}

int
afr_sh_dom_lock_failure (call_frame_t *frame, xlator_t *this)
{
        afr_local_t   *local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_internal_lock_t *int_lock = NULL;

        local = frame->local;
        sh = &local->self_heal;
        int_lock = &local->internal_lock;
        if (EAGAIN == int_lock->lock_op_errno)
                sh->possibly_healing = _gf_true;
        afr_sh_data_fail (frame, this);
        return 0;
}

int
afr_sh_data_post_blocking_inodelk_cbk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;
        afr_self_heal_t     *sh       = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;
        sh       = &local->self_heal;

        if (int_lock->lock_op_ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "Blocking data inodelks "
                        "failed for %s. by %s",
                        local->loc.path, lkowner_utoa (&frame->root->lk_owner));

                sh->data_lock_failure_handler (frame, this);
        } else {

                gf_log (this->name, GF_LOG_DEBUG, "Blocking data inodelks "
                        "done for %s by %s. Proceding to self-heal",
                        local->loc.path, lkowner_utoa (&frame->root->lk_owner));

                sh->data_lock_success_handler (frame, this);
        }

        return 0;
}

int
afr_sh_data_post_nonblocking_inodelk_cbk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;
        afr_self_heal_t     *sh       = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;
        sh       = &local->self_heal;

        if (int_lock->lock_op_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG, "Non Blocking data inodelks "
                        "failed for %s. by %s",
                        local->loc.path, lkowner_utoa (&frame->root->lk_owner));

		if (!sh->data_lock_block) {
			sh->data_lock_failure_handler(frame, this);
		} else {
			int_lock->lock_cbk =
				afr_sh_data_post_blocking_inodelk_cbk;
			afr_blocking_lock (frame, this);
		}
        } else {

                gf_log (this->name, GF_LOG_DEBUG, "Non Blocking data inodelks "
                        "done for %s by %s. Proceeding to self-heal",
                        local->loc.path, lkowner_utoa (&frame->root->lk_owner));
                sh->data_lock_success_handler (frame, this);
        }

        return 0;
}

int
afr_sh_data_lock_rec (call_frame_t *frame, xlator_t *this, char *dom,
                      off_t start, off_t len)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_inodelk_t       *inodelk  = NULL;
        afr_local_t         *local    = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;

        int_lock->transaction_lk_type = AFR_SELFHEAL_LK;
        int_lock->selfheal_lk_type    = AFR_DATA_SELF_HEAL_LK;

        afr_set_lock_number (frame, this);

        int_lock->lock_cbk         = afr_sh_data_post_nonblocking_inodelk_cbk;

        int_lock->domain = dom;
        inodelk = afr_get_inodelk (int_lock, int_lock->domain);
        inodelk->flock.l_start = start;
        inodelk->flock.l_len   = len;
        inodelk->flock.l_type  = F_WRLCK;

        afr_nonblocking_inodelk (frame, this);

        return 0;
}

int
afr_post_sh_big_lock_success (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local     = NULL;
        afr_self_heal_t *sh        = NULL;

        local = frame->local;
        sh = &local->self_heal;

        GF_ASSERT (sh->old_loop_frame);
        sh_loop_finish (sh->old_loop_frame, this);
        sh->old_loop_frame = NULL;
        sh->data_lock_held = _gf_true;
        sh->sync_done = _gf_true;
        afr_sh_data_fxattrop (frame, this);
        return 0;
}

int
afr_post_sh_big_lock_failure (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local     = NULL;
        afr_self_heal_t *sh        = NULL;

        local = frame->local;
        sh = &local->self_heal;

        GF_ASSERT (sh->old_loop_frame);
        sh_loop_finish (sh->old_loop_frame, this);
        sh->old_loop_frame = NULL;
        afr_sh_set_timestamps (frame, this);
        return 0;
}


int
afr_sh_data_lock (call_frame_t *frame, xlator_t *this,
                  off_t start, off_t len, gf_boolean_t block,
                  char *dom, afr_lock_cbk_t success_handler,
                  afr_lock_cbk_t failure_handler)
{
        afr_local_t *   local = NULL;
        afr_self_heal_t * sh  = NULL;

        local = frame->local;
        sh    = &local->self_heal;

        sh->data_lock_success_handler = success_handler;
        sh->data_lock_failure_handler = failure_handler;
	sh->data_lock_block = block;
        return afr_sh_data_lock_rec (frame, this, dom, start, len);
}

int
afr_sh_data_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;
        int              call_count = 0;
        int              child_index = 0;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        child_index = (long) cookie;

        /* TODO: some of the open's might fail.
           In that case, modify cleanup fn to send flush on those
           fd's which are already open */

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "open of %s failed on child %s (%s)",
                                local->loc.path,
                                priv->children[child_index]->name,
                                strerror (op_errno));
                        afr_set_self_heal_status (sh, AFR_SELF_HEAL_FAILED);
                } else {
                        gf_log (this->name, GF_LOG_TRACE,
                                "open of %s succeeded on child %s",
                                local->loc.path,
                                priv->children[child_index]->name);
                }
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                if (is_self_heal_failed (sh, AFR_CHECK_SPECIFIC)) {
                        afr_sh_data_fail (frame, this);
                        return 0;
                }

                gf_log (this->name, GF_LOG_TRACE,
                        "fd for %s opened, commencing sync",
                        local->loc.path);

                afr_sh_data_lock (frame, this, 0, 0, !sh->dry_run,
                                  priv->sh_domain, afr_sh_dom_lock_success,
                                  afr_sh_dom_lock_failure);
        }

        return 0;
}


int
afr_sh_data_open (call_frame_t *frame, xlator_t *this)
{
        int i = 0;
        int call_count = 0;
        fd_t *fd = NULL;
        afr_local_t *   local = NULL;
        afr_private_t * priv  = NULL;
        afr_self_heal_t *sh = NULL;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        call_count = afr_up_children_count (local->child_up, priv->child_count);
        local->call_count = call_count;

        fd = fd_create (local->loc.inode, frame->root->pid);
        sh->healing_fd = fd;

        for (i = 0; i < priv->child_count; i++) {
                if(!local->child_up[i])
                        continue;

                STACK_WIND_COOKIE (frame, afr_sh_data_open_cbk,
                                   (void *) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->open,
                                   &local->loc,
                                   O_RDWR|O_LARGEFILE, fd, NULL);

                if (!--call_count)
                        break;
        }

        return 0;
}

void
afr_sh_non_reg_fix (call_frame_t *frame, xlator_t *this,
                    int32_t op_ret, int32_t op_errno)
{
        afr_private_t   *priv = NULL;
        afr_self_heal_t *sh = NULL;
        afr_local_t     *local = NULL;
        int             i = 0;

        if (op_ret < 0) {
                afr_sh_data_fail (frame, this);
                return;
        }

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        for (i = 0; i < priv->child_count ; i++) {
                if (1 == local->child_up[i])
                        sh->success[i] = 1;
        }

        afr_sh_erase_pending (frame, this, AFR_DATA_TRANSACTION,
                              afr_sh_data_erase_pending_cbk,
                              afr_sh_data_finish);
}

int
afr_sh_non_reg_lock_success (call_frame_t *frame, xlator_t *this)
{
        afr_local_t   *local = NULL;
        afr_self_heal_t *sh = NULL;

        local = frame->local;
        sh = &local->self_heal;
        sh->data_lock_held = _gf_true;
        afr_sh_common_lookup (frame, this, &local->loc,
                              afr_sh_non_reg_fix, NULL,
                              AFR_LOOKUP_FAIL_CONFLICTS |
                              AFR_LOOKUP_FAIL_MISSING_GFIDS,
                              NULL);
        return 0;
}

int
afr_self_heal_data (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh    = NULL;
        afr_private_t   *priv  = this->private;
        int             ret    = -1;

        local = frame->local;
        sh = &local->self_heal;

        sh->sh_type_in_action = AFR_SELF_HEAL_DATA;

        if (afr_can_start_data_self_heal (local, priv)) {
                afr_set_self_heal_status (sh, AFR_SELF_HEAL_STARTED);
                ret = afr_inodelk_init (&local->internal_lock.inodelk[1],
                                        priv->sh_domain, priv->child_count);
                if (ret < 0) {
                        afr_set_self_heal_status (sh, AFR_SELF_HEAL_FAILED);
                        afr_sh_data_done (frame, this);
                        return 0;
                }

                if (IA_ISREG (sh->type)) {
                        afr_sh_data_open (frame, this);
                } else {
                        afr_sh_data_lock (frame, this, 0, 0, _gf_true,
                                          this->name,
                                          afr_sh_non_reg_lock_success,
                                          afr_sh_data_fail);
                }
        } else {
                gf_log (this->name, GF_LOG_TRACE,
                        "not doing data self heal on %s",
                        local->loc.path);
                afr_sh_data_done (frame, this);
        }

        return 0;
}
