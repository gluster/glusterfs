#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

function file_mime_type () {
    mime_type=$(file --mime $1 2>/dev/null | sed '/^[^:]*: /s///')
    echo $mime_type
}

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

## Make sure that user cannot change network.compression.mode
## This would break the cdc xlator if allowed!
TEST ! $CLI volume set $V0 network.compression.mode client

## Turn on network.compression.debug option
## This will dump compressed data onto disk as gzip file
## This is used to check if compression actually happened
TEST $CLI volume set $V0 network.compression.debug on
EXPECT 'on' volinfo_field $V0 'network.compression.debug'

## Start the volume
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

sleep 2
## Mount FUSE with caching disabled
TEST $GFS -s $H0 --volfile-id $V0 $M0;

####################
## Testing writev ##
####################

## Create a 1K file locally and find the md5sum
TEST dd if=/dev/zero of=/tmp/cdc-orig count=1 bs=1k 2>/dev/null
checksum[original-file]=`md5sum /tmp/cdc-orig | cut -d' ' -f1`

## Copy the file to mountpoint and find its md5sum on brick
TEST dd if=/tmp/cdc-orig of=$M0/cdc-server count=1 bs=1k 2>/dev/null
checksum[brick-file]=`md5sum $B0/${V0}1/cdc-server | cut -d' ' -f1`

## Uncompress the gzip dump file and find its md5sum
# mime outputs for gzip are different for file version > 5.14
TEST touch /tmp/gzipfile
TEST gzip /tmp/gzipfile
GZIP_MIME_TYPE=$(file_mime_type /tmp/gzipfile.gz)

TEST rm -f /tmp/gzipfile.gz

EXPECT "$GZIP_MIME_TYPE" echo $(file_mime_type /tmp/cdcdump.gz)

TEST gunzip -f /tmp/cdcdump.gz
checksum[dump-file-writev]=`md5sum /tmp/cdcdump | cut -d' ' -f1`

## Check if all 3 checksums are same
TEST test ${checksum[original-file]} = ${checksum[brick-file]}
TEST test ${checksum[brick-file]} = ${checksum[dump-file-writev]}

## Cleanup files
TEST rm -f /tmp/cdcdump.gz

###################
## Testing readv ##
###################

## Copy file from mount point to client and find checksum
TEST dd if=$M0/cdc-server of=/tmp/cdc-client count=1 bs=1k 2>/dev/null
checksum[client-file]=`md5sum /tmp/cdc-client | cut -d' ' -f1`

## Uncompress the gzip dump file and find its md5sum
# mime outputs for gzip are different for file version > 5.14
EXPECT "$GZIP_MIME_TYPE" echo $(file_mime_type /tmp/cdcdump.gz)

TEST gunzip -f /tmp/cdcdump.gz
checksum[dump-file-readv]=`md5sum /tmp/cdcdump | cut -d' ' -f1`

## Check if all 3 checksums are same
TEST test ${checksum[brick-file]} = ${checksum[client-file]}
TEST test ${checksum[client-file]} = ${checksum[dump-file-readv]}

## Cleanup files and unmount
TEST rm -f /tmp/cdc* $M0/cdc*
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
TEST ! test -e /tmp/cdcdump.gz

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
