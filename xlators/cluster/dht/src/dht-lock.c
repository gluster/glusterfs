/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "dht-lock.h"

static char *
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

static void
dht_log_lk_array (char *name, gf_loglevel_t log_level, dht_lock_t **lk_array,
                  int count)
{
        int   i      = 0;
        char *lk_buf = NULL;

        if ((lk_array == NULL) || (count == 0))
                goto out;

        for (i = 0; i < count; i++) {
                lk_buf = dht_lock_asprintf (lk_array[i]);
                if (!lk_buf)
                        goto out;

                gf_msg (name, log_level, 0, DHT_MSG_LK_ARRAY_INFO,
                        "%d. %s", i, lk_buf);
                GF_FREE (lk_buf);
        }

out:
        return;
}

static void
dht_lock_stack_destroy (call_frame_t *lock_frame, dht_lock_type_t lk)
{
        dht_local_t *local = NULL;

        local = lock_frame->local;

        if (lk == DHT_INODELK) {
                local->lock[0].layout.my_layout.locks = NULL;
                local->lock[0].layout.my_layout.lk_count = 0;
        } else {
                local->lock[0].ns.directory_ns.locks = NULL;
                local->lock[0].ns.directory_ns.lk_count = 0;
        }

        DHT_STACK_DESTROY (lock_frame);
        return;
}

static void
dht_lock_free (dht_lock_t *lock)
{
        if (lock == NULL)
                goto out;

        loc_wipe (&lock->loc);
        GF_FREE (lock->domain);
        GF_FREE (lock->basename);
        mem_put (lock);

out:
        return;
}

static void
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

static int
dht_lock_request_cmp (const void *val1, const void *val2)
{
        dht_lock_t *lock1 = NULL;
        dht_lock_t *lock2 = NULL;
        int         ret   = -1;

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

static int
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

static call_frame_t *
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

dht_lock_t *
dht_lock_new (xlator_t *this, xlator_t *xl, loc_t *loc, short type,
              const char *domain, const char *basename,
              dht_reaction_type_t do_on_failure)
{
        dht_conf_t *conf = NULL;
        dht_lock_t *lock = NULL;

        conf = this->private;

        lock = mem_get0 (conf->lock_pool);
        if (lock == NULL)
                goto out;

        lock->xl = xl;
        lock->type = type;
        lock->do_on_failure = do_on_failure;

        lock->domain = gf_strdup (domain);
        if (lock->domain == NULL) {
                dht_lock_free (lock);
                lock = NULL;
                goto out;
        }

        if (basename) {
                lock->basename = gf_strdup (basename);
                if (lock->basename == NULL) {
                        dht_lock_free (lock);
                        lock = NULL;
                        goto out;
                }
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

static int
dht_local_entrylk_init (call_frame_t *frame, dht_lock_t **lk_array,
                     int lk_count, fop_entrylk_cbk_t entrylk_cbk)
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

        local->lock[0].ns.directory_ns.entrylk_cbk = entrylk_cbk;
        local->lock[0].ns.directory_ns.locks = lk_array;
        local->lock[0].ns.directory_ns.lk_count = lk_count;

        ret = dht_lock_order_requests (local->lock[0].ns.directory_ns.locks,
                                       local->lock[0].ns.directory_ns.lk_count);
        if (ret < 0)
                goto out;

        ret = 0;
out:
        return ret;
}

static void
dht_entrylk_done (call_frame_t *lock_frame)
{
        fop_entrylk_cbk_t  entrylk_cbk = NULL;
        call_frame_t      *main_frame  = NULL;
        dht_local_t       *local       = NULL;

        local = lock_frame->local;
        main_frame = local->main_frame;

        local->lock[0].ns.directory_ns.locks = NULL;
        local->lock[0].ns.directory_ns.lk_count = 0;

        entrylk_cbk = local->lock[0].ns.directory_ns.entrylk_cbk;
        local->lock[0].ns.directory_ns.entrylk_cbk = NULL;

        entrylk_cbk (main_frame, NULL, main_frame->this,
                     local->lock[0].ns.directory_ns.op_ret,
                     local->lock[0].ns.directory_ns.op_errno, NULL);

        dht_lock_stack_destroy (lock_frame, DHT_ENTRYLK);
        return;
}

static int32_t
dht_unlock_entrylk_done (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        dht_local_t *local                   = NULL;
        char          gfid[GF_UUID_BUF_SIZE] = {0};

        local = frame->local;
        gf_uuid_unparse (local->lock[0].ns.directory_ns.locks[0]->loc.inode->gfid, gfid);

        if (op_ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, op_errno,
                        DHT_MSG_PARENT_LAYOUT_CHANGED,
                        "unlock failed on gfid: %s, stale lock might be left "
                        "in DHT_LAYOUT_HEAL_DOMAIN", gfid);
        }

        DHT_STACK_DESTROY (frame);
        return 0;
}

static int32_t
dht_unlock_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        dht_local_t *local                  = NULL;
        int          lk_index               = 0, call_cnt = 0;
        char         gfid[GF_UUID_BUF_SIZE] = {0};

        lk_index = (long) cookie;

        local = frame->local;

        uuid_utoa_r (local->lock[0].ns.directory_ns.locks[lk_index]->loc.gfid, gfid);

        if (op_ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, op_errno,
                        DHT_MSG_UNLOCKING_FAILED,
                        "unlocking failed on %s:%s",
                        local->lock[0].ns.directory_ns.locks[lk_index]->xl->name,
                        gfid);
        } else {
                local->lock[0].ns.directory_ns.locks[lk_index]->locked = 0;
        }

        call_cnt = dht_frame_return (frame);
        if (is_last_call (call_cnt)) {
                dht_entrylk_done (frame);
        }

        return 0;
}

static int32_t
dht_unlock_entrylk (call_frame_t *frame, dht_lock_t **lk_array, int lk_count,
                     fop_entrylk_cbk_t entrylk_cbk)
{
        dht_local_t     *local      = NULL;
        int              ret        = -1 , i = 0;
        call_frame_t    *lock_frame = NULL;
        int              call_cnt   = 0;

        GF_VALIDATE_OR_GOTO ("dht-locks", frame, done);
        GF_VALIDATE_OR_GOTO (frame->this->name, lk_array, done);
        GF_VALIDATE_OR_GOTO (frame->this->name, entrylk_cbk, done);

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
                        "entrylks:");

                dht_log_lk_array (frame->this->name, GF_LOG_WARNING, lk_array,
                                  lk_count);
                goto done;
        }

        ret = dht_local_entrylk_init (lock_frame, lk_array, lk_count,
                                      entrylk_cbk);
        if (ret < 0) {
                gf_msg (frame->this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_UNLOCKING_FAILED,
                        "storing locks in local failed, not unlocking "
                        "following entrylks:");

                dht_log_lk_array (frame->this->name, GF_LOG_WARNING, lk_array,
                                  lk_count);

                goto done;
        }

        local = lock_frame->local;
        local->main_frame = frame;
        local->call_cnt = call_cnt;

        for (i = 0; i < local->lock[0].ns.directory_ns.lk_count; i++) {
                if (!local->lock[0].ns.directory_ns.locks[i]->locked)
                        continue;

                lock_frame->root->lk_owner = local->lock[0].ns.directory_ns.locks[i]->lk_owner;
                STACK_WIND_COOKIE (lock_frame, dht_unlock_entrylk_cbk,
                                   (void *)(long)i,
                                   local->lock[0].ns.directory_ns.locks[i]->xl,
                                   local->lock[0].ns.directory_ns.locks[i]->xl->fops->entrylk,
                                   local->lock[0].ns.directory_ns.locks[i]->domain,
                                   &local->lock[0].ns.directory_ns.locks[i]->loc,
                                   local->lock[0].ns.directory_ns.locks[i]->basename,
                                   ENTRYLK_UNLOCK, ENTRYLK_WRLCK, NULL);
                if (!--call_cnt)
                        break;
        }

        return 0;

done:
        if (lock_frame)
                dht_lock_stack_destroy (lock_frame, DHT_ENTRYLK);

        /* no locks acquired, invoke entrylk_cbk */
        if (ret == 0)
                entrylk_cbk (frame, NULL, frame->this, 0, 0, NULL);

        return ret;
}

int32_t
dht_unlock_entrylk_wrapper (call_frame_t *frame, dht_elock_wrap_t *entrylk)
{
        dht_local_t  *local                   = NULL, *lock_local = NULL;
        call_frame_t *lock_frame              = NULL;
        char          pgfid[GF_UUID_BUF_SIZE] = {0};
        int           ret                     = 0;

        local = frame->local;

        if (!entrylk || !entrylk->locks)
                goto out;

        gf_uuid_unparse (local->loc.parent->gfid, pgfid);

        lock_frame = copy_frame (frame);
        if (lock_frame == NULL) {
                gf_msg (frame->this->name, GF_LOG_WARNING, ENOMEM,
                        DHT_MSG_PARENT_LAYOUT_CHANGED,
                        "mkdir (%s/%s) (path: %s): "
                        "copy frame failed", pgfid, local->loc.name,
                        local->loc.path);
                goto done;
        }

        lock_local = dht_local_init (lock_frame, NULL, NULL, 0);
        if (lock_local == NULL) {
                gf_msg (frame->this->name, GF_LOG_WARNING, ENOMEM,
                        DHT_MSG_PARENT_LAYOUT_CHANGED,
                        "mkdir (%s/%s) (path: %s): "
                        "local creation failed", pgfid, local->loc.name,
                        local->loc.path);
                goto done;
        }

        lock_frame->local = lock_local;

        lock_local->lock[0].ns.directory_ns.locks = entrylk->locks;
        lock_local->lock[0].ns.directory_ns.lk_count = entrylk->lk_count;
        entrylk->locks = NULL;
        entrylk->lk_count = 0;

        ret = dht_unlock_entrylk (lock_frame,
                                  lock_local->lock[0].ns.directory_ns.locks,
                                  lock_local->lock[0].ns.directory_ns.lk_count,
                                  dht_unlock_entrylk_done);
        if (ret)
                goto done;

        lock_frame = NULL;

done:
        if (lock_frame != NULL) {
                DHT_STACK_DESTROY (lock_frame);
        }

out:
        return 0;
}

static int
dht_entrylk_cleanup_cbk (call_frame_t *frame, void *cookie,
                         xlator_t *this, int32_t op_ret, int32_t op_errno,
                         dict_t *xdata)
{
        dht_entrylk_done (frame);
        return 0;
}

static void
dht_entrylk_cleanup (call_frame_t *lock_frame)
{
        dht_lock_t  **lk_array = NULL;
        int           lk_count = 0, lk_acquired = 0;
        dht_local_t  *local    = NULL;

        local = lock_frame->local;

        lk_array = local->lock[0].ns.directory_ns.locks;
        lk_count = local->lock[0].ns.directory_ns.lk_count;

        lk_acquired = dht_lock_count (lk_array, lk_count);
        if (lk_acquired != 0) {
                dht_unlock_entrylk (lock_frame, lk_array, lk_count,
                                     dht_entrylk_cleanup_cbk);
        } else {
                dht_entrylk_done (lock_frame);
        }

        return;
}


static int32_t
dht_blocking_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        int          lk_index = 0;
        int          i        = 0;
        dht_local_t *local    = NULL;

        lk_index = (long) cookie;

        local = frame->local;
        if (op_ret == 0) {
                local->lock[0].ns.directory_ns.locks[lk_index]->locked = _gf_true;
        } else {
                switch (op_errno) {
                case ESTALE:
                case ENOENT:
                        if (local->lock[0].ns.directory_ns.locks[lk_index]->do_on_failure != IGNORE_ENOENT_ESTALE) {
                                local->lock[0].ns.directory_ns.op_ret = -1;
                                local->lock[0].ns.directory_ns.op_errno = op_errno;
                                goto cleanup;
                        }
                        break;
                default:
                        local->lock[0].ns.directory_ns.op_ret = -1;
                        local->lock[0].ns.directory_ns.op_errno = op_errno;
                        goto cleanup;
                }
        }

        if (lk_index == (local->lock[0].ns.directory_ns.lk_count - 1)) {
                for (i = 0; (i < local->lock[0].ns.directory_ns.lk_count) &&
                     (!local->lock[0].ns.directory_ns.locks[i]->locked); i++)
                        ;

                if (i == local->lock[0].ns.directory_ns.lk_count) {
                        local->lock[0].ns.directory_ns.op_ret = -1;
                        local->lock[0].ns.directory_ns.op_errno = op_errno;
                }

                dht_entrylk_done (frame);
        } else {
                dht_blocking_entrylk_rec (frame, ++lk_index);
        }

        return 0;

cleanup:
        dht_entrylk_cleanup (frame);

        return 0;
}

void
dht_blocking_entrylk_rec (call_frame_t *frame, int i)
{
        dht_local_t     *local = NULL;

        local = frame->local;

        STACK_WIND_COOKIE (frame, dht_blocking_entrylk_cbk,
                           (void *) (long) i,
                           local->lock[0].ns.directory_ns.locks[i]->xl,
                           local->lock[0].ns.directory_ns.locks[i]->xl->fops->entrylk,
                           local->lock[0].ns.directory_ns.locks[i]->domain,
                           &local->lock[0].ns.directory_ns.locks[i]->loc,
                           local->lock[0].ns.directory_ns.locks[i]->basename,
                           ENTRYLK_LOCK, ENTRYLK_WRLCK, NULL);

        return;
}

int
dht_blocking_entrylk (call_frame_t *frame, dht_lock_t **lk_array,
                      int lk_count, fop_entrylk_cbk_t entrylk_cbk)
{
        int           ret        = -1;
        call_frame_t *lock_frame = NULL;
        dht_local_t  *local      = NULL;

        GF_VALIDATE_OR_GOTO ("dht-locks", frame, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, lk_array, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, entrylk_cbk, out);

        lock_frame = dht_lock_frame (frame);
        if (lock_frame == NULL)
                goto out;

        ret = dht_local_entrylk_init (lock_frame, lk_array, lk_count,
                                      entrylk_cbk);
        if (ret < 0) {
                goto out;
        }

        dht_set_lkowner (lk_array, lk_count, &lock_frame->root->lk_owner);

        local = lock_frame->local;
        local->main_frame = frame;

        dht_blocking_entrylk_rec (lock_frame, 0);

        return 0;
out:
        if (lock_frame)
                dht_lock_stack_destroy (lock_frame, DHT_ENTRYLK);

        return -1;
}

static int
dht_local_inodelk_init (call_frame_t *frame, dht_lock_t **lk_array,
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

        local->lock[0].layout.my_layout.inodelk_cbk = inodelk_cbk;
        local->lock[0].layout.my_layout.locks = lk_array;
        local->lock[0].layout.my_layout.lk_count = lk_count;

        ret = dht_lock_order_requests (local->lock[0].layout.my_layout.locks,
                                       local->lock[0].layout.my_layout.lk_count);
        if (ret < 0)
                goto out;

        ret = 0;
out:
        return ret;
}

static void
dht_inodelk_done (call_frame_t *lock_frame)
{
        fop_inodelk_cbk_t  inodelk_cbk = NULL;
        call_frame_t      *main_frame  = NULL;
        dht_local_t       *local       = NULL;

        local = lock_frame->local;
        main_frame = local->main_frame;

        local->lock[0].layout.my_layout.locks = NULL;
        local->lock[0].layout.my_layout.lk_count = 0;

        inodelk_cbk = local->lock[0].layout.my_layout.inodelk_cbk;
        local->lock[0].layout.my_layout.inodelk_cbk = NULL;

        inodelk_cbk (main_frame, NULL, main_frame->this,
                     local->lock[0].layout.my_layout.op_ret,
                     local->lock[0].layout.my_layout.op_errno, NULL);

        dht_lock_stack_destroy (lock_frame, DHT_INODELK);
        return;
}

static int32_t
dht_unlock_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        dht_local_t *local                  = NULL;
        int          lk_index               = 0, call_cnt = 0;
        char         gfid[GF_UUID_BUF_SIZE] = {0};

        lk_index = (long) cookie;

        local = frame->local;
        if (op_ret < 0) {
                uuid_utoa_r (local->lock[0].layout.my_layout.locks[lk_index]->loc.gfid,
                             gfid);

                gf_msg (this->name, GF_LOG_WARNING, op_errno,
                        DHT_MSG_UNLOCKING_FAILED,
                        "unlocking failed on %s:%s",
                        local->lock[0].layout.my_layout.locks[lk_index]->xl->name,
                        gfid);
        } else {
                local->lock[0].layout.my_layout.locks[lk_index]->locked = 0;
        }

        call_cnt = dht_frame_return (frame);
        if (is_last_call (call_cnt)) {
                dht_inodelk_done (frame);
        }

        return 0;
}

static int32_t
dht_unlock_inodelk_done (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        dht_local_t *local                   = NULL;
        char          gfid[GF_UUID_BUF_SIZE] = {0};

        local = frame->local;
        gf_uuid_unparse (local->lock[0].layout.my_layout.locks[0]->loc.inode->gfid, gfid);

        if (op_ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, op_errno,
                        DHT_MSG_PARENT_LAYOUT_CHANGED,
                        "unlock failed on gfid: %s, stale lock might be left "
                        "in DHT_LAYOUT_HEAL_DOMAIN", gfid);
        }

        DHT_STACK_DESTROY (frame);
        return 0;
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

        ret = dht_local_inodelk_init (lock_frame, lk_array, lk_count,
                                      inodelk_cbk);
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

        for (i = 0; i < local->lock[0].layout.my_layout.lk_count; i++) {
                if (!local->lock[0].layout.my_layout.locks[i]->locked)
                        continue;

                lock_frame->root->lk_owner = local->lock[0].layout.my_layout.locks[i]->lk_owner;
                STACK_WIND_COOKIE (lock_frame, dht_unlock_inodelk_cbk,
                                   (void *)(long)i,
                                   local->lock[0].layout.my_layout.locks[i]->xl,
                                   local->lock[0].layout.my_layout.locks[i]->xl->fops->inodelk,
                                   local->lock[0].layout.my_layout.locks[i]->domain,
                                   &local->lock[0].layout.my_layout.locks[i]->loc, F_SETLK,
                                   &flock, NULL);
                if (!--call_cnt)
                        break;
        }

        return 0;

done:
        if (lock_frame)
                dht_lock_stack_destroy (lock_frame, DHT_INODELK);

        /* no locks acquired, invoke inodelk_cbk */
        if (ret == 0)
                inodelk_cbk (frame, NULL, frame->this, 0, 0, NULL);

        return ret;
}

int32_t
dht_unlock_inodelk_wrapper (call_frame_t *frame, dht_ilock_wrap_t *inodelk)
{
        dht_local_t  *local                   = NULL, *lock_local = NULL;
        call_frame_t *lock_frame              = NULL;
        char          pgfid[GF_UUID_BUF_SIZE] = {0};
        int           ret                     = 0;

        local = frame->local;

        if (!inodelk || !inodelk->locks)
                goto out;

        gf_uuid_unparse (local->loc.parent->gfid, pgfid);

        lock_frame = copy_frame (frame);
        if (lock_frame == NULL) {
                gf_msg (frame->this->name, GF_LOG_WARNING, ENOMEM,
                        DHT_MSG_PARENT_LAYOUT_CHANGED,
                        "mkdir (%s/%s) (path: %s): "
                        "copy frame failed", pgfid, local->loc.name,
                        local->loc.path);
                goto done;
        }

        lock_local = dht_local_init (lock_frame, NULL, NULL, 0);
        if (lock_local == NULL) {
                gf_msg (frame->this->name, GF_LOG_WARNING, ENOMEM,
                        DHT_MSG_PARENT_LAYOUT_CHANGED,
                        "mkdir (%s/%s) (path: %s): "
                        "local creation failed", pgfid, local->loc.name,
                        local->loc.path);
                goto done;
        }

        lock_frame->local = lock_local;

        lock_local->lock[0].layout.my_layout.locks = inodelk->locks;
        lock_local->lock[0].layout.my_layout.lk_count = inodelk->lk_count;
        inodelk->locks = NULL;
        inodelk->lk_count = 0;

        ret = dht_unlock_inodelk (lock_frame,
                                  lock_local->lock[0].layout.my_layout.locks,
                                  lock_local->lock[0].layout.my_layout.lk_count,
                                  dht_unlock_inodelk_done);

        if (ret)
                goto done;

        lock_frame = NULL;

done:
        if (lock_frame != NULL) {
                DHT_STACK_DESTROY (lock_frame);
        }
out:
        return 0;
}

static int
dht_inodelk_cleanup_cbk (call_frame_t *frame, void *cookie,
                         xlator_t *this, int32_t op_ret, int32_t op_errno,
                         dict_t *xdata)
{
        dht_inodelk_done (frame);
        return 0;
}

static void
dht_inodelk_cleanup (call_frame_t *lock_frame)
{
        dht_lock_t  **lk_array = NULL;
        int           lk_count = 0, lk_acquired = 0;
        dht_local_t  *local    = NULL;

        local = lock_frame->local;

        lk_array = local->lock[0].layout.my_layout.locks;
        lk_count = local->lock[0].layout.my_layout.lk_count;

        lk_acquired = dht_lock_count (lk_array, lk_count);
        if (lk_acquired != 0) {
                dht_unlock_inodelk (lock_frame, lk_array, lk_count,
                                     dht_inodelk_cleanup_cbk);
        } else {
                dht_inodelk_done (lock_frame);
        }

        return;
}

static int32_t
dht_nonblocking_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        dht_local_t *local                   = NULL;
        int          lk_index               = 0, call_cnt = 0;
        char          gfid[GF_UUID_BUF_SIZE] = {0};

        local = frame->local;
        lk_index = (long) cookie;

        if (op_ret == -1) {
                local->lock[0].layout.my_layout.op_ret = -1;
                local->lock[0].layout.my_layout.op_errno = op_errno;

                if (local && local->lock[0].layout.my_layout.locks[lk_index]) {
                        uuid_utoa_r (local->lock[0].layout.my_layout.locks[lk_index]->loc.inode->gfid,
                                     gfid);

                        gf_msg_debug (this->name, op_errno,
                                      "inodelk failed on gfid: %s "
                                      "subvolume: %s", gfid,
                                      local->lock[0].layout.my_layout.locks[lk_index]->xl->name);
                }

                goto out;
        }

        local->lock[0].layout.my_layout.locks[lk_index]->locked = _gf_true;

out:
        call_cnt = dht_frame_return (frame);
        if (is_last_call (call_cnt)) {
                if (local->lock[0].layout.my_layout.op_ret < 0) {
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

        ret = dht_local_inodelk_init (lock_frame, lk_array, lk_count,
                                      inodelk_cbk);
        if (ret < 0) {
                goto out;
        }

        dht_set_lkowner (lk_array, lk_count, &lock_frame->root->lk_owner);

        local = lock_frame->local;
        local->main_frame = frame;

        local->call_cnt = lk_count;

        for (i = 0; i < lk_count; i++) {
                flock.l_type = local->lock[0].layout.my_layout.locks[i]->type;

                STACK_WIND_COOKIE (lock_frame, dht_nonblocking_inodelk_cbk,
                                   (void *) (long) i,
                                   local->lock[0].layout.my_layout.locks[i]->xl,
                                   local->lock[0].layout.my_layout.locks[i]->xl->fops->inodelk,
                                   local->lock[0].layout.my_layout.locks[i]->domain,
                                   &local->lock[0].layout.my_layout.locks[i]->loc,
                                   F_SETLK,
                                   &flock, NULL);
        }

        return 0;

out:
        if (lock_frame)
                dht_lock_stack_destroy (lock_frame, DHT_INODELK);

        return -1;
}

static int32_t
dht_blocking_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        int          lk_index                   = 0;
        int          i                          = 0;
        dht_local_t *local                      = NULL;
        char         gfid[GF_UUID_BUF_SIZE]     = {0,};

        lk_index = (long) cookie;

        local = frame->local;
        if (op_ret == 0) {
                local->lock[0].layout.my_layout.locks[lk_index]->locked = _gf_true;
        } else {
                switch (op_errno) {
                case ESTALE:
                case ENOENT:
                        if (local->lock[0].layout.my_layout.locks[lk_index]->do_on_failure
                            != IGNORE_ENOENT_ESTALE) {
                                gf_uuid_unparse (local->lock[0].layout.my_layout.locks[lk_index]->loc.gfid, gfid);
                                local->lock[0].layout.my_layout.op_ret = -1;
                                local->lock[0].layout.my_layout.op_errno = op_errno;
                                gf_msg (this->name, GF_LOG_ERROR, op_errno,
                                        DHT_MSG_INODELK_FAILED,
                                        "inodelk failed on subvol %s. gfid:%s",
                                        local->lock[0].layout.my_layout.locks[lk_index]->xl->name,
                                        gfid);
                                goto cleanup;
                        }
                        break;
                default:
                        gf_uuid_unparse (local->lock[0].layout.my_layout.locks[lk_index]->loc.gfid, gfid);
                        local->lock[0].layout.my_layout.op_ret = -1;
                        local->lock[0].layout.my_layout.op_errno = op_errno;
                        gf_msg (this->name, GF_LOG_ERROR, op_errno,
                                DHT_MSG_INODELK_FAILED,
                                "inodelk failed on subvol %s, gfid:%s",
                                local->lock[0].layout.my_layout.locks[lk_index]->xl->name, gfid);
                        goto cleanup;
                }
        }

        if (lk_index == (local->lock[0].layout.my_layout.lk_count - 1)) {
                for (i = 0; (i < local->lock[0].layout.my_layout.lk_count) &&
                     (!local->lock[0].layout.my_layout.locks[i]->locked); i++)
                        ;

                if (i == local->lock[0].layout.my_layout.lk_count) {
                        local->lock[0].layout.my_layout.op_ret = -1;
                        local->lock[0].layout.my_layout.op_errno = op_errno;
                }

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

        flock.l_type = local->lock[0].layout.my_layout.locks[i]->type;

        STACK_WIND_COOKIE (frame, dht_blocking_inodelk_cbk,
                           (void *) (long) i,
                           local->lock[0].layout.my_layout.locks[i]->xl,
                           local->lock[0].layout.my_layout.locks[i]->xl->fops->inodelk,
                           local->lock[0].layout.my_layout.locks[i]->domain,
                           &local->lock[0].layout.my_layout.locks[i]->loc,
                           F_SETLKW,
                           &flock, NULL);

        return;
}

int
dht_blocking_inodelk (call_frame_t *frame, dht_lock_t **lk_array,
                      int lk_count, fop_inodelk_cbk_t inodelk_cbk)
{
        int           ret                       = -1;
        call_frame_t *lock_frame                = NULL;
        dht_local_t  *local                     = NULL;
        dht_local_t  *tmp_local                 = NULL;
        char          gfid[GF_UUID_BUF_SIZE]    = {0,};

        GF_VALIDATE_OR_GOTO ("dht-locks", frame, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, lk_array, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, inodelk_cbk, out);

        tmp_local = frame->local;

        lock_frame = dht_lock_frame (frame);
        if (lock_frame == NULL) {
                gf_uuid_unparse (tmp_local->loc.gfid, gfid);
                gf_msg ("dht", GF_LOG_ERROR, ENOMEM,
                        DHT_MSG_LOCK_FRAME_FAILED,
                        "memory allocation failed for lock_frame. gfid:%s"
                        " path:%s", gfid, tmp_local->loc.path);
                goto out;
        }

        ret = dht_local_inodelk_init (lock_frame, lk_array, lk_count,
                                      inodelk_cbk);
        if (ret < 0) {
                gf_uuid_unparse (tmp_local->loc.gfid, gfid);
                gf_msg ("dht", GF_LOG_ERROR, ENOMEM,
                        DHT_MSG_LOCAL_LOCK_INIT_FAILED,
                        "dht_local_lock_init failed, gfid: %s path:%s", gfid,
                        tmp_local->loc.path);
                goto out;
        }

        dht_set_lkowner (lk_array, lk_count, &lock_frame->root->lk_owner);

        local = lock_frame->local;
        local->main_frame = frame;

        dht_blocking_inodelk_rec (lock_frame, 0);

        return 0;
out:
        if (lock_frame)
                dht_lock_stack_destroy (lock_frame, DHT_INODELK);

        return -1;
}

void
dht_unlock_namespace (call_frame_t *frame, dht_dir_transaction_t *lock)
{
        GF_VALIDATE_OR_GOTO ("dht-locks", frame, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, lock, out);

        dht_unlock_entrylk_wrapper (frame, &lock->ns.directory_ns);
        dht_unlock_inodelk_wrapper (frame, &lock->ns.parent_layout);

out:
        return;
}

static int32_t
dht_protect_namespace_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        dht_local_t *local    = NULL;

        local = frame->local;
        if (op_ret != 0)
                dht_unlock_inodelk_wrapper (frame,
                                            &local->current->ns.parent_layout);

        local->current->ns.ns_cbk (frame, cookie, this, op_ret, op_errno,
                                   xdata);
        return 0;
}

int32_t
dht_blocking_entrylk_after_inodelk (call_frame_t *frame, void *cookie,
                                    xlator_t *this, int32_t op_ret,
                                    int32_t op_errno, dict_t *xdata)
{
        dht_local_t           *local                   = NULL;
        int                    ret                     = -1;
        loc_t                 *loc                     = NULL;
        dht_lock_t           **lk_array                = NULL;
        char                   pgfid[GF_UUID_BUF_SIZE] = {0};
        int                    count                   = 0;
        dht_elock_wrap_t      *entrylk                 = NULL;

        local = frame->local;
        entrylk = &local->current->ns.directory_ns;

        if (op_ret < 0) {
                local->op_ret = -1;
                local->op_errno = op_errno;
                goto err;
        }

        loc = &entrylk->locks[0]->loc;
        gf_uuid_unparse (loc->gfid, pgfid);

        local->op_ret = 0;
        lk_array = entrylk->locks;
        count = entrylk->lk_count;

        ret = dht_blocking_entrylk (frame, lk_array, count,
                                    dht_protect_namespace_cbk);

        if (ret < 0) {
                local->op_ret = -1;
                local->op_errno = EIO;
                gf_msg (this->name, GF_LOG_WARNING, local->op_errno,
                        DHT_MSG_ENTRYLK_ERROR,
                        "%s (%s/%s): "
                        "dht_blocking_entrylk failed after taking inodelk",
                        gf_fop_list[local->fop], pgfid,
                        entrylk->locks[0]->basename);
                goto err;
        }

        return 0;

err:
        if (lk_array != NULL) {
                dht_lock_array_free (lk_array, count);
                GF_FREE (lk_array);
                entrylk->locks = NULL;
                entrylk->lk_count = 0;
        }

        /* Unlock inodelk. No harm calling unlock twice */
        dht_unlock_inodelk_wrapper (frame, &local->current->ns.parent_layout);
        /* Call ns_cbk. It will take care of unwinding */
        local->current->ns.ns_cbk (frame, NULL, this, local->op_ret,
                                   local->op_errno, NULL);
        return 0;
}

/* Given the loc and the subvol, this routine takes the inodelk on
 * the parent inode and entrylk on (parent, loc->name). This routine
 * is specific as it supports only one subvol on which it takes inodelk
 * and then entrylk serially.
 */
int
dht_protect_namespace (call_frame_t *frame, loc_t *loc,
                       xlator_t *subvol,
                       struct dht_namespace *ns,
                       fop_entrylk_cbk_t ns_cbk)
{
        dht_ilock_wrap_t  *inodelk                 = NULL;
        dht_elock_wrap_t  *entrylk                 = NULL;
        dht_lock_t       **lk_array                = NULL;
        dht_local_t       *local                   = NULL;
        xlator_t          *this                    = NULL;
        loc_t              parent                  = {0,};
        int                ret                     = -1;
        char               pgfid[GF_UUID_BUF_SIZE] = {0};
        int32_t            op_errno                = 0;
        int                count                   = 1;

        GF_VALIDATE_OR_GOTO ("dht-locks", frame, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, loc, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, loc->parent, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, subvol, out);

        local = frame->local;
        this = frame->this;

        inodelk = &ns->parent_layout;
        entrylk = &ns->directory_ns;

        /* Initialize entrylk_cbk and parent loc */
        ns->ns_cbk = ns_cbk;

        ret = dht_build_parent_loc (this, &parent, loc, &op_errno);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, op_errno,
                        DHT_MSG_LOC_FAILED, "gfid:%s (name:%s) (path: %s): "
                        "parent loc build failed", loc->gfid, loc->name,
                         loc->path);
                goto out;
        }
        gf_uuid_unparse (parent.gfid, pgfid);

        /* Alloc inodelk */
        inodelk->locks = GF_CALLOC (count, sizeof (*lk_array),
                                    gf_common_mt_pointer);
        if (inodelk->locks == NULL) {
                local->op_errno = ENOMEM;
                gf_msg (this->name, GF_LOG_WARNING, local->op_errno,
                        DHT_MSG_NO_MEMORY,
                        "%s (%s/%s) (path: %s): "
                        "calloc failure",
                        gf_fop_list[local->fop], pgfid, loc->name, loc->path);
                goto out;
        }

        inodelk->locks[0] = dht_lock_new (this, subvol, &parent, F_RDLCK,
                                          DHT_LAYOUT_HEAL_DOMAIN, NULL,
                                          FAIL_ON_ANY_ERROR);
        if (inodelk->locks[0] == NULL) {
                local->op_errno = ENOMEM;
                gf_msg (this->name, GF_LOG_WARNING, local->op_errno,
                        DHT_MSG_NO_MEMORY,
                        "%s (%s/%s) (path: %s): "
                        "inodelk: lock allocation failed",
                        gf_fop_list[local->fop], pgfid, loc->name, loc->path);
                goto err;
        }
        inodelk->lk_count = count;

        /* Allock entrylk */
        entrylk->locks = GF_CALLOC (count, sizeof (*lk_array),
                                    gf_common_mt_pointer);
        if (entrylk->locks == NULL) {
                local->op_errno = ENOMEM;
                gf_msg (this->name, GF_LOG_WARNING, local->op_errno,
                        DHT_MSG_NO_MEMORY,
                        "%s (%s/%s) (path: %s): "
                        "entrylk: calloc failure",
                        gf_fop_list[local->fop], pgfid, loc->name, loc->path);

                goto err;
        }

        entrylk->locks[0] = dht_lock_new (this, subvol, &parent, F_WRLCK,
                                          DHT_ENTRY_SYNC_DOMAIN, loc->name,
                                          FAIL_ON_ANY_ERROR);
        if (entrylk->locks[0] == NULL) {
                local->op_errno = ENOMEM;
                gf_msg (this->name, GF_LOG_WARNING, local->op_errno,
                        DHT_MSG_NO_MEMORY,
                        "%s (%s/%s) (path: %s): "
                        "entrylk: lock allocation failed",
                        gf_fop_list[local->fop], pgfid, loc->name, loc->path);

                goto err;
        }
        entrylk->lk_count = count;

        /* Take read inodelk on parent. If it is successful, take write entrylk
         * on name in cbk.
         */
        lk_array = inodelk->locks;
        ret = dht_blocking_inodelk (frame, lk_array, count,
                                    dht_blocking_entrylk_after_inodelk);
        if (ret < 0) {
                local->op_errno = EIO;
                gf_msg (this->name, GF_LOG_WARNING, local->op_errno,
                        DHT_MSG_INODELK_ERROR,
                        "%s (%s/%s) (path: %s): "
                        "dht_blocking_inodelk failed",
                        gf_fop_list[local->fop], pgfid, loc->name, loc->path);
                goto err;
        }

        loc_wipe (&parent);

        return 0;
err:
        if (entrylk->locks != NULL) {
                dht_lock_array_free (entrylk->locks, count);
                GF_FREE (entrylk->locks);
                entrylk->locks = NULL;
                entrylk->lk_count = 0;
        }

        if (inodelk->locks != NULL) {
                dht_lock_array_free (inodelk->locks, count);
                GF_FREE (inodelk->locks);
                inodelk->locks = NULL;
                inodelk->lk_count = 0;
        }

        loc_wipe (&parent);
out:
        return -1;
}
