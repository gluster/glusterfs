#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

cleanup;

NUM_VOLS=15


get_brick_base () {
	printf "%s/vol%02d" $B0 $1
}

function count_up_bricks {
        vol=$1;
        $CLI_1 --xml volume status $vol | grep '<status>1' | wc -l
}

create_volume () {

	local vol_name=$(printf "%s-vol%02d" $V0 $1)

        TEST $CLI_1 volume create $vol_name replica 3 $H1:$B1/${vol_name} $H2:$B2/${vol_name} $H3:$B3/${vol_name}
	TEST $CLI_1 volume start $vol_name
}

TEST launch_cluster 3
TEST $CLI_1 volume set all cluster.brick-multiplex on

# The option accepts the value in the range from 5 to 200
TEST ! $CLI_1 volume set all glusterd.vol_count_per_thread 210
TEST ! $CLI_1 volume set all glusterd.vol_count_per_thread 4

TEST $CLI_1 volume set all glusterd.vol_count_per_thread 5

TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

TEST $CLI_1 peer probe $H3;
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count

# Our infrastructure can't handle an arithmetic expression here.  The formula
# is (NUM_VOLS-1)*5 because it sees each TEST/EXPECT once but needs the other
# NUM_VOLS-1 and there are 5 such statements in each iteration.
TESTS_EXPECTED_IN_LOOP=28
for i in $(seq 1 $NUM_VOLS); do
        starttime="$(date +%s)";
	create_volume $i
done

TEST kill_glusterd 1

TESTS_EXPECTED_IN_LOOP=4
for i in `seq 1 3 15`
do
vol1=$(printf "%s-vol%02d" $V0 $i)
TEST $CLI_2 volume set $vol1 performance.readdir-ahead on
done

# Bring back 1st glusterd
TEST $glusterd_1
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count

TESTS_EXPECTED_IN_LOOP=4
for i in `seq 1 3 15`
do
vol1=$(printf "%s-vol%02d" $V0 $i)
EXPECT_WITHIN $PROBE_TIMEOUT "on" volinfo_field_1 $vol1 performance.readdir-ahead
done

cleanup
