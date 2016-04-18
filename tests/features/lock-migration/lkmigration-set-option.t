#!/bin/bash
# Test to check
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

#Check lock-migration set option sanity
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/brick1 $H0:$B0/brick2
TEST $CLI volume start $V0

TEST $CLI volume set $V0 lock-migration on
EXPECT "on" echo `$CLI volume info | grep lock-migration | awk '{print $2}'`
TEST $CLI volume set $V0 lock-migration off
EXPECT "off" echo `$CLI volume info | grep lock-migration | awk '{print $2}'`
TEST ! $CLI volume set $V0 lock-migration garbage
#make sure it is still off
EXPECT "off" echo `$CLI volume info | grep lock-migration | awk '{print $2}'`


TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;


#create a afr volume and make sure option setting fails

TEST $CLI volume create $V0 replica 2 $H0:$B0/brick1 $H0:$B0/brick2
TEST $CLI volume start $V0

TEST ! $CLI volume set $V0 lock-migration on

cleanup;
