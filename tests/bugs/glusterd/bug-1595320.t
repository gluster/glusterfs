#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc

cleanup

function count_up_bricks {
        $CLI --xml volume status $V0 | grep '<status>1' | wc -l
}

function count_brick_processes {
        pgrep glusterfsd | wc -l
}

# Setup 3 LVMS
LVM_PREFIX="test"
TEST init_n_bricks 3
TEST setup_lvm 3

# Start glusterd
TEST glusterd
TEST pidof glusterd

# Create volume and enable brick multiplexing
TEST $CLI volume create $V0 $H0:$L1 $H0:$L2 $H0:$L3
TEST $CLI v set all cluster.brick-multiplex on

# Start the volume
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 3 count_up_bricks
EXPECT 1 count_brick_processes

# Kill volume ungracefully
brick_pid=`pgrep glusterfsd`

# Make sure every brick root should be consumed by a brick process
n=`ls -lrth /proc/$brick_pid/fd | grep -iw $L1 | grep -v ".glusterfs" | wc -l`
TEST [ $n -eq 1 ]
n=`ls -lrth /proc/$brick_pid/fd | grep -iw $L2 | grep -v ".glusterfs" | wc -l`
TEST [ $n -eq 1 ]
n=`ls -lrth /proc/$brick_pid/fd | grep -iw $L3 | grep -v ".glusterfs" | wc -l`
TEST [ $n -eq 1 ]

b1_pid_file=$(ls $GLUSTERD_PIDFILEDIR/vols/$V0/*d-backends-1*.pid)
b2_pid_file=$(ls $GLUSTERD_PIDFILEDIR/vols/$V0/*d-backends-2*.pid)
b3_pid_file=$(ls $GLUSTERD_PIDFILEDIR/vols/$V0/*d-backends-3*.pid)

kill -9 $brick_pid
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 0 count_brick_processes

# Unmount 3rd brick root from node
brick_root=$L3
_umount_lv 3

# Start the volume only 2 brick should be start
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 2 count_up_bricks
EXPECT 1 count_brick_processes

brick_pid=`pgrep glusterfsd`

# Make sure only two brick root should be consumed by a brick process
n=`ls -lrth /proc/$brick_pid/fd | grep -iw $L1 | grep -v ".glusterfs" | wc -l`
TEST [ $n -eq 1 ]
n=`ls -lrth /proc/$brick_pid/fd | grep -iw $L2 | grep -v ".glusterfs" | wc -l`
TEST [ $n -eq 1 ]
n=`ls -lrth /proc/$brick_pid/fd | grep -iw $L3 | grep -v ".glusterfs" | wc -l`
TEST [ $n -eq 0 ]

# Mount the brick root
TEST mkdir -p $brick_root
TEST mount -t xfs -o nouuid  /dev/test_vg_3/brick_lvm $brick_root

# Replace brick_pid file to test brick_attach code
TEST cp $b1_pid_file $b3_pid_file

# Start the volume all brick should be up
TEST $CLI volume start $V0 force

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 3 count_up_bricks
EXPECT 1 count_brick_processes

# Make sure every brick root should be consumed by a brick process
n=`ls -lrth /proc/$brick_pid/fd | grep -iw $L1 | grep -v ".glusterfs" | wc -l`
TEST [ $n -eq 1 ]
n=`ls -lrth /proc/$brick_pid/fd | grep -iw $L2 | grep -v ".glusterfs" | wc -l`
TEST [ $n -eq 1 ]
n=`ls -lrth /proc/$brick_pid/fd | grep -iw $L3 | grep -v ".glusterfs" | wc -l`
TEST [ $n -eq 1 ]

cleanup
