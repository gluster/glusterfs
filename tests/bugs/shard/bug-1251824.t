#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0
TEST useradd -M test_user 2>/dev/null

# Create 3 files as root.
TEST touch $M0/foo
TEST touch $M0/bar
TEST touch $M0/baz

# Change ownership to non-root on foo and bar.
TEST chown test_user:test_user $M0/foo
TEST chown test_user:test_user $M0/bar

# Write 6M of data on foo as non-root, 2M overflowing into block-1.
su -m test_user -c "dd if=/dev/zero of=$M0/foo bs=1M count=6"

# Ensure owner and group are same on the shard as the main file.
gfid_foo=`getfattr -n glusterfs.gfid.string $M0/foo 2>/dev/null \
          | grep glusterfs.gfid.string | cut -d '"' -f 2`

EXPECT "test_user" echo `find $B0 -name $gfid_foo.1 | xargs stat -c %U`
EXPECT "test_user" echo `find $B0 -name $gfid_foo.1 | xargs stat -c %G`

# Write 6M of data on bar as root.
TEST dd if=/dev/zero of=$M0/bar bs=1M count=6

# Ensure owner and group are same on the shard as the main file.
gfid_bar=`getfattr -n glusterfs.gfid.string $M0/bar 2>/dev/null \
          | grep glusterfs.gfid.string | cut -d '"' -f 2`

EXPECT "test_user" echo `find $B0 -name $gfid_bar.1 | xargs stat -c %U`
EXPECT "test_user" echo `find $B0 -name $gfid_bar.1 | xargs stat -c %G`

# Write 6M of data on baz as root.
TEST dd if=/dev/zero of=$M0/baz bs=1M count=6

# Ensure owner andgroup are same on the shard as the main file.
gfid_baz=`getfattr -n glusterfs.gfid.string $M0/baz 2>/dev/null \
          | grep glusterfs.gfid.string | cut -d '"' -f 2`

EXPECT "root" echo `find $B0 -name $gfid_baz.1 | xargs stat -c %U`
EXPECT "root" echo `find $B0 -name $gfid_baz.1 | xargs stat -c %G`
userdel test_user

TEST umount $M0

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
