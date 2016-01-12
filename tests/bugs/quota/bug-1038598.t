#!/bin/bash
.  $(dirname $0)/../../include.rc
.  $(dirname $0)/../../volume.rc

cleanup;

QDD=$(dirname $0)/quota
# compile the test write program and run it
build_tester $(dirname $0)/../../basic/quota.c -o $QDD

TEST glusterd

TEST $CLI volume create $V0 replica 2  $H0:$B0/${V0}{1,2};

EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT '2' brick_count $V0

TEST $CLI volume start $V0;

TEST $CLI volume quota $V0 enable
sleep 5

TEST glusterfs -s $H0 --volfile-id $V0 $M0;

TEST mkdir -p $M0/test_dir
TEST $CLI volume quota $V0 limit-usage /test_dir 10MB 50

EXPECT "10.0MB" quota_hard_limit "/test_dir";
EXPECT "50%" quota_soft_limit "/test_dir";

TEST $QDD $M0/test_dir/file1.txt 256 16
EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "4.0MB" quotausage "/test_dir";
EXPECT 'No' quota_sl_exceeded "/test_dir";
EXPECT 'No' quota_hl_exceeded "/test_dir";

TEST $QDD $M0/test_dir/file1.txt 256 24
EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "6.0MB" quotausage "/test_dir";
EXPECT 'Yes' quota_sl_exceeded "/test_dir";
EXPECT 'No' quota_hl_exceeded "/test_dir";

#set timeout to 0 so that quota gets enforced without any lag
TEST $CLI volume set $V0 features.hard-timeout 0
TEST $CLI volume set $V0 features.soft-timeout 0

TEST ! $QDD $M0/test_dir/file1.txt 256 60
EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT 'Yes' quota_sl_exceeded "/test_dir";
EXPECT 'Yes' quota_hl_exceeded "/test_dir";

rm -f $QDD

cleanup;
