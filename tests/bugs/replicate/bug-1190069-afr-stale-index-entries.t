#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc
cleanup;

#Stale entries in xattrop folder for files which do not need heal must be removed during the next index heal crawl.

TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1};
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume start $V0;
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
TEST `echo hello>$M0/datafile`
TEST touch $M0/mdatafile

#Create split-brain and reset the afr xattrs, so that we have only the entry inside xattrop folder.
#This is to simulate the case where the brick crashed just before pre-op happened, but index xlator created the entry inside xattrop folder.

#Create data, metadata SB.
TEST kill_brick $V0 $H0 $B0/$V0"1"
TEST stat $M0/datafile
TEST `echo append>>$M0/datafile`
TEST chmod +x $M0/mdatafile
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT '1' afr_child_up_status_meta $M0 $V0-replicate-0 1
TEST kill_brick $V0 $H0 $B0/$V0"0"
TEST stat $M0/datafile
TEST `echo append>>$M0/datafile`
TEST chmod +x $M0/mdatafile
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT '1' afr_child_up_status_meta $M0 $V0-replicate-0 0
TEST ! cat $M0/datafile

TEST $CLI volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT '1' afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT '1' afr_child_up_status_in_shd $V0 1

TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT '2' count_sh_entries $B0/$V0"0"
EXPECT_WITHIN $HEAL_TIMEOUT '2' count_sh_entries $B0/$V0"1"

#Reset xattrs and trigger heal.
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000000000000000000000 $B0/${V0}0/datafile
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000000000000000000000 $B0/${V0}1/datafile

TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000000000000000000000 $B0/${V0}0/mdatafile
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000000000000000000000 $B0/${V0}1/mdatafile

TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0
EXPECT_WITHIN $HEAL_TIMEOUT '0' count_sh_entries $B0/$V0"0"
EXPECT_WITHIN $HEAL_TIMEOUT '0' count_sh_entries $B0/$V0"1"

cleanup
