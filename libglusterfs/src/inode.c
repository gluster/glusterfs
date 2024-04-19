/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "glusterfs/inode.h"
#include "glusterfs/statedump.h"
#include <pthread.h>
#include <sys/types.h>
#include <stdint.h>
#include "glusterfs/list.h"
#include <assert.h>
#include "glusterfs/libglusterfs-messages.h"

/* TODO:
   move latest accessed dentry to list_head of inode
*/

// clang-format off
/*

Details as per Xavi:

 I think we should have 3 lists: active, lru and invalidate.

We'll need 3 things: refs, nlookups and invalidate_sent flag. Any change of
refs, invalidate_sent flag and moving from one list to another must be done
atomically.

With this information, these are the states that cause a transition:

    refs nlookups inv_sent    op
      1      0        0      unref  -> refs = 0, active--->destroy
      1      1        0      unref  -> refs = 0, active--->lru
      1      1        0     forget  -> nlookups = 0, active--->active
     *0      1        0     forget  -> nlookups = 0, lru--->destroy
     *0      1        1     forget  -> nlookups = 0, invalidate--->destroy
      0      1        0       ref   -> refs = 1, lru--->active
      0      1        1       ref   -> refs = 1, inv_sent = 0, invalidate--->active
      0      1        0    overflow -> refs = 1, inv_sent = 1, lru--->invalidate
      1      1        1      unref  -> refs = 0, invalidate--->invalidate
      1      1        1     forget  -> nlookups = 0, inv_sent = 0, invalidate--->active

(*) technically these combinations cannot happen because a forget sent by the
kernel first calls ref() and then unref(). However it's equivalent.

overflow means that lru list has grown beyond the limit and the inode needs to
be invalidated. All other combinations do not cause a change in state or are not
possible.

Based on this, the code could be similar to this:

    ref(inode, inv)
    {
        if (refs == 0) {
            if (inv_sent) {
                invalidate_count--;
                inv_sent = 0;
            } else {
                lru_count--;
            }
            if (inv) {
                inv_sent = 1;
                invalidate_count++;
                list_move(inode, invalidate);
            } else {
                active_count++;
                list_move(inode, active);
            }
        }
        refs++;
    }

    unref(inode, clear)
    {
        if (clear && inv_sent) {
            // there is a case of fuse itself sending forget, without
            // invalidate, after entry delete, like unlink(), rmdir().
            inv_sent = 0;
            invalidate_count--;
            active_count++;
            list_move(inode, active);
        }
        refs--;
        if ((refs == 0) && !inv_sent) {
            active_count--;
            if (nlookups == 0) {
                destroy(inode);
            } else {
                lru_count++;
                list_move(inode, lru);
            }
        }
    }

    forget(inode)
    {
        ref(inode, false);
        nlookups--;
        unref(inode, true);
    }

    overflow(inode)
    {
        ref(inode, true);
        invalidator(inode);
        unref(inode, false);
    }

*/
// clang-format on

#define INODE_DUMP_LIST(head, key_buf, key_prefix, list_type)                  \
    {                                                                          \
        int i = 1;                                                             \
        inode_t *inode = NULL;                                                 \
        list_for_each_entry(inode, head, list)                                 \
        {                                                                      \
            gf_proc_dump_build_key(key_buf, key_prefix, "%s.%d", list_type,    \
                                   i++);                                       \
            gf_proc_dump_add_section("%s", key_buf);                           \
            inode_dump(inode, key);                                            \
        }                                                                      \
    }

static inode_t *
__inode_unref(inode_t *inode, bool clear);

static int
inode_table_prune(inode_table_t *table);

void
fd_dump(struct list_head *head, char *prefix);

/* Calculate index based on the inode_table root_level
   and xlator_id
*/
static int
inode_get_ctx_index(inode_table_t *table, xlator_t *xlator)
{
    int ctx_idx = xlator->level;

    if (ctx_idx > table->root_level) {
#ifdef DEBUG
        int32_t idx = xlator->xl_id - table->root_id;
        GF_ASSERT((idx > 0) && (idx < (table->ctxcount - table->root_level)));
#endif
        ctx_idx = (table->root_level + xlator->xl_id - table->root_id);
    }

    return ctx_idx;
}

static int
hash_dentry(inode_t *parent, const char *name, int mod)
{
    int hash = 0;
    int ret = 0;

    hash = *name;
    if (hash) {
        for (name += 1; *name != '\0'; name++) {
            hash = (hash << 5) - hash + *name;
        }
    }
    ret = (hash + (unsigned long)parent) % mod;

    return ret;
}

static int
hash_gfid(uuid_t uuid, int mod)
{
    return ((int *)uuid)[0] & (mod - 1);
}

static void
__dentry_hash(dentry_t *dentry, const int hash)
{
    inode_table_t *table = NULL;

    table = dentry->inode->table;

    list_del_init(&dentry->hash);
    list_add(&dentry->hash, &table->name_hash[hash]);
}

static int
__is_dentry_hashed(dentry_t *dentry)
{
    return !list_empty(&dentry->hash);
}

static void
dentry_destroy(dentry_t *dentry)
{
    if (dentry) {
        GF_FREE(dentry);
    }
}

static dentry_t *
__dentry_unset(dentry_t *dentry)
{
    if (!dentry)
        return NULL;

    list_del_init(&dentry->inode_list);
    list_del_init(&dentry->hash);

    if (dentry->parent) {
        GF_ATOMIC_DEC(dentry->parent->kids);
        __inode_unref(dentry->parent, false);
        dentry->parent = NULL;
    }

    return dentry;
}

static int
__foreach_ancestor_dentry(dentry_t *dentry,
                          int(per_dentry_fn)(dentry_t *dentry, inode_t *data),
                          inode_t *data)
{
    inode_t *parent = NULL;
    dentry_t *each = NULL;
    int ret = 0;

    if (!dentry) {
        gf_msg_callingfn(THIS->name, GF_LOG_WARNING, 0, LG_MSG_DENTRY_NOT_FOUND,
                         "dentry not found");
        return 0;
    }

    ret = per_dentry_fn(dentry, data);
    if (ret) {
        gf_smsg(THIS->name, GF_LOG_WARNING, 0, LG_MSG_PER_DENTRY_FAILED,
                "ret=%d", ret, NULL);
        goto out;
    }

    parent = dentry->parent;
    if (!parent) {
        gf_smsg(THIS->name, GF_LOG_WARNING, 0, LG_MSG_PARENT_DENTRY_NOT_FOUND,
                NULL);
        goto out;
    }

    list_for_each_entry(each, &parent->dentry_list, inode_list)
    {
        ret = __foreach_ancestor_dentry(each, per_dentry_fn, data);
        if (ret)
            goto out;
    }
out:
    return ret;
}

static int
__check_cycle(dentry_t *a_dentry, inode_t *data)
{
    inode_t *link_inode = NULL;

    link_inode = data;

    if (a_dentry->parent == link_inode)
        return 1;

    return 0;
}

static int
__is_dentry_cyclic(dentry_t *dentry)
{
    int ret = 0;

    ret = __foreach_ancestor_dentry(dentry, __check_cycle, dentry->inode);
    if (ret) {
        gf_smsg(dentry->inode->table->name, GF_LOG_CRITICAL, 0,
                LG_MSG_DENTRY_CYCLIC_LOOP, "gfid=%s name=-%s",
                uuid_utoa(dentry->inode->gfid), dentry->name, NULL);
    }

    return ret;
}

static int
__is_inode_hashed(inode_t *inode)
{
    return !list_empty(&inode->hash);
}

static void
__inode_hash(inode_t *inode, const int hash)
{
    inode_table_t *table = inode->table;

    list_del_init(&inode->hash);
    list_add(&inode->hash, &table->inode_hash[hash]);
}

static dentry_t *
__dentry_search_for_inode(inode_t *inode, uuid_t pargfid, const char *name)
{
    dentry_t *dentry = NULL;
    dentry_t *tmp = NULL;

    /* earlier, just the ino was sent, which could have been 0, now
       we deal with gfid, and if sent gfid is null or 0, no need to
       continue with the check */
    if (!pargfid || gf_uuid_is_null(pargfid))
        return NULL;

    list_for_each_entry(tmp, &inode->dentry_list, inode_list)
    {
        if ((gf_uuid_compare(tmp->parent->gfid, pargfid) == 0) &&
            !strcmp(tmp->name, name)) {
            dentry = tmp;
            break;
        }
    }

    return dentry;
}

static void
__inode_ctx_free(inode_t *inode)
{
    int index = 0;
    xlator_t *xl = NULL;
    xlator_t *old_THIS = THIS;

    for (index = 0; index < inode->table->ctxcount; index++) {
        if (inode->_ctx[index].value1 || inode->_ctx[index].value2) {
            xl = (xlator_t *)(long)inode->_ctx[index].xl_key;
            if (xl && !xl->call_cleanup && xl->cbks->forget) {
                THIS = xl;
                xl->cbks->forget(xl, inode);
            }
        }
    }

    THIS = old_THIS;
}

static void
__inode_destroy(inode_t *inode)
{
    inode_table_t *table = NULL;
    inode_t *ns_inode = inode->ns_inode;

    if (ns_inode) {
        table = ns_inode->table;
        pthread_mutex_lock(&table->lock);
        {
            __inode_unref(ns_inode, false);
        }
        pthread_mutex_unlock(&table->lock);
    }

    __inode_ctx_free(inode);
    LOCK_DESTROY(&inode->lock);
    //  memset (inode, 0xb, sizeof (*inode));
    GF_FREE(inode);
}

void
inode_ctx_merge(fd_t *fd, inode_t *inode, inode_t *linked_inode)
{
    int index = 0;
    xlator_t *xl = NULL;
    xlator_t *old_THIS = NULL;

    if (!fd || !inode || !linked_inode) {
        gf_msg_callingfn(THIS->name, GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "invalid inode");
        return;
    }

    for (; index < inode->table->ctxcount; index++) {
        if (inode->_ctx[index].xl_key) {
            xl = (xlator_t *)(long)inode->_ctx[index].xl_key;

            if (xl->cbks->ictxmerge) {
                if (!old_THIS)
                    old_THIS = THIS;
                THIS = xl;
                xl->cbks->ictxmerge(xl, fd, inode, linked_inode);
            }
        }
    }
    if (old_THIS)
        THIS = old_THIS;
}

static void
__inode_activate(inode_t *inode)
{
    list_move(&inode->list, &inode->table->active);
    inode->table->active_size++;
}

static void
__inode_passivate(inode_t *inode)
{
    dentry_t *dentry = NULL;
    dentry_t *t = NULL;

    GF_ASSERT(!inode->in_lru_list);
    list_move_tail(&inode->list, &inode->table->lru);
    inode->table->lru_size++;
    inode->in_lru_list = _gf_true;

    list_for_each_entry_safe(dentry, t, &inode->dentry_list, inode_list)
    {
        if (!__is_dentry_hashed(dentry))
            dentry_destroy(__dentry_unset(dentry));
    }
}

static void
__inode_retire(inode_t *inode)
{
    dentry_t *dentry = NULL;
    dentry_t *t = NULL;

    list_move_tail(&inode->list, &inode->table->purge);
    inode->table->purge_size++;

    list_del_init(&inode->hash);

    list_for_each_entry_safe(dentry, t, &inode->dentry_list, inode_list)
    {
        dentry_destroy(__dentry_unset(dentry));
    }
}

static int
__inode_get_xl_index(inode_t *inode, xlator_t *xlator)
{
    int set_idx = inode_get_ctx_index(inode->table, xlator);

    if (inode->_ctx[set_idx].xl_key == NULL)
        inode->_ctx[set_idx].xl_key = xlator;
    else if (inode->_ctx[set_idx].xl_key != xlator)
        return -1;

    return set_idx;
}

static inode_t *
__inode_unref(inode_t *inode, bool clear)
{
    int index = 0;
    xlator_t *this = NULL;
    uint64_t nlookup = 0;

    /*
     * Root inode should always be in active list of inode table. So unrefs
     * on root inode are no-ops.
     */
    if (__is_root_gfid(inode->gfid))
        return inode;

    /*
     * No need to acquire inode table's lock
     * as __inode_unref is called after acquiding
     * the inode table's lock.
     */
    if (inode->table->cleanup_started && !inode->ref)
        /*
         * There is a good chance that, the inode
         * on which unref came has already been
         * zero refed and added to the purge list.
         * This can happen when inode table is
         * being destroyed (glfs_fini is something
         * which destroys the inode table).
         *
         * Consider a directory 'a' which has a file
         * 'b'. Now as part of inode table destruction
         * zero refing of inodes does not happen from
         * leaf to the root. It happens in the order
         * inodes are present in the list. So, in this
         * example, the dentry of 'b' would have its
         * parent set to the inode of 'a'. So if
         * 'a' gets zero refed first (as part of
         * inode table cleanup) and then 'b' has to
         * zero refed, then dentry_unset is called on
         * the dentry of 'b' and it further goes on to
         * call inode_unref on b's parent which is 'a'.
         * In this situation, GF_ASSERT would be called
         * below as the refcount of 'a' has been already set
         * to zero.
         *
         * So return the inode if the inode table cleanup
         * has already started and inode refcount is 0.
         */
        return inode;

    this = THIS;

    if (clear && inode->in_invalidate_list) {
        inode->in_invalidate_list = false;
        inode->table->invalidate_size--;
        __inode_activate(inode);
    }
    GF_ASSERT(inode->ref);

    --inode->ref;

    index = __inode_get_xl_index(inode, this);
    if (index >= 0) {
        inode->_ctx[index].ref--;
    }

    if (!inode->ref && !inode->in_invalidate_list) {
        inode->table->active_size--;

        nlookup = GF_ATOMIC_GET(inode->nlookup);
        if (nlookup)
            __inode_passivate(inode);
        else
            __inode_retire(inode);
    }

    return inode;
}

static inode_t *
__inode_ref(inode_t *inode, bool is_invalidate)
{
    int index = 0;
    xlator_t *this = NULL;

    /*
     * Root inode should always be in active list of inode table. So unrefs
     * on root inode are no-ops. If we do not allow unrefs but allow refs,
     * it leads to refcount overflows and deleting and adding the inode
     * to active-list, which is ugly. active_size (check __inode_activate)
     * in inode table increases which is wrong. So just keep the ref
     * count as 1 always
     */
    if (inode->ref) {
        if (__is_root_gfid(inode->gfid))
            return inode;
    } else {
        if (inode->in_invalidate_list) {
            inode->in_invalidate_list = false;
            inode->table->invalidate_size--;
        } else {
            GF_ASSERT(inode->table->lru_size > 0);
            GF_ASSERT(inode->in_lru_list);
            inode->table->lru_size--;
            inode->in_lru_list = _gf_false;
        }
        if (is_invalidate) {
            inode->in_invalidate_list = true;
            inode->table->invalidate_size++;
            list_move_tail(&inode->list, &inode->table->invalidate);
        } else {
            __inode_activate(inode);
        }
    }

    this = THIS;

    inode->ref++;

    index = __inode_get_xl_index(inode, this);
    if (index >= 0)
        inode->_ctx[index].ref++;

    return inode;
}

inode_t *
inode_unref(inode_t *inode)
{
    inode_table_t *table = NULL;

    if (!inode)
        return NULL;

    table = inode->table;

    pthread_mutex_lock(&table->lock);
    {
        inode = __inode_unref(inode, false);
    }
    pthread_mutex_unlock(&table->lock);

    inode_table_prune(table);

    return inode;
}

inode_t *
inode_ref(inode_t *inode)
{
    inode_table_t *table = NULL;

    if (!inode)
        return NULL;

    table = inode->table;

    pthread_mutex_lock(&table->lock);
    {
        inode = __inode_ref(inode, false);
    }
    pthread_mutex_unlock(&table->lock);

    return inode;
}

static dentry_t *
dentry_create(inode_t *inode, inode_t *parent, const char *name)
{
    dentry_t *newd = NULL;
    int name_len = 0;

    name_len = strlen(name) + 1;

    newd = GF_MALLOC(sizeof(dentry_t) + name_len, gf_common_mt_dentry_ctx);
    if (caa_unlikely(newd == NULL)) {
        goto out;
    }

    INIT_LIST_HEAD(&newd->inode_list);
    INIT_LIST_HEAD(&newd->hash);

    newd->inode = inode;
    newd->parent = NULL;
    strcpy(newd->name, name);

out:
    return newd;
}

static inode_t *
inode_create(inode_table_t *table)
{
    inode_t *newi = NULL;

    newi = GF_CALLOC(
        1,
        sizeof(struct _inode) + (sizeof(struct _inode_ctx) * table->ctxcount),
        gf_common_mt_inode_ctx);
    if (caa_unlikely(!newi)) {
        goto out;
    }

    newi->table = table;

    LOCK_INIT(&newi->lock);

    GF_ATOMIC_INIT(newi->nlookup, 0);
    GF_ATOMIC_INIT(newi->kids, 0);

    INIT_LIST_HEAD(&newi->fd_list);
    INIT_LIST_HEAD(&newi->dentry_list);
    INIT_LIST_HEAD(&newi->hash);
    INIT_LIST_HEAD(&newi->list);

out:
    return newi;
}

inode_t *
inode_new(inode_table_t *table)
{
    inode_t *inode = NULL;

    if (!table) {
        gf_msg_callingfn(THIS->name, GF_LOG_WARNING, 0,
                         LG_MSG_INODE_TABLE_NOT_FOUND,
                         "inode not "
                         "found");
        return NULL;
    }

    inode = inode_create(table);
    if (inode) {
        pthread_mutex_lock(&table->lock);
        {
            list_add(&inode->list, &table->lru);
            table->lru_size++;
            GF_ASSERT(!inode->in_lru_list);
            inode->in_lru_list = _gf_true;
            __inode_ref(inode, false);
            /* let the dummy, 'unlinked' inodes have root as namespace */
            inode->ns_inode = __inode_ref(table->root, _gf_false);
        }
        pthread_mutex_unlock(&table->lock);
    }

    return inode;
}

/* Reduce the ref count by value 'nref'
 * Args:
 * inode - address of the inode to operate on
 * nref - number to subtracted from inode->ref
 *        if nref is 0, then the ref count is overwritten 0
 *
 * This function may cause the purging of the inode,
 * hence to be used only in destructor functions and not otherwise.
 */
static inode_t *
__inode_ref_reduce_by_n(inode_t *inode, uint64_t nref)
{
    uint64_t nlookup = 0;

    GF_ASSERT(inode->ref >= nref);

    inode->ref -= nref;

    if (!nref)
        inode->ref = 0;

    if (!inode->ref) {
        inode->table->active_size--;

        nlookup = GF_ATOMIC_GET(inode->nlookup);
        if (nlookup)
            __inode_passivate(inode);
        else
            __inode_retire(inode);
    }

    return inode;
}

static void
inode_forget_atomic(inode_t *inode, uint64_t nlookup)
{
    uint64_t inode_lookup = 0;

    if (nlookup == 0) {
        GF_ATOMIC_INIT(inode->nlookup, 0);
    } else {
        inode_lookup = GF_ATOMIC_FETCH_SUB(inode->nlookup, nlookup);
        /*
          GF_ASSERT(inode_lookup >= nlookup) assert may fail due to kernel bug
          more details can found on links:
          https://github.com/gluster/glusterfs/pull/4081
          https://github.com/gluster/glusterfs/issues/4074
        */
        if (inode_lookup < nlookup) {
            GF_ATOMIC_FETCH_ADD(inode->nlookup, nlookup - inode_lookup);
            gf_msg_callingfn(THIS->name, GF_LOG_CRITICAL, 0,
                             LG_MSG_ASSERTION_FAILED,
                             "GF_ASSERT(inode_lookup >= nlookup) may fail due "
                             "to kernel bug, reset inode->nlookup to 0");
        }
    }
}

dentry_t *
__dentry_grep(inode_table_t *table, inode_t *parent, const char *name,
              const int hash)
{
    dentry_t *dentry = NULL;
    dentry_t *tmp = NULL;

    list_for_each_entry(tmp, &table->name_hash[hash], hash)
    {
        if (tmp->parent == parent && !strcmp(tmp->name, name)) {
            dentry = tmp;
            break;
        }
    }

    return dentry;
}

inode_t *
inode_grep(inode_table_t *table, inode_t *parent, const char *name)
{
    inode_t *inode = NULL;
    dentry_t *dentry = NULL;

    if (!table || !parent || !name) {
        gf_msg_callingfn(THIS->name, GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "table || parent || name"
                         " not found");
        return NULL;
    }

    int hash = hash_dentry(parent, name, table->dentry_hashsize);

    pthread_mutex_lock(&table->lock);
    {
        dentry = __dentry_grep(table, parent, name, hash);
        if (dentry) {
            inode = dentry->inode;
            if (inode)
                __inode_ref(inode, false);
        }
    }
    pthread_mutex_unlock(&table->lock);

    return inode;
}

inode_t *
inode_resolve(inode_table_t *table, char *path)
{
    char *tmp = NULL, *bname = NULL, *str = NULL, *saveptr = NULL;
    inode_t *inode = NULL, *parent = NULL;

    if ((path == NULL) || (table == NULL)) {
        goto out;
    }

    parent = inode_ref(table->root);
    str = tmp = gf_strdup(path);
    if (str == NULL) {
        goto out;
    }

    while (1) {
        bname = strtok_r(str, "/", &saveptr);
        if (bname == NULL) {
            break;
        }

        if (inode != NULL) {
            inode_unref(inode);
        }

        inode = inode_grep(table, parent, bname);
        if (inode == NULL) {
            break;
        }

        if (parent != NULL) {
            inode_unref(parent);
        }

        parent = inode_ref(inode);
        str = NULL;
    }

    inode_unref(parent);
    GF_FREE(tmp);
out:
    return inode;
}

int
inode_grep_for_gfid(inode_table_t *table, inode_t *parent, const char *name,
                    uuid_t gfid, ia_type_t *type)
{
    inode_t *inode = NULL;
    dentry_t *dentry = NULL;
    int ret = -1;

    if (!table || !parent || !name) {
        gf_msg_callingfn(THIS->name, GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "table || parent || name"
                         " not found");
        return ret;
    }

    int hash = hash_dentry(parent, name, table->dentry_hashsize);

    pthread_mutex_lock(&table->lock);
    {
        dentry = __dentry_grep(table, parent, name, hash);
        if (dentry) {
            inode = dentry->inode;
            if (inode) {
                gf_uuid_copy(gfid, inode->gfid);
                *type = inode->ia_type;
                ret = 0;
            }
        }
    }
    pthread_mutex_unlock(&table->lock);

    return ret;
}

/* return 1 if gfid is of root, 0 if not */
gf_boolean_t
__is_root_gfid(uuid_t gfid)
{
    static uuid_t root = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};

    if (gf_uuid_compare(gfid, root) == 0)
        return _gf_true;

    return _gf_false;
}

static inode_t *
__inode_find(inode_table_t *table, uuid_t gfid, const int hash)
{
    inode_t *inode = NULL;
    inode_t *tmp = NULL;

    list_for_each_entry(tmp, &table->inode_hash[hash], hash)
    {
        if (gf_uuid_compare(tmp->gfid, gfid) == 0) {
            inode = tmp;
            break;
        }
    }

    return inode;
}

inode_t *
inode_find(inode_table_t *table, uuid_t gfid)
{
    inode_t *inode = NULL;

    if (!table) {
        gf_msg_callingfn(THIS->name, GF_LOG_WARNING, 0,
                         LG_MSG_INODE_TABLE_NOT_FOUND,
                         "table not "
                         "found");
        return NULL;
    }

    if (__is_root_gfid(gfid))
        return table->root;

    int hash = hash_gfid(gfid, table->inode_hashsize);

    pthread_mutex_lock(&table->lock);
    {
        inode = __inode_find(table, gfid, hash);
        if (inode)
            __inode_ref(inode, false);
    }
    pthread_mutex_unlock(&table->lock);

    return inode;
}

static inode_t *
__inode_link(inode_t *inode, inode_t *parent, const char *name,
             struct iatt *iatt, const int dhash)
{
    dentry_t *dentry = NULL;
    dentry_t *old_dentry = NULL;
    inode_t *old_inode = NULL;
    inode_table_t *table = NULL;
    inode_t *link_inode = NULL;
    char link_uuid_str[64] = {0}, parent_uuid_str[64] = {0};

    table = inode->table;

    if (parent) {
        /* We should prevent inode linking between different
           inode tables. This can cause errors which is very
           hard to catch/debug. */
        if (inode->table != parent->table) {
            errno = EINVAL;
            GF_ASSERT(!"link attempted b/w inodes of diff table");
        }

        if (parent->ia_type != IA_IFDIR) {
            errno = EINVAL;
            GF_ASSERT(!"link attempted on non-directory parent");
            return NULL;
        }

        if (!name || strlen(name) == 0) {
            errno = EINVAL;
            GF_ASSERT (!"link attempted with no basename on "
                                    "parent");
            return NULL;
        }
    }

    link_inode = inode;

    if (!__is_inode_hashed(inode)) {
        if (!iatt) {
            errno = EINVAL;
            return NULL;
        }

        if (gf_uuid_is_null(iatt->ia_gfid)) {
            errno = EINVAL;
            return NULL;
        }

        if (__is_root_gfid(iatt->ia_gfid))
            link_inode = table->root;
        else {
            int ihash = hash_gfid(iatt->ia_gfid, table->inode_hashsize);
            old_inode = __inode_find(table, iatt->ia_gfid, ihash);
            if (old_inode) {
                link_inode = old_inode;
            } else {
                gf_uuid_copy(inode->gfid, iatt->ia_gfid);
                inode->ia_type = iatt->ia_type;
                __inode_hash(inode, ihash);
            }
        }
    } else {
        /* @old_inode serves another important purpose - it indicates
           to the code further below whether a dentry cycle check is
           required or not (a new inode linkage can never result in
           creation of a loop.)

           if the given @inode is already hashed, it actually means
           it is an "old" inode and deserves to undergo the cyclic
           check.
        */
        old_inode = inode;
    }

    if (name && (!strcmp(name, ".") || !strcmp(name, ".."))) {
        return link_inode;
    }

    /* use only link_inode beyond this point */
    if (parent) {
        old_dentry = __dentry_grep(table, parent, name, dhash);

        if (!old_dentry || old_dentry->inode != link_inode) {
            dentry = dentry_create(link_inode, parent, name);
            if (!dentry) {
                gf_msg_callingfn(THIS->name, GF_LOG_ERROR, 0,
                                 LG_MSG_DENTRY_CREATE_FAILED,
                                 "dentry create failed on "
                                 "inode %s with parent %s",
                                 uuid_utoa_r(link_inode->gfid, link_uuid_str),
                                 uuid_utoa_r(parent->gfid, parent_uuid_str));
                errno = ENOMEM;
                return NULL;
            }

            /* dentry linking needs to happen inside lock */
            dentry->parent = __inode_ref(parent, false);
            GF_ATOMIC_INC(parent->kids);
            list_add(&dentry->inode_list, &link_inode->dentry_list);
            link_inode->ns_inode = __inode_ref(parent->ns_inode, false);
            if (old_inode && __is_dentry_cyclic(dentry)) {
                errno = ELOOP;
                dentry_destroy(__dentry_unset(dentry));
                return NULL;
            }
            __dentry_hash(dentry, dhash);

            if (old_dentry)
                dentry_destroy(__dentry_unset(old_dentry));
        }
    }

    return link_inode;
}

inode_t *
inode_link(inode_t *inode, inode_t *parent, const char *name, struct iatt *iatt)
{
    int hash = 0;
    inode_table_t *table = NULL;
    inode_t *linked_inode = NULL;

    if (!inode) {
        gf_msg_callingfn(THIS->name, GF_LOG_WARNING, 0, LG_MSG_INODE_NOT_FOUND,
                         "inode not found");
        return NULL;
    }

    table = inode->table;

    if (parent && name) {
        hash = hash_dentry(parent, name, table->dentry_hashsize);
    }

    if (name && strchr(name, '/')) {
        GF_ASSERT(!"inode link attempted with '/' in name");
        return NULL;
    }

    pthread_mutex_lock(&table->lock);
    {
        linked_inode = __inode_link(inode, parent, name, iatt, hash);
        if (linked_inode)
            __inode_ref(linked_inode, false);
    }
    pthread_mutex_unlock(&table->lock);

    inode_table_prune(table);

    return linked_inode;
}

int
inode_lookup(inode_t *inode)
{
    if (!inode) {
        gf_msg_callingfn(THIS->name, GF_LOG_WARNING, 0, LG_MSG_INODE_NOT_FOUND,
                         "inode not found");
        return -1;
    }

    GF_ATOMIC_INC(inode->nlookup);

    return 0;
}

int
inode_ref_reduce_by_n(inode_t *inode, uint64_t nref)
{
    inode_table_t *table = NULL;

    if (!inode) {
        gf_msg_callingfn(THIS->name, GF_LOG_WARNING, 0, LG_MSG_INODE_NOT_FOUND,
                         "inode not found");
        return -1;
    }

    table = inode->table;

    pthread_mutex_lock(&table->lock);
    {
        __inode_ref_reduce_by_n(inode, nref);
    }
    pthread_mutex_unlock(&table->lock);

    inode_table_prune(table);

    return 0;
}

int
inode_forget(inode_t *inode, uint64_t nlookup)
{
    inode_table_t *table = NULL;

    if (!inode) {
        gf_msg_callingfn(THIS->name, GF_LOG_WARNING, 0, LG_MSG_INODE_NOT_FOUND,
                         "inode not found");
        return -1;
    }

    table = inode->table;

    inode_forget_atomic(inode, nlookup);

    inode_table_prune(table);

    return 0;
}

int
inode_forget_with_unref(inode_t *inode, uint64_t nlookup)
{
    inode_table_t *table = NULL;

    if (!inode) {
        gf_msg_callingfn(THIS->name, GF_LOG_WARNING, 0, LG_MSG_INODE_NOT_FOUND,
                         "inode not found");
        return -1;
    }

    table = inode->table;

    pthread_mutex_lock(&table->lock);
    {
        inode_forget_atomic(inode, nlookup);
        __inode_unref(inode, true);
    }
    pthread_mutex_unlock(&table->lock);

    inode_table_prune(table);

    return 0;
}

/*
 * Invalidate an inode. This is invoked when a translator decides that an
 * inode's cache is no longer valid. Any translator interested in taking action
 * in this situation can define the invalidate callback.
 */
int
inode_invalidate(inode_t *inode)
{
    int ret = 0;
    xlator_t *xl = NULL;
    xlator_t *old_THIS = NULL;

    if (!inode) {
        gf_msg_callingfn(THIS->name, GF_LOG_WARNING, 0, LG_MSG_INODE_NOT_FOUND,
                         "inode not found");
        return -1;
    }

    /*
     * The root xlator is not in the graph but it can define an invalidate
     * handler.
     */
    xl = inode->table->xl->ctx->root;
    if (xl && xl->cbks->invalidate) {
        old_THIS = THIS;
        THIS = xl;
        ret = xl->cbks->invalidate(xl, inode);
        THIS = old_THIS;
        if (ret)
            return ret;
    }

    old_THIS = NULL;
    xl = inode->table->xl->graph->first;
    while (xl) {
        if (xl->cbks->invalidate) {
            if (!old_THIS)
                old_THIS = THIS;
            THIS = xl;
            ret = xl->cbks->invalidate(xl, inode);
        }

        if (ret)
            break;

        xl = xl->next;
    }

    if (old_THIS)
        THIS = old_THIS;

    return ret;
}

static dentry_t *
__inode_unlink(inode_t *inode, inode_t *parent, const char *name)
{
    dentry_t *dentry = NULL;

    dentry = __dentry_search_for_inode(inode, parent->gfid, name);

    /* dentry NULL for corrupted backend */
    if (dentry) {
        dentry = __dentry_unset(dentry);
    } else {
        char pgfid[64];
        gf_smsg("inode", GF_LOG_WARNING, 0, LG_MSG_DENTRY_NOT_FOUND,
                "parent-gfid=%s name=%s gfid%s",
                uuid_utoa_r(parent->gfid, pgfid), name, uuid_utoa(inode->gfid),
                NULL);
    }

    return dentry;
}

void
inode_unlink(inode_t *inode, inode_t *parent, const char *name)
{
    inode_table_t *table;
    dentry_t *dentry;

    if (!inode || !parent || !name)
        return;

    table = inode->table;

    pthread_mutex_lock(&table->lock);
    {
        dentry = __inode_unlink(inode, parent, name);
    }
    pthread_mutex_unlock(&table->lock);

    dentry_destroy(dentry);

    inode_table_prune(table);
}

int
inode_rename(inode_table_t *table, inode_t *srcdir, const char *srcname,
             inode_t *dstdir, const char *dstname, inode_t *inode,
             struct iatt *iatt)
{
    int hash = 0;
    dentry_t *dentry = NULL;
    inode_t *linked_inode = NULL;

    if (!inode) {
        gf_msg_callingfn(THIS->name, GF_LOG_WARNING, 0, LG_MSG_INODE_NOT_FOUND,
                         "inode not found");
        return -1;
    }

    table = inode->table;

    if (dstname && strchr(dstname, '/')) {
        GF_ASSERT(!"inode link attempted with '/' in name");
        return -1;
    }

    if (dstdir && dstname) {
        hash = hash_dentry(dstdir, dstname, table->dentry_hashsize);
    }

    pthread_mutex_lock(&table->lock);
    {
        linked_inode = __inode_link(inode, dstdir, dstname, iatt, hash);
        /* pick the old dentry */
        /* TODO: analyse properly about what should be the behavior if
         * 'linked_inode' is NULL. Adding this check makes the inode tree
         * robust, but is that good enough? (Ref: GH PR #1763) */
        if (linked_inode) {
            dentry = __inode_unlink(inode, srcdir, srcname);
        }
    }
    pthread_mutex_unlock(&table->lock);

    /* free the old dentry */
    dentry_destroy(dentry);

    inode_table_prune(table);

    return 0;
}

static dentry_t *
__dentry_search_arbit(inode_t *inode)
{
    dentry_t *dentry = NULL;
    dentry_t *trav = NULL;

    if (!inode)
        return NULL;

    list_for_each_entry(trav, &inode->dentry_list, inode_list)
    {
        if (__is_dentry_hashed(trav)) {
            dentry = trav;
            break;
        }
    }

    if (!dentry) {
        list_for_each_entry(trav, &inode->dentry_list, inode_list)
        {
            dentry = trav;
            break;
        }
    }

    return dentry;
}

inode_t *
inode_parent(inode_t *inode, uuid_t pargfid, const char *name)
{
    inode_t *parent = NULL;
    inode_table_t *table = NULL;
    dentry_t *dentry = NULL;
    gf_boolean_t search_for_inode = _gf_false;

    if (!inode) {
        gf_msg_callingfn(THIS->name, GF_LOG_WARNING, 0, LG_MSG_INODE_NOT_FOUND,
                         "inode not found");
        return NULL;
    }

    table = inode->table;

    if (pargfid && !gf_uuid_is_null(pargfid) && name)
        search_for_inode = _gf_true;

    pthread_mutex_lock(&table->lock);
    {
        if (search_for_inode) {
            dentry = __dentry_search_for_inode(inode, pargfid, name);
        } else {
            dentry = __dentry_search_arbit(inode);
        }

        if (dentry) {
            parent = dentry->parent;
            if (parent)
                __inode_ref(parent, false);
        }
    }
    pthread_mutex_unlock(&table->lock);

    return parent;
}

static int
__inode_has_dentry(inode_t *inode)
{
    return !list_empty(&inode->dentry_list);
}

int
inode_has_dentry(inode_t *inode)
{
    int dentry_present = 0;

    if (!inode) {
        gf_msg_callingfn(THIS->name, GF_LOG_WARNING, 0, LG_MSG_INODE_NOT_FOUND,
                         "inode not found");
        return 0;
    }

    LOCK(&inode->lock);
    {
        dentry_present = __inode_has_dentry(inode);
    }
    UNLOCK(&inode->lock);

    return dentry_present;
}

int
__inode_path(inode_t *inode, const char *name, char **bufp)
{
    inode_table_t *table = NULL;
    inode_t *itrav = NULL;
    dentry_t *trav = NULL;
    size_t i = 0, size = 0;
    int64_t ret = 0;
    int len = 0;
    char *buf = NULL;

    if (!inode || gf_uuid_is_null(inode->gfid)) {
        GF_ASSERT(0);
        gf_msg_callingfn(THIS->name, GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "invalid inode");
        return -EINVAL;
    }

    table = inode->table;

    itrav = inode;
    for (trav = __dentry_search_arbit(itrav); trav;
         trav = __dentry_search_arbit(itrav)) {
        itrav = trav->parent;
        i++; /* "/" */
        i += strlen(trav->name);
        if (i > PATH_MAX) {
            gf_smsg(table->name, GF_LOG_CRITICAL, 0, LG_MSG_DENTRY_CYCLIC_LOOP,
                    "name=%s", name, NULL);
            ret = -ENOENT;
            goto out;
        }
    }

    if (!__is_root_gfid(itrav->gfid)) {
        /* "<gfid:00000000-0000-0000-0000-000000000000>"/path */
        i += GFID_STR_PFX_LEN;
    }

    if (name) {
        i++;
        i += strlen(name);
    }

    ret = i;
    size = i + 1;
    buf = GF_CALLOC(size, sizeof(char), gf_common_mt_char);
    if (buf) {
        buf[size - 1] = 0;

        if (name) {
            len = strlen(name);
            memcpy(buf + (i - len), name, len);
            buf[i - len - 1] = '/';
            i -= (len + 1);
        }

        itrav = inode;
        for (trav = __dentry_search_arbit(itrav); trav;
             trav = __dentry_search_arbit(itrav)) {
            itrav = trav->parent;
            len = strlen(trav->name);
            memcpy(buf + (i - len), trav->name, len);
            buf[i - len - 1] = '/';
            i -= (len + 1);
        }

        if (!__is_root_gfid(itrav->gfid)) {
            snprintf(&buf[i - GFID_STR_PFX_LEN], GFID_STR_PFX_LEN,
                     INODE_PATH_FMT, uuid_utoa(itrav->gfid));
            buf[i - 1] = '>';
        }

        *bufp = buf;
    } else {
        ret = -ENOMEM;
    }

out:
    if (__is_root_gfid(inode->gfid) && !name) {
        ret = 1;
        GF_FREE(buf);
        buf = GF_CALLOC(ret + 1, sizeof(char), gf_common_mt_char);
        if (buf) {
            strcpy(buf, "/");
            *bufp = buf;
        } else {
            ret = -ENOMEM;
        }
    }

    if (ret < 0)
        *bufp = NULL;
    return ret;
}

int
inode_path(inode_t *inode, const char *name, char **bufp)
{
    inode_table_t *table = NULL;
    int ret = -1;

    if (!inode)
        return -EINVAL;

    table = inode->table;

    pthread_mutex_lock(&table->lock);
    {
        ret = __inode_path(inode, name, bufp);
    }
    pthread_mutex_unlock(&table->lock);

    return ret;
}

void
inode_table_set_lru_limit(inode_table_t *table, uint32_t lru_limit)
{
    pthread_mutex_lock(&table->lock);
    {
        table->lru_limit = lru_limit;
    }
    pthread_mutex_unlock(&table->lock);

    inode_table_prune(table);

    return;
}

static int
inode_table_prune(inode_table_t *table)
{
    int ret = 0;
    int ret1 = 0;
    struct list_head purge = {
        0,
    };
    inode_t *del = NULL;
    inode_t *tmp = NULL;
    inode_t *entry = NULL;
    uint64_t nlookup = 0;
    int64_t lru_size = 0;

    INIT_LIST_HEAD(&purge);

    pthread_mutex_lock(&table->lock);
    {
        if (!table->lru_limit)
            goto purge_list;

        lru_size = table->lru_size;
        while (lru_size > (table->lru_limit)) {
            if (list_empty(&table->lru)) {
                GF_ASSERT(0);
                gf_msg_callingfn(THIS->name, GF_LOG_WARNING, 0,
                                 LG_MSG_INVALID_INODE_LIST,
                                 "Empty inode lru list found"
                                 " but with (%d) lru_size",
                                 table->lru_size);
                break;
            }

            lru_size--;
            entry = list_entry(table->lru.next, inode_t, list);
            GF_ASSERT(entry->in_lru_list);
            /* The logic of invalidation is required only if invalidator_fn
               is present */
            if (table->invalidator_fn) {
                /* check for valid inode with 'nlookup' */
                nlookup = GF_ATOMIC_GET(entry->nlookup);
                if (nlookup) {
                    if (entry->invalidate_sent) {
                        list_move_tail(&entry->list, &table->lru);
                        continue;
                    }
                    __inode_ref(entry, true);
                    tmp = entry;
                    break;
                }
            }

            table->lru_size--;
            entry->in_lru_list = _gf_false;
            __inode_retire(entry);
            ret++;
        }

    purge_list:
        list_splice_init(&table->purge, &purge);
        table->purge_size = 0;
    }
    pthread_mutex_unlock(&table->lock);

    /* Pick 1 inode for invalidation */
    if (tmp) {
        xlator_t *old_THIS = THIS;
        THIS = table->invalidator_xl;
        ret1 = table->invalidator_fn(table->invalidator_xl, tmp);
        THIS = old_THIS;
        pthread_mutex_lock(&table->lock);
        {
            if (!ret1) {
                tmp->invalidate_sent = true;
                __inode_unref(tmp, false);
            } else {
                /* Move this back to the lru list*/
                __inode_unref(tmp, true);
            }
        }
        pthread_mutex_unlock(&table->lock);
    }

    /* Just so that if purge list is handled too, then clear it off */
    list_for_each_entry_safe(del, tmp, &purge, list)
    {
        list_del_init(&del->list);
        inode_forget_atomic(del, 0);
        __inode_destroy(del);
    }

    return ret;
}

static void
__inode_table_init_root(inode_table_t *table)
{
    inode_t *root = NULL;
    static uuid_t root_gfid = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};

    root = inode_create(table);

    list_add(&root->list, &table->lru);
    table->lru_size++;
    root->in_lru_list = _gf_true;

    gf_uuid_copy(root->gfid, root_gfid);
    root->ia_type = IA_IFDIR;
    __inode_hash(root, hash_gfid(root_gfid, table->inode_hashsize));

    root = __inode_ref(root, _gf_false);

    table->root = root;
    root->ns_inode = __inode_ref(root, _gf_false);
}

inode_table_t *
inode_table_with_invalidator(uint32_t lru_limit, xlator_t *xl,
                             int32_t (*invalidator_fn)(xlator_t *, inode_t *),
                             xlator_t *invalidator_xl, uint32_t dentry_hashsize,
                             uint32_t inode_hashsize)
{
    inode_table_t *new = NULL;
    uint32_t mem_pool_size = lru_limit;
    size_t diff;
    int ret = -1;
    int i = 0;

    new = (void *)GF_CALLOC(1, sizeof(*new), gf_common_mt_inode_table_t);
    if (!new)
        return NULL;

    new->xl = xl;

    /* root_id and root_level will be useful to access index of specific
       xlator
    */
    new->root_id = xl->xl_id;
    new->root_level = xl->level;

    /* The ctxcount value should be equal to the total xlators are associated
       with specific volume. The sum of xl->level and xl->child_count represents
       total xlators are associated with specific volume in a graph
    */
    new->ctxcount = (xl->level + xl->child_count + 1);

    new->lru_limit = lru_limit;
    new->invalidator_fn = invalidator_fn;
    new->invalidator_xl = invalidator_xl;

    if (dentry_hashsize == 0) {
        /* Prime number for uniform distribution */
        new->dentry_hashsize = 14057;
    } else {
        new->dentry_hashsize = dentry_hashsize;
    }

    /* The size of hash table MUST always be power of 2 as required by hash_gfid
     */
    if (inode_hashsize < 65536) {
        if (inode_hashsize > 0)
            gf_log(THIS->name, GF_LOG_WARNING,
                   "Set inode table size to minimum value of 65536 rather than "
                   "the configured %u",
                   inode_hashsize);
        new->inode_hashsize = 65536;
    } else {
        new->inode_hashsize = inode_hashsize;
        while ((diff = new->inode_hashsize & -new->inode_hashsize) !=
               new->inode_hashsize)
            new->inode_hashsize += diff;

        if (new->inode_hashsize != inode_hashsize)
            gf_log(THIS->name, GF_LOG_WARNING,
                   "Rounded inode table size up to %zu from %u",
                   new->inode_hashsize, inode_hashsize);
    }

    /* In case FUSE is initing the inode table. */
    if (!mem_pool_size || (mem_pool_size > DEFAULT_INODE_MEMPOOL_ENTRIES))
        mem_pool_size = DEFAULT_INODE_MEMPOOL_ENTRIES;

    new->inode_hash = (void *)GF_MALLOC(
        new->inode_hashsize * sizeof(struct list_head), gf_common_mt_list_head);
    if (!new->inode_hash)
        goto out;

    new->name_hash = (void *)GF_MALLOC(
        new->dentry_hashsize * sizeof(struct list_head),
        gf_common_mt_list_head);
    if (!new->name_hash)
        goto out;

    /* if number of fd open in one process is more than this,
       we may hit perf issues */
    new->fd_mem_pool = mem_pool_new(fd_t, 1024);

    if (!new->fd_mem_pool)
        goto out;

    for (i = 0; i < new->inode_hashsize; i++) {
        INIT_LIST_HEAD(&new->inode_hash[i]);
    }

    for (i = 0; i < new->dentry_hashsize; i++) {
        INIT_LIST_HEAD(&new->name_hash[i]);
    }

    INIT_LIST_HEAD(&new->active);
    INIT_LIST_HEAD(&new->lru);
    INIT_LIST_HEAD(&new->purge);
    INIT_LIST_HEAD(&new->invalidate);

    ret = gf_asprintf(&new->name, "%s/inode", xl->name);
    if (-1 == ret) {
        /* TODO: This should be ok to continue, check with avati */
        ;
    }

    new->cleanup_started = _gf_false;

    __inode_table_init_root(new);

    pthread_mutex_init(&new->lock, NULL);

    ret = 0;
out:
    if (ret) {
        if (new) {
            GF_FREE(new->inode_hash);
            GF_FREE(new->name_hash);
            GF_FREE(new);
            new = NULL;
        }
    }

    return new;
}

inode_table_t *
inode_table_new(uint32_t lru_limit, xlator_t *xl, uint32_t dentry_hashsize,
                uint32_t inode_hashsize)
{
    /* Only fuse for now requires the inode table with invalidator */
    return inode_table_with_invalidator(lru_limit, xl, NULL, NULL,
                                        dentry_hashsize, inode_hashsize);
}

void
inode_table_destroy_all(glusterfs_ctx_t *ctx)
{
    glusterfs_graph_t *trav_graph = NULL, *tmp = NULL;
    xlator_t *tree = NULL;
    inode_table_t *inode_table = NULL;

    if (ctx == NULL)
        goto out;

    /* TODO: Traverse ctx->graphs with in ctx->lock and also the other
     * graph additions and traversals in ctx->lock.
     */
    list_for_each_entry_safe(trav_graph, tmp, &ctx->graphs, list)
    {
        tree = trav_graph->first;
        inode_table = tree->itable;
        tree->itable = NULL;
        if (inode_table)
            inode_table_destroy(inode_table);
    }
out:
    return;
}

void
inode_table_destroy(inode_table_t *inode_table)
{
    inode_t *trav = NULL;

    if (inode_table == NULL)
        return;

    /* Ideally at this point in time, there should be no inodes with
     * refs remaining. But there are quite a few chances where the inodes
     * leak. So we can take three approaches for cleaning up the inode table:
     * 1. Assume there are no leaks and then send a forget on all the inodes
     *    in lru list.(If no leaks there should be no inodes in active list)
     * 2. Knowing there could be leaks and not freeing those inodes will
     *    also not free its inode context and this could leak a lot of
     *    memory, force free the inodes by changing the ref to 0.
     *    The problem with this is that any reference to inode after this
     *    calling this function will lead to a crash.
     * 3. Knowing there could be leakes, just free the inode contexts of
     *    all the inodes. and let the inodes be alive. This way the major
     *    memory consumed by the inode contexts are freed, but there can
     *    be errors when any inode contexts are accessed after destroying
     *    this table.
     *
     * Not sure which is the approach to be taken, going by approach 2.
     */

    pthread_mutex_lock(&inode_table->lock);
    {
        inode_table->cleanup_started = _gf_true;
        /* Process lru list first as we need to unset their dentry
         * entries (the ones which may not be unset during
         * '__inode_passivate' as they were hashed) which in turn
         * shall unref their parent
         *
         * These parent inodes when unref'ed may well again fall
         * into lru list and if we are at the end of traversing
         * the list, we may miss to delete/retire that entry. Hence
         * traverse the lru list till it gets empty.
         */
        while (!list_empty(&inode_table->lru)) {
            trav = list_first_entry(&inode_table->lru, inode_t, list);
            inode_forget_atomic(trav, 0);
            GF_ASSERT(inode_table->lru_size > 0);
            GF_ASSERT(trav->in_lru_list);
            __inode_retire(trav);
            inode_table->lru_size--;
            trav->in_lru_list = _gf_false;
        }

        /* Same logic for invalidate list */
        while (!list_empty(&inode_table->invalidate)) {
            trav = list_first_entry(&inode_table->invalidate, inode_t, list);
            inode_forget_atomic(trav, 0);
            __inode_retire(trav);
            inode_table->invalidate_size--;
        }

        while (!list_empty(&inode_table->active)) {
            trav = list_first_entry(&inode_table->active, inode_t, list);
            /* forget and unref the inode to retire and add it to
             * purge list. By this time there should not be any
             * inodes present in the active list except for root
             * inode. Its a ref_leak otherwise. */
            if (trav && (trav != inode_table->root))
                gf_msg_callingfn(THIS->name, GF_LOG_WARNING, 0,
                                 LG_MSG_REF_COUNT,
                                 "Active inode(%p) with refcount"
                                 "(%d) found during cleanup",
                                 trav, trav->ref);
            inode_forget_atomic(trav, 0);
            __inode_ref_reduce_by_n(trav, 0);
        }
    }
    pthread_mutex_unlock(&inode_table->lock);

    inode_table_prune(inode_table);

    GF_FREE(inode_table->inode_hash);
    GF_FREE(inode_table->name_hash);
    if (inode_table->fd_mem_pool)
        mem_pool_destroy(inode_table->fd_mem_pool);

    pthread_mutex_destroy(&inode_table->lock);

    GF_FREE(inode_table->name);
    GF_FREE(inode_table);

    return;
}

inode_t *
inode_from_path(inode_table_t *itable, const char *path)
{
    inode_t *inode = NULL;
#ifdef BUILD_GNFS
    inode_t *parent = NULL;
    inode_t *root = NULL;
    inode_t *curr = NULL;
    char *pathname = NULL;
    char *component = NULL, *next_component = NULL;
    char *strtokptr = NULL;

    if (!itable || !path)
        return NULL;

    /* top-down approach */
    pathname = gf_strdup(path);
    if (pathname == NULL) {
        goto out;
    }

    root = itable->root;
    parent = inode_ref(root);
    component = strtok_r(pathname, "/", &strtokptr);

    if (component == NULL)
        /* root inode */
        inode = inode_ref(parent);

    while (component) {
        curr = inode_grep(itable, parent, component);

        if (curr == NULL) {
            strtok_r(NULL, "/", &strtokptr);
            break;
        }

        next_component = strtok_r(NULL, "/", &strtokptr);

        if (next_component) {
            inode_unref(parent);
            parent = curr;
            curr = NULL;
        } else {
            inode = curr;
        }

        component = next_component;
    }

    if (parent)
        inode_unref(parent);

    GF_FREE(pathname);

out:
#endif
    return inode;
}

/* Function behaviour:
 * Function return true if inode_ctx is not present,
 * or value stored in inode_ctx is LOOKUP_NEEDED.
 * If inode_ctx value is LOOKUP_NOT_NEEDED, which means
 * inode_ctx is present for xlator this, but no lookup
 * needed.
 */
gf_boolean_t
inode_needs_lookup(inode_t *inode, xlator_t *this)
{
    uint64_t need_lookup = 0;
    gf_boolean_t ret = _gf_false;
    int op_ret = -1;

    if (!inode || !this)
        return ret;

    op_ret = inode_ctx_get(inode, this, &need_lookup);
    if (op_ret == -1) {
        ret = _gf_true;
    } else if (need_lookup == LOOKUP_NEEDED) {
        ret = _gf_true;
        need_lookup = LOOKUP_NOT_NEEDED;
        inode_ctx_set(inode, this, &need_lookup);
    }

    return ret;
}

int
__inode_ctx_set2(inode_t *inode, xlator_t *xlator, uint64_t *value1_p,
                 uint64_t *value2_p)
{
    int ret = 0;
    int set_idx = -1;

    if (!inode || !xlator)
        return -1;

    set_idx = __inode_get_xl_index(inode, xlator);
    if (set_idx < 0) {
        ret = -1;
        goto out;
    }

    if (value1_p)
        inode->_ctx[set_idx].value1 = *value1_p;
    if (value2_p)
        inode->_ctx[set_idx].value2 = *value2_p;
out:
    return ret;
}

int
__inode_ctx_set0(inode_t *inode, xlator_t *xlator, uint64_t *value1_p)
{
    return __inode_ctx_set2(inode, xlator, value1_p, NULL);
}

int
__inode_ctx_set1(inode_t *inode, xlator_t *xlator, uint64_t *value2_p)
{
    return __inode_ctx_set2(inode, xlator, NULL, value2_p);
}

int
inode_ctx_set2(inode_t *inode, xlator_t *xlator, uint64_t *value1_p,
               uint64_t *value2_p)
{
    int ret = 0;

    if (!inode || !xlator)
        return -1;

    LOCK(&inode->lock);
    {
        ret = __inode_ctx_set2(inode, xlator, value1_p, value2_p);
    }
    UNLOCK(&inode->lock);

    return ret;
}

int
inode_ctx_set1(inode_t *inode, xlator_t *xlator, uint64_t *value2_p)
{
    int ret = 0;

    if (!inode || !xlator)
        return -1;

    LOCK(&inode->lock);
    {
        ret = __inode_ctx_set1(inode, xlator, value2_p);
    }
    UNLOCK(&inode->lock);

    return ret;
}
int
inode_ctx_set0(inode_t *inode, xlator_t *xlator, uint64_t *value1_p)
{
    int ret = 0;

    if (!inode || !xlator)
        return -1;

    LOCK(&inode->lock);
    {
        ret = __inode_ctx_set0(inode, xlator, value1_p);
    }
    UNLOCK(&inode->lock);

    return ret;
}

int
__inode_ctx_get2(inode_t *inode, xlator_t *xlator, uint64_t *value1,
                 uint64_t *value2)
{
    int index = 0;
    int ret = -1;

    if (!inode || !xlator)
        goto out;

    index = inode_get_ctx_index(inode->table, xlator);

    if (inode->_ctx[index].xl_key != xlator)
        goto out;

    if (inode->_ctx[index].value1) {
        if (value1) {
            *value1 = inode->_ctx[index].value1;
            ret = 0;
        }
    }
    if (inode->_ctx[index].value2) {
        if (value2) {
            *value2 = inode->_ctx[index].value2;
            ret = 0;
        }
    }
out:
    return ret;
}

int
__inode_ctx_get0(inode_t *inode, xlator_t *xlator, uint64_t *value1)
{
    uint64_t tmp_value = 0;
    int ret = 0;

    ret = __inode_ctx_get2(inode, xlator, &tmp_value, NULL);
    if (!ret && value1)
        *value1 = tmp_value;

    return ret;
}

int
__inode_ctx_get1(inode_t *inode, xlator_t *xlator, uint64_t *value2)
{
    uint64_t tmp_value = 0;
    int ret = 0;

    ret = __inode_ctx_get2(inode, xlator, NULL, &tmp_value);
    if (!ret && value2)
        *value2 = tmp_value;

    return ret;
}

int
inode_ctx_get2(inode_t *inode, xlator_t *xlator, uint64_t *value1,
               uint64_t *value2)
{
    int ret = 0;

    if (!inode || !xlator)
        return -1;

    LOCK(&inode->lock);
    {
        ret = __inode_ctx_get2(inode, xlator, value1, value2);
    }
    UNLOCK(&inode->lock);

    return ret;
}

int
inode_ctx_get1(inode_t *inode, xlator_t *xlator, uint64_t *value2)
{
    int ret = 0;

    if (!inode || !xlator)
        return -1;

    LOCK(&inode->lock);
    {
        ret = __inode_ctx_get1(inode, xlator, value2);
    }
    UNLOCK(&inode->lock);

    return ret;
}

int
inode_ctx_get0(inode_t *inode, xlator_t *xlator, uint64_t *value1)
{
    int ret = 0;

    if (!inode || !xlator)
        return -1;

    LOCK(&inode->lock);
    {
        ret = __inode_ctx_get0(inode, xlator, value1);
    }
    UNLOCK(&inode->lock);

    return ret;
}

int
inode_ctx_del2(inode_t *inode, xlator_t *xlator, uint64_t *value1,
               uint64_t *value2)
{
    int index = 0;
    int ret = 0;

    if (!inode || !xlator)
        return -1;

    LOCK(&inode->lock);
    {
        index = inode_get_ctx_index(inode->table, xlator);

        if (inode->_ctx[index].xl_key != xlator) {
            ret = -1;
            goto unlock;
        }

        if (inode->_ctx[index].value1 && value1)
            *value1 = inode->_ctx[index].value1;
        if (inode->_ctx[index].value2 && value2)
            *value2 = inode->_ctx[index].value2;

        inode->_ctx[index].key = 0;
        inode->_ctx[index].xl_key = NULL;
        inode->_ctx[index].value1 = 0;
        inode->_ctx[index].value2 = 0;
    }
unlock:
    UNLOCK(&inode->lock);

    return ret;
}

/* function behavior:
 - if value1 is set, value1 in ctx is reset to 0 with current value passed
 back in value1 address.
 - if value2 is set, value2 in ctx is reset to 0 with current value passed
 back in value2 address.
 - if both are set, both fields are reset.
*/
static int
__inode_ctx_reset2(inode_t *inode, xlator_t *xlator, uint64_t *value1,
                   uint64_t *value2)
{
    int index = 0;
    int ret = 0;

    if (!inode || !xlator)
        return -1;

    LOCK(&inode->lock);
    {
        index = inode_get_ctx_index(inode->table, xlator);

        if (inode->_ctx[index].xl_key != xlator) {
            ret = -1;
            goto unlock;
        }

        if (inode->_ctx[index].value1 && value1) {
            *value1 = inode->_ctx[index].value1;
            inode->_ctx[index].value1 = 0;
        }
        if (inode->_ctx[index].value2 && value2) {
            *value2 = inode->_ctx[index].value2;
            inode->_ctx[index].value2 = 0;
        }
    }
unlock:
    UNLOCK(&inode->lock);

    return ret;
}

int
inode_ctx_reset2(inode_t *inode, xlator_t *xlator, uint64_t *value1_p,
                 uint64_t *value2_p)
{
    uint64_t tmp_value1 = 0;
    uint64_t tmp_value2 = 0;
    int ret = 0;

    ret = __inode_ctx_reset2(inode, xlator, &tmp_value1, &tmp_value2);
    if (!ret) {
        if (value1_p)
            *value1_p = tmp_value1;
        if (value2_p)
            *value2_p = tmp_value2;
    }
    return ret;
}

int
inode_ctx_reset1(inode_t *inode, xlator_t *xlator, uint64_t *value2_p)
{
    uint64_t tmp_value2 = 0;
    int ret = 0;

    ret = __inode_ctx_reset2(inode, xlator, NULL, &tmp_value2);

    if (!ret && value2_p)
        *value2_p = tmp_value2;

    return ret;
}
int
inode_ctx_reset0(inode_t *inode, xlator_t *xlator, uint64_t *value1_p)
{
    uint64_t tmp_value1 = 0;
    int ret = 0;

    ret = __inode_ctx_reset2(inode, xlator, &tmp_value1, NULL);

    if (!ret && value1_p)
        *value1_p = tmp_value1;

    return ret;
}

int
inode_is_linked(inode_t *inode)
{
    int ret = 0;
    inode_table_t *table = NULL;

    if (!inode) {
        gf_msg_callingfn(THIS->name, GF_LOG_WARNING, 0, LG_MSG_INODE_NOT_FOUND,
                         "inode not found");
        return 0;
    }

    table = inode->table;

    pthread_mutex_lock(&table->lock);
    {
        ret = __is_inode_hashed(inode);
    }
    pthread_mutex_unlock(&table->lock);

    return ret;
}

void
inode_dump(inode_t *inode, char *prefix)
{
    int ret = -1;
    xlator_t *xl = NULL;
    int i = 0;
    fd_t *fd = NULL;
    struct _inode_ctx *inode_ctx = NULL;
    struct list_head fd_list;
    int ref = 0;
    char key[GF_DUMP_MAX_BUF_LEN];
    uint64_t nlookup = 0;

    if (!inode)
        return;

    INIT_LIST_HEAD(&fd_list);

    ret = TRY_LOCK(&inode->lock);
    if (ret != 0) {
        return;
    }

    {
        nlookup = GF_ATOMIC_GET(inode->nlookup);
        gf_proc_dump_write("gfid", "%s", uuid_utoa(inode->gfid));
        gf_proc_dump_write("nlookup", "%" PRIu64, nlookup);
        gf_proc_dump_write("fd-count", "%u", inode->fd_count);
        gf_proc_dump_write("active-fd-count", "%u", inode->active_fd_count);
        gf_proc_dump_write("ref", "%u", inode->ref);
        gf_proc_dump_write("invalidate-sent", "%d", inode->invalidate_sent);
        gf_proc_dump_write("ia_type", "%d", inode->ia_type);
        gf_proc_dump_write("kids", "%" PRId64, GF_ATOMIC_GET(inode->kids));
        if (inode->ns_inode) {
            gf_proc_dump_write("namespace", "%s",
                               uuid_utoa(inode->ns_inode->gfid));
        }
        inode_ctx = GF_MALLOC(inode->table->ctxcount * sizeof(*inode_ctx),
                              gf_common_mt_inode_ctx);
        if (inode_ctx == NULL) {
            goto unlock;
        }

        for (i = 0; i < inode->table->ctxcount; i++) {
            inode_ctx[i] = inode->_ctx[i];
            xl = inode_ctx[i].xl_key;
            ref = inode_ctx[i].ref;
            if (ref != 0 && xl) {
                gf_proc_dump_build_key(key, "ref_by_xl:", "%s", xl->name);
                gf_proc_dump_write(key, "%d", ref);
            }
        }

        if (dump_options.xl_options.dump_fdctx != _gf_true)
            goto unlock;

        list_for_each_entry(fd, &inode->fd_list, inode_list)
        {
            fd_ctx_dump(fd, prefix);
        }
    }
unlock:
    UNLOCK(&inode->lock);

    if (inode_ctx && (dump_options.xl_options.dump_inodectx == _gf_true)) {
        for (i = 0; i < inode->table->ctxcount; i++) {
            if (inode_ctx[i].xl_key) {
                xl = (xlator_t *)(long)inode_ctx[i].xl_key;
                if (xl->dumpops && xl->dumpops->inodectx)
                    xl->dumpops->inodectx(xl, inode);
            }
        }
    }

    GF_FREE(inode_ctx);

    return;
}

static
size_t __itable_count_dentry_hash_entries(inode_table_t *itable)
{
    size_t ret = 0;
    int i;

    pthread_mutex_lock(&itable->lock);
    for (i = 0; i < itable->dentry_hashsize; ++i)
        ret += list_count(&itable->name_hash[i]);
    pthread_mutex_unlock(&itable->lock);

    return ret;
}

void
inode_table_dump(inode_table_t *itable, char *prefix)
{
    char key[GF_DUMP_MAX_BUF_LEN];
    int ret = 0;

    if (!itable)
        return;

    ret = pthread_mutex_trylock(&itable->lock);

    if (ret != 0) {
        return;
    }

    gf_proc_dump_build_key(key, prefix, "dentry_hashsize");
    gf_proc_dump_write(key, "%" GF_PRI_SIZET, itable->dentry_hashsize);
    gf_proc_dump_build_key(key, prefix, "dentry_hashentries");
    gf_proc_dump_write(key, "%" GF_PRI_SIZET, __itable_count_dentry_hash_entries(itable));
    gf_proc_dump_build_key(key, prefix, "inode_hashsize");
    gf_proc_dump_write(key, "%" GF_PRI_SIZET, itable->inode_hashsize);
    gf_proc_dump_build_key(key, prefix, "name");
    gf_proc_dump_write(key, "%s", itable->name);

    gf_proc_dump_build_key(key, prefix, "lru_limit");
    gf_proc_dump_write(key, "%d", itable->lru_limit);
    gf_proc_dump_build_key(key, prefix, "active_size");
    gf_proc_dump_write(key, "%d", itable->active_size);
    gf_proc_dump_build_key(key, prefix, "lru_size");
    gf_proc_dump_write(key, "%d", itable->lru_size);
    gf_proc_dump_build_key(key, prefix, "purge_size");
    gf_proc_dump_write(key, "%d", itable->purge_size);
    gf_proc_dump_build_key(key, prefix, "invalidate_size");
    gf_proc_dump_write(key, "%d", itable->invalidate_size);

    INODE_DUMP_LIST(&itable->active, key, prefix, "active");
    INODE_DUMP_LIST(&itable->lru, key, prefix, "lru");
    INODE_DUMP_LIST(&itable->purge, key, prefix, "purge");
    INODE_DUMP_LIST(&itable->invalidate, key, prefix, "invalidate");

    pthread_mutex_unlock(&itable->lock);
}

void
inode_dump_to_dict(inode_t *inode, char *prefix, dict_t *dict)
{
    int ret = -1;
    char key[GF_DUMP_MAX_BUF_LEN] = {
        0,
    };
    uint64_t nlookup = 0;

    ret = TRY_LOCK(&inode->lock);
    if (ret)
        return;

    snprintf(key, sizeof(key), "%s.gfid", prefix);
    ret = dict_set_dynstr(dict, key, gf_strdup(uuid_utoa(inode->gfid)));
    if (ret)
        goto out;

    snprintf(key, sizeof(key), "%s.nlookup", prefix);
    nlookup = GF_ATOMIC_GET(inode->nlookup);
    ret = dict_set_uint64(dict, key, nlookup);
    if (ret)
        goto out;

    snprintf(key, sizeof(key), "%s.ref", prefix);
    ret = dict_set_uint32(dict, key, inode->ref);
    if (ret)
        goto out;

    snprintf(key, sizeof(key), "%s.ia_type", prefix);
    ret = dict_set_int32(dict, key, inode->ia_type);
    if (ret)
        goto out;

out:
    UNLOCK(&inode->lock);
    return;
}

void
inode_table_dump_to_dict(inode_table_t *itable, char *prefix, dict_t *dict)
{
    char key[GF_DUMP_MAX_BUF_LEN] = {
        0,
    };
    int ret = 0;
#ifdef DEBUG
    inode_t *inode = NULL;
    int count = 0;
#endif
    ret = pthread_mutex_trylock(&itable->lock);
    if (ret)
        return;

    snprintf(key, sizeof(key), "%s.itable.lru_limit", prefix);
    ret = dict_set_uint32(dict, key, itable->lru_limit);
    if (ret)
        goto out;

    snprintf(key, sizeof(key), "%s.itable.active_size", prefix);
    ret = dict_set_uint32(dict, key, itable->active_size);
    if (ret)
        goto out;

    snprintf(key, sizeof(key), "%s.itable.lru_size", prefix);
    ret = dict_set_uint32(dict, key, itable->lru_size);
    if (ret)
        goto out;

    snprintf(key, sizeof(key), "%s.itable.purge_size", prefix);
    ret = dict_set_uint32(dict, key, itable->purge_size);
    if (ret)
        goto out;

#ifdef DEBUG
    /* Dumping inode details in dictionary and sending it to CLI is not
       required as when a developer (or support team) asks for this command
       output, they just want to get top level detail of inode table.
       If one wants to debug, let them take statedump and debug, this
       wouldn't be available in CLI during production setup.
    */
    list_for_each_entry(inode, &itable->active, list)
    {
        snprintf(key, sizeof(key), "%s.itable.active%d", prefix, count++);
        inode_dump_to_dict(inode, key, dict);
    }
    count = 0;

    list_for_each_entry(inode, &itable->lru, list)
    {
        snprintf(key, sizeof(key), "%s.itable.lru%d", prefix, count++);
        inode_dump_to_dict(inode, key, dict);
    }
    count = 0;

    list_for_each_entry(inode, &itable->purge, list)
    {
        snprintf(key, sizeof(key), "%s.itable.purge%d", prefix, count++);
        inode_dump_to_dict(inode, key, dict);
    }
#endif

out:
    pthread_mutex_unlock(&itable->lock);

    return;
}

/* *
 * This function finds name of the inode, if it has dentry. The dentry will be
 * created only if inode_link happens with valid parent and name. And this
 * function is only applicable for directories because multiple dentries are
 * not possible(no hardlinks)
 * */
void
inode_find_directory_name(inode_t *inode, const char **name)
{
    dentry_t *dentry = NULL;

    GF_VALIDATE_OR_GOTO("inode", inode, out);
    GF_VALIDATE_OR_GOTO("inode", name, out);

    if (!IA_ISDIR(inode->ia_type))
        return;

    pthread_mutex_lock(&inode->table->lock);
    {
        dentry = __dentry_search_arbit(inode);
        if (dentry) {
            *name = dentry->name;
        }
    }
    pthread_mutex_unlock(&inode->table->lock);
out:
    return;
}

/* *
 * This function sets a new namespace inode to a given inode.
 * */
void
inode_set_namespace_inode(inode_t *inode, inode_t *ns_inode)
{
    GF_VALIDATE_OR_GOTO("inode", inode, out);
    GF_VALIDATE_OR_GOTO("inode", ns_inode, out);

    /* namespace inode should always be a directory */
    if (!IA_ISDIR(ns_inode->ia_type)) {
        gf_log(THIS->name, GF_LOG_WARNING,
               "Trying to link namespace which is not a directory");
        return;
    }

    /* Ideally, if we do this, we should do this to complete tree */
    /* FIXME: fix above once we support setting quota after having data in
     * directories */
    /*
    if (GF_ATOMIC_GET(inode->kids)) {
        gf_log(THIS->name, GF_LOG_WARNING, "Trying to link inode with kids");
    }
    */

    inode_t *old_ns = inode->ns_inode;
    inode->ns_inode = inode_ref(ns_inode);
    inode_unref(old_ns);
out:
    return;
}
