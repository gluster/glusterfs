/*
  Copyright (c) 2007-2010 Gluster, Inc. <http://www.gluster.com>
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
#include "statedump.h"

#include "fd.h"

#include "afr-inode-read.h"
#include "afr-inode-write.h"
#include "afr-dir-read.h"
#include "afr-dir-write.h"
#include "afr-transaction.h"
#include "afr-self-heal.h"
#include "afr-self-heal-common.h"
#include "pump.h"

#define AFR_ICTX_OPENDIR_DONE_MASK     0x0000000200000000ULL
#define AFR_ICTX_SPLIT_BRAIN_MASK      0x0000000100000000ULL
#define AFR_ICTX_READ_CHILD_MASK       0x00000000FFFFFFFFULL

int32_t
afr_set_dict_gfid (dict_t *dict, uuid_t gfid)
{
        int ret       = 0;
        uuid_t *pgfid = NULL;

        GF_ASSERT (gfid);

        pgfid = GF_CALLOC (1, sizeof (uuid_t), gf_common_mt_char);
        if (!pgfid) {
                ret = -1;
                goto out;
        }

        uuid_copy (*pgfid, gfid);

        ret = dict_set_dynptr (dict, "gfid-req", pgfid, sizeof (uuid_t));
        if (ret)
                gf_log (THIS->name, GF_LOG_DEBUG, "gfid set failed");

out:
        if (ret && pgfid)
                GF_FREE (pgfid);

        return ret;
}

uint64_t
afr_is_split_brain (xlator_t *this, inode_t *inode)
{
        int ret = 0;

        uint64_t ctx         = 0;
        uint64_t split_brain = 0;

        VALIDATE_OR_GOTO (inode, out);

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_get (inode, this, &ctx);

                if (ret < 0)
                        goto unlock;

                split_brain = ctx & AFR_ICTX_SPLIT_BRAIN_MASK;
        }
unlock:
        UNLOCK (&inode->lock);

out:
        return split_brain;
}


void
afr_set_split_brain (xlator_t *this, inode_t *inode, gf_boolean_t set)
{
        uint64_t ctx = 0;
        int      ret = 0;

        VALIDATE_OR_GOTO (inode, out);

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_get (inode, this, &ctx);

                if (ret < 0) {
                        ctx = 0;
                }

                if (set) {
                        ctx = (~AFR_ICTX_SPLIT_BRAIN_MASK & ctx)
                                | (0xFFFFFFFFFFFFFFFFULL & AFR_ICTX_SPLIT_BRAIN_MASK);
                } else {
                        ctx = (~AFR_ICTX_SPLIT_BRAIN_MASK & ctx);
                }

                ret = __inode_ctx_put (inode, this, ctx);
                if (ret) {
                        gf_log_callingfn (this->name, GF_LOG_INFO,
                                          "failed to set the inode ctx (%s)",
                                          uuid_utoa (inode->gfid));
                }
        }
        UNLOCK (&inode->lock);
out:
        return;
}


uint64_t
afr_is_opendir_done (xlator_t *this, inode_t *inode)
{
        int      ret          = 0;
        uint64_t ctx          = 0;
        uint64_t opendir_done = 0;

        VALIDATE_OR_GOTO (inode, out);

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_get (inode, this, &ctx);

                if (ret < 0)
                        goto unlock;

                opendir_done = ctx & AFR_ICTX_OPENDIR_DONE_MASK;
        }
unlock:
        UNLOCK (&inode->lock);

out:
        return opendir_done;
}


void
afr_set_opendir_done (xlator_t *this, inode_t *inode)
{
        uint64_t ctx = 0;
        int      ret = 0;

        VALIDATE_OR_GOTO (inode, out);

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_get (inode, this, &ctx);

                if (ret < 0) {
                        ctx = 0;
                }

                ctx = (~AFR_ICTX_OPENDIR_DONE_MASK & ctx)
                        | (0xFFFFFFFFFFFFFFFFULL & AFR_ICTX_OPENDIR_DONE_MASK);

                ret = __inode_ctx_put (inode, this, ctx);
                if (ret) {
                        gf_log_callingfn (this->name, GF_LOG_INFO,
                                          "failed to set the inode ctx (%s)",
                                          uuid_utoa (inode->gfid));
                }
        }
        UNLOCK (&inode->lock);
out:
        return;
}


uint64_t
afr_read_child (xlator_t *this, inode_t *inode)
{
        int ret = 0;

        uint64_t ctx         = 0;
        uint64_t read_child  = 0;

        VALIDATE_OR_GOTO (inode, out);

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_get (inode, this, &ctx);

                if (ret < 0)
                        goto unlock;

                read_child = ctx & AFR_ICTX_READ_CHILD_MASK;
        }
unlock:
        UNLOCK (&inode->lock);

out:
        return read_child;
}


void
afr_set_read_child (xlator_t *this, inode_t *inode, int32_t read_child)
{
        uint64_t ctx = 0;
        int      ret = 0;

        VALIDATE_OR_GOTO (inode, out);

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_get (inode, this, &ctx);

                if (ret < 0) {
                        ctx = 0;
                }

                ctx = (~AFR_ICTX_READ_CHILD_MASK & ctx)
                        | (AFR_ICTX_READ_CHILD_MASK & read_child);

                ret = __inode_ctx_put (inode, this, ctx);
                if (ret) {
                        gf_log_callingfn (this->name, GF_LOG_INFO,
                                          "failed to set the inode ctx (%s)",
                                          uuid_utoa (inode->gfid));
                }
        }
        UNLOCK (&inode->lock);

out:
        return;
}


void
afr_local_sh_cleanup (afr_local_t *local, xlator_t *this)
{
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;
        int              i = 0;


        sh = &local->self_heal;
        priv = this->private;

        if (sh->buf)
                GF_FREE (sh->buf);

        if (sh->xattr) {
                for (i = 0; i < priv->child_count; i++) {
                        if (sh->xattr[i]) {
                                dict_unref (sh->xattr[i]);
                                sh->xattr[i] = NULL;
                        }
                }
                GF_FREE (sh->xattr);
        }

        if (sh->child_errno)
                GF_FREE (sh->child_errno);

        if (sh->pending_matrix) {
                for (i = 0; i < priv->child_count; i++) {
                        GF_FREE (sh->pending_matrix[i]);
                }
                GF_FREE (sh->pending_matrix);
        }

        if (sh->delta_matrix) {
                for (i = 0; i < priv->child_count; i++) {
                        GF_FREE (sh->delta_matrix[i]);
                }
                GF_FREE (sh->delta_matrix);
        }

        if (sh->sources)
                GF_FREE (sh->sources);

        if (sh->success)
                GF_FREE (sh->success);

        if (sh->locked_nodes)
                GF_FREE (sh->locked_nodes);

        if (sh->healing_fd && !sh->healing_fd_opened) {
                fd_unref (sh->healing_fd);
                sh->healing_fd = NULL;
        }

        if (sh->linkname)
                GF_FREE ((char *)sh->linkname);
        if (sh->child_success)
                GF_FREE (sh->child_success);

        loc_wipe (&sh->parent_loc);
}


void
afr_local_transaction_cleanup (afr_local_t *local, xlator_t *this)
{
        int             i = 0;
        afr_private_t * priv = NULL;

        priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                if (local->pending && local->pending[i])
                        GF_FREE (local->pending[i]);
        }

        GF_FREE (local->pending);

        if (local->internal_lock.locked_nodes)
                GF_FREE (local->internal_lock.locked_nodes);

        if (local->internal_lock.inode_locked_nodes)
                GF_FREE (local->internal_lock.inode_locked_nodes);

        if (local->internal_lock.entry_locked_nodes)
                GF_FREE (local->internal_lock.entry_locked_nodes);

        if (local->internal_lock.lower_locked_nodes)
                GF_FREE (local->internal_lock.lower_locked_nodes);


        GF_FREE (local->transaction.child_errno);
        GF_FREE (local->child_errno);

        GF_FREE (local->transaction.basename);
        GF_FREE (local->transaction.new_basename);

        loc_wipe (&local->transaction.parent_loc);
        loc_wipe (&local->transaction.new_parent_loc);
}


void
afr_local_cleanup (afr_local_t *local, xlator_t *this)
{
        int i = 0;
        afr_private_t * priv = NULL;

        if (!local)
                return;

        afr_local_sh_cleanup (local, this);

        afr_local_transaction_cleanup (local, this);

        priv = this->private;

        loc_wipe (&local->loc);
        loc_wipe (&local->newloc);

        if (local->fd)
                fd_unref (local->fd);

        if (local->xattr_req)
                dict_unref (local->xattr_req);

        if (local->child_up)
                GF_FREE (local->child_up);

        { /* lookup */
                if (local->cont.lookup.xattrs) {
                        for (i = 0; i < priv->child_count; i++) {
                                if (local->cont.lookup.xattrs[i]) {
                                        dict_unref (local->cont.lookup.xattrs[i]);
                                        local->cont.lookup.xattrs[i] = NULL;
                                }
                        }
                        GF_FREE (local->cont.lookup.xattrs);
                        local->cont.lookup.xattrs = NULL;
                }

                if (local->cont.lookup.xattr) {
                        dict_unref (local->cont.lookup.xattr);
                }

                if (local->cont.lookup.inode) {
                        inode_unref (local->cont.lookup.inode);
                }

                if (local->cont.lookup.postparents)
                        GF_FREE (local->cont.lookup.postparents);

                if (local->cont.lookup.bufs)
                        GF_FREE (local->cont.lookup.bufs);

                if (local->cont.lookup.child_success)
                        GF_FREE (local->cont.lookup.child_success);

                if (local->cont.lookup.sources)
                        GF_FREE (local->cont.lookup.sources);
        }

        { /* getxattr */
                if (local->cont.getxattr.name)
                        GF_FREE (local->cont.getxattr.name);
        }

        { /* lk */
                if (local->cont.lk.locked_nodes)
                        GF_FREE (local->cont.lk.locked_nodes);
        }

        { /* create */
                if (local->cont.create.fd)
                        fd_unref (local->cont.create.fd);
                if (local->cont.create.params)
                        dict_unref (local->cont.create.params);
        }

        { /* mknod */
                if (local->cont.mknod.params)
                        dict_unref (local->cont.mknod.params);
        }

        { /* mkdir */
                if (local->cont.mkdir.params)
                        dict_unref (local->cont.mkdir.params);
        }

        { /* symlink */
                if (local->cont.symlink.params)
                        dict_unref (local->cont.symlink.params);
        }

        { /* writev */
                GF_FREE (local->cont.writev.vector);
        }

        { /* setxattr */
                if (local->cont.setxattr.dict)
                        dict_unref (local->cont.setxattr.dict);
        }

        { /* removexattr */
                GF_FREE (local->cont.removexattr.name);
        }

        { /* symlink */
                GF_FREE (local->cont.symlink.linkpath);
        }

        { /* opendir */
                if (local->cont.opendir.checksum)
                        GF_FREE (local->cont.opendir.checksum);
        }
}


int
afr_frame_return (call_frame_t *frame)
{
        afr_local_t *local = NULL;
        int          call_count = 0;

        local = frame->local;

        LOCK (&frame->lock);
        {
                call_count = --local->call_count;
        }
        UNLOCK (&frame->lock);

        return call_count;
}


/**
 * up_children_count - return the number of children that are up
 */

int
afr_up_children_count (int child_count, unsigned char *child_up)
{
        int i   = 0;
        int ret = 0;

        for (i = 0; i < child_count; i++)
                if (child_up[i])
                        ret++;
        return ret;
}

gf_boolean_t
afr_is_fresh_lookup (loc_t *loc, xlator_t *this)
{
        uint64_t          ctx = 0;
        int32_t           ret = 0;

        GF_ASSERT (loc);
        GF_ASSERT (this);
        GF_ASSERT (loc->inode);

        ret = inode_ctx_get (loc->inode, this, &ctx);
        if (0 == ret)
                return _gf_false;
        return _gf_true;
}

void
afr_update_loc_gfids (loc_t *loc, struct iatt *buf, struct iatt *postparent)
{
        GF_ASSERT (loc);
        GF_ASSERT (buf);

        uuid_copy (loc->gfid, buf->ia_gfid);
        if (postparent)
                uuid_copy (loc->pargfid, postparent->ia_gfid);
}


int
afr_self_heal_lookup_unwind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;

        local = frame->local;

        if (local->govinda_gOvinda && local->cont.lookup.inode) {
                afr_set_split_brain (this, local->cont.lookup.inode, _gf_true);
        }

        AFR_STACK_UNWIND (lookup, frame, local->op_ret, local->op_errno,
                          local->cont.lookup.inode, &local->cont.lookup.buf,
                          local->cont.lookup.xattr,
                          &local->cont.lookup.postparent);

        return 0;
}

void
afr_lookup_build_response_params (afr_local_t *local, xlator_t *this)
{
        int32_t         read_child = -1;
        struct iatt     *buf = NULL;
        struct iatt     *postparent = NULL;
        dict_t          **xattr = NULL;

        GF_ASSERT (local);
        GF_ASSERT (local->cont.lookup.read_child >= 0);

        buf = &local->cont.lookup.buf;
        postparent = &local->cont.lookup.postparent;
        xattr = &local->cont.lookup.xattr;

        read_child = local->cont.lookup.read_child;
        *xattr = dict_ref (local->cont.lookup.xattrs[read_child]);
        *buf = local->cont.lookup.bufs[read_child];
        *postparent = local->cont.lookup.postparents[read_child];

        if (IA_INVAL == local->cont.lookup.inode->ia_type) {
                /* fix for RT #602 */
                local->cont.lookup.inode->ia_type = buf->ia_type;
        }
}


 static void
afr_lookup_update_lk_counts (afr_local_t *local, xlator_t *this,
                            int child_index, dict_t *xattr)
{
        uint32_t inodelk_count = 0;
        uint32_t entrylk_count = 0;
        int      ret           = -1;

        GF_ASSERT (local);
        GF_ASSERT (this);
        GF_ASSERT (xattr);
        GF_ASSERT (child_index >= 0);

        ret = dict_get_uint32 (xattr, GLUSTERFS_INODELK_COUNT,
                               &inodelk_count);
        if (ret == 0)
                local->inodelk_count += inodelk_count;

        ret = dict_get_uint32 (xattr, GLUSTERFS_ENTRYLK_COUNT,
                               &entrylk_count);
        if (ret == 0)
                local->entrylk_count += entrylk_count;
}

static void
afr_lookup_detect_self_heal_by_xattr (afr_local_t *local, xlator_t *this,
                                      dict_t *xattr)
{
        GF_ASSERT (local);
        GF_ASSERT (this);
        GF_ASSERT (xattr);

        if (afr_sh_has_metadata_pending (xattr, this)) {
                local->self_heal.need_metadata_self_heal = _gf_true;
                gf_log(this->name, GF_LOG_DEBUG,
                       "metadata self-heal is pending for %s.",
                       local->loc.path);
        }

        if (afr_sh_has_entry_pending (xattr, this)) {
                local->self_heal.need_entry_self_heal = _gf_true;
                gf_log(this->name, GF_LOG_DEBUG,
                       "entry self-heal is pending for %s.", local->loc.path);
        }

        if (afr_sh_has_data_pending (xattr, this)) {
                local->self_heal.need_data_self_heal = _gf_true;
                gf_log(this->name, GF_LOG_DEBUG,
                       "data self-heal is pending for %s.", local->loc.path);
        }
}

static void
afr_detect_self_heal_by_iatt (afr_local_t *local, xlator_t *this,
                            struct iatt *buf, struct iatt *lookup_buf)
{
        if (PERMISSION_DIFFERS (buf, lookup_buf)) {
                /* mismatching permissions */
                gf_log (this->name, GF_LOG_INFO,
                        "permissions differ for %s ", local->loc.path);
                local->self_heal.need_metadata_self_heal = _gf_true;
        }

        if (OWNERSHIP_DIFFERS (buf, lookup_buf)) {
                /* mismatching permissions */
                local->self_heal.need_metadata_self_heal = _gf_true;
                gf_log (this->name, GF_LOG_INFO,
                        "ownership differs for %s ", local->loc.path);
        }

        if (SIZE_DIFFERS (buf, lookup_buf)
            && IA_ISREG (buf->ia_type)) {
                gf_log (this->name, GF_LOG_INFO,
                        "size differs for %s ", local->loc.path);
                local->self_heal.need_data_self_heal = _gf_true;
        }

        if (uuid_compare (buf->ia_gfid, lookup_buf->ia_gfid)) {
                /* mismatching gfid */
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: gfid different on subvolume", local->loc.path);
        }
}

static void
afr_detect_self_heal_by_lookup_status (afr_local_t *local, xlator_t *this)
{
        GF_ASSERT (local);
        GF_ASSERT (this);

        if ((local->success_count > 0) && (local->enoent_count > 0)) {
                local->self_heal.need_metadata_self_heal = _gf_true;
                local->self_heal.need_data_self_heal     = _gf_true;
                local->self_heal.need_entry_self_heal    = _gf_true;
                gf_log(this->name, GF_LOG_INFO,
                       "entries are missing in lookup of %s.",
                       local->loc.path);
                //If all self-heals are needed no need to check for other rules
                goto out;
        }

        if (local->success_count > 0) {
                if (afr_is_split_brain (this, local->cont.lookup.inode) &&
                    IA_ISREG (local->cont.lookup.inode->ia_type)) {
                        local->self_heal.need_data_self_heal = _gf_true;
                        gf_log (this->name, GF_LOG_WARNING,
                                "split brain detected during lookup of %s.",
                                local->loc.path);
                }
        }

out:
        return;
}

gf_boolean_t
afr_can_self_heal_proceed (afr_self_heal_t *sh, afr_private_t *priv)
{
        GF_ASSERT (sh);
        GF_ASSERT (priv);

        return ((priv->data_self_heal && sh->need_data_self_heal)
                || (priv->metadata_self_heal && sh->need_metadata_self_heal)
                || (priv->entry_self_heal && sh->need_entry_self_heal));
}

gf_boolean_t
afr_is_self_heal_enabled (afr_private_t *priv)
{
        GF_ASSERT (priv);

        return (priv->data_self_heal || priv->metadata_self_heal
                || priv->entry_self_heal);
}

int
afr_lookup_select_read_child (afr_local_t *local, xlator_t *this,
                              int32_t *read_child)
{
        int32_t                 source = -1;
        ia_type_t               ia_type = 0;
        int                     ret = -1;
        afr_transaction_type    type = AFR_METADATA_TRANSACTION;
        dict_t                  **xattrs = NULL;
        int32_t                 *child_success = NULL;
        struct iatt             *bufs = NULL;

        GF_ASSERT (local);
        GF_ASSERT (this);

        bufs = local->cont.lookup.bufs;
        child_success = local->cont.lookup.child_success;
        ia_type = local->cont.lookup.bufs[child_success[0]].ia_type;
        if (IA_ISDIR (ia_type)) {
                type = AFR_ENTRY_TRANSACTION;
        } else if (IA_ISREG (ia_type)) {
                type = AFR_DATA_TRANSACTION;
        }
        xattrs = local->cont.lookup.xattrs;
        source = afr_lookup_select_read_child_by_txn_type (this, local, xattrs,
                                                           type);
        if (source < 0)
                goto out;

        *read_child = source;
        ret = 0;
out:
        return ret;
}

static inline gf_boolean_t
afr_is_self_heal_running (afr_local_t *local)
{
        GF_ASSERT (local);
        return ((local->inodelk_count > 0) || (local->entrylk_count > 0));
}

static void
afr_launch_self_heal (call_frame_t *frame, xlator_t *this,
                      gf_boolean_t is_background, ia_type_t ia_type,
                      int (*unwind) (call_frame_t *frame, xlator_t *this))
{
        afr_local_t             *local = NULL;
        char                    sh_type_str[256] = {0,};

        GF_ASSERT (frame);
        GF_ASSERT (this);

        local = frame->local;
        local->self_heal.background = is_background;
        local->self_heal.type       = ia_type;
        local->self_heal.unwind     = unwind;

        afr_self_heal_type_str_get (&local->self_heal,
                                    sh_type_str,
                                    sizeof (sh_type_str));

        gf_log (this->name, GF_LOG_INFO,
                "background %s self-heal triggered. path: %s",
                sh_type_str, local->loc.path);

        afr_self_heal (frame, this);
}

static void
afr_lookup_detect_self_heal (afr_local_t *local, xlator_t *this)
{
        int                     i = 0;
        struct iatt             *bufs = NULL;
        dict_t                  **xattr = NULL;
        afr_private_t           *priv = NULL;
        int32_t                 child1 = -1;
        int32_t                 child2 = -1;

        afr_detect_self_heal_by_lookup_status (local, this);

        bufs = local->cont.lookup.bufs;
        for (i = 1; i < local->success_count; i++) {
                child1 = local->cont.lookup.child_success[i-1];
                child2 = local->cont.lookup.child_success[i];;
                afr_detect_self_heal_by_iatt (local, this,
                                              &bufs[child1], &bufs[child2]);
        }

        xattr = local->cont.lookup.xattrs;
        priv  = this->private;
        for (i = 0; i < local->success_count; i++) {
                child1 = local->cont.lookup.child_success[i];;
                afr_lookup_detect_self_heal_by_xattr (local, this,
                                                      xattr[child1]);
        }
}

static void
afr_lookup_perform_self_heal_if_needed (call_frame_t *frame, xlator_t *this,
                                        gf_boolean_t *sh_launched)
{
        size_t              up_count = 0;
        afr_private_t       *priv    = NULL;
        afr_local_t         *local   = NULL;

        GF_ASSERT (sh_launched);
        *sh_launched = _gf_false;
        priv         = this->private;
        local        = frame->local;

        up_count  = afr_up_children_count (priv->child_count, local->child_up);
        if (up_count == 1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Only 1 child up - do not attempt to detect self heal");
                goto out;
        }

        if (_gf_false == afr_is_self_heal_enabled (priv)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Self heal is not enabled");
                goto out;
        }

        afr_lookup_detect_self_heal (local, this);
        if (afr_can_self_heal_proceed (&local->self_heal, priv)) {
                if  (afr_is_self_heal_running (local)) {
                        goto out;
                }

                afr_launch_self_heal (frame, this, _gf_true,
                                      local->cont.lookup.buf.ia_type,
                                      afr_self_heal_lookup_unwind);
                *sh_launched = _gf_true;
        }
out:
        return;
}

static gf_boolean_t
afr_lookup_split_brain (afr_local_t *local, xlator_t *this)
{
        int             i              = 0;
        gf_boolean_t    symptom        = _gf_false;
        struct iatt     *bufs          = NULL;
        int32_t         *child_success = NULL;
        struct iatt     *child1        = NULL;
        struct iatt     *child2        = NULL;
        const char      *path          = NULL;

        bufs = local->cont.lookup.bufs;
        child_success = local->cont.lookup.child_success;
        for (i = 1; i < local->success_count; i++) {
                child1 = &bufs[child_success[i-1]];
                child2 = &bufs[child_success[i]];
                /*
                 * TODO: gfid self-heal
                 * if (uuid_compare (child1->ia_gfid, child2->ia_gfid)) {
                 *        gf_log (this->name, GF_LOG_WARNING, "%s: gfid differs"
                 *                " on subvolumes (%d, %d)", local->loc.path,
                 *                child_success[i-1], child_success[i]);
                 *        symptom = _gf_true;
                 * }
                 */

                if (FILETYPE_DIFFERS (child1, child2)) {
                        path = local->loc.path;
                        gf_log (this->name, GF_LOG_WARNING, "%s: filetype "
                                "differs on subvolumes (%d, %d)", path,
                                child_success[i-1], child_success[i]);
                        symptom = _gf_true;
                        local->govinda_gOvinda = 1;
                }
                if (symptom)
                        break;
        }
        return symptom;
}

static int
afr_lookup_set_read_child (afr_local_t *local, xlator_t *this, int32_t read_child)
{
        GF_ASSERT (read_child >= 0);

        afr_set_read_child (this, local->cont.lookup.inode, read_child);
        local->cont.lookup.read_child = read_child;

        return 0;
}

static void
afr_lookup_done (call_frame_t *frame, xlator_t *this)
{
        int                 unwind = 1;
        afr_private_t       *priv  = NULL;
        afr_local_t         *local = NULL;
        int                 ret = -1;
        gf_boolean_t        sh_launched = _gf_false;
        int32_t             read_child = -1;

        priv  = this->private;
        local = frame->local;

        if (local->op_ret < 0)
                goto unwind;

        if (_gf_true == afr_lookup_split_brain (local, this)) {
                local->op_ret = -1;
                local->op_errno = EIO;
                goto unwind;
        }

        ret = afr_lookup_select_read_child (local, this, &read_child);
        if (ret) {
                local->op_ret = -1;
                local->op_errno = EIO;
                goto unwind;
        }

        ret = afr_lookup_set_read_child (local, this, read_child);
        if (ret)
                goto unwind;

        afr_lookup_build_response_params (local, this);
        if (afr_is_fresh_lookup (&local->loc, this)) {
                afr_update_loc_gfids (&local->loc, &local->cont.lookup.buf,
                                      &local->cont.lookup.postparent);
        }

        afr_lookup_perform_self_heal_if_needed (frame, this, &sh_launched);
        if (sh_launched)
                unwind = 0;
 unwind:
         if (unwind) {
                 AFR_STACK_UNWIND (lookup, frame, local->op_ret,
                                  local->op_errno, local->cont.lookup.inode,
                                   &local->cont.lookup.buf,
                                   local->cont.lookup.xattr,
                                   &local->cont.lookup.postparent);
        }
}

/*
 * During a lookup, some errors are more "important" than
 * others in that they must be given higher priority while
 * returning to the user.
 *
 * The hierarchy is ESTALE > ENOENT > others
 *
 */

static gf_boolean_t
__error_more_important (int32_t old_errno, int32_t new_errno)
{
        gf_boolean_t ret = _gf_true;

        /* Nothing should ever overwrite ESTALE */
        if (old_errno == ESTALE)
                ret = _gf_false;

        /* Nothing should overwrite ENOENT, except ESTALE */
        else if ((old_errno == ENOENT) && (new_errno != ESTALE))
                ret = _gf_false;

        return ret;
}

static void
afr_lookup_handle_error (afr_local_t *local, int32_t op_ret,  int32_t op_errno)
{
        GF_ASSERT (local);
        if (op_errno == ENOENT)
                local->enoent_count++;

        if (__error_more_important (local->op_errno, op_errno))
                local->op_errno = op_errno;

        if (local->op_errno == ESTALE) {
                local->op_ret = -1;
        }
}

static void
afr_set_root_inode_on_first_lookup (afr_local_t *local, xlator_t *this,
                                    inode_t *inode)
{
        afr_private_t           *priv = NULL;
        GF_ASSERT (inode);

        if (inode->ino != 1)
                goto out;
        if (!afr_is_fresh_lookup (&local->loc, this))
                goto out;
        priv = this->private;
        if ((priv->first_lookup)) {
                gf_log (this->name, GF_LOG_INFO, "added root inode");
                priv->root_inode = inode_ref (inode);
                priv->first_lookup = 0;
        }
out:
        return;
}

static void
afr_lookup_cache_args (afr_local_t *local, int child_index, dict_t *xattr,
                       struct iatt *buf, struct iatt *postparent)
{
        GF_ASSERT (child_index >= 0);
        local->cont.lookup.xattrs[child_index] = dict_ref (xattr);
        local->cont.lookup.postparents[child_index] = *postparent;
        local->cont.lookup.bufs[child_index] = *buf;
}

static void
afr_lookup_handle_first_success (afr_local_t *local, xlator_t *this,
                                 inode_t *inode, struct iatt *buf)
{
        local->cont.lookup.inode      = inode_ref (inode);
        local->cont.lookup.buf        = *buf;
        afr_set_root_inode_on_first_lookup (local, this, inode);
}

static void
afr_lookup_handle_success (afr_local_t *local, xlator_t *this, int32_t child_index,
                           int32_t op_ret, int32_t op_errno, inode_t *inode,
                           struct iatt *buf, dict_t *xattr,
                           struct iatt *postparent)
{
        if (local->success_count == 0) {
                if (local->op_errno != ESTALE) {
                        local->op_ret = op_ret;
                        local->op_errno = 0;
                }
                afr_lookup_handle_first_success (local, this, inode, buf);
        }
        afr_lookup_update_lk_counts (local, this,
                                     child_index, xattr);

        afr_lookup_cache_args (local, child_index, xattr,
                               buf, postparent);
        local->cont.lookup.child_success[local->success_count] = child_index;
        local->success_count++;
}

int
afr_lookup_cbk (call_frame_t *frame, void *cookie,
                xlator_t *this,  int32_t op_ret,  int32_t op_errno,
                inode_t *inode,   struct iatt *buf, dict_t *xattr,
                struct iatt *postparent)
{
        afr_local_t *   local = NULL;
        int             call_count      = -1;
        int             child_index     = -1;

         child_index = (long) cookie;

        LOCK (&frame->lock);
        {
                local = frame->local;

                if (op_ret == -1) {
                        afr_lookup_handle_error (local, op_ret, op_errno);
                        goto unlock;
                }
                afr_lookup_handle_success (local, this, child_index, op_ret,
                                           op_errno, inode, buf, xattr,
                                           postparent);

         }
unlock:
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);
        if (call_count == 0) {
               afr_lookup_done (frame, this);
        }

         return 0;
}

int
afr_lookup_cont_init (afr_local_t *local, unsigned int child_count)
{
        int               ret            = -ENOMEM;
        int32_t           *child_success = NULL;
        struct iatt       *iatts         = NULL;
        int               i              = 0;

        GF_ASSERT (local);
        local->cont.lookup.xattrs = GF_CALLOC (child_count,
                                               sizeof (*local->cont.lookup.xattr),
                                               gf_afr_mt_dict_t);
        if (NULL == local->cont.lookup.xattrs)
                goto out;

        iatts = GF_CALLOC (child_count, sizeof (*iatts), gf_afr_mt_iatt);
        if (NULL == iatts)
                goto out;
        local->cont.lookup.postparents = iatts;

        iatts = GF_CALLOC (child_count, sizeof (*iatts), gf_afr_mt_iatt);
        if (NULL == iatts)
                goto out;
        local->cont.lookup.bufs = iatts;

        child_success = GF_CALLOC (child_count, sizeof (*child_success),
                                   gf_afr_mt_char);
        if (NULL == child_success)
                goto out;
        for (i = 0; i < child_count; i++)
                child_success[i] = -1;

        local->cont.lookup.child_success = child_success;

        local->cont.lookup.read_child = -1;
        ret = 0;
out:
        return ret;
}

int
afr_lookup (call_frame_t *frame, xlator_t *this,
            loc_t *loc, dict_t *xattr_req)
{
        afr_private_t    *priv           = NULL;
        afr_local_t      *local          = NULL;
        int               ret            = -1;
        int               i              = 0;
        int               call_count     = 0;
        uint64_t          ctx            = 0;
        int32_t           op_errno       = 0;

        priv = this->private;

        ALLOC_OR_GOTO (local, afr_local_t, out);

        local->op_ret = -1;

        frame->local = local;

        if (!strcmp (loc->path, "/" GF_REPLICATE_TRASH_DIR)) {
                op_errno = ENOENT;
                goto out;
        }

        loc_copy (&local->loc, loc);

        ret = inode_ctx_get (loc->inode, this, &ctx);
        if (ret == 0) {
                /* lookup is a revalidate */

                local->read_child_index          = afr_read_child (this,
                                                                   loc->inode);
        } else {
                LOCK (&priv->read_child_lock);
                {
                        local->read_child_index = (++priv->read_child_rr)
                                % (priv->child_count);
                }
                UNLOCK (&priv->read_child_lock);
        }

        if (loc->parent)
                local->cont.lookup.parent_ino = loc->parent->ino;

        local->child_up = memdup (priv->child_up, priv->child_count);
        if (NULL == local->child_up) {
                op_errno = ENOMEM;
                goto out;
        }

        ret = afr_lookup_cont_init (local, priv->child_count);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        local->call_count = afr_up_children_count (priv->child_count,
                                                   local->child_up);
        call_count = local->call_count;

        if (local->call_count == 0) {
                ret      = -1;
                op_errno = ENOTCONN;
                goto out;
        }

        /* By default assume ENOTCONN. On success it will be set to 0. */
        local->op_errno = ENOTCONN;

        if (xattr_req == NULL)
                local->xattr_req = dict_new ();
        else
                local->xattr_req = dict_ref (xattr_req);

        for (i = 0; i < priv->child_count; i++) {
                ret = dict_set_uint64 (local->xattr_req, priv->pending_key[i],
                                       3 * sizeof(int32_t));
                if (ret < 0)
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s: Unable to set dict value for %s",
                                loc->path, priv->pending_key[i]);
                /* 3 = data+metadata+entry */
        }

        ret = dict_set_uint64 (local->xattr_req, GLUSTERFS_INODELK_COUNT, 0);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: Unable to set dict value for %s",
                        loc->path, GLUSTERFS_INODELK_COUNT);
        }

        ret = dict_set_uint64 (local->xattr_req, GLUSTERFS_ENTRYLK_COUNT, 0);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: Unable to set dict value for %s",
                        loc->path, GLUSTERFS_ENTRYLK_COUNT);
        }

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND_COOKIE (frame, afr_lookup_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->lookup,
                                           loc, local->xattr_req);
                        if (!--call_count)
                                break;
                }
        }

        ret = 0;
out:
        if (ret == -1)
                AFR_STACK_UNWIND (lookup, frame, -1, op_errno,
                                  NULL, NULL, NULL, NULL);

        return 0;
}


/* {{{ open */

int
afr_fd_ctx_set (xlator_t *this, fd_t *fd)
{
        afr_private_t * priv   = NULL;
        int             ret    = -1;
        uint64_t        ctx    = 0;
        afr_fd_ctx_t *  fd_ctx = NULL;

        VALIDATE_OR_GOTO (this->private, out);
        VALIDATE_OR_GOTO (fd, out);

        priv = this->private;

        LOCK (&fd->lock);
        {
                ret = __fd_ctx_get (fd, this, &ctx);

                if (ret == 0)
                        goto unlock;

                fd_ctx = GF_CALLOC (1, sizeof (afr_fd_ctx_t),
                                    gf_afr_mt_afr_fd_ctx_t);
                if (!fd_ctx) {
                        ret = -ENOMEM;
                        goto unlock;
                }

                fd_ctx->pre_op_done = GF_CALLOC (sizeof (*fd_ctx->pre_op_done),
                                                 priv->child_count,
                                                 gf_afr_mt_char);
                if (!fd_ctx->pre_op_done) {
                        ret = -ENOMEM;
                        goto unlock;
                }

                fd_ctx->pre_op_piggyback = GF_CALLOC (sizeof (*fd_ctx->pre_op_piggyback),
                                                      priv->child_count,
                                                      gf_afr_mt_char);
                if (!fd_ctx->pre_op_piggyback) {
                        ret = -ENOMEM;
                        goto unlock;
                }

                fd_ctx->opened_on = GF_CALLOC (sizeof (*fd_ctx->opened_on),
                                               priv->child_count,
                                               gf_afr_mt_char);
                if (!fd_ctx->opened_on) {
                        ret = -ENOMEM;
                        goto unlock;
                }

                fd_ctx->up_count   = priv->up_count;
                fd_ctx->down_count = priv->down_count;

                fd_ctx->locked_on = GF_CALLOC (sizeof (*fd_ctx->locked_on),
                                               priv->child_count,
                                               gf_afr_mt_char);
                if (!fd_ctx->locked_on) {
                        ret = -ENOMEM;
                        goto unlock;
                }

                ret = __fd_ctx_set (fd, this, (uint64_t)(long) fd_ctx);
                if (ret)
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to set fd ctx (%p)", fd);

                INIT_LIST_HEAD (&fd_ctx->entries);
        }
unlock:
        UNLOCK (&fd->lock);
out:
        return ret;
}

/* {{{ flush */

int
afr_flush_unwind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *   local = NULL;
        call_frame_t   *main_frame = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (local->transaction.main_frame)
                        main_frame = local->transaction.main_frame;
                local->transaction.main_frame = NULL;
        }
        UNLOCK (&frame->lock);

        if (main_frame) {
                AFR_STACK_UNWIND (flush, main_frame,
                                  local->op_ret, local->op_errno);
        }

        return 0;
}


int
afr_flush_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno)
{
        afr_local_t *   local = NULL;
        afr_private_t * priv  = NULL;
        int call_count  = -1;
        int child_index = (long) cookie;
        int need_unwind = 0;

        local = frame->local;
        priv  = this->private;

        LOCK (&frame->lock);
        {
                if (afr_fop_failed (op_ret, op_errno))
                        afr_transaction_fop_failed (frame, this, child_index);

                if (op_ret != -1) {
                        if (local->success_count == 0) {
                                local->op_ret = op_ret;
                        }
                        local->success_count++;

                        if (local->success_count == priv->wait_count) {
                                need_unwind = 1;
                        }
                }

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        if (need_unwind)
                afr_flush_unwind (frame, this);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                local->transaction.resume (frame, this);
        }

        return 0;
}


int
afr_flush_wind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        int i = 0;
        int call_count = -1;

        local = frame->local;
        priv = this->private;

        call_count = afr_up_children_count (priv->child_count, local->child_up);

        if (call_count == 0) {
                local->transaction.resume (frame, this);
                return 0;
        }

        local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND_COOKIE (frame, afr_flush_wind_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->flush,
                                           local->fd);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


int
afr_flush_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;

        local = frame->local;

        local->transaction.unwind (frame, this);

        AFR_STACK_DESTROY (frame);

        return 0;
}


int
afr_flush (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t  * transaction_frame = NULL;
        int ret        = -1;
        int op_ret   = -1;
        int op_errno = 0;
        int call_count = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        ALLOC_OR_GOTO (local, afr_local_t, out);

        ret = AFR_LOCAL_INIT (local, priv);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        call_count = afr_up_children_count (priv->child_count, local->child_up);

        transaction_frame = copy_frame (frame);
        if (!transaction_frame) {
                op_errno = ENOMEM;
                goto out;
        }

        transaction_frame->local = local;

        local->op = GF_FOP_FLUSH;

        local->transaction.fop    = afr_flush_wind;
        local->transaction.done   = afr_flush_done;
        local->transaction.unwind = afr_flush_unwind;

        local->fd                 = fd_ref (fd);

        local->transaction.main_frame = frame;
        local->transaction.start  = 0;
        local->transaction.len    = 0;

        afr_transaction (transaction_frame, this, AFR_DATA_TRANSACTION);


        op_ret = 0;
out:
        if (op_ret == -1) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);

                AFR_STACK_UNWIND (flush, frame, op_ret, op_errno);
        }

        return 0;
}

/* }}} */


int
afr_cleanup_fd_ctx (xlator_t *this, fd_t *fd)
{
        uint64_t        ctx = 0;
        afr_fd_ctx_t    *fd_ctx = NULL;
        int             ret = 0;

        ret = fd_ctx_get (fd, this, &ctx);
        if (ret < 0)
                goto out;

        fd_ctx = (afr_fd_ctx_t *)(long) ctx;

        if (fd_ctx) {
                if (fd_ctx->pre_op_done)
                        GF_FREE (fd_ctx->pre_op_done);

                if (fd_ctx->opened_on)
                        GF_FREE (fd_ctx->opened_on);

                if (fd_ctx->locked_on)
                        GF_FREE (fd_ctx->locked_on);

                if (fd_ctx->pre_op_piggyback)
                        GF_FREE (fd_ctx->pre_op_piggyback);

                GF_FREE (fd_ctx);
        }

out:
        return 0;
}


int
afr_release (xlator_t *this, fd_t *fd)
{
        afr_locked_fd_t *locked_fd = NULL;
        afr_locked_fd_t *tmp       = NULL;
        afr_private_t   *priv      = NULL;

        priv = this->private;

        afr_cleanup_fd_ctx (this, fd);

        list_for_each_entry_safe (locked_fd, tmp, &priv->saved_fds,
                                  list) {

                if (locked_fd->fd == fd) {
                        list_del_init (&locked_fd->list);
                        GF_FREE (locked_fd);
                }

        }

        return 0;
}


/* {{{ fsync */

int
afr_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
               struct iatt *postbuf)
{
        afr_local_t *local = NULL;
        int call_count = -1;
        int child_index = (long) cookie;
        int read_child  = 0;

        local = frame->local;

        read_child = afr_read_child (this, local->fd->inode);

        LOCK (&frame->lock);
        {
                if (child_index == read_child) {
                        local->read_child_returned = _gf_true;
                }

                if (op_ret == 0) {
                        local->op_ret = 0;

                        if (local->success_count == 0) {
                                local->cont.fsync.prebuf  = *prebuf;
                                local->cont.fsync.postbuf = *postbuf;
                        }

                        if (child_index == read_child) {
                                local->cont.fsync.prebuf  = *prebuf;
                                local->cont.fsync.postbuf = *postbuf;
                        }

                        local->success_count++;
                }

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                AFR_STACK_UNWIND (fsync, frame, local->op_ret, local->op_errno,
                                  &local->cont.fsync.prebuf,
                                  &local->cont.fsync.postbuf);
        }

        return 0;
}


int
afr_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd,
           int32_t datasync)
{
        afr_private_t *priv = NULL;
        afr_local_t *local = NULL;
        int ret = -1;
        int i = 0;
        int32_t call_count = 0;
        int32_t op_ret   = -1;
        int32_t op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        ALLOC_OR_GOTO (local, afr_local_t, out);

        ret = AFR_LOCAL_INIT (local, priv);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        call_count = local->call_count;
        frame->local = local;

        local->fd             = fd_ref (fd);
        local->cont.fsync.ino = fd->inode->ino;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND_COOKIE (frame, afr_fsync_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->fsync,
                                           fd, datasync);
                        if (!--call_count)
                                break;
                }
        }

        op_ret = 0;
out:
        if (op_ret == -1) {
                AFR_STACK_UNWIND (fsync, frame, op_ret, op_errno, NULL, NULL);
        }
        return 0;
}

/* }}} */

/* {{{ fsync */

int32_t
afr_fsyncdir_cbk (call_frame_t *frame, void *cookie,
                  xlator_t *this, int32_t op_ret, int32_t op_errno)
{
        afr_local_t *local = NULL;
        int call_count = -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == 0)
                        local->op_ret = 0;

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (fsyncdir, frame, local->op_ret,
                                  local->op_errno);

        return 0;
}


int32_t
afr_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd,
              int32_t datasync)
{
        afr_private_t *priv = NULL;
        afr_local_t *local = NULL;
        int ret = -1;
        int i = 0;
        int32_t call_count = 0;
        int32_t op_ret   = -1;
        int32_t op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        ALLOC_OR_GOTO (local, afr_local_t, out);

        ret = AFR_LOCAL_INIT (local, priv);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        call_count = local->call_count;
        frame->local = local;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND (frame, afr_fsyncdir_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->fsyncdir,
                                    fd, datasync);
                        if (!--call_count)
                                break;
                }
        }

        op_ret = 0;
out:
        if (op_ret == -1) {
                AFR_STACK_UNWIND (fsyncdir, frame, op_ret, op_errno);
        }
        return 0;
}

/* }}} */

/* {{{ xattrop */

int32_t
afr_xattrop_cbk (call_frame_t *frame, void *cookie,
                 xlator_t *this, int32_t op_ret, int32_t op_errno,
                 dict_t *xattr)
{
        afr_local_t *local = NULL;
        int call_count = -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == 0)
                        local->op_ret = 0;

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (xattrop, frame, local->op_ret, local->op_errno,
                                  xattr);

        return 0;
}


int32_t
afr_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
             gf_xattrop_flags_t optype, dict_t *xattr)
{
        afr_private_t *priv = NULL;
        afr_local_t *local  = NULL;
        int ret = -1;
        int i = 0;
        int32_t call_count = 0;
        int32_t op_ret   = -1;
        int32_t op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        ALLOC_OR_GOTO (local, afr_local_t, out);

        ret = AFR_LOCAL_INIT (local, priv);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        call_count = local->call_count;
        frame->local = local;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND (frame, afr_xattrop_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->xattrop,
                                    loc, optype, xattr);
                        if (!--call_count)
                                break;
                }
        }

        op_ret = 0;
out:
        if (op_ret == -1) {
                AFR_STACK_UNWIND (xattrop, frame, op_ret, op_errno, NULL);
        }
        return 0;
}

/* }}} */

/* {{{ fxattrop */

int32_t
afr_fxattrop_cbk (call_frame_t *frame, void *cookie,
                  xlator_t *this, int32_t op_ret, int32_t op_errno,
                  dict_t *xattr)
{
        afr_local_t *local = NULL;

        int call_count = -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == 0)
                        local->op_ret = 0;

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (fxattrop, frame, local->op_ret, local->op_errno,
                                  xattr);

        return 0;
}


int32_t
afr_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
              gf_xattrop_flags_t optype, dict_t *xattr)
{
        afr_private_t *priv = NULL;
        afr_local_t *local  = NULL;
        int ret = -1;
        int i = 0;
        int32_t call_count = 0;
        int32_t op_ret   = -1;
        int32_t op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        ALLOC_OR_GOTO (local, afr_local_t, out);

        ret = AFR_LOCAL_INIT (local, priv);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        call_count = local->call_count;
        frame->local = local;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND (frame, afr_fxattrop_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->fxattrop,
                                    fd, optype, xattr);
                        if (!--call_count)
                                break;
                }
        }

        op_ret = 0;
out:
        if (op_ret == -1) {
                AFR_STACK_UNWIND (fxattrop, frame, op_ret, op_errno, NULL);
        }
        return 0;
}

/* }}} */


int32_t
afr_inodelk_cbk (call_frame_t *frame, void *cookie,
                 xlator_t *this, int32_t op_ret, int32_t op_errno)

{
        afr_local_t *local = NULL;
        int call_count = -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == 0)
                        local->op_ret = 0;

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (inodelk, frame, local->op_ret,
                                  local->op_errno);

        return 0;
}


int32_t
afr_inodelk (call_frame_t *frame, xlator_t *this,
             const char *volume, loc_t *loc, int32_t cmd, struct gf_flock *flock)
{
        afr_private_t *priv = NULL;
        afr_local_t *local  = NULL;
        int ret = -1;
        int i = 0;
        int32_t call_count = 0;
        int32_t op_ret   = -1;
        int32_t op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        ALLOC_OR_GOTO (local, afr_local_t, out);

        ret = AFR_LOCAL_INIT (local, priv);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        call_count = local->call_count;
        frame->local = local;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND (frame, afr_inodelk_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->inodelk,
                                    volume, loc, cmd, flock);

                        if (!--call_count)
                                break;
                }
        }

        op_ret = 0;
out:
        if (op_ret == -1) {
                AFR_STACK_UNWIND (inodelk, frame, op_ret, op_errno);
        }
        return 0;
}


int32_t
afr_finodelk_cbk (call_frame_t *frame, void *cookie,
                  xlator_t *this, int32_t op_ret, int32_t op_errno)

{
        afr_local_t *local = NULL;
        int call_count = -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == 0)
                        local->op_ret = 0;

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (finodelk, frame, local->op_ret,
                                  local->op_errno);

        return 0;
}


int32_t
afr_finodelk (call_frame_t *frame, xlator_t *this,
              const char *volume, fd_t *fd, int32_t cmd, struct gf_flock *flock)
{
        afr_private_t *priv = NULL;
        afr_local_t *local  = NULL;
        int ret = -1;
        int i = 0;
        int32_t call_count = 0;
        int32_t op_ret   = -1;
        int32_t op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        ALLOC_OR_GOTO (local, afr_local_t, out);

        ret = AFR_LOCAL_INIT (local, priv);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        call_count = local->call_count;
        frame->local = local;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND (frame, afr_finodelk_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->finodelk,
                                    volume, fd, cmd, flock);

                        if (!--call_count)
                                break;
                }
        }

        op_ret = 0;
out:
        if (op_ret == -1) {
                AFR_STACK_UNWIND (finodelk, frame, op_ret, op_errno);
        }
        return 0;
}


int32_t
afr_entrylk_cbk (call_frame_t *frame, void *cookie,
                 xlator_t *this, int32_t op_ret, int32_t op_errno)

{
        afr_local_t *local = NULL;
        int call_count = -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == 0)
                        local->op_ret = 0;

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (entrylk, frame, local->op_ret,
                                  local->op_errno);

        return 0;
}


int32_t
afr_entrylk (call_frame_t *frame, xlator_t *this,
             const char *volume, loc_t *loc,
             const char *basename, entrylk_cmd cmd, entrylk_type type)
{
        afr_private_t *priv = NULL;
        afr_local_t *local  = NULL;
        int ret = -1;
        int i = 0;
        int32_t call_count = 0;
        int32_t op_ret   = -1;
        int32_t op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        ALLOC_OR_GOTO (local, afr_local_t, out);

        ret = AFR_LOCAL_INIT (local, priv);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        call_count = local->call_count;
        frame->local = local;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND (frame, afr_entrylk_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->entrylk,
                                    volume, loc, basename, cmd, type);

                        if (!--call_count)
                                break;
                }
        }

        op_ret = 0;
out:
        if (op_ret == -1) {
                AFR_STACK_UNWIND (entrylk, frame, op_ret, op_errno);
        }
        return 0;
}



int32_t
afr_fentrylk_cbk (call_frame_t *frame, void *cookie,
                  xlator_t *this, int32_t op_ret, int32_t op_errno)

{
        afr_local_t *local = NULL;
        int call_count = -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == 0)
                        local->op_ret = 0;

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (fentrylk, frame, local->op_ret,
                                  local->op_errno);

        return 0;
}


int32_t
afr_fentrylk (call_frame_t *frame, xlator_t *this,
              const char *volume, fd_t *fd,
              const char *basename, entrylk_cmd cmd, entrylk_type type)
{
        afr_private_t *priv = NULL;
        afr_local_t *local  = NULL;
        int ret = -1;
        int i = 0;
        int32_t call_count = 0;
        int32_t op_ret   = -1;
        int32_t op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        ALLOC_OR_GOTO (local, afr_local_t, out);

        ret = AFR_LOCAL_INIT (local, priv);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        call_count = local->call_count;
        frame->local = local;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND (frame, afr_fentrylk_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->fentrylk,
                                    volume, fd, basename, cmd, type);

                        if (!--call_count)
                                break;
                }
        }

        op_ret = 0;
out:
        if (op_ret == -1) {
                AFR_STACK_UNWIND (fentrylk, frame, op_ret, op_errno);
        }
        return 0;
}

int32_t
afr_statfs_cbk (call_frame_t *frame, void *cookie,
                xlator_t *this, int32_t op_ret, int32_t op_errno,
                struct statvfs *statvfs)
{
        afr_local_t *local = NULL;
        int call_count = 0;

        LOCK (&frame->lock);
        {
                local = frame->local;

                if (op_ret == 0) {
                        local->op_ret   = op_ret;

                        if (local->cont.statfs.buf_set) {
                                if (statvfs->f_bavail < local->cont.statfs.buf.f_bavail)
                                        local->cont.statfs.buf = *statvfs;
                        } else {
                                local->cont.statfs.buf = *statvfs;
                                local->cont.statfs.buf_set = 1;
                        }
                }

                if (op_ret == -1)
                        local->op_errno = op_errno;

        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (statfs, frame, local->op_ret, local->op_errno,
                                  &local->cont.statfs.buf);

        return 0;
}


int32_t
afr_statfs (call_frame_t *frame, xlator_t *this,
            loc_t *loc)
{
        afr_private_t *  priv        = NULL;
        int              child_count = 0;
        afr_local_t   *  local       = NULL;
        int              i           = 0;
        int              ret = -1;
        int              call_count = 0;
        int32_t          op_ret      = -1;
        int32_t          op_errno    = 0;

        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);
        VALIDATE_OR_GOTO (loc, out);

        priv = this->private;
        child_count = priv->child_count;

        ALLOC_OR_GOTO (local, afr_local_t, out);

        ret = AFR_LOCAL_INIT (local, priv);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        frame->local = local;
        call_count = local->call_count;

        for (i = 0; i < child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND (frame, afr_statfs_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->statfs,
                                    loc);
                        if (!--call_count)
                                break;
                }
        }

        op_ret = 0;
out:
        if (op_ret == -1) {
                AFR_STACK_UNWIND (statfs, frame, op_ret, op_errno, NULL);
        }
        return 0;
}


int32_t
afr_lk_unlock_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct gf_flock *lock)
{
        afr_local_t * local = NULL;
        int call_count = -1;

        local = frame->local;
        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (lk, frame, local->op_ret, local->op_errno,
                                  lock);

        return 0;
}


int32_t
afr_lk_unlock (call_frame_t *frame, xlator_t *this)
{
        afr_local_t   * local = NULL;
        afr_private_t * priv  = NULL;
        int i = 0;
        int call_count = 0;

        local = frame->local;
        priv  = this->private;

        call_count = afr_locked_nodes_count (local->cont.lk.locked_nodes,
                                             priv->child_count);

        if (call_count == 0) {
                AFR_STACK_UNWIND (lk, frame, local->op_ret, local->op_errno,
                                  &local->cont.lk.ret_flock);
                return 0;
        }

        local->call_count = call_count;

        local->cont.lk.user_flock.l_type = F_UNLCK;

        for (i = 0; i < priv->child_count; i++) {
                if (local->cont.lk.locked_nodes[i]) {
                        STACK_WIND (frame, afr_lk_unlock_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->lk,
                                    local->fd, F_SETLK,
                                    &local->cont.lk.user_flock);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


int32_t
afr_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
            int32_t op_ret, int32_t op_errno, struct gf_flock *lock)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        int child_index = -1;
/*        int            ret  = 0; */


        local = frame->local;
        priv  = this->private;

        child_index = (long) cookie;

        if (!child_went_down (op_ret, op_errno) && (op_ret == -1)) {
                local->op_ret   = -1;
                local->op_errno = op_errno;

                afr_lk_unlock (frame, this);
                return 0;
        }

        if (op_ret == 0) {
                local->op_ret        = 0;
                local->op_errno      = 0;
                local->cont.lk.locked_nodes[child_index] = 1;
                local->cont.lk.ret_flock = *lock;
        }

        child_index++;

        if (child_index < priv->child_count) {
                STACK_WIND_COOKIE (frame, afr_lk_cbk, (void *) (long) child_index,
                                   priv->children[child_index],
                                   priv->children[child_index]->fops->lk,
                                   local->fd, local->cont.lk.cmd,
                                   &local->cont.lk.user_flock);
        } else if (local->op_ret == -1) {
                /* all nodes have gone down */

                AFR_STACK_UNWIND (lk, frame, -1, ENOTCONN,
                                  &local->cont.lk.ret_flock);
        } else {
                /* locking has succeeded on all nodes that are up */

                /* temporarily
                   ret = afr_mark_locked_nodes (this, local->fd,
                   local->cont.lk.locked_nodes);
                   if (ret)
                   gf_log (this->name, GF_LOG_DEBUG,
                   "Could not save locked nodes info in fdctx");

                   ret = afr_save_locked_fd (this, local->fd);
                   if (ret)
                   gf_log (this->name, GF_LOG_DEBUG,
                   "Could not save locked fd");

                */
                AFR_STACK_UNWIND (lk, frame, local->op_ret, local->op_errno,
                                  &local->cont.lk.ret_flock);
        }

        return 0;
}


int
afr_lk (call_frame_t *frame, xlator_t *this,
        fd_t *fd, int32_t cmd, struct gf_flock *flock)
{
        afr_private_t *priv = NULL;
        afr_local_t *local = NULL;
        int i = 0;
        int32_t op_ret   = -1;
        int32_t op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        ALLOC_OR_GOTO (local, afr_local_t, out);
        AFR_LOCAL_INIT (local, priv);

        frame->local  = local;

        local->cont.lk.locked_nodes = GF_CALLOC (priv->child_count,
                                                 sizeof (*local->cont.lk.locked_nodes),
                                                 gf_afr_mt_char);

        if (!local->cont.lk.locked_nodes) {
                op_errno = ENOMEM;
                goto out;
        }

        local->fd            = fd_ref (fd);
        local->cont.lk.cmd   = cmd;
        local->cont.lk.user_flock = *flock;
        local->cont.lk.ret_flock = *flock;

        STACK_WIND_COOKIE (frame, afr_lk_cbk, (void *) (long) 0,
                           priv->children[i],
                           priv->children[i]->fops->lk,
                           fd, cmd, flock);

        op_ret = 0;
out:
        if (op_ret == -1) {
                AFR_STACK_UNWIND (lk, frame, op_ret, op_errno, NULL);
        }
        return 0;
}

int
afr_priv_dump (xlator_t *this)
{
        afr_private_t *priv = NULL;
        char  key_prefix[GF_DUMP_MAX_BUF_LEN];
        char  key[GF_DUMP_MAX_BUF_LEN];
        int   i = 0;


        GF_ASSERT (this);
        priv = this->private;

        GF_ASSERT (priv);
        snprintf(key_prefix, GF_DUMP_MAX_BUF_LEN, "%s.%s", this->type, this->name);
        gf_proc_dump_add_section(key_prefix);
        gf_proc_dump_build_key(key, key_prefix, "child_count");
        gf_proc_dump_write(key, "%u", priv->child_count);
        gf_proc_dump_build_key(key, key_prefix, "read_child_rr");
        gf_proc_dump_write(key, "%u", priv->read_child_rr);
        for (i = 0; i < priv->child_count; i++) {
                gf_proc_dump_build_key(key, key_prefix, "child_up[%d]", i);
                gf_proc_dump_write(key, "%d", priv->child_up[i]);
                gf_proc_dump_build_key(key, key_prefix,
                                       "pending_key[%d]", i);
                gf_proc_dump_write(key, "%s", priv->pending_key[i]);
        }
        gf_proc_dump_build_key(key, key_prefix, "data_self_heal");
        gf_proc_dump_write(key, "%d", priv->data_self_heal);
        gf_proc_dump_build_key(key, key_prefix, "metadata_self_heal");
        gf_proc_dump_write(key, "%d", priv->metadata_self_heal);
        gf_proc_dump_build_key(key, key_prefix, "entry_self_heal");
        gf_proc_dump_write(key, "%d", priv->entry_self_heal);
        gf_proc_dump_build_key(key, key_prefix, "data_change_log");
        gf_proc_dump_write(key, "%d", priv->data_change_log);
        gf_proc_dump_build_key(key, key_prefix, "metadata_change_log");
        gf_proc_dump_write(key, "%d", priv->metadata_change_log);
        gf_proc_dump_build_key(key, key_prefix, "entry_change_log");
        gf_proc_dump_write(key, "%d", priv->entry_change_log);
        gf_proc_dump_build_key(key, key_prefix, "read_child");
        gf_proc_dump_write(key, "%d", priv->read_child);
        gf_proc_dump_build_key(key, key_prefix, "favorite_child");
        gf_proc_dump_write(key, "%d", priv->favorite_child);
        gf_proc_dump_build_key(key, key_prefix, "data_lock_server_count");
        gf_proc_dump_write(key, "%u", priv->data_lock_server_count);
        gf_proc_dump_build_key(key, key_prefix, "metadata_lock_server_count");
        gf_proc_dump_write(key, "%u", priv->metadata_lock_server_count);
        gf_proc_dump_build_key(key, key_prefix, "entry_lock_server_count");
        gf_proc_dump_write(key, "%u", priv->entry_lock_server_count);
        gf_proc_dump_build_key(key, key_prefix, "wait_count");
        gf_proc_dump_write(key, "%u", priv->wait_count);

        return 0;
}


/**
 * find_child_index - find the child's index in the array of subvolumes
 * @this: AFR
 * @child: child
 */

static int
find_child_index (xlator_t *this, xlator_t *child)
{
        afr_private_t *priv = NULL;
        int i = -1;

        priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                if ((xlator_t *) child == priv->children[i])
                        break;
        }

        return i;
}

int32_t
afr_notify (xlator_t *this, int32_t event,
            void *data, ...)
{
        afr_private_t   *priv               = NULL;
        int             i                   = -1;
        int             up_children         = 0;
        int             down_children       = 0;
        int             propagate           = 0;

        int             had_heard_from_all  = 0;
        int             have_heard_from_all = 0;
        int             idx                 = -1;
        int             ret                 = -1;

        priv = this->private;

        if (!priv)
                return 0;

        had_heard_from_all = 1;
        for (i = 0; i < priv->child_count; i++) {
                if (!priv->last_event[i]) {
                        had_heard_from_all = 0;
                }
        }

        /* parent xlators dont need to know about every child_up, child_down
         * because of afr ha. If all subvolumes go down, child_down has
         * to be triggered. In that state when 1 subvolume comes up child_up
         * needs to be triggered. dht optimises revalidate lookup by sending
         * it only to one of its subvolumes. When child up/down happens
         * for afr's subvolumes dht should be notified by child_modified. The
         * subsequent revalidate lookup happens on all the dht's subvolumes
         * which triggers afr self-heals if any.
         */
        idx = find_child_index (this, data);
        if (idx < 0) {
                gf_log (this->name, GF_LOG_ERROR, "Received child_up "
                        "from invalid subvolume");
                goto out;
        }

        switch (event) {
        case GF_EVENT_CHILD_UP:
                LOCK (&priv->lock);
                {
                        priv->child_up[idx] = 1;
                        priv->up_count++;

                        for (i = 0; i < priv->child_count; i++)
                                if (priv->child_up[i] == 1)
                                        up_children++;
                        if (up_children == 1) {
                                gf_log (this->name, GF_LOG_INFO,
                                        "Subvolume '%s' came back up; "
                                        "going online.", ((xlator_t *)data)->name);
                        } else {
                                event = GF_EVENT_CHILD_MODIFIED;
                        }

                        priv->last_event[idx] = event;
                }
                UNLOCK (&priv->lock);

                break;

        case GF_EVENT_CHILD_DOWN:
                LOCK (&priv->lock);
                {
                        priv->child_up[idx] = 0;
                        priv->down_count++;

                        for (i = 0; i < priv->child_count; i++)
                                if (priv->child_up[i] == 0)
                                        down_children++;
                        if (down_children == priv->child_count) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "All subvolumes are down. Going offline "
                                        "until atleast one of them comes back up.");
                        } else {
                                event = GF_EVENT_CHILD_MODIFIED;
                        }

                        priv->last_event[idx] = event;
                }
                UNLOCK (&priv->lock);

                break;

        case GF_EVENT_CHILD_CONNECTING:
                LOCK (&priv->lock);
                {
                        priv->last_event[idx] = event;
                }
                UNLOCK (&priv->lock);
                break;
        default:
                propagate = 1;
                break;
        }

        /* have all subvolumes reported status once by now? */
        have_heard_from_all = 1;
        for (i = 0; i < priv->child_count; i++) {
                if (!priv->last_event[i])
                        have_heard_from_all = 0;
        }

        /* if all subvols have reported status, no need to hide anything
           or wait for anything else. Just propagate blindly */
        if (have_heard_from_all)
                propagate = 1;

        if (!had_heard_from_all && have_heard_from_all) {
                /* This is the first event which completes aggregation
                   of events from all subvolumes. If at least one subvol
                   had come up, propagate CHILD_UP, but only this time
                */
                event = GF_EVENT_CHILD_DOWN;

                LOCK (&priv->lock);
                {
                        for (i = 0; i < priv->child_count; i++) {
                                if (priv->last_event[i] == GF_EVENT_CHILD_UP) {
                                        event = GF_EVENT_CHILD_UP;
                                        break;
                                }

                                if (priv->last_event[i] ==
                                                GF_EVENT_CHILD_CONNECTING) {
                                        event = GF_EVENT_CHILD_CONNECTING;
                                        /* continue to check other events for CHILD_UP */
                                }
                        }
                }
                UNLOCK (&priv->lock);
        }

        ret = 0;
        if (propagate)
                ret = default_notify (this, event, data);

out:
        return ret;
}

int
AFR_LOCAL_INIT (afr_local_t *local, afr_private_t *priv)
{
        local->child_up = GF_CALLOC (sizeof (*local->child_up),
                                     priv->child_count,
                                     gf_afr_mt_char);
        if (!local->child_up) {
                return -ENOMEM;
        }

        memcpy (local->child_up, priv->child_up,
                sizeof (*local->child_up) * priv->child_count);

        local->call_count = afr_up_children_count (priv->child_count,
                                                   local->child_up);
        local->op_ret = -1;
        local->op_errno = EUCLEAN;

        if (local->call_count == 0) {
                gf_log (THIS->name, GF_LOG_INFO, "no subvolumes up");
                return -ENOTCONN;
        }

        return 0;
}

int
afr_internal_lock_init (afr_internal_lock_t *lk, size_t child_count,
                        transaction_lk_type_t lk_type)
{
        int             ret = -ENOMEM;

        lk->inode_locked_nodes = GF_CALLOC (sizeof (*lk->inode_locked_nodes),
                                            child_count, gf_afr_mt_char);
        if (NULL == lk->inode_locked_nodes)
                goto out;

        lk->entry_locked_nodes = GF_CALLOC (sizeof (*lk->entry_locked_nodes),
                                            child_count, gf_afr_mt_char);
        if (NULL == lk->entry_locked_nodes)
                goto out;

        lk->locked_nodes = GF_CALLOC (sizeof (*lk->locked_nodes),
                                      child_count, gf_afr_mt_char);
        if (NULL == lk->locked_nodes)
                goto out;

        lk->lower_locked_nodes = GF_CALLOC (sizeof (*lk->lower_locked_nodes),
                                            child_count, gf_afr_mt_char);
        if (NULL == lk->lower_locked_nodes)
                goto out;

        lk->lock_op_ret   = -1;
        lk->lock_op_errno = EUCLEAN;
        lk->transaction_lk_type = lk_type;

        ret = 0;
out:
        return ret;
}

int
afr_transaction_local_init (afr_local_t *local, afr_private_t *priv)
{
        int i;
        int child_up_count = 0;
        int ret = -ENOMEM;

        ret = afr_internal_lock_init (&local->internal_lock, priv->child_count,
                                      AFR_TRANSACTION_LK);
        if (ret < 0)
                goto out;

        ret = -ENOMEM;
        child_up_count = afr_up_children_count (priv->child_count, local->child_up);
        if (priv->optimistic_change_log && child_up_count == priv->child_count)
                local->optimistic_change_log = 1;

        local->first_up_child = afr_first_up_child (priv);

        local->child_errno = GF_CALLOC (sizeof (*local->child_errno),
                                        priv->child_count,
                                        gf_afr_mt_int32_t);
        if (!local->child_errno)
                goto out;

        local->pending = GF_CALLOC (sizeof (*local->pending),
                                    priv->child_count,
                                    gf_afr_mt_int32_t);

        if (!local->pending)
                goto out;

        for (i = 0; i < priv->child_count; i++) {
                local->pending[i] = GF_CALLOC (sizeof (*local->pending[i]),
                                               3, /* data + metadata + entry */
                                               gf_afr_mt_int32_t);
                if (!local->pending[i])
                        goto out;
        }

        local->transaction.child_errno =
                GF_CALLOC (sizeof (*local->transaction.child_errno),
                           priv->child_count,
                           gf_afr_mt_int32_t);
        local->transaction.erase_pending = 1;

        ret = 0;
out:
        return ret;
}
