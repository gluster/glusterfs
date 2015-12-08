#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1,2,3}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0

TEST mkdir $M0/dir
TEST touch $M0/dir/foo
TEST touch $M0/dir/bar
TEST touch $M0/dir/new

TEST truncate -s 14M $M0/dir/foo
TEST truncate -s 14M $M0/dir/bar

# Perform writes that fall on the 2nd block of "foo" (counting from 0)
TEST dd if=/dev/zero of=$M0/dir/foo bs=1024 seek=10240 count=2048 conv=notrunc

# Perform writes that fall on the 2nd block of "bar" (counting from 0)
TEST dd if=/dev/zero of=$M0/dir/bar bs=1024 seek=10240 count=2048 conv=notrunc

# Now unlink "foo". If the bug exists, it should fail with EINVAL.
TEST unlink $M0/dir/foo

# Now rename "new" to "bar". If the bug exists, it should fail with EINVAL.
TEST mv -f $M0/dir/new $M0/dir/bar

TEST dd if=/dev/zero of=$M0/dir/new bs=1024 count=5120

# Now test that this fix does not break unlink of files without holes
TEST unlink $M0/dir/new

cleanup
