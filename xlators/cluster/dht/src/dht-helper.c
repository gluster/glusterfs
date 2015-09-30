/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#include "glusterfs.h"
#include "xlator.h"
#include "dht-common.h"
#include "dht-helper.h"

void
dht_free_mig_info (void *data)
{
        dht_migrate_info_t *miginfo = NULL;

        miginfo = data;
        GF_FREE (miginfo);

        return;
}

static int
dht_inode_ctx_set_mig_info (xlator_t *this, inode_t *inode,
                            xlator_t *src_subvol, xlator_t *dst_subvol)
{
        dht_migrate_info_t *miginfo = NULL;
        uint64_t            value   = 0;
        int                 ret     = -1;

        miginfo = GF_CALLOC (1, sizeof (*miginfo), gf_dht_mt_miginfo_t);
        if (miginfo == NULL)
                goto out;

        miginfo->src_subvol = src_subvol;
        miginfo->dst_subvol = dst_subvol;
        GF_REF_INIT (miginfo, dht_free_mig_info);

        value = (uint64_t) miginfo;

        ret = inode_ctx_set1 (inode, this, &value);
        if (ret < 0) {
                GF_REF_PUT (miginfo);
        }

out:
        return ret;
}


int
dht_inode_ctx_get_mig_info (xlator_t *this, inode_t *inode,
                            xlator_t **src_subvol, xlator_t **dst_subvol)
{
        int                 ret         = -1;
        uint64_t            tmp_miginfo = 0;
        dht_migrate_info_t *miginfo     = NULL;

        LOCK (&inode->lock);
        {
                ret =  __inode_ctx_get1 (inode, this, &tmp_miginfo);
                if ((ret < 0) || (tmp_miginfo == 0)) {
                        UNLOCK (&inode->lock);
                        goto out;
                }

                miginfo = (dht_migrate_info_t *)tmp_miginfo;
                GF_REF_GET (miginfo);
        }
        UNLOCK (&inode->lock);

        if (src_subvol)
                *src_subvol = miginfo->src_subvol;

        if (dst_subvol)
                *dst_subvol = miginfo->dst_subvol;

        GF_REF_PUT (miginfo);

out:
        return ret;
}

gf_boolean_t
dht_mig_info_is_invalid (xlator_t *current, xlator_t *src_subvol,
                      xlator_t *dst_subvol)
{

/* Not set
 */
        if (!src_subvol || !dst_subvol)
                return _gf_true;

/* Invalid scenarios:
 * The src_subvol does not match the subvol on which the current op was sent
 * so the cached subvol has changed between the last mig_info_set and now.
 * src_subvol == dst_subvol. The file was migrated without any FOP detecting
 * a P2 so the old dst is now the current subvol.
 *
 * There is still one scenario where the info could be outdated - if
 * file has undergone multiple migrations and ends up on the same src_subvol
 * on which the mig_info was first set.
 */
        if ((current == dst_subvol) || (current != src_subvol))
                return _gf_true;

        return _gf_false;
}

int
dht_frame_return (call_frame_t *frame)
{
        dht_local_t *local = NULL;
        int          this_call_cnt = -1;

        if (!frame)
                return -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                this_call_cnt = --local->call_cnt;
        }
        UNLOCK (&frame->lock);

        return this_call_cnt;
}

/*
 * A slightly "updated" version of the algorithm described in the commit log
 * is used here.
 *
 * The only enhancement is that:
 *
 * - The number of bits used by the backend filesystem for HUGE d_off which
 *   is described as 63, and
 * - The number of bits used by the d_off presented by the transformation
 *   upwards which is described as 64, are both made "configurable."
 */

int
dht_filter_loc_subvol_key (xlator_t *this, loc_t *loc, loc_t *new_loc,
                           xlator_t **subvol)
{
        char          *new_name  = NULL;
        char          *new_path  = NULL;
        xlator_list_t *trav      = NULL;
        char           key[1024] = {0,};
        int            ret       = 0; /* not found */

        /* Why do other tasks if first required 'char' itself is not there */
        if (!new_loc || !loc || !loc->name || !strchr (loc->name, '@'))
                goto out;

        trav = this->children;
        while (trav) {
                snprintf (key, 1024, "*@%s:%s", this->name, trav->xlator->name);
                if (fnmatch (key, loc->name, FNM_NOESCAPE) == 0) {
                        new_name = GF_CALLOC(strlen (loc->name),
                                             sizeof (char),
                                             gf_common_mt_char);
                        if (!new_name)
                                goto out;
                        if (fnmatch (key, loc->path, FNM_NOESCAPE) == 0) {
                                new_path = GF_CALLOC(strlen (loc->path),
                                                     sizeof (char),
                                                     gf_common_mt_char);
                                if (!new_path)
                                        goto out;
                                strncpy (new_path, loc->path, (strlen (loc->path) -
                                                               strlen (key) + 1));
                        }
                        strncpy (new_name, loc->name, (strlen (loc->name) -
                                                       strlen (key) + 1));

                        if (new_loc) {
                                new_loc->path   = ((new_path) ? new_path:
                                                   gf_strdup (loc->path));
                                new_loc->name   = new_name;
                                new_loc->inode  = inode_ref (loc->inode);
                                new_loc->parent = inode_ref (loc->parent);
                        }
                        *subvol         = trav->xlator;
                        ret = 1;  /* success */
                        goto out;
                }
                trav = trav->next;
        }
out:
        if (!ret) {
                /* !success */
                GF_FREE (new_path);
                GF_FREE (new_name);
        }
        return ret;
}

static xlator_t *
dht_get_subvol_from_id(xlator_t *this, int client_id)
{
        xlator_t *xl = NULL;
        dht_conf_t *conf = NULL;
        char sid[6] = { 0 };

        conf = this->private;

        sprintf(sid, "%d", client_id);
        if (dict_get_ptr(conf->leaf_to_subvol, sid, (void **) &xl))
                xl = NULL;

        return xl;
}

int
dht_deitransform (xlator_t *this, uint64_t y, xlator_t **subvol_p)
{
        int         client_id = 0;
        xlator_t   *subvol = 0;
        dht_conf_t *conf = NULL;

        if (!this->private)
                return -1;

        conf = this->private;

        client_id = gf_deitransform(this, y);

        subvol = dht_get_subvol_from_id(this, client_id);

        if (!subvol)
                subvol = conf->subvolumes[0];

        if (subvol_p)
                *subvol_p = subvol;

        return 0;
}

char *
dht_lock_asprintf (dht_lock_t *lock)
{
        char *lk_buf                = NULL;
        char gfid[GF_UUID_BUF_SIZE] = {0, };

        if (lock == NULL)
                goto out;

        uuid_utoa_r (lock->loc.gfid, gfid);

        gf_asprintf (&lk_buf, "%s:%s", lock->xl->name, gfid);

out:
        return lk_buf;
}

void
dht_log_lk_array (char *name, gf_loglevel_t log_level, dht_lock_t **lk_array,
                  int count)
{
        int   i      = 0;
        char *lk_buf = NULL;

        if ((lk_array == NULL) || (count == 0))
                goto out;

        for (i = 0; i < count; i++) {
                lk_buf = dht_lock_asprintf (lk_array[i]);
                gf_msg (name, log_level, 0, DHT_MSG_LK_ARRAY_INFO,
                        "%d. %s", i, lk_buf);
                GF_FREE (lk_buf);
        }

out:
        return;
}

void
dht_lock_stack_destroy (call_frame_t *lock_frame)
{
        dht_local_t *local = NULL;

        local = lock_frame->local;

        local->lock.locks = NULL;
        local->lock.lk_count = 0;

        DHT_STACK_DESTROY (lock_frame);
        return;
}

void
dht_lock_free (dht_lock_t *lock)
{
        if (lock == NULL)
                goto out;

        loc_wipe (&lock->loc);
        GF_FREE (lock->domain);
        mem_put (lock);

out:
        return;
}

void
dht_lock_array_free (dht_lock_t **lk_array, int count)
{
        int            i       = 0;
        dht_lock_t    *lock    = NULL;

        if (lk_array == NULL)
                goto out;

        for (i = 0; i < count; i++) {
                lock = lk_array[i];
                lk_array[i] = NULL;
                dht_lock_free (lock);
        }

out:
        return;
}

dht_lock_t *
dht_lock_new (xlator_t *this, xlator_t *xl, loc_t *loc, short type,
              const char *domain)
{
        dht_conf_t *conf = NULL;
        dht_lock_t *lock = NULL;

        conf = this->private;

        lock = mem_get0 (conf->lock_pool);
        if (lock == NULL)
                goto out;

        lock->xl = xl;
        lock->type = type;
        lock->domain = gf_strdup (domain);
        if (lock->domain == NULL) {
                dht_lock_free (lock);
                lock = NULL;
                goto out;
        }

        /* Fill only inode and gfid.
           posix and protocol/server give preference to pargfid/basename over
           gfid/inode for resolution if all the three parameters of loc_t are
           present. I want to avoid the following hypothetical situation:

           1. rebalance did a lookup on a dentry and got a gfid.
           2. rebalance acquires lock on loc_t which was filled with gfid and
              path (pargfid/bname) from step 1.
           3. somebody deleted and recreated the same file
           4. rename on the same path acquires lock on loc_t which now points
              to a different inode (and hence gets the lock).
           5. rebalance continues to migrate file (note that not all fops done
              by rebalance during migration are inode/gfid based Eg., unlink)
           6. rename continues.
        */
        lock->loc.inode = inode_ref (loc->inode);
        loc_gfid (loc, lock->loc.gfid);

out:
        return lock;
}

int
dht_local_lock_init (call_frame_t *frame, dht_lock_t **lk_array,
                     int lk_count, fop_inodelk_cbk_t inodelk_cbk)
{
        int          ret   = -1;
        dht_local_t *local = NULL;

        local = frame->local;

        if (local == NULL) {
                local = dht_local_init (frame, NULL, NULL, 0);
        }

        if (local == NULL) {
                goto out;
        }

        local->lock.inodelk_cbk = inodelk_cbk;
        local->lock.locks = lk_array;
        local->lock.lk_count = lk_count;

        ret = dht_lock_order_requests (local->lock.locks,
                                       local->lock.lk_count);
        if (ret < 0)
                goto out;

        ret = 0;
out:
        return ret;
}

void
dht_local_wipe (xlator_t *this, dht_local_t *local)
{
        if (!local)
                return;

        loc_wipe (&local->loc);
        loc_wipe (&local->loc2);

        if (local->xattr)
                dict_unref (local->xattr);

        if (local->inode)
                inode_unref (local->inode);

        if (local->layout) {
                dht_layout_unref (this, local->layout);
                local->layout = NULL;
        }

        loc_wipe (&local->linkfile.loc);

        if (local->linkfile.xattr)
                dict_unref (local->linkfile.xattr);

        if (local->linkfile.inode)
                inode_unref (local->linkfile.inode);

        if (local->fd) {
                fd_unref (local->fd);
                local->fd = NULL;
        }

        if (local->params) {
                dict_unref (local->params);
                local->params = NULL;
        }

        if (local->xattr_req)
                dict_unref (local->xattr_req);

        if (local->selfheal.layout) {
                dht_layout_unref (this, local->selfheal.layout);
                local->selfheal.layout = NULL;
        }

        dht_lock_array_free (local->lock.locks, local->lock.lk_count);
        GF_FREE (local->lock.locks);

        GF_FREE (local->newpath);

        GF_FREE (local->key);

        GF_FREE (local->rebalance.vector);

        if (local->rebalance.iobref)
                iobref_unref (local->rebalance.iobref);

        mem_put (local);
}


dht_local_t *
dht_local_init (call_frame_t *frame, loc_t *loc, fd_t *fd, glusterfs_fop_t fop)
{
        dht_local_t *local = NULL;
        inode_t     *inode = NULL;
        int          ret   = 0;

        local = mem_get0 (THIS->local_pool);
        if (!local)
                goto out;

        if (loc) {
                ret = loc_copy (&local->loc, loc);
                if (ret)
                        goto out;

                inode = loc->inode;
        }

        if (fd) {
                local->fd = fd_ref (fd);
                if (!inode)
                        inode = fd->inode;
        }

        local->op_ret   = -1;
        local->op_errno = EUCLEAN;
        local->fop      = fop;

        if (inode) {
                local->layout   = dht_layout_get (frame->this, inode);
                local->cached_subvol = dht_subvol_get_cached (frame->this,
                                                              inode);
        }

        frame->local = local;

out:
        if (ret) {
                if (local)
                        mem_put (local);
                local = NULL;
        }
        return local;
}

xlator_t *
dht_first_up_subvol (xlator_t *this)
{
        dht_conf_t *conf = NULL;
        xlator_t   *child = NULL;
        int         i = 0;
        time_t      time = 0;

        conf = this->private;
        if (!conf)
                goto out;

        LOCK (&conf->subvolume_lock);
        {
                for (i = 0; i < conf->subvolume_cnt; i++) {
                        if (conf->subvol_up_time[i]) {
                                if (!time) {
                                        time = conf->subvol_up_time[i];
                                        child = conf->subvolumes[i];
                                } else if (time > conf->subvol_up_time[i]) {
                                        time  = conf->subvol_up_time[i];
                                        child = conf->subvolumes[i];
                                }
                        }
                }
        }
        UNLOCK (&conf->subvolume_lock);

out:
        return child;
}

xlator_t *
dht_last_up_subvol (xlator_t *this)
{
        dht_conf_t *conf = NULL;
        xlator_t   *child = NULL;
        int         i = 0;

        conf = this->private;
        if (!conf)
                goto out;

        LOCK (&conf->subvolume_lock);
        {
                for (i = conf->subvolume_cnt-1; i >= 0; i--) {
                        if (conf->subvolume_status[i]) {
                                child = conf->subvolumes[i];
                                break;
                        }
                }
        }
        UNLOCK (&conf->subvolume_lock);

out:
        return child;
}

xlator_t *
dht_subvol_get_hashed (xlator_t *this, loc_t *loc)
{
        dht_layout_t *layout = NULL;
        xlator_t     *subvol = NULL;
        dht_conf_t *conf = NULL;
        dht_methods_t *methods = NULL;

        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);

        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, conf, out);

        methods = conf->methods;
        GF_VALIDATE_OR_GOTO (this->name, conf->methods, out);

        if (__is_root_gfid (loc->gfid)) {
                subvol = dht_first_up_subvol (this);
                goto out;
        }

        GF_VALIDATE_OR_GOTO (this->name, loc->parent, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, out);

        layout = dht_layout_get (this, loc->parent);

        if (!layout) {
                gf_msg_debug (this->name, 0,
                              "Missing layout. path=%s, parent gfid =%s",
                              loc->path, uuid_utoa (loc->parent->gfid));
                goto out;
        }

        subvol = methods->layout_search (this, layout, loc->name);

        if (!subvol) {
                gf_msg_debug (this->name, 0,
                              "No hashed subvolume for path=%s",
                              loc->path);
                goto out;
        }

out:
        if (layout) {
                dht_layout_unref (this, layout);
        }

        return subvol;
}


xlator_t *
dht_subvol_get_cached (xlator_t *this, inode_t *inode)
{
        dht_layout_t *layout = NULL;
        xlator_t     *subvol = NULL;

        GF_VALIDATE_OR_GOTO (this->name, this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        layout = dht_layout_get (this, inode);

        if (!layout) {
                goto out;
        }

        subvol = layout->list[0].xlator;

out:
        if (layout) {
                dht_layout_unref (this, layout);
        }

        return subvol;
}


xlator_t *
dht_subvol_next (xlator_t *this, xlator_t *prev)
{
        dht_conf_t *conf = NULL;
        int         i = 0;
        xlator_t   *next = NULL;

        conf = this->private;
        if (!conf)
                goto out;

        for (i = 0; i < conf->subvolume_cnt; i++) {
                if (conf->subvolumes[i] == prev) {
                        if ((i + 1) < conf->subvolume_cnt)
                                next = conf->subvolumes[i + 1];
                        break;
                }
        }

out:
        return next;
}

/* This func wraps around, if prev is actually the last subvol.
 */
xlator_t *
dht_subvol_next_available (xlator_t *this, xlator_t *prev)
{
        dht_conf_t *conf = NULL;
        int         i = 0;
        xlator_t   *next = NULL;

        conf = this->private;
        if (!conf)
                goto out;

        for (i = 0; i < conf->subvolume_cnt; i++) {
                if (conf->subvolumes[i] == prev) {
                        /* if prev is last in conf->subvolumes, then wrap
                         * around.
                         */
                        if ((i + 1) < conf->subvolume_cnt) {
                                next = conf->subvolumes[i + 1];
                        } else {
                                next = conf->subvolumes[0];
                        }
                        break;
                }
        }

out:
        return next;
}
int
dht_subvol_cnt (xlator_t *this, xlator_t *subvol)
{
        int i = 0;
        int ret = -1;
        dht_conf_t *conf = NULL;

        conf = this->private;
        if (!conf)
                goto out;

        for (i = 0; i < conf->subvolume_cnt; i++) {
                if (subvol == conf->subvolumes[i]) {
                        ret = i;
                        break;
                }
        }

out:
        return ret;
}


#define set_if_greater(a, b) do {               \
                if ((a) < (b))                  \
                        (a) = (b);              \
        } while (0)


#define set_if_greater_time(a, an, b, bn) do {                          \
                if (((a) < (b)) || (((a) == (b)) && ((an) < (bn)))){    \
                        (a) = (b);                                      \
                        (an) = (bn);                                    \
                }                                                       \
        } while (0)                                                     \


int
dht_iatt_merge (xlator_t *this, struct iatt *to,
                struct iatt *from, xlator_t *subvol)
{
        if (!from || !to)
                return 0;

        to->ia_dev      = from->ia_dev;

        gf_uuid_copy (to->ia_gfid, from->ia_gfid);

        to->ia_ino      = from->ia_ino;
        to->ia_prot     = from->ia_prot;
        to->ia_type     = from->ia_type;
        to->ia_nlink    = from->ia_nlink;
        to->ia_rdev     = from->ia_rdev;
        to->ia_size    += from->ia_size;
        to->ia_blksize  = from->ia_blksize;
        to->ia_blocks  += from->ia_blocks;

        set_if_greater (to->ia_uid, from->ia_uid);
        set_if_greater (to->ia_gid, from->ia_gid);

        set_if_greater_time(to->ia_atime, to->ia_atime_nsec,
                            from->ia_atime, from->ia_atime_nsec);
        set_if_greater_time (to->ia_mtime, to->ia_mtime_nsec,
                             from->ia_mtime, from->ia_mtime_nsec);
        set_if_greater_time (to->ia_ctime, to->ia_ctime_nsec,
                             from->ia_ctime, from->ia_ctime_nsec);

        return 0;
}

int
dht_build_child_loc (xlator_t *this, loc_t *child, loc_t *parent, char *name)
{
        if (!child) {
                goto err;
        }

        if (strcmp (parent->path, "/") == 0)
                gf_asprintf ((char **)&child->path, "/%s", name);
        else
                gf_asprintf ((char **)&child->path, "%s/%s", parent->path, name);

        if (!child->path) {
                goto err;
        }

        child->name = strrchr (child->path, '/');
        if (child->name)
                child->name++;

        child->parent = inode_ref (parent->inode);
        child->inode = inode_new (parent->inode->table);

        if (!child->inode) {
                goto err;
        }

        return 0;
err:
        loc_wipe (child);
        return -1;
}

int
dht_init_local_subvolumes (xlator_t *this, dht_conf_t *conf)
{
        xlator_list_t *subvols = NULL;
        int            cnt = 0;

        if (!conf)
                return -1;

        for (subvols = this->children; subvols; subvols = subvols->next)
                cnt++;

        conf->local_subvols = GF_CALLOC (cnt, sizeof (xlator_t *),
                                        gf_dht_mt_xlator_t);
        if (!conf->local_subvols) {
                return -1;
        }

        conf->local_subvols_cnt = 0;

        return 0;
}

int
dht_init_subvolumes (xlator_t *this, dht_conf_t *conf)
{
        xlator_list_t *subvols = NULL;
        int            cnt = 0;

        if (!conf)
                return -1;

        for (subvols = this->children; subvols; subvols = subvols->next)
                cnt++;

        conf->subvolumes = GF_CALLOC (cnt, sizeof (xlator_t *),
                                      gf_dht_mt_xlator_t);
        if (!conf->subvolumes) {
                return -1;
        }
        conf->subvolume_cnt = cnt;

        conf->local_subvols_cnt = 0;

        dht_set_subvol_range(this);

        cnt = 0;
        for (subvols = this->children; subvols; subvols = subvols->next)
                conf->subvolumes[cnt++] = subvols->xlator;

        conf->subvolume_status = GF_CALLOC (cnt, sizeof (char),
                                            gf_dht_mt_char);
        if (!conf->subvolume_status) {
                return -1;
        }

        conf->last_event = GF_CALLOC (cnt, sizeof (int),
                                      gf_dht_mt_char);
        if (!conf->last_event) {
                return -1;
        }

        conf->subvol_up_time = GF_CALLOC (cnt, sizeof (time_t),
                                          gf_dht_mt_subvol_time);
        if (!conf->subvol_up_time) {
                return -1;
        }

        conf->du_stats = GF_CALLOC (conf->subvolume_cnt, sizeof (dht_du_t),
                                    gf_dht_mt_dht_du_t);
        if (!conf->du_stats) {
                return -1;
        }

        conf->decommissioned_bricks = GF_CALLOC (cnt, sizeof (xlator_t *),
                                                 gf_dht_mt_xlator_t);
        if (!conf->decommissioned_bricks) {
                return -1;
        }

        return 0;
}




static int
dht_migration_complete_check_done (int op_ret, call_frame_t *frame, void *data)
{
        dht_local_t *local  = NULL;
        xlator_t    *subvol = NULL;

        local = frame->local;

        if (op_ret == -1)
                goto out;

        if (local->cached_subvol == NULL) {
                local->op_errno = EINVAL;
                goto out;
        }

        subvol = local->cached_subvol;

out:
        local->rebalance.target_op_fn (THIS, subvol, frame);

        return 0;
}


int
dht_migration_complete_check_task (void *data)
{
        int                 ret         = -1;
        xlator_t           *src_node    = NULL;
        xlator_t           *dst_node    = NULL, *linkto_target = NULL;
        dht_local_t        *local       = NULL;
        dict_t             *dict        = NULL;
        struct iatt         stbuf       = {0,};
        xlator_t           *this        = NULL;
        call_frame_t       *frame       = NULL;
        loc_t               tmp_loc     = {0,};
        char               *path        = NULL;
        dht_conf_t         *conf        = NULL;
        inode_t            *inode       = NULL;
        fd_t               *iter_fd     = NULL;
        uint64_t            tmp_miginfo = 0;
        dht_migrate_info_t *miginfo     = NULL;
        int                 open_failed = 0;

        this  = THIS;
        frame = data;
        local = frame->local;
        conf = this->private;

        src_node = local->cached_subvol;

        if (!local->loc.inode && !local->fd) {
                local->op_errno = EINVAL;
                goto out;
        }

        inode = (!local->fd) ? local->loc.inode : local->fd->inode;

        /* getxattr on cached_subvol for 'linkto' value. Do path based getxattr
         * as root:root. If a fd is already open, access check wont be done*/

        if (!local->loc.inode) {
                ret = syncop_fgetxattr (src_node, local->fd, &dict,
                                        conf->link_xattr_name, NULL, NULL);
        } else {
                SYNCTASK_SETID (0, 0);
                ret = syncop_getxattr (src_node, &local->loc, &dict,
                                       conf->link_xattr_name, NULL, NULL);
                SYNCTASK_SETID (frame->root->uid, frame->root->gid);
        }

        /*
         * temporary check related to tier promoting/demoting the file;
         * the lower level DHT detects the migration (due to sticky
         * bits) when it is the responsibility of the tier translator
         * to complete the rebalance transaction. It will be corrected
         * when rebalance and tier migration are fixed to work together.
         */
        if (strcmp(this->parents->xlator->type, "cluster/tier") == 0) {
                ret = 0;
                goto out;
        }

        if (!ret)
                linkto_target = dht_linkfile_subvol (this, NULL, NULL, dict);

        if (local->loc.inode) {
                loc_copy (&tmp_loc, &local->loc);
        } else {
                tmp_loc.inode = inode_ref (inode);
                gf_uuid_copy (tmp_loc.gfid, inode->gfid);
        }

        ret = syncop_lookup (this, &tmp_loc, &stbuf, 0, 0, 0);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, -ret,
                        DHT_MSG_FILE_LOOKUP_FAILED,
                        "%s: failed to lookup the file on %s",
                        tmp_loc.path ? tmp_loc.path : uuid_utoa (tmp_loc.gfid),
                        this->name);
                local->op_errno = -ret;
                ret = -1;
                goto out;
        }

        dst_node = dht_subvol_get_cached (this, tmp_loc.inode);
        if (linkto_target && dst_node != linkto_target) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_INVALID_LINKFILE,
                        "linkto target (%s) is "
                        "different from cached-subvol (%s). Treating %s as "
                        "destination subvol", linkto_target->name,
                        dst_node->name, dst_node->name);
        }

        if (gf_uuid_compare (stbuf.ia_gfid, tmp_loc.inode->gfid)) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_GFID_MISMATCH,
                                "%s: gfid different on the target file on %s",
                                tmp_loc.path ? tmp_loc.path :
                                uuid_utoa (tmp_loc.gfid), dst_node->name);
                ret = -1;
                local->op_errno = EIO;
                goto out;
        }

        /* update local. A layout is set in inode-ctx in lookup already */

        dht_layout_unref (this, local->layout);

        local->layout   = dht_layout_get (frame->this, inode);
        local->cached_subvol = dst_node;

        ret = 0;

        /* once we detect the migration complete, the inode-ctx2 is no more
           required.. delete the ctx and also, it means, open() already
           done on all the fd of inode */
        ret = inode_ctx_reset1 (inode, this, &tmp_miginfo);
        if (tmp_miginfo) {
                miginfo = (void *)tmp_miginfo;
                GF_REF_PUT (miginfo);
                goto out;
        }

        if (list_empty (&inode->fd_list))
                goto out;

        /* perform open as root:root. There is window between linkfile
         * creation(root:root) and setattr with the correct uid/gid
         */
        SYNCTASK_SETID(0, 0);

        /* perform 'open()' on all the fd's present on the inode */
        tmp_loc.inode = inode;
        inode_path (inode, NULL, &path);
        if (path)
                tmp_loc.path = path;
        list_for_each_entry (iter_fd, &inode->fd_list, inode_list) {
                if (fd_is_anonymous (iter_fd))
                        continue;

                /* flags for open are stripped down to allow following the
                 * new location of the file, otherwise we can get EEXIST or
                 * truncate the file again as rebalance is moving the data */
                ret = syncop_open (dst_node, &tmp_loc,
                                   (iter_fd->flags &
                                   ~(O_CREAT | O_EXCL | O_TRUNC)), iter_fd,
                                   NULL, NULL);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, -ret,
                                DHT_MSG_OPEN_FD_ON_DST_FAILED, "failed to open "
                                "the fd (%p, flags=0%o) on file %s @ %s",
                                iter_fd, iter_fd->flags, path, dst_node->name);
                        open_failed = 1;
                        local->op_errno = -ret;
                        ret = -1;
                }
        }

        SYNCTASK_SETID (frame->root->uid, frame->root->gid);

        if (open_failed) {
                ret = -1;
                goto out;
        }
        ret = 0;
out:

        loc_wipe (&tmp_loc);

        return ret;
}

int
dht_rebalance_complete_check (xlator_t *this, call_frame_t *frame)
{
        int         ret     = -1;

        ret = synctask_new (this->ctx->env, dht_migration_complete_check_task,
                            dht_migration_complete_check_done,
                            frame, frame);
        return ret;

}
/* During 'in-progress' state, both nodes should have the file */
static int
dht_inprogress_check_done (int op_ret, call_frame_t *frame, void *data)
{
        dht_local_t *local      = NULL;
        xlator_t    *dst_subvol = NULL, *src_subvol = NULL;
        inode_t     *inode      = NULL;

        local = frame->local;

        if (op_ret == -1)
                goto out;

        inode = local->loc.inode ? local->loc.inode : local->fd->inode;

        dht_inode_ctx_get_mig_info (THIS, inode, &src_subvol, &dst_subvol);
        if (dht_mig_info_is_invalid (local->cached_subvol,
                                     src_subvol, dst_subvol)) {
                dst_subvol = dht_subvol_get_cached (THIS, inode);
                if (!dst_subvol) {
                        local->op_errno = EINVAL;
                        goto out;
                }
        }

out:
        local->rebalance.target_op_fn (THIS, dst_subvol, frame);

        return 0;
}

static int
dht_rebalance_inprogress_task (void *data)
{
        int           ret      = -1;
        xlator_t     *src_node = NULL;
        xlator_t     *dst_node = NULL;
        dht_local_t  *local    = NULL;
        dict_t       *dict     = NULL;
        call_frame_t *frame    = NULL;
        xlator_t     *this     = NULL;
        char         *path     = NULL;
        struct iatt   stbuf    = {0,};
        loc_t         tmp_loc  = {0,};
        dht_conf_t   *conf     = NULL;
        inode_t      *inode    = NULL;
        fd_t         *iter_fd  = NULL;
        int           open_failed = 0;

        this  = THIS;
        frame = data;
        local = frame->local;
        conf = this->private;

        src_node = local->cached_subvol;

        if (!local->loc.inode && !local->fd)
                goto out;

        inode = (!local->fd) ? local->loc.inode : local->fd->inode;

        /* getxattr on cached_subvol for 'linkto' value. Do path based getxattr
         * as root:root. If a fd is already open, access check wont be done*/
        if (local->loc.inode) {
                SYNCTASK_SETID (0, 0);
                ret = syncop_getxattr (src_node, &local->loc, &dict,
                                       conf->link_xattr_name, NULL, NULL);
                SYNCTASK_SETID (frame->root->uid, frame->root->gid);
        } else {
                ret = syncop_fgetxattr (src_node, local->fd, &dict,
                                        conf->link_xattr_name, NULL, NULL);
        }

        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, -ret,
                        DHT_MSG_GET_XATTR_FAILED,
                        "%s: failed to get the 'linkto' xattr",
                        local->loc.path);
                ret = -1;
                goto out;
        }

        dst_node = dht_linkfile_subvol (this, NULL, NULL, dict);
        if (!dst_node) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_SUBVOL_NOT_FOUND,
                        "%s: failed to get the 'linkto' xattr from dict",
                        local->loc.path);
                ret = -1;
                goto out;
        }

        local->rebalance.target_node = dst_node;

        if (local->loc.inode) {
                loc_copy (&tmp_loc, &local->loc);
        } else {
                tmp_loc.inode = inode_ref (inode);
                gf_uuid_copy (tmp_loc.gfid, inode->gfid);
        }

        /* lookup on dst */
        ret = syncop_lookup (dst_node, &tmp_loc, &stbuf, NULL,
                             NULL, NULL);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, -ret,
                        DHT_MSG_FILE_LOOKUP_ON_DST_FAILED,
                        "%s: failed to lookup the file on %s",
                        tmp_loc.path ? tmp_loc.path : uuid_utoa (tmp_loc.gfid),
                        dst_node->name);
                ret = -1;
                goto out;
        }

        if (gf_uuid_compare (stbuf.ia_gfid, tmp_loc.inode->gfid)) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_GFID_MISMATCH,
                        "%s: gfid different on the target file on %s",
                        tmp_loc.path ? tmp_loc.path : uuid_utoa (tmp_loc.gfid),
                        dst_node->name);
                ret = -1;
                goto out;
        }
        ret = 0;

        if (list_empty (&inode->fd_list))
                goto done;

        /* perform open as root:root. There is window between linkfile
         * creation(root:root) and setattr with the correct uid/gid
         */
        SYNCTASK_SETID (0, 0);

        inode_path (inode, NULL, &path);
        if (path)
                tmp_loc.path = path;

        list_for_each_entry (iter_fd, &inode->fd_list, inode_list) {
                if (fd_is_anonymous (iter_fd))
                        continue;

                /* flags for open are stripped down to allow following the
                 * new location of the file, otherwise we can get EEXIST or
                 * truncate the file again as rebalance is moving the data */
                ret = syncop_open (dst_node, &tmp_loc,
                                   (iter_fd->flags &
                                   ~(O_CREAT | O_EXCL | O_TRUNC)), iter_fd,
                                   NULL, NULL);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, -ret,
                                DHT_MSG_OPEN_FD_ON_DST_FAILED,
                                "failed to send open "
                                "the fd (%p, flags=0%o) on file %s @ %s",
                                iter_fd, iter_fd->flags, path, dst_node->name);
                        ret = -1;
                        open_failed = 1;
                }
        }

        SYNCTASK_SETID (frame->root->uid, frame->root->gid);

        if (open_failed) {
                ret = -1;
                goto out;
        }

done:
        ret = dht_inode_ctx_set_mig_info (this, inode, src_node, dst_node);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_SET_INODE_CTX_FAILED,
                        "%s: failed to set inode-ctx target file at %s",
                        local->loc.path, dst_node->name);
                goto out;
        }

        ret = 0;
out:
        loc_wipe (&tmp_loc);
        return ret;
}

int
dht_rebalance_in_progress_check (xlator_t *this, call_frame_t *frame)
{

        int         ret     = -1;

        ret = synctask_new (this->ctx->env, dht_rebalance_inprogress_task,
                            dht_inprogress_check_done,
                            frame, frame);
        return ret;
}

int
dht_inode_ctx_layout_set (inode_t *inode, xlator_t *this,
                          dht_layout_t *layout_int)
{
        dht_inode_ctx_t         *ctx            = NULL;
        int                      ret            = -1;

        ret = dht_inode_ctx_get (inode, this, &ctx);
        if (!ret && ctx) {
                ctx->layout = layout_int;
        } else {
                ctx = GF_CALLOC (1, sizeof (*ctx), gf_dht_mt_inode_ctx_t);
                if (!ctx)
                        return ret;
                ctx->layout = layout_int;
        }

        ret = dht_inode_ctx_set (inode, this, ctx);

        return ret;
}


void
dht_inode_ctx_time_set (inode_t *inode, xlator_t *this, struct iatt *stat)
{
        dht_inode_ctx_t         *ctx            = NULL;
        dht_stat_time_t         *time           = 0;
        int                      ret            = -1;

        ret = dht_inode_ctx_get (inode, this, &ctx);

        if (ret)
		return;

        time = &ctx->time;

	time->mtime      = stat->ia_mtime;
	time->mtime_nsec = stat->ia_mtime_nsec;

	time->ctime      = stat->ia_ctime;
	time->ctime_nsec = stat->ia_ctime_nsec;

	time->atime      = stat->ia_atime;
	time->atime_nsec = stat->ia_atime_nsec;

	return;
}


int
dht_inode_ctx_time_update (inode_t *inode, xlator_t *this, struct iatt *stat,
                           int32_t post)
{
        dht_inode_ctx_t         *ctx            = NULL;
        dht_stat_time_t         *time           = 0;
        int                      ret            = -1;

        GF_VALIDATE_OR_GOTO (this->name, stat, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        ret = dht_inode_ctx_get (inode, this, &ctx);

        if (ret) {
                ctx = GF_CALLOC (1, sizeof (*ctx), gf_dht_mt_inode_ctx_t);
                if (!ctx)
                        return -1;
        }

        time = &ctx->time;

        DHT_UPDATE_TIME(time->mtime, time->mtime_nsec,
                        stat->ia_mtime, stat->ia_mtime_nsec, inode, post);
        DHT_UPDATE_TIME(time->ctime, time->ctime_nsec,
                        stat->ia_ctime, stat->ia_ctime_nsec, inode, post);
        DHT_UPDATE_TIME(time->atime, time->atime_nsec,
                        stat->ia_atime, stat->ia_atime_nsec, inode, post);

        ret = dht_inode_ctx_set (inode, this, ctx);
out:
        return 0;
}

int
dht_inode_ctx_get (inode_t *inode, xlator_t *this, dht_inode_ctx_t **ctx)
{
        int             ret     = -1;
        uint64_t        ctx_int = 0;

        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        ret = inode_ctx_get (inode, this, &ctx_int);

        if (ret)
                return ret;

        if (ctx)
                *ctx = (dht_inode_ctx_t *) ctx_int;
out:
        return ret;
}

int dht_inode_ctx_set (inode_t *inode, xlator_t *this, dht_inode_ctx_t *ctx)
{
        int             ret = -1;
        uint64_t        ctx_int = 0;

        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);
        GF_VALIDATE_OR_GOTO (this->name, ctx, out);

        ctx_int = (long)ctx;
        ret = inode_ctx_set (inode, this, &ctx_int);
out:
        return ret;
}

void
dht_set_lkowner (dht_lock_t **lk_array, int count, gf_lkowner_t *lkowner)
{
        int i = 0;

        if (!lk_array || !lkowner)
                goto out;

        for (i = 0; i < count; i++) {
                lk_array[i]->lk_owner = *lkowner;
        }

out:
        return;
}

int
dht_subvol_status (dht_conf_t *conf, xlator_t *subvol)
{
        int i;

        for (i=0 ; i < conf->subvolume_cnt; i++) {
                if (conf->subvolumes[i] == subvol) {
                        return conf->subvolume_status[i];
                }
        }
        return 0;
}

void
dht_inodelk_done (call_frame_t *lock_frame)
{
        fop_inodelk_cbk_t  inodelk_cbk = NULL;
        call_frame_t      *main_frame  = NULL;
        dht_local_t       *local       = NULL;

        local = lock_frame->local;
        main_frame = local->main_frame;

        local->lock.locks = NULL;
        local->lock.lk_count = 0;

        inodelk_cbk = local->lock.inodelk_cbk;
        local->lock.inodelk_cbk = NULL;

        inodelk_cbk (main_frame, NULL, main_frame->this, local->lock.op_ret,
                     local->lock.op_errno, NULL);

        dht_lock_stack_destroy (lock_frame);
        return;
}

int
dht_inodelk_cleanup_cbk (call_frame_t *frame, void *cookie,
                         xlator_t *this, int32_t op_ret, int32_t op_errno,
                         dict_t *xdata)
{
        dht_inodelk_done (frame);
        return 0;
}

int32_t
dht_lock_count (dht_lock_t **lk_array, int lk_count)
{
        int i = 0, locked = 0;

        if ((lk_array == NULL) || (lk_count == 0))
                goto out;

        for (i = 0; i < lk_count; i++) {
                if (lk_array[i]->locked)
                        locked++;
        }
out:
        return locked;
}

void
dht_inodelk_cleanup (call_frame_t *lock_frame)
{
        dht_lock_t  **lk_array = NULL;
        int           lk_count = 0, lk_acquired = 0;
        dht_local_t  *local    = NULL;

        local = lock_frame->local;

        lk_array = local->lock.locks;
        lk_count = local->lock.lk_count;

        lk_acquired = dht_lock_count (lk_array, lk_count);
        if (lk_acquired != 0) {
                dht_unlock_inodelk (lock_frame, lk_array, lk_count,
                                    dht_inodelk_cleanup_cbk);
        } else {
                dht_inodelk_done (lock_frame);
        }

        return;
}

int32_t
dht_unlock_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        dht_local_t *local                  = NULL;
        int          lk_index               = 0, call_cnt = 0;
        char         gfid[GF_UUID_BUF_SIZE] = {0};

        lk_index = (long) cookie;

        local = frame->local;
        if (op_ret < 0) {
                uuid_utoa_r (local->lock.locks[lk_index]->loc.gfid,
                             gfid);

                gf_msg (this->name, GF_LOG_WARNING, op_errno,
                        DHT_MSG_UNLOCKING_FAILED,
                        "unlocking failed on %s:%s",
                        local->lock.locks[lk_index]->xl->name,
                        gfid);
        } else {
                local->lock.locks[lk_index]->locked = 0;
        }

        call_cnt = dht_frame_return (frame);
        if (is_last_call (call_cnt)) {
                dht_inodelk_done (frame);
        }

        return 0;
}

call_frame_t *
dht_lock_frame (call_frame_t *parent_frame)
{
        call_frame_t *lock_frame = NULL;

        lock_frame = copy_frame (parent_frame);
        if (lock_frame == NULL)
                goto out;

        set_lk_owner_from_ptr (&lock_frame->root->lk_owner, parent_frame->root);

out:
        return lock_frame;
}

int32_t
dht_unlock_inodelk (call_frame_t *frame, dht_lock_t **lk_array, int lk_count,
                    fop_inodelk_cbk_t inodelk_cbk)
{
        dht_local_t     *local      = NULL;
        struct gf_flock  flock      = {0,};
        int              ret        = -1 , i = 0;
        call_frame_t    *lock_frame = NULL;
        int              call_cnt   = 0;

        GF_VALIDATE_OR_GOTO ("dht-locks", frame, done);
        GF_VALIDATE_OR_GOTO (frame->this->name, lk_array, done);
        GF_VALIDATE_OR_GOTO (frame->this->name, inodelk_cbk, done);

        call_cnt = dht_lock_count (lk_array, lk_count);
        if (call_cnt == 0) {
                ret = 0;
                goto done;
        }

        lock_frame = dht_lock_frame (frame);
        if (lock_frame == NULL) {
                gf_msg (frame->this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_UNLOCKING_FAILED,
                        "cannot allocate a frame, not unlocking following "
                        "locks:");

                dht_log_lk_array (frame->this->name, GF_LOG_WARNING, lk_array,
                                  lk_count);
                goto done;
        }

        ret = dht_local_lock_init (lock_frame, lk_array, lk_count, inodelk_cbk);
        if (ret < 0) {
                gf_msg (frame->this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_UNLOCKING_FAILED,
                        "storing locks in local failed, not unlocking "
                        "following locks:");

                dht_log_lk_array (frame->this->name, GF_LOG_WARNING, lk_array,
                                  lk_count);

                goto done;
        }

        local = lock_frame->local;
        local->main_frame = frame;
        local->call_cnt = call_cnt;

        flock.l_type = F_UNLCK;

        for (i = 0; i < local->lock.lk_count; i++) {
                if (!local->lock.locks[i]->locked)
                        continue;

                lock_frame->root->lk_owner = local->lock.locks[i]->lk_owner;
                STACK_WIND_COOKIE (lock_frame, dht_unlock_inodelk_cbk,
                                   (void *)(long)i,
                                   local->lock.locks[i]->xl,
                                   local->lock.locks[i]->xl->fops->inodelk,
                                   local->lock.locks[i]->domain,
                                   &local->lock.locks[i]->loc, F_SETLK,
                                   &flock, NULL);
                if (!--call_cnt)
                        break;
        }

        return 0;

done:
        if (lock_frame)
                dht_lock_stack_destroy (lock_frame);

        /* no locks acquired, invoke inodelk_cbk */
        if (ret == 0)
                inodelk_cbk (frame, NULL, frame->this, 0, 0, NULL);

        return ret;
}

int32_t
dht_nonblocking_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        dht_local_t *local                   = NULL;
        int          lk_index               = 0, call_cnt = 0;
        char          gfid[GF_UUID_BUF_SIZE] = {0};

        local = frame->local;
        lk_index = (long) cookie;

        if (op_ret == -1) {
                local->lock.op_ret = -1;
                local->lock.op_errno = op_errno;

                if (local && local->lock.locks[lk_index]) {
                        uuid_utoa_r (local->lock.locks[lk_index]->loc.inode->gfid,
                                     gfid);

                        gf_msg_debug (this->name, op_errno,
                                      "inodelk failed on gfid: %s "
                                      "subvolume: %s", gfid,
                                      local->lock.locks[lk_index]->xl->name);
                }

                goto out;
        }

        local->lock.locks[lk_index]->locked = _gf_true;

out:
        call_cnt = dht_frame_return (frame);
        if (is_last_call (call_cnt)) {
                if (local->lock.op_ret < 0) {
                        dht_inodelk_cleanup (frame);
                        return 0;
                }

                dht_inodelk_done (frame);
        }

        return 0;
}

int
dht_nonblocking_inodelk (call_frame_t *frame, dht_lock_t **lk_array,
                         int lk_count, fop_inodelk_cbk_t inodelk_cbk)
{
        struct gf_flock  flock      = {0,};
        int              i          = 0, ret = 0;
        dht_local_t     *local      = NULL;
        call_frame_t    *lock_frame = NULL;

        GF_VALIDATE_OR_GOTO ("dht-locks", frame, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, lk_array, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, inodelk_cbk, out);

        lock_frame = dht_lock_frame (frame);
        if (lock_frame == NULL)
                goto out;

        ret = dht_local_lock_init (lock_frame, lk_array, lk_count, inodelk_cbk);
        if (ret < 0) {
                goto out;
        }

        dht_set_lkowner (lk_array, lk_count, &lock_frame->root->lk_owner);

        local = lock_frame->local;
        local->main_frame = frame;

        local->call_cnt = lk_count;

        for (i = 0; i < lk_count; i++) {
                flock.l_type = local->lock.locks[i]->type;

                STACK_WIND_COOKIE (lock_frame, dht_nonblocking_inodelk_cbk,
                                   (void *) (long) i,
                                   local->lock.locks[i]->xl,
                                   local->lock.locks[i]->xl->fops->inodelk,
                                   local->lock.locks[i]->domain,
                                   &local->lock.locks[i]->loc, F_SETLK,
                                   &flock, NULL);
        }

        return 0;

out:
        if (lock_frame)
                dht_lock_stack_destroy (lock_frame);

        return -1;
}

int32_t
dht_blocking_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        int          lk_index = 0;
        dht_local_t *local    = NULL;

        lk_index = (long) cookie;

        local = frame->local;

        if (op_ret == 0) {
                local->lock.locks[lk_index]->locked = _gf_true;
        } else {
                local->lock.op_ret = -1;
                local->lock.op_errno = op_errno;
                goto cleanup;
        }

        if (lk_index == (local->lock.lk_count - 1)) {
                dht_inodelk_done (frame);
        } else {
                dht_blocking_inodelk_rec (frame, ++lk_index);
        }

        return 0;

cleanup:
        dht_inodelk_cleanup (frame);

        return 0;
}

void
dht_blocking_inodelk_rec (call_frame_t *frame, int i)
{
        dht_local_t     *local = NULL;
        struct gf_flock  flock = {0,};

        local = frame->local;

        flock.l_type = local->lock.locks[i]->type;

        STACK_WIND_COOKIE (frame, dht_blocking_inodelk_cbk,
                           (void *) (long) i,
                           local->lock.locks[i]->xl,
                           local->lock.locks[i]->xl->fops->inodelk,
                           local->lock.locks[i]->domain,
                           &local->lock.locks[i]->loc, F_SETLKW, &flock, NULL);

        return;
}

int
dht_lock_request_cmp (const void *val1, const void *val2)
{
        dht_lock_t *lock1 = NULL;
        dht_lock_t *lock2 = NULL;
        int         ret   = 0;

        lock1 = *(dht_lock_t **)val1;
        lock2 = *(dht_lock_t **)val2;

        GF_VALIDATE_OR_GOTO ("dht-locks", lock1, out);
        GF_VALIDATE_OR_GOTO ("dht-locks", lock2, out);

        ret = strcmp (lock1->xl->name, lock2->xl->name);

        if (ret == 0) {
                ret = gf_uuid_compare (lock1->loc.gfid, lock2->loc.gfid);
        }

out:
        return ret;
}

int
dht_lock_order_requests (dht_lock_t **locks, int count)
{
        int        ret     = -1;

        if (!locks || !count)
                goto out;

        qsort (locks, count, sizeof (*locks), dht_lock_request_cmp);
        ret = 0;

out:
        return ret;
}

int
dht_blocking_inodelk (call_frame_t *frame, dht_lock_t **lk_array,
                      int lk_count, fop_inodelk_cbk_t inodelk_cbk)
{
        int           ret        = -1;
        call_frame_t *lock_frame = NULL;
        dht_local_t  *local      = NULL;

        GF_VALIDATE_OR_GOTO ("dht-locks", frame, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, lk_array, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, inodelk_cbk, out);

        lock_frame = dht_lock_frame (frame);
        if (lock_frame == NULL)
                goto out;

        ret = dht_local_lock_init (lock_frame, lk_array, lk_count, inodelk_cbk);
        if (ret < 0) {
                goto out;
        }

        dht_set_lkowner (lk_array, lk_count, &lock_frame->root->lk_owner);

        local = lock_frame->local;
        local->main_frame = frame;

        dht_blocking_inodelk_rec (lock_frame, 0);

        return 0;
out:
        if (lock_frame)
                dht_lock_stack_destroy (lock_frame);

        return -1;
}
