#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}1

EXPECT "$V0" volinfo_field $V0 'Volume Name'
EXPECT 'Created' volinfo_field $V0 'Status'

TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'

## Mount FUSE with caching disabled (read-write)
TEST $GFS -s $H0 --volfile-id $V0 $M0

## Tests for the volume level WORM
TEST `echo "File 1" > $M0/file1`
TEST touch $M0/file2

## Enable the volume level WORM
TEST $CLI volume set $V0 features.worm 1
TEST ! mv $M0/file1 $M0/file11
TEST `echo "block" > $M0/file2`

## Disable the volume level WORM and delete the legacy files
TEST $CLI volume set $V0 features.worm 0
TEST rm -f $M0/*

## Enable file level WORM
TEST $CLI volume set $V0 features.worm-file-level 1
TEST $CLI volume set $V0 features.default-retention-period 10
TEST $CLI volume set $V0 features.auto-commit-period 5

## Tests for manual transition to WORM/Retained state
TEST `echo "worm 1" > $M0/file1`
TEST chmod 0444 $M0/file1
sleep 5
TEST `echo "line 1" > $M0/file1`
TEST ! mv $M0/file1 $M0/file2
sleep 10
TEST ! link $M0/file1 $M0/file2
sleep 5
TEST rm -f $M0/file1

TEST $CLI volume stop $V0
EXPECT 'Stopped' volinfo_field $V0 'Status'

cleanup;
