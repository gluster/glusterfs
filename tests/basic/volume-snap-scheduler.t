#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd;
TEST pidof glusterd;

TEST $CLI volume create $V0 replica 2 $H0:$B0/${GMV0}{1,2,3,4};
TEST $CLI volume start $V0

## Create, start and mount meta_volume as
## snap_scheduler expects shared storage to be enabled.
## This test is very basic in nature not creating any snapshot
## and purpose is to validate snap scheduling commands.

TEST $CLI volume create $META_VOL replica 3 $H0:$B0/${META_VOL}{1,2,3};
TEST $CLI volume start $META_VOL
TEST mkdir -p $META_MNT
TEST glusterfs -s $H0 --volfile-id $META_VOL $META_MNT

##function to check status
function check_status_scheduler()
{
     local key=$1
     snap_scheduler.py status | grep -F "$key" | wc -l
}

##Basic snap_scheduler command test init/enable/disable/list

TEST snap_scheduler.py init

TEST snap_scheduler.py enable

EXPECT 1 check_status_scheduler "Enabled"

TEST snap_scheduler.py disable

EXPECT 1 check_status_scheduler "Disabled"

TEST snap_scheduler.py list

TEST $CLI volume stop $V0;

TEST $CLI volume delete $V0;

cleanup;
