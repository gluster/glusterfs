/*
  Copyright (c) 2007-2010 Gluster, Inc. <http://www.gluster.com>
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


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <fnmatch.h>
#include <sys/socket.h>
#include <netdb.h>
#include "authenticate.h"
#include "dict.h"

#define ADDR_DELIMITER " ,"
#define PRIVILEGED_PORT_CEILING 1024

#ifndef AF_INET_SDP
#define AF_INET_SDP 27
#endif

/* TODO: duplicate declaration */
typedef struct peer_info {
        struct sockaddr_storage sockaddr;
        socklen_t sockaddr_len;
        char identifier[UNIX_PATH_MAX];
}peer_info_t;

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

                peer_port = atoi (service);
                if (peer_port >= PRIVILEGED_PORT_CEILING) {
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
        if (addr_cpy)
                GF_FREE (addr_cpy);

        return result;
}

struct volume_options options[] = {
        { .key   = {"auth.addr.*.allow"},
          .type  = GF_OPTION_TYPE_ANY
        },
        { .key   = {"auth.addr.*.reject"},
          .type  = GF_OPTION_TYPE_ANY
        },
        /* Backword compatibility */
        { .key   = {"auth.ip.*.allow"},
          .type  = GF_OPTION_TYPE_ANY
        },
        { .key = {NULL} }
};
