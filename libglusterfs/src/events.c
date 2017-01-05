/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>
#include <netinet/in.h>
#include <netdb.h>

#include "syscall.h"
#include "mem-pool.h"
#include "glusterfs.h"
#include "globals.h"
#include "events.h"


#define EVENT_HOST "127.0.0.1"
#define EVENT_PORT 24009


int
_gf_event (eventtypes_t event, char *fmt, ...)
{
        int                ret                   = 0;
        int                sock                  = -1;
        char              *eventstr              = NULL;
        struct             sockaddr_in server;
        va_list            arguments;
        char              *msg                   = NULL;
        glusterfs_ctx_t   *ctx                   = NULL;
        char              *host                  = NULL;
        struct addrinfo    hints;
        struct addrinfo   *result                = NULL;

        /* Global context */
        ctx = THIS->ctx;

        if (event < 0 || event >= EVENT_LAST) {
                ret = EVENT_ERROR_INVALID_INPUTS;
                goto out;
        }

        /* Initialize UDP socket */
        sock = socket (AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
                ret = EVENT_ERROR_SOCKET;
                goto out;
        }

        memset (&hints, 0, sizeof (hints));
        hints.ai_family = AF_UNSPEC;

        /* Get Host name to send message */
        if (ctx && ctx->cmd_args.volfile_server) {
                /* If it is client code then volfile_server is set
                   use that information to push the events. */
                if ((getaddrinfo (ctx->cmd_args.volfile_server,
                                  NULL, &hints, &result)) != 0) {
                        ret = EVENT_ERROR_RESOLVE;
                        goto out;
                }

                if (get_ip_from_addrinfo (result, &host) == NULL) {
                        ret = EVENT_ERROR_RESOLVE;
                        goto out;
                }
        } else {
                /* Localhost, Use the defined IP for localhost */
                host = gf_strdup (EVENT_HOST);
        }

        /* Socket Configurations */
        server.sin_family = AF_INET;
        server.sin_port = htons (EVENT_PORT);
        server.sin_addr.s_addr = inet_addr (host);
        memset (&server.sin_zero, '\0', sizeof (server.sin_zero));

        va_start (arguments, fmt);
        ret = gf_vasprintf (&msg, fmt, arguments);
        va_end (arguments);

        if (ret < 0) {
                ret = EVENT_ERROR_INVALID_INPUTS;
                goto out;
        }

        ret = gf_asprintf (&eventstr, "%u %d %s",
                            (unsigned)time(NULL), event, msg);

        if (ret <= 0) {
                ret = EVENT_ERROR_MSG_FORMAT;
                goto out;
        }

        /* Send Message */
        if (sendto (sock, eventstr, strlen (eventstr),
                    0, (struct sockaddr *)&server, sizeof (server)) <= 0) {
                ret = EVENT_ERROR_SEND;
        }

        ret = EVENT_SEND_OK;

 out:
        if (sock >= 0) {
                sys_close (sock);
        }

        /* Allocated by gf_vasprintf */
        if (msg)
                GF_FREE (msg);

        /* Allocated by gf_asprintf */
        if (eventstr)
                GF_FREE (eventstr);

        if (host)
                GF_FREE (host);

        if (result)
                freeaddrinfo (result);

        return ret;
}
