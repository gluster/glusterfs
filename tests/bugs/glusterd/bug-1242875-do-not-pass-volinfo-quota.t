#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

cleanup;

## Start glusterd
TEST glusterd;
TEST pidof glusterd;

## Lets create volume V0 and start the volume
TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1
TEST $CLI volume start $V0

## Lets create volume V1 and start the volume
TEST $CLI volume create $V1 $H0:$B0/${V0}2 $H0:$B0/${V0}3
TEST $CLI volume start $V1

## Enable quota on 2nd volume
TEST $CLI volume quota $V1 enable
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_quotad_count

## Killing all gluster process
pkill gluster;

## there should not be any quota daemon running after killing quota process
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" get_quotad_count

## Start glusterd
TEST glusterd;
TEST pidof glusterd;

## Quotad daemon should start on restarting the glusterd
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_quotad_count

cleanup;
