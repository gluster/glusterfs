#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;
TEST glusterd
TEST pidof glusterd

#test functionality of post-op-delay-secs
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}

#Strings should not be accepted.
TEST ! $CLI volume set $V0 cluster.post-op-delay-secs abc

#-ve ints should not be accepted.
TEST ! $CLI volume set $V0 cluster.post-op-delay-secs -1

#INT_MAX+1 should not be accepted.
TEST ! $CLI volume set $V0 cluster.post-op-delay-secs 2147483648

#floats should not be accepted.
TEST ! $CLI volume set $V0 cluster.post-op-delay-secs 1.25

#min val 0 should be accepted
TEST $CLI volume set $V0 cluster.post-op-delay-secs 0
EXPECT "0" volume_option $V0 cluster.post-op-delay-secs

#max val 2147483647 should be accepted
TEST $CLI volume set $V0 cluster.post-op-delay-secs 2147483647
EXPECT "2147483647" volume_option $V0 cluster.post-op-delay-secs

#some middle val in range 2147 should be accepted
TEST $CLI volume set $V0 cluster.post-op-delay-secs 2147
EXPECT "2147" volume_option $V0 cluster.post-op-delay-secs
cleanup;
