#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/brick
EXPECT 'Created' volinfo_field $V0 'Status';
TEST $CLI volume start $V0

# Running with a locale not using '.' as decimal separator should work
export LC_NUMERIC=sv_SE.utf8
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

# As should a locale using '.' as a decimal separator
export LC_NUMERIC=C
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

cleanup
