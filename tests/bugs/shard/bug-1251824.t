#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../common-utils.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1,2,3}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0
TEST useradd -M test_user 2>/dev/null

# Create 3 files as root.
TEST touch $M0/foo
TEST touch $M0/bar
TEST touch $M0/baz
TEST touch $M0/qux
TEST mkdir $M0/dir

# Change ownership to non-root on foo and bar.
TEST chown test_user:test_user $M0/foo
TEST chown test_user:test_user $M0/bar

# Write 6M of data on foo as non-root, 2M overflowing into block-1.
TEST run_cmd_as_user test_user "dd if=/dev/zero of=$M0/foo bs=1M count=6"

# Ensure owner and group are root on the block-1 shard.
gfid_foo=$(get_gfid_string $M0/foo)

EXPECT "root" echo `find $B0 -name $gfid_foo.1 | xargs stat -c %U`
EXPECT "root" echo `find $B0 -name $gfid_foo.1 | xargs stat -c %G`

#Ensure /.shard is owned by root.
EXPECT "root" echo `find $B0/${V0}0 -name .shard | xargs stat -c %U`
EXPECT "root" echo `find $B0/${V0}0 -name .shard | xargs stat -c %G`
EXPECT "root" echo `find $B0/${V0}1 -name .shard | xargs stat -c %U`
EXPECT "root" echo `find $B0/${V0}1 -name .shard | xargs stat -c %G`
EXPECT "root" echo `find $B0/${V0}2 -name .shard | xargs stat -c %U`
EXPECT "root" echo `find $B0/${V0}2 -name .shard | xargs stat -c %G`
EXPECT "root" echo `find $B0/${V0}3 -name .shard | xargs stat -c %U`
EXPECT "root" echo `find $B0/${V0}3 -name .shard | xargs stat -c %G`

# Write 6M of data on bar as root.
TEST dd if=/dev/zero of=$M0/bar bs=1M count=6

# Ensure owner and group are root on the block-1 shard.
gfid_bar=$(get_gfid_string $M0/bar)

EXPECT "root" echo `find $B0 -name $gfid_bar.1 | xargs stat -c %U`
EXPECT "root" echo `find $B0 -name $gfid_bar.1 | xargs stat -c %G`

# Write 6M of data on baz as root.
TEST dd if=/dev/zero of=$M0/baz bs=1M count=6

gfid_baz=$(get_gfid_string $M0/baz)

# Ensure owner and group are root on the block-1 shard.
EXPECT "root" echo `find $B0 -name $gfid_baz.1 | xargs stat -c %U`
EXPECT "root" echo `find $B0 -name $gfid_baz.1 | xargs stat -c %G`

# Test to ensure unlink from an unauthorized user does not lead to only
# the shards under /.shard getting unlinked while that on the base file fails
# with EPERM/ACCES.

TEST ! run_cmd_as_user test_user "unlink $M0/baz"
TEST find $B0/*/.shard/$gfid_baz.1

# Test to ensure rename of a file where the dest file exists and is sharded,
# from an unauthorized user does not lead to only the shards under /.shard
# getting unlinked while that on the base file fails with EPERM/ACCES.

TEST ! run_cmd_as_user test_user "mv -f $M0/qux $M0/baz"
TEST find $B0/*/.shard/$gfid_baz.1
TEST stat $M0/qux

# Shard translator executes steps in the following order while doing a truncate
# to a lower size:
# 1) unlinking shards under /.shard first with frame->root->{uid,gid} being 0,
# 2) truncate the original file by the right amount.
# The following two tests are towards ensuring that truncate attempt from an
# unauthorised user doesn't result in only the shards under /.shard getting
# removed (since they're being performed as root) while step 2) above fails,
# leaving the file in an inconsistent state.

TEST ! run_cmd_as_user test_user "truncate -s 1M $M0/baz"
TEST find $B0/*/.shard/$gfid_baz.1

# Perform a cp as non-root user. This should trigger readv() which will trigger
# reads on first shard of "foo" under /.shard, and this must not fail if shard
# translator correctly sets frame->root->uid,gid to 0 before reading off the
# first shard, since it's owned by root.
TEST chown test_user:test_user $M0/dir
TEST run_cmd_as_user test_user "cp $M0/foo $M0/dir/quux"

md5sum_foo=$(md5sum $M0/foo | awk '{print $1}')
EXPECT "$md5sum_foo" echo `md5sum $M0/dir/quux | awk '{print $1}'`

userdel test_user

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
