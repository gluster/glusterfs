#!/bin/bash

# Test that the rsync and "extra" regexes cause rename-in-place without
# creating linkfiles, when they're supposed to.  Without the regex we'd have a
# 1/4 chance of each file being assigned to the right place, so with 16 files
# we have a 1/2^32 chance of getting the correct result by accident.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

function count_linkfiles {
	local i
	local count=0
	for i in $(seq $2 $3); do
		x=$(find $1$i -perm -1000 | wc -l)
		# Divide by two because of the .glusterfs links.
		count=$((count+x/2))
	done
	echo $count
}

# This function only exists to get around quoting difficulties in TEST.
function set_regex {
	$CLI volume set $1 cluster.extra-hash-regex '^foo(.+)bar$'
}

cleanup;

TEST glusterd
TEST pidof glusterd

mkdir -p $H0:$B0/${V0}0
mkdir -p $H0:$B0/${V0}1
mkdir -p $H0:$B0/${V0}2
mkdir -p $H0:$B0/${V0}3

# Create and start a volume.
TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1 \
			    $H0:$B0/${V0}2 $H0:$B0/${V0}3
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Started' volinfo_field $V0 'Status';

# Mount it.
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0

# Make sure the rsync regex works, by verifying that no linkfiles are
# created.
rm -f $M0/file*
for i in $(seq 0 15); do
	fn=$(printf file%x $i)
	tmp_fn=$(printf .%s.%d $fn $RANDOM)
	echo testing > $M0/$tmp_fn
	mv $M0/$tmp_fn $M0/$fn
done
lf=$(count_linkfiles $B0/$V0 0 3)
TEST [ "$lf" -eq "0" ]

# Make sure that linkfiles *are* created for normal files.
rm -f $M0/file*
for i in $(seq 0 15); do
	fn=$(printf file%x $i)
	tmp_fn=$(printf foo%sbar $fn)
	echo testing > $M0/$tmp_fn
	mv $M0/$tmp_fn $M0/$fn
done
lf=$(count_linkfiles $B0/$V0 0 3)
TEST [ "$lf" -ne "0" ]

# Make sure that setting an extra regex suppresses the linkfiles.
TEST set_regex $V0
rm -f $M0/file*
for i in $(seq 0 15); do
	fn=$(printf file%x $i)
	tmp_fn=$(printf foo%sbar $fn)
	echo testing > $M0/$tmp_fn
	mv $M0/$tmp_fn $M0/$fn
done
lf=$(count_linkfiles $B0/$V0 0 3)
TEST [ "$lf" -eq "0" ]

# Re-test the rsync regex, to make sure the extra one didn't break it.
rm -f $M0/file*
for i in $(seq 0 15); do
	fn=$(printf file%x $i)
	tmp_fn=$(printf .%s.%d $fn $RANDOM)
	echo testing > $M0/$tmp_fn
	mv $M0/$tmp_fn $M0/$fn
done
lf=$(count_linkfiles $B0/$V0 0 3)
TEST [ "$lf" -eq "0" ]

cleanup
