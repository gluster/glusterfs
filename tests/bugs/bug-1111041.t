#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../fileio.rc
. $(dirname $0)/../nfs.rc

cleanup;

function is_snapd_running {
         $CLI volume status $1 | grep "Snapshot Daemon" | wc -l;
}

TEST glusterd;

TEST pidof glusterd;

TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1

TEST $CLI volume start $V0;

EXPECT "0" is_snapd_running $v0

TEST $CLI volume set $V0 features.uss enable;

EXPECT "1" is_snapd_running $V0

SNAPD_PID=$(ps aux | grep snapd | grep -v grep | awk '{print $2}');

TEST [ $SNAPD_PID -gt 0 ];

SNAPD_PID2=$($CLI volume status $V0 | grep "Snapshot Daemon" | awk {'print $7'});

TEST [ $SNAPD_PID -eq $SNAPD_PID2 ]

cleanup  ;
