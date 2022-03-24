#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 arbiter 1 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal on
TEST $CLI volume profile $V0 start

TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M1;
dd if=/dev/zero of=$M0/datafile bs=1024 count=1024
EXPECT "^0$" echo $?

#Perform a lookup from second client to validate the metadata

$CLI volume profile $V0 info clear>>/dev/null
TEST ls $M1/datafile
#Check whether SETATTR present in the profile, a normal lookup should not trigger setattr unless client side heal is performed
setattrCount=`$CLI volume profile $V0 info increment | grep SETATTR | awk '{print $8}'|tr -d '\n'`
EXPECT "^$" echo $setattrCount

cleanup;
