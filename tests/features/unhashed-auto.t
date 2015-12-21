#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../dht.rc

NFILES=100

touch_files () {
	for i in $(seq 1 $NFILES); do
		touch $(printf $M0/dir/file%02d $i)
	done
}

count_files () {
	found=0
	for i in $(seq 1 $NFILES); do
		if [ -f $(printf $M0/dir/file%02d $i) ]; then
			found=$((found+1))
		fi
	done
	echo "found $found files" > /dev/tty
	echo $found
}

wait_for_rebalance () {
	while true; do
		tmp=$(rebalance_completed)
		if [ $tmp -eq 1 ]; then
			sleep 1
		else
			break
		fi
	done
}

get_xattr () {
	cmd="getfattr --absolute-names --only-values -n trusted.glusterfs.dht"
	$cmd $1 | od -tx1 -An | tr -d ' '
}

get_xattr_hash () {
        cmd="getfattr --absolute-names --only-values -n trusted.glusterfs.dht"
        $cmd $1 | od -tx1 -An | awk '{printf("%s%s%s%s\n", $1, $2, $3, $4);}'
}

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2}
EXPECT "$V0" volinfo_field $V0 'Volume Name'
EXPECT 'Created' volinfo_field $V0 'Status'

TEST $CLI volume set $V0 cluster.lookup-optimize ON

TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'

# Create some files for later tests.
TEST $GFS -s $H0 --volfile-id $V0 $M0
TEST mkdir $M0/dir
TEST touch_files
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

# Add a brick and do the fix-layout part of rebalance to update directory layouts
# (including their directory commit hashes).
TEST $CLI volume add-brick $V0 $H0:$B0/${V0}3
EXPECT '3' brick_count $V0
TEST $CLI volume rebalance $V0 fix-layout start
TEST wait_for_rebalance

# Now for the sneaky part.  *Undo* the part of rebalance that updated the volume
# commit hash, forcing a false match between that and the directory commit hashes.
TEST setfattr -x trusted.glusterfs.dht.commithash $B0/${V0}1
TEST setfattr -x trusted.glusterfs.dht.commithash $B0/${V0}2
TEST setfattr -x trusted.glusterfs.dht.commithash $B0/${V0}3

# Mount and check that we do *not* see all of the files.  This indicates that we
# correctly skipped the broadcast lookup that would have found them.
TEST $GFS -s $H0 --volfile-id $V0 $M0
TEST [ $(count_files) -ne 100 ]
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

# Do the fix-layout again to generate a new volume commit hash.
TEST $CLI volume rebalance $V0 fix-layout start
TEST wait_for_rebalance

# Mount and check that we *do* see all of the files.  This indicates that we saw
# the mismatch and did the broadcast lookup this time.
TEST $GFS -s $H0 --volfile-id $V0 $M0
TEST [ $(count_files) -eq 100 ]
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

# Do a *full* rebalance and verify that the directory commit hash changed.
old_val=$(get_xattr $B0/${V0}1/dir)
TEST $CLI volume rebalance $V0 start
TEST wait_for_rebalance
new_val=$(get_xattr $B0/${V0}1/dir)
TEST [ ! x"$old_val" = x"$new_val" ]

# Force an anomoly on an existing layout and heal it
## The healed layout should not carry a commit-hash (or should carry 1 in the
## commit-hash)
TEST setfattr -x trusted.glusterfs.dht $B0/${V0}1/dir
TEST $GFS -s $H0 --volfile-id $V0 $M0
TEST [ -d $M0/dir ]
new_hash=$(get_xattr_hash $B0/${V0}1/dir)
TEST [ x"$new_hash" = x"00000001" ]
new_hash=$(get_xattr_hash $B0/${V0}2/dir)
TEST [ x"$new_hash" = x"00000001" ]

# Unset the option and check that newly created directories get 1 in the
# disk layout
TEST $CLI volume reset $V0 cluster.lookup-optimize
TEST mkdir $M0/dir1
new_hash=$(get_xattr_hash $B0/${V0}1/dir1)
TEST [ x"$new_hash" = x"00000001" ]
new_hash=$(get_xattr_hash $B0/${V0}2/dir1)
TEST [ x"$new_hash" = x"00000001" ]


cleanup
