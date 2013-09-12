/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __AFR_SELF_HEALD_H__
#define __AFR_SELF_HEALD_H__
#include "xlator.h"

#define IS_ROOT_PATH(path) (!strcmp (path, "/"))
#define IS_ENTRY_CWD(entry) (!strcmp (entry, "."))
#define IS_ENTRY_PARENT(entry) (!strcmp (entry, ".."))
#define AFR_ALL_CHILDREN -1

typedef struct afr_crawl_data_ {
        int                 child;
        pid_t               pid;
        afr_crawl_type_t    crawl;
        xlator_t            *readdir_xl;
        void                *op_data;
        int                 crawl_flags;
        int (*process_entry) (xlator_t *this, struct afr_crawl_data_ *crawl_data,
                              gf_dirent_t *entry, loc_t *child, loc_t *parent,
                              struct iatt *iattr);
} afr_crawl_data_t;

typedef struct crawl_event_stats_ {
        uint64_t healed_count;
        uint64_t split_brain_count;
        uint64_t heal_failed_count;
        char     *start_time_str;
        char     *end_time_str;
        char     *crawl_type;
        gf_boolean_t crawl_inprogress;
} shd_crawl_event_t;

void _destroy_crawl_event_data (void *data);
void _destroy_shd_event_data (void *data);

typedef int (*process_entry_cbk_t) (xlator_t *this, afr_crawl_data_t *crawl_data,
                              gf_dirent_t *entry, loc_t *child, loc_t *parent,
                              struct iatt *iattr);

void afr_build_root_loc (xlator_t *this, loc_t *loc);

int afr_set_root_gfid (dict_t *dict);

void
afr_proactive_self_heal (void *data);

int
afr_xl_op (xlator_t *this, dict_t *input, dict_t *output);

/*
 * In addition to its self-heal use, this is used to find a local default
 * read_child.
 */
int
afr_local_pathinfo (char *pathinfo, gf_boolean_t *local);
#endif /* __AFR_SELF_HEALD_H__ */
