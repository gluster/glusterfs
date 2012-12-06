#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0
TEST $CLI volume start $V0

TEST glusterfs -s $H0 --volfile-id $V0 $M0
TEST touch $M0/{1..10000}

function ls-loop
{
  while true; do
    ls -lR $M0 1>/dev/null 2>&1
  done;
}
ls-loop &
LS_LOOP=$!

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

kill $LS_LOOP >/dev/null 2>&1
sleep 2

TEST umount $M0

cleanup;



