/*
  BD translator - Exports Block devices on server side as regular
  files to client

  Copyright IBM, Corp. 2012

  This file is part of GlusterFS.

  Author:
  M. Mohan Kumar <mohan@in.ibm.com>

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#define __XOPEN_SOURCE 500

#include <libgen.h>
#include <time.h>
#include <lvm2app.h>

#include "bd_map.h"
#include "bd_map_help.h"
#include "defaults.h"
#include "glusterfs3-xdr.h"

#define CHILD_ENTRY(node) list_entry ((&node->child)->next, typeof(*node), \
                child)

bd_entry_t *bd_rootp;
gf_lock_t inode_lk;
static uint64_t bd_entry_ino = 5000; /* Starting inode */

static void bd_entry_get_ino (uint64_t *inode)
{
        LOCK (&inode_lk);
        {
                *inode = bd_entry_ino++;
        }
        UNLOCK (&inode_lk);
}

void bd_update_time (bd_entry_t *entry, int type)
{
        struct timespec ts;

        clock_gettime (CLOCK_REALTIME, &ts);
        if (type == 0) {
                entry->attr->ia_mtime = ts.tv_sec;
                entry->attr->ia_mtime_nsec = ts.tv_nsec;
                entry->attr->ia_atime = ts.tv_sec;
                entry->attr->ia_atime_nsec = ts.tv_nsec;
        } else if (type == 1) {
                entry->attr->ia_mtime = ts.tv_sec;
                entry->attr->ia_mtime_nsec = ts.tv_nsec;
        } else {
                entry->attr->ia_atime = ts.tv_sec;
                entry->attr->ia_atime_nsec = ts.tv_nsec;
        }
}

static bd_entry_t *bd_entry_init (const char *name)
{
        bd_entry_t *bdentry;

        bdentry = GF_MALLOC (sizeof(bd_entry_t), gf_bd_entry);
        if (!bdentry)
                return NULL;

        bdentry->attr = GF_MALLOC (sizeof(struct iatt), gf_bd_attr);
        if (!bdentry->attr) {
                GF_FREE (bdentry);
                return NULL;
        }

        strcpy (bdentry->name, name);
        INIT_LIST_HEAD (&bdentry->sibling);
        INIT_LIST_HEAD (&bdentry->child);
        bdentry->link = NULL;
        bdentry->refcnt = 0;
        return bdentry;
}

static bd_entry_t *bd_entry_clone (bd_entry_t *orig, char *name)
{
        bd_entry_t *bdentry;

        bdentry = GF_MALLOC (sizeof(bd_entry_t), gf_bd_entry);
        if (!bdentry)
                return NULL;

        bdentry->attr = orig->attr;

        strcpy (bdentry->name, name);
        INIT_LIST_HEAD (&bdentry->sibling);
        INIT_LIST_HEAD (&bdentry->child);
        bdentry->link = orig;
        bdentry->refcnt = 0;
        return bdentry;
}

static void bd_entry_init_iattr (struct iatt *attr, int type)
{
        struct timespec ts        = {0, };

        clock_gettime (CLOCK_REALTIME, &ts);
        attr->ia_dev         = ia_makedev (0, 0); /* FIXME: */
        attr->ia_type        = type;
        attr->ia_prot        = ia_prot_from_st_mode (0750);
        attr->ia_nlink       = 2;
        attr->ia_uid         = 0;
        attr->ia_gid         = 0;
        attr->ia_rdev        = ia_makedev (0, 0);

        attr->ia_size        = 4096; /* FIXME */
        attr->ia_blksize     = 4096;
        attr->ia_blocks      = 0;

        attr->ia_atime      = ts.tv_sec;
        attr->ia_atime_nsec = ts.tv_nsec;
        attr->ia_mtime      = ts.tv_sec;
        attr->ia_mtime_nsec = ts.tv_nsec;
        attr->ia_ctime      = ts.tv_sec;
        attr->ia_ctime_nsec = ts.tv_nsec;
}

/*
 * bd_entry_istat: Initialize iatt strucutre for a given path on success
 */
void bd_entry_istat (const char *path, struct iatt *attr, int type)
{
        struct  stat stbuf    = {0, };

        if (stat (path, &stbuf) < 0)
                bd_entry_init_iattr (attr, type);
        else
                iatt_from_stat (attr, &stbuf);
        sprintf ((char *)attr->ia_gfid, "%lx", stbuf.st_ino);
}

/*
 * Adds the root entry and required entries
 * ie header entry followed by . and .. entries
 */
bd_entry_t *bd_entry_add_root (void)
{
        bd_entry_t      *bdentry  = NULL;
        bd_entry_t      *h_entry  = NULL;
        bd_entry_t      *d_entry  = NULL;
        bd_entry_t      *dd_entry = NULL;

        bdentry = bd_entry_init ("/");
        if (!bdentry)
                return NULL;

        bdentry->parent = bdentry;

        bd_entry_get_ino (&bdentry->attr->ia_ino);
        sprintf ((char *)bdentry->attr->ia_gfid, "%ld",
                        bdentry->attr->ia_ino << 2);
        bd_entry_init_iattr (bdentry->attr, IA_IFDIR);

        h_entry = bd_entry_clone (bdentry, "");
        bdentry->child.next = &h_entry->child;
        bdentry->child.prev = &h_entry->child;

        d_entry = bd_entry_clone (bdentry, ".");
        dd_entry = bd_entry_clone (bdentry, "..");

        list_add_tail (&d_entry->sibling, &h_entry->sibling);
        list_add_tail (&dd_entry->sibling, &h_entry->sibling);
        return bdentry;
}

bd_entry_t *bd_entry_add (bd_entry_t *parent, const char *name,
                struct iatt *iattr, ia_type_t type)
{
        bd_entry_t           *bdentry  = NULL;
        bd_entry_t           *h_entry  = NULL;
        bd_entry_t           *d_entry  = NULL;
        bd_entry_t           *dd_entry = NULL;
        bd_entry_t           *sentry   = NULL;
        struct timespec      ts        = { 0, };

        if (!parent)
                parent = bd_rootp;

        if (type != IA_IFREG && type != IA_IFDIR)
                return NULL;

        bdentry = bd_entry_init (name);
        if (!bdentry)
                return NULL;

        bdentry->parent = parent;

        iattr->ia_type = type;

        bd_entry_get_ino (&iattr->ia_ino);
        if (IA_ISDIR(type)) {
                h_entry = bd_entry_clone (bdentry, "");
                parent->attr->ia_nlink++;
                bdentry->child.next = &h_entry->child;
                bdentry->child.prev = &h_entry->child;

                d_entry = bd_entry_clone (bdentry, ".");
                dd_entry = bd_entry_clone (bdentry, "..");

                list_add_tail (&d_entry->sibling, &h_entry->sibling);
                list_add_tail (&dd_entry->sibling, &h_entry->sibling);
        }
        memcpy (bdentry->attr, iattr, sizeof(*iattr));

        clock_gettime (CLOCK_REALTIME, &ts);
        parent->attr->ia_mtime = ts.tv_sec;
        parent->attr->ia_mtime_nsec = ts.tv_nsec;
        bdentry->size = iattr->ia_size;

        sentry = CHILD_ENTRY (parent);
        list_add_tail (&bdentry->sibling, &sentry->sibling);
        return bdentry;
}

bd_entry_t *bd_entry_get_list (const char *name, bd_entry_t *parent)
{
        bd_entry_t     *centry  = NULL;
        bd_entry_t     *bdentry = NULL;

        if (!parent)
                parent = bd_rootp;

        if (parent->child.next == &parent->child)
                return NULL;

        centry = CHILD_ENTRY (parent);
        if (!strcmp (centry->name, name))
                return centry;

        list_for_each_entry (bdentry, &centry->sibling, sibling) {
                if (!strcmp (bdentry->name, name))
                        return bdentry;
        }
        return NULL;
}

/* FIXME: Do we need hashing here? */
bd_entry_t *bd_entry_find_by_gfid (const char *path)
{
        bd_entry_t  *h     = NULL;
        bd_entry_t  *tmp   = NULL;
        bd_entry_t  *tmp2  = NULL;
        bd_entry_t  *node  = NULL;
        bd_entry_t  *cnode = NULL;
        bd_entry_t  *leaf  = NULL;
        char        *gfid  = NULL;
        char        *cp    = NULL;
        char        *bgfid = NULL;
        bd_entry_t  *entry = NULL;

        gfid = GF_MALLOC (strlen(path) + 1, gf_common_mt_char);
        sscanf (path, "<gfid:%s", gfid);
        if (!gfid)
                return NULL;

        cp = strchr(gfid, '>');
        *cp = '\0';

        node = CHILD_ENTRY (bd_rootp);

        bgfid = GF_MALLOC (GF_UUID_BUF_SIZE, gf_common_mt_char);
        if (!bgfid)
                return NULL;

        list_for_each_entry_safe (h, tmp, &node->sibling, sibling) {
                uuid_utoa_r (h->attr->ia_gfid, bgfid);
                if (!h->link && !strcmp (gfid, bgfid)) {
                        entry = h;
                        goto out;
                }

                /* if we have children for this node */
                if (h->child.next != &h->child) {
                        cnode = CHILD_ENTRY (h);
                        uuid_utoa_r (cnode->attr->ia_gfid, bgfid);
                        if (!cnode->link && !strcmp (gfid, bgfid)) {
                                entry = cnode;
                                goto out;
                        }

                        list_for_each_entry_safe (leaf, tmp2, (&cnode->sibling),
                                                  sibling) {
                                uuid_utoa_r (leaf->attr->ia_gfid, bgfid);
                                if (!leaf->link && !strcmp (gfid, bgfid)) {
                                        entry = leaf;
                                        goto out;
                                }

                        }
                }
        }
out:
        if (bgfid)
                GF_FREE (bgfid);

        return entry;
}

/* Called with priv->bd_lock held */
bd_entry_t *bd_entry_get (const char *name)
{
        bd_entry_t     *pentry = NULL;
        char           *path   = NULL;
        char           *comp   = NULL;
        char           *save   = NULL;

        if (!strncmp (name, "<gfid:", 5)) {
                pentry = bd_entry_find_by_gfid (name);
                if (pentry)
                        pentry->refcnt++;
                return pentry;
        }

        if (!strcmp (name, "/")) {
                bd_rootp->refcnt++;
                return bd_rootp;
        }

        path = gf_strdup (name);
        comp = strtok_r (path, "/", &save);
        pentry = bd_entry_get_list (comp, NULL);
        if (!pentry)
                goto out;
        while (comp) {
                comp = strtok_r (NULL, "/", &save);
                if (!comp)
                        break;
                pentry = bd_entry_get_list (comp, pentry);
                if (!pentry)
                        goto out;
        }

        pentry->refcnt++;
out:
        GF_FREE (path);
        return pentry;
}

int bd_entry_rm (const char *path)
{
        bd_entry_t      *bdentry  = NULL;
        int             ret       = -1;

        bdentry = bd_entry_get (path);
        if (!bdentry)
                goto out;

        list_del_init (&bdentry->sibling);
        list_del_init (&bdentry->child);
        GF_FREE (bdentry);

        ret = 0;
out:
        return ret;
}



/* Called with priv->bd_lock held */
void bd_entry_put (bd_entry_t *entry)
{
        entry->refcnt--;
}

int bd_build_lv_list (bd_priv_t *priv, char *vg_name)
{
        struct dm_list         *lv_dm_list    = NULL;
        struct lvm_lv_list     *lv_list       = NULL;
        struct iatt            iattr          = {0, };
        char                   path[PATH_MAX] = {0, };
        vg_t                   vg             = NULL;
        bd_entry_t             *vg_map        = NULL;
        bd_entry_t             *bd            = NULL;
        int                    ret            = -1;
        const char             *lv_name       = NULL;

        priv->handle = lvm_init (NULL);
        if (!priv->handle) {
                gf_log (THIS->name, GF_LOG_CRITICAL, "FATAL: bd_init failed");
                return -1;
        }

        BD_WR_LOCK (&priv->lock);

        vg = lvm_vg_open (priv->handle, vg_name, "r", 0);
        if (!vg) {
                gf_log (THIS->name, GF_LOG_CRITICAL,
                        "opening vg %s failed", vg_name);
                goto out;
        }
        /* get list of LVs associated with this VG */
        lv_dm_list = lvm_vg_list_lvs (vg);
        sprintf (path, "/dev/%s", vg_name);
        bd_entry_istat (path, &iattr, IA_IFDIR);
        vg_map = bd_entry_add (bd_rootp, vg_name, &iattr,
                        IA_IFDIR);
        if (!vg_map) {
                gf_log (THIS->name, GF_LOG_CRITICAL,
                                "bd_add_entry failed");
                goto out;
        }
        ret = 0;
        if (!lv_dm_list) /* no lvs for this VG */
                goto out;

        dm_list_iterate_items (lv_list, lv_dm_list) {
                if (!lv_list)
                        continue;
                lv_name = lvm_lv_get_name (lv_list->lv);
                /* snapshot%d is reserved name */
                if (!strncmp (lv_name, "snapshot", 8))
                        continue;
                /* get symbolic path for this LV */
                sprintf (path, "/dev/%s/%s", vg_name, lv_name);
                bd_entry_istat (path, &iattr, IA_IFREG);
                /* Make the file size equivalant to BD size */
                iattr.ia_size = lvm_lv_get_size (lv_list->lv);
                /* got LV, add it to our tree */
                bd = bd_entry_add (vg_map,
                                lvm_lv_get_name (lv_list->lv),
                                &iattr, IA_IFREG);
                if (bd == NULL) {
                        gf_log (THIS->name, GF_LOG_ERROR,
                                        "bd_add_entry failed");
                        goto out;
                }
        }
out:
        if (vg)
                lvm_vg_close (vg);

        BD_UNLOCK (&priv->lock);
        return ret;
}

/*
 * Called with bd_lock held to cleanup entire list. If there was a
 * reference to any one of the entry, nothing cleared.
 * Return 0 on success -1 in case if there is a reference to the entry
 */
int bd_entry_cleanup (void)
{
        bd_entry_t     *node         = NULL;
        bd_entry_t     *tmp          = NULL;
        bd_entry_t     *tmp2         = NULL;
        bd_entry_t     *cnode        = NULL;
        bd_entry_t     *h            = NULL;
        bd_entry_t     *leaf         = NULL;

        if (!bd_rootp)
                return 0;

        node = CHILD_ENTRY (bd_rootp);
        if (node->refcnt) {
                gf_log (THIS->name, GF_LOG_WARNING,
                                "entry %s is inuse\n", node->name);
                return -1;
        }
        list_for_each_entry_safe (h, tmp, &node->sibling, sibling) {
                /* if we have children for this node */
                if (h->child.next != &h->child) {
                        cnode = CHILD_ENTRY (h);
                        list_for_each_entry_safe (leaf, tmp2, (&cnode->sibling),
                                        sibling) {
                                list_del_init (&leaf->sibling);
                                list_del_init (&leaf->child);
                                if (!leaf->link)
                                        GF_FREE (leaf->attr);
                                GF_FREE (leaf);
                        }
                        list_del_init (&cnode->sibling);
                        list_del_init (&cnode->child);
                        if (!cnode->link)
                                GF_FREE (cnode->attr);
                        GF_FREE (cnode);
                }
                if (!h->link)
                        GF_FREE (h->attr);
                GF_FREE (h);
        }
        GF_FREE (h);
        GF_FREE (bd_rootp->attr);
        GF_FREE (bd_rootp);
        return 0;
}
