/*
  Copyright (c) 2007-2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/



#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <dlfcn.h>
#include <errno.h>
#include "authenticate.h"

static int
init (dict_t *this, char *key, data_t *value, void *data)
{
        void          *handle       = NULL;
        char          *auth_file    = NULL;
        auth_handle_t *auth_handle  = NULL;
        auth_fn_t      authenticate = NULL;
        int           *error        = NULL;
        int            ret          = 0;

        /* It gets over written */
        error = data;

        if (!strncasecmp (key, "ip", strlen ("ip"))) {
                gf_log ("authenticate", GF_LOG_ERROR,
                        "AUTHENTICATION MODULE \"IP\" HAS BEEN REPLACED "
                        "BY \"ADDR\"");
                dict_set (this, key, data_from_dynptr (NULL, 0));
                /* TODO: 1.3.x backword compatibility */
                // *error = -1;
                // return;
                key = "addr";
        }

        ret = gf_asprintf (&auth_file, "%s/%s.so", LIBDIR, key);
        if (-1 == ret) {
                dict_set (this, key, data_from_dynptr (NULL, 0));
                *error = -1;
                return -1;
        }

        handle = dlopen (auth_file, RTLD_LAZY);
        if (!handle) {
                gf_log ("authenticate", GF_LOG_ERROR, "dlopen(%s): %s\n",
                        auth_file, dlerror ());
                dict_set (this, key, data_from_dynptr (NULL, 0));
                GF_FREE (auth_file);
                *error = -1;
                return -1;
        }
        GF_FREE (auth_file);

        authenticate = dlsym (handle, "gf_auth");
        if (!authenticate) {
                gf_log ("authenticate", GF_LOG_ERROR,
                        "dlsym(gf_auth) on %s\n", dlerror ());
                dict_set (this, key, data_from_dynptr (NULL, 0));
                dlclose (handle);
                *error = -1;
                return -1;
        }

        auth_handle = GF_CALLOC (1, sizeof (*auth_handle),
                                 gf_common_mt_auth_handle_t);
        if (!auth_handle) {
                dict_set (this, key, data_from_dynptr (NULL, 0));
                *error = -1;
                dlclose (handle);
                return -1;
        }
        auth_handle->vol_opt = GF_CALLOC (1, sizeof (volume_opt_list_t),
                                          gf_common_mt_volume_opt_list_t);
        if (!auth_handle->vol_opt) {
                dict_set (this, key, data_from_dynptr (NULL, 0));
                *error = -1;
                GF_FREE (auth_handle);
                dlclose (handle);
                return -1;
        }
        auth_handle->vol_opt->given_opt = dlsym (handle, "options");
        if (auth_handle->vol_opt->given_opt == NULL) {
                gf_log ("authenticate", GF_LOG_DEBUG,
                        "volume option validation not specified");
        }

        auth_handle->authenticate = authenticate;
        auth_handle->handle = handle;

        dict_set (this, key,
                  data_from_dynptr (auth_handle, sizeof (*auth_handle)));
        return 0;
}

static int
fini (dict_t *this, char *key, data_t *value, void *data)
{
        auth_handle_t *handle = data_to_ptr (value);
        if (handle) {
                dlclose (handle->handle);
        }
        return 0;
}

static int
_gf_auth_option_validate (dict_t *d, char *k, data_t *v, void *tmp)
{
        auth_handle_t *handle = NULL;
        xlator_t      *xl = NULL;
        int ret = 0;

        xl = tmp;

        handle = data_to_ptr (v);
        if (!handle)
                return 0;

        list_add_tail (&(handle->vol_opt->list), &(xl->volume_options));

        ret = xlator_options_validate_list (xl, xl->options,
                                            handle->vol_opt, NULL);
        if (ret) {
                gf_log ("authenticate", GF_LOG_ERROR,
                        "volume option validation failed");
                return -1;
        }
        return 0;
}

int32_t
gf_auth_init (xlator_t *xl, dict_t *auth_modules)
{
        int ret = 0;

        dict_foreach (auth_modules, init, &ret);
        if (ret)
                goto out;

        ret = dict_foreach (auth_modules, _gf_auth_option_validate, xl);

out:
        if (ret) {
                gf_log (xl->name, GF_LOG_ERROR, "authentication init failed");
                dict_foreach (auth_modules, fini, &ret);
                ret = -1;
        }
        return ret;
}

static dict_t *__input_params;
static dict_t *__config_params;

int
map (dict_t *this, char *key, data_t *value, void *data)
{
        dict_t *res = data;
        auth_fn_t authenticate;
        auth_handle_t *handle = NULL;

        if (value && (handle = data_to_ptr (value)) &&
            (authenticate = handle->authenticate)) {
                dict_set (res, key,
                          int_to_data (authenticate (__input_params,
                                                     __config_params)));
        } else {
                dict_set (res, key, int_to_data (AUTH_DONT_CARE));
        }
        return 0;
}

int
reduce (dict_t *this, char *key, data_t *value, void *data)
{
        int64_t val = 0;
        int64_t *res = data;
        if (!data)
                return 0;

        val = data_to_int64 (value);
        switch (val)
        {
        case AUTH_ACCEPT:
                if (AUTH_DONT_CARE == *res)
                        *res = AUTH_ACCEPT;
                break;

        case AUTH_REJECT:
                *res = AUTH_REJECT;
                break;

        case AUTH_DONT_CARE:
                break;
        }
        return 0;
}


auth_result_t
gf_authenticate (dict_t *input_params,
                 dict_t *config_params,
                 dict_t *auth_modules)
{
        char *name = NULL;
        dict_t *results = NULL;
        int64_t result = AUTH_DONT_CARE;
        data_t *peerinfo_data = NULL;

        results = get_new_dict ();
        __input_params = input_params;
        __config_params = config_params;

        dict_foreach (auth_modules, map, results);

        dict_foreach (results, reduce, &result);
        if (AUTH_DONT_CARE == result) {
                peerinfo_data = dict_get (input_params, "peer-info-name");

                if (peerinfo_data) {
                        name = peerinfo_data->data;
                }

                gf_log ("auth", GF_LOG_ERROR,
                        "no authentication module is interested in "
                        "accepting remote-client %s", name);
                result = AUTH_REJECT;
        }

        dict_destroy (results);
        return result;
}

void
gf_auth_fini (dict_t *auth_modules)
{
        int32_t dummy;

        dict_foreach (auth_modules, fini, &dummy);
}
