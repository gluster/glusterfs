#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc

cleanup;

TEST launch_cluster 2;

#bug-1047955 - remove brick from new peer in cluster
TEST $CLI_1 volume create $V0 replica 2 $H1:$B1/${V0}{1,2,3,4}
TEST $CLI_1 volume start $V0;

TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

TEST $CLI_2 volume remove-brick $V0 $H1:$B1/${V0}{3,4} start;
TEST $CLI_2 volume info

#bug-964059 - volume status post remove brick start
TEST $CLI_1 volume create $V1 $H1:$B1/${V1}0 $H2:$B2/${V1}1
TEST $CLI_1 volume start $V1
TEST $CLI_1 volume remove-brick $V1 $H2:$B2/${V1}1 start
TEST $CLI_1 volume status

TEST $CLI_1 volume stop $V0
TEST $CLI_1 volume delete $V0

#bug-1230121 - decrease replica count by remove-brick and increse by add-brick
## Creating a 2x3 replicate volume
TEST $CLI_1 volume create $V0 replica 3 $H1:$B1/brick1 $H2:$B2/brick2 \
                                        $H1:$B1/brick3 $H2:$B2/brick4 \
                                        $H1:$B1/brick5 $H2:$B2/brick6

## Start the volume
TEST $CLI_1 volume start $V0

## Shrinking volume replica 2x3 to 2x2 by performing remove-brick operation.
TEST $CLI_1 volume remove-brick $V0 replica 2 $H1:$B1/brick1 $H2:$B2/brick6 force

## Shrinking volume replica 2x2 to 1x2 by performing remove-brick operation
TEST $CLI_1 volume remove-brick $V0 replica 2 $H1:$B1/brick3 $H2:$B2/brick2 force

## Shrinking volume replica from 1x2 to 1x1 by performing remove-brick operation
TEST $CLI_1 volume remove-brick $V0 replica 1 $H1:$B1/brick5 force


### Expanding volume replica by performing add-brick operation.

## Expend volume replica from 1x1 to 1x2 by performing add-brick operation
TEST $CLI_1 volume add-brick $V0 replica 2 $H1:$B1/brick5 force

## Expend volume replica from 1x2 to 2x2 by performing add-brick operation
TEST $CLI_1 volume add-brick $V0 replica 2 $H1:$B1/brick3 $H2:$B2/brick2 force

## Expend volume replica from 2x2 to 2x3 by performing add-brick operation
TEST $CLI_1 volume add-brick $V0 replica 3 $H1:$B1/brick1 $H2:$B2/brick6 force

cleanup

