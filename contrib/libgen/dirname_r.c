/*	$OpenBSD: dirname.c,v 1.13 2005/08/08 08:05:33 espie Exp $	*/

/*
 * Copyright (c) 1997, 2004 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * MT-SAFE by Kaleb S. KEITHLEY, Red Hat Inc., kkeithle@redhat.com
 */

#if 0
#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/lib/libc/gen/dirname.c,v 1.8.2.1.6.1 2010/12/21 17:09:25 kensmith Exp $");
#endif

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <pthread.h>

static pthread_key_t dname_key;

static void
make_dname_key(void)
{
      (void) pthread_key_create(&dname_key, free);
}

char *
dirname_r(const char *path)
{
	static pthread_once_t dname_key_once = PTHREAD_ONCE_INIT;

	size_t len;
	const char *endp;
	char *dname;

	(void) pthread_once(&dname_key_once, make_dname_key);

	if ((dname = pthread_getspecific(dname_key)) == NULL) {
		dname = (char *)malloc(MAXPATHLEN);
		if (dname == NULL)
			return(NULL);
		(void) pthread_setspecific(dname_key, dname);
	}

	/* Empty or NULL string gets treated as "." */
	if (path == NULL || *path == '\0') {
		dname[0] = '.';
		dname[1] = '\0';
		return (dname);
	}

	/* Strip any trailing slashes */
	endp = path + strlen(path) - 1;
	while (endp > path && *endp == '/')
		endp--;

	/* Find the start of the dir */
	while (endp > path && *endp != '/')
		endp--;

	/* Either the dir is "/" or there are no slashes */
	if (endp == path) {
		dname[0] = *endp == '/' ? '/' : '.';
		dname[1] = '\0';
		return (dname);
	} else {
		/* Move forward past the separating slashes */
		do {
			endp--;
		} while (endp > path && *endp == '/');
	}

	len = endp - path + 1;
	if (len >= MAXPATHLEN) {
		errno = ENAMETOOLONG;
		return (NULL);
	}
	memcpy(dname, path, len);
	dname[len] = '\0';
	return (dname);
}
