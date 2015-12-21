#!/bin/bash

## Test case for BZ 1266818;
## Disabling enable-shared-storage option should not delete user created
## volume with name glusterd_shared_storage

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

cleanup;

## Start a 2 node virtual cluster
TEST launch_cluster 2;

## Peer probe server 2 from server 1 cli
TEST $CLI_1 peer probe $H2;

EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

## Creating a volume with name glusterd_shared_storage
TEST $CLI_1 volume create glusterd_shared_storage  $H1:$B1/${V0}0 $H2:$B1/${V0}1

## Disabling enable-shared-storage should not succeed and should not delete the
## user created volume with name "glusterd_shared_storage"
TEST ! $CLI_1 volume all enable-shared-storage disable

## Volume with name should exist
TEST $CLI_1 volume info glusterd_shared_storage

cleanup;





