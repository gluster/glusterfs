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
#include "syscall.h"
#include "mem-pool.h"
#include "events.h"

int
gf_event (int event, char *fmt, ...)
{
        int      ret                      = 0;
#if (USE_EVENTS)
        int      sock                     = -1;
        char     eventstr[EVENTS_MSG_MAX] = "";
        struct   sockaddr_un server;
        va_list  arguments;
        char     *msg                     = NULL;
        size_t   eventstr_size            = 0;

        if (event < 0 || event >= EVENT_LAST) {
                ret = EVENT_ERROR_INVALID_INPUTS;
                goto out;
        }

        sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock < 0) {
                ret = EVENT_ERROR_SOCKET;
                goto out;
        }
        server.sun_family = AF_UNIX;
        strcpy(server.sun_path, EVENT_PATH);

        if (connect(sock,
                    (struct sockaddr *) &server,
                    sizeof(struct sockaddr_un)) < 0) {
                ret = EVENT_ERROR_CONNECT;
                goto out;
        }

        va_start (arguments, fmt);
        ret = gf_vasprintf (&msg, fmt, arguments);
        va_end (arguments);
        if (ret < 0) {
                ret = EVENT_ERROR_INVALID_INPUTS;
                goto out;
        }

        eventstr_size = snprintf(NULL, 0, "%u %d %s", (unsigned)time(NULL),
                                 event, msg);

        if (eventstr_size + 1 > EVENTS_MSG_MAX) {
                eventstr_size = EVENTS_MSG_MAX - 1;
        }

        snprintf(eventstr, eventstr_size+1, "%u %d %s",
                 (unsigned)time(NULL), event, msg);

        if (sys_write(sock, eventstr, strlen(eventstr)) <= 0) {
                ret = EVENT_ERROR_SEND;
                goto out;
        }

        ret = EVENT_SEND_OK;

 out:
        sys_close(sock);
        GF_FREE(msg);
#endif
        return ret;
}
