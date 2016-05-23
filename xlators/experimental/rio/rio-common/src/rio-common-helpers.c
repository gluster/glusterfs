/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* File: rio-common-helpers.c
 * This file contains common rio helper routines
 */

#include "glusterfs.h"
#include "rio-common-helpers.h"

/* This function is a simple node allocator and appending
it to the given list */
int
rio_add_subvol_child (xlator_t *child, struct list_head *head)
{
        int ret = -1;
        struct rio_subvol *newnode;

        newnode = GF_CALLOC (1, sizeof (struct rio_subvol),
                             gf_rio_mt_rio_subvol);
        if (!newnode) {
                goto out;
        }

        newnode->d2svl_xlator = child;
        list_add_tail (&newnode->d2svl_node, head);

        ret = 0;
out:
        return ret;
}

/* IN:
        this: the current xlator
        subvol_str: String of the format "subvol_name:subvol_name:..."
   OUT:
        head populated with parsed subvol information as linked
        struct rio_subvol nodes
        ret is the number of subvolumes that were parsed, -1 on errors

This function parses the subvol string, and finds the appropriate subvol from
the passed in xlators children, and populates the struct rio_subvol
appropriately and inserts it into the passed in list.

errno is set appropriately.
No in args check, all values are assumed to be non-NULL
error return will not cleanup partial work done
*/
int
rio_create_subvol_list (xlator_t *this, char *subvol_str,
                        struct list_head *head)
{
        int ret = 0, count = 0, first_done = 0;
        char *svstr_copy;
        char *subvol, *next_subvol;
        char *saveptr, *first_subvol = NULL;
        xlator_t *child, *first_child = NULL;
        xlator_list_t *children;

        svstr_copy = gf_strdup (subvol_str);
        if (!svstr_copy) {
                errno = ENOMEM;
                return -1;
        }

        /* TODO: HACK
        Currently sorting the given subvol list is a hack, so that we get
        idempotent layouts on all clients and servers. Ideally, we need a
        subvol ID, that can be stored against a layout, in the absence of
        that, and as we create hand written vol files, to get idempotent
        layouts, we sort the subvol names, and name the server side subvol
        the same as the client side subvols for rio client and server. Further
        the server side subvol list is in the same order as the client except
        for the first subvol.
        This hack is handled in storing and using first_subvol/child */
        for (subvol = strtok_r (svstr_copy, RIO_SUBVOL_STRING_SEP, &saveptr);
                subvol; subvol = next_subvol) {
                next_subvol = strtok_r (NULL, RIO_SUBVOL_STRING_SEP, &saveptr);

                /* find subvol in current children */
                for (children = this->children; children;
                     children = children->next) {
                        child = children->xlator;
                        if (strcmp (subvol, child->name) == 0) {
                                if (!first_subvol) {
                                        first_subvol = subvol;
                                        first_child = child;
                                        break;
                                }

                                if (!first_done &&
                                    strcmp(first_subvol, subvol) < 0) {
                                        ret = rio_add_subvol_child (
                                                first_child, head);
                                        if (ret)
                                                goto out;
                                        count++;
                                        first_done = 1;
                                }
                                /* add to list and update found count */
                                ret = rio_add_subvol_child (child, head);
                                if (ret)
                                        goto out;
                                count++;
                                break;
                        }
                }
                /* TODO: If subvol is not found in the list, then error out */
        }

        if (!first_done) {
                /* add to list and update found count */
                ret = rio_add_subvol_child (first_child, head);
                if (ret)
                        goto out;
                count++;
        }
out:
        GF_FREE (svstr_copy);
        ret = ret ? ret : count;
        return ret;
}

/* IN:
        this: the current xlator
        conf: rio_conf structure, with the subvolume strings
   OUT:
        conf: populated with the subvolumes list
        ret: 0 on success, -1 on errors

This function populates rio conf with the subvolume xlator lists
split between the DC and the MDC as appropriate. It requires that the
subvolumes strings are already populated in the conf.

errno is set on appropriately
No args check, expects arguments are non-NULL
On error no cleanup is done, and potentially partially filled data is returned
*/
int
rio_process_volume_lists (xlator_t *this, struct rio_conf *conf)
{
        int ret;

        INIT_LIST_HEAD (&conf->d2cnf_dc_list.d2svl_node);
        INIT_LIST_HEAD (&conf->d2cnf_mdc_list.d2svl_node);

        ret = rio_create_subvol_list (this, conf->d2cnf_data_subvolumes,
                                      &conf->d2cnf_dc_list.d2svl_node);
        if (ret == -1 || ret == 0) {
                /* no volumes in either data or metadata lists is an error */
                ret = -1;
                goto out;
        } else {
                conf->d2cnf_dc_count = ret;
                ret = 0;
        }

        ret = rio_create_subvol_list (this, conf->d2cnf_metadata_subvolumes,
                                      &conf->d2cnf_mdc_list.d2svl_node);
        if (ret == -1 || ret == 0) {
                /* no volumes in either data or metadata lists is an error */
                ret = -1;
                goto out;
        } else {
                conf->d2cnf_mdc_count = ret;
                ret = 0;
        }

        /* TODO: check that the subvolumes are mutually exclusive */
out:
        return ret;
}

void
rio_destroy_volume_lists (struct rio_conf *conf)
{
        struct rio_subvol *node, *tmp;

        list_for_each_entry_safe (node, tmp, &conf->d2cnf_dc_list.d2svl_node,
                                  d2svl_node) {
                list_del_init (&node->d2svl_node);
                GF_FREE (node);
        }

        list_for_each_entry_safe (node, tmp, &conf->d2cnf_mdc_list.d2svl_node,
                                  d2svl_node) {
                list_del_init (&node->d2svl_node);
                GF_FREE (node);
        }

        return;
}
