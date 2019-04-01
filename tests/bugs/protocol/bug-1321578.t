#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

check_mounted () {
	df | grep $1 | wc -l
}

CHECK_MOUNT_TIMEOUT=7

TEST glusterd
TEST $CLI volume create $V0 $H0:$B0/$V0

# Set auth.allow to dummy hostname so it *doesn't* include ourselves.
TEST $CLI volume set $V0 auth.allow example.org
TEST $CLI volume start $V0

# "System getspec" will include the username and password if the request comes
# from a server (which we are).  Unfortunately, this will cause authentication
# to succeed in auth.login regardless of whether auth.addr is working properly
# or not, which is useless to us.  To get a proper test, strip out those lines.
$CLI system getspec $V0 | sed -e /username/d -e /password/d > fubar.vol

# This mount should fail because auth.allow doesn't include us.
TEST $GFS -f fubar.vol $M0

EXPECT_WITHIN $CHECK_MOUNT_TIMEOUT 0 check_mounted $M0

# Add tests when only username is present, but not password
# "System getspec" will include the username and password if the request comes
# from a server (which we are).  Unfortunately, this will cause authentication
# to succeed in auth.login regardless of whether auth.addr is working properly
# or not, which is useless to us.  To get a proper test, strip out those lines.
$CLI system getspec $V0 | sed -e /password/d > fubar.vol

# This mount should fail because auth.allow doesn't include our password.
TEST $GFS -f fubar.vol $M0

# If we had DONT_EXPECT_WITHIN we could use that, but we don't.
EXPECT_WITHIN $CHECK_MOUNT_TIMEOUT 0 check_mounted $M0

# Now, add a test for login failure when server doesn't have the password entry
# Add tests when only username is present, but not password
# "System getspec" will include the username and password if the request comes
# from a server (which we are).  Unfortunately, this will cause authentication
# to succeed in auth.login regardless of whether auth.addr is working properly
# or not, which is useless to us.  To get a proper test, strip out those lines.
$CLI system getspec $V0 > fubar.vol
TEST $CLI volume stop $V0

sed -i -e '/password /d' /var/lib/glusterd/vols/$V0/$V0.*$V0.vol

TEST $CLI volume start $V0

# This mount should fail because auth.allow doesn't include our password.
TEST $GFS -f fubar.vol $M0

EXPECT_WITHIN $CHECK_MOUNT_TIMEOUT 0 check_mounted $M0

# Set auth.allow to include us.  This mount should therefore succeed.
TEST $CLI volume set $V0 auth.allow $H0
$CLI system getspec $V0 | sed -e /password/d > fubar.vol

TEST $GFS -f fubar.vol $M0
EXPECT_WITHIN $CHECK_MOUNT_TIMEOUT 1 check_mounted $M0

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

# Set auth.reject to include us.  This mount should therefore fail.
TEST $CLI volume stop $V0

TEST $CLI volume set $V0 auth.allow "\*"
TEST $CLI volume set $V0 auth.reject $H0
TEST $CLI volume start $V0

# Do this, so login module is not in picture
$CLI system getspec $V0 | sed -e /password/d > fubar.vol

TEST $GFS -f fubar.vol $M0
EXPECT_WITHIN $CHECK_MOUNT_TIMEOUT 0 check_mounted $M0

cleanup
