/*
   Copyright 2014-present Facebook. All Rights Reserved

   This file is part of GlusterFS.

   Author :
   Shreyas Siravara <shreyas.siravara@gmail.com>

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _NETGROUPS_H
#define _NETGROUPS_H

#include "nfs-mem-types.h"
#include "dict.h"
#include "nfs.h"

#define GF_NG GF_NFS"-netgroup"

#define NG_FILE_PARSE_REGEX "([a-zA-Z0-9.(,)-]+)"
#define NG_HOST_PARSE_REGEX "([a-zA-Z0-9.-]+)"

struct netgroup_host {
        char *hostname;         /* Hostname of entry */
        char *user;             /* User field in the entry */
        char *domain;           /* Domain field in the entry */
};

struct netgroup_entry {
        char    *netgroup_name;         /* Name of the netgroup */
        dict_t  *netgroup_ngs;          /* Dict of netgroups in this netgroup */
        dict_t  *netgroup_hosts;        /* Dict of hosts in this netgroup. */
};

struct netgroups_file {
        char    *filename;         /* Filename on disk */
        dict_t  *ng_file_dict;   /* Dict of netgroup entries */
};

struct netgroups_file *
ng_file_parse (const char *filepath);

struct netgroup_entry *
ng_file_get_netgroup (const struct netgroups_file *ngfile,
                      const char *netgroup);

void
ng_file_deinit (struct netgroups_file *ngfile);

#endif /* _NETGROUPS_H */
