#!/bin/bash
#### Test iatt and user xattr heal from lookup code path ####

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/brick{0,1,2}
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0

cd $M0
TEST touch file
TEST setfattr -n user.attribute1 -v "value" $B0/brick0/file
TEST kill_brick $V0 $H0 $B0/brick2
TEST chmod +x file
iatt=$(stat -c "%g:%u:%A" file)

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2

#Trigger metadataheal
TEST stat file

#iattrs must be matching
iatt1=$(stat -c "%g:%u:%A" $B0/brick0/file)
iatt2=$(stat -c "%g:%u:%A" $B0/brick1/file)
iatt3=$(stat -c "%g:%u:%A" $B0/brick2/file)
EXPECT $iatt echo $iatt1
EXPECT $iatt echo $iatt2
EXPECT $iatt echo $iatt3

#xattrs must be matching
xatt1_cnt=$(getfattr -d $B0/brick0/file|wc|awk '{print $1}')
xatt2_cnt=$(getfattr -d $B0/brick1/file|wc|awk '{print $1}')
xatt3_cnt=$(getfattr -d $B0/brick2/file|wc|awk '{print $1}')
EXPECT "$xatt1_cnt" echo $xatt2_cnt
EXPECT "$xatt1_cnt" echo $xatt3_cnt

#changelogs must be zero
xattr1=$(get_hex_xattr trusted.afr.$V0-client-2 $B0/brick0/file)
xattr2=$(get_hex_xattr trusted.afr.$V0-client-2 $B0/brick1/file)
EXPECT "000000000000000000000000" echo $xattr1
EXPECT "000000000000000000000000" echo $xattr2

cd -
cleanup;
