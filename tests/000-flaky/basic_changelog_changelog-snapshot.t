#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../snapshot.rc

cleanup;
ROLLOVER_TIME=3

TEST verify_lvm_version;
TEST glusterd;
TEST pidof glusterd;

TEST setup_lvm 1

TEST $CLI volume create $V0 $H0:$L1
BRICK_LOG=$(echo "$L1" | tr / - | sed 's/^-//g')
TEST $CLI volume start $V0

#Enable changelog
TEST $CLI volume set $V0 changelog.changelog on
TEST $CLI volume set $V0 changelog.rollover-time $ROLLOVER_TIME
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;

#Create snapshot
S1="${V0}-snap1"

mkdir $M0/RENAME
mkdir $M0/LINK
mkdir $M0/UNLINK
mkdir $M0/RMDIR
mkdir $M0/SYMLINK

for i in {1..400} ; do touch $M0/RENAME/file$i; done
for i in {1..400} ; do touch $M0/LINK/file$i; done
for i in {1..400} ; do touch $M0/UNLINK/file$i; done
for i in {1..400} ; do mkdir $M0/RMDIR/dir$i; done
for i in {1..400} ; do touch $M0/SYMLINK/file$i; done

#Write I/O in background
for i in {1..400} ; do touch $M0/file$i 2>/dev/null; done &
for i in {1..400} ; do mknod $M0/mknod-file$i p 2>/dev/null; done &
for i in {1..400} ; do mkdir $M0/dir$i 2>/dev/null; done & 2>/dev/null
for i in {1..400} ; do mv $M0/RENAME/file$i $M0/RENAME/rn-file$i 2>/dev/null; done &
for i in {1..400} ; do ln $M0/LINK/file$i $M0/LINK/ln-file$i 2>/dev/null; done &
for i in {1..400} ; do rm -f $M0/UNLINK/file$i 2>/dev/null; done &
for i in {1..400} ; do rmdir $M0/RMDIR/dir$i 2>/dev/null; done &
for i in {1..400} ; do ln -s $M0/SYMLINK/file$i $M0/SYMLINK/sym-file$i 2>/dev/null; done &

sleep 1
TEST $CLI snapshot create $S1 $V0 no-timestamp
TEST snapshot_exists 0 $S1

TEST grep '"Enabled changelog barrier"' /var/log/glusterfs/bricks/$BRICK_LOG.log
TEST grep '"Disabled changelog barrier"' /var/log/glusterfs/bricks/$BRICK_LOG.log

TEST glusterfs -s $H0 --volfile-id=/snaps/$S1/$V0 $M1

#Clean up
TEST $CLI volume stop $V0 force
cleanup;
