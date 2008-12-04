/*
   Copyright (c) 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "inode.h"
#include "common-utils.h"
#include <pthread.h>
#include <sys/types.h>
#include <stdint.h>
#include "list.h"
#include <time.h>
#include <assert.h>

/* TODO: 
   move latest accessed dentry to list_head of inode
*/

static inode_t *
__inode_unref (inode_t *inode);

static int
inode_table_prune (inode_table_t *table);

static int
hash_name (ino_t par,
           const char *name,
           int mod)
{
        int hash = 0;
        int ret = 0;

        hash = *name;
        if (hash) {
                for (name += 1; *name != '\0'; name++) {
                        hash = (hash << 5) - hash + *name;
                }
        }
        ret = (hash + par) % mod;

        return ret;
}


static int
hash_inode (ino_t ino,
            int mod)
{
        int hash = 0;

        hash = ino % mod;

        return hash;
}


static void
__dentry_hash (dentry_t *dentry)
{
        inode_table_t   *table = NULL;
        int              hash = 0;

        table = dentry->inode->table;
        hash = hash_name (dentry->parent->ino, dentry->name,
                          table->hashsize);

        list_del_init (&dentry->hash);
        list_add (&dentry->hash, &table->name_hash[hash]);

        list_del_init (&dentry->parent_list);
        list_add (&dentry->parent_list, &dentry->parent->child_list);
	
	gf_log (table->name, GF_LOG_DEBUG,
		"dentry hashed %s (%"PRId64")",
		dentry->name, dentry->inode->ino);
}


static int
__is_dentry_hashed (dentry_t *dentry)
{
        return !list_empty (&dentry->hash);
}


static void
__dentry_unhash (dentry_t *dentry)
{
        list_del_init (&dentry->hash);
	
	gf_log (dentry->inode->table->name, GF_LOG_DEBUG,
		"dentry unhashed %s (%"PRId64")",
		dentry->name, dentry->inode->ino);
}


static void
__dentry_unset (dentry_t *dentry)
{
        __dentry_unhash (dentry);

        list_del_init (&dentry->inode_list);

	gf_log (dentry->inode->table->name, GF_LOG_DEBUG,
		"unset dentry %s (%"PRId64")",
		dentry->name, dentry->inode->ino);

        if (dentry->name)
                FREE (dentry->name);

        if (dentry->parent) {
                list_del_init (&dentry->parent_list);
                __inode_unref (dentry->parent);
                dentry->parent = NULL;
        }

        FREE (dentry);
}


static void
__inode_unhash (inode_t *inode)
{
        list_del_init (&inode->hash);
}


static int
__is_inode_hashed (inode_t *inode)
{
        return !list_empty (&inode->hash);
}


static void
__inode_hash (inode_t *inode)
{
        inode_table_t *table = NULL;
        int            hash = 0;

        table = inode->table;
        hash = hash_inode (inode->ino, table->hashsize);

        list_del_init (&inode->hash);
        list_add (&inode->hash, &table->inode_hash[hash]);
}


static inode_t *
__inode_search (inode_table_t *table,
                ino_t ino)
{
        int  hash = 0;
        inode_t  *inode = NULL;
        inode_t  *tmp = NULL;

        hash = hash_inode (ino, table->hashsize);

        list_for_each_entry (tmp, &table->inode_hash[hash], hash) {
                if (tmp->ino == ino) {
                        inode = tmp;
                        break;
                }
        }

        return inode;
}


static dentry_t *
__dentry_search_for_inode (inode_t *inode,
                           ino_t par,
                           const char *name)
{
        dentry_t *dentry = NULL;
        dentry_t *tmp = NULL;

        list_for_each_entry (tmp, &inode->dentry_list, inode_list) {
                if (tmp->parent->ino == par && !strcmp (tmp->name, name)) {
                        dentry = tmp;
                        break;
                }
        }

        return dentry;
}


static dentry_t *
__dentry_search (inode_table_t *table,
                 ino_t par,
                 const char *name)
{
        int       hash = 0;
        dentry_t *dentry = NULL;
        dentry_t *tmp = NULL;

        hash = hash_name (par, name, table->hashsize);

        list_for_each_entry (tmp, &table->name_hash[hash], hash) {
                if (tmp->parent->ino == par && !strcmp (tmp->name, name)) {
                        dentry = tmp;
                        break;
                }
        }

        return dentry;
}


static void
__inode_destroy (inode_t *inode)
{
        data_pair_t *pair = NULL;
        xlator_t    *xl = NULL;

        if (!inode->ctx) {
                goto noctx;
        }
        for (pair = inode->ctx->members_list; pair; pair = pair->next) {
                /* notify all xlators which have a context */
                xl = xlator_search_by_name (inode->table->xl, pair->key);

                if (!xl) {
                        gf_log (inode->table->name, GF_LOG_CRITICAL,
                                "inode(%"PRId64")->ctx has invalid key(%s)",
                                inode->ino, pair->key);
                        continue;
                }

		if (xl->cbks->forget)
			xl->cbks->forget (xl, inode);
		else
                        gf_log (inode->table->name, GF_LOG_CRITICAL,
                                "xlator(%s) in inode(%"PRId64") no FORGET fop",
                                xl->name, inode->ino);
        }
        dict_destroy (inode->ctx);

noctx:

        if (inode->ino)
                gf_log (inode->table->name, GF_LOG_DEBUG,
                        "destroy inode(%"PRId64") [@%p]", inode->ino, inode);
  
        LOCK_DESTROY (&inode->lock);
        //  memset (inode, 0xb, sizeof (*inode));
        FREE (inode);
}


static void
__inode_activate (inode_t *inode)
{
        list_move (&inode->list, &inode->table->active);
        inode->table->active_size++;
	
        gf_log (inode->table->name, GF_LOG_DEBUG,
                "activating inode(%"PRId64"), lru=%d/%d active=%d purge=%d",
                inode->ino, inode->table->lru_size, inode->table->lru_limit,
                inode->table->active_size, inode->table->purge_size);
}


static void
__inode_passivate (inode_t *inode)
{
        dentry_t      *dentry = NULL;
        dentry_t      *t = NULL;
        inode_table_t *table = NULL;

        table = inode->table;

        list_move_tail (&inode->list, &inode->table->lru);
        inode->table->lru_size++;

        gf_log (table->name, GF_LOG_DEBUG,
                "passivating inode(%"PRId64") lru=%d/%d active=%d purge=%d",
                inode->ino, table->lru_size, table->lru_limit,
                table->active_size, table->purge_size);

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
        inode_table_t *table = NULL;

        table = inode->table;

        list_move_tail (&inode->list, &inode->table->purge);
        inode->table->purge_size++;

        gf_log (table->name, GF_LOG_DEBUG,
                "retiring inode(%"PRId64") lru=%d/%d active=%d purge=%d",
                inode->ino, table->lru_size, table->lru_limit,
                table->active_size, table->purge_size);

        __inode_unhash (inode);
        assert (list_empty (&inode->child_list));

        list_for_each_entry_safe (dentry, t, &inode->dentry_list, inode_list) {
                __dentry_unset (dentry);
        }
}


static inode_t *
__inode_unref (inode_t *inode)
{
        if (inode->ino == 1)
                return inode;

        assert (inode->ref);

        --inode->ref;

        if (!inode->ref) {
                inode->table->active_size--;

                if (inode->nlookup && __is_inode_hashed (inode))
                        __inode_passivate (inode);
                else
                        __inode_retire (inode);
        }

        return inode;
}


static inode_t *
__inode_ref (inode_t *inode)
{
        if (!inode->ref) {
                inode->table->lru_size--;
                __inode_activate (inode);
        }
        inode->ref++;

        return inode;
}


inode_t *
inode_unref (inode_t *inode)
{
        inode_table_t *table = NULL;

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

        table = inode->table;

        pthread_mutex_lock (&table->lock);
        {
                inode = __inode_ref (inode);
        }
        pthread_mutex_unlock (&table->lock);

        return inode;
}


static dentry_t *
__dentry_create (inode_t *inode,
                 inode_t *parent,
                 const char *name)
{
        dentry_t      *newd = NULL;

        newd = (void *) calloc (1, sizeof (*newd));

        INIT_LIST_HEAD (&newd->inode_list);
        INIT_LIST_HEAD (&newd->parent_list);
        INIT_LIST_HEAD (&newd->hash);

        list_add (&newd->parent_list, &parent->child_list);
        newd->parent = __inode_ref (parent);
        newd->name = strdup (name);

        list_add (&newd->inode_list, &inode->dentry_list);
        newd->inode = inode;

        return newd;
}


static inode_t *
__inode_create (inode_table_t *table)
{
        inode_t  *newi = NULL;

        newi = (void *) calloc (1, sizeof (*newi));
        if (!newi)
                return NULL;

        newi->table = table;

        LOCK_INIT (&newi->lock);

        INIT_LIST_HEAD (&newi->fd_list);
        INIT_LIST_HEAD (&newi->list);
        INIT_LIST_HEAD (&newi->hash);
        INIT_LIST_HEAD (&newi->dentry_list);
        INIT_LIST_HEAD (&newi->child_list);


        list_add (&newi->list, &table->lru);
        table->lru_size++;

        newi->ctx = get_new_dict ();
        gf_log (table->name, GF_LOG_DEBUG,
                "create inode(%"PRId64")", newi->ino);

        return newi;
}


inode_t *
inode_new (inode_table_t *table)
{
        inode_t *inode = NULL;

        pthread_mutex_lock (&table->lock);
        {
                inode = __inode_create (table);
                __inode_ref (inode);
        }
        pthread_mutex_unlock (&table->lock);

        return inode;
}


static inode_t *
__inode_lookup (inode_t *inode)
{
        inode->nlookup++;

        return inode;
}


static inode_t *
__inode_forget (inode_t *inode, uint64_t nlookup)
{
        assert (inode->nlookup >= nlookup);

        inode->nlookup -= nlookup;

        if (!nlookup)
                inode->nlookup = 0;

        return inode;
}


inode_t *
inode_search (inode_table_t *table,
              ino_t ino,
              const char *name)
{
        inode_t *inode = NULL;
        dentry_t *dentry = NULL;

        pthread_mutex_lock (&table->lock);
        {
                if (!name) {
                        inode = __inode_search (table, ino);
                } else {
                        dentry = __dentry_search (table, ino, name);
      
                        if (dentry)
                                inode = dentry->inode;
                }
    
                if (inode)
                        __inode_ref (inode);
        }
        pthread_mutex_unlock (&table->lock);

        return inode;
}


static void
__copy_dentries (inode_t *oldi, inode_t *newi)
{
        dentry_t *dentry = NULL;
        dentry_t *newd = NULL;
        dentry_t *tmp = NULL;

        list_for_each_entry (dentry, &oldi->dentry_list, inode_list) {
                tmp = __dentry_search_for_inode (newi, dentry->parent->ino,
                                                 dentry->name);

                if (!tmp) {
                        newd = __dentry_create (newi, dentry->parent,
                                                dentry->name);
                } else {
                        newd = tmp;
                }

                if (__is_dentry_hashed (dentry)) {
                        __dentry_unhash (dentry);
                        __dentry_hash (newd);
                }
        }
}


static void
__adopt_children (inode_t *oldi, inode_t *newi)
{
        dentry_t *dentry = NULL;

        list_for_each_entry (dentry, &oldi->child_list, parent_list) {
                assert (dentry->parent == oldi);
                __inode_unref (dentry->parent);
                dentry->parent = __inode_ref (newi);
        }

        list_splice_init (&oldi->child_list, &newi->child_list);
}


static void
__inode_replace (inode_t *oldi, inode_t *newi)
{
        gf_log (oldi->table->name, GF_LOG_DEBUG,
                "inode(%"PRId64") replaced (%"PRId64"",
                oldi->ino, newi->ino);

        __copy_dentries (oldi, newi);
        __adopt_children (oldi, newi);

        newi->nlookup    = oldi->nlookup;
        newi->generation = oldi->generation;

        oldi->nlookup    = 0;
        oldi->generation = 0;

        __inode_unhash (oldi);
}


static inode_t *
__inode_link (inode_t *inode,
              inode_t *parent,
              const char *name,
              struct stat *stbuf)
{
        dentry_t      *dentry = NULL;
        dentry_t      *old_dentry = NULL;
        inode_t       *old_inode = NULL;
        inode_table_t *table = NULL;

        table = inode->table;

	if (inode->ino)
		assert (inode->ino == stbuf->st_ino);

        inode->ino     = stbuf->st_ino;
        inode->st_mode = stbuf->st_mode;

        old_inode = __inode_search (table, stbuf->st_ino);

        if (old_inode && old_inode != inode) {
                __inode_ref (old_inode);
                __inode_replace (old_inode, inode);
                __inode_unref (old_inode);
        }
        __inode_hash (inode);

        if (parent) {
                dentry = __dentry_search_for_inode (inode, parent->ino, name);
                if (!dentry) {
                        dentry = __dentry_create (inode, parent, name);
                }

                old_dentry = __dentry_search (table, parent->ino, name);
                if (old_dentry) {
                        __dentry_unhash (old_dentry);
                }

                __dentry_hash (dentry);
        } else if (inode->ino != 1) {
		gf_log (table->name, GF_LOG_ERROR,
			"child (%"PRId64") without a parent :O", inode->ino);
	}
	
        return inode;
}


int
inode_link (inode_t *inode,
            inode_t *parent,
            const char *name,
            struct stat *stbuf)
{
        inode_table_t *table = NULL;

        table = inode->table;

        pthread_mutex_lock (&table->lock);
        {
                inode = __inode_link (inode, parent, name, stbuf);
        }
        pthread_mutex_unlock (&table->lock);

        inode_table_prune (table);

        return 0;
}


int
inode_lookup (inode_t *inode)
{
        inode_table_t *table = NULL;
	inode_t       *lookup_inode = NULL;

        table = inode->table;
	lookup_inode = inode;

        pthread_mutex_lock (&table->lock);
        {
		if (!__is_inode_hashed (inode)) {
			lookup_inode = __inode_search (table, inode->ino);
		}

                __inode_lookup (lookup_inode);
        }
        pthread_mutex_unlock (&table->lock);

        return 0;
}


int
inode_forget (inode_t *inode, uint64_t nlookup)
{
        inode_table_t *table = NULL;
	inode_t       *forget_inode = NULL;

        table = inode->table;
	forget_inode = inode;

        pthread_mutex_lock (&table->lock);
        {
		if (!__is_inode_hashed (inode)) {
			forget_inode = __inode_search (table, inode->ino);
		}

                 __inode_forget (forget_inode, nlookup);
        }
        pthread_mutex_unlock (&table->lock);

        inode_table_prune (table);

        return 0;
}


static void
__inode_unlink (inode_t *inode,
                inode_t *parent,
                const char *name)
{
        dentry_t *dentry = NULL;

        dentry = __dentry_search_for_inode (inode, parent->ino, name);

	__dentry_unset (dentry);
}

			      
void
inode_unlink (inode_t *inode,
              inode_t *parent,
              const char *name)
{
        inode_table_t *table = NULL;
	inode_t       *unlink_inode = NULL;

        table = inode->table;
	unlink_inode = inode;

        pthread_mutex_lock (&table->lock);
        {
		if (!__is_inode_hashed (inode)) {
			unlink_inode = __inode_search (table, inode->ino);
		}

                __inode_unlink (unlink_inode, parent, name);
        }
        pthread_mutex_unlock (&table->lock);

        inode_table_prune (table);
}


int
inode_rename (inode_table_t *table,
              inode_t *srcdir,
              const char *srcname,
              inode_t *dstdir,
              const char *dstname,
              inode_t *inode,
              struct stat *stbuf)
{
	dentry_t      *old_dst = NULL;
	inode_t       *rename_inode = NULL;

	rename_inode = inode;

        pthread_mutex_lock (&table->lock);
        {
		if (!__is_inode_hashed (inode)) {
			rename_inode = __inode_search (table, inode->ino);
		}

		old_dst = __dentry_search (table, dstdir->ino, dstname);
		if (old_dst)
			__dentry_unset (old_dst);

                __inode_unlink (rename_inode, srcdir, srcname);
                __inode_link (rename_inode, dstdir, dstname, stbuf);
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
inode_parent (inode_t *inode, ino_t par, const char *name)
{
        inode_t       *parent = NULL;
        inode_table_t *table = NULL;
        dentry_t      *dentry = NULL;

        table = inode->table;

        pthread_mutex_lock (&table->lock);
        {
                if (par && name) {
                        dentry = __dentry_search_for_inode (inode, par, name);
                } else {
                        dentry = __dentry_search_arbit (inode);
                }

                if (dentry)
                        parent = __inode_ref (dentry->parent);
        }
        pthread_mutex_unlock (&table->lock);

        return parent;
}


int64_t
inode_path (inode_t *inode, const char *name, char *buf, size_t size)
{
        inode_table_t *table = NULL;
        dentry_t      *trav = NULL;
        size_t         i = 0;
        int64_t        ret = 0;
        int            len = 0;

        table = inode->table;

        pthread_mutex_lock (&table->lock);
        {
                for (trav = __dentry_search_arbit (inode); trav;
                     trav = __dentry_search_arbit (trav->parent)) {
                        i ++; /* "/" */
                        i += strlen (trav->name);
                }
		
		if ((inode->ino != 1) &&
		    (i == 0)) {
			gf_log (table->name, GF_LOG_ERROR,
				"dentry information missing for non-root inode %"PRId64,
				inode->ino);
			ret = -1;
			goto unlock;
		}

                if (name) {
                        i++;
                        i += strlen (name);
                }

                ret = i;
		
                if (buf && size > i) {

                        buf[size - 1] = 0;

                        if (name) {
                                len = strlen (name);
                                strncpy (buf + (i - len), name, len);
                                buf[i-len-1] = '/';
                                i -= (len + 1);
                        }

                        for (trav = __dentry_search_arbit (inode); trav;
                             trav = __dentry_search_arbit (trav->parent)) {
                                len = strlen (trav->name);
                                strncpy (buf + (i - len), trav->name, len);
                                buf[i-len-1] = '/';
                                i -= (len + 1);
                        }
                }
        }
unlock:
        pthread_mutex_unlock (&table->lock);

	if (inode->ino == 1 && !name) {
		ret = 1;
		if (buf)
			strcpy (buf, "/");
	}

        return ret;
}


static int
inode_table_prune (inode_table_t *table)
{
        int               ret = 0;
        struct list_head  purge = {0, };
	inode_t          *del = NULL;
	inode_t          *tmp = NULL;
	inode_t          *entry = NULL;


        INIT_LIST_HEAD (&purge);

        pthread_mutex_lock (&table->lock);
        {
                while (table->lru_limit
                       && table->lru_size > (table->lru_limit)) {
        
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
        inode_t *root = NULL;
        struct stat stbuf = {0, };

        root = __inode_create (table);

        stbuf.st_ino = 1;
        stbuf.st_mode = S_IFDIR|0755;

        __inode_link (root, NULL, NULL, &stbuf);
        table->root = root;
}


inode_table_t *
inode_table_new (size_t lru_limit, xlator_t *xl)
{
        inode_table_t *new = NULL;
        int            i = 0;


        new = (void *)calloc (1, sizeof (*new));
        if (!new)
                return NULL;

        gf_log (xl->name, GF_LOG_DEBUG,
                "creating new inode table with lru_limit=%"GF_PRI_SIZET"", lru_limit);

        new->xl = xl;

        new->lru_limit = lru_limit;

        new->hashsize = 14057; /* TODO: Random Number?? */

        new->inode_hash = (void *)calloc (new->hashsize,
                                          sizeof (struct list_head));
        if (!new->inode_hash) {
                FREE (new);
                return NULL;
        }

        new->name_hash = (void *)calloc (new->hashsize,
                                         sizeof (struct list_head));
        if (!new->name_hash) {
                FREE (new->inode_hash);
                FREE (new);
                return NULL;
        }

        for (i=0; i<new->hashsize; i++) {
                INIT_LIST_HEAD (&new->inode_hash[i]);
        }


        for (i=0; i<new->hashsize; i++) {
                INIT_LIST_HEAD (&new->name_hash[i]);
        }

        INIT_LIST_HEAD (&new->active);
        INIT_LIST_HEAD (&new->lru);
        INIT_LIST_HEAD (&new->purge);

        asprintf (&new->name, "%s/inode", xl->name);

	__inode_table_init_root (new);

        pthread_mutex_init (&new->lock, NULL);

        return new;
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

	/* top-down approach */
	root = itable->root;
	parent = inode_ref (root);
	pathname = strdup (path);
	component = strtok_r (pathname, "/", &strtokptr);

	if (component == NULL)
		/* root inode */
		inode = inode_ref (parent);

	while (component) {
		curr = inode_search (itable, parent->ino, component);
		
		if (curr == NULL) {
			component = strtok_r (NULL, "/", &strtokptr);
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

	if (pathname)
		free (pathname);

	return inode;
}

