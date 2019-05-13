#!/bin/bash

SCRIPT_TIMEOUT=300

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../fileio.rc
cleanup;

TEST glusterd;
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0};
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.open-behind off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume start $V0
TEST $GFS -s $H0 --volfile-id=$V0  $M0;

TEST touch $M0/a

#When all bricks are up, lock and unlock should succeed
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST flock -x $fd1
TEST fd_close $fd1

#When all bricks are down, lock/unlock should fail
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST $CLI volume stop $V0
TEST ! flock -x $fd1
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" client_connected_status_meta $M0 $V0-client-0
TEST fd_close $fd1

#When a brick goes down and comes back up operations on fd which had locks on it should succeed by default
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST flock -x $fd1
TEST $CLI volume stop $V0
sleep 2
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" client_connected_status_meta $M0 $V0-client-0
TEST fd_write $fd1 "data"
TEST fd_close $fd1

#When a brick goes down and comes back up operations on fd which had locks on it should fail when client.strict-locks is on
TEST $CLI volume set $V0 client.strict-locks on
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST flock -x $fd1
TEST $CLI volume stop $V0
sleep 2
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" client_connected_status_meta $M0 $V0-client-0
TEST ! fd_write $fd1 "data"
TEST fd_close $fd1

cleanup
