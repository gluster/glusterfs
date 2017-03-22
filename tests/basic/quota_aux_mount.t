#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

##-------------------------------------------------------------
## Tests to verify that aux mount is unmounted after each quota
## command executes.
##-------------------------------------------------------------

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2  $H0:$B0/${V0}{1,2,3,4};

EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT '4' brick_count $V0

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $GFS -s $H0 --volfile-id $V0 $M0;

TEST mkdir -p $M0/test_dir/

TEST $CLI volume quota $V0 enable
EXPECT 'on' volinfo_field $V0 'features.quota'
EXPECT 'on' volinfo_field $V0 'features.inode-quota'

TEST $CLI volume quota $V0 limit-usage /test_dir 150MB
EXPECT "1"  get_limit_aux
TEST $CLI volume quota $V0 limit-objects /test_dir 10
EXPECT "1"  get_limit_aux
EXPECT "150.0MB" quota_hard_limit "/test_dir";
EXPECT "1"  get_list_aux
EXPECT "10" quota_object_hard_limit "/test_dir";
EXPECT "1"  get_list_aux

TEST $CLI volume quota $V0 remove /test_dir/
EXPECT "1"  get_limit_aux
TEST $CLI volume quota $V0 remove-objects /test_dir
EXPECT "1"  get_limit_aux

TEST $CLI volume quota $V0 disable

TEST $CLI volume stop $V0;

cleanup;
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=1447344
