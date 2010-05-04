/*
  Copyright (c) 2007-2009 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _AUTHENTICATE_H
#define _AUTHENTICATE_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <fnmatch.h>
#include "dict.h"
#include "compat.h"
#include "list.h"
#include "transport.h"
#include "xlator.h"

typedef enum {
	AUTH_ACCEPT,
	AUTH_REJECT,
	AUTH_DONT_CARE
} auth_result_t;

typedef auth_result_t (*auth_fn_t) (dict_t *input_params, 
				    dict_t *config_params);

typedef struct {
	void              *handle;
	auth_fn_t          authenticate;
	volume_opt_list_t *vol_opt;
} auth_handle_t;

auth_result_t gf_authenticate (dict_t *input_params, 
			       dict_t *config_params, 
			       dict_t *auth_modules);
int32_t gf_auth_init (xlator_t *xl, dict_t *auth_modules);
void gf_auth_fini (dict_t *auth_modules);

#endif /* _AUTHENTICATE_H */
