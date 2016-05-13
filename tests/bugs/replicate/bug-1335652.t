#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 shard on
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 data-self-heal off
TEST $CLI volume set $V0 entry-self-heal off
TEST $CLI volume set $V0 metadata-self-heal off
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;

#Kill the zero'th brick so that 1st and 2nd get marked dirty
TEST kill_brick $V0 $H0 $B0/${V0}0

TEST dd if=/dev/urandom of=$M0/file bs=10MB count=20

#At any point value of dirty should not be greater than 0 on source bricks
EXPECT "000000000000000000000000" get_hex_xattr trusted.afr.dirty $B0/${V0}1/.shard
EXPECT "000000000000000000000000" get_hex_xattr trusted.afr.dirty $B0/${V0}2/.shard

rm -rf $M0/file;

cleanup;
