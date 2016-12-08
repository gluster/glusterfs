#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../tier.rc

LAST_BRICK=3
CACHE_BRICK_FIRST=4
CACHE_BRICK_LAST=5
DEMOTE_TIMEOUT=12
PROMOTE_TIMEOUT=5


LAST_BRICK=1
CACHE_BRICK=2
DEMOTE_TIMEOUT=12
PROMOTE_TIMEOUT=5
MIGRATION_TIMEOUT=10
cleanup


TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0..$LAST_BRICK}
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;

# Basic operations.
cd $M0
TEST stat .
TEST mkdir d1
TEST [ -d d1 ]
TEST touch file1
TEST [ -e file1 ]

TEST $CLI volume attach-tier $V0 replica 2 $H0:$B0/${V0}$CACHE_BRICK_FIRST $H0:$B0/${V0}$CACHE_BRICK_LAST
TEST $CLI volume set $V0 features.ctr-enabled on

#check whether the directory's and files are present on mount or not.
TEST [ -d d1 ]
TEST [ -e file1 ]

cd
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0;

tier_status ()
{
	$CLI volume tier $V0 detach status | grep progress | wc -l
}

TEST $CLI volume detach-tier $V0 start
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" tier_status
TEST $CLI volume detach-tier $V0 commit

EXPECT "0" confirm_tier_removed ${V0}${CACHE_BRICK_FIRST}

EXPECT_WITHIN $REBALANCE_TIMEOUT "0" confirm_vol_stopped $V0


cleanup
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=000000
#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=000000
