/*
  BD translator - Exports Block devices on server side as regular
  files to client.

  Copyright IBM, Corp. 2012

  This file is part of GlusterFS.

  Author:
  M. Mohan Kumar <mohan@in.ibm.com>

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/
#ifndef _BD_MAP_HELP_H
#define _BD_MAP_HELP_H

#define BD_RD_LOCK(lock) \
        pthread_rwlock_rdlock (lock);

#define BD_WR_LOCK(lock) \
        pthread_rwlock_wrlock (lock);

#define BD_UNLOCK(lock) \
        pthread_rwlock_unlock (lock);

#define BD_WR_ENTRY(priv, bdentry, path)       \
        do {                                   \
                BD_WR_LOCK (&priv->lock);      \
                bdentry = bd_entry_get (path); \
                BD_UNLOCK (&priv->lock);       \
        } while (0)

#define BD_ENTRY(priv, bdentry, path)          \
        do {                                   \
                BD_RD_LOCK (&priv->lock);      \
                bdentry = bd_entry_get (path); \
                BD_UNLOCK (&priv->lock);       \
        } while (0)

#define BD_PUT_ENTRY(priv, bdentry)             \
        do {                                    \
                BD_RD_LOCK (&priv->lock);       \
                bd_entry_put (bdentry);         \
                BD_UNLOCK (&priv->lock);        \
        } while (0)

#define BD_ENTRY_UPDATE_TIME(bdentry)  bd_update_time (bdentry, 0)
#define BD_ENTRY_UPDATE_ATIME(bdentry) bd_update_time (bdentry, 2)
#define BD_ENTRY_UPDATE_MTIME(bdentry) bd_update_time (bdentry, 1)

extern bd_entry_t *bd_rootp;
extern gf_lock_t inode_lk;

void bd_entry_istat (const char *path, struct iatt *attr, int type);
bd_entry_t *bd_entry_add_root (void);
bd_entry_t *bd_entry_add (bd_entry_t *parent, const char *name,
                struct iatt *iattr, ia_type_t type);
bd_entry_t *bd_entry_get_list (const char *name, bd_entry_t *parent);
bd_entry_t *bd_entry_get (const char *name);
void bd_entry_put (bd_entry_t *entry);
int bd_build_lv_list (bd_priv_t *priv, char *vg);
int bd_entry_cleanup (void);
void bd_update_time (bd_entry_t *entry, int type);
int bd_entry_rm (const char *path);

#endif
