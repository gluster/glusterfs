/*
   Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>

   Based on http://www.opensource.apple.com/source/Libc/Libc-583/gen/FreeBSD/daemon.c
 */

/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

int
os_daemon_return(nochdir, noclose)
	int nochdir, noclose;
{
	struct sigaction osa, sa;
	int fd;
	pid_t newgrp;
	int oerrno;
	int osa_ok;
	int ret;

	/* A SIGHUP may be thrown when the parent exits below. */
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	osa_ok = sigaction(SIGHUP, &sa, &osa);

	ret = fork();
	if (ret)
		return ret;

	newgrp = setsid();
	oerrno = errno;
	if (osa_ok != -1)
		sigaction(SIGHUP, &osa, NULL);

	if (newgrp == -1) {
		errno = oerrno;
		return (-1);
	}

	if (!nochdir)
		(void)chdir("/");

        /* Was using '_PATH_DEVNULL' earlier. As it was not defined in solaris,
           changed to hardcoded path (and removed '#include <paths.h>' too) */
	if (!noclose && (fd = open("/dev/null", O_RDWR, 0)) != -1) {
		(void)dup2(fd, STDIN_FILENO);
		(void)dup2(fd, STDOUT_FILENO);
		(void)dup2(fd, STDERR_FILENO);
		if (fd > 2)
			(void)close(fd);
	}
	return (0);
}

int
os_daemon(int nochdir, int noclose)
{
	int ret;

	ret = os_daemon_return(nochdir, noclose);
	if (ret <= 0)
		return ret;

	_exit(0);
}
