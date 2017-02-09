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
#include "byte-order.h"
#include "syncop.h"
#include "syncop-utils.h"
#include "cluster-syncop.h"

#include "ec.h"
#include "ec-mem-types.h"
#include "ec-types.h"
#include "ec-messages.h"
#include "ec-helpers.h"
#include "ec-common.h"
#include "ec-combine.h"
#include "ec-method.h"
#include "ec-fops.h"

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

static gf_boolean_t
ec_sh_key_match (dict_t *dict, char *key, data_t *val, void *mdata)
{
        return !ec_ignorable_key_match (dict, key, val, mdata);
}
/* FOP: heal */

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

int32_t ec_heal_lock_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    ec_fop_data_t *fop = cookie;
    ec_heal_t *heal = fop->data;

    if (op_ret >= 0) {
        GF_ASSERT(ec_set_inode_size(heal->fop, heal->fd->inode,
                                    heal->total_size));
    }

    return 0;
}

void ec_heal_lock(ec_heal_t *heal, int32_t type, fd_t *fd, loc_t *loc,
                  off_t offset, size_t size)
{
    struct gf_flock flock;
    fop_inodelk_cbk_t cbk = NULL;

    flock.l_type = type;
    flock.l_whence = SEEK_SET;
    flock.l_start = offset;
    flock.l_len = size;
    flock.l_pid = 0;
    flock.l_owner.len = 0;

    if (type == F_UNLCK) {
        /* Remove inode size information before unlocking it. */
        if (fd == NULL) {
            ec_clear_inode_info(heal->fop, heal->loc.inode);
        } else {
            ec_clear_inode_info(heal->fop, heal->fd->inode);
        }
    } else {
        /* Otherwise use the callback to update size information. */
        cbk = ec_heal_lock_cbk;
    }

    if (fd != NULL)
    {
        ec_finodelk(heal->fop->frame, heal->xl,
                    &heal->fop->frame->root->lk_owner, heal->fop->mask,
                    EC_MINIMUM_ALL, cbk, heal, heal->xl->name, fd, F_SETLKW,
                    &flock, NULL);
    }
    else
    {
        ec_inodelk(heal->fop->frame, heal->xl,
                   &heal->fop->frame->root->lk_owner, heal->fop->mask,
                   EC_MINIMUM_ALL, cbk, heal, heal->xl->name, loc, F_SETLKW,
                   &flock, NULL);
    }
}

void ec_heal_inodelk(ec_heal_t *heal, int32_t type, int32_t use_fd,
                     off_t offset, size_t size)
{
    ec_heal_lock(heal, type, use_fd ? heal->fd : NULL, &heal->loc, offset,
                 size);
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

int32_t
ec_heal_writev_cbk (call_frame_t *frame, void *cookie,
                    xlator_t *this, int32_t op_ret, int32_t op_errno,
                    struct iatt *prebuf, struct iatt *postbuf,
                    dict_t *xdata)
{
    ec_fop_data_t *fop = cookie;
    ec_heal_t *heal = fop->data;

    ec_trace("WRITE_CBK", cookie, "ret=%d, errno=%d", op_ret, op_errno);

    gf_msg_debug (fop->xl->name, 0, "%s: write op_ret %d, op_errno %s"
            " at %"PRIu64, uuid_utoa (heal->fd->inode->gfid), op_ret,
            strerror (op_errno), heal->offset);

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
        gf_msg_debug (fop->xl->name, 0, "%s: read succeeded, proceeding "
                "to write at %"PRIu64, uuid_utoa (heal->fd->inode->gfid),
                heal->offset);
        ec_writev(heal->fop->frame, heal->xl, heal->bad, EC_MINIMUM_ONE,
                  ec_heal_writev_cbk, heal, heal->fd, vector, count,
                  heal->offset, 0, iobref, NULL);
    }
    else
    {
        if (op_ret < 0) {
                gf_msg_debug (fop->xl->name, 0, "%s: read failed %s, failing "
                        "to heal block at %"PRIu64,
                        uuid_utoa (heal->fd->inode->gfid), strerror (op_errno),
                        heal->offset);
                heal->bad = 0;
        }
        heal->done = 1;
    }

    return 0;
}

void ec_heal_data_block(ec_heal_t *heal)
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

/* FOP: fheal */

void ec_fheal(call_frame_t * frame, xlator_t * this, uintptr_t target,
              int32_t minimum, fop_fheal_cbk_t func, void * data, fd_t * fd,
              int32_t partial, dict_t *xdata)
{
    ec_fd_t * ctx = ec_fd_get(fd, this);

    if (ctx != NULL)
    {
        gf_msg_trace ("ec", 0, "FHEAL ctx: flags=%X, open=%lX", ctx->flags,
                      ctx->open);
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

uintptr_t
ec_char_array_to_mask (unsigned char *array, int numsubvols)
{
        int       i    = 0;
        uintptr_t mask = 0;

        for (i = 0; i < numsubvols; i++)
                if (array[i])
                        mask |= (1ULL<<i);
        return mask;
}

int
ec_heal_entry_find_direction (ec_t *ec, default_args_cbk_t *replies,
                        uint64_t *versions, uint64_t *dirty,
                        unsigned char *sources, unsigned char *healed_sinks)
{
        uint64_t    xattr[EC_VERSION_SIZE] = {0};
        int         source      = -1;
        uint64_t    max_version = 0;
        int         ret         = 0;
        int         i           = 0;

        for (i = 0; i < ec->nodes; i++) {
                if (!replies[i].valid)
                        continue;

                if (replies[i].op_ret == -1)
                        continue;

                if (source == -1)
                        source = i;

                ret = ec_dict_del_array (replies[i].xdata, EC_XATTR_VERSION,
                                         xattr, EC_VERSION_SIZE);
                if (ret == 0) {
                        versions[i] = xattr[EC_DATA_TXN];
                        if (max_version < versions[i]) {
                                max_version = versions[i];
                                source = i;
                        }
                }

                memset (xattr, 0, sizeof(xattr));
                ret = ec_dict_del_array (replies[i].xdata, EC_XATTR_DIRTY,
                                         xattr, EC_VERSION_SIZE);
                if (ret == 0) {
                        dirty[i] = xattr[EC_DATA_TXN];
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

out:
        return source;
}

int
ec_adjust_versions (call_frame_t *frame, ec_t *ec, ec_txn_t type,
                    inode_t *inode, int source, unsigned char *sources,
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
ec_heal_metadata_find_direction (ec_t *ec, default_args_cbk_t *replies,
                                 uint64_t *versions, uint64_t *dirty,
                            unsigned char *sources, unsigned char *healed_sinks)
{
        uint64_t xattr[EC_VERSION_SIZE] = {0};
        uint64_t max_version    = 0;
        int      same_count     = 0;
        int      max_same_count = 0;
        int      same_source    = -1;
        int      ret            = 0;
        int      i              = 0;
        int      j              = 0;
        int      *groups        = NULL;
        struct iatt source_ia   = {0};
        struct iatt child_ia    = {0};

        groups = alloca0 (ec->nodes * sizeof(*groups));
        for (i = 0; i < ec->nodes; i++)
                groups[i] = -1;

        for (i = 0; i < ec->nodes; i++) {
                if (!replies[i].valid)
                        continue;
                if (replies[i].op_ret < 0)
                        continue;
                ret = ec_dict_del_array (replies[i].xdata, EC_XATTR_VERSION,
                                         xattr, EC_VERSION_SIZE);
                if (ret == 0) {
                        versions[i] = xattr[EC_METADATA_TXN];
                }

                memset (xattr, 0, sizeof (xattr));
                ret = ec_dict_del_array (replies[i].xdata, EC_XATTR_DIRTY,
                                         xattr, EC_VERSION_SIZE);
                if (ret == 0) {
                        dirty[i] = xattr[EC_METADATA_TXN];
                }
                if (groups[i] >= 0) /*Already part of group*/
                        continue;
                groups[i] = i;
                same_count = 1;
                source_ia = replies[i].stat;
                for (j = i + 1; j < ec->nodes; j++) {
                        if (!replies[j].valid || replies[j].op_ret < 0)
                                continue;
                        child_ia = replies[j].stat;
                        if (!IA_EQUAL(source_ia, child_ia, gfid) ||
                            !IA_EQUAL(source_ia, child_ia, type) ||
                            !IA_EQUAL(source_ia, child_ia, prot) ||
                            !IA_EQUAL(source_ia, child_ia, uid) ||
                            !IA_EQUAL(source_ia, child_ia, gid))
                                continue;
                        if (!are_dicts_equal(replies[i].xdata, replies[j].xdata,
                                             ec_sh_key_match, NULL))
                                continue;
                        groups[j] = i; /*If iatts match put them into a group*/
                        same_count++;
                }

                if (max_same_count < same_count) {
                        max_same_count = same_count;
                        same_source = i;
                }
        }

        if (max_same_count < ec->fragments) {
                ret = -EIO;
                goto out;
        }

        for (i = 0; i < ec->nodes; i++) {
                if (groups[i] == groups[same_source])
                        sources[i] = 1;
                else if (replies[i].valid && replies[i].op_ret >= 0)
                        healed_sinks[i] = 1;
        }
        for (i = 0; i < ec->nodes; i++) {
                if (sources[i] && (versions[i] > max_version)) {
                         same_source = i;
                         max_version = versions[i];
                }
        }
        ret = same_source;
out:
        return ret;
}


int
__ec_heal_metadata_prepare (call_frame_t *frame, ec_t *ec, inode_t *inode,
                   unsigned char *locked_on, default_args_cbk_t *replies,
                   uint64_t *versions, uint64_t *dirty, unsigned char *sources,
                   unsigned char *healed_sinks)
{
        loc_t              loc        = {0};
        unsigned char      *output    = NULL;
        unsigned char      *lookup_on = NULL;
        int                ret        = 0;
        int                source     = 0;
        default_args_cbk_t *greplies  = NULL;
        int                i          = 0;
        EC_REPLIES_ALLOC (greplies, ec->nodes);

        loc.inode = inode_ref (inode);
        gf_uuid_copy (loc.gfid, inode->gfid);
        output = alloca0 (ec->nodes);
        lookup_on = alloca0 (ec->nodes);
        ret = cluster_lookup (ec->xl_list, locked_on, ec->nodes, replies,
                              output, frame, ec->xl, &loc, NULL);
        if (ret <= ec->fragments) {
                ret = -ENOTCONN;
                goto out;
        }

        memcpy (lookup_on, output, ec->nodes);
        /*Use getxattr to get the filtered xattrs which filter internal xattrs*/
        ret = cluster_getxattr (ec->xl_list, lookup_on, ec->nodes, greplies,
                                output, frame, ec->xl, &loc, NULL, NULL);
        for (i = 0; i < ec->nodes; i++) {
                if (lookup_on[i] && !output[i]) {
                        replies[i].valid = 0;
                        continue;
                }
                if (replies[i].xdata) {
                        dict_unref (replies[i].xdata);
                        replies[i].xdata = NULL;
                        if (greplies[i].xattr)
                                replies[i].xdata = dict_ref (greplies[i].xattr);
                }
        }

        source = ec_heal_metadata_find_direction (ec, replies, versions,
                                         dirty, sources, healed_sinks);
        if (source < 0) {
                ret = -EIO;
                goto out;
        }
        ret = source;
out:
        cluster_replies_wipe (greplies, ec->nodes);
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
                ret = dict_foreach (replies[i].xdata, ec_heal_xattr_clean,
                                    replies[source].xdata);
                if (ret < 0) {
                        sources[i] = 0;
                        healed_sinks[i] = 0;
                        continue;
                }

                if (replies[i].xdata->count == 0) {
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
                                          replies[i].xdata, NULL);
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
                    unsigned char *locked_on, unsigned char *sources,
                    unsigned char *healed_sinks)
{
        loc_t              loc           = {0};
        int                ret           = 0;
        int                source        = 0;
        default_args_cbk_t *replies      = NULL;
        default_args_cbk_t *sreplies     = NULL;
        uint64_t           *versions     = NULL;
        uint64_t           *dirty        = NULL;
        unsigned char      *output       = NULL;
        dict_t             *source_dict  = NULL;
        struct iatt        source_buf    = {0};

        EC_REPLIES_ALLOC (replies, ec->nodes);
        EC_REPLIES_ALLOC (sreplies, ec->nodes);

        loc.inode = inode_ref (inode);
        gf_uuid_copy (loc.gfid, inode->gfid);
        output = alloca0 (ec->nodes);
        versions = alloca0 (ec->nodes * sizeof (*versions));
        dirty = alloca0 (ec->nodes * sizeof (*dirty));
        source = __ec_heal_metadata_prepare (frame, ec, inode, locked_on, replies,
                                    versions, dirty, sources, healed_sinks);
        if (source < 0) {
                ret = -EIO;
                goto out;
        }

        if (EC_COUNT (sources, ec->nodes) == ec->nodes) {
                ret = 0;
                goto erase_dirty;
        }

        if (EC_COUNT (healed_sinks, ec->nodes) == 0) {
                ret = -ENOTCONN;
                goto out;
        }
        source_buf = replies[source].stat;
        ret = cluster_setattr (ec->xl_list, healed_sinks, ec->nodes, sreplies,
                               output, frame, ec->xl, &loc,
                               &source_buf, GF_SET_ATTR_MODE |
                               GF_SET_ATTR_UID | GF_SET_ATTR_GID, NULL);
        /*In case the operation fails on some of the subvols*/
        memcpy (healed_sinks, output, ec->nodes);
        if (EC_COUNT (healed_sinks, ec->nodes) == 0) {
                ret = -ENOTCONN;
                goto out;
        }

        ret = __ec_removexattr_sinks (frame, ec, inode, source, sources,
                                      healed_sinks, replies);
        if (ret < 0)
                goto out;

        source_dict = dict_ref (replies[source].xdata);
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

erase_dirty:
        ret = ec_adjust_versions (frame, ec, EC_METADATA_TXN, inode, source,
                                  sources, healed_sinks, versions, dirty);
out:
        if (source_dict)
                dict_unref (source_dict);

        loc_wipe (&loc);
        cluster_replies_wipe (replies, ec->nodes);
        cluster_replies_wipe (sreplies, ec->nodes);
        return ret;
}

int
ec_heal_metadata (call_frame_t *frame, ec_t *ec, inode_t *inode,
                  unsigned char *sources, unsigned char *healed_sinks)
{
        unsigned char      *locked_on  = NULL;
        unsigned char      *up_subvols = NULL;
        unsigned char      *output     = NULL;
        int                ret         = 0;
        default_args_cbk_t *replies    = NULL;

        EC_REPLIES_ALLOC (replies, ec->nodes);
        locked_on = alloca0(ec->nodes);
        output = alloca0(ec->nodes);
        up_subvols = alloca0(ec->nodes);
        ec_mask_to_char_array (ec->xl_up, up_subvols, ec->nodes);
        ret = cluster_inodelk (ec->xl_list, up_subvols, ec->nodes, replies,
                               locked_on, frame, ec->xl, ec->xl->name, inode, 0,
                               0);
        {
                if (ret <= ec->fragments) {
                        gf_msg_debug (ec->xl->name, 0, "%s: Skipping heal "
                                "as only %d number of subvolumes could "
                                "be locked", uuid_utoa (inode->gfid), ret);
                        ret = -ENOTCONN;
                        goto unlock;
                }
                ret = __ec_heal_metadata (frame, ec, inode, locked_on, sources,
                                          healed_sinks);
        }
unlock:
        cluster_uninodelk (ec->xl_list, locked_on, ec->nodes, replies, output,
                           frame, ec->xl, ec->xl->name, inode, 0, 0);
        cluster_replies_wipe (replies, ec->nodes);
        return ret;
}

/*entry heal*/
int
__ec_heal_entry_prepare (call_frame_t *frame, ec_t *ec, inode_t *inode,
                         unsigned char *locked_on, uint64_t *versions,
                         uint64_t *dirty, unsigned char *sources,
                         unsigned char *healed_sinks)
{
        loc_t              loc      = {0};
        int                source   = 0;
        int                ret      = 0;
        default_args_cbk_t *replies = NULL;
        unsigned char      *output  = NULL;
        dict_t             *xdata   = NULL;

        EC_REPLIES_ALLOC (replies, ec->nodes);

        loc.inode = inode_ref (inode);
        gf_uuid_copy (loc.gfid, inode->gfid);
        xdata = dict_new ();
        if (!xdata) {
                ret = -ENOMEM;
                goto out;
        }

        if (dict_set_uint64(xdata, EC_XATTR_VERSION, 0) ||
            dict_set_uint64(xdata, EC_XATTR_DIRTY, 0)) {
                ret = -ENOMEM;
                goto out;
        }

        output = alloca0 (ec->nodes);
        ret = cluster_lookup (ec->xl_list, locked_on, ec->nodes, replies,
                              output, frame, ec->xl, &loc, xdata);
        if (ret <= ec->fragments) {
                ret = -ENOTCONN;
                goto out;
        }

        source = ec_heal_entry_find_direction (ec, replies, versions,
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
        cluster_replies_wipe (replies, ec->nodes);
        return ret;
}
int32_t
ec_set_new_entry_dirty (ec_t *ec, loc_t *loc, struct iatt *ia,
                        call_frame_t *frame, xlator_t *this, unsigned char *on)
{
        dict_t              *xattr = NULL;
        int32_t             ret    = -1;
        default_args_cbk_t  *replies = NULL;
        unsigned char       *output  = NULL;
        uint64_t            dirty[EC_VERSION_SIZE] = {1, 1};
        loc_t               newloc = {0};

        /*Symlinks don't have any data to be healed*/
        if (ia->ia_type == IA_IFLNK)
                dirty[EC_DATA_TXN] = 0;

        newloc.inode = inode_ref (loc->inode);
        gf_uuid_copy (newloc.gfid, ia->ia_gfid);
        EC_REPLIES_ALLOC (replies, ec->nodes);
        output = alloca0 (ec->nodes);
        xattr = dict_new();
        if (!xattr) {
                ret = -ENOMEM;
                goto out;
        }

        ret = ec_dict_set_array (xattr, EC_XATTR_DIRTY, dirty,
                                 EC_VERSION_SIZE);
        if (ret)
                goto out;

        ret = cluster_xattrop (ec->xl_list, on, ec->nodes, replies, output,
                               frame, ec->xl, &newloc,
                               GF_XATTROP_ADD_ARRAY64, xattr, NULL);

        if (ret < ec->fragments) {
                ret = -ENOTCONN;
                goto out;
        }
out:
        if (xattr)
                dict_unref (xattr);
        cluster_replies_wipe (replies, ec->nodes);
        loc_wipe (&newloc);
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
        if (ret < 0) {
                gf_msg_debug (ec->xl->name, 0, "%s/%s: heal failed %s",
                        uuid_utoa (name_data->parent->gfid), name_data->name,
                        strerror (-ret));
        }
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
        unsigned char       *on       = NULL;
        default_args_cbk_t  *replies  = NULL;
        loc_t               loc       = {0};
        loc_t               srcloc    = {0};
        unsigned char       *link     = NULL;
        unsigned char       *create   = NULL;
        dict_t              *xdata    = NULL;
        char                *linkname = NULL;
        ec_config_t         config;
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
        on = alloca0 (ec->nodes);
        output = alloca0 (ec->nodes);
        output1 = alloca0 (ec->nodes);

        for (i = 0; i < ec->nodes; i++) {
                if (!lookup_replies[i].valid)
                        continue;
                if (lookup_replies[i].op_ret)
                        continue;
                on[i] = 1;
        }
        switch (ia->ia_type) {
        case IA_IFDIR:
                ec_set_new_entry_dirty (ec, &loc, ia, frame, ec->xl, on);
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
                        ec_set_new_entry_dirty (ec, &loc, ia, frame,
                                                ec->xl, on);
                        cluster_symlink (ec->xl_list, create, ec->nodes,
                                         replies, output, frame, ec->xl,
                                         linkname, &loc, 0, xdata);
                }
                for (i = 0; i < ec->nodes; i++)
                        if (output1[i])
                                output[i] = 1;
                break;
        case IA_IFREG:
                ec_set_new_entry_dirty (ec, &loc, ia,
                                        frame, ec->xl, on);
                config.version = EC_CONFIG_VERSION;
                config.algorithm = EC_CONFIG_ALGORITHM;
                config.gf_word_size = EC_GF_BITS;
                config.bricks = ec->nodes;
                config.redundancy = ec->redundancy;
                config.chunk_size = EC_METHOD_CHUNK_SIZE;

                ret = ec_dict_set_config(xdata, EC_XATTR_CONFIG, &config);
                if (ret != 0) {
                        goto out;
                }
        default:
                ret = dict_set_int32 (xdata, GLUSTERFS_INTERNAL_FOP_KEY,
                                      1);
                if (ret)
                        goto out;
                ret = cluster_mknod (ec->xl_list, enoent, ec->nodes,
                                     replies, output, frame, ec->xl,
                                     &loc, st_mode_from_ia (ia->ia_prot,
                                                           ia->ia_type),
                         makedev(ia_major(ia->ia_rdev), ia_minor(ia->ia_rdev)),
                         0, xdata);
                break;
        }

        for (i = 0; i < ec->nodes; i++) {
                if (enoent[i] && !output[i])
                        participants[i] = 0;
        }

        ret = 0;
out:
        if (ret < 0)
                gf_msg_debug (ec->xl->name, 0, "%s/%s: heal failed %s",
                        uuid_utoa (parent->gfid), name, strerror (-ret));
        cluster_replies_wipe (replies, ec->nodes);
        loc_wipe (&loc);
        loc_wipe (&srcloc);
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

        EC_REPLIES_ALLOC (replies, ec->nodes);
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
                gf_msg (ec->xl->name, GF_LOG_INFO, 0,
                        EC_MSG_HEAL_FAIL, "%s/%s: Not able to heal",
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
        ret = cluster_inodelk (ec->xl_list, participants, ec->nodes, replies,
                               locked_on, frame, ec->xl, ec->xl->name, parent,
                               0, 0);
        {
                if (ret <= ec->fragments) {
                        gf_msg_debug (ec->xl->name, 0, "%s/%s: Skipping "
                                "heal as only %d number of subvolumes could "
                                "be locked", uuid_utoa (parent->gfid), name,
                                ret);
                        ret = -ENOTCONN;
                        goto unlock;
                }
                EC_INTERSECT (participants, participants, locked_on, ec->nodes);
                ret = __ec_heal_name (frame, ec, parent, name, participants);
        }
unlock:
        cluster_uninodelk (ec->xl_list, locked_on, ec->nodes, replies, output,
                           frame, ec->xl, ec->xl->name, parent, 0, 0);
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
                                GF_CLIENT_PID_SELF_HEALD, &name_data,
                                ec_name_heal_handler);
                for (j = 0; j < ec->nodes; j++)
                        if (name_data.failed_on[j])
                                participants[j] = 0;

                if (EC_COUNT (participants, ec->nodes) <= ec->fragments)
                        return -ENOTCONN;
        }
        loc_wipe (&loc);
        return 0;
}

int
__ec_heal_entry (call_frame_t *frame, ec_t *ec, inode_t *inode,
                 unsigned char *heal_on, unsigned char *sources,
                 unsigned char *healed_sinks)
{
        unsigned char      *locked_on    = NULL;
        unsigned char      *output       = NULL;
        uint64_t           *versions     = NULL;
        uint64_t           *dirty        = NULL;
        unsigned char      *participants = NULL;
        default_args_cbk_t *replies      = NULL;
        int                ret           = 0;
        int                source        = 0;
        int                i             = 0;

        locked_on = alloca0(ec->nodes);
        output = alloca0(ec->nodes);
        versions = alloca0 (ec->nodes * sizeof (*versions));
        dirty = alloca0 (ec->nodes * sizeof (*dirty));

        EC_REPLIES_ALLOC (replies, ec->nodes);
        ret = cluster_inodelk (ec->xl_list, heal_on, ec->nodes, replies,
                               locked_on, frame, ec->xl, ec->xl->name, inode,
                               0, 0);
        {
                if (ret <= ec->fragments) {
                        gf_msg_debug (ec->xl->name, 0, "%s: Skipping heal "
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
        cluster_uninodelk (ec->xl_list, locked_on, ec->nodes, replies, output,
                           frame, ec->xl, ec->xl->name, inode, 0, 0);
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
ec_heal_entry (call_frame_t *frame, ec_t *ec, inode_t *inode,
               unsigned char *sources, unsigned char *healed_sinks)
{
        unsigned char      *locked_on            = NULL;
        unsigned char      *up_subvols           = NULL;
        unsigned char      *output               = NULL;
        char               selfheal_domain[1024] = {0};
        int                ret                   = 0;
        default_args_cbk_t *replies              = NULL;

        EC_REPLIES_ALLOC (replies, ec->nodes);
        locked_on = alloca0(ec->nodes);
        output = alloca0(ec->nodes);
        up_subvols = alloca0(ec->nodes);

        sprintf (selfheal_domain, "%s:self-heal", ec->xl->name);
        ec_mask_to_char_array (ec->xl_up, up_subvols, ec->nodes);
        /*If other processes are already doing the heal, don't block*/
        ret = cluster_inodelk (ec->xl_list, up_subvols, ec->nodes, replies,
                               locked_on, frame, ec->xl, selfheal_domain, inode,
                               0, 0);
        {
                if (ret <= ec->fragments) {
                        gf_msg_debug (ec->xl->name, 0, "%s: Skipping heal "
                                "as only %d number of subvolumes could "
                                "be locked", uuid_utoa (inode->gfid), ret);
                        ret = -ENOTCONN;
                        goto unlock;
                }
                ret = __ec_heal_entry (frame, ec, inode, locked_on,
                                       sources, healed_sinks);
        }
unlock:
        cluster_uninodelk (ec->xl_list, locked_on, ec->nodes, replies, output,
                           frame, ec->xl, selfheal_domain, inode, 0, 0);
        cluster_replies_wipe (replies, ec->nodes);
        return ret;
}

/*Find direction for data heal and heal info*/
int
ec_heal_data_find_direction (ec_t *ec, default_args_cbk_t *replies,
                       uint64_t *data_versions,  uint64_t *meta_versions,
                       uint64_t *dirty, uint64_t *size, unsigned char *sources,
                       unsigned char *healed_sinks, int which)
{
        uint64_t        xattr[EC_VERSION_SIZE] = {0};
        char            version_size[128] = {0};
        dict_t          *version_size_db = NULL;
        uint64_t        *m_versions      = NULL;
        unsigned char   *same            = NULL;
        int             max_same_count   = 0;
        int             source           = 0;
        int             i                = 0;
        int             ret              = 0;
        dict_t          *dict            = NULL;

        if (!meta_versions) {
                m_versions = alloca0 (ec->nodes * sizeof (*m_versions));
        } else {
                m_versions = meta_versions;
        }
        version_size_db = dict_new ();
        if (!version_size_db) {
                ret = -ENOMEM;
                goto out;
        }

        for (i = 0; i < ec->nodes; i++) {
                if (!replies[i].valid)
                        continue;
                if (replies[i].op_ret < 0)
                        continue;
                dict = (which == EC_COMBINE_XDATA) ? replies[i].xdata :
                                                     replies[i].xattr;

                ret = ec_dict_del_array (dict, EC_XATTR_VERSION,
                                         xattr, EC_VERSION_SIZE);
                if (ret == 0) {
                        data_versions[i] = xattr[EC_DATA_TXN];
                        if (meta_versions) {
                                m_versions[i] = xattr[EC_METADATA_TXN];
                        }
                }

                memset (xattr, 0, sizeof (xattr));
                ret = ec_dict_del_array (dict, EC_XATTR_DIRTY,
                                         xattr, EC_VERSION_SIZE);
                if (ret == 0) {
                        dirty[i] = xattr[EC_DATA_TXN];
                }
                ret = ec_dict_del_number (dict, EC_XATTR_SIZE,
                                          &size[i]);
                /*Build a db of same metadata and data version and size*/
                snprintf (version_size, sizeof (version_size),
                          "%"PRIu64"-%"PRIu64"-%"PRIu64, data_versions[i],
                          m_versions[i], size[i]);

                ret = dict_get_bin (version_size_db, version_size,
                                    (void **)&same);
                if (ret < 0) {
                        same = alloca0 (ec->nodes);
                }

                same[i] = 1;
                if (max_same_count < EC_COUNT (same, ec->nodes)) {
                        max_same_count = EC_COUNT (same, ec->nodes);
                        source = i;
                }

                if (ret < 0) {
                        ret = dict_set_static_bin (version_size_db,
                                                version_size, same, ec->nodes);
                }

                if (ret < 0) {
                        ret = -ENOMEM;
                        goto out;
                }
        }
        /* If we don't have ec->fragments number of same version,size it is not
         * recoverable*/
        if (max_same_count < ec->fragments) {
                ret = -EIO;
                goto out;
        } else {
                snprintf (version_size, sizeof (version_size),
                          "%"PRIu64"-%"PRIu64"-%"PRIu64,
                          data_versions[source],
                          m_versions[source],
                          size[source]);

                ret = dict_get_bin (version_size_db, version_size,
                                    (void **)&same);
                if (ret < 0)
                        goto out;
                memcpy (sources, same, ec->nodes);
                for (i = 0; i < ec->nodes; i++) {
                        if (replies[i].valid && (replies[i].op_ret == 0) &&
                            !sources[i])
                                healed_sinks[i] = 1;
                }
        }

        ret = source;
out:
        if (version_size_db)
                dict_unref (version_size_db);
        return ret;
}

int
__ec_heal_data_prepare (call_frame_t *frame, ec_t *ec, fd_t *fd,
                        unsigned char *locked_on, uint64_t *versions,
                        uint64_t *dirty, uint64_t *size, unsigned char *sources,
                        unsigned char *healed_sinks, unsigned char *trim,
                        struct iatt *stbuf)
{
        default_args_cbk_t *replies = NULL;
        unsigned char      *output  = NULL;
        dict_t             *xattrs  = NULL;
        uint64_t           zero_array[2] = {0};
        int                source   = 0;
        int                ret      = 0;
        uint64_t           zero_value = 0;
        uint64_t           source_size = 0;
        int                i = 0;

        EC_REPLIES_ALLOC (replies, ec->nodes);
        output = alloca0(ec->nodes);
        xattrs = dict_new ();
        if (!xattrs ||
            dict_set_static_bin (xattrs, EC_XATTR_VERSION, zero_array,
                                 sizeof (zero_array)) ||
            dict_set_static_bin (xattrs, EC_XATTR_DIRTY, zero_array,
                                 sizeof (zero_array)) ||
            dict_set_static_bin (xattrs, EC_XATTR_SIZE, &zero_value,
                                 sizeof (zero_value))) {
                ret = -ENOMEM;
                goto out;
        }

        ret = cluster_fxattrop (ec->xl_list, locked_on, ec->nodes,
                                replies, output, frame, ec->xl, fd,
                                GF_XATTROP_ADD_ARRAY64, xattrs, NULL);
        if (EC_COUNT (output, ec->nodes) <= ec->fragments) {
                ret = -ENOTCONN;
                goto out;
        }

        source = ec_heal_data_find_direction (ec, replies, versions, NULL,
                                              dirty, size, sources,
                                              healed_sinks, EC_COMBINE_DICT);
        ret = source;
        if (ret < 0)
                goto out;

        /* There could be files with versions, size same but on disk ia_size
         * could be different because of disk crashes, mark them as sinks as
         * well*/
        ret = cluster_fstat (ec->xl_list, locked_on, ec->nodes, replies,
                             output, frame, ec->xl, fd, NULL);
        EC_INTERSECT (sources, sources, output, ec->nodes);
        EC_INTERSECT (healed_sinks, healed_sinks, output, ec->nodes);
        if (EC_COUNT (sources, ec->nodes) < ec->fragments) {
                ret = -ENOTCONN;
                goto out;
        }

        source_size = ec_adjust_size (ec, size[source], 1);

        for (i = 0; i < ec->nodes; i++) {
                if (sources[i]) {
                        if (replies[i].stat.ia_size != source_size) {
                                sources[i] = 0;
                                healed_sinks[i] = 1;
                        } else if (stbuf) {
                                source = i;
                                *stbuf = replies[i].stat;
                        }
                }

                if (healed_sinks[i]) {
                        if (replies[i].stat.ia_size)
                                trim[i] = 1;
                }
        }

        if (EC_COUNT(sources, ec->nodes) < ec->fragments) {
                ret = -ENOTCONN;
                goto out;
        }

        ret = source;
out:
        if (xattrs)
                dict_unref (xattrs);
        cluster_replies_wipe (replies, ec->nodes);
        if (ret < 0) {
                gf_msg_debug (ec->xl->name, 0, "%s: heal failed %s",
                        uuid_utoa (fd->inode->gfid), strerror (-ret));
        } else {
                gf_msg_debug (ec->xl->name, 0, "%s: sources: %d, sinks: "
                        "%d", uuid_utoa (fd->inode->gfid),
                        EC_COUNT (sources, ec->nodes),
                        EC_COUNT (healed_sinks, ec->nodes));
        }
        return ret;
}

int
__ec_heal_mark_sinks (call_frame_t *frame, ec_t *ec, fd_t *fd,
                      uint64_t *versions, unsigned char *healed_sinks)
{
        int                i        = 0;
        int                ret      = 0;
        unsigned char      *mark    = NULL;
        dict_t             *xattrs  = NULL;
        default_args_cbk_t *replies = NULL;
        unsigned char      *output  = NULL;
        uint64_t versions_xattr[2]  = {0};

        EC_REPLIES_ALLOC (replies, ec->nodes);
        xattrs = dict_new ();
        if (!xattrs) {
                ret = -ENOMEM;
                goto out;
        }

        mark = alloca0 (ec->nodes);
        for (i = 0; i < ec->nodes; i++) {
                if (!healed_sinks[i])
                        continue;
                if ((versions[i] >> EC_SELFHEAL_BIT) & 1)
                        continue;
                mark[i] = 1;
        }

        if (EC_COUNT (mark, ec->nodes) == 0)
                return 0;

        versions_xattr[EC_DATA_TXN] = hton64(1ULL<<EC_SELFHEAL_BIT);
        if (dict_set_static_bin (xattrs, EC_XATTR_VERSION, versions_xattr,
                                 sizeof (versions_xattr))) {
                ret = -ENOMEM;
                goto out;
        }

        output = alloca0 (ec->nodes);
        ret = cluster_fxattrop (ec->xl_list, mark, ec->nodes,
                                replies, output, frame, ec->xl, fd,
                                GF_XATTROP_ADD_ARRAY64, xattrs, NULL);
        for (i = 0; i < ec->nodes; i++) {
                if (!output[i]) {
                        if (mark[i])
                                healed_sinks[i] = 0;
                        continue;
                }
                versions[i] |= (1ULL<<EC_SELFHEAL_BIT);
        }

        if (EC_COUNT (healed_sinks, ec->nodes) == 0) {
                ret = -ENOTCONN;
                goto out;
        }
        ret = 0;

out:
        cluster_replies_wipe (replies, ec->nodes);
        if (xattrs)
                dict_unref (xattrs);
        if (ret < 0)
                gf_msg_debug (ec->xl->name, 0, "%s: heal failed %s",
                        uuid_utoa (fd->inode->gfid), strerror (-ret));
        return ret;
}

int32_t
ec_manager_heal_block (ec_fop_data_t *fop, int32_t state)
{
    ec_heal_t *heal = fop->data;
    heal->fop = fop;

    switch (state) {
    case EC_STATE_INIT:
        ec_owner_set(fop->frame, fop->frame->root);

        ec_heal_inodelk(heal, F_WRLCK, 1, 0, 0);

        return EC_STATE_HEAL_DATA_COPY;

    case EC_STATE_HEAL_DATA_COPY:
        gf_msg_debug (fop->xl->name, 0, "%s: read/write starting",
                uuid_utoa (heal->fd->inode->gfid));
        ec_heal_data_block (heal);

        return EC_STATE_HEAL_DATA_UNLOCK;

    case -EC_STATE_HEAL_DATA_COPY:
    case -EC_STATE_HEAL_DATA_UNLOCK:
    case EC_STATE_HEAL_DATA_UNLOCK:
        ec_heal_inodelk(heal, F_UNLCK, 1, 0, 0);

         return EC_STATE_REPORT;

    case EC_STATE_REPORT:
        if (fop->cbks.heal) {
            fop->cbks.heal (fop->req_frame, fop, fop->xl, 0,
                            0, (heal->good | heal->bad),
                            heal->good, heal->bad, NULL);
        }

        return EC_STATE_END;
    case -EC_STATE_REPORT:
        if (fop->cbks.heal) {
            fop->cbks.heal (fop->req_frame, fop, fop->xl, -1,
                            fop->error, 0, 0, 0, NULL);
        }

        return EC_STATE_END;
    default:
        gf_msg (fop->xl->name, GF_LOG_ERROR, 0,
                EC_MSG_UNHANDLED_STATE, "Unhandled state %d for %s",
                state, ec_fop_name(fop->id));

        return EC_STATE_END;
    }
}

/*Takes lock */
void
ec_heal_block (call_frame_t *frame, xlator_t *this, uintptr_t target,
              int32_t minimum, fop_heal_cbk_t func, ec_heal_t *heal)
{
    ec_cbk_t callback = { .heal = func };
    ec_fop_data_t *fop = NULL;
    int32_t error = ENOMEM;

    gf_msg_trace("ec", 0, "EC(HEAL) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate (frame, this, EC_FOP_HEAL, 0, target, minimum,
                                NULL, ec_manager_heal_block, callback,
                                heal);
    if (fop == NULL)
        goto out;

    error = 0;

out:
    if (fop != NULL) {
        ec_manager(fop, error);
    } else {
        func(frame, NULL, this, -1, error, 0, 0, 0, NULL);
    }
}

int32_t
ec_heal_block_done (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, uintptr_t mask,
                    uintptr_t good, uintptr_t bad, dict_t *xdata)
{
        ec_fop_data_t *fop = cookie;
        ec_heal_t *heal = fop->data;

        fop->heal = NULL;
        heal->fop = NULL;
        heal->error = op_ret < 0 ? op_errno : 0;
        syncbarrier_wake (heal->data);
        return 0;
}

int
ec_sync_heal_block (call_frame_t *frame, xlator_t *this, ec_heal_t *heal)
{
        ec_heal_block (frame, this, heal->bad|heal->good, EC_MINIMUM_ONE,
                       ec_heal_block_done, heal);
        syncbarrier_wait (heal->data, 1);
        if (heal->error != 0) {
                return -heal->error;
        }
        if (heal->bad == 0)
                return -ENOTCONN;
        return 0;
}

int
ec_rebuild_data (call_frame_t *frame, ec_t *ec, fd_t *fd, uint64_t size,
                 unsigned char *sources, unsigned char *healed_sinks)
{
        ec_heal_t        *heal = NULL;
        int              ret = 0;
        syncbarrier_t    barrier;
        struct iobuf_pool *pool = NULL;

        if (syncbarrier_init (&barrier))
                return -ENOMEM;

        heal = alloca0(sizeof (*heal));
        heal->fd = fd_ref (fd);
        heal->xl = ec->xl;
        heal->data = &barrier;
        syncbarrier_init (heal->data);
        pool = ec->xl->ctx->iobuf_pool;
        heal->total_size = size;
        heal->size = iobpool_default_pagesize (pool);
        /* We need to adjust the size to a multiple of the stripe size of the
         * volume. Otherwise writes would need to fill gaps (head and/or tail)
         * with existent data from the bad bricks. This could be garbage on a
         * damaged file or it could fail if there aren't enough bricks. */
        heal->size -= heal->size % ec->stripe_size;
        heal->bad       = ec_char_array_to_mask (healed_sinks, ec->nodes);
        heal->good      = ec_char_array_to_mask (sources, ec->nodes);
        heal->iatt.ia_type = IA_IFREG;
        LOCK_INIT(&heal->lock);

        for (heal->offset = 0; (heal->offset < size) && !heal->done;
                                                   heal->offset += heal->size) {
                gf_msg_debug (ec->xl->name, 0, "%s: sources: %d, sinks: "
                        "%d, offset: %"PRIu64" bsize: %"PRIu64,
                        uuid_utoa (fd->inode->gfid),
                        EC_COUNT (sources, ec->nodes),
                        EC_COUNT (healed_sinks, ec->nodes), heal->offset,
                        heal->size);
                ret = ec_sync_heal_block (frame, ec->xl, heal);
                if (ret < 0)
                        break;

        }
        memset (healed_sinks, 0, ec->nodes);
        ec_mask_to_char_array (heal->bad, healed_sinks, ec->nodes);
        fd_unref (heal->fd);
        LOCK_DESTROY (&heal->lock);
        syncbarrier_destroy (heal->data);
        if (ret < 0)
                gf_msg_debug (ec->xl->name, 0, "%s: heal failed %s",
                        uuid_utoa (fd->inode->gfid), strerror (-ret));
        return ret;
}

int
__ec_heal_trim_sinks (call_frame_t *frame, ec_t *ec, fd_t *fd,
                      unsigned char *healed_sinks, unsigned char *trim)
{
        default_args_cbk_t *replies = NULL;
        unsigned char      *output  = NULL;
        int                ret      = 0;
        int                i        = 0;

        EC_REPLIES_ALLOC (replies, ec->nodes);
        output       = alloca0 (ec->nodes);

        if (EC_COUNT (trim, ec->nodes) == 0) {
                ret = 0;
                goto out;
        }

        ret = cluster_ftruncate (ec->xl_list, trim, ec->nodes, replies, output,
                                 frame, ec->xl, fd, 0, NULL);
        for (i = 0; i < ec->nodes; i++) {
                if (!output[i] && trim[i])
                        healed_sinks[i] = 0;
        }

        if (EC_COUNT (healed_sinks, ec->nodes) == 0) {
                ret = -ENOTCONN;
                goto out;
        }

out:
        cluster_replies_wipe (replies, ec->nodes);
        if (ret < 0)
                gf_msg_debug (ec->xl->name, 0, "%s: heal failed %s",
                        uuid_utoa (fd->inode->gfid), strerror (-ret));
        return ret;
}

int
ec_data_undo_pending (call_frame_t *frame, ec_t *ec, fd_t *fd, dict_t *xattr,
                      uint64_t *versions, uint64_t *dirty, uint64_t *size,
                      int source, gf_boolean_t erase_dirty, int idx)
{
        uint64_t versions_xattr[2] = {0};
        uint64_t dirty_xattr[2]    = {0};
        uint64_t allzero[2]        = {0};
        uint64_t size_xattr        = 0;
        int      ret               = 0;

        versions_xattr[EC_DATA_TXN] = hton64(versions[source] - versions[idx]);
        ret = dict_set_static_bin (xattr, EC_XATTR_VERSION,
                                   versions_xattr,
                                   sizeof (versions_xattr));
        if (ret < 0)
                goto out;

        size_xattr = hton64(size[source] - size[idx]);
        ret = dict_set_static_bin (xattr, EC_XATTR_SIZE,
                                   &size_xattr, sizeof (size_xattr));
        if (ret < 0)
                goto out;

        if (erase_dirty) {
                dirty_xattr[EC_DATA_TXN] = hton64(-dirty[idx]);
                ret = dict_set_static_bin (xattr, EC_XATTR_DIRTY,
                                           dirty_xattr,
                                           sizeof (dirty_xattr));
                if (ret < 0)
                        goto out;
        }

        if ((memcmp (versions_xattr, allzero, sizeof (allzero)) == 0) &&
            (memcmp (dirty_xattr, allzero, sizeof (allzero)) == 0) &&
             (size == 0)) {
                ret = 0;
                goto out;
        }

        ret = syncop_fxattrop (ec->xl_list[idx], fd,
                               GF_XATTROP_ADD_ARRAY64, xattr, NULL, NULL);
out:
        return ret;
}

int
__ec_fd_data_adjust_versions (call_frame_t *frame, ec_t *ec, fd_t *fd,
                            unsigned char *sources, unsigned char *healed_sinks,
                            uint64_t *versions, uint64_t *dirty, uint64_t *size)
{
        dict_t                     *xattr            = NULL;
        int                        i                 = 0;
        int                        ret               = 0;
        int                        op_ret            = 0;
        int                        source            = -1;
        gf_boolean_t               erase_dirty       = _gf_false;

        xattr = dict_new ();
        if (!xattr) {
                op_ret = -ENOMEM;
                goto out;
        }

        /* dirty xattr represents if the file needs heal. Unless all the
         * copies are healed, don't erase it */
        if (EC_COUNT (sources, ec->nodes) +
            EC_COUNT (healed_sinks, ec->nodes) == ec->nodes)
                erase_dirty = _gf_true;

        for (i = 0; i < ec->nodes; i++) {
                if (sources[i]) {
                        source = i;
                        break;
                }
        }

        for (i = 0; i < ec->nodes; i++) {
                if (healed_sinks[i]) {
                        ret = ec_data_undo_pending (frame, ec, fd, xattr,
                                                    versions, dirty, size,
                                                    source, erase_dirty, i);
                        if (ret < 0)
                                goto out;
                }

        }

        if (!erase_dirty)
                goto out;

        for (i = 0; i < ec->nodes; i++) {
                if (sources[i]) {
                        ret = ec_data_undo_pending (frame, ec, fd, xattr,
                                                    versions, dirty, size,
                                                    source, erase_dirty, i);
                        if (ret < 0)
                                continue;
                }

        }
out:
        if (xattr)
                dict_unref (xattr);
        return op_ret;
}

int
ec_restore_time_and_adjust_versions (call_frame_t *frame, ec_t *ec, fd_t *fd,
                                     unsigned char *sources,
                                     unsigned char *healed_sinks,
                                     uint64_t *versions, uint64_t *dirty,
                                     uint64_t *size)
{
        unsigned char      *locked_on           = NULL;
        unsigned char      *participants        = NULL;
        unsigned char      *output              = NULL;
        default_args_cbk_t *replies             = NULL;
        unsigned char      *postsh_sources      = NULL;
        unsigned char      *postsh_healed_sinks = NULL;
        unsigned char      *postsh_trim         = NULL;
        uint64_t           *postsh_versions     = NULL;
        uint64_t           *postsh_dirty        = NULL;
        uint64_t           *postsh_size         = NULL;
        int                ret                  = 0;
        int                i                    = 0;
        struct iatt        source_buf           = {0};
        loc_t              loc                  = {0};

        locked_on           = alloca0(ec->nodes);
        output              = alloca0(ec->nodes);
        participants        = alloca0(ec->nodes);
        postsh_sources      = alloca0(ec->nodes);
        postsh_healed_sinks = alloca0(ec->nodes);
        postsh_trim         = alloca0(ec->nodes);
        postsh_versions     = alloca0(ec->nodes * sizeof (*postsh_versions));
        postsh_dirty        = alloca0(ec->nodes * sizeof (*postsh_dirty));
        postsh_size         = alloca0(ec->nodes * sizeof (*postsh_size));

        for (i = 0; i < ec->nodes; i++) {
                if (healed_sinks[i] || sources[i])
                        participants[i] = 1;
        }

        EC_REPLIES_ALLOC (replies, ec->nodes);
        ret = cluster_inodelk (ec->xl_list, participants, ec->nodes, replies,
                               locked_on, frame, ec->xl, ec->xl->name,
                               fd->inode, 0, 0);
        {
                if (ret <= ec->fragments) {
                        gf_msg_debug (ec->xl->name, 0, "%s: Skipping heal "
                                "as only %d number of subvolumes could "
                                "be locked", uuid_utoa (fd->inode->gfid), ret);
                        ret = -ENOTCONN;
                        goto unlock;
                }

                ret = __ec_heal_data_prepare (frame, ec, fd, locked_on,
                                              postsh_versions, postsh_dirty,
                                              postsh_size, postsh_sources,
                                              postsh_healed_sinks, postsh_trim,
                                              &source_buf);
                if (ret < 0)
                        goto unlock;

                loc.inode = inode_ref (fd->inode);
                gf_uuid_copy (loc.gfid, fd->inode->gfid);
                ret = cluster_setattr (ec->xl_list, healed_sinks, ec->nodes,
                                       replies, output, frame, ec->xl, &loc,
                                       &source_buf,
                                       GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME,
                                       NULL);
                EC_INTERSECT (healed_sinks, healed_sinks, output, ec->nodes);
                if (EC_COUNT (healed_sinks, ec->nodes) == 0) {
                        ret = -ENOTCONN;
                        goto unlock;
                }
                ret = __ec_fd_data_adjust_versions (frame, ec, fd, sources,
                                           healed_sinks, versions, dirty, size);
        }
unlock:
        cluster_uninodelk (ec->xl_list, locked_on, ec->nodes, replies, output,
                           frame, ec->xl, ec->xl->name, fd->inode, 0, 0);
        cluster_replies_wipe (replies, ec->nodes);
        loc_wipe (&loc);
        return ret;
}

int
__ec_heal_data (call_frame_t *frame, ec_t *ec, fd_t *fd, unsigned char *heal_on,
                unsigned char *sources, unsigned char *healed_sinks)
{
        unsigned char      *locked_on    = NULL;
        unsigned char      *output       = NULL;
        uint64_t           *versions     = NULL;
        uint64_t           *dirty        = NULL;
        uint64_t           *size         = NULL;
        unsigned char      *trim         = NULL;
        default_args_cbk_t *replies      = NULL;
        int                ret           = 0;
        int                source        = 0;

        locked_on    = alloca0(ec->nodes);
        output       = alloca0(ec->nodes);
        trim         = alloca0 (ec->nodes);
        versions     = alloca0 (ec->nodes * sizeof (*versions));
        dirty        = alloca0 (ec->nodes * sizeof (*dirty));
        size         = alloca0 (ec->nodes * sizeof (*size));

        EC_REPLIES_ALLOC (replies, ec->nodes);
        ret = cluster_inodelk (ec->xl_list, heal_on, ec->nodes, replies,
                               locked_on, frame, ec->xl, ec->xl->name,
                               fd->inode, 0, 0);
        {
                if (ret <= ec->fragments) {
                        gf_msg_debug (ec->xl->name, 0, "%s: Skipping heal "
                                "as only %d number of subvolumes could "
                                "be locked", uuid_utoa (fd->inode->gfid), ret);
                        ret = -ENOTCONN;
                        goto unlock;
                }

                ret = __ec_heal_data_prepare (frame, ec, fd, locked_on,
                                              versions, dirty, size, sources,
                                              healed_sinks, trim, NULL);
                if (ret < 0)
                        goto unlock;

                if (EC_COUNT(healed_sinks, ec->nodes) == 0) {
                        ret = __ec_fd_data_adjust_versions (frame, ec, fd,
                                                            sources,
                                        healed_sinks, versions, dirty, size);
                        goto unlock;
                }

                source = ret;
                ret = __ec_heal_mark_sinks (frame, ec, fd, versions,
                                            healed_sinks);
                if (ret < 0)
                        goto unlock;

                ret = __ec_heal_trim_sinks (frame, ec, fd, healed_sinks, trim);
        }
unlock:
        cluster_uninodelk (ec->xl_list, locked_on, ec->nodes, replies, output,
                           frame, ec->xl, ec->xl->name, fd->inode, 0, 0);
        if (ret < 0)
                goto out;

        if (EC_COUNT(healed_sinks, ec->nodes) == 0)
                goto out;

        gf_msg_debug (ec->xl->name, 0, "%s: sources: %d, sinks: "
                "%d", uuid_utoa (fd->inode->gfid),
                EC_COUNT (sources, ec->nodes),
                EC_COUNT (healed_sinks, ec->nodes));

        ret = ec_rebuild_data (frame, ec, fd, size[source], sources,
                               healed_sinks);
        if (ret < 0)
                goto out;

        ret = ec_restore_time_and_adjust_versions (frame, ec, fd, sources,
                                                   healed_sinks, versions,
                                                   dirty, size);
out:
        cluster_replies_wipe (replies, ec->nodes);
        return ret;
}

int
ec_heal_data (call_frame_t *frame, ec_t *ec, gf_boolean_t block, inode_t *inode,
              unsigned char *sources, unsigned char *healed_sinks)
{
        unsigned char      *locked_on            = NULL;
        unsigned char      *up_subvols           = NULL;
        unsigned char      *output               = NULL;
        default_args_cbk_t *replies              = NULL;
        fd_t               *fd                   = NULL;
        loc_t               loc                  = {0};
        char               selfheal_domain[1024] = {0};
        int                ret                   = 0;

        EC_REPLIES_ALLOC (replies, ec->nodes);

        locked_on  = alloca0(ec->nodes);
        output     = alloca0(ec->nodes);
        up_subvols = alloca0(ec->nodes);
        loc.inode = inode_ref (inode);
        gf_uuid_copy (loc.gfid, inode->gfid);

        fd = fd_create (inode, 0);
        if (!fd) {
                ret = -ENOMEM;
                goto out;
        }

        ec_mask_to_char_array (ec->xl_up, up_subvols, ec->nodes);

        ret = cluster_open (ec->xl_list, up_subvols, ec->nodes, replies, output,
                            frame, ec->xl, &loc, O_RDWR|O_LARGEFILE, fd, NULL);
        if (ret <= ec->fragments) {
                ret = -ENOTCONN;
                goto out;
        }

        fd_bind (fd);
        sprintf (selfheal_domain, "%s:self-heal", ec->xl->name);
        /*If other processes are already doing the heal, don't block*/
        if (block) {
                ret = cluster_inodelk (ec->xl_list, output, ec->nodes, replies,
                                       locked_on, frame, ec->xl,
                                       selfheal_domain, inode, 0, 0);
        } else {
                ret = cluster_tryinodelk (ec->xl_list, output, ec->nodes,
                                          replies, locked_on, frame, ec->xl,
                                          selfheal_domain, inode, 0, 0);
        }
        {
                if (ret <= ec->fragments) {
                        gf_msg_debug (ec->xl->name, 0, "%s: Skipping heal "
                                "as only %d number of subvolumes could "
                                "be locked", uuid_utoa (inode->gfid), ret);
                        ret = -ENOTCONN;
                        goto unlock;
                }
                ret = __ec_heal_data (frame, ec, fd, locked_on, sources,
                                      healed_sinks);
        }
unlock:
        cluster_uninodelk (ec->xl_list, locked_on, ec->nodes, replies, output,
                           frame, ec->xl, selfheal_domain, inode, 0, 0);
out:
        if (fd)
                fd_unref (fd);
        loc_wipe (&loc);
        cluster_replies_wipe (replies, ec->nodes);
        return ret;
}

void
ec_heal_do (xlator_t *this, void *data, loc_t *loc, int32_t partial)
{
        call_frame_t  *frame         = NULL;
        unsigned char *participants  = NULL;
        unsigned char *msources      = NULL;
        unsigned char *mhealed_sinks = NULL;
        unsigned char *sources       = NULL;
        unsigned char *healed_sinks  = NULL;
        ec_t          *ec            = NULL;
        int           ret            = 0;
        int           op_ret         = 0;
        int           op_errno       = 0;
        intptr_t      mgood          = 0;
        intptr_t      mbad           = 0;
        intptr_t      good           = 0;
        intptr_t      bad            = 0;
        ec_fop_data_t *fop           = data;
        gf_boolean_t  blocking       = _gf_false;
        gf_boolean_t  need_heal      = _gf_false;
        unsigned char *up_subvols    = NULL;
	char up_bricks[32];

        ec = this->private;

        /* If it is heal request from getxattr, complete the heal and then
         * unwind, if it is ec_heal with NULL as frame then no need to block
         * the heal as the caller doesn't care about its completion*/
        if (fop->req_frame)
                blocking = _gf_true;

        frame = create_frame (this, this->ctx->pool);
        if (!frame)
                return;

        ec_owner_set(frame, frame->root);
        /*Do heal as root*/
        frame->root->uid = 0;
        frame->root->gid = 0;
        /*Mark the fops as internal*/
        frame->root->pid = GF_CLIENT_PID_SELF_HEALD;
        participants = alloca0(ec->nodes);
        ec_mask_to_char_array (ec->xl_up, participants, ec->nodes);

        up_subvols = alloca0(ec->nodes);
        ec_mask_to_char_array (ec->xl_up, up_subvols, ec->nodes);

        ec_heal_inspect (frame, ec, loc->inode, up_subvols,
                         &need_heal);
        if (!need_heal) {
                gf_msg (ec->xl->name, GF_LOG_DEBUG, 0,
                        EC_MSG_HEAL_FAIL, "Heal is not required for : %s ",
                        uuid_utoa(loc->gfid));
                goto out;
        }

        if (loc->name && strlen (loc->name)) {
                ret = ec_heal_name (frame, ec, loc->parent, (char *)loc->name,
                                    participants);
                if (ret == 0) {
                        gf_msg_debug (this->name, 0, "%s: name heal "
                                "successful on %lX", loc->path,
                                ec_char_array_to_mask (participants,
                                    ec->nodes));
                } else {
                        gf_msg_debug (this->name, 0, "%s: name heal "
                                "failed. ret = %d, subvolumes up = %s",
                                loc->path, ret,
                                ec_bin(up_bricks, sizeof(up_bricks), ec->xl_up,
                                ec->nodes));
                }
        }

        msources = alloca0(ec->nodes);
        mhealed_sinks = alloca0(ec->nodes);
        ret = ec_heal_metadata (frame, ec, loc->inode, msources, mhealed_sinks);
        if (ret == 0) {
                mgood = ec_char_array_to_mask (msources, ec->nodes);
                mbad  = ec_char_array_to_mask (mhealed_sinks, ec->nodes);
        } else {
                op_ret = -1;
                op_errno = -ret;
        }
        sources = alloca0(ec->nodes);
        healed_sinks = alloca0(ec->nodes);
        if (IA_ISREG (loc->inode->ia_type)) {
                ret = ec_heal_data (frame, ec, blocking, loc->inode, sources,
                                    healed_sinks);
        } else if (IA_ISDIR (loc->inode->ia_type) && !partial) {
                ret = ec_heal_entry (frame, ec, loc->inode, sources,
                                     healed_sinks);
        } else {
                ret = 0;
                memcpy (sources, participants, ec->nodes);
                memcpy (healed_sinks, participants, ec->nodes);
        }

        if (ret == 0) {
                good = ec_char_array_to_mask (sources, ec->nodes);
                bad  = ec_char_array_to_mask (healed_sinks, ec->nodes);
        } else {
                op_ret = -1;
                op_errno = -ret;
        }

out:
        if (fop->cbks.heal) {
                fop->cbks.heal (fop->req_frame, fop, fop->xl, op_ret,
                                op_errno, ec_char_array_to_mask (participants,
                                                                 ec->nodes),
                                mgood & good, mbad & bad, NULL);
        }
        STACK_DESTROY (frame->root);
        return;
}

int
ec_synctask_heal_wrap (void *opaque)
{
        ec_fop_data_t *fop = opaque;
        ec_heal_do (fop->xl, fop, &fop->loc[0], fop->int32);
        return 0;
}

int
ec_heal_done (int ret, call_frame_t *heal, void *opaque)
{
        if (opaque)
                ec_fop_data_release (opaque);
        return 0;
}

ec_fop_data_t*
__ec_dequeue_heals (ec_t *ec)
{
        ec_fop_data_t *fop = NULL;

        if (list_empty (&ec->heal_waiting))
                goto none;

        if ((ec->background_heals > 0) && (ec->healers >= ec->background_heals))
                goto none;

        fop = list_entry(ec->heal_waiting.next, ec_fop_data_t, healer);
        ec->heal_waiters--;
        list_del_init(&fop->healer);
        list_add(&fop->healer, &ec->healing);
        ec->healers++;
        return fop;
none:
        gf_msg_debug (ec->xl->name, 0, "Num healers: %d, Num Waiters: %d",
                      ec->healers, ec->heal_waiters);
        return NULL;
}

void
ec_heal_fail (ec_t *ec, ec_fop_data_t *fop)
{
        if (fop->cbks.heal) {
            fop->cbks.heal (fop->req_frame, NULL, ec->xl, -1, fop->error, 0, 0,
                            0, NULL);
        }
        if (fop)
            ec_fop_data_release (fop);
}

void
ec_launch_heal (ec_t *ec, ec_fop_data_t *fop)
{
        int     ret = 0;

        ret = synctask_new (ec->xl->ctx->env, ec_synctask_heal_wrap,
                            ec_heal_done, NULL, fop);
        if (ret < 0) {
                ec_fop_set_error(fop, ENOMEM);
                ec_heal_fail (ec, fop);
        }
}

void
ec_handle_healers_done (ec_fop_data_t *fop)
{
        ec_t *ec = fop->xl->private;
        ec_fop_data_t *heal_fop = NULL;

        if (list_empty (&fop->healer))
                return;

        LOCK (&ec->lock);
        {
                list_del_init (&fop->healer);
                ec->healers--;
                heal_fop = __ec_dequeue_heals (ec);
        }
        UNLOCK (&ec->lock);

        if (heal_fop)
                ec_launch_heal (ec, heal_fop);

}

void
ec_heal_throttle (xlator_t *this, ec_fop_data_t *fop)
{
        gf_boolean_t can_heal = _gf_true;
        ec_t         *ec      = this->private;

        if (fop->req_frame == NULL) {

                LOCK (&ec->lock);
                {
                        if ((ec->background_heals > 0) &&
                            (ec->heal_wait_qlen + ec->background_heals) >
                                             (ec->heal_waiters + ec->healers)) {
                                list_add_tail(&fop->healer, &ec->heal_waiting);
                                ec->heal_waiters++;
                                fop = __ec_dequeue_heals (ec);
                        } else {
                                can_heal = _gf_false;
                        }
                }
                UNLOCK (&ec->lock);
        }

        if (can_heal) {
                if (fop)
                        ec_launch_heal (ec, fop);
        } else {
                gf_msg_debug (this->name, 0, "Max number of heals are "
                              "pending, background self-heal rejected");
                ec_fop_set_error(fop, EBUSY);
                ec_heal_fail (ec, fop);
        }
}

void
ec_heal (call_frame_t *frame, xlator_t *this, uintptr_t target,
         int32_t minimum, fop_heal_cbk_t func, void *data, loc_t *loc,
         int32_t partial, dict_t *xdata)
{
    ec_cbk_t callback = { .heal = func };
    ec_fop_data_t *fop = NULL;
    int32_t err = EINVAL;

    gf_msg_trace ("ec", 0, "EC(HEAL) %p", frame);

    VALIDATE_OR_GOTO(this, fail);
    GF_VALIDATE_OR_GOTO(this->name, this->private, fail);

    if (!loc || !loc->inode || gf_uuid_is_null (loc->inode->gfid))
            goto fail;

    if (frame && frame->local)
            goto fail;
    fop = ec_fop_data_allocate (frame, this, EC_FOP_HEAL, 0, target, minimum,
                                NULL, NULL, callback, data);

    err = ENOMEM;

    if (fop == NULL)
        goto fail;

    fop->int32 = partial;

    if (loc) {
        if (loc_copy(&fop->loc[0], loc) != 0)
            goto fail;
    }

    if (xdata)
        fop->xdata = dict_ref(xdata);

    ec_heal_throttle (this, fop);

    return;

fail:
    if (fop)
            ec_fop_data_release (fop);
    if (func)
            func (frame, NULL, this, -1, err, 0, 0, 0, NULL);
}

int
ec_replace_heal_done (int ret, call_frame_t *heal, void *opaque)
{
        ec_t *ec = opaque;

        gf_msg_debug (ec->xl->name, 0,
                "getxattr on bricks is done ret %d", ret);
        return 0;
}

int32_t
ec_replace_heal (ec_t *ec, inode_t *inode)
{
        loc_t        loc = {0};
        int          ret = 0;

        loc.inode = inode_ref (inode);
        gf_uuid_copy (loc.gfid, inode->gfid);
        ret = syncop_getxattr (ec->xl, &loc, NULL, EC_XATTR_HEAL,
                               NULL, NULL);
        if (ret < 0)
                gf_msg_debug (ec->xl->name, 0,
                        "Heal failed for replace brick ret = %d", ret);

        loc_wipe (&loc);
        return ret;
}

int32_t
ec_replace_brick_heal_wrap (void  *opaque)
{
         ec_t *ec = opaque;
         inode_table_t   *itable = NULL;
         int32_t         ret     = -1;

         if (ec->xl->itable)
                 itable = ec->xl->itable;
         else
                 goto out;
         ret = ec_replace_heal (ec, itable->root);
out:
         return ret;
}

int32_t
ec_launch_replace_heal (ec_t *ec)
{
        int ret = -1;

        if (!ec)
                return ret;
        ret = synctask_new (ec->xl->ctx->env, ec_replace_brick_heal_wrap,
                            ec_replace_heal_done, NULL, ec);
        if (ret < 0) {
                gf_msg_debug (ec->xl->name, 0,
                        "Heal failed for replace brick ret = %d", ret);
        }
        return ret;
}

int32_t
ec_set_heal_info(dict_t **dict_rsp, char *status)
{
        dict_t *dict = NULL;
        int    ret   = 0;

        dict = dict_new ();
        if (!dict) {
                ret = -ENOMEM;
                goto out;
        }
        ret = dict_set_str (dict, "heal-info", status);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_WARNING, -ret,
                        EC_MSG_HEAL_FAIL,
                        "Failed to set heal-info key to "
                        "%s", status);
                dict_unref(dict);
                dict = NULL;
        }
        *dict_rsp = dict;
out:
        return ret;
}

int32_t
ec_need_heal (ec_t *ec, default_args_cbk_t *replies,
              gf_boolean_t *need_heal, int32_t lock_count)
{
        uint64_t           *dirty         = NULL;
        unsigned char      *sources       = NULL;
        unsigned char      *healed_sinks  = NULL;
        uint64_t           *data_versions = NULL;
        uint64_t           *meta_versions = NULL;
        uint64_t           *size          = NULL;
        int                ret            = 0;
        int                source_count   = 0;

        sources = alloca0(ec->nodes);
        healed_sinks = alloca0(ec->nodes);
        dirty = alloca0 (ec->nodes * sizeof (*dirty));
        size = alloca0 (ec->nodes * sizeof (*size));
        data_versions = alloca0 (ec->nodes * sizeof (*data_versions));
        meta_versions = alloca0 (ec->nodes * sizeof (*meta_versions));

        ret = ec_heal_data_find_direction (ec, replies, data_versions,
                                           meta_versions, dirty, size,
                                           sources, healed_sinks,
                                           EC_COMBINE_XDATA);
        if (ret < 0 && ret != -EIO) {
                goto out;
        }
        source_count = EC_COUNT (sources, ec->nodes);
        if (source_count == ec->nodes && lock_count > 0) {
                *need_heal = _gf_false;
        } else {
                *need_heal = _gf_true;
        }
        ret = source_count;
out:
        return ret;
}

int32_t
ec_heal_inspect (call_frame_t *frame, ec_t *ec,
                 inode_t *inode, unsigned char *locked_on,
                 gf_boolean_t *need_heal)
{
        loc_t              loc           = {0};
        int                i             = 0;
        int                ret           = 0;
        dict_t             *xdata        = NULL;
        uint64_t           zero_array[2] = {0};
        uint64_t           zero_value    = 0;
        unsigned char      *output       = NULL;
        default_args_cbk_t *replies      = NULL;
        int32_t            lock_count    = 0;

        EC_REPLIES_ALLOC (replies, ec->nodes);
        output = alloca0 (ec->nodes);

        loc.inode = inode_ref (inode);
        gf_uuid_copy (loc.gfid, inode->gfid);

        xdata = dict_new ();
        if (!xdata ||
            dict_set_str(xdata, GLUSTERFS_INODELK_DOM_COUNT,
                         ec->xl->name) ||
            dict_set_static_bin (xdata, EC_XATTR_VERSION, zero_array,
                                 sizeof (zero_array)) ||
            dict_set_static_bin (xdata, EC_XATTR_DIRTY, zero_array,
                                 sizeof (zero_array)) ||
            dict_set_static_bin (xdata, EC_XATTR_SIZE, &zero_value,
                                 sizeof (zero_value))) {
                ret = -ENOMEM;
                goto out;
        }
        ret = cluster_lookup (ec->xl_list, locked_on, ec->nodes, replies,
                              output, frame, ec->xl, &loc, xdata);

        if (ret != ec->nodes) {
                ret = ec->nodes;
                *need_heal = _gf_true;
                goto out;
        }

        for (i = 0; i < ec->nodes; i++) {
                if (!output[i] || !replies[i].xdata) {
                        continue;
                }
                if ((dict_get_int32 (replies[i].xdata, GLUSTERFS_INODELK_COUNT,
                                     &lock_count) == 0) && lock_count > 0) {
                        break;
                }
        }
        ret = ec_need_heal (ec, replies, need_heal, lock_count);

out:
        cluster_replies_wipe (replies, ec->nodes);
        loc_wipe (&loc);
        if (xdata) {
                dict_unref(xdata);
        }
        return ret;
}

int32_t
ec_heal_locked_inspect (call_frame_t *frame, ec_t *ec, inode_t *inode,
                        gf_boolean_t *need_heal)
{
        unsigned char      *locked_on  = NULL;
        unsigned char      *up_subvols = NULL;
        unsigned char      *output     = NULL;
        default_args_cbk_t *replies    = NULL;
        int                ret         = 0;

        EC_REPLIES_ALLOC (replies, ec->nodes);
        locked_on = alloca0(ec->nodes);
        output = alloca0(ec->nodes);
        up_subvols = alloca0(ec->nodes);
        ec_mask_to_char_array (ec->xl_up, up_subvols, ec->nodes);

        ret = cluster_inodelk (ec->xl_list, up_subvols, ec->nodes,
                               replies, locked_on, frame, ec->xl,
                               ec->xl->name, inode, 0, 0);
        if (ret != ec->nodes) {
                *need_heal = _gf_true;
                goto unlock;
        }
        ret = ec_heal_inspect (frame, ec, inode,
                               locked_on, need_heal);
unlock:
        cluster_uninodelk (ec->xl_list, locked_on, ec->nodes,
                           replies, output, frame, ec->xl,
                           ec->xl->name, inode, 0, 0);
        cluster_replies_wipe (replies, ec->nodes);
        return ret;
}

int32_t
ec_get_heal_info (xlator_t *this, loc_t *entry_loc, dict_t **dict_rsp)
{
        int             ret             = -ENOMEM;
        gf_boolean_t    need_heal       = _gf_false;
        call_frame_t    *frame          = NULL;
        ec_t            *ec             = NULL;
        unsigned char   *up_subvols     = NULL;
        loc_t           loc             = {0, };

        VALIDATE_OR_GOTO(this, out);
        GF_VALIDATE_OR_GOTO(this->name, entry_loc, out);

        ec = this->private;
        up_subvols = alloca0(ec->nodes);
        ec_mask_to_char_array (ec->xl_up, up_subvols, ec->nodes);

        frame = create_frame (this, this->ctx->pool);
        if (!frame) {
                goto out;
        }
        ec_owner_set(frame, frame->root);
        frame->root->uid = 0;
        frame->root->gid = 0;
        frame->root->pid = GF_CLIENT_PID_SELF_HEALD;

        if (loc_copy(&loc, entry_loc) != 0) {
                gf_msg (this->name, GF_LOG_ERROR,
                        ENOMEM, EC_MSG_LOC_COPY_FAIL,
                        "Failed to copy a location.");
                goto out;
        }
        if (!loc.inode) {
                ret = syncop_inode_find (this, this, loc.gfid,
                                         &loc.inode, NULL, NULL);
                if (ret < 0)
                        goto out;
        }

        ret = ec_heal_inspect (frame, ec, loc.inode, up_subvols,
                               &need_heal);
        if (ret == ec->nodes) {
                goto set_heal;
        }
        need_heal = _gf_false;
        ret = ec_heal_locked_inspect (frame, ec, loc.inode,
                                      &need_heal);
        if (ret < 0)
                goto out;
set_heal:
        if (need_heal) {
                ret =  ec_set_heal_info (dict_rsp, "heal");
        } else {
                ret =  ec_set_heal_info (dict_rsp, "no-heal");
        }
out:
        if (frame) {
                STACK_DESTROY (frame->root);
        }
        loc_wipe (&loc);
        return ret;
}
