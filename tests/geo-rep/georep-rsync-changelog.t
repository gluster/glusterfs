#!/bin/bash

# Following tests involves geo-rep regresseion tests with changelog
# as change detector, and rsync as sync mode on both fuse and nfs mount

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/geo-rep-helper.rc
. $(dirname $0)/geo-rep-config.rc

cleanup;
AREQUAL_PATH=$(dirname $0)/../utils
CFLAGS=""
test "`uname -s`" != "Linux" && {
    CFLAGS="$CFLAGS -I$(dirname $0)/../../../contrib/argp-standalone ";
    CFLAGS="$CFLAGS -L$(dirname $0)/../../../contrib/argp-standalone -largp ";
    CFLAGS="$CFLAGS -lintl";
}
build_tester $AREQUAL_PATH/arequal-checksum.c $CFLAGS

TEST glusterd
TEST pidof glusterd

setup_georep ;

# start of tests on fuse mount

TEST glusterfs -s $H0 --volfile-id $GMV0 $M0

TEST changelog_mode_test "create" $M0

TEST changelog_mode_test "chmod" $M0

TEST changelog_mode_test "chown" $M0

TEST changelog_mode_test "chgrp" $M0

# Bug 1083963
#TEST changelog_mode_test "rename" $M0

TEST changelog_mode_test "truncate" $M0

TEST changelog_mode_test "symlink" $M0

# Bug 1003020
#TEST changelog_mode_test "hardlink" $M0

#TEST changelog_mode_remove_test $M0

# start of tests on nfs mount

TEST mount -t nfs -o vers=3,nolock $H0:$GMV0 $N0

TEST changelog_mode_test "create" $N0

TEST changelog_mode_test "chmod" $N0

TEST changelog_mode_test "chown" $N0

TEST changelog_mode_test "chgrp" $N0

#TEST changelog_mode_test "rename" $N0

TEST changelog_mode_test "truncate" $N0

TEST changelog_mode_test "symlink" $N0

#TEST changelog_mode_test "hardlink" $N0

#TEST changelog_mode_remove_test $N0

TEST rm -rf $AREQUAL_PATH/arequal-checksum
cleanup_georep;
