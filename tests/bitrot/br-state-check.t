#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../nfs.rc

cleanup;

TEST glusterd
TEST pidof glusterd

## Create a distribute volume
TEST $CLI volume create $V0 $H0:$B0/${V0}1 $H0:$B0/${V0}2 $H0:$B0/${V0}3;
TEST $CLI volume start $V0;

## Enable bitrot on volume $V0
TEST $CLI volume bitrot $V0 enable

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_bitd_count
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_scrubd_count

## perform a series of scrub related state change tests. As of now, there'
## no way to check if a given change has been correctly acknowledged by
## the scrub process as there isn't an _interface_ to check scrub internal
## state (yet). What's been verified here is scrub state machine execution
## w.r.t. locking and faults.

## 0x0: verify scrub rescheduling
TEST $CLI volume bitrot $V0 scrub-frequency monthly
TEST $CLI volume bitrot $V0 scrub-frequency daily
TEST $CLI volume bitrot $V0 scrub-frequency hourly

## 0x1: test reschedule after pause/resume
TEST $CLI volume bitrot $V0 scrub pause
TEST $CLI volume bitrot $V0 scrub-frequency daily
TEST $CLI volume bitrot $V0 scrub resume

## 0x2: test reschedule w/ an offline brick
TEST kill_brick $V0 $H0 $B0/${V0}1

TEST $CLI volume bitrot $V0 scrub-frequency hourly
TEST $CLI volume bitrot $V0 scrub-throttle aggressive

## 0x3: test pause/resume w/ an offline brick
TEST $CLI volume bitrot $V0 scrub pause
TEST $CLI volume bitrot $V0 scrub-frequency monthly
TEST $CLI volume bitrot $V0 scrub resume

## 0x4: test "start" from a paused scrub state

TEST $CLI volume bitrot $V0 scrub pause
TEST $CLI volume start $V0 force

## 0x4a: try pausing an already paused scrub
TEST ! $CLI volume bitrot $V0 scrub pause

## 0x4b: perform configuration changes
TEST $CLI volume bitrot $V0 scrub-frequency hourly
TEST $CLI volume bitrot $V0 scrub-throttle lazy
TEST $CLI volume bitrot $V0 scrub resume

## 0x5: test cleanup upon brick going offline
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST kill_brick $V0 $H0 $B0/${V0}3

## 0x6: test cleanup upon brick going offline when srubber is paused
##      (initially paused and otherwise)

## 0x6a: initially paused case
TEST $CLI volume bitrot $V0 scrub pause
TEST $CLI volume start $V0 force
TEST kill_brick $V0 $H0 $B0/${V0}3
TEST $CLI volume bitrot $V0 scrub resume

## 0x6b: paused _after_ execution
TEST $CLI volume start $V0 force
TEST $CLI volume bitrot $V0 scrub pause
TEST kill_brick $V0 $H0 $B0/${V0}2

cleanup;
#G_TESTDEF_TEST_STATUS_NETBSD7=KNOWN_ISSUE,BUG=1332473
