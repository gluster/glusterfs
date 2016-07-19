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

#ifndef _EXPORTS_H_
#define _EXPORTS_H_

#include "nfs-mem-types.h"
#include "dict.h"
#include "nfs.h"

#define GF_EXP GF_NFS"-exports"

#define NETGROUP_REGEX_PATTERN  "(@([a-zA-Z0-9\\(=, .])+)())"
#define HOSTNAME_REGEX_PATTERN  "[[:space:]]([a-zA-Z0-9.\\(=,*/)-]+)"
#define OPTIONS_REGEX_PATTERN   "([a-zA-Z0-9=\\.]+)"

#define NETGROUP_MAX_LEN        128
#define FQDN_MAX_LEN            256

#define SEC_OPTION_MAX          10
#define UID_MAX_LEN             6

#define DIR_MAX_LEN             1024

/* The following 2 definitions are in mount3.h
 * but we don't want to include it because mount3.h
 * depends on structs in this file so we get a cross
 * dependency.
 */
struct mount3_state;

extern struct mnt3_export *
mnt3_mntpath_to_export (struct mount3_state *ms, const char *dirpath,
                        gf_boolean_t export_parsing_match);

struct export_options {
        gf_boolean_t    rw;                /* Read-write option */
        gf_boolean_t    nosuid;            /* nosuid option */
        gf_boolean_t    root;              /* root option */
        char            *anon_uid;         /* anonuid option */
        char            *sec_type;         /* X, for sec=X */
};

struct export_item {
        char                  *name;  /* Name of the export item */
        struct export_options *opts;  /* NFS Options */
};

struct export_dir {
        char    *dir_name;      /* Directory */
        dict_t  *netgroups;     /* Dict of netgroups */
        dict_t  *hosts;         /* Dict of hosts */
};

struct exports_file {
        char    *filename;      /* Filename */
        dict_t  *exports_dict;  /* Dict of export_dir_t */
        dict_t  *exports_map;   /* Map of SuperFastHash(<export>) -> expdir */
};

void
exp_file_deinit (struct exports_file *expfile);

int
exp_file_parse (const char *filepath, struct exports_file **expfile,
                struct mount3_state *ms);

struct export_dir *
exp_file_get_dir (const struct exports_file *file, const char *dir);

struct export_item *
exp_dir_get_host (const struct export_dir *expdir, const char *host);

struct export_item *
exp_dir_get_netgroup (const struct export_dir *expdir, const char *netgroup);

struct export_dir *
exp_file_dir_from_uuid (const struct exports_file *file,
                        const uuid_t export_uuid);

#endif  /* _EXPORTS_H_ */
