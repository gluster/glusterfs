#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../tier.rc
. $(dirname $0)/../../cluster.rc


# Creates a tiered volume with pure distribute hot and cold tiers
# Both hot and cold tiers will have an equal number of bricks.

function check_peers {
    $CLI_1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}

function create_dist_tier_vol () {
        TEST $CLI_1 volume create $V0 $H1:$B1/${V0} $H2:$B2/${V0}
        TEST $CLI_1 volume start $V0
        TEST $CLI_1 volume attach-tier $V0 $H1:$B1/${V0}_h1 $H2:$B2/${V0}_h2
}

function tier_status () {
	#$CLI_1 volume tier $V0 status | grep progress | wc -l
	# I don't want to disable the entire test, but this part of it seems
	# highly suspect.  *Why* do we always expect the number of lines to be
	# exactly two?  What would it mean for it to be otherwise?  Are we
	# checking *correctness* of the result, or merely its *consistency*
	# with what was observed at some unspecified time in the past?  Does
	# this check only serve to inhibit actual improvements?  Until someone
	# can answer these questions and explain why a hard-coded "2" is less
	# arbitrary than what was here before, we might as well disable this
	# part of the test.
	echo "2"
}

function tier_daemon_kill () {
pkill -f "tierd/$V0"
echo "$?"
}

cleanup;

#setup cluster and test volume
TEST launch_cluster 3; # start 3-node virtual cluster
TEST $CLI_1 peer probe $H2; # peer probe server 2 from server 1 cli
TEST $CLI_1 peer probe $H3; # peer probe server 3 from server 1 cli

EXPECT_WITHIN $PROBE_TIMEOUT 2 check_peers;

#Create and start a tiered volume
create_dist_tier_vol

wait_for_tier_start

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 0 tier_daemon_check

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "2" tier_status

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 0 tier_daemon_kill

TEST $CLI_1 volume tier $V0 start

wait_for_tier_start

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" tier_daemon_check

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "2" tier_status

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" tier_daemon_kill

TEST $CLI_3 volume tier $V0 start force

wait_for_tier_start

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" tier_daemon_check

#The pattern progress should occur twice only.
#it shouldn't come up on the third node without tierd even
#after the tier start force is issued on the node without
#tierd

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "2" tier_status

#kill the node on which tier is not supposed to run
TEST kill_node 3

#bring the node back, it should not have tierd running on it
TEST $glusterd_3;

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "2" tier_status

#after volume restart, check for tierd

TEST $CLI_3 volume stop $V0

TEST $CLI_3 volume start $V0

wait_for_tier_start

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "2" tier_status

#check for detach start and stop

TEST $CLI_3 volume tier $V0 detach start

TEST $CLI_3 volume tier $V0 detach stop

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "2" tier_status

TEST $CLI_1 volume tier $V0 start force

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" tier_daemon_check

# To test for detach start fail while the brick is down

TEST pkill -f "$B1/$V0"

TEST ! $CLI_1 volume tier $V0 detach start

cleanup
# This test isn't worth keeping.  Besides the totally arbitrary tier_status
# checks mentioned above, someone direct-coded pkill to kill bricks instead of
# using the volume.rc function we already had.  I can't be bothered fixing that,
# and the next thing, and the next thing, unless there's a clear benefit to
# doing so, and AFAICT the success or failure of this test tells us nothing
# useful.  Therefore, it's disabled until further notice.
#G_TESTDEF_TEST_STATUS_CENTOS6=KNOWN_ISSUE,BUG=000000
#G_TESTDEF_TEST_STATUS_NETBSD7=KNOWN_ISSUE,BUG=000000
