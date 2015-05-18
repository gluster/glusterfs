/*
  Copyright (c) 2007-2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _AUTHENTICATE_H
#define _AUTHENTICATE_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <fnmatch.h>
#include "dict.h"
#include "compat.h"
#include "list.h"
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
