#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

CHANGELOG_PATH_0="$B0/${V0}0/.glusterfs/changelogs"
ROLLOVER_TIME=30

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume set $V0 changelog.changelog on
TEST $CLI volume set $V0 changelog.rollover-time $ROLLOVER_TIME
TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
touch $M0/file1
mv $M0/file1 $M0/rn_file1
mkdir $M0/dir1
mv $M0/dir1 $M0/rn_dir1

EXPECT "2" check_changelog_op ${CHANGELOG_PATH_0} "RENAME"

cleanup;

#####Test on multiple subvolume#####
#==========================================#

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 changelog.changelog on
TEST $CLI volume set $V0 changelog.rollover-time $ROLLOVER_TIME
TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
touch $M0/gluster_file
mv $M0/gluster_file $M0/rn_gluster_file
mkdir $M0/dir1
mv $M0/dir1 $M0/rn_dir1

EXPECT "2" check_changelog_op ${CHANGELOG_PATH_0} "RENAME"

cleanup;
