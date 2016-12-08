#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc

cleanup

TEST launch_cluster 3;
TEST $CLI_1 peer probe $H2;
TEST $CLI_1 peer probe $H3;
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count

TEST $CLI_1 volume create $V0 $H1:$B1/$V0 $H2:$B2/$V0
TEST $CLI_1 volume start $V0

kill_glusterd 2

#remove-brick should fail as the peer hosting the brick is down
TEST ! $CLI_1 volume remove-brick $V0 $H2:$B2/${V0} start

TEST start_glusterd 2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H2 $B2/${V0}

EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count

#volume status should work
TEST $CLI_2 volume status


TEST $CLI_1 volume remove-brick $V0 $H2:$B2/${V0} start
kill_glusterd 2

#remove-brick commit should fail as the peer hosting the brick is down
TEST ! $CLI_1 volume remove-brick $V0 $H2:$B2/${V0} commit

TEST start_glusterd 2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H2 $B2/${V0}

EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count

#volume status should work
TEST $CLI_2 volume status

TEST $CLI_1 volume remove-brick $V0 $H2:$B2/${V0} stop

kill_glusterd 3
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

TEST $CLI_1 volume remove-brick $V0 $H2:$B2/${V0} start

TEST start_glusterd 3
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count

TEST $CLI_3 volume status

cleanup
