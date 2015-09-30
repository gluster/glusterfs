/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#include <fnmatch.h>
#include <sys/socket.h>
#include <netdb.h>
#include "authenticate.h"
#include "dict.h"
#include "rpc-transport.h"

#define ADDR_DELIMITER " ,"
#define PRIVILEGED_PORT_CEILING 1024

#ifndef AF_INET_SDP
#define AF_INET_SDP 27
#endif

auth_result_t
gf_auth (dict_t *input_params, dict_t *config_params)
{
        auth_result_t  result         = AUTH_DONT_CARE;
        int            ret            = 0;
        char          *name           = NULL;
        char          *searchstr      = NULL;
        peer_info_t   *peer_info      = NULL;
        data_t        *peer_info_data = NULL;
        data_t        *allow_addr     = NULL;
        data_t        *reject_addr    = NULL;
        char          *addr_str       = NULL;
        char          *tmp            = NULL;
        char          *addr_cpy       = NULL;
        char          *service        = NULL;
        uint16_t       peer_port      = 0;
        char           is_inet_sdp    = 0;
        char           negate         = 0;
        char           match          = 0;
        char           peer_addr[UNIX_PATH_MAX];
        char          *type           = NULL;
        gf_boolean_t   allow_insecure = _gf_false;

        name = data_to_str (dict_get (input_params, "remote-subvolume"));
        if (!name) {
                gf_log ("authenticate/addr", GF_LOG_DEBUG,
                        "remote-subvolume not specified");
                goto out;
        }

        ret = gf_asprintf (&searchstr, "auth.addr.%s.allow", name);
        if (-1 == ret) {
                gf_log ("auth/addr", GF_LOG_DEBUG,
                        "asprintf failed while setting search string");
                goto out;
        }

        allow_addr = dict_get (config_params, searchstr);
        GF_FREE (searchstr);

        ret = gf_asprintf (&searchstr, "auth.addr.%s.reject", name);
        if (-1 == ret) {
                gf_log ("auth/addr", GF_LOG_ERROR,
                        "asprintf failed while setting search string");
                goto out;
        }
        reject_addr = dict_get (config_params, searchstr);
        GF_FREE (searchstr);

        if (!allow_addr) {
                /* TODO: backword compatibility */
                ret = gf_asprintf (&searchstr, "auth.ip.%s.allow", name);
                if (-1 == ret) {
                        gf_log ("auth/addr", GF_LOG_ERROR,
                                "asprintf failed while setting search string");
                        goto out;
                }
                allow_addr = dict_get (config_params, searchstr);
                GF_FREE (searchstr);
        }

        if (!(allow_addr || reject_addr)) {
                gf_log ("auth/addr",  GF_LOG_DEBUG,
                        "none of the options auth.addr.%s.allow or "
                        "auth.addr.%s.reject specified, returning auth_dont_care",
                        name, name);
                goto out;
        }

        peer_info_data = dict_get (input_params, "peer-info");
        if (!peer_info_data) {
                gf_log ("auth/addr", GF_LOG_ERROR,
                        "peer-info not present");
                goto out;
        }

        peer_info = data_to_ptr (peer_info_data);

        switch (((struct sockaddr *) &peer_info->sockaddr)->sa_family)
        {
        case AF_INET_SDP:
                is_inet_sdp = 1;
                ((struct sockaddr *) &peer_info->sockaddr)->sa_family = AF_INET;

        case AF_INET:
        case AF_INET6:
        {
                strcpy (peer_addr, peer_info->identifier);
                service = strrchr (peer_addr, ':');
                *service = '\0';
                service ++;

                if (is_inet_sdp) {
                        ((struct sockaddr *) &peer_info->sockaddr)->sa_family = AF_INET_SDP;
                }

                ret = dict_get_str (config_params, "rpc-auth-allow-insecure",
                                    &type);
                if (ret == 0) {
                        ret = gf_string2boolean (type, &allow_insecure);
                        if (ret < 0) {
                                gf_log ("auth/addr", GF_LOG_WARNING,
                                        "rpc-auth-allow-insecure option %s "
                                        "is not a valid bool option", type);
                                goto out;
                        }
                }

                peer_port = atoi (service);
                if (peer_port >= PRIVILEGED_PORT_CEILING && !allow_insecure) {
                        gf_log ("auth/addr", GF_LOG_ERROR,
                                "client is bound to port %d which is not privileged",
                                peer_port);
                        goto out;
                }
                break;

        case AF_UNIX:
                strcpy (peer_addr, peer_info->identifier);
                break;

        default:
                gf_log ("authenticate/addr", GF_LOG_ERROR,
                        "unknown address family %d",
                        ((struct sockaddr *) &peer_info->sockaddr)->sa_family);
                goto out;
        }
        }

        if (reject_addr) {
                addr_cpy = gf_strdup (reject_addr->data);
                if (!addr_cpy)
                        goto out;

                addr_str = strtok_r (addr_cpy, ADDR_DELIMITER, &tmp);

                while (addr_str) {
                        gf_log (name,  GF_LOG_DEBUG,
                                "rejected = \"%s\", received addr = \"%s\"",
                                addr_str, peer_addr);
                        if (addr_str[0] == '!') {
                                negate = 1;
                                addr_str++;
                        }

                        match = fnmatch (addr_str, peer_addr, 0);
                        if (negate ? match : !match) {
                                result = AUTH_REJECT;
                                goto out;
                        }
                        addr_str = strtok_r (NULL, ADDR_DELIMITER, &tmp);
                }
                GF_FREE (addr_cpy);
                addr_cpy = NULL;
        }

        if (allow_addr) {
                addr_cpy = gf_strdup (allow_addr->data);
                if (!addr_cpy)
                        goto out;

                addr_str = strtok_r (addr_cpy, ADDR_DELIMITER, &tmp);

                while (addr_str) {
                        gf_log (name,  GF_LOG_DEBUG,
                                "allowed = \"%s\", received addr = \"%s\"",
                                addr_str, peer_addr);
                        if (addr_str[0] == '!') {
                                negate = 1;
                                addr_str++;
                        }

                        match = fnmatch (addr_str, peer_addr, 0);
                        if (negate ? match : !match) {
                                result = AUTH_ACCEPT;
                                goto out;
                        }
                        addr_str = strtok_r (NULL, ADDR_DELIMITER, &tmp);
                }
        }

out:
        GF_FREE (addr_cpy);

        return result;
}

struct volume_options options[] = {
        { .key   = {"auth.addr.*.allow"},
          .type  = GF_OPTION_TYPE_INTERNET_ADDRESS_LIST
        },
        { .key   = {"auth.addr.*.reject"},
          .type  = GF_OPTION_TYPE_INTERNET_ADDRESS_LIST
        },
        /* Backword compatibility */
        { .key   = {"auth.ip.*.allow"},
          .type  = GF_OPTION_TYPE_INTERNET_ADDRESS_LIST
        },
        { .key = {NULL} }
};
