#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

check_mounted () {
	df | grep $1 | wc -l
}

get_addresses () {
	ip addr | sed -n '/.*inet \([0-9.]*\).*/s//\1/p' | tr '\n' ','
}

TEST glusterd
TEST $CLI volume create $V0 $H0:$B0/$V0

# Set auth.allow so it *doesn't* include ourselves.
TEST $CLI volume set $V0 auth.allow 1.2.3.4
TEST $CLI volume start $V0

# "System getspec" will include the username and password if the request comes
# from a server (which we are).  Unfortunately, this will cause authentication
# to succeed in auth.login regardless of whether auth.addr is working properly
# or not, which is useless to us.  To get a proper test, strip out those lines.
$CLI system getspec $V0 | sed -e /username/d -e /password/d > fubar.vol

# This mount should fail because auth.allow doesn't include us.
TEST $GFS -f fubar.vol $M0
# If we had DONT_EXPECT_WITHIN we could use that, but we don't.
sleep 10
EXPECT 0 check_mounted $M0

# Set auth.allow to include us.  This mount should therefore succeed.
TEST $CLI volume set $V0 auth.allow "$(get_addresses)"
TEST $GFS -f fubar.vol $M0
sleep 10
EXPECT 1 check_mounted $M0

cleanup
