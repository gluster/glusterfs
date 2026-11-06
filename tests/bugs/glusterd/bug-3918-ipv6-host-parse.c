/*
  Copyright (c) 2026 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/*
 * Regression test for #3918: IPv6 host:port splitting in common-utils.c.
 *
 * get_host_name() split a host token on its LAST ':', which for an
 * unbracketed IPv6 literal is part of the address, lopping off the final
 * hextet (ff00::1 -> "ff00:") and, in gf_set_volfile_server_common(), taking
 * IPv6 bricks/mounts offline (regression from gluster 11, see #4269/#4643).
 *
 * This drives the exported get_host_name() over a matrix that pins the fixed
 * behaviour: bare and bracketed IPv6 literals keep every hextet, brackets are
 * stripped, and IPv4/hostname/host:path parsing is unchanged. Exit non-zero on
 * any mismatch so the .t wrapper fails.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Declared in <glusterfs/common-utils.h>; declared locally here so the tester
 * builds standalone (the internal header chain pulls atomic.h, which needs
 * build-time SIZEOF_* defines). Resolved from libglusterfs at link time. */
char *
get_host_name(char *word, char **host);

static int failures = 0;

/* get_host_name() mutates 'word' in place and returns a pointer into it (or
 * NULL). Expect NULL is encoded as a NULL 'expect'. */
static void
check(const char *word, const char *expect)
{
    char *dup = strdup(word);
    char *host = NULL;
    char *rv = get_host_name(dup, &host);
    const char *got = (rv && host) ? host : NULL;

    int ok = (expect == NULL) ? (got == NULL)
                              : (got != NULL && strcmp(got, expect) == 0);
    if (!ok) {
        fprintf(stderr, "FAIL: get_host_name(\"%s\") -> %s%s%s, expected %s%s%s\n",
                word, got ? "\"" : "", got ? got : "NULL", got ? "\"" : "",
                expect ? "\"" : "", expect ? expect : "NULL", expect ? "\"" : "");
        failures++;
    }
    free(dup);
}

int
main(void)
{
    /* bare IPv6 literals: the whole address is the host (the #3918 bug lopped
     * the final hextet) */
    check("ff00::1", "ff00::1");
    check("::1", "::1");
    check("2620:0000:1111:2222:dddd:cccc:bbbb:aaaa",
          "2620:0000:1111:2222:dddd:cccc:bbbb:aaaa");
    check("::ffff:1.2.3.4", "::ffff:1.2.3.4"); /* IPv4-mapped is a v6 literal */

    /* scoped (zone-ID) literals, RFC 4007: address kept whole, bare or
     * bracketed (inet_pton rejects the "%zone" suffix, so this is only handled
     * once gf_is_ipv6_addr validates the address part) */
    check("fe80::1%lo", "fe80::1%lo");
    check("[fe80::1%lo]", "fe80::1%lo");

    /* bracketed literals: brackets stripped, address intact */
    check("[ff00::1]", "ff00::1");
    check("[ff00::1]:/export/brick", "ff00::1");
    check("[2620:0000:1111:2222:dddd:cccc:bbbb:aaaa]:24007",
          "2620:0000:1111:2222:dddd:cccc:bbbb:aaaa");

    /* IPv6 host:path — the trailing ':/path' is a real delimiter, so the last
     * ':' split already worked here; must keep working (no regression) */
    check("ff00::1:/export/brick", "ff00::1");

    /* IPv4 / hostname: last ':' really is the delimiter — unchanged */
    check("1.2.3.4", NULL);            /* no ':' -> NULL, as before */
    check("1.2.3.4:24007", "1.2.3.4");
    check("myhost:/export/brick", "myhost");
    check("myhost:24007", "myhost");
    check("myhost", NULL);             /* no ':' -> NULL */

    /* malformed brackets are rejected (NULL), not returned as a bad host */
    check("[]", NULL);

    if (failures) {
        fprintf(stderr, "%d get_host_name() case(s) failed\n", failures);
        return 1;
    }
    printf("all get_host_name() IPv6 host:port cases passed\n");
    return 0;
}
