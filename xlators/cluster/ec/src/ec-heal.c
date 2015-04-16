/*
  Copyright (c) 2012-2014 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "xlator.h"
#include "defaults.h"
#include "compat-errno.h"

#include "ec-helpers.h"
#include "ec-common.h"
#include "ec-combine.h"
#include "ec-method.h"
#include "ec-fops.h"

#include "ec-mem-types.h"
#include "ec-data.h"
#include "byte-order.h"
#include "syncop.h"
#include "syncop-utils.h"
#include "cluster-syncop.h"

#define alloca0(size) ({void *__ptr; __ptr = alloca(size); memset(__ptr, 0, size); __ptr; })
#define EC_COUNT(array, max) ({int __i; int __res = 0; for (__i = 0; __i < max; __i++) if (array[__i]) __res++; __res; })
#define EC_INTERSECT(dst, src1, src2, max) ({int __i; for (__i = 0; __i < max; __i++) dst[__i] = src1[__i] && src2[__i]; })
#define EC_ADJUST_SOURCE(source, sources, max) ({int __i; if (sources[source] == 0) {source = -1; for (__i = 0; __i < max; __i++) if (sources[__i]) source = __i; } })
#define IA_EQUAL(f, s, field) (memcmp (&(f.ia_##field), &(s.ia_##field), sizeof (s.ia_##field)) == 0)
#define EC_REPLIES_ALLOC(replies, numsubvols) do {              \
        int __i = 0;                                            \
        replies = alloca0(numsubvols * sizeof (*replies));      \
        for (__i = 0; __i < numsubvols; __i++)                  \
                INIT_LIST_HEAD (&replies[__i].entries.list);    \
        } while (0)


struct ec_name_data {
        call_frame_t *frame;
        unsigned char *participants;
        unsigned char *failed_on;
        unsigned char *gfidless;
        unsigned char *enoent;
        unsigned char *same;
        char *name;
        inode_t *parent;
        default_args_cbk_t *replies;
};

static char *ec_ignore_xattrs[] = {
        GF_SELINUX_XATTR_KEY,
        QUOTA_SIZE_KEY,
        NULL
};

static gf_boolean_t
ec_ignorable_key_match (dict_t *dict, char *key, data_t *val, void *mdata)
{
        int i = 0;

        if (!key)
                goto out;

        if (strncmp (key, EC_XATTR_PREFIX, strlen (EC_XATTR_PREFIX)) == 0)
                        return _gf_true;

        for (i = 0; ec_ignore_xattrs[i]; i++) {
                if (!strcmp (key, ec_ignore_xattrs[i]))
                       return _gf_true;
        }

out:
        return _gf_false;
}

/* FOP: heal */

void ec_heal_exclude(ec_heal_t * heal, uintptr_t mask)
{
    LOCK(&heal->lock);

    heal->bad &= ~mask;

    UNLOCK(&heal->lock);
}

void ec_heal_lookup_resume(ec_fop_data_t * fop)
{
    ec_heal_t * heal = fop->data;
    ec_cbk_data_t * cbk;
    uintptr_t good = 0, bad = 0;

    if (heal->lookup != NULL)
    {
        ec_fop_data_release(heal->lookup);
    }
    ec_fop_data_acquire(fop);

    list_for_each_entry(cbk, &fop->cbk_list, list)
    {
        if ((cbk->op_ret < 0) && (cbk->op_errno == ENOTCONN))
        {
            continue;
        }

        if (cbk == fop->answer)
        {
            if (cbk->op_ret >= 0)
            {
                heal->iatt = cbk->iatt[0];
                heal->version[0] = cbk->version[0];
                heal->version[1] = cbk->version[1];
                heal->raw_size = cbk->size;
                heal->fop->pre_size = cbk->iatt[0].ia_size;
                heal->fop->post_size = cbk->iatt[0].ia_size;
                heal->fop->have_size = 1;

                if (ec_loc_update(heal->xl, &heal->loc, cbk->inode,
                                  &cbk->iatt[0]) != 0)
                {
                    fop->answer = NULL;
                    fop->error = EIO;

                    bad |= cbk->mask;

                    continue;
                }
            }

            good |= cbk->mask;
        }
        else
        {
            bad |= cbk->mask;
        }
    }

    /* Heal lookups are not executed concurrently with anything else. So, when
     * a lookup finishes, it's safe to access heal->good and heal->bad without
     * acquiring any lock.
     */
    heal->good = good;
    heal->bad = bad;

    heal->lookup = fop;

    ec_resume_parent(fop, fop->answer != NULL ? 0 : fop->error);
}

int32_t ec_heal_entry_lookup_cbk(call_frame_t * frame, void * cookie,
                                 xlator_t * this, int32_t op_ret,
                                 int32_t op_errno, inode_t * inode,
                                 struct iatt * buf, dict_t * xdata,
                                 struct iatt * postparent)
{
    ec_heal_lookup_resume(cookie);

    return 0;
}

int32_t ec_heal_inode_lookup_cbk(call_frame_t * frame, void * cookie,
                                 xlator_t * this, int32_t op_ret,
                                 int32_t op_errno, inode_t * inode,
                                 struct iatt * buf, dict_t * xdata,
                                 struct iatt * postparent)
{
    ec_heal_lookup_resume(cookie);

    return 0;
}

uintptr_t ec_heal_check(ec_fop_data_t * fop, uintptr_t * pgood)
{
    ec_cbk_data_t * cbk;
    uintptr_t mask[2] = { 0, 0 };

    list_for_each_entry(cbk, &fop->cbk_list, list)
    {
        mask[cbk->op_ret >= 0] |= cbk->mask;
    }

    if (pgood != NULL)
    {
        *pgood = mask[1];
    }

    return mask[0];
}

void ec_heal_update(ec_fop_data_t * fop, int32_t is_open)
{
    ec_heal_t * heal = fop->data;
    uintptr_t good, bad;

    bad = ec_heal_check(fop, &good);

    LOCK(&heal->lock);

    heal->bad &= ~bad;
    if (is_open)
    {
        heal->open |= good;
    }

    UNLOCK(&heal->lock);

    fop->error = 0;
}

void ec_heal_avoid(ec_fop_data_t * fop)
{
    ec_heal_t * heal = fop->data;
    uintptr_t bad;

    bad = ec_heal_check(fop, NULL);

    LOCK(&heal->lock);

    heal->good &= ~bad;

    UNLOCK(&heal->lock);
}

int32_t ec_heal_mkdir_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                          int32_t op_ret, int32_t op_errno, inode_t * inode,
                          struct iatt * buf, struct iatt * preparent,
                          struct iatt * postparent, dict_t * xdata)
{
    ec_heal_update(cookie, 0);

    return 0;
}

int32_t ec_heal_mknod_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                          int32_t op_ret, int32_t op_errno, inode_t * inode,
                          struct iatt * buf, struct iatt * preparent,
                          struct iatt * postparent, dict_t * xdata)
{
    ec_heal_update(cookie, 0);

    return 0;
}

int32_t ec_heal_symlink_cbk(call_frame_t * frame, void * cookie,
                            xlator_t * this, int32_t op_ret, int32_t op_errno,
                            inode_t * inode, struct iatt * buf,
                            struct iatt * preparent, struct iatt * postparent,
                            dict_t * xdata)
{
    ec_heal_update(cookie, 0);

    return 0;
}

int32_t ec_heal_setattr_cbk(call_frame_t * frame, void * cookie,
                            xlator_t * this, int32_t op_ret, int32_t op_errno,
                            struct iatt * preop_stbuf,
                            struct iatt * postop_stbuf,
                            dict_t * xdata)
{
    ec_heal_update(cookie, 0);

    return 0;
}

int32_t ec_heal_setxattr_cbk(call_frame_t * frame, void * cookie,
                             xlator_t * this, int32_t op_ret, int32_t op_errno,
                             dict_t * xdata)
{
    ec_heal_update(cookie, 0);

    return 0;
}

int32_t ec_heal_removexattr_cbk(call_frame_t * frame, void * cookie,
                                xlator_t * this, int32_t op_ret,
                                int32_t op_errno, dict_t * xdata)
{
    ec_heal_update(cookie, 0);

    return 0;
}

int32_t ec_heal_target_open_cbk(call_frame_t * frame, void * cookie,
                                xlator_t * this, int32_t op_ret,
                                int32_t op_errno, fd_t * fd, dict_t * xdata)
{
    ec_heal_update(cookie, 1);

    return 0;
}

int32_t ec_heal_source_open_cbk(call_frame_t * frame, void * cookie,
                                xlator_t * this, int32_t op_ret,
                                int32_t op_errno, fd_t * fd, dict_t * xdata)
{
    ec_heal_avoid(cookie);

    return 0;
}

int32_t ec_heal_reopen_cbk(call_frame_t * frame, void * cookie,
                           xlator_t * this, int32_t op_ret, int32_t op_errno,
                           fd_t * fd, dict_t * xdata)
{
    ec_fop_data_t * fop = cookie;
    ec_fd_t * ctx;
    uintptr_t good;

    ec_heal_check(fop, &good);

    if (good != 0)
    {
        LOCK(&fd->lock);

        ctx = __ec_fd_get(fd, fop->xl);
        if (ctx != NULL) {
            ctx->bad &= ~good;
            ctx->open |= good;
        }

        UNLOCK(&fd->lock);
    }

    return 0;
}

int32_t ec_heal_create (ec_heal_t *heal, uintptr_t mask)
{
    dict_t * xdata;
    int error = 0;

    xdata = dict_new();
    if (xdata == NULL)
        return ENOMEM;

    if (dict_set_static_bin(xdata, "gfid-req", heal->iatt.ia_gfid,
                            sizeof(uuid_t))) {
            error = ENOMEM;
            goto out;
    }

    switch (heal->iatt.ia_type)
    {
        case IA_IFDIR:
            ec_mkdir(heal->fop->frame, heal->xl, mask, EC_MINIMUM_ONE,
                     ec_heal_mkdir_cbk, heal, &heal->loc,
                     st_mode_from_ia(heal->iatt.ia_prot, heal->iatt.ia_type),
                     0, xdata);

            break;

        case IA_IFLNK:
            ec_symlink(heal->fop->frame, heal->xl, mask, EC_MINIMUM_ONE,
                       ec_heal_symlink_cbk, heal, heal->symlink, &heal->loc,
                       0, xdata);

            break;

        default:
            /* If mknod comes with the presence of GLUSTERFS_INTERNAL_FOP_KEY
             * then posix_mknod checks if there are already any gfid-links and
             * does link() instead of mknod. There still can be a race where
             * two posix_mknods with same gfid see that gfid-link file is not
             * present and proceeds with mknods and result in two different
             * files with same gfid. which is yet to be fixed in posix.*/
            if (dict_set_int32 (xdata, GLUSTERFS_INTERNAL_FOP_KEY, 1)) {
                    error = ENOMEM;
                    goto out;
            }

            ec_mknod(heal->fop->frame, heal->xl, mask, EC_MINIMUM_ONE,
                     ec_heal_mknod_cbk, heal, &heal->loc,
                     st_mode_from_ia(heal->iatt.ia_prot, heal->iatt.ia_type),
                     heal->iatt.ia_rdev, 0, xdata);

            break;
    }
    error = 0;

out:
    if (xdata)
        dict_unref(xdata);

    return error;
}

int32_t ec_heal_parent_cbk(call_frame_t *frame, void *cookie, xlator_t *xl,
                           int32_t op_ret, int32_t op_errno, uintptr_t mask,
                           uintptr_t good, uintptr_t bad, dict_t *xdata)
{
    ec_fop_data_t *fop = cookie;
    ec_heal_t *heal = fop->data;

    /* Even if parent self-heal has failed, we try to heal the current entry */
    ec_heal_create(heal, fop->mask);

    return 0;
}

void ec_heal_parent(ec_heal_t *heal, uintptr_t mask)
{
    loc_t parent;
    int32_t healing = 0;

    /* First we try to do a partial heal of the parent directory to avoid
     * ENOENT/ENOTDIR errors caused by missing parents */
    if (ec_loc_parent(heal->xl, &heal->loc, &parent) == 0) {
        if (!__is_root_gfid(parent.gfid)) {
            ec_heal(heal->fop->frame, heal->xl, mask, EC_MINIMUM_ONE,
                    ec_heal_parent_cbk, heal, &parent, 1, NULL);

            healing = 1;
        }
        loc_wipe(&parent);
    }

    if (!healing) {
        ec_heal_create(heal, mask);
    }
}

void ec_heal_recreate(ec_fop_data_t * fop)
{
    ec_cbk_data_t * cbk;
    ec_heal_t * heal = fop->data;
    uintptr_t mask = 0;

    if (heal->iatt.ia_type == IA_INVAL)
    {
        return;
    }

    list_for_each_entry(cbk, &fop->cbk_list, list)
    {
        if ((cbk->op_ret >= 0) || (cbk->op_errno == ENOENT) ||
            (cbk->op_errno == ENOTDIR))
        {
            mask |= cbk->mask;
        }
    }

    if (mask != 0)
    {
        ec_heal_parent(heal, mask);
    }
}

int32_t ec_heal_rmdir_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                          int32_t op_ret, int32_t op_errno,
                          struct iatt * preparent, struct iatt * postparent,
                          dict_t * xdata)
{
    ec_heal_update(cookie, 0);
    ec_heal_recreate(cookie);

    return 0;
}

int32_t ec_heal_unlink_cbk(call_frame_t * frame, void * cookie,
                           xlator_t * this, int32_t op_ret, int32_t op_errno,
                           struct iatt * preparent, struct iatt * postparent,
                           dict_t * xdata)
{
    ec_heal_update(cookie, 0);
    ec_heal_recreate(cookie);

    return 0;
}

int32_t
ec_heal_init (ec_fop_data_t * fop)
{
    ec_t * ec = fop->xl->private;
    struct iobuf_pool * pool;
    inode_t * inode;
    ec_inode_t * ctx;
    ec_heal_t * heal = NULL;
    int32_t error = 0;

    inode = fop->loc[0].inode;
    if (inode == NULL)
    {
        gf_log(fop->xl->name, GF_LOG_WARNING, "Unable to start inode healing "
                                              "because there is not enough "
                                              "information");

        return ENODATA;
    }

    heal = GF_MALLOC(sizeof(ec_heal_t), ec_mt_ec_heal_t);
    if (heal == NULL)
    {
        return ENOMEM;
    }

    memset(heal, 0, sizeof(ec_heal_t));

    if (ec_loc_from_loc(fop->xl, &heal->loc, &fop->loc[0]) != 0) {
        error = ENOMEM;
        goto out;
    }

    LOCK_INIT(&heal->lock);

    heal->xl = fop->xl;
    heal->fop = fop;
    pool = fop->xl->ctx->iobuf_pool;
    heal->size = iobpool_default_pagesize(pool) * ec->fragments;
    heal->partial = fop->int32;
    fop->heal = heal;

    LOCK(&inode->lock);

    ctx = __ec_inode_get(inode, fop->xl);
    if (ctx == NULL)
    {
        error = EIO;

        goto unlock;
    }

    if (list_empty(&ctx->heal)) {
        gf_log("ec", GF_LOG_INFO, "Healing '%s', gfid %s", heal->loc.path,
               uuid_utoa(heal->loc.gfid));
    } else {
        error = EEXIST;
    }

    list_add_tail(&heal->list, &ctx->heal);
    heal = NULL;

unlock:
    UNLOCK(&inode->lock);

    if (error == EEXIST) {
        LOCK(&fop->lock);

        fop->jobs++;
        fop->refs++;

        UNLOCK(&fop->lock);

        error = 0;
    }

out:
    GF_FREE(heal);

    return error;
}

void ec_heal_entrylk(ec_heal_t * heal, entrylk_cmd cmd)
{
    loc_t loc;

    if (ec_loc_parent(heal->xl, &heal->loc, &loc) != 0) {
        ec_fop_set_error(heal->fop, EIO);

        return;
    }

    ec_entrylk(heal->fop->frame, heal->xl, -1, EC_MINIMUM_ALL, NULL, NULL,
               heal->xl->name, &loc, NULL, cmd, ENTRYLK_WRLCK, NULL);

    loc_wipe(&loc);
}

void ec_heal_inodelk(ec_heal_t * heal, int32_t type, int32_t use_fd,
                     off_t offset, size_t size)
{
    struct gf_flock flock;

    flock.l_type = type;
    flock.l_whence = SEEK_SET;
    flock.l_start = offset;
    flock.l_len = size;
    flock.l_pid = 0;
    flock.l_owner.len = 0;

    if (use_fd)
    {
        ec_finodelk(heal->fop->frame, heal->xl, heal->fop->mask,
                    EC_MINIMUM_ALL, NULL, NULL, heal->xl->name, heal->fd,
                    F_SETLKW, &flock, NULL);
    }
    else
    {
        ec_inodelk(heal->fop->frame, heal->xl, heal->fop->mask, EC_MINIMUM_ALL,
                   NULL, NULL, heal->xl->name, &heal->loc, F_SETLKW, &flock,
                   NULL);
    }
}

void ec_heal_lookup(ec_heal_t *heal, uintptr_t mask)
{
    dict_t * xdata;
    int32_t error = ENOMEM;

    xdata = dict_new();
    if (xdata == NULL)
    {
        goto out;
    }
    if (dict_set_uint64(xdata, "list-xattr", 0) != 0)
    {
        goto out;
    }

    ec_lookup(heal->fop->frame, heal->xl, mask, EC_MINIMUM_MIN,
              ec_heal_inode_lookup_cbk, heal, &heal->loc, xdata);

    error = 0;

out:
    if (xdata != NULL)
    {
        dict_unref(xdata);
    }

    ec_fop_set_error(heal->fop, error);
}

void ec_heal_remove(ec_heal_t * heal, ec_cbk_data_t * cbk)
{
    if (cbk->iatt[0].ia_type == IA_IFDIR)
    {
        ec_rmdir(heal->fop->frame, heal->xl, cbk->mask, EC_MINIMUM_ONE,
                 ec_heal_rmdir_cbk, heal, &heal->loc, 1, NULL);
    }
    else
    {
        ec_unlink(heal->fop->frame, heal->xl, cbk->mask, EC_MINIMUM_ONE,
                  ec_heal_unlink_cbk, heal, &heal->loc, 0, NULL);
    }
}

void ec_heal_remove_others(ec_heal_t * heal)
{
    struct list_head * item;
    ec_cbk_data_t * cbk;

    item = heal->lookup->cbk_list.next;
    do
    {
        item = item->next;
        cbk = list_entry(item, ec_cbk_data_t, list);

        if (cbk->op_ret < 0)
        {
            if ((cbk->op_errno != ENOENT) && (cbk->op_errno != ENOTDIR) &&
                (cbk->op_errno != ESTALE))
            {
                gf_log(heal->xl->name, GF_LOG_WARNING, "Don't know how to "
                                                       "remove inode with "
                                                       "error %d",
                       cbk->op_errno);
            }

            ec_heal_exclude(heal, cbk->mask);

            continue;
        }

        ec_heal_remove(heal, cbk);
    } while (item->next != &heal->lookup->cbk_list);
}

void ec_heal_prepare_others(ec_heal_t * heal)
{
    struct list_head * item;
    ec_cbk_data_t * cbk;

    item = heal->lookup->cbk_list.next;
    while (item->next != &heal->lookup->cbk_list)
    {
        item = item->next;
        cbk = list_entry(item, ec_cbk_data_t, list);

        if (cbk->op_ret < 0)
        {
            if ((cbk->op_errno == ENOENT) || (cbk->op_errno == ESTALE))
            {
                ec_heal_create(heal, cbk->mask);
            }
            else
            {
                gf_log(heal->xl->name, GF_LOG_ERROR, "Don't know how to "
                                                     "heal error %d",
                       cbk->op_errno);

                ec_heal_exclude(heal, cbk->mask);
            }
        }
        else
        {
            if ((heal->iatt.ia_type != cbk->iatt[0].ia_type) ||
                (gf_uuid_compare(heal->iatt.ia_gfid, cbk->iatt[0].ia_gfid) != 0))
            {
                ec_heal_remove(heal, cbk);
            }
        }
    }
}

int32_t ec_heal_readlink_cbk(call_frame_t * frame, void * cookie,
                             xlator_t * this, int32_t op_ret, int32_t op_errno,
                             const char * path, struct iatt * buf,
                             dict_t * xdata)
{
    ec_fop_data_t * fop = cookie;
    ec_heal_t * heal = fop->data;

    if (op_ret >= 0)
    {
        heal->symlink = gf_strdup(path);
        if (heal->symlink != NULL)
        {
            ec_heal_prepare_others(heal);
        }
        else
        {
            ec_fop_set_error(fop, EIO);
        }
    }

    return 0;
}

ec_cbk_data_t * ec_heal_lookup_check(ec_heal_t * heal, uintptr_t * pgood,
                                     uintptr_t * pbad)
{
    ec_fop_data_t * fop = heal->lookup;
    ec_cbk_data_t * cbk = NULL, * ans = NULL;
    uintptr_t good = 0, bad = 0;

    list_for_each_entry(ans, &fop->cbk_list, list)
    {
        if ((ans->op_ret < 0) && (ans->op_errno == ENOTCONN))
        {
            continue;
        }

        if (ans == fop->answer)
        {
            good |= ans->mask;
            cbk = ans;
        }
        else
        {
            bad |= ans->mask;
        }
    }

    *pgood = good;
    *pbad = bad;

    return cbk;
}

void ec_heal_prepare(ec_heal_t * heal)
{
    ec_cbk_data_t * cbk;
    int32_t error = ENOMEM;

    heal->available = heal->good;

    cbk = heal->lookup->answer;
    if (cbk->op_ret < 0)
    {
        if ((cbk->op_errno == ENOENT) || (cbk->op_errno == ENOTDIR))
        {
            ec_heal_remove_others(heal);
        }
        else
        {
            gf_log(heal->xl->name, GF_LOG_ERROR, "Don't know how to heal "
                                                 "error %d",
                   cbk->op_errno);
        }
    }
    else
    {
        if (heal->iatt.ia_type == IA_IFREG)
        {
            heal->fd = fd_create(heal->loc.inode, heal->fop->frame->root->pid);
            if (heal->fd == NULL)
            {
                gf_log(heal->xl->name, GF_LOG_ERROR, "Unable to create a new "
                                                     "file descriptor");

                goto out;
            }
        }

        if (heal->iatt.ia_type == IA_IFLNK)
        {
            ec_readlink(heal->fop->frame, heal->xl, cbk->mask, EC_MINIMUM_ONE,
                        ec_heal_readlink_cbk, heal, &heal->loc,
                        heal->iatt.ia_size, NULL);
        }
        else
        {
            ec_heal_prepare_others(heal);
        }
    }

    error = 0;

out:
    ec_fop_set_error(heal->fop, error);
}

int32_t ec_heal_open_others(ec_heal_t * heal)
{
    struct list_head * item;
    ec_cbk_data_t * cbk;
    uintptr_t mask = 0, open = heal->open;

    item = heal->lookup->cbk_list.next;
    while (item->next != &heal->lookup->cbk_list)
    {
        item = item->next;
        cbk = list_entry(item, ec_cbk_data_t, list);

        if ((cbk->op_ret < 0) || (cbk->iatt[0].ia_type != IA_IFREG) ||
            (gf_uuid_compare(heal->iatt.ia_gfid, cbk->iatt[0].ia_gfid) != 0))
        {
            ec_heal_exclude(heal, cbk->mask);
        }
        else
        {
            mask |= cbk->mask & ~heal->open;
        }
    }

    if (mask != 0)
    {
        ec_open(heal->fop->frame, heal->xl, mask, EC_MINIMUM_ONE,
                ec_heal_target_open_cbk, heal, &heal->loc, O_RDWR | O_TRUNC,
                heal->fd, NULL);

        open |= mask;
    }

    return (open != 0);
}

uintptr_t ec_heal_needs_data_rebuild(ec_heal_t *heal)
{
    ec_fop_data_t *fop = heal->lookup;
    ec_cbk_data_t *cbk = NULL;
    uintptr_t bad = 0;

    if ((heal->fop->error != 0) || (heal->good == 0) ||
        (heal->iatt.ia_type != IA_IFREG)) {
        return 0;
    }

    list_for_each_entry(cbk, &fop->cbk_list, list) {
        if ((cbk->op_ret >= 0) &&
            ((cbk->size != heal->raw_size) ||
             (cbk->version != heal->version))) {
            bad |= cbk->mask;
        }
    }

    return bad;
}

void ec_heal_setxattr_others(ec_heal_t * heal)
{
    ec_cbk_data_t * cbk;
    dict_t * xdata;
    int32_t error = ENOMEM;

    if ((heal->good != 0) && (heal->bad != 0))
    {
        cbk = heal->lookup->answer;
        xdata = cbk->xdata;

        if (dict_foreach_match (xdata, ec_ignorable_key_match, NULL,
                                dict_remove_foreach_fn, NULL) == -1)
                goto out;

        if ((cbk->iatt[0].ia_type == IA_IFREG) ||
            (cbk->iatt[0].ia_type == IA_IFDIR))
        {
            if (ec_dict_set_array(xdata, EC_XATTR_VERSION,
                                  cbk->version, EC_VERSION_SIZE) != 0)
            {
                goto out;
            }
            if (cbk->iatt[0].ia_type == IA_IFREG)
            {
                uint64_t dirty;

                dirty = ec_heal_needs_data_rebuild(heal) != 0;
                if ((ec_dict_set_number(xdata, EC_XATTR_SIZE,
                                        cbk->iatt[0].ia_size) != 0) ||
                    (ec_dict_set_number(xdata, EC_XATTR_DIRTY, dirty) != 0)) {
                    goto out;
                }
            }
        }

        ec_setxattr(heal->fop->frame, heal->xl, heal->bad, EC_MINIMUM_ONE,
                    ec_heal_setxattr_cbk, heal, &heal->loc, xdata, 0, NULL);
    }

    error = 0;

out:
    ec_fop_set_error(heal->fop, error);
}

int32_t
ec_heal_xattr_clean (dict_t *dict, char *key, data_t *data,
                     void *arg)
{
        dict_t *base = arg;

        if (ec_ignorable_key_match (NULL, key, NULL, NULL)) {
                dict_del (dict, key);
                return 0;
        }

        if (dict_get (base, key) != NULL)
                dict_del (dict, key);

        return 0;
}

void ec_heal_removexattr_others(ec_heal_t * heal)
{
    struct list_head * item;
    ec_cbk_data_t * cbk;
    dict_t * xdata;

    if ((heal->good == 0) || (heal->bad == 0))
    {
        return;
    }

    xdata = heal->lookup->answer->xdata;
    item = heal->lookup->cbk_list.next;
    while (item->next != &heal->lookup->cbk_list)
    {
        item = item->next;
        cbk = list_entry(item, ec_cbk_data_t, list);

        if (cbk->op_ret >= 0)
        {
            if (dict_foreach(cbk->xdata, ec_heal_xattr_clean, xdata) == 0)
            {
                ec_removexattr(heal->fop->frame, heal->xl, cbk->mask,
                               EC_MINIMUM_ONE, ec_heal_removexattr_cbk, heal,
                               &heal->loc, "", cbk->xdata);
            }
        }
    }
}

void ec_heal_attr(ec_heal_t * heal)
{
    if ((heal->good != 0) && (heal->bad != 0))
    {
        ec_setattr(heal->fop->frame, heal->xl, heal->bad, EC_MINIMUM_ONE,
                   ec_heal_setattr_cbk, heal, &heal->loc, &heal->iatt,
                   GF_SET_ATTR_MODE | GF_SET_ATTR_UID | GF_SET_ATTR_GID |
                   GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME, NULL);
    }
}

void
ec_heal_open (ec_heal_t * heal)
{
    heal->bad = ec_heal_needs_data_rebuild(heal);
    if (heal->bad == 0) {
        return;
    }

    if (!heal->fd) {
            /* name-less loc heal */
            heal->fd = fd_create (heal->loc.inode,
                                  heal->fop->frame->root->pid);
    }

    if (!heal->fd) {
            ec_fop_set_error(heal->fop, ENOMEM);
            return;
    }

    if (ec_heal_open_others(heal))
    {
        ec_open(heal->fop->frame, heal->xl, heal->good, EC_MINIMUM_MIN,
                ec_heal_source_open_cbk, heal, &heal->loc, O_RDONLY, heal->fd,
                NULL);
    }
}

void ec_heal_reopen_fd(ec_heal_t * heal)
{
    inode_t * inode;
    fd_t * fd;
    ec_fd_t *ctx_fd;
    ec_inode_t *ctx_inode;
    uintptr_t mask;
    int32_t flags;

    inode = heal->loc.inode;

    LOCK(&inode->lock);

    ctx_inode = __ec_inode_get(inode, heal->xl);
    if (ctx_inode != NULL) {
        ctx_inode->bad &= ~(heal->good | heal->bad);
    }

    list_for_each_entry(fd, &inode->fd_list, inode_list)
    {
        ctx_fd = ec_fd_get(fd, heal->xl);
        if (ctx_fd != NULL) {
            mask = heal->bad & ~ctx_fd->open;
            if (mask != 0)
            {
                UNLOCK(&inode->lock);

                if (heal->iatt.ia_type == IA_IFDIR)
                {
                    ec_opendir(heal->fop->frame, heal->xl, mask,
                               EC_MINIMUM_ONE, ec_heal_reopen_cbk, NULL,
                               &heal->loc, fd, NULL);
                }
                else
                {
                    flags = ctx_fd->flags & ~(O_TRUNC | O_APPEND);

                    ec_open(heal->fop->frame, heal->xl, mask, EC_MINIMUM_ONE,
                            ec_heal_reopen_cbk, NULL, &heal->loc, flags, fd,
                            NULL);
                }

                LOCK(&inode->lock);
            }
        }
    }

    UNLOCK(&inode->lock);
}

int32_t ec_heal_writev_cbk(call_frame_t * frame, void * cookie,
                           xlator_t * this, int32_t op_ret, int32_t op_errno,
                           struct iatt * prebuf, struct iatt * postbuf,
                           dict_t * xdata)
{
    ec_trace("WRITE_CBK", cookie, "ret=%d, errno=%d", op_ret, op_errno);

    ec_heal_update(cookie, 0);

    return 0;
}

int32_t ec_heal_readv_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                          int32_t op_ret, int32_t op_errno,
                          struct iovec * vector, int32_t count,
                          struct iatt * stbuf, struct iobref * iobref,
                          dict_t * xdata)
{
    ec_fop_data_t * fop = cookie;
    ec_heal_t * heal = fop->data;

    ec_trace("READ_CBK", fop, "ret=%d, errno=%d", op_ret, op_errno);

    ec_heal_avoid(fop);

    if (op_ret > 0)
    {
        ec_writev(heal->fop->frame, heal->xl, heal->bad, EC_MINIMUM_ONE,
                  ec_heal_writev_cbk, heal, heal->fd, vector, count,
                  heal->offset, 0, iobref, NULL);
    }
    else
    {
        heal->done = 1;
    }

    return 0;
}

void ec_heal_data(ec_heal_t * heal)
{
    ec_trace("DATA", heal->fop, "good=%lX, bad=%lX", heal->good, heal->bad);

    if ((heal->good != 0) && (heal->bad != 0) &&
        (heal->iatt.ia_type == IA_IFREG))
    {
        ec_readv(heal->fop->frame, heal->xl, heal->good, EC_MINIMUM_MIN,
                 ec_heal_readv_cbk, heal, heal->fd, heal->size, heal->offset,
                 0, NULL);
    }
}

void ec_heal_update_dirty(ec_heal_t *heal, uintptr_t mask)
{
    dict_t *dict;

    dict = dict_new();
    if (dict == NULL) {
        ec_fop_set_error(heal->fop, EIO);

        return;
    }

    if (ec_dict_set_number(dict, EC_XATTR_DIRTY, -1) != 0) {
        dict_unref(dict);
        ec_fop_set_error(heal->fop, EIO);

        return;
    }

    ec_fxattrop(heal->fop->frame, heal->xl, mask, EC_MINIMUM_ONE, NULL, NULL,
                heal->fd, GF_XATTROP_ADD_ARRAY64, dict, NULL);

    dict_unref(dict);
}

void ec_heal_dispatch(ec_heal_t *heal)
{
    ec_fop_data_t *fop;
    ec_cbk_data_t *cbk;
    inode_t *inode;
    ec_inode_t *ctx;
    ec_heal_t *next = NULL;
    struct list_head list;
    int32_t error;

    inode = heal->loc.inode;

    INIT_LIST_HEAD(&list);

    LOCK(&inode->lock);

    /* A heal object not belonging to any list means that it has not been fully
     * executed. It got its information from a previous heal that was executing
     * when this heal started. */
    if (!list_empty(&heal->list)) {
        list_del_init(&heal->list);
        ctx = __ec_inode_get(inode, heal->xl);
        if (ctx != NULL) {
            ctx->bad &= ~heal->good;

            if (heal->partial) {
                /* Collect all partial heal requests. All of them will receive
                 * the same answer. 'next' will contain a pointer to the first
                 * full request (if any) after this partial heal request.*/
                while (!list_empty(&ctx->heal)) {
                    next = list_entry(ctx->heal.next, ec_heal_t, list);
                    if (!next->partial) {
                        break;
                    }
                    list_move_tail(&next->list, &list);
                }
                if (list_empty(&ctx->heal)) {
                    next = NULL;
                }
            } else {
                /* This is a full heal request, so take all received heal
                 * requests to answer them now. */
                list_splice_init(&ctx->heal, &list);
            }
        }
    }

    UNLOCK(&inode->lock);

    fop = heal->fop;
    error = fop->error;

    cbk = ec_cbk_data_allocate(fop->frame, heal->xl, fop, fop->id, 0,
                               error == 0 ? 0 : -1, error);
    if (cbk != NULL) {
        cbk->uintptr[0] = heal->available;
        cbk->uintptr[1] = heal->good;
        cbk->uintptr[2] = heal->fixed;

        ec_combine(cbk, NULL);

        fop->answer = cbk;
    } else if (error == 0) {
        error = ENOMEM;
    }

    if (heal->lookup != NULL)
    {
        ec_fop_data_release(heal->lookup);
    }
    if (heal->fd != NULL)
    {
        fd_unref(heal->fd);
    }
    GF_FREE(heal->symlink);
    loc_wipe(&heal->loc);

    LOCK_DESTROY(&heal->lock);

    GF_FREE(heal);

    ec_fop_set_error(fop, error);

    /* Resume all pending heal requests, setting the same data obtained by
     * this heal execution. */
    while (!list_empty(&list)) {
        heal = list_entry(list.next, ec_heal_t, list);
        list_del_init(&heal->list);

        heal->available = cbk->uintptr[0];
        heal->good = cbk->uintptr[1];
        heal->fixed = cbk->uintptr[2];

        /* Setting 'done' to 1 avoids executing all heal logic and directly
         * reports the result to the caller. */
        heal->done = 1;

        ec_resume(heal->fop, error);
    }

    /* If there is a pending full request, resume it. */
    if (next != NULL) {
        ec_resume(next->fop, 0);
    }
}

void ec_wind_heal(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_cbk_data_t * cbk;
    ec_heal_t *heal = fop->heal;

    ec_trace("WIND", fop, "idx=%d", idx);

    cbk = ec_cbk_data_allocate(fop->frame, fop->xl, fop, EC_FOP_HEAL, idx,
                               fop->error == 0 ? 0 : -1, fop->error);
    if (cbk != NULL)
    {
        cbk->uintptr[0] = heal->available;
        cbk->uintptr[1] = heal->good;
        cbk->uintptr[2] = heal->bad;

        ec_combine(cbk, NULL);
    }

    ec_complete(fop);
}

int32_t
ec_manager_heal (ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;
    ec_heal_t *heal = fop->heal;

    switch (state)
    {
        case EC_STATE_INIT:
            ec_owner_set(fop->frame, fop->frame->root);

            fop->error = ec_heal_init(fop);
            if (fop->error != 0)
            {
                return EC_STATE_REPORT;
            }

            heal = fop->heal;
            /* root loc doesn't have pargfid/parent */
            if (loc_is_root (&heal->loc) ||
                !gf_uuid_is_null(heal->loc.pargfid) || heal->loc.parent) {
                    heal->nameheal = _gf_true;
                    return EC_STATE_DISPATCH;
            } else {
                    /* No need to perform 'name' heal.*/
                    return EC_STATE_HEAL_PRE_INODELK_LOCK;
            }

        case EC_STATE_DISPATCH:
            if (heal->done) {
                return EC_STATE_HEAL_DISPATCH;
            }

            ec_heal_entrylk(heal, ENTRYLK_LOCK);

            return EC_STATE_HEAL_ENTRY_LOOKUP;

        case EC_STATE_HEAL_ENTRY_LOOKUP:
            ec_lookup(fop->frame, heal->xl, fop->mask, EC_MINIMUM_MIN,
                      ec_heal_entry_lookup_cbk, heal, &heal->loc, NULL);

            return EC_STATE_HEAL_ENTRY_PREPARE;

        case EC_STATE_HEAL_ENTRY_PREPARE:
            if (!heal->partial || (heal->iatt.ia_type == IA_IFDIR)) {
                ec_heal_prepare(heal);
            }

            if (heal->partial) {
                return EC_STATE_HEAL_UNLOCK_ENTRY;
            }

            return EC_STATE_HEAL_PRE_INODELK_LOCK;

        case EC_STATE_HEAL_PRE_INODELK_LOCK:
            if (heal->done)
                    return EC_STATE_HEAL_DISPATCH;

            /* Only heal data/metadata if enough information is supplied. */
            if (gf_uuid_is_null(heal->loc.gfid))
            {
                ec_heal_entrylk(heal, ENTRYLK_UNLOCK);

                return EC_STATE_HEAL_DISPATCH;
            }

            ec_heal_inodelk(heal, F_WRLCK, 0, 0, 0);

            return EC_STATE_HEAL_PRE_INODE_LOOKUP;

        case EC_STATE_HEAL_PRE_INODE_LOOKUP:
            ec_heal_lookup(heal, heal->fop->mask);

            return EC_STATE_HEAL_XATTRIBUTES_REMOVE;

        case EC_STATE_HEAL_XATTRIBUTES_REMOVE:
            ec_heal_removexattr_others(heal);

            return EC_STATE_HEAL_XATTRIBUTES_SET;

        case EC_STATE_HEAL_XATTRIBUTES_SET:
            ec_heal_setxattr_others(heal);

            return EC_STATE_HEAL_ATTRIBUTES;

        case EC_STATE_HEAL_ATTRIBUTES:
            ec_heal_attr(heal);

            return EC_STATE_HEAL_OPEN;

        case EC_STATE_HEAL_OPEN:
            ec_heal_open(heal);

            return EC_STATE_HEAL_REOPEN_FD;

        case EC_STATE_HEAL_REOPEN_FD:
            ec_heal_reopen_fd(heal);

            return EC_STATE_HEAL_UNLOCK;

        case -EC_STATE_HEAL_XATTRIBUTES_REMOVE:
        case -EC_STATE_HEAL_XATTRIBUTES_SET:
        case -EC_STATE_HEAL_ATTRIBUTES:
        case -EC_STATE_HEAL_OPEN:
        case -EC_STATE_HEAL_REOPEN_FD:
        case -EC_STATE_HEAL_UNLOCK:
        case EC_STATE_HEAL_UNLOCK:
            ec_heal_inodelk(heal, F_UNLCK, 0, 0, 0);

        /* Fall through */

        case -EC_STATE_HEAL_ENTRY_PREPARE:
        case -EC_STATE_HEAL_PRE_INODELK_LOCK:
        case -EC_STATE_HEAL_PRE_INODE_LOOKUP:
        case -EC_STATE_HEAL_UNLOCK_ENTRY:
        case EC_STATE_HEAL_UNLOCK_ENTRY:
            if (heal->nameheal)
                    ec_heal_entrylk(heal, ENTRYLK_UNLOCK);

            heal->bad = ec_heal_needs_data_rebuild(heal);
            if (heal->bad != 0)
            {
                return EC_STATE_HEAL_DATA_LOCK;
            }

            return EC_STATE_HEAL_DISPATCH;

        case EC_STATE_HEAL_DATA_LOCK:
            if (heal->done)
            {
                return EC_STATE_HEAL_POST_INODELK_LOCK;
            }

            ec_heal_inodelk(heal, F_WRLCK, 1, heal->offset, heal->size);

            return EC_STATE_HEAL_DATA_COPY;

        case EC_STATE_HEAL_DATA_COPY:
            ec_heal_data(heal);

            return EC_STATE_HEAL_DATA_UNLOCK;

        case -EC_STATE_HEAL_DATA_COPY:
        case -EC_STATE_HEAL_DATA_UNLOCK:
        case EC_STATE_HEAL_DATA_UNLOCK:
            ec_heal_inodelk(heal, F_UNLCK, 1, heal->offset, heal->size);

            heal->offset += heal->size;

            return EC_STATE_HEAL_DATA_LOCK;

        case EC_STATE_HEAL_POST_INODELK_LOCK:
            ec_heal_inodelk(heal, F_WRLCK, 1, 0, 0);

            return EC_STATE_HEAL_POST_INODE_LOOKUP;

        case EC_STATE_HEAL_POST_INODE_LOOKUP:
            heal->fixed = heal->bad;
            ec_heal_update_dirty(heal, heal->bad);
            ec_heal_lookup(heal, heal->good);

            return EC_STATE_HEAL_SETATTR;

        case EC_STATE_HEAL_SETATTR:
            ec_setattr(heal->fop->frame, heal->xl, heal->fixed, EC_MINIMUM_ONE,
                       ec_heal_setattr_cbk, heal, &heal->loc, &heal->iatt,
                       GF_SET_ATTR_MODE | GF_SET_ATTR_UID | GF_SET_ATTR_GID |
                       GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME, NULL);

            return EC_STATE_HEAL_POST_INODELK_UNLOCK;

        case -EC_STATE_HEAL_SETATTR:
        case -EC_STATE_HEAL_POST_INODELK_UNLOCK:
        case EC_STATE_HEAL_POST_INODELK_UNLOCK:
            ec_heal_inodelk(heal, F_UNLCK, 1, 0, 0);

            return EC_STATE_HEAL_DISPATCH;

        case -EC_STATE_HEAL_POST_INODELK_LOCK:
        case -EC_STATE_HEAL_POST_INODE_LOOKUP:
        case -EC_STATE_HEAL_ENTRY_LOOKUP:
        case -EC_STATE_HEAL_DATA_LOCK:
        case -EC_STATE_HEAL_DISPATCH:
        case EC_STATE_HEAL_DISPATCH:
            ec_heal_dispatch(heal);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            cbk = fop->answer;
            if (cbk != NULL)
            {
                if (!ec_dict_combine(cbk, EC_COMBINE_XDATA))
                {
                    if (cbk->op_ret >= 0)
                    {
                        cbk->op_ret = -1;
                        cbk->op_errno = EIO;
                    }
                }
                if (cbk->op_ret < 0)
                {
                    ec_fop_set_error(fop, cbk->op_errno);
                }
            }
            else
            {
                ec_fop_set_error(fop, EIO);
            }

            return EC_STATE_REPORT;

        case EC_STATE_REPORT:
            cbk = fop->answer;

            GF_ASSERT(cbk != NULL);

            if (fop->id == EC_FOP_HEAL)
            {
                if (fop->cbks.heal != NULL)
                {
                    fop->cbks.heal(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                   cbk->op_errno, cbk->uintptr[0],
                                   cbk->uintptr[1], cbk->uintptr[2],
                                   cbk->xdata);
                }
            }
            else
            {
                if (fop->cbks.fheal != NULL)
                {
                    fop->cbks.fheal(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                    cbk->op_errno, cbk->uintptr[0],
                                    cbk->uintptr[1], cbk->uintptr[2],
                                    cbk->xdata);
                }
            }

            return EC_STATE_END;

        case -EC_STATE_INIT:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->id == EC_FOP_HEAL)
            {
                if (fop->cbks.heal != NULL)
                {
                    fop->cbks.heal(fop->req_frame, fop, fop->xl, -1,
                                   fop->error, 0, 0, 0, NULL);
                }
            }
            else
            {
                if (fop->cbks.fheal != NULL)
                {
                    fop->cbks.fheal(fop->req_frame, fop, fop->xl, -1,
                                    fop->error, 0, 0, 0, NULL);
                }
            }

            return EC_STATE_END;

        default:
            gf_log(fop->xl->name, GF_LOG_ERROR, "Unhandled state %d for %s",
                   state, ec_fop_name(fop->id));

            return EC_STATE_END;
    }
}

void ec_heal(call_frame_t * frame, xlator_t * this, uintptr_t target,
             int32_t minimum, fop_heal_cbk_t func, void * data, loc_t * loc,
             int32_t partial, dict_t *xdata)
{
    ec_cbk_t callback = { .heal = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(HEAL) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, EC_FOP_HEAL,
                               EC_FLAG_UPDATE_LOC_INODE, target, minimum,
                               ec_wind_heal, ec_manager_heal, callback, data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->int32 = partial;

    if (loc != NULL)
    {
        if (loc_copy(&fop->loc[0], loc) != 0)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to copy a location.");

            goto out;
        }
    }
    if (xdata != NULL)
    {
        fop->xdata = dict_ref(xdata);
        if (fop->xdata == NULL)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
                                             "dictionary.");

            goto out;
        }
    }

    error = 0;

out:
    if (fop != NULL)
    {
        ec_manager(fop, error);
    }
    else
    {
        func(frame, NULL, this, -1, EIO, 0, 0, 0, NULL);
    }
}

/* FOP: fheal */

void ec_wind_fheal(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_cbk_data_t * cbk;
    ec_heal_t *heal = fop->heal;

    ec_trace("WIND", fop, "idx=%d", idx);

    cbk = ec_cbk_data_allocate(fop->frame, fop->xl, fop, EC_FOP_FHEAL, idx,
                               fop->error == 0 ? 0 : -1, fop->error);
    if (cbk != NULL)
    {
        cbk->uintptr[0] = heal->available;
        cbk->uintptr[1] = heal->good;
        cbk->uintptr[2] = heal->bad;

        ec_combine(cbk, NULL);
    }

    ec_complete(fop);
}

void ec_fheal(call_frame_t * frame, xlator_t * this, uintptr_t target,
              int32_t minimum, fop_fheal_cbk_t func, void * data, fd_t * fd,
              int32_t partial, dict_t *xdata)
{
    ec_fd_t * ctx = ec_fd_get(fd, this);

    if (ctx != NULL)
    {
        gf_log("ec", GF_LOG_DEBUG, "FHEAL ctx: flags=%X, open=%lX, bad=%lX",
               ctx->flags, ctx->open, ctx->bad);
        ec_heal(frame, this, target, minimum, func, data, &ctx->loc, partial,
                xdata);
    }
}

/* Common heal code */
void
ec_mask_to_char_array (uintptr_t mask, unsigned char *array, int numsubvols)
{
        int     i = 0;

        for (i = 0; i < numsubvols; i++)
                array[i] = ((mask >> i) & 1);
}

int
ec_heal_find_direction (ec_t *ec, ec_txn_t type, default_args_cbk_t *replies,
                        uint64_t *versions, uint64_t *dirty,
                        unsigned char *sources, unsigned char *healed_sinks)
{
        void        *ptr        = NULL;
        uint64_t    *value      = NULL;
        uint64_t    max_version = 0;
        int         source      = -1;
        int32_t     len         = 0;
        int         ret         = 0;
        int         i           = 0;
        struct iatt source_ia   = {0};
        struct iatt child_ia    = {0};

        for (i = 0; i < ec->nodes; i++) {
                if (!replies[i].valid)
                        continue;

                if (replies[i].op_ret == -1)
                        continue;

                if (source == -1)
                        source = i;

                ret = dict_get_ptr_and_len (replies[i].xdata, EC_XATTR_VERSION,
                                            &ptr, &len);
                if (ret == 0) {
                        value = ptr;
                        versions[i] = ntoh64(value[type]);
                        if (max_version < versions[i]) {
                                max_version = versions[i];
                                source = i;
                        }
                }

                ret = dict_get_ptr_and_len (replies[i].xdata, EC_XATTR_DIRTY,
                                            &ptr, &len);
                if (ret == 0) {
                        value = ptr;
                        dirty[i] = ntoh64(value[type]);
                }
        }

        if (source < 0)
                goto out;

        for (i = 0; i < ec->nodes; i++) {
                if (!replies[i].valid)
                        continue;

                if (replies[i].op_ret == -1)
                        continue;

                if (versions[i] == versions[source])
                        sources[i] = 1;
                else
                        healed_sinks[i] = 1;
        }

        if (type == EC_METADATA_TXN) {
                source_ia = replies[source].stat;
                for (i = 0; i < ec->nodes; i++) {
                        if (!sources[i])
                                continue;
                        child_ia = replies[i].stat;
                        if (!IA_EQUAL(source_ia, child_ia, gfid) ||
                            !IA_EQUAL(source_ia, child_ia, type) ||
                            !IA_EQUAL(source_ia, child_ia, prot) ||
                            !IA_EQUAL(source_ia, child_ia, uid) ||
                            !IA_EQUAL(source_ia, child_ia, gid)) {
                                sources[i] = 0;
                                healed_sinks[i] = 1;
                        }
                }
        }
out:
        return source;
}

int
ec_adjust_versions (call_frame_t *frame, ec_t *ec, ec_txn_t type, inode_t *inode, int source,
                    unsigned char *sources,
                    unsigned char *healed_sinks, uint64_t *versions,
                    uint64_t *dirty)
{
        int                        i                 = 0;
        int                        ret               = 0;
        dict_t                     *xattr            = NULL;
        int                        op_ret            = 0;
        loc_t                      loc               = {0};
        gf_boolean_t               erase_dirty       = _gf_false;
        uint64_t                   versions_xattr[2] = {0};
        uint64_t                   dirty_xattr[2]    = {0};
        uint64_t                   allzero[2]        = {0};

        loc.inode = inode_ref (inode);
        gf_uuid_copy (loc.gfid, inode->gfid);
        xattr = dict_new ();
        if (!xattr)
                goto out;

        /* dirty xattr represents if the file/dir needs heal. Unless all the
         * copies are healed, don't erase it */
        if (EC_COUNT (sources, ec->nodes) +
            EC_COUNT (healed_sinks, ec->nodes) == ec->nodes)
                erase_dirty = _gf_true;

        for (i = 0; i < ec->nodes; i++) {
                if (!sources[i] && !healed_sinks[i])
                        continue;
                versions_xattr[type] = hton64(versions[source] - versions[i]);
                ret = dict_set_static_bin (xattr, EC_XATTR_VERSION,
                                           versions_xattr,
                                           sizeof (versions_xattr));
                if (ret < 0) {
                        op_ret = -ENOTCONN;
                        continue;
                }

                if (erase_dirty) {
                        dirty_xattr[type] = hton64(-dirty[i]);
                        ret = dict_set_static_bin (xattr, EC_XATTR_DIRTY,
                                                   dirty_xattr,
                                                   sizeof (dirty_xattr));
                        if (ret < 0) {
                                op_ret = -ENOTCONN;
                                continue;
                        }
                }

                if ((memcmp (versions_xattr, allzero, sizeof (allzero)) == 0) &&
                    (memcmp (dirty_xattr, allzero, sizeof (allzero)) == 0))
                        continue;

                ret = syncop_xattrop (ec->xl_list[i], &loc,
                                      GF_XATTROP_ADD_ARRAY64, xattr, NULL,
                                      NULL);
                if (ret < 0) {
                        op_ret = -ret;
                        continue;
                }
        }

out:
        if (xattr)
                dict_unref (xattr);
        loc_wipe (&loc);
        return op_ret;
}

int
__ec_heal_prepare (call_frame_t *frame, ec_t *ec, inode_t *inode,
                   unsigned char *locked_on, default_args_cbk_t *replies,
                   uint64_t *versions, uint64_t *dirty, unsigned char *sources,
                   unsigned char *healed_sinks, ec_txn_t type)
{
        loc_t         loc     = {0};
        unsigned char *output = NULL;
        dict_t        *xdata  = NULL;
        int           ret     = 0;
        int           source  = 0;

        xdata = dict_new ();
        if (!xdata) {
                ret = -ENOMEM;
                goto out;
        }

        if (dict_set_uint64(xdata, "list-xattr", 0)) {
                ret = -ENOMEM;
                goto out;
        }

        loc.inode = inode_ref (inode);
        gf_uuid_copy (loc.gfid, inode->gfid);
        output = alloca0 (ec->nodes);
        ret = cluster_lookup (ec->xl_list, locked_on, ec->nodes, replies,
                              output, frame, ec->xl, &loc, xdata);
        if (ret <= ec->fragments) {
                ret = -ENOTCONN;
                goto out;
        }

        source = ec_heal_find_direction (ec, type, replies, versions,
                                         dirty, sources, healed_sinks);
        if (source < 0) {
                ret = -EIO;
                goto out;
        }
        ret = source;
out:
        if (xdata)
                dict_unref (xdata);

        loc_wipe (&loc);
        return ret;
}

/* Metadata heal */
int
__ec_removexattr_sinks (call_frame_t *frame, ec_t *ec, inode_t *inode,
                        int source, unsigned char *sources,
                        unsigned char *healed_sinks,
                        default_args_cbk_t *replies)
{
        int   i   = 0;
        int   ret = 0;
        loc_t loc = {0};

        loc.inode = inode_ref (inode);
        gf_uuid_copy (loc.gfid, inode->gfid);

        for (i = 0; i < ec->nodes; i++) {
                if (i == source)
                        continue;
                if (!sources[i] && !healed_sinks[i])
                        continue;
                ret = dict_foreach (replies[i].xattr, ec_heal_xattr_clean,
                                    replies[source].xattr);
                if (ret < 0) {
                        sources[i] = 0;
                        healed_sinks[i] = 0;
                }

                if (replies[i].xattr->count == 0) {
                        continue;
                } else if (sources[i]) {
                        /* This can happen if setxattr/removexattr succeeds on
                         * the bricks but fails to update the version. This
                         * will make sure that the xattrs are made equal after
                         * heal*/
                        sources[i] = 0;
                        healed_sinks[i] = 1;
                }

                ret = syncop_removexattr (ec->xl_list[i], &loc, "",
                                          replies[i].xattr, NULL);
                if (ret < 0)
                        healed_sinks[i] = 0;
        }

        loc_wipe (&loc);
        if (EC_COUNT (healed_sinks, ec->nodes) == 0)
                return -ENOTCONN;
        return 0;
}

int
__ec_heal_metadata (call_frame_t *frame, ec_t *ec, inode_t *inode,
                    unsigned char *locked_on)
{
        loc_t              loc           = {0};
        int                ret           = 0;
        int                source        = 0;
        default_args_cbk_t *replies      = NULL;
        uint64_t           *versions     = NULL;
        uint64_t           *dirty        = NULL;
        unsigned char      *sources      = NULL;
        unsigned char      *healed_sinks = NULL;
        unsigned char      *output       = NULL;
        dict_t             *source_dict  = NULL;
        struct iatt        source_buf    = {0};

        EC_REPLIES_ALLOC (replies, ec->nodes);

        loc.inode = inode_ref (inode);
        gf_uuid_copy (loc.gfid, inode->gfid);
        output = alloca0 (ec->nodes);
        versions = alloca0 (ec->nodes * sizeof (*versions));
        dirty = alloca0 (ec->nodes * sizeof (*dirty));
        sources = alloca0 (ec->nodes);
        healed_sinks = alloca0 (ec->nodes);
        source = __ec_heal_prepare (frame, ec, inode, locked_on, replies,
                                    versions, dirty, sources, healed_sinks,
                                    EC_METADATA_TXN);
        if (source < 0) {
                ret = -EIO;
                goto out;
        }

        source_buf = replies[source].stat;
        ret = cluster_setattr (ec->xl_list, healed_sinks, ec->nodes, replies,
                               output, frame, ec->xl, &loc,
                               &source_buf, GF_SET_ATTR_MODE |
                               GF_SET_ATTR_UID | GF_SET_ATTR_GID, NULL);
        /*In case the operation fails on some of the subvols*/
        memcpy (healed_sinks, output, ec->nodes);
        if (EC_COUNT (healed_sinks, ec->nodes) == 0) {
                ret = -ENOTCONN;
                goto out;
        }

        ret = cluster_getxattr (ec->xl_list, locked_on, ec->nodes, replies,
                                output, frame, ec->xl, &loc, NULL, NULL);
        EC_INTERSECT (sources, sources, output, ec->nodes);
        EC_INTERSECT (healed_sinks, healed_sinks, output, ec->nodes);
        EC_ADJUST_SOURCE (source, sources, ec->nodes);
        if ((EC_COUNT (healed_sinks, ec->nodes) == 0) || (source < 0)) {
                ret = -ENOTCONN;
                goto out;
        }

        ret = __ec_removexattr_sinks (frame, ec, inode, source, sources,
                                      healed_sinks, replies);
        if (ret < 0)
                goto out;

        source_dict = dict_ref (replies[source].xattr);
        if (dict_foreach_match (source_dict, ec_ignorable_key_match, NULL,
                                dict_remove_foreach_fn, NULL) == -1) {
                ret = -ENOMEM;
                goto out;
        }

        ret = cluster_setxattr (ec->xl_list, healed_sinks, ec->nodes,
                                replies, output, frame, ec->xl, &loc,
                                source_dict, 0, NULL);

        EC_INTERSECT (healed_sinks, healed_sinks, output, ec->nodes);
        if (EC_COUNT (healed_sinks, ec->nodes) == 0) {
                ret = -ENOTCONN;
                goto out;
        }

        ret = ec_adjust_versions (frame, ec, EC_METADATA_TXN, inode, source,
                                  sources, healed_sinks, versions, dirty);
out:
        if (source_dict)
                dict_unref (source_dict);

        loc_wipe (&loc);
        cluster_replies_wipe (replies, ec->nodes);
        return ret;
}

int
ec_heal_metadata (call_frame_t *req_frame, ec_t *ec, inode_t *inode)
{
        unsigned char      *locked_on  = NULL;
        unsigned char      *up_subvols = NULL;
        unsigned char      *output     = NULL;
        int                ret         = 0;
        default_args_cbk_t *replies    = NULL;
        call_frame_t       *frame      = NULL;

        EC_REPLIES_ALLOC (replies, ec->nodes);
        frame = copy_frame (req_frame);
        if (!frame) {
                ret = -ENOMEM;
                goto out;
        }

        /*Do heal as root*/
        frame->root->uid = 0;
        frame->root->gid = 0;
        locked_on = alloca0(ec->nodes);
        output = alloca0(ec->nodes);
        up_subvols = alloca0(ec->nodes);
        ec_mask_to_char_array (ec->xl_up, up_subvols, ec->nodes);
        ret = cluster_inodelk (ec->xl_list, up_subvols, ec->nodes, replies,
                               locked_on, frame, ec->xl, ec->xl->name, inode, 0,
                               0);
        {
                if (ret <= ec->fragments) {
                        gf_log (ec->xl->name, GF_LOG_DEBUG, "%s: Skipping heal "
                                "as only %d number of subvolumes could "
                                "be locked", uuid_utoa (inode->gfid), ret);
                        ret = -ENOTCONN;
                        goto unlock;
                }
                ret = __ec_heal_metadata (frame, ec, inode, locked_on);
        }
unlock:
        cluster_uninodelk (ec->xl_list, locked_on, ec->nodes, replies, output,
                           frame, ec->xl, ec->xl->name, inode, 0, 0);
out:
        cluster_replies_wipe (replies, ec->nodes);
        if (frame)
                STACK_DESTROY (frame->root);
        return ret;
}

/*entry heal*/
int
__ec_heal_entry_prepare (call_frame_t *frame, ec_t *ec, inode_t *inode,
                         unsigned char *locked_on, uint64_t *versions,
                         uint64_t *dirty, unsigned char *sources,
                         unsigned char *healed_sinks)
{
        int                source   = 0;
        default_args_cbk_t *replies = NULL;
        loc_t              loc      = {0};
        int                ret      = 0;

        EC_REPLIES_ALLOC (replies, ec->nodes);

        loc.inode = inode_ref (inode);
        gf_uuid_copy (loc.gfid, inode->gfid);
        source = __ec_heal_prepare (frame, ec, inode, locked_on, replies,
                                    versions, dirty, sources, healed_sinks,
                                    EC_DATA_TXN);
        if (source < 0) {
                ret = -EIO;
                goto out;
        }
        ret = source;
out:
        loc_wipe (&loc);
        cluster_replies_wipe (replies, ec->nodes);
        return ret;
}

/*Name heal*/
int
ec_delete_stale_name (dict_t *gfid_db, char *key, data_t *d, void *data)
{
        struct ec_name_data *name_data   = data;
        struct iatt         *ia          = NULL;
        ec_t                *ec          = NULL;
        loc_t               loc          = {0};
        unsigned char       *same        = data_to_bin (d);
        default_args_cbk_t  *replies     = NULL;
        unsigned char       *output      = NULL;
        int                 ret          = 0;
        int                 estale_count = 0;
        int                 i            = 0;
        call_frame_t        *frame       = name_data->frame;

        ec = name_data->frame->this->private;
        EC_REPLIES_ALLOC (replies, ec->nodes);
        if (EC_COUNT (same, ec->nodes) >= ec->fragments) {
                ret = 0;
                goto out;
        }

        loc.inode = inode_new (name_data->parent->table);
        if (!loc.inode) {
                ret = -ENOMEM;
                goto out;
        }
        gf_uuid_parse (key, loc.gfid);
        output = alloca0(ec->nodes);
        ret = cluster_lookup (ec->xl_list, name_data->participants, ec->nodes,
                              replies, output, name_data->frame, ec->xl, &loc,
                              NULL);

        for (i = 0; i < ec->nodes; i++) {
                if (!replies[i].valid)
                        continue;
                if (replies[i].op_ret == -1) {
                        if (replies[i].op_errno == ESTALE ||
                            replies[i].op_errno == ENOENT)
                                estale_count++;
                        else
                                name_data->participants[i] = 0;
                }
        }

        if (estale_count <= ec->redundancy) {
                /* We have at least ec->fragments number of fragments, so the
                 * file is recoverable, so don't delete it*/

                /* Please note that the lookup call above could fail with
                 * ENOTCONN on all subvoumes and still this branch will be
                 * true, but in those cases conservatively we decide to not
                 * delete the file until we are sure*/
                ret = 0;
                goto out;
        }

        /*Noway to recover, delete the name*/
        loc_wipe (&loc);
        loc.parent = inode_ref (name_data->parent);
        gf_uuid_copy (loc.pargfid, loc.parent->gfid);
        loc.name = name_data->name;
        for (i = 0; i < ec->nodes; i++) {
                if (same[i] && replies[i].valid && (replies[i].op_ret == 0)) {
                        ia = &replies[i].stat;
                        break;
                }
        }

        if (!ia) {
                ret = -ENOTCONN;
                goto out;
        }

        if (IA_ISDIR (ia->ia_type)) {
                ret = cluster_rmdir (ec->xl_list, same, ec->nodes, replies,
                                     output, frame, ec->xl, &loc, 1, NULL);
        } else {
                ret = cluster_unlink (ec->xl_list, same, ec->nodes, replies,
                                      output, frame, ec->xl, &loc, 0, NULL);
        }

        for (i = 0; i < ec->nodes; i++) {
                if (output[i]) {
                        same[i] = 0;
                        name_data->enoent[i] = 1;
                } else {
                        /*op failed*/
                        if (same[i])
                                name_data->participants[i] = 0;
                }
        }
        ret = 0;
        /*This will help in making decisions about creating names*/
        dict_del (gfid_db, key);
out:
        cluster_replies_wipe (replies, ec->nodes);
        loc_wipe (&loc);
        return ret;
}

int
ec_delete_stale_names (call_frame_t *frame, ec_t *ec, inode_t *parent,
                       char *name, default_args_cbk_t *replies, dict_t *gfid_db,
                       unsigned char *enoent, unsigned char *gfidless,
                       unsigned char *participants)
{
        struct ec_name_data name_data = {0};

        name_data.enoent = enoent;
        name_data.gfidless = gfidless;
        name_data.participants = participants;
        name_data.name = name;
        name_data.parent = parent;
        name_data.frame = frame;
        name_data.replies = replies;
        return dict_foreach (gfid_db, ec_delete_stale_name, &name_data);
}

int
_assign_same (dict_t *dict, char *key, data_t *value, void *data)
{
        struct ec_name_data *name_data = data;

        name_data->same = data_to_bin (value);
        return 0;
}

int
ec_create_name (call_frame_t *frame, ec_t *ec, inode_t *parent, char *name,
                default_args_cbk_t *lookup_replies, dict_t *gfid_db,
                unsigned char *enoent, unsigned char *participants)
{
        int                 ret       = 0;
        int                 i         = 0;
        struct ec_name_data name_data = {0};
        struct iatt         *ia       = NULL;
        unsigned char       *output   = 0;
        unsigned char       *output1  = 0;
        default_args_cbk_t  *replies  = NULL;
        loc_t               loc       = {0};
        loc_t               srcloc    = {0};
        unsigned char       *link     = NULL;
        unsigned char       *create   = NULL;
        dict_t              *xdata    = NULL;
        char                *linkname = NULL;

        /* There should be just one gfid key */
        EC_REPLIES_ALLOC (replies, ec->nodes);
        if (gfid_db->count != 1) {
                ret = -EINVAL;
                goto out;
        }

        ret = dict_foreach (gfid_db, _assign_same, &name_data);
        if (ret < 0)
                goto out;
        /*There should at least be one valid success reply with gfid*/
        for (i = 0; i < ec->nodes; i++)
                if (name_data.same[i])
                        break;

        if (i == ec->nodes) {
                ret = -EINVAL;
                goto out;
        }

        ia = &lookup_replies[i].stat;
        xdata = dict_new ();
        loc.parent = inode_ref (parent);
        gf_uuid_copy (loc.pargfid, parent->gfid);
        loc.inode = inode_new (parent->table);
        if (loc.inode)
                srcloc.inode = inode_ref (loc.inode);
        gf_uuid_copy (srcloc.gfid, ia->ia_gfid);
        if (!loc.inode || !xdata || dict_set_static_bin (xdata, "gfid-req",
                                                         ia->ia_gfid,
                                                        sizeof (ia->ia_gfid))) {
                ret = -ENOMEM;
                goto out;
        }
        loc.name = name;
        link = alloca0 (ec->nodes);
        create = alloca0 (ec->nodes);
        output = alloca0 (ec->nodes);
        output1 = alloca0 (ec->nodes);
        switch (ia->ia_type) {
        case IA_IFDIR:
                ret = cluster_mkdir (ec->xl_list, enoent, ec->nodes,
                                   replies, output, frame, ec->xl, &loc,
                                   st_mode_from_ia (ia->ia_prot,
                                                ia->ia_type), 0, xdata);
                break;

        case IA_IFLNK:
                /*Check for hard links and create/link*/
                ret = cluster_lookup (ec->xl_list, enoent, ec->nodes,
                                      replies, output, frame, ec->xl,
                                      &srcloc, NULL);
                for (i = 0; i < ec->nodes; i++) {
                        if (output[i]) {
                                link[i] = 1;
                        } else {
                                if (replies[i].op_errno == ENOENT ||
                                    replies[i].op_errno == ESTALE) {
                                        create[i] = 1;
                                }
                        }
                }

                if (EC_COUNT (link, ec->nodes)) {
                        cluster_link (ec->xl_list, link, ec->nodes,
                                      replies, output1, frame, ec->xl,
                                      &srcloc, &loc, NULL);
                }

                if (EC_COUNT (create, ec->nodes)) {
                        cluster_readlink (ec->xl_list, name_data.same,
                                          ec->nodes, replies, output,
                                          frame, ec->xl, &srcloc, 4096,
                                          NULL);
                        if (EC_COUNT (output, ec->nodes) == 0) {
                                ret = -ENOTCONN;
                                goto out;
                        }

                        for (i = 0; i < ec->nodes; i++) {
                                if (output[i])
                                        break;
                        }
                        linkname = alloca0 (strlen(replies[i].buf) + 1);
                        strcpy (linkname, replies[i].buf);
                        cluster_symlink (ec->xl_list, create, ec->nodes,
                                         replies, output, frame, ec->xl,
                                         linkname, &loc, 0, xdata);
                }
                for (i = 0; i < ec->nodes; i++)
                        if (output1[i])
                                output[i] = 1;
                break;
        default:
                ret = dict_set_int32 (xdata, GLUSTERFS_INTERNAL_FOP_KEY,
                                      1);
                if (ret)
                        goto out;
                ret = cluster_mknod (ec->xl_list, enoent, ec->nodes,
                                     replies, output, frame, ec->xl,
                                     &loc, st_mode_from_ia (ia->ia_prot,
                                                           ia->ia_type),
                                     ia->ia_rdev, 0, xdata);
                break;
        }

        for (i = 0; i < ec->nodes; i++) {
                if (enoent[i] && !output[i])
                        participants[i] = 0;
        }

        ret = 0;
out:
        loc_wipe (&loc);
        loc_wipe (&srcloc);
        EC_REPLIES_ALLOC (replies, ec->nodes);
        if (xdata)
                dict_unref (xdata);
        return ret;
}

int
__ec_heal_name (call_frame_t *frame, ec_t *ec, inode_t *parent, char *name,
                unsigned char *participants)
{
        unsigned char      *output   = NULL;
        unsigned char      *enoent   = NULL;
        default_args_cbk_t *replies  = NULL;
        dict_t             *xdata    = NULL;
        dict_t             *gfid_db  = NULL;
        int                ret       = 0;
        loc_t              loc       = {0};
        int                i         = 0;
        struct iatt        *ia       = NULL;
        char               gfid[64]  = {0};
        unsigned char      *same     = NULL;
        unsigned char      *gfidless = NULL;

        loc.parent = inode_ref (parent);
        loc.inode = inode_new (parent->table);
        gf_uuid_copy (loc.pargfid, parent->gfid);
        loc.name = name;
        xdata = dict_new ();
        gfid_db = dict_new ();
        if (!xdata || !gfid_db || !loc.inode) {
                ret = -ENOMEM;
                goto out;
        }

        ret = dict_set_int32 (xdata, GF_GFIDLESS_LOOKUP, 1);
        if (ret) {
                ret = -ENOMEM;
                goto out;
        }

        output = alloca0 (ec->nodes);
        gfidless = alloca0 (ec->nodes);
        enoent = alloca0 (ec->nodes);
        EC_REPLIES_ALLOC (replies, ec->nodes);
        ret = cluster_lookup (ec->xl_list, participants, ec->nodes, replies,
                              output, frame, ec->xl, &loc, NULL);
        for (i = 0; i < ec->nodes; i++) {
                if (!replies[i].valid)
                        continue;

                if (replies[i].op_ret == -1) {
                        /*If ESTALE comes here, that means parent dir is not
                         * present, nothing to do there, so reset participants
                         * for that brick*/
                        if (replies[i].op_errno == ENOENT)
                                enoent[i] = 1;
                        else
                                participants[i] = 0;
                        continue;
                }
                ia = &replies[i].stat;
                if (gf_uuid_is_null (ia->ia_gfid)) {
                        if (IA_ISDIR (ia->ia_type) || ia->ia_size == 0)
                                gfidless[i] = 1;
                        else
                                participants[i] = 0;
                } else {
                        uuid_utoa_r (ia->ia_gfid, gfid);
                        ret = dict_get_bin (gfid_db, gfid, (void **)&same);
                        if (ret < 0) {
                                same = alloca0(ec->nodes);
                        }
                        same[i] = 1;
                        if (ret < 0) {
                                ret = dict_set_static_bin (gfid_db, gfid, same,
                                                           ec->nodes);
                        }
                        if (ret < 0)
                                goto out;
                }
        }

        ret = ec_delete_stale_names (frame, ec, parent, name, replies, gfid_db,
                                     enoent, gfidless, participants);

        if (gfid_db->count == 0) {
                /* All entries seem to be stale entries and deleted,
                 * nothing more to do.*/
                goto out;
        }

        if (gfid_db->count > 1) {
                gf_log (ec->xl->name, GF_LOG_INFO, "%s/%s: Not able to heal",
                        uuid_utoa (parent->gfid), name);
                memset (participants, 0, ec->nodes);
                goto out;
        }

        EC_INTERSECT (enoent, enoent, participants, ec->nodes);
        if (EC_COUNT (enoent, ec->nodes) == 0) {
                ret = 0;
                goto out;
        }

        ret = ec_create_name (frame, ec, parent, name, replies, gfid_db, enoent,
                              participants);
out:
        cluster_replies_wipe (replies, ec->nodes);
        loc_wipe (&loc);
        if (xdata)
                dict_unref (xdata);
        if (gfid_db)
                dict_unref (gfid_db);
        return ret;
}

int
ec_heal_name (call_frame_t *frame, ec_t *ec, inode_t *parent, char *name,
              unsigned char *participants)
{
        int                ret        = 0;
        default_args_cbk_t *replies   = NULL;
        unsigned char      *output    = NULL;
        unsigned char      *locked_on = NULL;
        loc_t              loc        = {0};

        loc.parent = inode_ref (parent);
        loc.name = name;
        loc.inode = inode_new (parent->table);
        if (!loc.inode) {
                ret = -ENOMEM;
                goto out;
        }

        EC_REPLIES_ALLOC (replies, ec->nodes);
        output = alloca0 (ec->nodes);
        locked_on = alloca0 (ec->nodes);
        ret = cluster_entrylk (ec->xl_list, participants, ec->nodes, replies,
                               locked_on, frame, ec->xl, ec->xl->name, parent,
                               NULL);
        {
                if (ret <= ec->fragments) {
                        gf_log (ec->xl->name, GF_LOG_DEBUG, "%s: Skipping heal "
                                "as only %d number of subvolumes could "
                                "be locked", uuid_utoa (parent->gfid), ret);
                        ret = -ENOTCONN;
                        goto unlock;
                }
                EC_INTERSECT (participants, participants, locked_on, ec->nodes);
                ret = __ec_heal_name (frame, ec, parent, name, participants);
        }
unlock:
        cluster_unentrylk (ec->xl_list, locked_on, ec->nodes, replies, output,
                           frame, ec->xl, ec->xl->name, parent, NULL);
out:
        cluster_replies_wipe (replies, ec->nodes);
        loc_wipe (&loc);
        return ret;
}

int
ec_name_heal_handler (xlator_t *subvol, gf_dirent_t *entry, loc_t *parent,
                      void *data)
{
        struct ec_name_data *name_data = data;
        xlator_t            *this      = THIS;
        ec_t                *ec        = this->private;
        unsigned char       *name_on   = alloca0 (ec->nodes);
        int                 i          = 0;
        int                 ret        = 0;

        memcpy (name_on, name_data->participants, ec->nodes);
        ret = ec_heal_name (name_data->frame, ec, parent->inode,
                            entry->d_name, name_on);

        if (ret < 0)
                memset (name_on, 0, ec->nodes);

        for (i = 0; i < ec->nodes; i++)
                if (name_data->participants[i] && !name_on[i])
                        name_data->failed_on[i] = 1;
        return 0;
}

int
ec_heal_names (call_frame_t *frame, ec_t *ec, inode_t *inode,
               unsigned char *participants)
{
        int i = 0;
        int j = 0;
        loc_t loc = {0};
        struct ec_name_data name_data = {0};

        loc.inode = inode_ref (inode);
        gf_uuid_copy (loc.gfid, inode->gfid);
        name_data.frame = frame;
        name_data.participants = participants;
        name_data.failed_on = alloca0(ec->nodes);;

        for (i = 0; i < ec->nodes; i++) {
                if (!participants[i])
                        continue;
                syncop_dir_scan (ec->xl_list[i], &loc,
                                GF_CLIENT_PID_AFR_SELF_HEALD, &name_data,
                                ec_name_heal_handler);
                for (j = 0; j < ec->nodes; j++)
                        if (name_data.failed_on[j])
                                participants[j] = 0;

                if (EC_COUNT (participants, ec->nodes) <= ec->fragments)
                        return -ENOTCONN;
        }
        return 0;
}

int
__ec_heal_entry (call_frame_t *frame, ec_t *ec, inode_t *inode,
                 unsigned char *heal_on)
{
        unsigned char      *locked_on    = NULL;
        unsigned char      *output       = NULL;
        uint64_t           *versions     = NULL;
        uint64_t           *dirty        = NULL;
        unsigned char      *sources      = NULL;
        unsigned char      *healed_sinks = NULL;
        unsigned char      *participants = NULL;
        default_args_cbk_t *replies      = NULL;
        int                ret           = 0;
        int                source        = 0;
        int                i             = 0;

        locked_on = alloca0(ec->nodes);
        output = alloca0(ec->nodes);
        versions = alloca0 (ec->nodes * sizeof (*versions));
        dirty = alloca0 (ec->nodes * sizeof (*dirty));
        sources = alloca0 (ec->nodes);
        healed_sinks = alloca0 (ec->nodes);

        EC_REPLIES_ALLOC (replies, ec->nodes);
        ret = cluster_entrylk (ec->xl_list, heal_on, ec->nodes, replies,
                               locked_on, frame, ec->xl, ec->xl->name, inode,
                               NULL);
        {
                if (ret <= ec->fragments) {
                        gf_log (ec->xl->name, GF_LOG_DEBUG, "%s: Skipping heal "
                                "as only %d number of subvolumes could "
                                "be locked", uuid_utoa (inode->gfid), ret);
                        ret = -ENOTCONN;
                        goto unlock;
                }
                ret = __ec_heal_entry_prepare (frame, ec, inode, locked_on,
                                               versions, dirty, sources,
                                               healed_sinks);
                source = ret;
        }
unlock:
        cluster_unentrylk (ec->xl_list, locked_on, ec->nodes, replies, output,
                           frame, ec->xl, ec->xl->name, inode, NULL);
        if (ret < 0)
                goto out;

        participants = alloca0 (ec->nodes);
        for (i = 0; i < ec->nodes; i++) {
                if (sources[i] || healed_sinks[i])
                        participants[i] = 1;
        }
        ret = ec_heal_names (frame, ec, inode, participants);

        if (EC_COUNT (participants, ec->nodes) <= ec->fragments)
                goto out;

        for (i = 0; i < ec->nodes; i++) {
                if (!participants[i]) {
                        sources[i] = 0;
                        healed_sinks[i] = 0;
                }
        }

        ec_adjust_versions (frame, ec, EC_DATA_TXN, inode, source,
                            sources, healed_sinks, versions, dirty);
out:
        cluster_replies_wipe (replies, ec->nodes);
        return ret;
}

int
ec_heal_entry (call_frame_t *req_frame, ec_t *ec, inode_t *inode)
{
        unsigned char      *locked_on            = NULL;
        unsigned char      *up_subvols           = NULL;
        unsigned char      *output               = NULL;
        char               selfheal_domain[1024] = {0};
        int                ret                   = 0;
        default_args_cbk_t *replies              = NULL;
        call_frame_t       *frame                = NULL;

        EC_REPLIES_ALLOC (replies, ec->nodes);
        locked_on = alloca0(ec->nodes);
        output = alloca0(ec->nodes);
        up_subvols = alloca0(ec->nodes);

        frame = copy_frame (req_frame);
        if (!frame) {
                ret = -ENOMEM;
                goto out;
        }
        /*Do heal as root*/
        frame->root->uid = 0;
        frame->root->gid = 0;
        sprintf (selfheal_domain, "%s:self-heal", ec->xl->name);
        ec_mask_to_char_array (ec->xl_up, up_subvols, ec->nodes);
        /*If other processes are already doing the heal, don't block*/
        ret = cluster_entrylk (ec->xl_list, up_subvols, ec->nodes, replies,
                               locked_on, frame, ec->xl, selfheal_domain, inode,
                               NULL);
        {
                if (ret <= ec->fragments) {
                        gf_log (ec->xl->name, GF_LOG_DEBUG, "%s: Skipping heal "
                                "as only %d number of subvolumes could "
                                "be locked", uuid_utoa (inode->gfid), ret);
                        ret = -ENOTCONN;
                        goto unlock;
                }
                ret = __ec_heal_entry (frame, ec, inode, locked_on);
        }
unlock:
        cluster_unentrylk (ec->xl_list, locked_on, ec->nodes, replies, output,
                           frame, ec->xl, selfheal_domain, inode, NULL);
out:
        cluster_replies_wipe (replies, ec->nodes);
        if (frame)
                STACK_DESTROY (frame->root);
        return ret;
}
