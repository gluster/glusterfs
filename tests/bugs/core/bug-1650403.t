#!/bin/bash

SCRIPT_TIMEOUT=500

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../traps.rc

cleanup;

NUM_VOLS=5
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
TESTS_EXPECTED_IN_LOOP=24
for i in $(seq 1 $NUM_VOLS); do
	create_volume $i
	TEST dd if=/dev/zero of=$(get_mount_point $i)/a_file bs=4k count=1
        # Unmounting to reduce memory footprint on regression hosts
        mnt_point=$(get_mount_point $i)
        EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $mnt_point
done

glustershd_pid=`ps auxwww | grep glustershd | grep -v grep | awk -F " " '{print $2}'`
TEST [ $glustershd_pid != 0 ]
start=`pmap -x $glustershd_pid | grep total | awk -F " " '{print $4}'`
echo "Memory consumption for glustershd process"
for i in $(seq 1 50); do
        pmap -x $glustershd_pid | grep total
        for j in $(seq 1 $NUM_VOLS); do
                vol_name=$(printf "%s-vol%02d" $V0 $j)
                gluster v set $vol_name cluster.self-heal-daemon off > /dev/null
                gluster v set $vol_name cluster.self-heal-daemon on  > /dev/null
        done
done

end=`pmap -x $glustershd_pid | grep total | awk -F " " '{print $4}'`
diff=$((end-start))

# If memory consumption is more than 10M it means some leak in reconfigure
# code path

TEST [ $diff -lt 10000 ]

trap - EXIT
cleanup
