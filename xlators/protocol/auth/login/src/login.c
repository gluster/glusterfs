/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <fnmatch.h>
#include "authenticate.h"

auth_result_t gf_auth (dict_t *input_params, dict_t *config_params)
{
        auth_result_t  result  = AUTH_DONT_CARE;
        int             ret             = 0;
        data_t          *allow_user     = NULL;
        data_t          *username_data  = NULL;
        data_t          *passwd_data    = NULL;
        data_t          *password_data  = NULL;
        char            *username       = NULL;
        char            *password       = NULL;
        char            *brick_name     = NULL;
        char            *searchstr      = NULL;
        char            *username_str   = NULL;
        char            *tmp            = NULL;
        char            *username_cpy   = NULL;
        gf_boolean_t    using_ssl       = _gf_false;

        username_data = dict_get (input_params, "ssl-name");
        if (username_data) {
                gf_log ("auth/login", GF_LOG_INFO,
                        "connecting user name: %s", username_data->data);
                using_ssl = _gf_true;
        }
        else {
                username_data = dict_get (input_params, "username");
                if (!username_data) {
                        gf_log ("auth/login", GF_LOG_DEBUG,
                                "username not found, returning DONT-CARE");
                        goto out;
                }
                password_data = dict_get (input_params, "password");
                if (!password_data) {
                        gf_log ("auth/login", GF_LOG_WARNING,
                                "password not found, returning DONT-CARE");
                        goto out;
                }
                password = data_to_str (password_data);
        }
        username = data_to_str (username_data);

        brick_name = data_to_str (dict_get (input_params, "remote-subvolume"));
        if (!brick_name) {
                gf_log ("auth/login", GF_LOG_ERROR,
                        "remote-subvolume not specified");
                result = AUTH_REJECT;
                goto out;
        }

        ret = gf_asprintf (&searchstr, "auth.login.%s.%s", brick_name,
                           using_ssl ? "ssl-allow" : "allow");
        if (-1 == ret) {
                gf_log ("auth/login", GF_LOG_WARNING,
                        "asprintf failed while setting search string, "
                        "returning DONT-CARE");
                goto out;
        }

        allow_user = dict_get (config_params, searchstr);
        GF_FREE (searchstr);

        if (allow_user) {
                gf_log ("auth/login", GF_LOG_INFO,
                        "allowed user names: %s", allow_user->data);
                /*
                 * There's a subtle difference between SSL and non-SSL behavior
                 * if we can't match anything in the "while" loop below.
                 * Intuitively, we should AUTH_REJECT if there's no match.
                 * However, existing code depends on allowing untrusted users
                 * to connect with *no credentials at all* by falling through
                 * the loop.  They're still distinguished from trusted users
                 * who do provide a valid username and password (in fact that's
                 * pretty much the only thing we use non-SSL login auth for),
                 * but they are allowed to connect.  It's wrong, but it's not
                 * worth changing elsewhere.  Therefore, we do the sane thing
                 * only for SSL here.
                 *
                 * For SSL, if there's a list *you must be on it*.  Note that
                 * if there's no list we don't care.  In that case (and the
                 * ssl-allow=* case as well) authorization is effectively
                 * disabled, though authentication and encryption are still
                 * active.
                 */
                if (using_ssl) {
                        result = AUTH_REJECT;
                }
                username_cpy = gf_strdup (allow_user->data);
                if (!username_cpy)
                        goto out;

                username_str = strtok_r (username_cpy, " ,", &tmp);

                /*
                 * We have to match a user's *authenticated* name to one in the
                 * list.  If we're using SSL, they're already authenticated.
                 * Otherwise, they need a matching password to complete the
                 * process.
                 */
                while (username_str) {
                        if (!fnmatch (username_str, username, 0)) {
                                if (using_ssl) {
                                        result = AUTH_ACCEPT;
                                        break;
                                }
                                ret = gf_asprintf (&searchstr,
                                                   "auth.login.%s.password",
                                                   username);
                                if (-1 == ret) {
                                        gf_log ("auth/login", GF_LOG_WARNING,
                                                "asprintf failed while setting search string");
                                        goto out;
                                }
                                passwd_data = dict_get (config_params, searchstr);
                                GF_FREE (searchstr);

                                if (!passwd_data) {
                                        gf_log ("auth/login", GF_LOG_ERROR,
                                                "wrong username/password combination");
                                        result = AUTH_REJECT;
                                        goto out;
                                }

                                result = !((strcmp (data_to_str (passwd_data),
                                                    password)) ?
                                           AUTH_ACCEPT :
                                           AUTH_REJECT);
                                if (result == AUTH_REJECT)
                                        gf_log ("auth/login", GF_LOG_ERROR,
                                                "wrong password for user %s",
                                                username);

                                break;
                        }
                        username_str = strtok_r (NULL, " ,", &tmp);
                }
        }

out:
        GF_FREE (username_cpy);

        return result;
}

struct volume_options options[] = {
        { .key   = {"auth.login.*.allow"},
          .type  = GF_OPTION_TYPE_ANY
        },
        { .key   = {"auth.login.*.password"},
          .type  = GF_OPTION_TYPE_ANY
        },
        { .key = {NULL} }
};
