/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "xlator.h"
#include "defaults.h"
#include "compat-errno.h"
#include "ec.h"
#include "ec-messages.h"
#include "ec-heald.h"
#include "ec-mem-types.h"
#include "syncop.h"
#include "syncop-utils.h"
#include "protocol-common.h"

#define ASSERT_LOCAL(this, healer)				        \
        do {                                                            \
                if (!ec_shd_is_subvol_local (this, healer->subvol)) {	\
                        healer->local = _gf_false;			\
                        if (safe_break (healer)) {			\
                                break;					\
                        } else {					\
                                continue;				\
                        }						\
                } else {						\
                        healer->local = _gf_true;			\
                }                                                       \
        } while (0);


#define NTH_INDEX_HEALER(this, n) (&((((ec_t *)this->private))->shd.index_healers[n]))
#define NTH_FULL_HEALER(this, n) (&((((ec_t *)this->private))->shd.full_healers[n]))

gf_boolean_t
ec_shd_is_subvol_local (xlator_t *this, int subvol)
{
        ec_t *ec = NULL;
        gf_boolean_t is_local = _gf_false;
        loc_t loc = {0, };

        ec = this->private;
        loc.inode = this->itable->root;
        syncop_is_subvol_local (ec->xl_list[subvol], &loc, &is_local);
        return is_local;
}

char *
ec_subvol_name (xlator_t *this, int subvol)
{
        ec_t *ec = NULL;

        ec = this->private;
        if (subvol < 0 || subvol > ec->nodes)
                return NULL;

        return ec->xl_list[subvol]->name;
}

int
__ec_shd_healer_wait (struct subvol_healer *healer)
{
        ec_t *ec = NULL;
        struct timespec wait_till = {0, };
        int ret = 0;

        ec = healer->this->private;

disabled_loop:
        wait_till.tv_sec = time (NULL) + 60;

        while (!healer->rerun) {
                ret = pthread_cond_timedwait (&healer->cond,
                                              &healer->mutex,
                                              &wait_till);
                if (ret == ETIMEDOUT)
                        break;
        }

        ret = healer->rerun;
        healer->rerun = 0;

        if (!ec->shd.enabled || !ec->up)
                goto disabled_loop;

        return ret;
}


int
ec_shd_healer_wait (struct subvol_healer *healer)
{
        int ret = 0;

        pthread_mutex_lock (&healer->mutex);
        {
                ret = __ec_shd_healer_wait (healer);
        }
        pthread_mutex_unlock (&healer->mutex);

        return ret;
}


gf_boolean_t
safe_break (struct subvol_healer *healer)
{
        gf_boolean_t ret = _gf_false;

        pthread_mutex_lock (&healer->mutex);
        {
                if (healer->rerun)
                        goto unlock;

                healer->running = _gf_false;
                ret = _gf_true;
        }
unlock:
        pthread_mutex_unlock (&healer->mutex);

        return ret;
}

int
ec_shd_index_inode (xlator_t *this, xlator_t *subvol, inode_t **inode)
{
        loc_t   rootloc     = {0, };
        int     ret         = 0;
        dict_t  *xattr      = NULL;
        void    *index_gfid = NULL;

        *inode = NULL;
        rootloc.inode = inode_ref (this->itable->root);
        gf_uuid_copy (rootloc.gfid, rootloc.inode->gfid);

        ret = syncop_getxattr (subvol, &rootloc, &xattr,
                               GF_XATTROP_INDEX_GFID, NULL, NULL);
        if (ret < 0)
                goto out;
        if (!xattr) {
                ret = -EINVAL;
                goto out;
        }

        ret = dict_get_ptr (xattr, GF_XATTROP_INDEX_GFID, &index_gfid);
        if (ret)
                goto out;

        gf_msg_debug (this->name, 0, "index-dir gfid for %s: %s",
                subvol->name, uuid_utoa (index_gfid));

        ret = syncop_inode_find (this, subvol, index_gfid,
                                 inode, NULL, NULL);

out:
        loc_wipe (&rootloc);

        if (xattr)
                dict_unref (xattr);

        return ret;
}

int
ec_shd_index_purge (xlator_t *subvol, inode_t *inode, char *name)
{
        loc_t loc = {0, };
        int ret = 0;

        loc.parent = inode_ref (inode);
        loc.name = name;

        ret = syncop_unlink (subvol, &loc, NULL, NULL);

        loc_wipe (&loc);
        return ret;
}

int
ec_shd_selfheal (struct subvol_healer *healer, int child, loc_t *loc)
{
        return syncop_getxattr (healer->this, loc, NULL, EC_XATTR_HEAL, NULL,
                                NULL);
}


int
ec_shd_index_heal (xlator_t *subvol, gf_dirent_t *entry, loc_t *parent,
                   void *data)
{
        struct subvol_healer *healer = data;
        ec_t                 *ec     = NULL;
        loc_t                loc     = {0};
        int                  ret     = 0;

        ec = healer->this->private;
        if (!ec->shd.enabled)
                return -EBUSY;

        gf_msg_debug (healer->this->name, 0, "got entry: %s",
                entry->d_name);

        ret = gf_uuid_parse (entry->d_name, loc.gfid);
        if (ret)
                return 0;

        /* If this fails with ENOENT/ESTALE index is stale */
        ret = syncop_gfid_to_path (healer->this->itable, subvol, loc.gfid,
                                   (char **)&loc.path);
        if (ret < 0)
                goto out;

        ret = syncop_inode_find (healer->this, healer->this, loc.gfid,
                                 &loc.inode, NULL, NULL);
        if (ret < 0)
                goto out;

        ec_shd_selfheal (healer, healer->subvol, &loc);
out:
        if (ret == -ENOENT || ret == -ESTALE) {
                gf_msg (healer->this->name, GF_LOG_DEBUG, 0,
                        EC_MSG_HEAL_FAIL, "Purging index for gfid %s:",
                        uuid_utoa(loc.gfid));
                ec_shd_index_purge (subvol, parent->inode, entry->d_name);
        }
        loc_wipe (&loc);

        return 0;
}

int
ec_shd_index_sweep (struct subvol_healer *healer)
{
        loc_t         loc     = {0};
        ec_t          *ec     = NULL;
        int           ret     = 0;
        xlator_t      *subvol = NULL;
        dict_t        *xdata  = NULL;

        ec = healer->this->private;
        subvol = ec->xl_list[healer->subvol];

        ret = ec_shd_index_inode (healer->this, subvol, &loc.inode);
        if (ret < 0) {
                gf_msg (healer->this->name, GF_LOG_WARNING, errno,
                        EC_MSG_INDEX_DIR_GET_FAIL,
                        "unable to get index-dir on %s", subvol->name);
                goto out;
        }

        xdata = dict_new ();
        if (!xdata || dict_set_int32 (xdata, "get-gfid-type", 1)) {
                ret = -ENOMEM;
                goto out;
        }

        ret = syncop_mt_dir_scan (NULL, subvol, &loc, GF_CLIENT_PID_SELF_HEALD,
                                  healer, ec_shd_index_heal, xdata,
                                  ec->shd.max_threads, ec->shd.wait_qlength);
out:
        if (xdata)
                dict_unref (xdata);
        loc_wipe (&loc);

        return ret;
}

int
ec_shd_full_heal (xlator_t *subvol, gf_dirent_t *entry, loc_t *parent,
                   void *data)
{
        struct subvol_healer *healer = data;
        xlator_t             *this   = healer->this;
        ec_t                 *ec     = NULL;
        loc_t                loc    = {0};
        int                  ret    = 0;

        ec = this->private;
        if (!ec->shd.enabled)
                return -EBUSY;

        loc.parent = inode_ref (parent->inode);
        loc.name   = entry->d_name;
        gf_uuid_copy (loc.gfid, entry->d_stat.ia_gfid);

        /* If this fails with ENOENT/ESTALE index is stale */
        ret = syncop_gfid_to_path (this->itable, subvol, loc.gfid,
                                   (char **)&loc.path);
        if (ret < 0)
                goto out;

        ret = syncop_inode_find (this, this, loc.gfid,
                                 &loc.inode, NULL, NULL);
        if (ret < 0)
                goto out;

        ec_shd_selfheal (healer, healer->subvol, &loc);

        ret = 0;

out:
        loc_wipe (&loc);
        return ret;
}

int
ec_shd_full_sweep (struct subvol_healer *healer, inode_t *inode)
{
        ec_t           *ec  = NULL;
        loc_t          loc  = {0};

        ec = healer->this->private;
        loc.inode = inode;
        return syncop_ftw (ec->xl_list[healer->subvol], &loc,
                           GF_CLIENT_PID_SELF_HEALD, healer,
                           ec_shd_full_heal);
}


void *
ec_shd_index_healer (void *data)
{
        struct subvol_healer *healer = NULL;
        xlator_t *this = NULL;

        healer = data;
        THIS = this = healer->this;
        ec_t *ec = this->private;

        for (;;) {
                ec_shd_healer_wait (healer);

                ASSERT_LOCAL(this, healer);


                if (ec->xl_up_count > ec->fragments) {
                        gf_msg_debug (this->name, 0,
                                "starting index sweep on subvol %s",
                                ec_subvol_name (this, healer->subvol));
                        ec_shd_index_sweep (healer);
                }
                gf_msg_debug (this->name, 0,
                        "finished index sweep on subvol %s",
                        ec_subvol_name (this, healer->subvol));
        }

        return NULL;
}


void *
ec_shd_full_healer (void *data)
{
        struct subvol_healer *healer = NULL;
        xlator_t *this = NULL;
        loc_t rootloc = {0};

        int run = 0;

        healer = data;
        THIS = this = healer->this;
        ec_t *ec = this->private;

        rootloc.inode = this->itable->root;
        for (;;) {
                pthread_mutex_lock (&healer->mutex);
                {
                        run = __ec_shd_healer_wait (healer);
                        if (!run)
                                healer->running = _gf_false;
                }
                pthread_mutex_unlock (&healer->mutex);

                if (!run)
                        break;

                ASSERT_LOCAL(this, healer);


                if (ec->xl_up_count > ec->fragments) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                EC_MSG_FULL_SWEEP_START,
                                "starting full sweep on subvol %s",
                                ec_subvol_name (this, healer->subvol));

                        ec_shd_selfheal (healer, healer->subvol, &rootloc);
                        ec_shd_full_sweep (healer, this->itable->root);
                }

                gf_msg (this->name, GF_LOG_INFO, 0,
                        EC_MSG_FULL_SWEEP_STOP,
                        "finished full sweep on subvol %s",
                        ec_subvol_name (this, healer->subvol));
        }

        return NULL;
}


int
ec_shd_healer_init (xlator_t *this, struct subvol_healer *healer)
{
        int ret = 0;

        ret = pthread_mutex_init (&healer->mutex, NULL);
        if (ret)
                goto out;

        ret = pthread_cond_init (&healer->cond, NULL);
        if (ret)
                goto out;

        healer->this = this;
        healer->running = _gf_false;
        healer->rerun = _gf_false;
        healer->local = _gf_false;
out:
        return ret;
}


int
ec_shd_healer_spawn (xlator_t *this, struct subvol_healer *healer,
                      void *(threadfn)(void *))
{
        int ret = 0;

        pthread_mutex_lock (&healer->mutex);
        {
                if (healer->running) {
                        pthread_cond_signal (&healer->cond);
                } else {
                        ret = gf_thread_create (&healer->thread, NULL,
                                                threadfn, healer);
                        if (ret)
                                goto unlock;
                        healer->running = 1;
                }

                healer->rerun = 1;
        }
unlock:
        pthread_mutex_unlock (&healer->mutex);

        return ret;
}

int
ec_shd_full_healer_spawn (xlator_t *this, int subvol)
{
        return ec_shd_healer_spawn (this, NTH_FULL_HEALER (this, subvol),
                                    ec_shd_full_healer);
}


int
ec_shd_index_healer_spawn (xlator_t *this, int subvol)
{
        return ec_shd_healer_spawn (this, NTH_INDEX_HEALER (this, subvol),
                                    ec_shd_index_healer);
}

void
ec_selfheal_childup (ec_t *ec, int child)
{
        if (!ec->shd.iamshd)
                return;
        ec_shd_index_healer_spawn (ec->xl, child);
}

int
ec_selfheal_daemon_init (xlator_t *this)
{
        ec_t    *ec = NULL;
        ec_self_heald_t *shd = NULL;
        int ret = -1;
        int i = 0;

        ec = this->private;
        shd = &ec->shd;

        shd->index_healers = GF_CALLOC (sizeof(*shd->index_healers),
                                        ec->nodes,
                                        ec_mt_subvol_healer_t);
        if (!shd->index_healers)
                goto out;

        for (i = 0; i < ec->nodes; i++) {
                shd->index_healers[i].subvol = i;
                ret = ec_shd_healer_init (this, &shd->index_healers[i]);
                if (ret)
                        goto out;
        }

        shd->full_healers = GF_CALLOC (sizeof(*shd->full_healers),
                                       ec->nodes,
                                       ec_mt_subvol_healer_t);
        if (!shd->full_healers)
                goto out;

        for (i = 0; i < ec->nodes; i++) {
                shd->full_healers[i].subvol = i;
                ret = ec_shd_healer_init (this, &shd->full_healers[i]);
                if (ret)
                        goto out;
        }

        ret = 0;
out:
        return ret;
}


int
ec_heal_op (xlator_t *this, dict_t *output, gf_xl_afr_op_t op, int xl_id)
{
        char key[64] = {0};
        int op_ret = 0;
        ec_t *ec = NULL;
        int     i = 0;
        GF_UNUSED int     ret = 0;

        ec = this->private;

        op_ret = -1;
        for (i = 0; i < ec->nodes; i++) {
                snprintf (key, sizeof (key), "%d-%d-status", xl_id, i);

                if (((ec->xl_up >> i) & 1) == 0) {
                        ret = dict_set_str (output, key, "Brick is not connected");
                } else if (!ec->up) {
                        ret = dict_set_str (output, key,
                                            "Disperse subvolume is not up");
                } else if (!ec_shd_is_subvol_local (this, i)) {
                        ret = dict_set_str (output, key, "Brick is remote");
                } else {
                        ret = dict_set_str (output, key, "Started self-heal");
                        if (op == GF_SHD_OP_HEAL_FULL) {
                                ec_shd_full_healer_spawn (this, i);
                        } else if (op == GF_SHD_OP_HEAL_INDEX) {
                                ec_shd_index_healer_spawn (this, i);
                        }
                        op_ret = 0;
                }
        }
        return op_ret;
}

int
ec_xl_op (xlator_t *this, dict_t *input, dict_t *output)
{
        gf_xl_afr_op_t op     = GF_SHD_OP_INVALID;
        int            ret    = 0;
        int            xl_id  = 0;

        ret = dict_get_int32 (input, "xl-op", (int32_t *)&op);
        if (ret)
                goto out;

        ret = dict_get_int32 (input, this->name, &xl_id);
        if (ret)
                goto out;

        ret = dict_set_int32 (output, this->name, xl_id);
        if (ret)
                goto out;

        switch (op) {
        case GF_SHD_OP_HEAL_FULL:
                ret = ec_heal_op (this, output, op, xl_id);
                break;

        case GF_SHD_OP_HEAL_INDEX:
                ret = ec_heal_op (this, output, op, xl_id);
                break;

        default:
                ret = -1;
                break;
        }
out:
        dict_del (output, this->name);
        return ret;
}
