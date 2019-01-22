#!/bin/bash

SCRIPT_TIMEOUT=800

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../traps.rc

cleanup;

NUM_VOLS=15
MOUNT_BASE=$(dirname $M0)

# GlusterD reports that bricks are started when in fact their attach requests
# might still need to be retried.  That's a bit of a hack, but there's no
# feasible way to wait at that point (in attach_brick) and the rest of the
# code is unprepared to deal with transient errors so the whole "brick start"
# would fail.  Meanwhile, glusterfsd can only handle attach requests at a
# rather slow rate.  After GlusterD tries to start a couple of hundred bricks,
# glusterfsd can fall behind and we start getting mount failures.  Arguably,
# those are spurious because we will eventually catch up.  We're just not
# ready *yet*.  More to the point, even if the errors aren't spurious that's
# not what we're testing right now.  Therefore, we give glusterfsd a bit more
# breathing room for this test than we would otherwise.
MOUNT_TIMEOUT=15

get_brick_base () {
	printf "%s/vol%02d" $B0 $1
}

get_mount_point () {
	printf "%s/vol%02d" $MOUNT_BASE $1
}

function count_up_bricks {
        vol=$1;
        $CLI --xml volume status $vol | grep '<status>1' | wc -l
}

create_volume () {

	local vol_name=$(printf "%s-vol%02d" $V0 $1)

	local brick_base=$(get_brick_base $1)
	local cmd="$CLI volume create $vol_name replica 3"
	local b
	for b in $(seq 0 5); do
		local this_brick=${brick_base}/brick$b
		mkdir -p $this_brick
		cmd="$cmd $H0:$this_brick"
	done
	TEST $cmd
	TEST $CLI volume start $vol_name
	# check for 6 bricks and 1 shd daemon to be up and running
        EXPECT_WITHIN 120 7 count_up_bricks $vol_name
	local mount_point=$(get_mount_point $1)
	mkdir -p $mount_point
	TEST $GFS -s $H0 --volfile-id=$vol_name $mount_point
}

cleanup_func () {
	local v
	for v in $(seq 1 $NUM_VOLS); do
		local mount_point=$(get_mount_point $v)
		force_umount $mount_point
		rm -rf $mount_point
		local vol_name=$(printf "%s-vol%02d" $V0 $v)
		$CLI volume stop $vol_name
		$CLI volume delete $vol_name
		rm -rf $(get_brick_base $1) &
	done &> /dev/null
	wait
}
push_trapfunc cleanup_func

TEST glusterd
TEST $CLI volume set all cluster.brick-multiplex on

# Our infrastructure can't handle an arithmetic expression here.  The formula
# is (NUM_VOLS-1)*5 because it sees each TEST/EXPECT once but needs the other
# NUM_VOLS-1 and there are 5 such statements in each iteration.
TESTS_EXPECTED_IN_LOOP=84
for i in $(seq 1 $NUM_VOLS); do
        starttime="$(date +%s)";

	create_volume $i
	TEST dd if=/dev/zero of=$(get_mount_point $i)/a_file bs=4k count=1
        # Unmounting to reduce memory footprint on regression hosts
        mnt_point=$(get_mount_point $i)
        EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $mnt_point
        endtime=$(expr $(date +%s) - $starttime)

        echo "Memory Used after $i volumes : $(pmap -x $(pgrep glusterfsd) | grep total)"
        echo "Thread Count after $i volumes: $(ps -T -p $(pgrep glusterfsd) | wc -l)"
        echo "Time taken                   : ${endtime} seconds"
done

echo "=========="
echo "List of all the threads in the Brick process"
ps -T -p $(pgrep glusterfsd)
echo "=========="

# Kill glusterd, and wait a bit for all traces to disappear.
TEST killall -9 glusterd
sleep 5
TEST killall -9 glusterfsd
sleep 5

# Restart glusterd.  This is where the brick daemon supposedly dumps core,
# though I (jdarcy) have yet to see that.  Again, give it a while to settle,
# just to be sure.
TEST glusterd

cleanup_func
trap - EXIT
cleanup
