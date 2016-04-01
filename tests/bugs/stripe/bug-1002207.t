#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume create $V0 stripe 2 $H0:$B0/${V0}{1,2,3,4,5,6,7,8};

## Verify volume is is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST glusterfs --attribute-timeout=0 --entry-timeout=0 -s $H0 --volfile-id=$V0 $M0;
TEST dd if=/dev/zero of=$M0/file$i.data bs=1024 count=1024;

function xattr_query_check()
{
    local path=$1
    local xa_name=$2

    local ret=$(getfattr -n $xa_name $path 2>&1 | grep -o "$xa_name: No such attribute" | wc -l)
    echo $ret
}

function set_xattr()
{
    local path=$1
    local xa_name=$2
    local xa_val=$3

    setfattr -n $xa_name -v $xa_val $path
    echo $?
}

EXPECT 0 set_xattr $M0/file$i.data "trusted.name" "testofafairlylongxattrstringthatbutnotlongenoughtofailmemoryallocation"
EXPECT 0 xattr_query_check $M0/file$i.data "trusted.name"

## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=000000
#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=000000
