/*
  Copyright (c) 2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
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
        struct list_head speclist;
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
