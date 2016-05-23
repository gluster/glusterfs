/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* File: rio-common.h
 * Primary header for rio-common code.
 */

#ifndef _RIO_COMMON_H
#define _RIO_COMMON_H

#include "xlator.h"
#include "rio-mem-types.h"
#include "rio-messages.h"

#define RIO_SUBVOL_STRING_SEP  ":"
#define RIO_SERVER_NONE_SUBVOL "rio-server-none-subvol"

struct rio_subvol {
        struct list_head d2svl_node;
        xlator_t *d2svl_xlator; /* subvolume xlator */
};

struct rio_conf {
        /* memory pools */

        /* subvolume configuration */
        char    *d2cnf_data_subvolumes; /* string of data subvolumes */
        char    *d2cnf_metadata_subvolumes;  /* string of metadata subvolumes */
        struct rio_subvol d2cnf_dc_list; /* list of dc xlators */
        struct rio_subvol d2cnf_mdc_list; /* list of mdc xlators */
        int     d2cnf_dc_count; /* count of dc subvolumes in dc_list */
        int     d2cnf_mdc_count; /* count of dc subvolumes in mdc_list */

        /* layout */
        char    *d2cnf_layout_type_dc;  /* defines the layout type as a string
                                        permissible values are, inodehash-bucket
                                        */
        char    *d2cnf_layout_type_mdc; /* defines the layout type as a string
                                        permissible values are, static-bucket */
        struct layout *d2cnf_dclayout; /* layout class for dc */
        struct layout *d2cnf_mdclayout; /* layout class for mdc */

        /* Store the name of the local subvolume, for RIO server */
        char    *d2cnf_server_local_subvol;
        /* Store the xlator pointer for d2cnf_server_local_subvol */
        xlator_t *d2cnf_server_local_xlator;
        /* xattr specification */
        char    *d2cnf_xattr_base_name; /* xattr base name for RIO xattrs */
};

int32_t rio_common_init (xlator_t *);
void rio_common_fini (xlator_t *);

#endif /* _RIO_COMMON_H */
