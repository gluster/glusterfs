#!/bin/bash

. $(dirname $0)/../include.rc

create_files () {
	mkdir $1
	for i in $(seq 0 99); do
		mkdir $1/dir$i
		for j in $(seq 0 99); do
			touch $1/dir$i/file$j
		done
	done
}

count_files () {
	ls $1 | wc -l
}

LANDFILL=$B0/${V0}1/.glusterfs/landfill

TEST glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}1
TEST $CLI volume start $V0
TEST $GFS -s $H0 --volfile-id $V0 $M0

TEST create_files $M0/foo
TEST [ $(count_files $LANDFILL) = "0" ]

# This should immediately send the whole directory to the landfill.
TEST setfattr -n glusterfs.dht.nuke -v trinity $M0/foo

# Make sure the directory's not visible on the mountpoint, and is visible in
# the brick's landfill.
TEST ! ls $M0/foo
TEST [ $(count_files $LANDFILL) = "1" ]

# Make sure the janitor thread cleans it up in a timely fashion.
EXPECT_WITHIN 60 "0" count_files $LANDFILL

cleanup
