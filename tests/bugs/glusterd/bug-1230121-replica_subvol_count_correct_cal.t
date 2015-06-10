#!/bin/bash

## Test case for BZ:1230121  glusterd crashed while trying to remove a bricks
## one selected from each replica set - after shrinking nX3 to nX2 to nX1

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

cleanup;

## Start a 2 node virtual cluster
TEST launch_cluster 2;
TEST pidof glusterd

## Peer probe server 2 from server 1 cli
TEST $CLI_1 peer probe $H2;

EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

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

cleanup;
