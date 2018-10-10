#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

#This .t tests that the fds from client are closed on brick when gluster volume
#stop is executed in brick-mux setup.

cleanup;
TEST glusterd
TEST pidof glusterd

function keep_fd_open {
#This function has to be run as background job because opening the fd in
#foreground and running commands is leading to flush calls on these fds
#which is making it very difficult to create the race where fds will be left
#open even after the brick dies.
    exec 5>$M1/a
    exec 6>$M1/b
    while [ -f $M0/a ]; do sleep 1; done
}

function count_open_files {
    local brick_pid="$1"
    local pattern="$2"
    ls -l /proc/$brick_pid/fd | grep -i "$pattern" | wc -l
}

TEST $CLI volume set all cluster.brick-multiplex on
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume create $V1 replica 2 $H0:$B0/${V1}{2,3}
#Have same configuration on both bricks so that they are multiplexed
#Delay flush fop for a second
TEST $CLI volume heal $V0 disable
TEST $CLI volume heal $V1 disable
TEST $CLI volume set $V0 delay-gen posix
TEST $CLI volume set $V0 delay-gen.enable flush
TEST $CLI volume set $V0 delay-gen.delay-percentage 100
TEST $CLI volume set $V0 delay-gen.delay-duration 1000000
TEST $CLI volume set $V1 delay-gen posix
TEST $CLI volume set $V1 delay-gen.enable flush
TEST $CLI volume set $V1 delay-gen.delay-percentage 100
TEST $CLI volume set $V1 delay-gen.delay-duration 1000000

TEST $CLI volume start $V0
TEST $CLI volume start $V1

TEST $GFS -s $H0 --volfile-id=$V0 --direct-io-mode=enable $M0
TEST $GFS -s $H0 --volfile-id=$V1 --direct-io-mode=enable $M1

TEST touch $M0/a
keep_fd_open &
TEST $CLI volume profile $V1 start
brick_pid=$(get_brick_pid $V1 $H0 $B0/${V1}2)
TEST count_open_files $brick_pid "$B0/${V1}2/a"
TEST count_open_files $brick_pid "$B0/${V1}2/b"
TEST count_open_files $brick_pid "$B0/${V1}3/a"
TEST count_open_files $brick_pid "$B0/${V1}3/b"

#If any other flush fops are introduced into the system other than the one at
#cleanup it interferes with the race, so test for it
EXPECT "^0$" echo "$($CLI volume profile $V1 info incremental | grep -i flush | wc -l)"
#Stop the volume
TEST $CLI volume stop $V1

#Wait for cleanup resources or volume V1
EXPECT_WITHIN $GRAPH_SWITCH_TIMEOUT "^0$" count_open_files $brick_pid "$B0/${V1}2/a"
EXPECT_WITHIN $GRAPH_SWITCH_TIMEOUT "^0$" count_open_files $brick_pid "$B0/${V1}2/b"
EXPECT_WITHIN $GRAPH_SWITCH_TIMEOUT "^0$" count_open_files $brick_pid "$B0/${V1}3/a"
EXPECT_WITHIN $GRAPH_SWITCH_TIMEOUT "^0$" count_open_files $brick_pid "$B0/${V1}3/b"

TEST rm -f $M0/a #Exit keep_fd_open()
wait

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M1

cleanup
