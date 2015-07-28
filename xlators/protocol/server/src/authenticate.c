/*
  Copyright (c) 2007-2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/



#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <dlfcn.h>
#include <errno.h>
#include "authenticate.h"
#include "server-messages.h"

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
                gf_msg ("authenticate", GF_LOG_ERROR, 0,
                        PS_MSG_AUTHENTICATE_ERROR, "AUTHENTICATION MODULE "
                        "\"IP\" HAS BEEN REPLACED BY \"ADDR\"");
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
                gf_msg ("authenticate", GF_LOG_ERROR, 0,
                        PS_MSG_AUTHENTICATE_ERROR, "dlopen(%s): %s\n",
                        auth_file, dlerror ());
                dict_set (this, key, data_from_dynptr (NULL, 0));
                GF_FREE (auth_file);
                *error = -1;
                return -1;
        }
        GF_FREE (auth_file);

        authenticate = dlsym (handle, "gf_auth");
        if (!authenticate) {
                gf_msg ("authenticate", GF_LOG_ERROR, 0,
                        PS_MSG_AUTHENTICATE_ERROR, "dlsym(gf_auth) on %s\n",
                        dlerror ());
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
                gf_msg_debug ("authenticate", 0, "volume option validation "
                              "not specified");
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
                gf_msg ("authenticate", GF_LOG_ERROR, 0,
                        PS_MSG_VOL_VALIDATE_FAILED, "volume option validation "
                        "failed");
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
                gf_msg (xl->name, GF_LOG_ERROR, 0, PS_MSG_AUTH_INIT_FAILED,
                        "authentication init failed");
                dict_foreach (auth_modules, fini, &ret);
                ret = -1;
        }
        return ret;
}

typedef struct {
        dict_t  *iparams;
        dict_t  *cparams;
        int64_t result;
} gf_auth_args_t;

static int
gf_auth_one_method (dict_t *this, char *key, data_t *value, void *data)
{
        gf_auth_args_t  *args   = data;
        auth_handle_t   *handle = NULL;

        if (!value) {
                return 0;
        }

        handle = data_to_ptr (value);
        if (!handle || !handle->authenticate) {
                return 0;
        }

        switch (handle->authenticate (args->iparams, args->cparams)) {
        case AUTH_ACCEPT:
                if (args->result != AUTH_REJECT) {
                        args->result = AUTH_ACCEPT;
                }
                /* FALLTHROUGH */
        default:
                return 0;
        case AUTH_REJECT:
                args->result = AUTH_REJECT;
                return -1;
        }
}

auth_result_t
gf_authenticate (dict_t *input_params,
                 dict_t *config_params,
                 dict_t *auth_modules)
{
        char *name = NULL;
        data_t *peerinfo_data = NULL;
        gf_auth_args_t  args;

        args.iparams = input_params;
        args.cparams = config_params;
        args.result = AUTH_DONT_CARE;

        dict_foreach (auth_modules, gf_auth_one_method, &args);

        if (AUTH_DONT_CARE == args.result) {
                peerinfo_data = dict_get (input_params, "peer-info-name");

                if (peerinfo_data) {
                        name = peerinfo_data->data;
                }

                gf_msg ("auth", GF_LOG_ERROR, 0, PS_MSG_REMOTE_CLIENT_REFUSED,
                        "no authentication module is interested in "
                        "accepting remote-client %s", name);
                args.result = AUTH_REJECT;
        }

        return args.result;
}

void
gf_auth_fini (dict_t *auth_modules)
{
        int32_t dummy;

        dict_foreach (auth_modules, fini, &dummy);
}
