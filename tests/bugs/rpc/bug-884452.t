#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0
TEST $CLI volume start $V0

TEST glusterfs -s $H0 --volfile-id $V0 $M0
TEST touch $M0/{1..10000}

RUN_LS_LOOP_FILE="$M0/run-ls-loop"
function ls-loop
{
  while [ -f $RUN_LS_LOOP_FILE ]; do
    ls -lR $M0 1>/dev/null 2>&1
  done;
}

touch $RUN_LS_LOOP_FILE
ls-loop &

function vol-status-loop
{
  for i in {1..1000}; do
    $CLI volume status $V0 clients >/dev/null 2>&1
    if [ $? -ne 0 ]; then
      return 1
    fi
  done;

  return 0
}

TEST vol-status-loop

rm -f $RUN_LS_LOOP_FILE
wait

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

cleanup;
