#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

## Create a volume with one brick
TEST $CLI volume create $V0 $H0:$B0/${V0}1;
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT '1' brick_count $V0

## Turn off performance translators
## This is required for testing readv calls
TEST $CLI volume set $V0 performance.io-cache off
EXPECT 'off' volinfo_field $V0 'performance.io-cache'
TEST $CLI volume set $V0 performance.quick-read off
EXPECT 'off' volinfo_field $V0 'performance.quick-read'

TEST $CLI volume set $V0 performance.strict-write-ordering on
EXPECT 'on' volinfo_field $V0 'performance.strict-write-ordering'

## Turn on cdc xlator by setting network.compression to on
TEST $CLI volume set $V0 network.compression on
EXPECT 'on' volinfo_field $V0 'network.compression'

## Turn on network.compression.debug option
## This will dump compressed data onto disk as gzip file
## This is used to check if compression actually happened
TEST $CLI volume set $V0 network.compression.debug on
EXPECT 'on' volinfo_field $V0 'network.compression.debug'

## don't use min-size, so any size will be compressed.
TEST $CLI volume set $V0 network.compression.min-size 0

## Start the volume
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

sleep 2
## Mount FUSE with caching disabled
TEST $GFS -s $H0 --volfile-id $V0 $M0;

####################
## Testing writev ##
####################

## Create a 1K file locally and find the sha256sum
TEST dd if=/dev/zero of=/tmp/cdc-orig count=1 bs=1K 2>/dev/null
checksum[original-file]=`sha256sum /tmp/cdc-orig | cut -d' ' -f1`

## Copy the file to mountpoint and find its sha256sum on brick
TEST cp /tmp/cdc-orig $M0/cdc-server
checksum[brick-file]=`sha256sum $B0/${V0}1/cdc-server | cut -d' ' -f1`


## Check if all 3 checksums are same
TEST test ${checksum[original-file]} = ${checksum[brick-file]}

###################
## Testing readv ##
###################

## Copy file from mount point to client and find checksum
TEST cp $M0/cdc-server /tmp/cdc-client
checksum[client-file]=`sha256sum /tmp/cdc-client | cut -d' ' -f1`


## Check if all 3 checksums are same
TEST test ${checksum[brick-file]} = ${checksum[client-file]}

## Cleanup files and unmount
# TEST rm -f /tmp/cdc* $M0/cdc*
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

## Stop the volume
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

## Turn on network.compression.min-size and set it to 100 bytes
## Compression should not take place if file size
## is less than 100 bytes
TEST $CLI volume set $V0 network.compression.min-size 100
EXPECT '100' volinfo_field $V0 'network.compression.min-size'

## Start the volume
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Mount FUSE with caching disabled
TEST $GFS -s $H0 --volfile-id $V0 $M0;

## Create a file of size 99 bytes on mountpoint
## This is should not be compressed
TEST dd if=/dev/zero of=$M0/cdc-small count=1 bs=99 2>/dev/null

## Cleanup files and unmount
TEST rm -f /tmp/cdc* $M0/cdc*
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

## Stop the volume
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

## Reset the network.compression options
TEST $CLI volume reset $V0 network.compression.debug
TEST $CLI volume reset $V0 network.compression.min-size
TEST $CLI volume reset $V0 network.compression

## Delete the volume
TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
