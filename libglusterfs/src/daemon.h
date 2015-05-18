/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _DAEMON_H
#define _DAEMON_H

#define DEVNULLPATH "/dev/null"

int os_daemon_return(int nochdir, int noclose);
int os_daemon(int nochdir, int noclose);
#endif /*_DAEMON_H */
