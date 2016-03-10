#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

create_files () {
	for i in {1..1000}; do
		orig=$(printf %s/abc%04d $1 $i)
		real=$(printf %s/src%04d $1 $i)
		# Make sure lots of these have linkfiles.
		echo "This is file $i" > $orig
		mv $orig $real
	done
	sync
}

move_files_inner () {
	sfile=$M0/status_$(basename $1)
	for i in {1..1000}; do
		src=$(printf %s/src%04d $1 $i)
		dst=$(printf %s/dst%04d $1 $i)
		mv $src $dst 2> /dev/null
	done
	echo "done" > $sfile
}

move_files () {
        #Create the status file here to prevent spurious failures
        #caused by the file not being created in time by the
        #background process
	sfile=$M0/status_$(basename $1)
	echo "running" > $sfile
	move_files_inner $* &
}

check_files () {
	errors=0
	for i in {1..1000}; do
		if [ ! -f $(printf %s/dst%04d $1 $i) ]; then
			if [ -f $(printf %s/src%04d $1 $i) ]; then
				echo "file $i didnt get moved" > /dev/stderr
			else
				echo "file $i is MISSING" > /dev/stderr
				errors=$((errors+1))
			fi
		fi
	done
	if [ $((errors)) != 0 ]; then
		: ls -l $1 > /dev/stderr
	fi
	return $errors
}

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2,3,4,5,6};

EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT '6' brick_count $V0

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Mount FUSE with caching disabled (read-write)
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0;

TEST create_files $M0

## Mount FUSE with caching disabled (read-write) again
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M1;

TEST move_files $M0
TEST move_files $M1

# It's regrettable that renaming 1000 files might take more than 30 seconds,
# but on our test systems sometimes it does, so double the time from what we'd
# use otherwise.  There still seem to be some spurious failures, 1 in 20 when
# this does not complete, added an additional 60 seconds to take false reports
# out of the system, during test runs, especially on slower test systems.
EXPECT_WITHIN 120 "done" cat $M0/status_0
EXPECT_WITHIN 120 "done" cat $M1/status_1

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M1
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0;
TEST check_files $M0

TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
