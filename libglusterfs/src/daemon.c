/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include "daemon.h"

int
os_daemon_return (int nochdir, int noclose)
{
	pid_t   pid  = -1;
	int     ret  = -1;
        FILE    *ptr = NULL;

	ret = fork();
	if (ret)
		return ret;

	pid = setsid();

	if (pid == -1) {
                ret = -1;
		goto out;
        }

	if (!nochdir)
		ret = chdir("/");

        if (!noclose) {
                ptr = freopen (DEVNULLPATH, "r", stdin);
                if (!ptr)
                        goto out;

                ptr = freopen (DEVNULLPATH, "w", stdout);
                if (!ptr)
                        goto out;

                ptr = freopen (DEVNULLPATH, "w", stderr);
                if (!ptr)
                        goto out;
	}

        ret = 0;
out:
	return ret;
}

int
os_daemon (int nochdir, int noclose)
{
	int ret = -1;

	ret = os_daemon_return (nochdir, noclose);
	if (ret <= 0)
		return ret;

	_exit (0);
}
