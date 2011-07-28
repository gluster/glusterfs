/*
  Copyright (c) 2011 Gluster, Inc. <http://www.gluster.com>
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

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int
os_daemon (int nochdir, int noclose)
{
	pid_t   pid = -1;
	int     ret = -1;
        FILE    *ptr = NULL;

	ret = fork();

        switch (ret) {
        case -1:
                return (-1);
        case 0:
                break;
        default:
                _exit(0);
        }

	pid = setsid();

	if (pid == -1) {
                ret = -1;
		goto out;
        }

	if (!nochdir)
		ret = chdir("/");

        if (!noclose) {
                ptr = freopen ("/dev/null", "r", stdin);
                ptr = freopen ("/dev/null", "w", stdout);
                ptr = freopen ("/dev/null", "w", stderr);
	}

        ret = 0;
out:
	return ret;
}
