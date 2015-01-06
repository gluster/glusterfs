/*
   Copyright (c) 2011-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#define MB_HIVE "mb_hive"

typedef enum {
        SET_SUB = 1,
        SET_SUPER,
        SET_EQUAL,
        SET_INTERSECT
} gf_setrel_t;

struct gf_mount_pattern {
        char **components;
        gf_setrel_t condition;
        gf_boolean_t negative;
};
typedef struct gf_mount_pattern gf_mount_pattern_t;

struct gf_mount_spec {
        struct cds_list_head speclist;
        char *label;
        gf_mount_pattern_t *patterns;
        size_t len;
};
typedef struct gf_mount_spec gf_mount_spec_t;


int parse_mount_pattern_desc (gf_mount_spec_t *mspec, char *pdesc);

int make_georep_mountspec (gf_mount_spec_t *mspec, const char *volname,
                           char *user);
int make_ghadoop_mountspec (gf_mount_spec_t *mspec, const char *volname,
                            char *user, char *server);

int glusterd_do_mount (char *label, dict_t *argdict, char **path, int *op_errno);
