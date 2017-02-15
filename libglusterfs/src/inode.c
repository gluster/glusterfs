/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "inode.h"
#include "fd.h"
#include "common-utils.h"
#include "statedump.h"
#include <pthread.h>
#include <sys/types.h>
#include <stdint.h>
#include "list.h"
#include <time.h>
#include <assert.h>
#include "libglusterfs-messages.h"

/* TODO:
   move latest accessed dentry to list_head of inode
*/

#define INODE_DUMP_LIST(head, key_buf, key_prefix, list_type)           \
        {                                                               \
                int i = 1;                                              \
                inode_t *inode = NULL;                                  \
                list_for_each_entry (inode, head, list) {               \
                        gf_proc_dump_build_key(key_buf, key_prefix,     \
                                               "%s.%d",list_type, i++); \
                        gf_proc_dump_add_section(key_buf);              \
                        inode_dump(inode, key);                         \
                }                                                       \
        }

static inode_t *
__inode_unref (inode_t *inode);

static int
inode_table_prune (inode_table_t *table);

void
fd_dump (struct list_head *head, char *prefix);

static int
hash_dentry (inode_t *parent, const char *name, int mod)
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
hash_gfid (uuid_t uuid, int mod)
{
        int ret = 0;

        ret = uuid[15] + (uuid[14] << 8);

        return ret;
}


static void
__dentry_hash (dentry_t *dentry)
{
        inode_table_t   *table = NULL;
        int              hash = 0;

        if (!dentry) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_DENTRY_NOT_FOUND, "dentry not found");
                return;
        }

        table = dentry->inode->table;
        hash = hash_dentry (dentry->parent, dentry->name,
                            table->hashsize);

        list_del_init (&dentry->hash);
        list_add (&dentry->hash, &table->name_hash[hash]);
}


static int
__is_dentry_hashed (dentry_t *dentry)
{
        if (!dentry) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_DENTRY_NOT_FOUND, "dentry not found");
                return 0;
        }

        return !list_empty (&dentry->hash);
}


static void
__dentry_unhash (dentry_t *dentry)
{
        if (!dentry) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_DENTRY_NOT_FOUND, "dentry not found");
                return;
        }

        list_del_init (&dentry->hash);
}


static void
__dentry_unset (dentry_t *dentry)
{
        if (!dentry) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_DENTRY_NOT_FOUND, "dentry not found");
                return;
        }

        __dentry_unhash (dentry);

        list_del_init (&dentry->inode_list);

        GF_FREE (dentry->name);
        dentry->name = NULL;

        if (dentry->parent) {
                __inode_unref (dentry->parent);
                dentry->parent = NULL;
        }

        mem_put (dentry);
}


static int
__foreach_ancestor_dentry (dentry_t *dentry,
                           int (per_dentry_fn) (dentry_t *dentry,
                                                void *data),
                           void *data)
{
        inode_t  *parent = NULL;
        dentry_t *each = NULL;
        int       ret = 0;

        if (!dentry) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_DENTRY_NOT_FOUND, "dentry not found");
                return 0;
        }

        ret = per_dentry_fn (dentry, data);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_WARNING, 0,
                        LG_MSG_PER_DENTRY_FAILED, "per dentry fn returned %d",
                        ret);
                goto out;
        }

        parent = dentry->parent;
        if (!parent) {
                gf_msg (THIS->name, GF_LOG_WARNING, 0,
                        LG_MSG_PARENT_DENTRY_NOT_FOUND,
                        "parent not found");
                goto out;
        }

        list_for_each_entry (each, &parent->dentry_list, inode_list) {
                ret = __foreach_ancestor_dentry (each, per_dentry_fn, data);
                if (ret)
                        goto out;
        }
out:
        return ret;
}


static int
__check_cycle (dentry_t *a_dentry, void *data)
{
        inode_t *link_inode = NULL;

        link_inode = data;

        if (a_dentry->parent == link_inode)
                return 1;

        return 0;
}


static int
__is_dentry_cyclic (dentry_t *dentry)
{
        int       ret = 0;
        inode_t  *inode = NULL;
        char     *name = "<nul>";

        ret = __foreach_ancestor_dentry (dentry, __check_cycle,
                                         dentry->inode);
        if (ret) {
                inode = dentry->inode;

                if (dentry->name)
                        name = dentry->name;

                gf_msg (dentry->inode->table->name, GF_LOG_CRITICAL, 0,
                        LG_MSG_DENTRY_CYCLIC_LOOP, "detected cyclic loop "
                        "formation during inode linkage. inode (%s) linking "
                        "under itself as %s", uuid_utoa (inode->gfid), name);
        }

        return ret;
}


static void
__inode_unhash (inode_t *inode)
{
        if (!inode) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_INODE_NOT_FOUND, "inode not found");
                return;
        }

        list_del_init (&inode->hash);
}


static int
__is_inode_hashed (inode_t *inode)
{
        if (!inode) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_INODE_NOT_FOUND, "inode not found");
                return 0;
        }

        return !list_empty (&inode->hash);
}


static void
__inode_hash (inode_t *inode)
{
        inode_table_t *table = NULL;
        int            hash = 0;

        if (!inode) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_INODE_NOT_FOUND, "inode not found");
                return;
        }

        table = inode->table;
        hash = hash_gfid (inode->gfid, 65536);

        list_del_init (&inode->hash);
        list_add (&inode->hash, &table->inode_hash[hash]);
}


static dentry_t *
__dentry_search_for_inode (inode_t *inode, uuid_t pargfid, const char *name)
{
        dentry_t *dentry = NULL;
        dentry_t *tmp = NULL;

        if (!inode || !name) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG,
                                  "inode || name not found");
                return NULL;
        }

        /* earlier, just the ino was sent, which could have been 0, now
           we deal with gfid, and if sent gfid is null or 0, no need to
           continue with the check */
        if (!pargfid || gf_uuid_is_null (pargfid))
                return NULL;

        list_for_each_entry (tmp, &inode->dentry_list, inode_list) {
                if ((gf_uuid_compare (tmp->parent->gfid, pargfid) == 0) &&
                    !strcmp (tmp->name, name)) {
                        dentry = tmp;
                        break;
                }
        }

        return dentry;
}


static void
__inode_ctx_free (inode_t *inode)
{
        int          index = 0;
        xlator_t    *xl = NULL;
        xlator_t    *old_THIS = NULL;

        if (!inode) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_INODE_NOT_FOUND, "inode not found");
                return;
        }

        if (!inode->_ctx) {
                gf_msg (THIS->name, GF_LOG_WARNING, 0, LG_MSG_CTX_NULL,
                        "_ctx not found");
                goto noctx;
        }

        for (index = 0; index < inode->table->xl->graph->xl_count; index++) {
                if (inode->_ctx[index].value1 || inode->_ctx[index].value2) {
                        xl = (xlator_t *)(long)inode->_ctx[index].xl_key;
                        old_THIS = THIS;
                        THIS = xl;
                        if (xl->cbks->forget)
                                xl->cbks->forget (xl, inode);
                        THIS = old_THIS;
                }
        }

        GF_FREE (inode->_ctx);
        inode->_ctx = NULL;

noctx:
        return;
}

static void
__inode_destroy (inode_t *inode)
{
        if (!inode) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_INODE_NOT_FOUND, "inode not found");
                return;
        }

        __inode_ctx_free (inode);

        LOCK_DESTROY (&inode->lock);
        //  memset (inode, 0xb, sizeof (*inode));
        mem_put (inode);
}

void
inode_ctx_merge (fd_t *fd, inode_t *inode, inode_t *linked_inode)
{
        int       index    = 0;
        xlator_t *xl       = NULL;
        xlator_t *old_THIS = NULL;

        if (!fd || !inode || !linked_inode) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "invalid inode");
                return;
        }

        if (!inode->_ctx || !linked_inode->_ctx) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG,
                                  "invalid inode context");
                return;
        }

        for (; index < inode->table->ctxcount; index++) {
                if (inode->_ctx[index].xl_key) {
                        xl = (xlator_t *)(long) inode->_ctx[index].xl_key;

                        old_THIS = THIS;
                        THIS = xl;
                        if (xl->cbks->ictxmerge)
                                xl->cbks->ictxmerge (xl, fd,
                                                     inode, linked_inode);
                        THIS = old_THIS;
                }
        }
}

static void
__inode_activate (inode_t *inode)
{
        if (!inode)
                return;

        list_move (&inode->list, &inode->table->active);
        inode->table->active_size++;
}


static void
__inode_passivate (inode_t *inode)
{
        dentry_t      *dentry = NULL;
        dentry_t      *t = NULL;

        if (!inode) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_INODE_NOT_FOUND, "inode not found");
                return;
        }

        list_move_tail (&inode->list, &inode->table->lru);
        inode->table->lru_size++;

        list_for_each_entry_safe (dentry, t, &inode->dentry_list, inode_list) {
                if (!__is_dentry_hashed (dentry))
                        __dentry_unset (dentry);
        }
}


static void
__inode_retire (inode_t *inode)
{
        dentry_t      *dentry = NULL;
        dentry_t      *t = NULL;

        if (!inode) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_INODE_NOT_FOUND, "inode not found");
                return;
        }

        list_move_tail (&inode->list, &inode->table->purge);
        inode->table->purge_size++;

        __inode_unhash (inode);

        list_for_each_entry_safe (dentry, t, &inode->dentry_list, inode_list) {
                __dentry_unset (dentry);
        }
}


static int
__inode_get_xl_index (inode_t *inode, xlator_t *xlator)
{
        int set_idx = -1;

        if ((inode->_ctx[xlator->xl_id].xl_key != NULL) &&
            (inode->_ctx[xlator->xl_id].xl_key != xlator))
                goto out;

        set_idx = xlator->xl_id;
        inode->_ctx[set_idx].xl_key = xlator;

out:
        return set_idx;
}


static inode_t *
__inode_unref (inode_t *inode)
{
        int       index = 0;
        xlator_t *this  = NULL;

        if (!inode)
                return NULL;

        this = THIS;

        /*
         * Root inode should always be in active list of inode table. So unrefs
         * on root inode are no-ops.
         */
        if (__is_root_gfid(inode->gfid))
                return inode;

        GF_ASSERT (inode->ref);

        --inode->ref;

        index = __inode_get_xl_index (inode, this);
        if (index >= 0) {
                inode->_ctx[index].xl_key = this;
                inode->_ctx[index].ref--;
        }

        if (!inode->ref) {
                inode->table->active_size--;

                if (inode->nlookup)
                        __inode_passivate (inode);
                else
                        __inode_retire (inode);
        }

        return inode;
}


static inode_t *
__inode_ref (inode_t *inode)
{
        int       index = 0;
        xlator_t *this  = NULL;

        if (!inode)
                return NULL;

        this = THIS;

        if (!inode->ref) {
                inode->table->lru_size--;
                __inode_activate (inode);
        }

        /*
         * Root inode should always be in active list of inode table. So unrefs
         * on root inode are no-ops. If we do not allow unrefs but allow refs,
         * it leads to refcount overflows and deleting and adding the inode
         * to active-list, which is ugly. active_size (check __inode_activate)
         * in inode table increases which is wrong. So just keep the ref
         * count as 1 always
         */
        if (__is_root_gfid(inode->gfid) && inode->ref)
                return inode;

        inode->ref++;

        index = __inode_get_xl_index (inode, this);
        if (index >= 0) {
                inode->_ctx[index].xl_key = this;
                inode->_ctx[index].ref++;
        }

        return inode;
}


inode_t *
inode_unref (inode_t *inode)
{
        inode_table_t *table = NULL;

        if (!inode)
                return NULL;

        table = inode->table;

        pthread_mutex_lock (&table->lock);
        {
                inode = __inode_unref (inode);
        }
        pthread_mutex_unlock (&table->lock);

        inode_table_prune (table);

        return inode;
}


inode_t *
inode_ref (inode_t *inode)
{
        inode_table_t *table = NULL;

        if (!inode)
                return NULL;

        table = inode->table;

        pthread_mutex_lock (&table->lock);
        {
                inode = __inode_ref (inode);
        }
        pthread_mutex_unlock (&table->lock);

        return inode;
}


static dentry_t *
__dentry_create (inode_t *inode, inode_t *parent, const char *name)
{
        dentry_t      *newd = NULL;

        if (!inode || !parent || !name) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG,
                                  "inode || parent || name not found");
                return NULL;
        }

        newd = mem_get0 (parent->table->dentry_pool);
        if (newd == NULL) {
                goto out;
        }

        INIT_LIST_HEAD (&newd->inode_list);
        INIT_LIST_HEAD (&newd->hash);

        newd->name = gf_strdup (name);
        if (newd->name == NULL) {
                mem_put (newd);
                newd = NULL;
                goto out;
        }

        if (parent)
                newd->parent = __inode_ref (parent);

        list_add (&newd->inode_list, &inode->dentry_list);
        newd->inode = inode;

out:
        return newd;
}


static inode_t *
__inode_create (inode_table_t *table)
{
        inode_t  *newi = NULL;

        if (!table) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_INODE_TABLE_NOT_FOUND, "table not "
                                  "found");
                return NULL;
        }

        newi = mem_get0 (table->inode_pool);
        if (!newi) {
                goto out;
        }

        newi->table = table;

        LOCK_INIT (&newi->lock);

        INIT_LIST_HEAD (&newi->fd_list);
        INIT_LIST_HEAD (&newi->list);
        INIT_LIST_HEAD (&newi->hash);
        INIT_LIST_HEAD (&newi->dentry_list);

        newi->_ctx = GF_CALLOC (1,
                                (sizeof (struct _inode_ctx) * table->ctxcount),
                                gf_common_mt_inode_ctx);
        if (newi->_ctx == NULL) {
                LOCK_DESTROY (&newi->lock);
                mem_put (newi);
                newi = NULL;
                goto out;
        }

        list_add (&newi->list, &table->lru);
        table->lru_size++;

out:

        return newi;
}


inode_t *
inode_new (inode_table_t *table)
{
        inode_t *inode = NULL;

        if (!table) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_INODE_TABLE_NOT_FOUND, "inode not "
                                  "found");
                return NULL;
        }

        pthread_mutex_lock (&table->lock);
        {
                inode = __inode_create (table);
                if (inode != NULL) {
                        __inode_ref (inode);
                }
        }
        pthread_mutex_unlock (&table->lock);

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
__inode_ref_reduce_by_n (inode_t *inode, uint64_t nref)
{
        if (!inode)
                return NULL;

        GF_ASSERT (inode->ref >= nref);

        inode->ref -= nref;

        if (!nref)
                inode->ref = 0;

        if (!inode->ref) {
                inode->table->active_size--;

                if (inode->nlookup)
                        __inode_passivate (inode);
                else
                        __inode_retire (inode);
        }

        return inode;
}


static inode_t *
__inode_lookup (inode_t *inode)
{
        if (!inode)
                return NULL;

        inode->nlookup++;

        return inode;
}


static inode_t *
__inode_forget (inode_t *inode, uint64_t nlookup)
{
        if (!inode)
                return NULL;

        GF_ASSERT (inode->nlookup >= nlookup);

        inode->nlookup -= nlookup;

        if (!nlookup)
                inode->nlookup = 0;

        return inode;
}


dentry_t *
__dentry_grep (inode_table_t *table, inode_t *parent, const char *name)
{
        int       hash = 0;
        dentry_t *dentry = NULL;
        dentry_t *tmp = NULL;

        if (!table || !name || !parent)
                return NULL;

        hash = hash_dentry (parent, name, table->hashsize);

        list_for_each_entry (tmp, &table->name_hash[hash], hash) {
                if (tmp->parent == parent && !strcmp (tmp->name, name)) {
                        dentry = tmp;
                        break;
                }
        }

        return dentry;
}


inode_t *
inode_grep (inode_table_t *table, inode_t *parent, const char *name)
{
        inode_t   *inode = NULL;
        dentry_t  *dentry = NULL;

        if (!table || !parent || !name) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "table || parent || name"
                                  " not found");
                return NULL;
        }

        pthread_mutex_lock (&table->lock);
        {
                dentry = __dentry_grep (table, parent, name);

                if (dentry)
                        inode = dentry->inode;

                if (inode)
                        __inode_ref (inode);
        }
        pthread_mutex_unlock (&table->lock);

        return inode;
}


inode_t *
inode_resolve (inode_table_t *table, char *path)
{
        char    *tmp   = NULL, *bname = NULL, *str = NULL, *saveptr = NULL;
        inode_t *inode = NULL, *parent = NULL;

        if ((path == NULL) || (table == NULL)) {
                goto out;
        }

        parent = inode_ref (table->root);
        str = tmp = gf_strdup (path);

        while (1) {
                bname = strtok_r (str, "/", &saveptr);
                if (bname == NULL) {
                        break;
                }

                if (inode != NULL) {
                        inode_unref (inode);
                }

                inode = inode_grep (table, parent, bname);
                if (inode == NULL) {
                        break;
                }

                if (parent != NULL) {
                        inode_unref (parent);
                }

                parent = inode_ref (inode);
                str = NULL;
        }

        inode_unref (parent);
        GF_FREE (tmp);
out:
        return inode;
}


int
inode_grep_for_gfid (inode_table_t *table, inode_t *parent, const char *name,
                     uuid_t gfid, ia_type_t *type)
{
        inode_t   *inode = NULL;
        dentry_t  *dentry = NULL;
        int        ret = -1;

        if (!table || !parent || !name) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "table || parent || name"
                                  " not found");
                return ret;
        }

        pthread_mutex_lock (&table->lock);
        {
                dentry = __dentry_grep (table, parent, name);

                if (dentry)
                        inode = dentry->inode;

                if (inode) {
                        gf_uuid_copy (gfid, inode->gfid);
                        *type = inode->ia_type;
                        ret = 0;
                }
        }
        pthread_mutex_unlock (&table->lock);

        return ret;
}


/* return 1 if gfid is of root, 0 if not */
gf_boolean_t
__is_root_gfid (uuid_t gfid)
{
        static uuid_t root = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};

        if (gf_uuid_compare (gfid, root) == 0)
                return _gf_true;

        return _gf_false;
}


inode_t *
__inode_find (inode_table_t *table, uuid_t gfid)
{
        inode_t   *inode = NULL;
        inode_t   *tmp = NULL;
        int        hash = 0;

        if (!table) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_INODE_TABLE_NOT_FOUND, "table not "
                                  "found");
                goto out;
        }

        if (__is_root_gfid (gfid))
                return table->root;

        hash = hash_gfid (gfid, 65536);

        list_for_each_entry (tmp, &table->inode_hash[hash], hash) {
                if (gf_uuid_compare (tmp->gfid, gfid) == 0) {
                        inode = tmp;
                        break;
                }
        }

out:
        return inode;
}


inode_t *
inode_find (inode_table_t *table, uuid_t gfid)
{
        inode_t   *inode = NULL;

        if (!table) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_INODE_TABLE_NOT_FOUND, "table not "
                                  "found");
                return NULL;
        }

        pthread_mutex_lock (&table->lock);
        {
                inode = __inode_find (table, gfid);
                if (inode)
                        __inode_ref (inode);
        }
        pthread_mutex_unlock (&table->lock);

        return inode;
}


static inode_t *
__inode_link (inode_t *inode, inode_t *parent, const char *name,
              struct iatt *iatt)
{
        dentry_t      *dentry = NULL;
        dentry_t      *old_dentry = NULL;
        inode_t       *old_inode = NULL;
        inode_table_t *table = NULL;
        inode_t       *link_inode = NULL;

        if (!inode) {
                errno = EINVAL;
                return NULL;
        }

        table = inode->table;
        if (!table) {
                errno = EINVAL;
                return NULL;
        }

        if (parent) {
                /* We should prevent inode linking between different
                   inode tables. This can cause errors which is very
                   hard to catch/debug. */
                if (inode->table != parent->table) {
                        errno = EINVAL;
                        GF_ASSERT (!"link attempted b/w inodes of diff table");
                }

                if (parent->ia_type != IA_IFDIR) {
                        errno = EINVAL;
                        GF_ASSERT (!"link attempted on non-directory parent");
                        return NULL;
                }

                if (!name || strlen (name) == 0) {
                        errno = EINVAL;
                        GF_ASSERT (!"link attempted with no basename on "
                                    "parent");
                        return NULL;
                }
        }

        link_inode = inode;

        if (!__is_inode_hashed (inode)) {
                if (!iatt) {
                        errno = EINVAL;
                        return NULL;
                }

                if (gf_uuid_is_null (iatt->ia_gfid)) {
                        errno = EINVAL;
                        return NULL;
                }

                old_inode = __inode_find (table, iatt->ia_gfid);

                if (old_inode) {
                        link_inode = old_inode;
                } else {
                        gf_uuid_copy (inode->gfid, iatt->ia_gfid);
                        inode->ia_type    = iatt->ia_type;
                        __inode_hash (inode);
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

        if (name) {
                if (!strcmp(name, ".") || !strcmp(name, ".."))
                        return link_inode;

                if (strchr (name, '/')) {
                        GF_ASSERT (!"inode link attempted with '/' in name");
                        return NULL;
                }
        }

        /* use only link_inode beyond this point */
        if (parent) {
                old_dentry = __dentry_grep (table, parent, name);

                if (!old_dentry || old_dentry->inode != link_inode) {
                        dentry = __dentry_create (link_inode, parent, name);
                        if (!dentry) {
                                gf_msg_callingfn (THIS->name, GF_LOG_ERROR, 0,
                                                  LG_MSG_DENTRY_CREATE_FAILED,
                                                  "dentry create failed on "
                                                  "inode %s with parent %s",
                                                  uuid_utoa (link_inode->gfid),
                                                  uuid_utoa (parent->gfid));
                                errno = ENOMEM;
                                return NULL;
                        }
                        if (old_inode && __is_dentry_cyclic (dentry)) {
                                errno = ELOOP;
                                __dentry_unset (dentry);
                                return NULL;
                        }
                        __dentry_hash (dentry);

                        if (old_dentry)
                                __dentry_unset (old_dentry);
                }
        }

        return link_inode;
}


inode_t *
inode_link (inode_t *inode, inode_t *parent, const char *name,
            struct iatt *iatt)
{
        inode_table_t *table = NULL;
        inode_t       *linked_inode = NULL;

        if (!inode) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_INODE_NOT_FOUND, "inode not found");
                return NULL;
        }

        table = inode->table;

        pthread_mutex_lock (&table->lock);
        {
                linked_inode = __inode_link (inode, parent, name, iatt);

                if (linked_inode)
                        __inode_ref (linked_inode);
        }
        pthread_mutex_unlock (&table->lock);

        inode_table_prune (table);

        return linked_inode;
}


int
inode_lookup (inode_t *inode)
{
        inode_table_t *table = NULL;

        if (!inode) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_INODE_NOT_FOUND, "inode not found");
                return -1;
        }

        table = inode->table;

        pthread_mutex_lock (&table->lock);
        {
                __inode_lookup (inode);
        }
        pthread_mutex_unlock (&table->lock);

        return 0;
}


int
inode_ref_reduce_by_n (inode_t *inode, uint64_t nref)
{
        inode_table_t *table = NULL;

        if (!inode) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_INODE_NOT_FOUND, "inode not found");
                return -1;
        }

        table = inode->table;

        pthread_mutex_lock (&table->lock);
        {
                __inode_ref_reduce_by_n (inode, nref);
        }
        pthread_mutex_unlock (&table->lock);

        inode_table_prune (table);

        return 0;
}


int
inode_forget (inode_t *inode, uint64_t nlookup)
{
        inode_table_t *table = NULL;

        if (!inode) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_INODE_NOT_FOUND, "inode not found");
                return -1;
        }

        table = inode->table;

        pthread_mutex_lock (&table->lock);
        {
                __inode_forget (inode, nlookup);
        }
        pthread_mutex_unlock (&table->lock);

        inode_table_prune (table);

        return 0;
}

/*
 * Invalidate an inode. This is invoked when a translator decides that an inode's
 * cache is no longer valid. Any translator interested in taking action in this
 * situation can define the invalidate callback.
 */
int
inode_invalidate(inode_t *inode)
{
	int ret = 0;
	xlator_t *xl = NULL;
	xlator_t *old_THIS = NULL;

	if (!inode) {
		gf_msg_callingfn(THIS->name, GF_LOG_WARNING, 0,
                                 LG_MSG_INODE_NOT_FOUND, "inode not found");
		return -1;
	}

	/*
	 * The master xlator is not in the graph but it can define an invalidate
	 * handler.
	 */
	xl = inode->table->xl->ctx->master;
	if (xl && xl->cbks->invalidate) {
		old_THIS = THIS;
		THIS = xl;
		ret = xl->cbks->invalidate(xl, inode);
		THIS = old_THIS;
		if (ret)
			return ret;
	}

	xl = inode->table->xl->graph->first;
	while (xl) {
		old_THIS = THIS;
		THIS = xl;
		if (xl->cbks->invalidate)
			ret = xl->cbks->invalidate(xl, inode);
		THIS = old_THIS;

		if (ret)
			break;

		xl = xl->next;
	}

	return ret;
}


static void
__inode_unlink (inode_t *inode, inode_t *parent, const char *name)
{
        dentry_t *dentry = NULL;
        char pgfid[64] = {0};
        char gfid[64] = {0};

        if (!inode || !parent || !name)
                return;

        dentry = __dentry_search_for_inode (inode, parent->gfid, name);

        /* dentry NULL for corrupted backend */
        if (dentry) {
                __dentry_unset (dentry);
        } else {
                gf_msg ("inode", GF_LOG_WARNING, 0, LG_MSG_DENTRY_NOT_FOUND,
                        "%s/%s: dentry not found in %s",
                        uuid_utoa_r (parent->gfid, pgfid), name,
                        uuid_utoa_r (inode->gfid, gfid));
        }

}


void
inode_unlink (inode_t *inode, inode_t *parent, const char *name)
{
        inode_table_t *table = NULL;

        if (!inode) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_INODE_NOT_FOUND, "inode not found");
                return;
        }

        table = inode->table;

        pthread_mutex_lock (&table->lock);
        {
                __inode_unlink (inode, parent, name);
        }
        pthread_mutex_unlock (&table->lock);

        inode_table_prune (table);
}


int
inode_rename (inode_table_t *table, inode_t *srcdir, const char *srcname,
              inode_t *dstdir, const char *dstname, inode_t *inode,
              struct iatt *iatt)
{
        if (!inode) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_INODE_NOT_FOUND, "inode not found");
                return -1;
        }

        table = inode->table;

        pthread_mutex_lock (&table->lock);
        {
                __inode_link (inode, dstdir, dstname, iatt);
                __inode_unlink (inode, srcdir, srcname);
        }
        pthread_mutex_unlock (&table->lock);

        inode_table_prune (table);

        return 0;
}


static dentry_t *
__dentry_search_arbit (inode_t *inode)
{
        dentry_t *dentry = NULL;
        dentry_t *trav = NULL;

        if (!inode)
                return NULL;

        list_for_each_entry (trav, &inode->dentry_list, inode_list) {
                if (__is_dentry_hashed (trav)) {
                        dentry = trav;
                        break;
                }
        }

        if (!dentry) {
                list_for_each_entry (trav, &inode->dentry_list, inode_list) {
                        dentry = trav;
                        break;
                }
        }

        return dentry;
}


inode_t *
inode_parent (inode_t *inode, uuid_t pargfid, const char *name)
{
        inode_t       *parent = NULL;
        inode_table_t *table = NULL;
        dentry_t      *dentry = NULL;

        if (!inode) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_INODE_NOT_FOUND, "inode not found");
                return NULL;
        }

        table = inode->table;

        pthread_mutex_lock (&table->lock);
        {
                if (pargfid && !gf_uuid_is_null (pargfid) && name) {
                        dentry = __dentry_search_for_inode (inode, pargfid, name);
                } else {
                        dentry = __dentry_search_arbit (inode);
                }

                if (dentry)
                        parent = dentry->parent;

                if (parent)
                        __inode_ref (parent);
        }
        pthread_mutex_unlock (&table->lock);

        return parent;
}

static int
__inode_has_dentry (inode_t *inode)
{
        if (!inode) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_INODE_NOT_FOUND, "inode not found");
                return 0;
        }

        return !list_empty (&inode->dentry_list);
}

int
inode_has_dentry (inode_t *inode)
{

        int dentry_present = 0;

        LOCK (&inode->lock);
        {
                dentry_present = __inode_has_dentry (inode);
        }
        UNLOCK (&inode->lock);

        return dentry_present;
}

int
__inode_path (inode_t *inode, const char *name, char **bufp)
{
        inode_table_t *table = NULL;
        inode_t       *itrav = NULL;
        dentry_t      *trav  = NULL;
        size_t         i     = 0, size = 0;
        int64_t        ret   = 0;
        int            len   = 0;
        char          *buf   = NULL;

        if (!inode || gf_uuid_is_null (inode->gfid)) {
                GF_ASSERT (0);
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "invalid inode");
                return -EINVAL;
        }

        table = inode->table;

        itrav = inode;
        for (trav = __dentry_search_arbit (itrav); trav;
             trav = __dentry_search_arbit (itrav)) {
                itrav = trav->parent;
                i ++; /* "/" */
                i += strlen (trav->name);
                if (i > PATH_MAX) {
                        gf_msg (table->name, GF_LOG_CRITICAL, 0,
                                LG_MSG_DENTRY_CYCLIC_LOOP, "possible infinite "
                                "loop detected, forcing break. name=(%s)",
                                name);
                        ret = -ENOENT;
                        goto out;
                }
        }

        if (!__is_root_gfid (itrav->gfid)) {
                /* "<gfid:00000000-0000-0000-0000-000000000000>"/path */
                i += GFID_STR_PFX_LEN;
        }

        if (name) {
                i++;
                i += strlen (name);
        }

        ret = i;
        size = i + 1;
        buf = GF_CALLOC (size, sizeof (char), gf_common_mt_char);
        if (buf) {

                buf[size - 1] = 0;

                if (name) {
                        len = strlen (name);
                        strncpy (buf + (i - len), name, len);
                        buf[i-len-1] = '/';
                        i -= (len + 1);
                }

                itrav = inode;
                for (trav = __dentry_search_arbit (itrav); trav;
                     trav = __dentry_search_arbit (itrav)) {
                        itrav = trav->parent;
                        len = strlen (trav->name);
                        strncpy (buf + (i - len), trav->name, len);
                        buf[i-len-1] = '/';
                        i -= (len + 1);
                }

                if (!__is_root_gfid (itrav->gfid)) {
                        snprintf (&buf[i-GFID_STR_PFX_LEN], GFID_STR_PFX_LEN,
                                  INODE_PATH_FMT, uuid_utoa (itrav->gfid));
                        buf[i-1] = '>';
                }

                *bufp = buf;
        } else {
                ret = -ENOMEM;
        }

out:
        if (__is_root_gfid (inode->gfid) && !name) {
                ret = 1;
                GF_FREE (buf);
                buf = GF_CALLOC (ret + 1, sizeof (char), gf_common_mt_char);
                if (buf) {
                        strcpy (buf, "/");
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
inode_path (inode_t *inode, const char *name, char **bufp)
{
        inode_table_t *table = NULL;
        int            ret   = -1;

        if (!inode)
                return -EINVAL;

        table = inode->table;

        pthread_mutex_lock (&table->lock);
        {
                ret = __inode_path (inode, name, bufp);
        }
        pthread_mutex_unlock (&table->lock);

        return ret;
}

void
__inode_table_set_lru_limit (inode_table_t *table, uint32_t lru_limit)
{
        table->lru_limit = lru_limit;
        return;
}


void
inode_table_set_lru_limit (inode_table_t *table, uint32_t lru_limit)
{
        pthread_mutex_lock (&table->lock);
        {
                __inode_table_set_lru_limit (table, lru_limit);
        }
        pthread_mutex_unlock (&table->lock);

        inode_table_prune (table);

        return;
}

static int
inode_table_prune (inode_table_t *table)
{
        int               ret = 0;
        struct list_head  purge = {0, };
        inode_t          *del = NULL;
        inode_t          *tmp = NULL;
        inode_t          *entry = NULL;

        if (!table)
                return -1;

        INIT_LIST_HEAD (&purge);

        pthread_mutex_lock (&table->lock);
        {
                while (table->lru_limit
                       && table->lru_size > (table->lru_limit)) {
                        if (list_empty (&table->lru)) {
                                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                                  LG_MSG_INVALID_INODE_LIST,
                                                  "Empty inode lru list found"
                                                  " but with (%d) lru_size",
                                                  table->lru_size);
                                break;
                        }

                        entry = list_entry (table->lru.next, inode_t, list);

                        table->lru_size--;
                        __inode_retire (entry);

                        ret++;
                }

                list_splice_init (&table->purge, &purge);
                table->purge_size = 0;
        }
        pthread_mutex_unlock (&table->lock);

        {
                list_for_each_entry_safe (del, tmp, &purge, list) {
                        list_del_init (&del->list);
                        __inode_forget (del, 0);
                        __inode_destroy (del);
                }
        }

        return ret;
}


static void
__inode_table_init_root (inode_table_t *table)
{
        inode_t     *root = NULL;
        struct iatt  iatt = {0, };

        if (!table)
                return;

        root = __inode_create (table);

        iatt.ia_gfid[15] = 1;
        iatt.ia_ino = 1;
        iatt.ia_type = IA_IFDIR;

        __inode_link (root, NULL, NULL, &iatt);
        table->root = root;
}


inode_table_t *
inode_table_new (size_t lru_limit, xlator_t *xl)
{
        inode_table_t *new = NULL;
        int            ret = -1;
        int            i = 0;

        new = (void *)GF_CALLOC(1, sizeof (*new), gf_common_mt_inode_table_t);
        if (!new)
                return NULL;

        new->xl = xl;
        new->ctxcount = xl->graph->xl_count + 1;

        new->lru_limit = lru_limit;

        new->hashsize = 14057; /* TODO: Random Number?? */

        /* In case FUSE is initing the inode table. */
        if (lru_limit == 0)
                lru_limit = DEFAULT_INODE_MEMPOOL_ENTRIES;

        new->inode_pool = mem_pool_new (inode_t, lru_limit);

        if (!new->inode_pool)
                goto out;

        new->dentry_pool = mem_pool_new (dentry_t, lru_limit);

        if (!new->dentry_pool)
                goto out;

        new->inode_hash = (void *)GF_CALLOC (65536,
                                             sizeof (struct list_head),
                                             gf_common_mt_list_head);
        if (!new->inode_hash)
                goto out;

        new->name_hash = (void *)GF_CALLOC (new->hashsize,
                                            sizeof (struct list_head),
                                            gf_common_mt_list_head);
        if (!new->name_hash)
                goto out;

        /* if number of fd open in one process is more than this,
           we may hit perf issues */
        new->fd_mem_pool = mem_pool_new (fd_t, 1024);

        if (!new->fd_mem_pool)
                goto out;

        for (i = 0; i < 65536; i++) {
                INIT_LIST_HEAD (&new->inode_hash[i]);
        }


        for (i = 0; i < new->hashsize; i++) {
                INIT_LIST_HEAD (&new->name_hash[i]);
        }

        INIT_LIST_HEAD (&new->active);
        INIT_LIST_HEAD (&new->lru);
        INIT_LIST_HEAD (&new->purge);

        ret = gf_asprintf (&new->name, "%s/inode", xl->name);
        if (-1 == ret) {
                /* TODO: This should be ok to continue, check with avati */
                ;
        }

        __inode_table_init_root (new);

        pthread_mutex_init (&new->lock, NULL);

        ret = 0;
out:
        if (ret) {
                if (new) {
                        GF_FREE (new->inode_hash);
                        GF_FREE (new->name_hash);
                        if (new->dentry_pool)
                                mem_pool_destroy (new->dentry_pool);
                        if (new->inode_pool)
                                mem_pool_destroy (new->inode_pool);
                        GF_FREE (new);
                        new = NULL;
                }
        }

        return new;
}

int
inode_table_ctx_free (inode_table_t *table)
{
        int ret = 0;
        inode_t *del = NULL;
        inode_t *tmp = NULL;
        int purge_count = 0;
        int lru_count = 0;
        int active_count = 0;
        xlator_t *this = NULL;
        int itable_size = 0;

        if (!table)
                return -1;

        this = THIS;

        pthread_mutex_lock (&table->lock);
        {
                list_for_each_entry_safe (del, tmp, &table->purge, list) {
                        if (del->_ctx) {
                                __inode_ctx_free (del);
                                purge_count++;
                        }
                }

                list_for_each_entry_safe (del, tmp, &table->lru, list) {
                        if (del->_ctx) {
                                __inode_ctx_free (del);
                                lru_count++;
                        }
                }

                /* should the contexts of active inodes be freed?
                 * Since before this function being called fds would have
                 * been migrated and would have held the ref on the new
                 * inode from the new inode table, the older inode would not
                 * be used.
                 */
                list_for_each_entry_safe (del, tmp, &table->active, list) {
                        if (del->_ctx) {
                                __inode_ctx_free (del);
                                active_count++;
                        }
                }
        }
        pthread_mutex_unlock (&table->lock);

        ret = purge_count + lru_count + active_count;
        itable_size = table->active_size + table->lru_size + table->purge_size;
        gf_msg_callingfn (this->name, GF_LOG_INFO, 0,
                          LG_MSG_INODE_CONTEXT_FREED, "total %d (itable size: "
                          "%d) inode contexts have been freed (active: %d, ("
                          "active size: %d), lru: %d, (lru size: %d),  purge: "
                          "%d, (purge size: %d))", ret, itable_size,
                          active_count, table->active_size, lru_count,
                          table->lru_size, purge_count, table->purge_size);
        return ret;
}

void
inode_table_destroy_all (glusterfs_ctx_t *ctx) {

        glusterfs_graph_t *trav_graph  = NULL, *tmp = NULL;
        xlator_t          *tree        = NULL;
        inode_table_t     *inode_table = NULL;

        if (ctx == NULL)
                goto out;

        /* TODO: Traverse ctx->graphs with in ctx->lock and also the other
         * graph additions and traversals in ctx->lock.
         */
        list_for_each_entry_safe (trav_graph, tmp, &ctx->graphs, list) {
                tree = trav_graph->first;
                inode_table = tree->itable;
                tree->itable = NULL;
                if (inode_table)
                        inode_table_destroy (inode_table);
        }
 out:
        return;
}

void
inode_table_destroy (inode_table_t *inode_table) {

        inode_t  *trav = NULL;

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
         *    calling this funtion will lead to a crash.
         * 3. Knowing there could be leakes, just free the inode contexts of
         *    all the inodes. and let the inodes be alive. This way the major
         *    memory consumed by the inode contexts are freed, but there can
         *    be errors when any inode contexts are accessed after destroying
         *    this table.
         *
         * Not sure which is the approach to be taken, going by approach 2.
         */

        /* Approach 3:
         * ret = inode_table_ctx_free (inode_table);
         */
        pthread_mutex_lock (&inode_table->lock);
        {
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
                while (!list_empty (&inode_table->lru)) {
                        trav = list_first_entry (&inode_table->lru,
                                                 inode_t, list);
                        __inode_forget (trav, 0);
                        __inode_retire (trav);
                        inode_table->lru_size--;
                }

                while (!list_empty (&inode_table->active)) {
                        trav = list_first_entry (&inode_table->active,
                                                 inode_t, list);
                        /* forget and unref the inode to retire and add it to
                         * purge list. By this time there should not be any
                         * inodes present in the active list except for root
                         * inode. Its a ref_leak otherwise. */
                        if (trav != inode_table->root)
                                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                                  LG_MSG_REF_COUNT,
                                                  "Active inode(%p) with refcount"
                                                  "(%d) found during cleanup",
                                                  trav, trav->ref);
                        __inode_forget (trav, 0);
                        __inode_ref_reduce_by_n (trav, 0);
                }

        }
        pthread_mutex_unlock (&inode_table->lock);

        inode_table_prune (inode_table);

        GF_FREE (inode_table->inode_hash);
        GF_FREE (inode_table->name_hash);
        if (inode_table->dentry_pool)
                mem_pool_destroy (inode_table->dentry_pool);
        if (inode_table->inode_pool)
                mem_pool_destroy (inode_table->inode_pool);
        if (inode_table->fd_mem_pool)
                mem_pool_destroy (inode_table->fd_mem_pool);

        pthread_mutex_destroy (&inode_table->lock);

        GF_FREE (inode_table->name);
        GF_FREE (inode_table);

        return;
}

inode_t *
inode_from_path (inode_table_t *itable, const char *path)
{
        inode_t  *inode = NULL;
        inode_t  *parent = NULL;
        inode_t  *root = NULL;
        inode_t  *curr = NULL;
        char     *pathname = NULL;
        char     *component = NULL, *next_component = NULL;
        char     *strtokptr = NULL;

        if (!itable || !path)
                return NULL;

        /* top-down approach */
        pathname = gf_strdup (path);
        if (pathname == NULL) {
                goto out;
        }

        root = itable->root;
        parent = inode_ref (root);
        component = strtok_r (pathname, "/", &strtokptr);

        if (component == NULL)
                /* root inode */
                inode = inode_ref (parent);

        while (component) {
                curr = inode_grep (itable, parent, component);

                if (curr == NULL) {
                        strtok_r (NULL, "/", &strtokptr);
                        break;
                }

                next_component = strtok_r (NULL, "/", &strtokptr);

                if (next_component) {
                        inode_unref (parent);
                        parent = curr;
                        curr = NULL;
                } else {
                        inode = curr;
                }

                component = next_component;
        }

        if (parent)
                inode_unref (parent);

        GF_FREE (pathname);

out:
        return inode;
}

void
inode_set_need_lookup (inode_t *inode, xlator_t *this)
{
        uint64_t  need_lookup = LOOKUP_NEEDED;

        if (!inode || !this)
                return;

        inode_ctx_set (inode, this, &need_lookup);

        return;
}

/* Function behaviour:
 * Function return true if inode_ctx is not present,
 * or value stored in inode_ctx is LOOKUP_NEEDED.
 * If inode_ctx value is LOOKUP_NOT_NEEDED, which means
 * inode_ctx is present for xlator this, but no lookup
 * needed.
 */
gf_boolean_t
inode_needs_lookup (inode_t *inode, xlator_t *this)
{
        uint64_t     need_lookup = 0;
        gf_boolean_t ret         = _gf_false;
        int          op_ret      = -1;

        if (!inode || !this)
                return ret;

        op_ret = inode_ctx_get (inode, this, &need_lookup);
        if (op_ret == -1) {
                ret = _gf_true;
        } else if (need_lookup == LOOKUP_NEEDED) {
                ret = _gf_true;
                need_lookup = LOOKUP_NOT_NEEDED;
                inode_ctx_set (inode, this, &need_lookup);
        }

        return ret;
}

int
__inode_ctx_set2 (inode_t *inode, xlator_t *xlator, uint64_t *value1_p,
                  uint64_t *value2_p)
{
        int ret = 0;
        int set_idx = -1;

        if (!inode || !xlator || !inode->_ctx)
                return -1;

        set_idx = __inode_get_xl_index (inode, xlator);
        if (set_idx == -1) {
                ret = -1;
                goto out;;
        }

        inode->_ctx[set_idx].xl_key = xlator;
        if (value1_p)
                inode->_ctx[set_idx].value1 = *value1_p;
        if (value2_p)
                inode->_ctx[set_idx].value2 = *value2_p;
out:
        return ret;
}

int
__inode_ctx_set0 (inode_t *inode, xlator_t *xlator, uint64_t *value1_p)
{
        return __inode_ctx_set2 (inode, xlator, value1_p, NULL);
}

int
__inode_ctx_set1 (inode_t *inode, xlator_t *xlator, uint64_t *value2_p)
{
        return __inode_ctx_set2 (inode, xlator, NULL, value2_p);
}


int
inode_ctx_set2 (inode_t *inode, xlator_t *xlator, uint64_t *value1_p,
                uint64_t *value2_p)
{
        int ret = 0;

        if (!inode || !xlator)
                return -1;

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_set2 (inode, xlator, value1_p, value2_p);
        }
        UNLOCK (&inode->lock);

        return ret;
}

int
inode_ctx_set1 (inode_t *inode, xlator_t *xlator, uint64_t *value2_p)
{
        int ret = 0;

        if (!inode || !xlator)
                return -1;

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_set1 (inode, xlator, value2_p);
        }
        UNLOCK (&inode->lock);

        return ret;
}
int
inode_ctx_set0 (inode_t *inode, xlator_t *xlator, uint64_t *value1_p)
{
        int ret = 0;

        if (!inode || !xlator)
                return -1;

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_set0 (inode, xlator, value1_p);
        }
        UNLOCK (&inode->lock);

        return ret;
}


int
__inode_ctx_get2 (inode_t *inode, xlator_t *xlator, uint64_t *value1,
                  uint64_t *value2)
{
        int index = 0;
        int ret = -1;

        if (!inode || !xlator || !inode->_ctx)
                goto out;

        index = xlator->xl_id;
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
__inode_ctx_get0 (inode_t *inode, xlator_t *xlator, uint64_t *value1)
{
        uint64_t tmp_value = 0;
        int ret = 0;

        ret =  __inode_ctx_get2 (inode, xlator, &tmp_value, NULL);
        if (!ret && value1)
                *value1 = tmp_value;

        return ret;
}

int
__inode_ctx_get1 (inode_t *inode, xlator_t *xlator, uint64_t *value2)
{
        uint64_t tmp_value = 0;
        int ret = 0;

        ret =  __inode_ctx_get2 (inode, xlator, NULL, &tmp_value);
        if (!ret && value2)
                *value2 = tmp_value;

        return ret;
}


int
inode_ctx_get2 (inode_t *inode, xlator_t *xlator, uint64_t *value1,
                uint64_t *value2)
{
        int ret = 0;

        if (!inode || !xlator)
                return -1;

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_get2 (inode, xlator, value1, value2);
        }
        UNLOCK (&inode->lock);

        return ret;
}

int
inode_ctx_get1 (inode_t *inode, xlator_t *xlator, uint64_t *value2)
{
        int ret = 0;

        if (!inode || !xlator)
                return -1;

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_get1 (inode, xlator, value2);
        }
        UNLOCK (&inode->lock);

        return ret;
}

int
inode_ctx_get0 (inode_t *inode, xlator_t *xlator, uint64_t *value1)
{
        int ret = 0;

        if (!inode || !xlator)
                return -1;

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_get0 (inode, xlator, value1);
        }
        UNLOCK (&inode->lock);

        return ret;
}


int
inode_ctx_del2 (inode_t *inode, xlator_t *xlator, uint64_t *value1,
                uint64_t *value2)
{
        int index = 0;
        int ret = 0;

        if (!inode || !xlator)
                return -1;

        LOCK (&inode->lock);
        {
                if (!inode->_ctx)
                        goto unlock;

                index = xlator->xl_id;
                if (inode->_ctx[index].xl_key != xlator) {
                        ret = -1;
                        goto unlock;
                }

                if (inode->_ctx[index].value1 && value1)
                        *value1 = inode->_ctx[index].value1;
                if (inode->_ctx[index].value2 && value2)
                        *value2 = inode->_ctx[index].value2;

                inode->_ctx[index].key    = 0;
                inode->_ctx[index].xl_key = NULL;
                inode->_ctx[index].value1 = 0;
                inode->_ctx[index].value2 = 0;
        }
unlock:
        UNLOCK (&inode->lock);

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
__inode_ctx_reset2 (inode_t *inode, xlator_t *xlator, uint64_t *value1,
                    uint64_t *value2)
{
        int index = 0;
        int ret = 0;

        if (!inode || !xlator)
                return -1;

        LOCK (&inode->lock);
        {
                index = xlator->xl_id;
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
        UNLOCK (&inode->lock);

        return ret;
}

int
inode_ctx_reset2 (inode_t *inode, xlator_t *xlator, uint64_t *value1_p,
                  uint64_t *value2_p)
{
        uint64_t tmp_value1 = 0;
        uint64_t tmp_value2 = 0;
        int ret = 0;

        ret =  __inode_ctx_reset2 (inode, xlator, &tmp_value1, &tmp_value2);
        if (!ret) {
                if (value1_p)
                        *value1_p = tmp_value1;
                if (value2_p)
                        *value2_p = tmp_value2;
        }
        return ret;
}

int
inode_ctx_reset1 (inode_t *inode, xlator_t *xlator, uint64_t *value2_p)
{
        uint64_t tmp_value2 = 0;
        int ret = 0;

        ret = __inode_ctx_reset2 (inode, xlator, NULL, &tmp_value2);

        if (!ret && value2_p)
                *value2_p = tmp_value2;

        return ret;

}
int
inode_ctx_reset0 (inode_t *inode, xlator_t *xlator, uint64_t *value1_p)
{
        uint64_t tmp_value1 = 0;
        int ret = 0;

        ret = __inode_ctx_reset2 (inode, xlator, &tmp_value1, NULL);

        if (!ret && value1_p)
                *value1_p = tmp_value1;

        return ret;
}

int
inode_is_linked (inode_t *inode)
{
        int            ret   = 0;
        inode_table_t *table = NULL;

        if (!inode) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_INODE_NOT_FOUND, "inode not found");
                return 0;
        }

        table = inode->table;

        pthread_mutex_lock (&table->lock);
        {
                ret = __is_inode_hashed (inode);
        }
        pthread_mutex_unlock (&table->lock);

        return ret;
}

void
inode_dump (inode_t *inode, char *prefix)
{
        int                ret       = -1;
        xlator_t          *xl        = NULL;
        int                i         = 0;
        fd_t              *fd        = NULL;
        struct _inode_ctx *inode_ctx = NULL;
        struct list_head   fd_list;
        int                ref       = 0;
        char               key[GF_DUMP_MAX_BUF_LEN];

        if (!inode)
                return;

        memset(key, 0, sizeof(key));
        INIT_LIST_HEAD (&fd_list);

        ret = TRY_LOCK(&inode->lock);
        if (ret != 0) {
                return;
        }

        {
                gf_proc_dump_write("gfid", "%s", uuid_utoa (inode->gfid));
                gf_proc_dump_write("nlookup", "%ld", inode->nlookup);
                gf_proc_dump_write("fd-count", "%u", inode->fd_count);
                gf_proc_dump_write("ref", "%u", inode->ref);
                gf_proc_dump_write("ia_type", "%d", inode->ia_type);
                if (inode->_ctx) {
                        inode_ctx = GF_CALLOC (inode->table->ctxcount,
                                               sizeof (*inode_ctx),
                                               gf_common_mt_inode_ctx);
                        if (inode_ctx == NULL) {
                                goto unlock;
                        }

                        for (i = 0; i < inode->table->ctxcount;
                             i++) {
                                inode_ctx[i] = inode->_ctx[i];
                                xl = inode_ctx[i].xl_key;
                                ref = inode_ctx[i].ref;
                                if (ref != 0 && xl) {
                                        gf_proc_dump_build_key (key,
                                                                "ref_by_xl:",
                                                                "%s",
                                                                xl->name);
                                        gf_proc_dump_write (key, "%d", ref);
                                }
                        }
                }

		if (dump_options.xl_options.dump_fdctx != _gf_true)
			goto unlock;


                list_for_each_entry (fd, &inode->fd_list, inode_list) {
                        fd_ctx_dump (fd, prefix);
                }
        }
unlock:
        UNLOCK(&inode->lock);

        if (inode_ctx && (dump_options.xl_options.dump_inodectx == _gf_true)) {
                for (i = 0; i < inode->table->ctxcount; i++) {
                        if (inode_ctx[i].xl_key) {
                                xl = (xlator_t *)(long)inode_ctx[i].xl_key;
                                if (xl->dumpops && xl->dumpops->inodectx)
                                        xl->dumpops->inodectx (xl, inode);
                        }
                }
        }

        GF_FREE (inode_ctx);

        return;
}

void
inode_table_dump (inode_table_t *itable, char *prefix)
{

        char    key[GF_DUMP_MAX_BUF_LEN];
        int     ret = 0;

        if (!itable)
                return;

        memset(key, 0, sizeof(key));
        ret = pthread_mutex_trylock(&itable->lock);

        if (ret != 0) {
                return;
        }

        gf_proc_dump_build_key(key, prefix, "hashsize");
        gf_proc_dump_write(key, "%d", itable->hashsize);
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

        INODE_DUMP_LIST(&itable->active, key, prefix, "active");
        INODE_DUMP_LIST(&itable->lru, key, prefix, "lru");
        INODE_DUMP_LIST(&itable->purge, key, prefix, "purge");

        pthread_mutex_unlock(&itable->lock);
}

void
inode_dump_to_dict (inode_t *inode, char *prefix, dict_t *dict)
{
        int             ret = -1;
        char            key[GF_DUMP_MAX_BUF_LEN] = {0,};

        ret = TRY_LOCK (&inode->lock);
        if (ret)
                return;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.gfid", prefix);
        ret = dict_set_dynstr (dict, key, gf_strdup (uuid_utoa (inode->gfid)));
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.nlookup", prefix);
        ret = dict_set_uint64 (dict, key, inode->nlookup);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.ref", prefix);
        ret = dict_set_uint32 (dict, key, inode->ref);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.ia_type", prefix);
        ret = dict_set_int32 (dict, key, inode->ia_type);

out:
        UNLOCK (&inode->lock);
        return;
}

void
inode_table_dump_to_dict (inode_table_t *itable, char *prefix, dict_t *dict)
{
        char            key[GF_DUMP_MAX_BUF_LEN] = {0,};
        int             ret = 0;
        inode_t         *inode = NULL;
        int             count = 0;

        ret = pthread_mutex_trylock (&itable->lock);
        if (ret)
                return;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.itable.active_size", prefix);
        ret = dict_set_uint32 (dict, key, itable->active_size);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.itable.lru_size", prefix);
        ret = dict_set_uint32 (dict, key, itable->lru_size);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.itable.purge_size", prefix);
        ret = dict_set_uint32 (dict, key, itable->purge_size);
        if (ret)
                goto out;

        list_for_each_entry (inode, &itable->active, list) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.itable.active%d", prefix,
                          count++);
                inode_dump_to_dict (inode, key, dict);
        }
        count = 0;

        list_for_each_entry (inode, &itable->lru, list) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.itable.lru%d", prefix,
                          count++);
                inode_dump_to_dict (inode, key, dict);
        }
        count = 0;

        list_for_each_entry (inode, &itable->purge, list) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.itable.purge%d", prefix,
                          count++);
                inode_dump_to_dict (inode, key, dict);
        }

out:
        pthread_mutex_unlock (&itable->lock);

        return;
}

size_t
inode_ctx_size (inode_t *inode)
{
        int       i    = 0;
        size_t    size = 0;
        xlator_t *xl   = NULL, *old_THIS = NULL;

        if (!inode)
                goto out;

        LOCK (&inode->lock);
        {
                for (i = 0; i < inode->table->ctxcount; i++) {
                        if (!inode->_ctx[i].xl_key)
                                continue;

                        xl = (xlator_t *)(long)inode->_ctx[i].xl_key;
                        old_THIS = THIS;
                        THIS = xl;

                        if (xl->cbks->ictxsize)
                                size += xl->cbks->ictxsize (xl, inode);

                        THIS = old_THIS;
                }
        }
        UNLOCK (&inode->lock);

out:
        return size;
}
