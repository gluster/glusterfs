/*
 *   Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
 *   This file is part of GlusterFS.
 *
 *   This file is licensed to you under your choice of the GNU Lesser
 *   General Public License, version 3 or any later version (LGPLv3 or
 *   later), or the GNU General Public License, version 2 (GPLv2), in all
 *   cases as published by the Free Software Foundation.
 */


#include "nl-cache.h"
#include "statedump.h"
#include "upcall-utils.h"

static void
nlc_dentry_op (call_frame_t *frame, xlator_t *this, gf_boolean_t multilink)
{
        nlc_local_t *local = frame->local;

        GF_VALIDATE_OR_GOTO (this->name, local, out);

        switch (local->fop) {
        case GF_FOP_MKDIR:
                nlc_set_dir_state (this, local->loc.inode, NLC_PE_FULL);
                /*fall-through*/
        case GF_FOP_MKNOD:
        case GF_FOP_CREATE:
        case GF_FOP_SYMLINK:
                nlc_dir_add_pe (this, local->loc.parent, local->loc.inode,
                                local->loc.name);
                break;
        case GF_FOP_LINK:
                nlc_dir_add_pe (this, local->loc2.parent, NULL,
                                local->loc2.name);
                break;
        case GF_FOP_RMDIR:
                nlc_inode_clear_cache (this, local->loc.inode, _gf_false);
                /*fall-through*/
        case GF_FOP_UNLINK:
                nlc_dir_remove_pe (this, local->loc.parent, local->loc.inode,
                                   local->loc.name, multilink);
                break;
        case GF_FOP_RENAME:
                /* TBD: Should these be atomic ?  In case of rename, the
                 * newloc->inode can be NULL, and hence use oldloc->inode */
                nlc_dir_remove_pe (this, local->loc2.parent, local->loc2.inode,
                                   local->loc2.name, _gf_false);

                /*TODO: Remove old dentry from destination before adding this pe*/
                nlc_dir_add_pe (this, local->loc.parent, local->loc2.inode,
                                local->loc.name);

        default:
                return;
        }

        nlc_lru_prune (this, NULL);
out:
        return;
}

#define NLC_FOP(_name, _op, loc1, loc2, frame, this, args ...)  do {    \
        nlc_local_t      *__local   = NULL;                             \
        nlc_conf_t       *conf      = NULL;                             \
                                                                        \
        conf = this->private;                                           \
                                                                        \
        if (!IS_PEC_ENABLED (conf))                                     \
                goto disabled;                                          \
                                                                        \
        __local = nlc_local_init (frame, this, _op, loc1, loc2);        \
        GF_VALIDATE_OR_GOTO (this->name, __local, err);                 \
                                                                        \
        STACK_WIND (frame, nlc_##_name##_cbk,                           \
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->_name,  \
                    args);                                              \
        break;                                                          \
disabled:                                                               \
        default_##_name##_resume (frame, this, args);                   \
        break;                                                          \
err:                                                                    \
        default_##_name##_failure_cbk (frame, ENOMEM);                  \
        break;                                                          \
} while (0)

#define NLC_FOP_CBK(_name, multilink, frame, cookie, this, op_ret, op_errno, \
                    args ...) do {                                      \
        nlc_conf_t  *conf  = NULL;                                      \
                                                                        \
        if (op_ret != 0)                                                \
                goto out;                                               \
                                                                        \
        conf = this->private;                                           \
                                                                        \
        if (op_ret < 0 || !IS_PEC_ENABLED (conf))                       \
                goto out;                                               \
        nlc_dentry_op (frame, this, multilink);                         \
out:                                                                    \
        NLC_STACK_UNWIND (_name, frame, op_ret, op_errno, args);        \
} while (0)

static int32_t
nlc_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *buf,
                struct iatt *preoldparent, struct iatt *postoldparent,
                struct iatt *prenewparent, struct iatt *postnewparent,
                dict_t *xdata)
{
        NLC_FOP_CBK (rename, _gf_false, frame, cookie, this, op_ret, op_errno,
                     buf, preoldparent, postoldparent, prenewparent,
                     postnewparent, xdata);
        return 0;
}


static int32_t
nlc_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
            dict_t *xdata)
{
        NLC_FOP (rename, GF_FOP_RENAME, newloc, oldloc, frame, this, oldloc,
                 newloc, xdata);
        return 0;
}


static int32_t
nlc_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
               int32_t op_errno, inode_t *inode, struct iatt *buf,
               struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
        NLC_FOP_CBK(mknod, _gf_false, frame, cookie, this, op_ret, op_errno,
                    inode, buf, preparent, postparent, xdata);
        return 0;
}


static int32_t
nlc_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc,
           mode_t mode, dev_t rdev, mode_t umask,
           dict_t *xdata)
{
        NLC_FOP(mknod, GF_FOP_MKNOD, loc, NULL, frame, this, loc, mode, rdev,
                umask, xdata);
        return 0;
}

static int32_t
nlc_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
                struct iatt *buf, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
        NLC_FOP_CBK (create, _gf_false, frame, cookie, this, op_ret, op_errno,
                     fd, inode, buf, preparent, postparent, xdata);
        return 0;
}


static int32_t
nlc_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
            mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        NLC_FOP (create, GF_FOP_CREATE, loc, NULL, frame, this, loc, flags,
                 mode, umask, fd, xdata);
        return 0;
}

static int32_t
nlc_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
               int32_t op_errno, inode_t *inode, struct iatt *buf,
               struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
        NLC_FOP_CBK (mkdir, _gf_false, frame, cookie, this, op_ret, op_errno,
                     inode, buf, preparent, postparent, xdata);
        return 0;
}


static int32_t
nlc_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
           mode_t umask, dict_t *xdata)
{
        NLC_FOP (mkdir, GF_FOP_MKDIR, loc, NULL, frame, this, loc, mode,
                 umask, xdata);
        return 0;
}


static int32_t
nlc_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, inode_t *inode,
                struct iatt *buf, dict_t *xdata, struct iatt *postparent)
{
        nlc_local_t *local = NULL;
        nlc_conf_t  *conf  = NULL;

        local = frame->local;
        conf = this->private;

        if (!local)
                goto out;

        /* Donot add to pe, this may lead to duplicate entry and
         * requires search before adding if list of strings */
        if (op_ret < 0 && op_errno == ENOENT) {
                nlc_dir_add_ne (this, local->loc.parent, local->loc.name);
                GF_ATOMIC_INC (conf->nlc_counter.nlc_miss);
        }

out:
        NLC_STACK_UNWIND (lookup, frame, op_ret, op_errno, inode, buf, xdata,
                          postparent);
        return 0;
}


static int32_t
nlc_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        nlc_local_t *local = NULL;
        nlc_conf_t  *conf  = NULL;
        inode_t     *inode = NULL;

        if (loc_is_nameless (loc))
                goto wind;

        local = nlc_local_init (frame, this, GF_FOP_LOOKUP, loc, NULL);
        if (!local)
                goto err;

        conf = this->private;

        inode = inode_grep (loc->inode->table, loc->parent, loc->name);
        if (inode) {
                inode_unref (inode);
                goto wind;
        }

        if (nlc_is_negative_lookup (this, loc)) {
                GF_ATOMIC_INC (conf->nlc_counter.nlc_hit);
                gf_msg_trace (this->name, 0, "Serving negative lookup from "
                              "cache:%s", loc->name);
                goto unwind;
        }

wind:
        STACK_WIND (frame, nlc_lookup_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->lookup,
                    loc, xdata);
        return 0;
unwind:
        NLC_STACK_UNWIND (lookup, frame, -1, ENOENT, NULL, NULL, NULL, NULL);
        return 0;
err:
        NLC_STACK_UNWIND (lookup, frame, -1, ENOMEM, NULL, NULL, NULL, NULL);
        return 0;
}

static int32_t
nlc_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *preparent,
               struct iatt *postparent, dict_t *xdata)
{
        NLC_FOP_CBK (rmdir, _gf_false, frame, cookie, this, op_ret, op_errno,
                     preparent, postparent, xdata);
        return 0;
}


static int32_t
nlc_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
           dict_t *xdata)
{
        NLC_FOP (rmdir, GF_FOP_RMDIR, loc, NULL, frame, this, loc, flags,
                 xdata);
        return 0;
}


static int32_t
nlc_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *dict,
                  dict_t *xdata)
{
        nlc_conf_t  *conf  = NULL;

        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, conf, out);

        if (!IS_PEC_ENABLED (conf))
                goto out;

        if (op_ret < 0 && op_errno == ENOENT) {
                GF_ATOMIC_INC (conf->nlc_counter.getrealfilename_miss);
        }

out:
        NLC_STACK_UNWIND (getxattr, frame, op_ret, op_errno, dict, xdata);
        return 0;
}


static int32_t
nlc_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, const char *key,
              dict_t *xdata)
{
        int32_t               op_ret                = -1;
        int32_t               op_errno              = 0;
        dict_t               *dict                  = NULL;
        nlc_local_t           *local                 = NULL;
        gf_boolean_t          hit                   = _gf_false;
        const char           *fname                 = NULL;
        nlc_conf_t            *conf                  = NULL;

        conf = this->private;

        if (!IS_PEC_ENABLED (conf))
                goto wind;

        if (!key || (strncmp (key, GF_XATTR_GET_REAL_FILENAME_KEY,
                     strlen (GF_XATTR_GET_REAL_FILENAME_KEY)) != 0))
                goto wind;

        local = nlc_local_init (frame, this, GF_FOP_GETXATTR, loc, NULL);
        if (!local)
                goto err;

        if (loc->inode && key) {
                dict = dict_new ();
                if (!dict)
                        goto err;

                fname = key + strlen (GF_XATTR_GET_REAL_FILENAME_KEY);
                hit = nlc_get_real_file_name (this, loc, fname, &op_ret,
                                             &op_errno, dict);
                if (hit)
                        goto unwind;
                else
                        dict_unref (dict);
        }

        STACK_WIND (frame, nlc_getxattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->getxattr,
                    loc, key, xdata);
        return 0;
wind:
        STACK_WIND (frame, default_getxattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->getxattr,
                    loc, key, xdata);
        return 0;
unwind:
        GF_ATOMIC_INC (conf->nlc_counter.getrealfilename_hit);
        NLC_STACK_UNWIND (getxattr, frame, op_ret, op_errno, dict, NULL);
        dict_unref (dict);
        return 0;
err:
        NLC_STACK_UNWIND (getxattr, frame, -1, ENOMEM, NULL, NULL);
        return 0;
}


static int32_t
nlc_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, inode_t *inode,
                 struct iatt *buf, struct iatt *preparent,
                 struct iatt *postparent, dict_t *xdata)
{
        NLC_FOP_CBK (symlink, _gf_false, frame, cookie, this, op_ret, op_errno,
                     inode, buf, preparent, postparent, xdata);
        return 0;
}


static int32_t
nlc_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
             loc_t *loc, mode_t umask, dict_t *xdata)
{
        NLC_FOP (symlink, GF_FOP_SYMLINK, loc, NULL, frame, this, linkpath,
                 loc, umask, xdata);
        return 0;
}


static int32_t
nlc_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, inode_t *inode, struct iatt *buf,
              struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
        NLC_FOP_CBK (link, _gf_false, frame, cookie, this, op_ret, op_errno,
                     inode, buf, preparent, postparent, xdata);
        return 0;
}


static int32_t
nlc_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
          dict_t *xdata)
{
        NLC_FOP (link, GF_FOP_LINK, oldloc, newloc, frame, this, oldloc,
                 newloc, xdata);
        return 0;
}


static int32_t
nlc_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
        uint32_t    link_count = 0;
        gf_boolean_t multilink = _gf_false;

        if (xdata && !dict_get_uint32 (xdata, GET_LINK_COUNT, &link_count)) {
                if (link_count > 1)
                        multilink = _gf_true;
        } else {
                /* Don't touch cache if we don't know enough */
                gf_msg (this->name, GF_LOG_WARNING, 0, NLC_MSG_DICT_FAILURE,
                        "Failed to get GET_LINK_COUNT from dict");
                NLC_STACK_UNWIND (unlink, frame, op_ret, op_errno, preparent,
                                  postparent, xdata);
                return 0;
        }

        NLC_FOP_CBK (unlink, multilink, frame, cookie, this, op_ret, op_errno,
                     preparent, postparent, xdata);
        return 0;
}


static int32_t
nlc_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
            dict_t *xdata)
{
        nlc_conf_t   *conf     = NULL;
        gf_boolean_t new_dict = _gf_false;

        conf = this->private;

        if (!IS_PEC_ENABLED (conf))
                goto do_fop;

        if (!xdata) {
                xdata = dict_new ();
                if (xdata)
                        new_dict = _gf_true;
        }

        if (xdata && dict_set_uint32 (xdata, GET_LINK_COUNT, 0)) {
                gf_msg (this->name, GF_LOG_WARNING, 0, NLC_MSG_DICT_FAILURE,
                        "Failed to set GET_LINK_COUNT in dict");
                goto err;
        }

do_fop:
        NLC_FOP (unlink, GF_FOP_UNLINK, loc, NULL, frame, this, loc, flags,
                 xdata);

        if (new_dict)
                dict_unref (xdata);
        return 0;
}


static int32_t
nlc_invalidate (xlator_t *this, void *data)
{
        struct gf_upcall                    *up_data    = NULL;
        struct gf_upcall_cache_invalidation *up_ci      = NULL;
        inode_t                             *inode      = NULL;
        inode_t                             *parent1    = NULL;
        inode_t                             *parent2    = NULL;
        int                                  ret        = 0;
        inode_table_t                       *itable     = NULL;
        nlc_conf_t                          *conf       = NULL;

        up_data = (struct gf_upcall *)data;

        if (up_data->event_type != GF_UPCALL_CACHE_INVALIDATION)
                goto out;

        conf = this->private;
        if (!conf)
                 goto out;

        up_ci = (struct gf_upcall_cache_invalidation *)up_data->data;

        /*TODO: Add he inodes found as a member in gf_upcall_cache_invalidation
         * so that it prevents subsequent xlators from doing inode_find again
         */
        itable = ((xlator_t *)this->graph->top)->itable;
        inode = inode_find (itable, up_data->gfid);
        if (!inode) {
                ret = -1;
                goto out;
        }

        if ((!((up_ci->flags & UP_TIMES) && inode->ia_type == IA_IFDIR)) &&
            (!(up_ci->flags & UP_PARENT_DENTRY_FLAGS))) {
                goto out;
        }

        if (!gf_uuid_is_null (up_ci->p_stat.ia_gfid)) {
                parent1 = inode_find (itable, up_ci->p_stat.ia_gfid);
                if (!parent1) {
                        ret = -1;
                        goto out;
                }
        }

        if (!gf_uuid_is_null (up_ci->oldp_stat.ia_gfid)) {
                parent2 = inode_find (itable, up_ci->oldp_stat.ia_gfid);
                if (!parent2) {
                        ret = -1;
                        goto out;
                }
        }

        /* TODO: get enough data in upcall so that we do not invalidate but
         * update */
        if (inode && inode->ia_type == IA_IFDIR)
                nlc_inode_clear_cache (this, inode, NLC_NONE);
        if (parent1)
                nlc_inode_clear_cache (this, parent1, NLC_NONE);
        if (parent2)
                nlc_inode_clear_cache (this, parent2, NLC_NONE);

        GF_ATOMIC_INC (conf->nlc_counter.nlc_invals);

out:
        if (inode)
                inode_unref (inode);
        if (parent1)
                inode_unref (parent1);
        if (parent2)
                inode_unref (parent2);

        return ret;
}


int
nlc_notify (xlator_t *this, int event, void *data, ...)
{
        int        ret  = 0;
        time_t     now  = 0;

        switch (event) {
        case GF_EVENT_CHILD_DOWN:
        case GF_EVENT_SOME_DESCENDENT_DOWN:
        case GF_EVENT_CHILD_UP:
        case GF_EVENT_SOME_DESCENDENT_UP:
                time (&now);
                nlc_update_child_down_time (this, &now);
                /* TODO: nlc_clear_all_cache (this); else
                 lru prune will lazily clear it*/
                break;
        case GF_EVENT_UPCALL:
                ret = nlc_invalidate (this, data);
                break;
        case GF_EVENT_PARENT_DOWN:
                nlc_disable_cache (this);
                nlc_clear_all_cache (this);
        default:
                break;
        }

        if (default_notify (this, event, data) != 0)
                ret = -1;

        return ret;
}


static int32_t
nlc_forget (xlator_t *this, inode_t *inode)
{
        uint64_t         pe_int      = 0;
        uint64_t         nlc_ctx_int = 0;
        nlc_ctx_t       *nlc_ctx     = NULL;
        nlc_conf_t      *conf        = NULL;

        conf = this->private;

        inode_ctx_reset1 (inode, this, &pe_int);
        GF_ASSERT (pe_int == 0);

        nlc_inode_clear_cache (this, inode, NLC_NONE);
        inode_ctx_reset0 (inode, this, &nlc_ctx_int);
        nlc_ctx = (void *) (long) nlc_ctx_int;
        if (nlc_ctx) {
                GF_FREE (nlc_ctx);
                GF_ATOMIC_SUB (conf->current_cache_size, sizeof (*nlc_ctx));
        }

        return 0;
}


static int32_t
nlc_inodectx (xlator_t *this, inode_t *inode)
{
        nlc_dump_inodectx (this, inode);
        return 0;
}


static int32_t
nlc_priv_dump (xlator_t *this)
{
        nlc_conf_t *conf = NULL;
        char  key_prefix[GF_DUMP_MAX_BUF_LEN];

        conf = this->private;

        snprintf(key_prefix, GF_DUMP_MAX_BUF_LEN, "%s.%s", this->type, this->name);
        gf_proc_dump_add_section(key_prefix);

        gf_proc_dump_write("negative_lookup_hit_count", "%"PRId64,
                           GF_ATOMIC_GET(conf->nlc_counter.nlc_hit));
        gf_proc_dump_write("negative_lookup_miss_count", "%"PRId64,
                           GF_ATOMIC_GET(conf->nlc_counter.nlc_miss));
        gf_proc_dump_write("get_real_filename_hit_count", "%"PRId64,
                         GF_ATOMIC_GET(conf->nlc_counter.getrealfilename_hit));
        gf_proc_dump_write("get_real_filename_miss_count", "%"PRId64,
                        GF_ATOMIC_GET(conf->nlc_counter.getrealfilename_miss));
        gf_proc_dump_write("nameless_lookup_count", "%"PRId64,
                           GF_ATOMIC_GET(conf->nlc_counter.nameless_lookup));
        gf_proc_dump_write("inodes_with_positive_dentry_cache", "%"PRId64,
                           GF_ATOMIC_GET(conf->nlc_counter.pe_inode_cnt));
        gf_proc_dump_write("inodes_with_negative_dentry_cache", "%"PRId64,
                           GF_ATOMIC_GET(conf->nlc_counter.ne_inode_cnt));
        gf_proc_dump_write("dentry_invalidations_recieved", "%"PRId64,
                           GF_ATOMIC_GET(conf->nlc_counter.nlc_invals));
        gf_proc_dump_write("cache_limit", "%"PRIu64,
                           conf->cache_size);
        gf_proc_dump_write("consumed_cache_size", "%"PRId64,
                           GF_ATOMIC_GET(conf->current_cache_size));
        gf_proc_dump_write("inode_limit", "%"PRIu64,
                           conf->inode_limit);
        gf_proc_dump_write("consumed_inodes", "%"PRId64,
                           GF_ATOMIC_GET(conf->refd_inodes));

        return 0;
}


static int32_t
nlc_dump_metrics (xlator_t *this, int fd)
{
        nlc_conf_t *conf = NULL;

        conf = this->private;

        dprintf (fd, "%s.negative_lookup_hit_count %"PRId64"\n", this->name,
                 GF_ATOMIC_GET(conf->nlc_counter.nlc_hit));
        dprintf (fd, "%s.negative_lookup_miss_count %"PRId64"\n", this->name,
                 GF_ATOMIC_GET(conf->nlc_counter.nlc_miss));
        dprintf (fd, "%s.get_real_filename_hit_count %"PRId64"\n", this->name,
                 GF_ATOMIC_GET(conf->nlc_counter.getrealfilename_hit));
        dprintf (fd, "%s.get_real_filename_miss_count %"PRId64"\n", this->name,
                 GF_ATOMIC_GET(conf->nlc_counter.getrealfilename_miss));
        dprintf (fd, "%s.nameless_lookup_count %"PRId64"\n", this->name,
                 GF_ATOMIC_GET(conf->nlc_counter.nameless_lookup));
        dprintf (fd, "%s.inodes_with_positive_dentry_cache %"PRId64"\n",
                 this->name,
                 GF_ATOMIC_GET(conf->nlc_counter.pe_inode_cnt));
        dprintf (fd, "%s.inodes_with_negative_dentry_cache %"PRId64"\n",
                 this->name, GF_ATOMIC_GET(conf->nlc_counter.ne_inode_cnt));
        dprintf (fd, "%s.dentry_invalidations_recieved %"PRId64"\n",
                 this->name, GF_ATOMIC_GET(conf->nlc_counter.nlc_invals));
        dprintf (fd, "%s.cache_limit %"PRIu64"\n", this->name,
                 conf->cache_size);
        dprintf (fd, "%s.consumed_cache_size %"PRId64"\n", this->name,
                 GF_ATOMIC_GET(conf->current_cache_size));
        dprintf (fd, "%s.inode_limit %"PRIu64"\n", this->name,
                 conf->inode_limit);
        dprintf (fd, "%s.consumed_inodes %"PRId64"\n", this->name,
                 GF_ATOMIC_GET(conf->refd_inodes));

        return 0;
}


void
nlc_fini (xlator_t *this)
{
        nlc_conf_t      *conf       = NULL;

        conf = this->private;
        GF_FREE (conf);

        glusterfs_ctx_tw_put (this->ctx);

        return;
}


int32_t
nlc_mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        ret = xlator_mem_acct_init (this, gf_nlc_mt_end + 1);
        return ret;
}


int32_t
nlc_reconfigure (xlator_t *this, dict_t *options)
{
        nlc_conf_t *conf = NULL;

        conf = this->private;

        GF_OPTION_RECONF ("nl-cache-timeout", conf->cache_timeout, options,
                          int32, out);
        GF_OPTION_RECONF ("nl-cache-positive-entry", conf->positive_entry_cache,
                          options, bool, out);
        GF_OPTION_RECONF ("nl-cache-limit", conf->cache_size, options,
                          size_uint64, out);
        GF_OPTION_RECONF ("pass-through", this->pass_through, options, bool,
                          out);

out:
        return 0;
}


int32_t
nlc_init (xlator_t *this)
{
        nlc_conf_t      *conf       = NULL;
        int              ret        = -1;
        inode_table_t   *itable     = NULL;

        conf = GF_CALLOC (sizeof (*conf), 1, gf_nlc_mt_nlc_conf_t);
        if (!conf)
                goto out;

        GF_OPTION_INIT ("nl-cache-timeout", conf->cache_timeout, int32, out);
        GF_OPTION_INIT ("nl-cache-positive-entry", conf->positive_entry_cache,
                        bool, out);
        GF_OPTION_INIT ("nl-cache-limit", conf->cache_size, size_uint64, out);
        GF_OPTION_INIT ("pass-through", this->pass_through, bool, out);

        /* Since the positive entries are stored as list of refs on
         * existing inodes, we should not overflow the inode lru_limit.
         * Hence keep the limit of inodes that are refed by this xlator,
         * to 80% of inode_table->lru_limit. In fuse where the limit is
         * infinite, take 131072 as lru limit (as in gfapi). */
        itable = ((xlator_t *)this->graph->top)->itable;
        if (itable && itable->lru_limit)
                conf->inode_limit = itable->lru_limit * 80 / 100;
        else
                conf->inode_limit = 131072 * 80 / 100;

        LOCK_INIT (&conf->lock);
        GF_ATOMIC_INIT (conf->current_cache_size, 0);
        GF_ATOMIC_INIT (conf->refd_inodes, 0);
        GF_ATOMIC_INIT (conf->nlc_counter.nlc_hit, 0);
        GF_ATOMIC_INIT (conf->nlc_counter.nlc_miss, 0);
        GF_ATOMIC_INIT (conf->nlc_counter.nameless_lookup, 0);
        GF_ATOMIC_INIT (conf->nlc_counter.getrealfilename_hit, 0);
        GF_ATOMIC_INIT (conf->nlc_counter.getrealfilename_miss, 0);
        GF_ATOMIC_INIT (conf->nlc_counter.pe_inode_cnt, 0);
        GF_ATOMIC_INIT (conf->nlc_counter.ne_inode_cnt, 0);
        GF_ATOMIC_INIT (conf->nlc_counter.nlc_invals, 0);

        INIT_LIST_HEAD (&conf->lru);
        time (&conf->last_child_down);

        conf->timer_wheel = glusterfs_ctx_tw_get (this->ctx);
        if (!conf->timer_wheel) {
                gf_msg (this->name, GF_LOG_ERROR, 0, NLC_MSG_NO_TIMER_WHEEL,
                        "Initing the global timer wheel failed");
                        goto out;
        }

        this->private = conf;

        ret = 0;
out:
        if (ret < 0)
                GF_FREE (conf);

        return ret;
}


struct xlator_fops nlc_fops = {
        .rename               = nlc_rename,
        .mknod                = nlc_mknod,
        .create               = nlc_create,
        .mkdir                = nlc_mkdir,
        .lookup               = nlc_lookup,
        .rmdir                = nlc_rmdir,
        .getxattr             = nlc_getxattr,
        .symlink              = nlc_symlink,
        .link                 = nlc_link,
        .unlink               = nlc_unlink,
        /* TODO:
        .readdir              = nlc_readdir,
        .readdirp             = nlc_readdirp,
        .seek                 = nlc_seek,
        .opendir              = nlc_opendir, */
};


struct xlator_cbks nlc_cbks = {
        .forget               = nlc_forget,
};


struct xlator_dumpops nlc_dumpops = {
        .inodectx             = nlc_inodectx,
        .priv                 = nlc_priv_dump,
};

struct volume_options nlc_options[] = {
        { .key = {"nl-cache-positive-entry"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "false",
          .op_version = {GD_OP_VERSION_3_11_0},
          .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
          .description = "Cache the name of the files/directories that was"
                         " looked up and are present in a directory",
        },
        { .key = {"nl-cache-limit"},
          .type = GF_OPTION_TYPE_SIZET,
          .min = 0,
          .default_value = "131072",
          .op_version = {GD_OP_VERSION_3_11_0},
          .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
          .description = "the value over which caching will be disabled for"
                         "a while and the cache is cleared based on LRU",
        },
        { .key = {"nl-cache-timeout"},
          .type = GF_OPTION_TYPE_TIME,
          .min = 0,
          .default_value = "60",
          .op_version = {GD_OP_VERSION_3_11_0},
          .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
          .description = "Time period after which cache has to be refreshed",
        },
        { .key  = {"pass-through"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "false",
          .op_version = {GD_OP_VERSION_4_1_0},
          .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC | OPT_FLAG_CLIENT_OPT,
          .tags = {"nl-cache"},
          .description = "Enable/Disable nl cache translator"
        },

        { .key = {NULL} },
};

xlator_api_t xlator_api = {
        .init          = nlc_init,
        .fini          = nlc_fini,
        .notify        = nlc_notify,
        .reconfigure   = nlc_reconfigure,
        .mem_acct_init = nlc_mem_acct_init,
        .dump_metrics  = nlc_dump_metrics,
        .op_version    = {1}, /* Present from the initial version */
        .dumpops       = &nlc_dumpops,
        .fops          = &nlc_fops,
        .cbks          = &nlc_cbks,
        .options       = nlc_options,
        .identifier    = "nl-cache",
};
