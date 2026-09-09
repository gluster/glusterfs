#!/bin/bash
#
# debug/error-gen must inject the errno configured with "error-no"
# (volume option debug.error-number).  Since efb4636ace (2017) the
# configured value was only consulted on the random-failure path; with the
# default random-failure=off every injected failure carried a random errno
# from error-gen's per-fop list instead.
#
# EIO is not in error-gen's MKDIR list, so the unpatched xlator can never
# produce it here: the assertion fails deterministically without the fix.

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
cleanup;

function mkdir_errmsg {
    mkdir $1 2>&1 | sed -n 's/.*: //p'
    return 0
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume set $V0 debug.error-gen posix
TEST $CLI volume set $V0 debug.error-fops mkdir
TEST $CLI volume set $V0 debug.error-number EIO
TEST $CLI volume set $V0 debug.error-failure 100
TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0

TEST ! mkdir $M0/d1
EXPECT "Input/output error" mkdir_errmsg $M0/d2
EXPECT "Input/output error" mkdir_errmsg $M0/d3

# The configured errno must survive a reconfigure as well.
TEST $CLI volume set $V0 debug.error-number ENOTEMPTY
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Directory not empty" mkdir_errmsg $M0/d4

cleanup;
