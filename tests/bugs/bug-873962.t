#!/bin/bash

#AFR TEST-IDENTIFIER SPLIT-BRAIN
. $(dirname $0)/../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

B0_hiphenated=`echo $B0 | tr '/' '-'`
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2}

#Make sure self-heal is not triggered when the bricks are re-started
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume start $V0
TEST glusterfs -s $H0 --volfile-id=$V0 $M0
TEST glusterfs -s $H0 --volfile-id=$V0 $M1
TEST cd $M0
TEST touch a
TEST touch b
TEST touch c
TEST touch d
echo "1" > b
echo "1" > d
kill -9 `cat /var/lib/glusterd/vols/$V0/run/$H0$B0_hiphenated-${V0}2.pid`
echo "1" > a
echo "1" > c
TEST setfattr -n trusted.mdata -v abc b
TEST setfattr -n trusted.mdata -v abc d
TEST $CLI volume start $V0 force
sleep 5
kill -9 `cat /var/lib/glusterd/vols/$V0/run/$H0$B0_hiphenated-${V0}1.pid`
echo "2" > a
echo "2" > c
TEST setfattr -n trusted.mdata -v def b
TEST setfattr -n trusted.mdata -v def d
TEST $CLI volume start $V0 force
sleep 10

#Files are in split-brain, so open should fail
TEST ! cat a;
TEST ! cat $M1/a;
TEST ! cat b;
TEST ! cat $M1/b;

#Reset split-brain status
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000000000000000000000 $B0/${V0}1/a;
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000000000000000000000 $B0/${V0}1/b;

sleep 1
#The operations should do self-heal and give correct output
EXPECT "2" cat a;
EXPECT "2" cat $M1/a;
EXPECT "def" getfattr -n trusted.mdata --only-values b 2>/dev/null
EXPECT "def" getfattr -n trusted.mdata --only-values $M1/b 2>/dev/null

TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
sleep 5 #Wait for vol file to be updated

sleep 1
#Files are in split-brain, so open should fail
TEST ! cat c
TEST ! cat $M1/c
TEST ! cat d
TEST ! cat $M1/d

TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000000000000000000000 $B0/${V0}1/c
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000000000000000000000 $B0/${V0}1/d

sleep 1
#The operations should NOT do self-heal but give correct output
EXPECT "2" cat c
EXPECT "2" cat $M1/c
EXPECT "1" cat d
EXPECT "1" cat $M1/d

#Check that the self-heal is not triggered.
EXPECT "1" cat $B0/${V0}1/c
EXPECT "abc" getfattr -n trusted.mdata --only-values $B0/${V0}1/d 2>/dev/null
cd
cleanup;
