#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

#Create a distributed volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1..2};
TEST $CLI volume start $V0

# Mount FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0

#Create a file and perform fop on a DIR
TEST touch $M0/foo

function xattr_query_check() {
    local path=$1

    local ret=`getfattr -m . -d $path 2>&1 | grep -c 'trusted.glusterfs'`
    echo $ret
}

function set_xattr() {
    local path=$1
    local xa_name=$2
    local xa_val=$3

    setfattr -n $xa_name -v $xa_val $path
    echo $?
}

function remove_xattr() {
    local path=$1
    local xa_name=$2

    setfattr -x $xa_name $path
    echo $?
}

EXPECT 0 xattr_query_check $M0/
EXPECT 0 xattr_query_check $M0/foo

EXPECT 1 set_xattr $M0/ 'trusted.glusterfs.volume-id' 'foo'
EXPECT 1 remove_xattr $M0/ 'trusted.glusterfs.volume-id'


## Finish up
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
