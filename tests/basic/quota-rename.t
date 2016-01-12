#!/bin/bash

# This regression test tries to ensure renaming a directory with content, and
# no limit set, is accounted properly, when moved into a directory with quota
# limit set.

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}
TEST $CLI volume start $V0;

TEST $CLI volume quota $V0 enable;

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;

TEST $CLI volume quota $V0 hard-timeout 0
TEST $CLI volume quota $V0 soft-timeout 0

TEST mkdir -p $M0/dir/dir1
TEST $CLI volume quota $V0 limit-objects /dir 20

TEST mkdir $M0/dir/dir1/d{1..5}
TEST touch $M0/dir/dir1/f{1..5}
TEST mv $M0/dir/dir1 $M0/dir/dir2

#Number of files under /dir is 5
EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "5" quota_object_list_field "/dir" 4

#Number of directories under /dir is 7
EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "7" quota_object_list_field "/dir" 5

cleanup;
