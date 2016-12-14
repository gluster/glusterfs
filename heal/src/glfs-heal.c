/*
 Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
*/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "glfs.h"
#include "glfs-handles.h"
#include "glfs-internal.h"
#include "protocol-common.h"
#include "syscall.h"
#include "syncop.h"
#include "syncop-utils.h"
#include <string.h>
#include <time.h>
#include "glusterfs.h"

#if (HAVE_LIB_XML)
#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>

xmlTextWriterPtr glfsh_writer;
xmlDocPtr        glfsh_doc = NULL;
#endif

#define XML_RET_CHECK_AND_GOTO(ret, label)      do {            \
                if (ret < 0) {                                  \
                        ret = -1;                               \
                        goto label;                             \
                }                                               \
                else                                            \
                        ret = 0;                                \
        } while (0)                                             \

typedef int    (*print_status) (dict_t *, char *, uuid_t, uint64_t *,
                 gf_boolean_t flag);

int glfsh_heal_splitbrain_file (glfs_t *fs, xlator_t *top_subvol,
                                loc_t *rootloc, char *file, dict_t *xattr_req);


typedef struct glfs_info {
        int (*init)(void);
        int (*print_brick_from_xl)(xlator_t *xl, loc_t *rootloc);
        int (*print_heal_op_status)(int ret, uint64_t num_entries,
                        char *fmt_str);
        void (*print_heal_status)(char *path, uuid_t gfid, char *status);
        void (*print_spb_status)(char *path, uuid_t gfid, char *status);
        int (*end) (int op_ret, char *op_errstr);
} glfsh_info_t;

glfsh_info_t *glfsh_output = NULL;
int32_t is_xml;

#define DEFAULT_HEAL_LOG_FILE_DIRECTORY DATADIR "/log/glusterfs"
#define USAGE_STR "Usage: %s <VOLNAME> [bigger-file <FILE> | "\
                  "latest-mtime <FILE> | "\
                  "source-brick <HOSTNAME:BRICKNAME> [<FILE>] | "\
                  "split-brain-info]\n"

typedef enum {
        GLFSH_MODE_CONTINUE_ON_ERROR = 1,
        GLFSH_MODE_EXIT_ON_FIRST_FAILURE,
} glfsh_fail_mode_t;

int
glfsh_init ()
{
        return 0;
}

int
glfsh_end_op_granular_entry_heal (int op_ret, char *op_errstr)
{
        /* If error sting is available, give it higher precedence.*/

        if (op_errstr) {
                printf ("%s\n", op_errstr);
        } else if (op_ret < 0) {
                if (op_ret == -EAGAIN)
                        printf ("One or more entries need heal. Please execute "
                                "the command again after there are no entries "
                                "to be healed\n");
                else if (op_ret == -ENOTCONN)
                        printf ("One or more bricks could be down. Please "
                                "execute the command again after bringing all "
                                "bricks online and finishing any pending "
                                "heals\n");
                else
                        printf ("Command failed - %s. Please check the logs for"
                                " more details\n", strerror (-op_ret));
        }
        return 0;
}

int
glfsh_end (int op_ret, char *op_errstr)
{
        if (op_errstr)
                printf ("%s\n", op_errstr);
        return 0;
}

void
glfsh_print_hr_spb_status (char *path, uuid_t gfid, char *status)
{
        printf ("%s\n", path);
        return;
}

void
glfsh_no_print_hr_heal_status (char *path, uuid_t gfid, char *status)
{
        return;
}

void
glfsh_print_hr_heal_status (char *path, uuid_t gfid, char *status)
{
        printf ("%s%s\n", path, status);
}

#if (HAVE_LIB_XML)

int
glfsh_xml_init ()
{
        int     ret     = -1;
        glfsh_writer = xmlNewTextWriterDoc (&glfsh_doc, 0);
        if (glfsh_writer == NULL) {
                return -1;
        }

        ret = xmlTextWriterStartDocument (glfsh_writer, "1.0", "UTF-8",
                        "yes");
        XML_RET_CHECK_AND_GOTO (ret, xml_out);

        /* <cliOutput> */
        ret = xmlTextWriterStartElement (glfsh_writer,
                        (xmlChar *)"cliOutput");
        XML_RET_CHECK_AND_GOTO (ret, xml_out);

        /* <healInfo> */
        xmlTextWriterStartElement (glfsh_writer,
                        (xmlChar *)"healInfo");
        XML_RET_CHECK_AND_GOTO (ret, xml_out);
        /* <bricks> */
        xmlTextWriterStartElement (glfsh_writer,
                        (xmlChar *)"bricks");
        xmlTextWriterFlush (glfsh_writer);
xml_out:
        return ret;
}

int
glfsh_xml_end (int op_ret, char *op_errstr)
{
        int                     ret             = -1;
        int                     op_errno        = 0;
        gf_boolean_t            alloc           = _gf_false;

        if (op_ret < 0) {
                op_errno = -op_ret;
                op_ret = -1;
                if (op_errstr == NULL) {
                        op_errstr = gf_strdup (strerror (op_errno));
                        alloc = _gf_true;
                }
        } else {
                op_errstr = NULL;
        }

        /* </bricks> */
        ret = xmlTextWriterEndElement (glfsh_writer);
        XML_RET_CHECK_AND_GOTO (ret, xml_out);

        /* </healInfo> */
        ret = xmlTextWriterEndElement (glfsh_writer);
        XML_RET_CHECK_AND_GOTO (ret, xml_out);

        ret = xmlTextWriterWriteFormatElement (glfsh_writer,
                        (xmlChar *)"opRet", "%d", op_ret);

        XML_RET_CHECK_AND_GOTO (ret, xml_out);

        ret = xmlTextWriterWriteFormatElement (glfsh_writer,
                        (xmlChar *)"opErrno",
                        "%d", op_errno);
        XML_RET_CHECK_AND_GOTO (ret, xml_out);

        if (op_errstr)
                ret = xmlTextWriterWriteFormatElement (glfsh_writer,
                                (xmlChar *)"opErrstr",
                                "%s", op_errstr);
        else
                ret = xmlTextWriterWriteFormatElement (glfsh_writer,
                                (xmlChar *)"opErrstr",
                                "%s", "");
        ret = xmlTextWriterEndDocument (glfsh_writer);
        XML_RET_CHECK_AND_GOTO (ret, xml_out);


        /* Dump xml document to stdout and pretty format it */
        xmlSaveFormatFileEnc ("-", glfsh_doc, "UTF-8", 1);

        xmlFreeTextWriter (glfsh_writer);
        xmlFreeDoc (glfsh_doc);
xml_out:
        if (alloc)
                GF_FREE (op_errstr);
        return ret;
}

int
glfsh_print_xml_heal_op_status (int ret, uint64_t num_entries, char *fmt_str)
{
        if (ret < 0 && num_entries == 0) {
                xmlTextWriterWriteFormatElement (glfsh_writer,
                                (xmlChar *)"status",
                                "%s", strerror (-ret));
                if (fmt_str) {
                        xmlTextWriterWriteFormatElement (glfsh_writer,
                                        (xmlChar *)"numberOfEntries",
                                        "-");
                }
                goto out;
        } else if (ret == 0) {
                xmlTextWriterWriteFormatElement (glfsh_writer,
                                                 (xmlChar *)"status",
                                                 "%s", "Connected");
        }

        if (ret < 0) {
                if (fmt_str) {
                        xmlTextWriterWriteFormatElement (glfsh_writer,
                                        (xmlChar *)"status",
                                        "Failed to process entries completely. "
                                        "(%s)%s %"PRIu64"", strerror (-ret),
                                        fmt_str,
                                        num_entries);
                }
        } else {
                if (fmt_str)
                        xmlTextWriterWriteFormatElement (glfsh_writer,
                                        (xmlChar *)"numberOfEntries",
                                        "%"PRIu64"", num_entries);
        }
out:
        ret = xmlTextWriterEndElement (glfsh_writer);
        xmlTextWriterFlush (glfsh_writer);
        return ret;
}

void
glfsh_print_xml_file_status (char *path, uuid_t gfid, char *status)
{
        xmlTextWriterStartElement (glfsh_writer, (xmlChar *)"file");
        xmlTextWriterWriteFormatAttribute (glfsh_writer, (xmlChar *)"gfid",
                                           "%s", uuid_utoa (gfid));
        xmlTextWriterWriteFormatString (glfsh_writer, "%s", path);
        xmlTextWriterEndElement (glfsh_writer);
        xmlTextWriterFlush (glfsh_writer);
        return;
}

int
glfsh_print_xml_brick_from_xl (xlator_t *xl, loc_t *rootloc)
{
        char    *remote_host = NULL;
        char    *remote_subvol = NULL;
        char    *uuid = NULL;
        int     ret = 0;
        int     x_ret = 0;

        ret = dict_get_str (xl->options, "remote-host", &remote_host);
        if (ret < 0)
                goto print;

        ret = dict_get_str (xl->options, "remote-subvolume", &remote_subvol);
        if (ret < 0)
                goto print;
        ret = syncop_getxattr (xl, rootloc, &xl->options,
                        GF_XATTR_NODE_UUID_KEY, NULL, NULL);
        if (ret < 0)
                goto print;

        ret = dict_get_str (xl->options, GF_XATTR_NODE_UUID_KEY, &uuid);
        if (ret < 0)
                goto print;
print:

        x_ret = xmlTextWriterStartElement (glfsh_writer, (xmlChar *)"brick");
        XML_RET_CHECK_AND_GOTO (x_ret, xml_out);
        x_ret = xmlTextWriterWriteFormatAttribute (glfsh_writer,
                        (xmlChar *)"hostUuid", "%s", uuid?uuid:"-");
        XML_RET_CHECK_AND_GOTO (x_ret, xml_out);

        x_ret = xmlTextWriterWriteFormatElement (glfsh_writer,
                        (xmlChar *)"name", "%s:%s",
                        remote_host ? remote_host : "-",
                        remote_subvol ? remote_subvol : "-");
        XML_RET_CHECK_AND_GOTO (x_ret, xml_out);
        xmlTextWriterFlush (glfsh_writer);
xml_out:
        return ret;
}
#endif

int
glfsh_link_inode_update_loc (loc_t *loc, struct iatt *iattr)
{
        inode_t       *link_inode = NULL;
        int           ret = -1;

        link_inode = inode_link (loc->inode, NULL, NULL, iattr);
        if (link_inode == NULL)
                goto out;

        inode_unref (loc->inode);
        loc->inode = link_inode;
        ret = 0;
out:
        return ret;
}

int
glfsh_no_print_hr_heal_op_status (int ret, uint64_t num_entries, char *fmt_str)
{
        return 0;
}

int
glfsh_print_hr_heal_op_status (int ret, uint64_t num_entries, char *fmt_str)
{
        if (ret < 0 && num_entries == 0) {
                printf ("Status: %s\n", strerror (-ret));
                if (fmt_str)
                        printf ("%s -\n", fmt_str);
                goto out;
        } else if (ret == 0) {
                printf ("Status: Connected\n");
        }

        if (ret < 0) {
                if (fmt_str)
                        printf ("Status: Failed to process entries completely. "
                                "(%s)\n%s %"PRIu64"\n",
                         strerror (-ret), fmt_str, num_entries);
        } else {
                if (fmt_str)
                        printf ("%s %"PRIu64"\n", fmt_str, num_entries);
        }
out:
        printf ("\n");
        return 0;
}

int
glfsh_print_heal_op_status (int ret, uint64_t num_entries,
                            gf_xl_afr_op_t heal_op)
{
        char *fmt_str = NULL;

        if (heal_op == GF_SHD_OP_INDEX_SUMMARY)
                fmt_str = "Number of entries:";
        else if (heal_op == GF_SHD_OP_SPLIT_BRAIN_FILES)
                fmt_str = "Number of entries in split-brain:";
        else if (heal_op == GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK)
                fmt_str = "Number of healed entries:";

        return glfsh_output->print_heal_op_status (ret, num_entries, fmt_str);
}

int
glfsh_get_index_dir_loc (loc_t *rootloc, xlator_t *xl, loc_t *dirloc,
                         int32_t *op_errno, char *vgfid)
{
        void      *index_gfid = NULL;
        int       ret = 0;
        dict_t    *xattr = NULL;
        struct iatt   iattr = {0};
        struct iatt   parent = {0};

        ret = syncop_getxattr (xl, rootloc, &xattr, vgfid, NULL, NULL);
        if (ret < 0) {
                *op_errno = -ret;
                goto out;
        }

        ret = dict_get_ptr (xattr, vgfid, &index_gfid);
        if (ret < 0) {
                *op_errno = EINVAL;
                goto out;
        }

        gf_uuid_copy (dirloc->gfid, index_gfid);
        dirloc->path = "";
        dirloc->inode = inode_new (rootloc->inode->table);
        ret = syncop_lookup (xl, dirloc, &iattr, &parent, NULL, NULL);
        dirloc->path = NULL;
        if (ret < 0) {
                *op_errno = -ret;
                goto out;
        }
        ret = glfsh_link_inode_update_loc (dirloc, &iattr);
        if (ret)
                goto out;
        glfs_loc_touchup (dirloc);

        ret = 0;
out:
        if (xattr)
                dict_unref (xattr);
        return ret;
}

static xlator_t*
_get_ancestor (xlator_t *xl, gf_xl_afr_op_t heal_op)
{
        static char *replica_xl[] = {"cluster/replicate", NULL};
        static char *heal_xls[] = {"cluster/replicate", "cluster/disperse",
                                         NULL};
        char **ancestors = NULL;

        if (heal_op == GF_SHD_OP_INDEX_SUMMARY)
                ancestors = heal_xls;
        else
                ancestors = replica_xl;

        if (!xl || !xl->parents)
                return NULL;

        while (xl->parents) {
                xl = xl->parents->xlator;
                if (!xl)
                        break;
                if (gf_get_index_by_elem (ancestors, xl->type) != -1)
                        return xl;
        }

        return NULL;
}

int
glfsh_index_purge (xlator_t *subvol, inode_t *inode, char *name)
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
glfsh_print_spb_status (dict_t *dict, char *path, uuid_t gfid,
                        uint64_t *num_entries, gf_boolean_t flag)
{
        int   ret    = 0;
        gf_boolean_t    pending         = _gf_false;
        gf_boolean_t    split_b         = _gf_false;
        char            *value          = NULL;
        char            gfid_str[64]    = {0};

        ret = dict_get_str (dict, "heal-info", &value);
        if (ret)
                return 0;

        if (!strcmp (value, "split-brain")) {
                split_b = _gf_true;
        } else if (!strcmp (value, "split-brain-pending")) {
                split_b = _gf_true;
                pending = _gf_true;
        }
        /* Consider the entry only iff :
         * 1) The dir being processed is not indices/dirty, indicated by
         *    flag == _gf_false
         * 2) The dir being processed is indices/dirty but the entry also
         *    exists in indices/xattrop dir and has already been processed.
         */
        if (split_b) {
                if (!flag || (flag && !pending)) {
                        (*num_entries)++;
                        glfsh_output->print_spb_status (path ? path :
                                                uuid_utoa_r (gfid, gfid_str),
                                                gfid, NULL);
                }
        }
        return 0;
}

int
glfsh_print_heal_status (dict_t *dict, char *path, uuid_t gfid,
                         uint64_t *num_entries, gf_boolean_t ignore_dirty)
{
        int             ret             = 0;
        gf_boolean_t    pending         = _gf_false;
        char            *status         = NULL;
        char            *value          = NULL;
        char            gfid_str[64]    = {0};

        ret = dict_get_str (dict, "heal-info", &value);
        if (ret || (!strcmp (value, "no-heal")))
                return 0;

        if (!strcmp (value, "heal")) {
                ret = gf_asprintf (&status, " ");
                if (ret < 0)
                        goto out;
        } else if (!strcmp (value, "possibly-healing")) {
                ret = gf_asprintf (&status,
                                   " - Possibly undergoing heal\n");
                if (ret < 0)
                        goto out;
        } else if (!strcmp (value, "split-brain")) {
                ret = gf_asprintf (&status, " - Is in split-brain\n");
                if (ret < 0)
                        goto out;
        } else if (!strcmp (value, "heal-pending")) {
                pending = _gf_true;
                ret = gf_asprintf (&status, " ");
                if (ret < 0)
                        goto out;
        } else if (!strcmp (value, "split-brain-pending")) {
                pending = _gf_true;
                ret = gf_asprintf (&status, " - Is in split-brain\n");
                if (ret < 0)
                        goto out;
        } else if (!strcmp (value, "possibly-healing-pending")) {
                pending = _gf_true;
                ret = gf_asprintf (&status,
                                   " - Possibly undergoing heal\n");
                if (ret < 0)
                        goto out;
        }
out:
        /* If ignore_dirty is set, it means indices/dirty directory is
         * being processed. Ignore the entry if it also exists in
         * indices/xattrop.
         * Boolean pending is set to true if the entry also exists in
         * indices/xattrop directory.
         */
        if (ignore_dirty) {
                if (pending) {
                        GF_FREE (status);
                        status = NULL;
                        return 0;
                }
        }
        if (ret == -1)
                status = NULL;

        (*num_entries)++;
        glfsh_output->print_heal_status (path ? path :
                                         uuid_utoa_r (gfid, gfid_str),
                                         gfid,
                                         status ? status : "");

        GF_FREE (status);
        return 0;
}

int
glfsh_heal_status_boolean (dict_t *dict, char *path, uuid_t gfid,
                           uint64_t *num_entries, gf_boolean_t ignore_dirty)
{
        int             ret             = 0;
        char            *value          = NULL;

        ret = dict_get_str (dict, "heal-info", &value);
        if ((!ret) && (!strcmp (value, "no-heal")))
                return 0;
        else
                return -1;
}

static int
glfsh_heal_entries (glfs_t *fs, xlator_t *top_subvol, loc_t *rootloc,
                    gf_dirent_t *entries,  uint64_t *offset,
                    uint64_t *num_entries, dict_t *xattr_req) {

        gf_dirent_t      *entry          = NULL;
        gf_dirent_t      *tmp            = NULL;
        int               ret            = 0;
        char              file[64]      = {0};

        list_for_each_entry_safe (entry, tmp, &entries->list, list) {
                *offset = entry->d_off;
                if ((strcmp (entry->d_name, ".") == 0) ||
                    (strcmp (entry->d_name, "..") == 0))
                        continue;
                memset (file, 0, sizeof(file));
                snprintf (file, sizeof(file), "gfid:%s", entry->d_name);
                ret = glfsh_heal_splitbrain_file (fs, top_subvol, rootloc, file,
                                                 xattr_req);
                if (ret)
                        continue;
                (*num_entries)++;
        }

        return ret;
}

static int
glfsh_process_entries (xlator_t *xl, fd_t *fd, gf_dirent_t *entries,
                       uint64_t *offset, uint64_t *num_entries,
                       print_status glfsh_print_status,
                       gf_boolean_t ignore_dirty, glfsh_fail_mode_t mode)
{
        gf_dirent_t      *entry = NULL;
        gf_dirent_t      *tmp = NULL;
        int              ret = 0;
        int              print_status = 0;
        char            *path = NULL;
        uuid_t          gfid = {0};
        xlator_t        *this = NULL;
        dict_t          *dict = NULL;
        loc_t           loc   = {0,};
        this = THIS;

        list_for_each_entry_safe (entry, tmp, &entries->list, list) {
                *offset = entry->d_off;
                if ((strcmp (entry->d_name, ".") == 0) ||
                    (strcmp (entry->d_name, "..") == 0))
                        continue;

                if (dict) {
                        dict_unref (dict);
                        dict = NULL;
                }
                gf_uuid_clear (gfid);
                GF_FREE (path);
                path = NULL;

                gf_uuid_parse (entry->d_name, gfid);
                gf_uuid_copy (loc.gfid, gfid);
                ret = syncop_getxattr (this, &loc, &dict, GF_HEAL_INFO, NULL,
                                       NULL);
                if (ret) {
                        if ((mode != GLFSH_MODE_CONTINUE_ON_ERROR) &&
                            (ret == -ENOTCONN))
                                goto out;
                        else
                                continue;
                }

                ret = syncop_gfid_to_path (this->itable, xl, gfid, &path);

                if (ret == -ENOENT || ret == -ESTALE) {
                        glfsh_index_purge (xl, fd->inode, entry->d_name);
                        ret = 0;
                        continue;
                }
                if (dict) {
                        print_status = glfsh_print_status (dict, path, gfid,
                                                           num_entries,
                                                           ignore_dirty);
                        if ((print_status) &&
                            (mode != GLFSH_MODE_CONTINUE_ON_ERROR)) {
                                ret = -EAGAIN;
                                goto out;
                        }
                }
        }
        ret = 0;
out:
        GF_FREE (path);
        if (dict) {
                dict_unref (dict);
                dict = NULL;
        }
        return ret;
}

static int
glfsh_crawl_directory (glfs_t *fs, xlator_t *top_subvol, loc_t *rootloc,
                       xlator_t *readdir_xl, fd_t *fd, loc_t *loc,
                       dict_t *xattr_req, uint64_t *num_entries,
                       gf_boolean_t ignore)
{
        int             ret          = 0;
        int             heal_op      = -1;
        uint64_t        offset       = 0;
        gf_dirent_t     entries;
        gf_boolean_t    free_entries = _gf_false;
        glfsh_fail_mode_t mode = GLFSH_MODE_CONTINUE_ON_ERROR;

        INIT_LIST_HEAD (&entries.list);
        ret = dict_get_int32 (xattr_req, "heal-op", &heal_op);
        if (ret)
                return ret;

        if (heal_op == GF_SHD_OP_GRANULAR_ENTRY_HEAL_ENABLE)
                mode = GLFSH_MODE_EXIT_ON_FIRST_FAILURE;

        while (1) {
                ret = syncop_readdir (readdir_xl, fd, 131072, offset, &entries,
                                      NULL, NULL);
                if (ret <= 0)
                        break;
                ret = 0;
                free_entries = _gf_true;

                if (list_empty (&entries.list))
                        goto out;

                if (heal_op == GF_SHD_OP_INDEX_SUMMARY) {
                        ret = glfsh_process_entries (readdir_xl, fd,
                                                     &entries, &offset,
                                                     num_entries,
                                                     glfsh_print_heal_status,
                                                     ignore, mode);
                        if (ret < 0)
                                goto out;
                } else if (heal_op == GF_SHD_OP_SPLIT_BRAIN_FILES) {
                        ret = glfsh_process_entries (readdir_xl, fd,
                                                     &entries, &offset,
                                                     num_entries,
                                                     glfsh_print_spb_status,
                                                     ignore, mode);
                        if (ret < 0)
                                goto out;
                } else if (heal_op == GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK) {
                        ret = glfsh_heal_entries (fs, top_subvol, rootloc,
                                                  &entries, &offset,
                                                  num_entries, xattr_req);
                } else if (heal_op == GF_SHD_OP_GRANULAR_ENTRY_HEAL_ENABLE) {
                        ret = glfsh_process_entries (readdir_xl, fd, &entries,
                                                     &offset, num_entries,
                                                     glfsh_heal_status_boolean,
                                                     ignore, mode);
                        if (ret < 0)
                                goto out;
                }
                gf_dirent_free (&entries);
                free_entries = _gf_false;
        }
        ret = 0;
out:
        if (free_entries)
                gf_dirent_free (&entries);
        return ret;
}

static int
glfsh_no_print_brick_from_xl (xlator_t *xl, loc_t *rootloc)
{
        return 0;
}

static int
glfsh_print_brick_from_xl (xlator_t *xl, loc_t *rootloc)
{
        char    *remote_host = NULL;
        char    *remote_subvol = NULL;
        int     ret = 0;

        ret = dict_get_str (xl->options, "remote-host", &remote_host);
        if (ret < 0)
                goto out;

        ret = dict_get_str (xl->options, "remote-subvolume", &remote_subvol);
        if (ret < 0)
                goto out;
out:
        if (ret < 0)
                printf ("Brick - Not able to get brick information\n");
        else
                printf ("Brick %s:%s\n", remote_host, remote_subvol);
        return ret;
}

int
glfsh_print_pending_heals_type (glfs_t *fs, xlator_t *top_subvol, loc_t *rootloc,
                                xlator_t *xl, gf_xl_afr_op_t heal_op,
                                dict_t *xattr_req, char *vgfid,
                                uint64_t *num_entries)
{
        int ret = 0;
        loc_t   dirloc = {0};
        fd_t    *fd = NULL;
        int32_t op_errno = 0;
        gf_boolean_t ignore = _gf_false;

        if (!strcmp(vgfid, GF_XATTROP_DIRTY_GFID))
                ignore = _gf_true;

        ret = glfsh_get_index_dir_loc (rootloc, xl, &dirloc, &op_errno,
                                       vgfid);
        if (ret < 0) {
                if (op_errno == ESTALE || op_errno == ENOENT ||
                    op_errno == ENOTSUP)
                        ret = 0;
                else
                        ret = -op_errno;
                goto out;
        }

        ret = syncop_dirfd (xl, &dirloc, &fd, GF_CLIENT_PID_GLFS_HEAL);
        if (ret)
                goto out;

        ret = glfsh_crawl_directory (fs, top_subvol, rootloc, xl, fd, &dirloc,
                                     xattr_req, num_entries, ignore);
        if (fd)
                fd_unref (fd);
out:
        loc_wipe (&dirloc);
        return ret;
}

int
glfsh_print_pending_heals (glfs_t *fs, xlator_t *top_subvol, loc_t *rootloc,
                           xlator_t *xl, gf_xl_afr_op_t heal_op, gf_boolean_t
                           is_parent_replicate)
{
        int ret = 0;
        uint64_t count = 0, total = 0;

        dict_t *xattr_req = NULL;

        xattr_req = dict_new();
        if (!xattr_req)
                goto out;
        ret = dict_set_int32 (xattr_req, "heal-op", heal_op);
        if (ret)
                goto out;

        if ((!is_parent_replicate) &&
            ((heal_op == GF_SHD_OP_GRANULAR_ENTRY_HEAL_ENABLE) ||
             (heal_op == GF_SHD_OP_GRANULAR_ENTRY_HEAL_DISABLE))) {
                ret = 0;
                goto out;
        }

        ret = glfsh_output->print_brick_from_xl (xl, rootloc);
        if (ret < 0)
                goto out;

        ret = glfsh_print_pending_heals_type (fs, top_subvol, rootloc, xl,
                                              heal_op, xattr_req,
                                              GF_XATTROP_INDEX_GFID, &count);

        if (ret < 0 && heal_op == GF_SHD_OP_GRANULAR_ENTRY_HEAL_ENABLE)
                goto out;

        total += count;
        count = 0;
        if (ret == -ENOTCONN)
                goto out;

        if (is_parent_replicate) {
                ret = glfsh_print_pending_heals_type (fs, top_subvol,
                                                      rootloc, xl,
                                                      heal_op, xattr_req,
                                                      GF_XATTROP_DIRTY_GFID,
                                                      &count);
                total += count;
        }
out:
        if (xattr_req)
                dict_unref (xattr_req);
        glfsh_print_heal_op_status (ret, total, heal_op);
        return ret;

}

static int
glfsh_set_heal_options (glfs_t *fs, gf_xl_afr_op_t heal_op)
{
        int ret = 0;

        if ((heal_op != GF_SHD_OP_SBRAIN_HEAL_FROM_BIGGER_FILE) &&
            (heal_op != GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK) &&
            (heal_op != GF_SHD_OP_SBRAIN_HEAL_FROM_LATEST_MTIME))
                return 0;
        ret = glfs_set_xlator_option (fs, "*-replicate-*", "data-self-heal",
                                      "on");
        if (ret)
                goto out;

        ret = glfs_set_xlator_option (fs, "*-replicate-*", "metadata-self-heal",
                                      "on");
        if (ret)
                goto out;

        ret = glfs_set_xlator_option (fs, "*-replicate-*", "entry-self-heal",
                                      "on");
out:
        return ret;
}

static int
glfsh_validate_volume (xlator_t *xl, gf_xl_afr_op_t heal_op)
{
        xlator_t        *heal_xl = NULL;
        int             ret = -1;

        while (xl->next)
                xl = xl->next;

        while (xl) {
                if (strcmp (xl->type, "protocol/client") == 0) {
                        heal_xl = _get_ancestor (xl, heal_op);
                        if (heal_xl) {
                                ret = 0;
                                break;
                        }
                }

                xl = xl->prev;
        }

        return ret;
}

static xlator_t*
_brick_path_to_client_xlator (xlator_t *top_subvol, char *hostname,
                              char *brickpath)
{
        int ret             = 0;
        xlator_t *xl        = NULL;
        char *remote_host   = NULL;
        char *remote_subvol = NULL;

        xl = top_subvol;

        while (xl->next)
                xl = xl->next;

        while (xl) {
                if (!strcmp (xl->type, "protocol/client")) {
                        ret = dict_get_str (xl->options, "remote-host",
                                                    &remote_host);
                        if (ret < 0)
                                goto out;
                        ret = dict_get_str (xl->options,
                                            "remote-subvolume", &remote_subvol);
                        if (ret < 0)
                                goto out;
                        if (!strcmp (hostname, remote_host) &&
                            !strcmp (brickpath, remote_subvol))
                                return xl;
                }
                xl = xl->prev;
        }

out:
        return NULL;
}

int
glfsh_gather_heal_info (glfs_t *fs, xlator_t *top_subvol, loc_t *rootloc,
                        gf_xl_afr_op_t heal_op)
{
        int        ret       = 0;
        xlator_t  *xl        = NULL;
        xlator_t  *heal_xl   = NULL;
        xlator_t  *old_THIS  = NULL;

        xl = top_subvol;
        while (xl->next)
                xl = xl->next;
        while (xl) {
                if (strcmp (xl->type, "protocol/client") == 0) {
                        heal_xl = _get_ancestor (xl, heal_op);
                        if (heal_xl) {
                                old_THIS = THIS;
                                THIS = heal_xl;
                                ret = glfsh_print_pending_heals (fs, top_subvol,
                                                                 rootloc, xl,
                                                                 heal_op,
                                                                 !strcmp
                                                                (heal_xl->type,
                                                          "cluster/replicate"));
                                THIS = old_THIS;

                                if ((ret < 0) &&
                              (heal_op == GF_SHD_OP_GRANULAR_ENTRY_HEAL_ENABLE))
                                        goto out;
                        }
                }

                xl = xl->prev;
        }

out:
        if (heal_op != GF_SHD_OP_GRANULAR_ENTRY_HEAL_ENABLE)
                ret = 0;

        return ret;
}

int
_validate_directory (dict_t *xattr_req, char *file)
{
        int heal_op = -1;
        int ret = 0;

        ret = dict_get_int32 (xattr_req, "heal-op", &heal_op);
        if (ret)
                return ret;

        if (heal_op == GF_SHD_OP_SBRAIN_HEAL_FROM_BIGGER_FILE) {
                printf ("'bigger-file' not a valid option for directories.\n");
                ret = -1;
        } else if (heal_op == GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK) {
                printf ("'source-brick' option used on a directory (%s). "
                        "Performing conservative merge.\n", file);
        }

        return ret;
}

int
glfsh_heal_splitbrain_file (glfs_t *fs, xlator_t *top_subvol, loc_t *rootloc,
                           char *file, dict_t *xattr_req)
{
        int          ret        = -1;
        int          reval      = 0;
        loc_t        loc        = {0, };
        char        *path       = NULL;
        char        *filename   = NULL;
        struct iatt  iatt       = {0, };
        xlator_t    *xl         = top_subvol;
        dict_t      *xattr_rsp  = NULL;
        char        *sh_fail_msg = NULL;
        int32_t      op_errno   = 0;

        if (!strncmp (file, "gfid:", 5)) {
                filename = gf_strdup(file);
                path = strtok (filename, ":");
                path = strtok (NULL, ";");
                gf_uuid_parse (path, loc.gfid);
                loc.path = gf_strdup (uuid_utoa (loc.gfid));
                loc.inode = inode_new (rootloc->inode->table);
                ret = syncop_lookup (xl, &loc, &iatt, 0, xattr_req, &xattr_rsp);
                if (ret) {
                        op_errno = -ret;
                        printf ("Lookup failed on %s:%s.\n", file,
                                strerror(op_errno));
                        goto out;
                }
        } else {
                if (file[0] != '/') {
                        printf ("<FILE> must be absolute path w.r.t. the "
                                "volume, starting with '/'\n");
                        ret = -1;
                        goto out;
                }
retry:
                ret = glfs_resolve (fs, xl, file, &loc, &iatt, reval);
                ESTALE_RETRY (ret, errno, reval, &loc, retry);
                if (ret) {
                        printf("Lookup failed on %s:%s\n",
                               file, strerror (errno));
                        goto out;
                }
        }

        if (iatt.ia_type == IA_IFDIR) {
                ret = _validate_directory (xattr_req, file);
                if (ret)
                        goto out;
        }
        ret = syncop_getxattr (xl, &loc, &xattr_rsp, GF_AFR_HEAL_SBRAIN,
                               xattr_req, NULL);
        if (ret) {
                op_errno = -ret;
                printf ("Healing %s failed:%s.\n", file, strerror(op_errno));
                goto out;
        }
        ret = dict_get_str (xattr_rsp, "sh-fail-msg", &sh_fail_msg);
        if (!ret) {
                printf ("Healing %s failed: %s.\n", file, sh_fail_msg);
                ret = -1;
                goto out;
        }
        printf ("Healed %s.\n", file);
        ret = 0;
out:
        if (xattr_rsp)
                dict_unref (xattr_rsp);
        return ret;
}

int
glfsh_heal_from_brick_type (glfs_t *fs, xlator_t *top_subvol, loc_t *rootloc,
                            char *hostname, char *brickpath, xlator_t *client,
                            dict_t *xattr_req, char *vgfid,
                            uint64_t *num_entries)
{
        fd_t     *fd        = NULL;
        loc_t     dirloc    = {0};
        int32_t   op_errno  = 0;
        int       ret       = -1;

        ret = glfsh_get_index_dir_loc (rootloc, client, &dirloc,
                                       &op_errno, vgfid);
        if (ret < 0) {
                if (op_errno == ESTALE || op_errno == ENOENT)
                        ret = 0;
                else
                        ret = -op_errno;
                goto out;
        }

        ret = syncop_dirfd (client, &dirloc, &fd,
                            GF_CLIENT_PID_GLFS_HEAL);
        if (ret)
                goto out;
        ret = glfsh_crawl_directory (fs, top_subvol, rootloc, client,
                                     fd, &dirloc, xattr_req, num_entries,
                                     _gf_false);
        if (fd)
                fd_unref (fd);
out:
        loc_wipe (&dirloc);
        return ret;
}

int
glfsh_heal_from_brick (glfs_t *fs, xlator_t *top_subvol, loc_t *rootloc,
                      char *hostname, char *brickpath, char *file)
{
        int       ret       = -1;
        uint64_t   count     = 0, total = 0;
        dict_t   *xattr_req = NULL;
        xlator_t *client    = NULL;

        xattr_req = dict_new();
        if (!xattr_req)
                goto out;
        ret = dict_set_int32 (xattr_req, "heal-op",
                              GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK);
        if (ret)
                goto out;
        client = _brick_path_to_client_xlator (top_subvol, hostname, brickpath);
        if (!client) {
                printf("\"%s:%s\"- No such brick available in the volume.\n",
                       hostname, brickpath);
                ret = -1;
                goto out;
        }
        ret = dict_set_str (xattr_req, "child-name", client->name);
        if (ret)
                goto out;
        if (file)
                ret = glfsh_heal_splitbrain_file (fs, top_subvol, rootloc, file,
                                                  xattr_req);
        else {
                ret = glfsh_heal_from_brick_type (fs, top_subvol, rootloc,
                                                  hostname, brickpath,
                                                  client, xattr_req,
                                                  GF_XATTROP_INDEX_GFID,
                                                  &count);
                total += count;
                count = 0;
                if (ret == -ENOTCONN)
                        goto out;

                ret = glfsh_heal_from_brick_type (fs, top_subvol, rootloc,
                                                  hostname, brickpath,
                                                  client, xattr_req,
                                                  GF_XATTROP_DIRTY_GFID,
                                                  &count);
                total += count;
                if (ret < 0)
                        goto out;
        }
out:
        if (xattr_req)
                dict_unref (xattr_req);
        if (!file)
                glfsh_print_heal_op_status (ret, total,
                                            GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK);

        return ret;
}

int
glfsh_heal_from_bigger_file_or_mtime (glfs_t *fs, xlator_t *top_subvol,
                                      loc_t *rootloc, char *file,
                                      gf_xl_afr_op_t heal_op)
{

        int ret = -1;
        dict_t *xattr_req = NULL;

        xattr_req = dict_new();
        if (!xattr_req)
                goto out;
        ret = dict_set_int32 (xattr_req, "heal-op", heal_op);
        if (ret)
                goto out;
        ret = glfsh_heal_splitbrain_file (fs, top_subvol, rootloc, file,
                                         xattr_req);
out:
        if (xattr_req)
                dict_unref (xattr_req);
        return ret;
}

static void
cleanup (glfs_t *fs)
{
        if (!fs)
                return;
#if 0
        /* glfs fini path is still racy and crashing the program. Since
         * this program any way has to die, we are not gonna call fini
         * in the released versions. i.e. final builds. For all
         * internal testing lets enable this so that glfs_fini code
         * path becomes stable. */
        glfs_fini (fs);
#endif
}


glfsh_info_t glfsh_human_readable = {
        .init = glfsh_init,
        .print_brick_from_xl = glfsh_print_brick_from_xl,
        .print_heal_op_status = glfsh_print_hr_heal_op_status,
        .print_heal_status = glfsh_print_hr_heal_status,
        .print_spb_status = glfsh_print_hr_spb_status,
        .end = glfsh_end
};

glfsh_info_t glfsh_no_print = {
        .init = glfsh_init,
        .print_brick_from_xl = glfsh_no_print_brick_from_xl,
        .print_heal_op_status = glfsh_no_print_hr_heal_op_status,
        .print_heal_status = glfsh_no_print_hr_heal_status,
        .print_spb_status = glfsh_no_print_hr_heal_status,
        .end = glfsh_end_op_granular_entry_heal
};

#if (HAVE_LIB_XML)
glfsh_info_t glfsh_xml_output = {
        .init = glfsh_xml_init,
        .print_brick_from_xl = glfsh_print_xml_brick_from_xl,
        .print_heal_op_status = glfsh_print_xml_heal_op_status,
        .print_heal_status = glfsh_print_xml_file_status,
        .print_spb_status = glfsh_print_xml_file_status,
        .end = glfsh_xml_end
};
#endif

int
main (int argc, char **argv)
{
        glfs_t    *fs = NULL;
        int        ret = 0;
        char      *volname = NULL;
        xlator_t  *top_subvol = NULL;
        loc_t     rootloc = {0};
        char      logfilepath[PATH_MAX] = {0};
        char      *hostname = NULL;
        char      *path = NULL;
        char      *file = NULL;
        char      *op_errstr = NULL;
        gf_xl_afr_op_t heal_op = -1;

        if (argc < 2) {
                printf (USAGE_STR, argv[0]);
                ret = -1;
                goto out;
        }

        volname = argv[1];
        switch (argc) {
        case 2:
                heal_op = GF_SHD_OP_INDEX_SUMMARY;
                break;
        case 3:
                if (!strcmp (argv[2], "split-brain-info")) {
                        heal_op = GF_SHD_OP_SPLIT_BRAIN_FILES;
                } else if (!strcmp (argv[2], "xml")) {
                        heal_op = GF_SHD_OP_INDEX_SUMMARY;
                        is_xml = 1;
                } else if (!strcmp (argv[2], "granular-entry-heal-op")) {
                        heal_op = GF_SHD_OP_GRANULAR_ENTRY_HEAL_ENABLE;
                } else {
                        printf (USAGE_STR, argv[0]);
                        ret = -1;
                        goto out;
                }
                break;
        case 4:
                if ((!strcmp (argv[2], "split-brain-info"))
                                && (!strcmp (argv[3], "xml"))) {
                        heal_op = GF_SHD_OP_SPLIT_BRAIN_FILES;
                        is_xml = 1;
                } else if (!strcmp (argv[2], "bigger-file")) {
                        heal_op = GF_SHD_OP_SBRAIN_HEAL_FROM_BIGGER_FILE;
                        file = argv[3];
                } else if (!strcmp (argv[2], "latest-mtime")) {
                        heal_op = GF_SHD_OP_SBRAIN_HEAL_FROM_LATEST_MTIME;
                        file = argv[3];
                } else if (!strcmp (argv[2], "source-brick")) {
                        heal_op = GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK;
                        hostname = strtok (argv[3], ":");
                        path = strtok (NULL, ":");
                } else {
                        printf (USAGE_STR, argv[0]);
                        ret = -1;
                        goto out;
                }
                break;
        case 5:
                if (!strcmp (argv[2], "source-brick")) {
                        heal_op = GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK;
                        hostname = strtok (argv[3], ":");
                        path = strtok (NULL, ":");
                        file = argv[4];
                } else {
                        printf (USAGE_STR, argv[0]);
                        ret = -1;
                        goto out;
                }
                break;
        default:
                printf (USAGE_STR, argv[0]);
                ret = -1;
                goto out;
        }

        glfsh_output = &glfsh_human_readable;
        if (is_xml) {
#if (HAVE_LIB_XML)
                glfsh_output = &glfsh_xml_output;
#else
                /*No point doing anything, just fail the command*/
                exit (EXIT_FAILURE);
#endif

        }

        if (heal_op == GF_SHD_OP_GRANULAR_ENTRY_HEAL_ENABLE)
                glfsh_output = &glfsh_no_print;

        ret = glfsh_output->init ();
        if (ret)
                exit (EXIT_FAILURE);

        fs = glfs_new (volname);
        if (!fs) {
                ret = -errno;
                gf_asprintf (&op_errstr, "Not able to initialize volume '%s'",
                             volname);
                goto out;
        }

        if (sys_access(SECURE_ACCESS_FILE, F_OK) == 0) {
                fs->ctx->secure_mgmt = 1;
        }

        ret = glfs_set_volfile_server (fs, "unix", DEFAULT_GLUSTERD_SOCKFILE, 0);
        if (ret) {
                ret = -errno;
                gf_asprintf (&op_errstr, "Setting the volfile server failed, "
                             "%s", strerror (errno));
                goto out;
        }

        ret = glfsh_set_heal_options (fs, heal_op);
        if (ret) {
                printf ("Setting xlator heal options failed, %s\n",
                        strerror(errno));
                goto out;
        }
        snprintf (logfilepath, sizeof (logfilepath),
                  DEFAULT_HEAL_LOG_FILE_DIRECTORY"/glfsheal-%s.log", volname);
        ret = glfs_set_logging(fs, logfilepath, GF_LOG_INFO);
        if (ret < 0) {
                ret = -errno;
                gf_asprintf (&op_errstr, "Failed to set the log file path, "
                             "%s", strerror (errno));
                goto out;
        }

        ret = glfs_init (fs);
        if (ret < 0) {
                ret = -errno;
                if (errno == ENOENT) {
                        gf_asprintf (&op_errstr, "Volume %s does not exist",
                                     volname);
                } else {
                        gf_asprintf (&op_errstr, "%s: Not able to fetch "
                                     "volfile from glusterd", volname);
                }
                goto out;
        }

        top_subvol = glfs_active_subvol (fs);
        if (!top_subvol) {
                ret = -errno;
                if (errno == ENOTCONN) {
                        gf_asprintf (&op_errstr, "Volume %s is not started "
                                                 "(Or) All the bricks are not "
                                                 "running.", volname);
                }
                else {
                        gf_asprintf (&op_errstr, "%s: Not able to mount the "
                                             "volume, %s", volname,
                                             strerror (errno));
                }
                goto out;
        }

        ret = glfsh_validate_volume (top_subvol, heal_op);
        if (ret < 0) {
                ret = -EINVAL;
                gf_asprintf (&op_errstr, "Volume %s is not of type %s", volname,
                                     (heal_op == GF_SHD_OP_INDEX_SUMMARY) ?
                                     "replicate/disperse":"replicate");
                goto out;
        }
        rootloc.inode = inode_ref (top_subvol->itable->root);
        glfs_loc_touchup (&rootloc);

        switch (heal_op) {
        case GF_SHD_OP_INDEX_SUMMARY:
        case GF_SHD_OP_SPLIT_BRAIN_FILES:
        case GF_SHD_OP_GRANULAR_ENTRY_HEAL_ENABLE:
                ret = glfsh_gather_heal_info (fs, top_subvol, &rootloc,
                                              heal_op);
                break;
        case GF_SHD_OP_SBRAIN_HEAL_FROM_BIGGER_FILE:
        case GF_SHD_OP_SBRAIN_HEAL_FROM_LATEST_MTIME:
                ret = glfsh_heal_from_bigger_file_or_mtime (fs, top_subvol,
                                                   &rootloc, file, heal_op);
                        break;
        case GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK:
                ret = glfsh_heal_from_brick (fs, top_subvol, &rootloc,
                                             hostname, path, file);
                break;
        default:
                ret = -EINVAL;
                break;
        }

        glfsh_output->end (ret, NULL);
        if (ret < 0)
                ret = -ret;
        loc_wipe (&rootloc);
        glfs_subvol_done (fs, top_subvol);
        cleanup (fs);

        return ret;
out:
        if (fs && top_subvol)
                glfs_subvol_done (fs, top_subvol);
        loc_wipe (&rootloc);
        cleanup (fs);
        if (glfsh_output)
                glfsh_output->end (ret, op_errstr);
        if (op_errstr)
                GF_FREE (op_errstr);
        return ret;
}
