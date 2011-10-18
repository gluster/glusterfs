/*
  Copyright (c) 2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
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
