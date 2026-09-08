#!/bin/bash
#
# Regression test for #3918: IPv6 host:port splitting in common-utils.c.
#
# get_host_name() (and the gf_set_volfile_server_common / getspec-list splitters
# that share its IPv6 detection) split a host token on its LAST ':', which for
# an unbracketed IPv6 literal is part of the address -- lopping off the final
# hextet and taking IPv6 bricks/mounts offline (a gluster-11 regression, see
# #4269/#4643/#4670). This is a pure-parse unit check: it drives the exported
# get_host_name() over an IPv6/IPv4/hostname matrix, no cluster required.
#
. $(dirname $0)/../../include.rc

cleanup;

TESTER_SRC=$(dirname $0)/bug-3918-ipv6-host-parse.c
TESTER=$(dirname $0)/bug-3918-ipv6-host-parse

# get_host_name() lives in libglusterfs. -lgfapi makes build_tester pull the
# gluster libdir from pkg-config; -lglusterfs resolves the symbol itself.
TEST build_tester $TESTER_SRC -lgfapi -lglusterfs
TEST $TESTER

cleanup_tester $TESTER

cleanup;
