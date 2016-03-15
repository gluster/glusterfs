#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../nfs.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2 stripe 2 $H0:$B0/${V0}{1,2,3,4,5,6,7,8};
TEST $CLI volume set $V0 nfs.disable false

TEST $CLI volume start $V0;

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" nfs_up_status

## Mount FUSE
TEST $GFS -s $H0 --volfile-id $V0 $M0;

##Wait for connection establishment between nfs server and brick process
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;

## Mount NFS
TEST mount_nfs $H0:/$V0 $N0 nolock;

TEST $CLI volume status all
TEST $CLI volume status $V0

function test_nfs_cmds () {
    local ret=0
    declare -a nfs_cmds=("clients" "mem" "inode" "callpool")
    for cmd in ${nfs_cmds[@]}; do
        $CLI volume status $V0 nfs $cmd
        (( ret += $? ))
    done
    return $ret
}

function test_shd_cmds () {
    local ret=0
    declare -a shd_cmds=("mem" "inode" "callpool")
    for cmd in ${shd_cmds[@]}; do
        $CLI volume status $V0 shd $cmd
        (( ret += $? ))
    done
    return $ret
}

function test_brick_cmds () {
    local ret=0
    declare -a cmds=("detail" "clients" "mem" "inode" "fd" "callpool")
    for cmd in ${cmds[@]}; do
        for i in {1..2}; do
            $CLI volume status $V0 $H0:$B0/${V0}$i $cmd
            (( ret += $? ))
        done
    done
    return $ret
}

TEST test_shd_cmds;
TEST test_nfs_cmds;
TEST test_brick_cmds;


## Before killing daemon to avoid deadlocks
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

cleanup;
