#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

cleanup;

QDD=$(dirname $0)/quota
# compile the test write program and run it
build_tester $(dirname $0)/../../basic/quota.c -o $QDD

TESTS_EXPECTED_IN_LOOP=9

TEST glusterd
TEST pidof glusterd

# --------------------------------------------------
# Create, start and mount a volume with single brick
# --------------------------------------------------

TEST $CLI volume create $V0 $H0:$B0/{V0}
EXPECT "$V0" volinfo_field $V0 'Volume Name'
EXPECT 'Created' volinfo_field $V0 'Status'

TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'

TEST $GFS -s $H0 --volfile-id $V0 $M0
TEST mkdir -p $M0/test_dir

#--------------------------------------------------------
# Enable quota of the volume and set hard and soft timeout
#------------------------------------------------------

TEST $CLI volume quota $V0 enable
EXPECT 'on' volinfo_field $V0 'features.quota'
TEST $CLI volume quota $V0 soft-timeout 0
EXPECT '0' volinfo_field $V0 'features.soft-timeout'
TEST $CLI volume quota $V0 hard-timeout 0
EXPECT '0' volinfo_field $V0 'features.hard-timeout'


#-------------------------------------------------------
# Set and remove quota limits on the directory and
# verify if the limits are being reflected properly
#------------------------------------------------------

TEST $CLI volume quota $V0 limit-usage /test_dir 100MB
EXPECT "100.0MB" quota_list_field "/test_dir" 2

TEST $CLI volume quota $V0 limit-objects /test_dir 100
EXPECT "100" quota_object_list_field "/test_dir" 2

TEST $CLI volume quota $V0 remove /test_dir
EXPECT "" quota_list_field "/test_dir" 2

# Need to verify this once
#TEST $CLI volume quota $V0 remove-objects /test_dir
#EXPECT "" quota_object_list_field "/test_dir" 2

# Set back the limits

TEST $CLI volume quota $V0 limit-usage /test_dir 10MB
EXPECT "10.0MB" quota_list_field "/test_dir" 2

TEST $CLI volume quota $V0 limit-objects /test_dir 10
EXPECT "10" quota_object_list_field "/test_dir" 2

#-----------------------------------------------------
# Check the quota enforcement mechanism for usage
#-----------------------------------------------------

# try creating a 8MB file and it should fail
TEST $QDD $M0/test_dir/test1.txt 256 32
EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "8.0MB" quotausage "/test_dir"
TEST rm -f $M0/test_dir/test1.txt
EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "0Bytes" quotausage "/test_dir"

# try creating a 15MB file and it should not succeed
TEST ! $QDD $M0/test_dir/test2.txt 256 60
TEST rm -f $M0/test_dir/test2.txt
EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "0Bytes" quotausage "/test_dir"


#------------------------------------------------------
# Check the quota enforcement mechanism for object count
#-------------------------------------------------------

# Try creating 9 files and it should succeed as object limit
# is set to 10, since directory where limit is set is accounted
# as well.

for i in {1..9}; do
        TEST_IN_LOOP touch $M0/test_dir/test$i.txt
done
EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "9" quota_object_list_field "/test_dir" 4

# Check available limit
EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "0" quota_object_list_field "/test_dir" 6

# Check if hard-limit exceeded
EXPECT "Yes" quota_object_list_field "/test_dir" 8

# Check if soft-limit exceeded
EXPECT "Yes" quota_object_list_field "/test_dir" 7

# Creation of 11th file should throw out an error
TEST ! touch $M0/test_dir/test11.txt

TEST rm -rf $M0/test_dir/test*
EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "0" quota_object_list_field "/test_dir" 4

TEST $CLI volume quota $V0 remove-objects /test_dir

TEST $CLI volume stop $V0
EXPECT "1" get_aux
TEST $CLI volume delete $V0

rm -f $QDD
cleanup;
