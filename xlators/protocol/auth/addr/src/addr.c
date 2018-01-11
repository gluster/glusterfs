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

#define ENTRY_DELIMITER     ","
#define ADDR_DELIMITER      "|"
#define PRIVILEGED_PORT_CEILING 1024

#ifndef AF_INET_SDP
#define AF_INET_SDP 27
#endif

/* An option for subdir validation be like below */

/* 1. '*'
   2. '192.168.*'
   3. '
   4. '!10.10.1*' (Today as per the code, if negate is set on one entry, its never reset)
   5. '192.168.1.*, 10.1.10.*';168.168.2.* =/dir;* =/another-dir'

*/

int
compare_addr_and_update (char *option_str, char *peer_addr, char *subvol,
                         char *delimiter,
                         auth_result_t *result, auth_result_t status)
{
        char          *addr_str       = NULL;
        char          *tmp            = NULL;
        char           negate         = 0;
        char           match          = 0;
        int            length         = 0;
        int            ret            = 0;

        addr_str = strtok_r (option_str, delimiter, &tmp);

        while (addr_str) {
                gf_log (subvol,  GF_LOG_INFO,
                        "%s = \"%s\", received addr = \"%s\"",
                        (status == AUTH_ACCEPT) ? "allowed" : "rejected",
                        addr_str, peer_addr);
                if (addr_str[0] == '!') {
                        negate = 1;
                        addr_str++;
                }

                length = strlen(addr_str);
                if ((addr_str[0] != '*') &&
                    valid_host_name (addr_str, length)) {
                        match = gf_is_same_address(addr_str, peer_addr);
                        if (match) {
                                *result = status;
                                goto out;
                        }
                } else {
                        match = fnmatch (addr_str, peer_addr, 0);
                        if (negate ? match : !match) {
                                *result = status;
                                goto out;
                        }
                }

                addr_str = strtok_r (NULL, delimiter, &tmp);
        }

        ret = -1;
out:
        return ret;
}


void
parse_entries_and_compare (char *option_str, char *peer_addr, char *subvol,
                           char *subdir, auth_result_t *result, auth_result_t status)
{
        char *entry = NULL;
        char *entry_cpy = NULL;
        char *directory = NULL;
        char *entries = NULL;
        char *addr_str = NULL;
        char *addr = NULL;
        char *tmp = NULL;
        char *tmpdir = NULL;
        int   ret = 0;

        if (!subdir) {
                gf_log (subvol, GF_LOG_WARNING,
                        "subdir entry not present, not performing any operation.");
                goto out;
        }

        entries = gf_strdup (option_str);
        if (!entries)
                goto out;

        if (entries[0] != '/' && !strchr (entries, '(')) {
                /* Backward compatible option */
                ret = compare_addr_and_update (entries, peer_addr, subvol,
                                               ",", result, status);
                goto out;
        }

        entry = strtok_r (entries, ENTRY_DELIMITER, &tmp);
        while (entry) {
                entry_cpy = gf_strdup (entry);
                if (!entry_cpy) {
                        goto out;
                }

                directory = strtok_r (entry_cpy, "(", &tmpdir);
                if (directory[0] != '/')
                        goto out;

                /* send second portion, after ' =' if directory matches */
                if (strcmp (subdir, directory))
                        goto next_entry;

                addr_str = strtok_r (NULL, ")", &tmpdir);
                if (!addr_str)
                        goto out;

                addr = gf_strdup (addr_str);
                if (!addr)
                        goto out;

                gf_log (subvol, GF_LOG_INFO, "Found an entry for dir %s (%s),"
                        " performing validation", subdir, addr);

                ret = compare_addr_and_update (addr, peer_addr, subvol,
                                               ADDR_DELIMITER, result, status);
                if (ret == 0) {
                        break;
                }

                GF_FREE (addr);
                addr = NULL;

        next_entry:
                entry = strtok_r (NULL, ENTRY_DELIMITER, &tmp);
                GF_FREE (entry_cpy);
                entry_cpy = NULL;
        }

out:
        GF_FREE (entries);
        GF_FREE (entry_cpy);
        GF_FREE (addr);
}

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
        char          *service        = NULL;
        uint16_t       peer_port      = 0;
        char           peer_addr[UNIX_PATH_MAX] = {0,};
        char          *type           = NULL;
        gf_boolean_t   allow_insecure = _gf_false;
        char          *subdir         = NULL;

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
                /* TODO: backward compatibility */
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


        ret = dict_get_str (input_params, "subdir-mount", &subdir);
        if (ret) {
                subdir = "/";
        }

        peer_info = data_to_ptr (peer_info_data);

        switch (((struct sockaddr *) &peer_info->sockaddr)->sa_family) {
        case AF_INET_SDP:
        case AF_INET:
        case AF_INET6:
                strcpy (peer_addr, peer_info->identifier);
                service = strrchr (peer_addr, ':');
                *service = '\0';
                service++;

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
                        result = AUTH_REJECT;
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

        if (reject_addr) {
                parse_entries_and_compare (reject_addr->data, peer_addr, name,
                                           subdir, &result, AUTH_REJECT);
                if (result == AUTH_REJECT)
                        goto out;
        }

        if (allow_addr) {
                parse_entries_and_compare (allow_addr->data, peer_addr, name,
                                           subdir, &result, AUTH_ACCEPT);
        }

out:
        return result;
}

struct volume_options options[] = {
        { .key   = {"auth.addr.*.allow"},
          .type  = GF_OPTION_TYPE_INTERNET_ADDRESS_LIST,
          .default_value = "*",
          .description   = "List of addresses to be allowed to access volume",
          .op_version    = {1},
          .flags         = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
          .tags          = {},
          /* option_validation_fn validate_fn; */
        },
        { .key   = {"auth.addr.*.reject"},
          .type  = GF_OPTION_TYPE_INTERNET_ADDRESS_LIST,
          .default_value = "*",
          .description   = "List of addresses to be rejected to access volume",
          .op_version    = {1},
          .flags         = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
          .tags          = {},
          /* option_validation_fn validate_fn; */
        },
        /* Backword compatibility */
        { .key   = {"auth.ip.*.allow"},
          .type  = GF_OPTION_TYPE_INTERNET_ADDRESS_LIST,
          .default_value = "*",
          .description   = "List of addresses to be allowed to access volume",
          .op_version    = {1},
          .flags         = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
          .tags          = {},
          /* option_validation_fn validate_fn; */
        },
        { .key = {NULL} }
};
