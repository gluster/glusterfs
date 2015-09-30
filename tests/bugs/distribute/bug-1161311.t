#!/bin/bash

# This tests for hard link preservation for files that are linked, when the
# file is undergoing migration

# --- Improvements and other tests ---
## Fail rebalance of the large file for which links are created during P1/2
### phases of migration
## Start with multiple hard links to the file and then create more during P1/2
### phases of migration
## Test the same with NFS as the mount rather than FUSE
## Create links when file is under P2 of migration specifically
## Test with quota, to error out during hard link creation (if possible)

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

checksticky () {
	i=0;
	while [ ! -k $1 ]; do
		sleep 1
		i=$((i+1));
		# Try for 10 seconds to get the sticky bit state
		# else fail the test, as we may never see it
		if [[ $i == 10 ]]; then
			return $i
		fi
		echo "Waiting... $i"
	done
        echo "Done... got out @ $i"
	return 0
}

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/${V0}{1..3};

EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT '3' brick_count $V0

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Mount FUSE with caching disabled (read-write)
TEST glusterfs -s $H0 --volfile-id $V0 $M0;

# Create a directories to hold the links
TEST mkdir $M0/dir1
TEST mkdir -p $M0/dir2/dir3

# Create a large file (1GB), so that rebalance takes time
dd if=/dev/urandom of=$M0/dir1/FILE2 bs=64k count=10240

# Rename the file to create a linkto, for rebalance to
# act on the file
## FILE1 and FILE2 hashes are, 678b1c4a e22c1ada, so they fall
## into separate bricks when brick count is 3
TEST mv $M0/dir1/FILE2 $M0/dir1/FILE1

# unmount and remount the volume
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST glusterfs -s $H0 --volfile-id $V0 $M0;

# Start the rebalance
TEST $CLI volume rebalance $V0 start force

# Wait for FILE to get the sticky bit on, so that file is under
# active rebalance, before creating the links
TEST checksticky $B0/${V0}3/dir1/FILE1

# Create the links
## FILE3 FILE5 FILE7 have hashes, c8c91469 566d26ce 22ce7eba
## Which fall into separate bricks on a 3 brick layout
cd $M0
TEST ln ./dir1/FILE1 ./dir1/FILE7
TEST ln ./dir1/FILE1 ./dir1/FILE5
TEST ln ./dir1/FILE1 ./dir1/FILE3

TEST ln ./dir1/FILE1 ./dir2/FILE7
TEST ln ./dir1/FILE1 ./dir2/FILE5
TEST ln ./dir1/FILE1 ./dir2/FILE3

TEST ln ./dir1/FILE1 ./dir2/dir3/FILE7
TEST ln ./dir1/FILE1 ./dir2/dir3/FILE5
TEST ln ./dir1/FILE1 ./dir2/dir3/FILE3
cd /

# Ideally for this test to have done its job, the file should still be
# under migration, so check the sticky bit again
TEST checksticky $B0/${V0}3/dir1/FILE1

# Wait for rebalance to complete
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" rebalance_status_field $V0

# Check if all files are clean and migrated right
## stat on the original file should show linkcount of 10
linkcountsrc=$(stat -c %h $M0/dir1/FILE1)
TEST [[ $linkcountsrc == 10 ]]

## inode and size of every file should be same as original file
inodesrc=$(stat -c %i $M0/dir1/FILE1)
TEST [[ $(stat -c %i $M0/dir1/FILE3) == $inodesrc ]]
TEST [[ $(stat -c %i $M0/dir1/FILE5) == $inodesrc ]]
TEST [[ $(stat -c %i $M0/dir1/FILE7) == $inodesrc ]]

TEST [[ $(stat -c %i $M0/dir2/FILE3) == $inodesrc ]]
TEST [[ $(stat -c %i $M0/dir2/FILE5) == $inodesrc ]]
TEST [[ $(stat -c %i $M0/dir2/FILE7) == $inodesrc ]]

TEST [[ $(stat -c %i $M0/dir2/dir3/FILE3) == $inodesrc ]]
TEST [[ $(stat -c %i $M0/dir2/dir3/FILE5) == $inodesrc ]]
TEST [[ $(stat -c %i $M0/dir2/dir3/FILE7) == $inodesrc ]]

# Check, newer link creations
cd $M0
TEST ln ./dir1/FILE1 ./FILE1
TEST ln ./dir2/FILE3 ./FILE3
TEST ln ./dir2/dir3/FILE5 ./FILE5
TEST ln ./dir1/FILE7 ./FILE7
cd /
linkcountsrc=$(stat -c %h $M0/dir1/FILE1)
TEST [[ $linkcountsrc == 14 ]]

cleanup;
