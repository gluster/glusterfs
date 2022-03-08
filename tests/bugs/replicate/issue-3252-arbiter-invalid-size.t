#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 arbiter 1 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 features.cache-invalidation on
TEST $CLI volume set $V0 performance.cache-invalidation on
TEST $CLI volume set $V0 features.cache-invalidation-timeout 600
TEST $CLI volume set $V0 performance.md-cache-timeout 600
TEST $CLI volume set $V0 performance.write-behind off

TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M1;

#Kill the arbiter brick and restart it to minimize the impact of setattr delay
TEST $CLI volume set $V0 delay-gen arbiter
TEST $CLI volume set $V0 delay-gen.enable setattr,fsetattr
TEST $CLI volume set $V0 delay-gen.delay-percentage 100
TEST $CLI volume set $V0 delay-gen.delay-duration 5000000

TEST kill_brick $V0 $H0 $B0/${V0}2
TEST $CLI volume start $V0 force

dd if=/dev/zero of=$M0/datafile bs=1024 count=1024
EXPECT "^0$" echo $?

TEST ls $M1/datafile
TEST touch -m -t 203012311159.59 $M0/datafile
#touch command will have a delay in arbiter xlator and the upcall will be delayed
fileSize=`stat -c '%s' $M1/datafile`
EXPECT_NOT "^0$" echo $fileSize
cleanup;
