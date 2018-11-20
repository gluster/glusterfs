#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# When shard and cloudsync xlators enabled on a volume, shard xlator
# should be an ancestor of cloudsync. This testcase is to check this condition.

cleanup;
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/b1 $H0:$B0/b2 $H0:$B0/b3

volfile=$(gluster system:: getwd)"/vols/$V0/trusted-$V0.tcp-fuse.vol"

#Test that both shard and cloudsync are not loaded
EXPECT "N" volgen_volume_exists $volfile $V0-shard features shard
EXPECT "N" volgen_volume_exists $volfile $V0-cloudsync features cloudsync

#Enable shard and cloudsync in that order and check if volfile is correct
TEST $CLI volume set $V0 shard on
TEST $CLI volume set $V0 cloudsync on

#Test that both shard and cloudsync are loaded
EXPECT "Y" volgen_volume_exists $volfile $V0-shard features shard
EXPECT "Y" volgen_volume_exists $volfile $V0-cloudsync features cloudsync

EXPECT "Y" volgen_check_ancestry $volfile features shard features cloudsync

#Disable shard and cloudsync
TEST $CLI volume set $V0 shard off
TEST $CLI volume set $V0 cloudsync off

#Test that both shard and cloudsync are not loaded
EXPECT "N" volgen_volume_exists $volfile $V0-shard features shard
EXPECT "N" volgen_volume_exists $volfile $V0-cloudsync features cloudsync

#Enable cloudsync and shard in that order and check if volfile is correct
TEST $CLI volume set $V0 cloudsync on
TEST $CLI volume set $V0 shard on

#Test that both shard and cloudsync are loaded
EXPECT "Y" volgen_volume_exists $volfile $V0-shard features shard
EXPECT "Y" volgen_volume_exists $volfile $V0-cloudsync features cloudsync

EXPECT "Y" volgen_check_ancestry $volfile features shard features cloudsync

cleanup;
