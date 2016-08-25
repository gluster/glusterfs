#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2};
TEST $CLI volume start $V0;
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status

#Create base entry in indices/xattrop
echo "Data">$M0/file

TEST $CLI volume heal $V0
#Entries from indices/xattrop should not be cleared after a heal.
EXPECT 1 count_index_entries  $B0/$V0"1"
EXPECT 1 count_index_entries  $B0/$V0"2"

TEST kill_brick $V0 $H0 $B0/${V0}2
echo "More data">>$M0/file

EXPECT 1 echo `$CLI volume heal $V0 statistics heal-count|grep "Number of entries:"|head -n1|awk '{print $4}'`

cleanup;
