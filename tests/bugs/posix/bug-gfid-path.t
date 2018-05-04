#!/bin/bash

# This test case is for the bug where, even though a file is
# created when gfid2path option is turned off (default is ON),
# getfattr of "glusterfs.gfidtopath" was succeeding for that
# file. Ideally the getfattr should fail, as the file does not
# have its path(s) stored as a extended attribute (because it
# was created when gfid2path option was off)

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2,3,4};

EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT '4' brick_count $V0

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST glusterfs -s $H0 --volfile-id $V0 $M0;

TEST mkdir $M0/dir
TEST mkdir $M0/new
TEST mkdir $M0/3

TEST touch $M0/dir/file

# except success as by default gfid2path is enabled
# and the glusterfs.gfidtopath xattr should give the
# path of the object as the value

TEST getfattr -n glusterfs.gfidtopath $M0/dir/file

# turn off gfid2path feature
TEST $CLI volume set $V0 storage.gfid2path off

TEST touch $M0/new/foo

# again enable gfid2path. This has to be enabled before
# trying the getfattr. Because, glusterfs.gfidtopath xattr
# request is handled only if gfid2path is enabled. If not,
# then getxattr on glusterfs.gfid2path fails anyways. In this
# context we want getfattr to fail, because the file was created
# when gfid2path feature was disabled and not because gfid2path
# feature itself is disabled.
TEST $CLI volume set $V0 storage.gfid2path on

# getfattr should fail as it is attempted on a file
# which does not have its path stored as a xattr
# (because file got created after disabling gfid2path)
TEST ! getfattr -n glusterfs.gfidtopath $M0/new/foo;



TEST touch $M0/3/new

# should be successful
TEST getfattr -n glusterfs.gfidtopath $M0/3/new

TEST rm -rf $M0/*

cleanup;
