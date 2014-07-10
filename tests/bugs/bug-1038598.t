#!/bin/bash
.  $(dirname $0)/../include.rc
.  $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2  $H0:$B0/${V0}{1,2};

function hard_limit()
{
        local QUOTA_PATH=$1;
        $CLI volume quota $V0 list $QUOTA_PATH | grep "$QUOTA_PATH" | awk '{print $2}'
}

function soft_limit()
{
        local QUOTA_PATH=$1;
        $CLI volume quota $V0 list $QUOTA_PATH | grep "$QUOTA_PATH" | awk '{print $3}'
}

function usage()
{
        local QUOTA_PATH=$1;
        $CLI volume quota $V0 list $QUOTA_PATH | grep "$QUOTA_PATH" | awk '{print $4}'
}

function sl_exceeded()
{
        local QUOTA_PATH=$1;
        $CLI volume quota $V0 list $QUOTA_PATH | grep "$QUOTA_PATH" | awk '{print $6}'
}

function hl_exceeded()
{
        local QUOTA_PATH=$1;
        $CLI volume quota $V0 list $QUOTA_PATH | grep "$QUOTA_PATH" | awk '{print $7}'

}

EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT '2' brick_count $V0

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $CLI volume quota $V0 enable
sleep 5

TEST glusterfs -s $H0 --volfile-id $V0 $M0;

TEST mkdir -p $M0/test_dir
TEST $CLI volume quota $V0 limit-usage /test_dir 10MB 50

EXPECT "10.0MB" hard_limit "/test_dir";
EXPECT "50%" soft_limit "/test_dir";

TEST dd if=/dev/zero of=$M0/test_dir/file1.txt bs=1024k count=4
EXPECT "4.0MB" usage "/test_dir";
EXPECT 'No' sl_exceeded "/test_dir";
EXPECT 'No' hl_exceeded "/test_dir";

TEST dd if=/dev/zero of=$M0/test_dir/file1.txt bs=1024k count=6
EXPECT "6.0MB" usage "/test_dir";
EXPECT 'Yes' sl_exceeded "/test_dir";
EXPECT 'No' hl_exceeded "/test_dir";

#set timeout to 0 so that quota gets enforced without any lag
TEST $CLI volume set $V0 features.hard-timeout 0
TEST $CLI volume set $V0 features.soft-timeout 0

TEST ! dd if=/dev/zero of=$M0/test_dir/file1.txt bs=1024k count=15
EXPECT 'Yes' sl_exceeded "/test_dir";
EXPECT 'Yes' hl_exceeded "/test_dir";

cleanup;
