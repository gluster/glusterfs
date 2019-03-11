#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

## Start glusterd
TEST glusterd;
TEST pidof glusterd;

## Lets create and start the volume
TEST $CLI volume create $V0 $H0:$B0/${V0}1
TEST $CLI volume start $V0

## Enable bitrot for volume $V0
TEST $CLI volume bitrot $V0 enable

## Turn off quick-read so that it wont cache the contents
# of the file in lookup. For corrupted files, it might
# end up in reads being served from the cache instead of
# an error.
TEST $CLI volume set $V0 performance.quick-read off

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_bitd_count

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Active' scrub_status $V0 'State of scrub'
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '/var/log/glusterfs/bitd.log' scrub_status $V0 'Bitrot error log location'
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '/var/log/glusterfs/scrub.log' scrub_status $V0 'Scrubber error log location'

## Set expiry-timeout to 1 sec
TEST $CLI volume set $V0 features.expiry-time 1

##Mount $V0
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0

## Turn off quick-read xlator so that, the contents are not served from the
# quick-read cache.
TEST $CLI volume set $V0 performance.quick-read off

#Create sample file
TEST `echo "1234" > $M0/FILE1`
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'trusted.bit-rot.signature' check_for_xattr 'trusted.bit-rot.signature' "/$B0/${V0}1/FILE1"

##disable bitrot
TEST $CLI volume bitrot $V0 disable

## modify the file
TEST `echo "write" >> $M0/FILE1`

# unmount and remount when the file has to be accessed.
# This is to ensure that, when the remount happens,
# and the file is read, its contents are served from the
# brick instead of cache.
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

##enable bitrot
TEST $CLI volume bitrot $V0 enable

# expiry time is set to 1 second. Hence sleep for 2 seconds for the
# oneshot crawler to finish its crawling and sign the file properly.
sleep 2

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_bitd_count

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Active' scrub_status $V0 'State of scrub'
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '/var/log/glusterfs/bitd.log' scrub_status $V0 'Bitrot error log location'
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '/var/log/glusterfs/scrub.log' scrub_status $V0 'Scrubber error log location'

## Ondemand scrub
TEST $CLI volume bitrot $V0 scrub ondemand

# the scrub ondemand CLI command, just ensures that
# the scrubber has received the ondemand scrub directive
# and started. sleep for 2 seconds for scrubber to finish
# crawling and marking file(s) as bad (if if finds that
# corruption has happened) which are filesystem operations.
sleep 2

TEST ! getfattr -n 'trusted.bit-rot.bad-file' $B0/${V0}1/FILE1

##Mount $V0
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0

TEST cat $M0/FILE1

cleanup;
