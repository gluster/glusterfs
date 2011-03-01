/*
  Copyright (c) 2006-2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _GLUSTERD_VOLGEN_H_
#define _GLUSTERD_VOLGEN_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterd.h"

#define MARKER_VOL_KEY "monitor.xtime-marker"

int glusterd_create_rb_volfiles (glusterd_volinfo_t *volinfo,
                                 glusterd_brickinfo_t *brickinfo);

int glusterd_create_volfiles (glusterd_volinfo_t *volinfo);

void glusterd_get_nfs_filepath (char *filename);

int glusterd_create_nfs_volfile ();

int glusterd_delete_volfile (glusterd_volinfo_t *volinfo,
                             glusterd_brickinfo_t *brickinfo);

int glusterd_volinfo_get (glusterd_volinfo_t *volinfo, char *key, char **value);

int glusterd_validate_globalopts (glusterd_volinfo_t *volinfo, dict_t *val_dict, char **op_errstr);

int glusterd_validate_localopts (dict_t *val_dict, char **op_errstr);
gf_boolean_t glusterd_check_globaloption (char *key);

#endif
