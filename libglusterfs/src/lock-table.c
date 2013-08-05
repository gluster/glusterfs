/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "lock-table.h"
#include "common-utils.h"


struct _lock_table *
gf_lock_table_new (void)
{
        struct _lock_table *new = NULL;

        new = GF_CALLOC (1, sizeof (struct _lock_table), gf_common_mt_lock_table);
        if (new == NULL) {
                goto out;
        }
        INIT_LIST_HEAD (&new->entrylk_lockers);
        INIT_LIST_HEAD (&new->inodelk_lockers);
        LOCK_INIT (&new->lock);
out:
        return new;
}


int
gf_add_locker (struct _lock_table *table, const char *volume,
               loc_t *loc, fd_t *fd, pid_t pid, gf_lkowner_t *owner,
               glusterfs_fop_t type)
{
        int32_t         ret = -1;
        struct _locker *new = NULL;

        GF_VALIDATE_OR_GOTO ("lock-table", table, out);
        GF_VALIDATE_OR_GOTO ("lock-table", volume, out);

        new = GF_CALLOC (1, sizeof (struct _locker), gf_common_mt_locker);
        if (new == NULL) {
                goto out;
        }
        INIT_LIST_HEAD (&new->lockers);

        new->volume = gf_strdup (volume);

        if (fd == NULL) {
                loc_copy (&new->loc, loc);
        } else {
                new->fd = fd_ref (fd);
        }

        new->pid   = pid;
        new->owner = *owner;

        LOCK (&table->lock);
        {
                if (type == GF_FOP_ENTRYLK)
                        list_add_tail (&new->lockers, &table->entrylk_lockers);
                else
                        list_add_tail (&new->lockers, &table->inodelk_lockers);
        }
        UNLOCK (&table->lock);
out:
        return ret;
}

int
gf_del_locker (struct _lock_table *table, const char *volume,
               loc_t *loc, fd_t *fd, gf_lkowner_t *owner, glusterfs_fop_t type)
{
        struct _locker    *locker = NULL;
        struct _locker    *tmp = NULL;
        int32_t            ret = -1;
        struct list_head  *head = NULL;
        struct list_head   del;

        GF_VALIDATE_OR_GOTO ("lock-table", table, out);
        GF_VALIDATE_OR_GOTO ("lock-table", volume, out);

        INIT_LIST_HEAD (&del);

        LOCK (&table->lock);
        {
                if (type == GF_FOP_ENTRYLK) {
                        head = &table->entrylk_lockers;
                } else {
                        head = &table->inodelk_lockers;
                }

                list_for_each_entry_safe (locker, tmp, head, lockers) {
                        if (!is_same_lkowner (&locker->owner, owner) ||
                            strcmp (locker->volume, volume))
                                continue;

                        /*
                         * It is possible for inodelk lock to come on anon-fd
                         * and inodelk unlock to come on normal fd in case of
                         * client re-opens. So don't check for fds to be equal.
                         */
                        if (locker->fd && fd)
                                list_move_tail (&locker->lockers, &del);
                        else if (locker->loc.inode && loc &&
                                 (locker->loc.inode == loc->inode))
                                list_move_tail (&locker->lockers, &del);
                }
        }
        UNLOCK (&table->lock);

        tmp = NULL;
        locker = NULL;

        list_for_each_entry_safe (locker, tmp, &del, lockers) {
                list_del_init (&locker->lockers);
                if (locker->fd)
                        fd_unref (locker->fd);
                else
                        loc_wipe (&locker->loc);

                GF_FREE (locker->volume);
                GF_FREE (locker);
        }

        ret = 0;
out:
        return ret;

}

