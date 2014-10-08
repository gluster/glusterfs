/*
  Copyright (c) 2012 DataLab, s.l. <http://www.datalab.es>

  This file is part of the cluster/ec translator for GlusterFS.

  The cluster/ec translator for GlusterFS is free software: you can
  redistribute it and/or modify it under the terms of the GNU General
  Public License as published by the Free Software Foundation, either
  version 3 of the License, or (at your option) any later version.

  The cluster/ec translator for GlusterFS is distributed in the hope
  that it will be useful, but WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE. See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with the cluster/ec translator for GlusterFS. If not, see
  <http://www.gnu.org/licenses/>.
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
                heal->version = cbk->version;
                heal->raw_size = cbk->size;
                heal->fop->pre_size = cbk->iatt[0].ia_size;
                heal->fop->post_size = cbk->iatt[0].ia_size;
                heal->fop->have_size = 1;

                if (!ec_loc_prepare(heal->xl, &heal->loc, cbk->inode,
                                    &cbk->iatt[0]))
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

int32_t ec_heal_create_cbk(call_frame_t * frame, void * cookie,
                           xlator_t * this, int32_t op_ret, int32_t op_errno,
                           fd_t * fd, inode_t * inode, struct iatt * buf,
                           struct iatt * preparent, struct iatt * postparent,
                           dict_t * xdata)
{
    ec_heal_update(cookie, 1);

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

int32_t ec_heal_link_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                         int32_t op_ret, int32_t op_errno, inode_t * inode,
                         struct iatt * buf, struct iatt * preparent,
                         struct iatt * postparent, dict_t * xdata)
{
    ec_fop_data_t * fop = cookie;
    ec_heal_t * heal = fop->data;
    uintptr_t good, bad;

    bad = ec_heal_check(fop, &good);
    ec_heal_exclude(heal, good);

    if (bad != 0)
    {
        fop->error = 0;

        xdata = fop->xdata;
        fop = fop->parent;

        ec_create(fop->frame, fop->xl, bad, EC_MINIMUM_ONE,
                  ec_heal_create_cbk, heal, &heal->loc, 0,
                  st_mode_from_ia(heal->iatt.ia_prot, IA_INVAL),
                  0, heal->fd, xdata);
    }

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

int32_t ec_heal_create(ec_heal_t * heal, uintptr_t mask, int32_t try_link)
{
    loc_t loc;
    dict_t * xdata;

    xdata = dict_new();
    if (xdata == NULL)
    {
        return ENOMEM;
    }

    if (dict_set_static_bin(xdata, "gfid-req", heal->iatt.ia_gfid,
                            sizeof(uuid_t)) != 0)
    {
        dict_unref(xdata);

        return ENOMEM;
    }

    if ((heal->iatt.ia_type == IA_IFREG) && try_link)
    {
        memset(&loc, 0, sizeof(loc));
        loc.inode = heal->loc.inode;
        uuid_copy(loc.gfid, heal->iatt.ia_gfid);

        ec_link(heal->fop->frame, heal->xl, mask, EC_MINIMUM_ONE,
                ec_heal_link_cbk, heal, &loc, &heal->loc, xdata);

        dict_unref(xdata);

        return 0;
    }

    switch (heal->iatt.ia_type)
    {
        case IA_IFDIR:
            ec_mkdir(heal->fop->frame, heal->xl, mask, EC_MINIMUM_ONE,
                     ec_heal_mkdir_cbk, heal, &heal->loc,
                     st_mode_from_ia(heal->iatt.ia_prot, IA_INVAL),
                     0, xdata);

            break;

        case IA_IFLNK:
            ec_symlink(heal->fop->frame, heal->xl, mask, EC_MINIMUM_ONE,
                       ec_heal_symlink_cbk, heal, heal->symlink, &heal->loc,
                       0, xdata);

            break;

        case IA_IFREG:
            ec_create(heal->fop->frame, heal->xl, mask, EC_MINIMUM_ONE,
                      ec_heal_create_cbk, heal, &heal->loc, 0,
                      st_mode_from_ia(heal->iatt.ia_prot, IA_INVAL),
                      0, heal->fd, xdata);

            break;

        default:
            ec_mknod(heal->fop->frame, heal->xl, mask, EC_MINIMUM_ONE,
                     ec_heal_mknod_cbk, heal, &heal->loc,
                     st_mode_from_ia(heal->iatt.ia_prot, IA_INVAL),
                     heal->iatt.ia_rdev, 0, xdata);

            break;
    }

    dict_unref(xdata);

    return 0;
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
        ec_heal_create(heal, mask, 0);
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

int32_t ec_heal_init(ec_fop_data_t * fop)
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

    if (!ec_loc_from_loc(fop->xl, &heal->loc, &fop->loc[0]))
    {
        error = ENOMEM;

        goto out;
    }

    LOCK_INIT(&heal->lock);

    heal->xl = fop->xl;
    heal->fop = fop;
    pool = fop->xl->ctx->iobuf_pool;
    heal->size = iobpool_default_pagesize(pool) * ec->fragments;
    heal->partial = fop->int32;

    LOCK(&inode->lock);

    ctx = __ec_inode_get(inode, fop->xl);
    if (ctx == NULL)
    {
        error = EIO;

        goto unlock;
    }

    if (ctx->heal != NULL)
    {
        error = EEXIST;

        goto unlock;
    }

    fop->data = heal;

    ctx->heal = heal;
    heal = NULL;

unlock:
    UNLOCK(&inode->lock);
out:
    GF_FREE(heal);

    return error;
}

void ec_heal_entrylk(ec_heal_t * heal, entrylk_cmd cmd)
{
    loc_t loc;
    int32_t error;

    error = ec_loc_parent(heal->xl, &heal->loc, &loc);
    if (error != 0)
    {
        ec_fop_set_error(heal->fop, error);

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
        // TODO: Remove directory recursively ?
        ec_rmdir(heal->fop->frame, heal->xl, cbk->mask, EC_MINIMUM_ONE,
                 ec_heal_rmdir_cbk, heal, &heal->loc, 0, NULL);
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
            if ((cbk->op_errno != ENOENT) && (cbk->op_errno != ENOTDIR))
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
            if (cbk->op_errno == ENOENT)
            {
                ec_heal_create(heal, cbk->mask, 1);
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
                (uuid_compare(heal->iatt.ia_gfid, cbk->iatt[0].ia_gfid) != 0))
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
    ec_fd_t * ctx;
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
            ctx = ec_fd_get(heal->fd, heal->xl);
            if ((ctx == NULL) || (loc_copy(&ctx->loc, &heal->loc) != 0))
            {
                goto out;
            }

            ctx->flags = O_RDWR;
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
            (uuid_compare(heal->iatt.ia_gfid, cbk->iatt[0].ia_gfid) != 0))
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

void ec_heal_setxattr_others(ec_heal_t * heal)
{
    ec_cbk_data_t * cbk;
    dict_t * xdata;
    int32_t error = ENOMEM;

    if ((heal->good != 0) && (heal->bad != 0))
    {
        cbk = heal->lookup->answer;
        xdata = cbk->xdata;

        if ((cbk->iatt[0].ia_type == IA_IFREG) ||
            (cbk->iatt[0].ia_type == IA_IFDIR))
        {
            if (ec_dict_set_number(xdata, EC_XATTR_VERSION, cbk->version) != 0)
            {
                goto out;
            }
            if (cbk->iatt[0].ia_type == IA_IFREG)
            {
                if (ec_dict_set_number(xdata, EC_XATTR_SIZE,
                                       cbk->iatt[0].ia_size) != 0)
                {
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

int32_t ec_heal_xattr_clean(dict_t * dict, char * key, data_t * data,
                            void * arg)
{
    dict_t * base = arg;

    if (dict_get(base, key) == NULL)
    {
        if (dict_set_static_bin(dict, key, dict, 0) != 0)
        {
            return -1;
        }
    }
    else
    {
        dict_del(dict, key);
    }

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

int32_t ec_heal_needs_data_rebuild(ec_heal_t * heal)
{
    ec_fop_data_t * fop = heal->lookup;
    ec_cbk_data_t * cbk = NULL;
    uintptr_t bad = 0;

    if ((heal->fop->error != 0) || (heal->good == 0) ||
        (heal->iatt.ia_type != IA_IFREG))
    {
        return 0;
    }

    list_for_each_entry(cbk, &fop->cbk_list, list)
    {
        if ((cbk->op_ret >= 0) &&
            ((cbk->size != heal->raw_size) || (cbk->version != heal->version)))
        {
            bad |= cbk->mask;
        }
    }

    /* This function can only be called concurrently with entrylk, which do
     * not modify heal structure, so it's safe to access heal->bad without
     * acquiring any lock.
     */
    heal->bad = bad;

    return (bad != 0);
}

void ec_heal_open(ec_heal_t * heal)
{
    if (!ec_heal_needs_data_rebuild(heal))
    {
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
                    flags = ctx_fd->flags & ~O_TRUNC;
                    if ((flags & O_ACCMODE) == O_WRONLY)
                    {
                        flags &= ~O_ACCMODE;
                        flags |= O_RDWR;
                    }

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

void ec_heal_dispatch(ec_heal_t * heal)
{
    ec_fop_data_t * fop = heal->fop;
    ec_cbk_data_t * cbk;
    inode_t * inode;
    ec_inode_t * ctx;
    int32_t error;

    inode = heal->loc.inode;

    LOCK(&inode->lock);

    ctx = __ec_inode_get(inode, heal->xl);
    if (ctx != NULL)
    {
        ctx->bad &= ~heal->good;
        ctx->heal = NULL;
    }

    fop->data = NULL;

    UNLOCK(&inode->lock);

    error = fop->error;

    cbk = ec_cbk_data_allocate(fop->frame, heal->xl, fop, fop->id, 0,
                               error == 0 ? 0 : -1, error);
    if (cbk != NULL)
    {
        cbk->uintptr[0] = heal->available;
        cbk->uintptr[1] = heal->good;
        cbk->uintptr[2] = heal->fixed;

        ec_combine(cbk, NULL);

        fop->answer = cbk;
    }
    else if (error == 0)
    {
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
}

void ec_wind_heal(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_cbk_data_t * cbk;
    ec_heal_t * heal = fop->data;

    ec_trace("WIND", fop, "idx=%d", idx);

    cbk = ec_cbk_data_allocate(fop->req_frame, fop->xl, fop, EC_FOP_HEAL, idx,
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

int32_t ec_manager_heal(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;
    ec_heal_t * heal = fop->data;

    switch (state)
    {
        case EC_STATE_INIT:
            ec_owner_set(fop->frame, fop->frame->root);

            fop->error = ec_heal_init(fop);
            if (fop->error != 0)
            {
                return EC_STATE_REPORT;
            }

        /* Fall through */

        case EC_STATE_DISPATCH:
            ec_heal_entrylk(fop->data, ENTRYLK_LOCK);

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
            // Only heal data/metadata if enough information is supplied.
            if (uuid_is_null(heal->loc.gfid))
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
            ec_heal_entrylk(heal, ENTRYLK_UNLOCK);

            if (ec_heal_needs_data_rebuild(heal))
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
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(NULL, this, EC_FOP_HEAL,
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
    ec_heal_t * heal = fop->data;

    ec_trace("WIND", fop, "idx=%d", idx);

    cbk = ec_cbk_data_allocate(fop->req_frame, fop->xl, fop, EC_FOP_FHEAL, idx,
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
