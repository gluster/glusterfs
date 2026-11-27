#!/bin/bash
#
# Regression test for #3918 (integration side): IPv6 host:port parsing on the
# real CLI -> glusterd brick path.
#
# A bracketed IPv6 brick "[<addr>]:<path>" must be accepted by the CLI and
# recorded by glusterd with the bare address -- brackets stripped, every hextet
# intact. On the unpatched get_host_name() / gf_set_volfile_server_common()
# splitters the CLI rejects "[::1]" ("internet address '[::1]' does not conform
# to standards"), so the volume create fails and this test fails -- catching the
# gluster-11 IPv6 regression (#4269 / #4643 / #4670) in situ.
#
# Uses IPv6 loopback (::1); skips where that is unavailable.
#
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

if ! ip -6 addr show dev lo 2>/dev/null | grep -q 'inet6 ::1'; then
    echo "Skipping: IPv6 loopback (::1) not available" >&2
    SKIP_TESTS
    exit 0
fi

TEST glusterd
TEST pidof glusterd

# Bracketed IPv6 brick. Fails to parse (CLI rejects) on the unpatched code.
TEST $CLI volume create $V0 "[::1]:$B0/${V0}0" force
EXPECT 'Created' volinfo_field $V0 'Status'

# glusterd must have stored the bare "::1" -- not "[::1]", not a truncated ":".
brick_host=$($CLI volume info $V0 | awk -F': ' '/^Brick1:/{print $2}' | sed 's|:/.*||')
TEST [ "x$brick_host" = "x::1" ]

cleanup;
