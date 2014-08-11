#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/brick0 $H0:$B0/brick1
TEST $CLI volume start $V0

sleep 5

TEST glusterfs -s $H0 --volfile-id $V0 $M0

var=`gluster volume rebalance $V0 start force`

EXPECT "0" echo $?

var1="Initiated rebalance on volume $V0. Execute \"gluster\
 volume rebalance <volume-name> status\" to check status"


echo $var | grep "$var1"

EXPECT "0" echo $?

cleanup
