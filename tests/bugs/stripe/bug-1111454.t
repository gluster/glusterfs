#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

#symlink resolution should succeed
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 stripe 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
TEST mkdir $M0/dir
TEST touch $M0/dir/file
TEST ln -s file $M0/dir/symlinkfile
TEST ls -lR $M0
cleanup
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=000000
#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=000000
