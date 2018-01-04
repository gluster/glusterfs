#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

function shd_up_status_1 {
        $CLI_1 volume status | grep "localhost" | grep "Self-heal Daemon" | awk '{print $7}'
}

function shd_up_status_2 {
        $CLI_2 volume status | grep "localhost" | grep "Self-heal Daemon" | awk '{print $7}'
}

function get_shd_pid_2 {
        $CLI_2 volume status | grep "localhost" | grep "Self-heal Daemon" | awk '{print $8}'
}

cleanup;

function check_peers {
        $CLI_1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}

TEST launch_cluster 3
TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 check_peers

TEST $CLI_1 volume create $V0 replica 2 $H1:$B0/${V0} $H2:$B0/${V0}
TEST $CLI_1 volume start $V0

#testcase: bug-1507466 - validate reset-brick commit force
# Negative case with brick not killed && volume-id xattrs present
TEST ! $CLI_1 volume reset-brick $V0 $H1:$B0/${V0} $H1:$B0/${V0} commit force

TEST $CLI_1 volume reset-brick $V0 $H1:$B0/${V0} start
# Now test if reset-brick commit force works
TEST $CLI_1 volume reset-brick $V0 $H1:$B0/${V0} $H1:$B0/${V0} commit force

#testcase: bug-1383893 - shd should not come up after restarting the peer glusterd

TEST $CLI_1 peer probe $H3;
EXPECT_WITHIN $PROBE_TIMEOUT 2  peer_count

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H1 $B0/${V0}
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H2 $B0/${V0}
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" shd_up_status_1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" shd_up_status_2

# Bring down shd on 2nd node
kill -15 $(get_shd_pid_2)

# Bring down glusterd on 1st node
TEST kill_glusterd 1

#Bring back 1st glusterd
TEST $glusterd_1

# We need to wait till PROCESS_UP_TIMEOUT and then check shd service does not
# come up on node 2
sleep $PROCESS_UP_TIMEOUT
EXPECT "N" shd_up_status_2

cleanup;
