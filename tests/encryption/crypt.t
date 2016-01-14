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

TEST $CLI volume set $V0 performance.quick-read off
EXPECT 'off' volinfo_field $V0 'performance.quick-read'
TEST $CLI volume set $V0 performance.write-behind off
EXPECT 'off' volinfo_field $V0 'performance.write-behind'
TEST $CLI volume set $V0 performance.open-behind off
EXPECT 'off' volinfo_field $V0 'performance.open-behind'

## Create a file with master key

echo "0000111122223333444455556666777788889999aaaabbbbccccddddeeeeffff" > $GLUSTERD_WORKDIR/$V0-master-key

## Specify location of master key
TEST $CLI volume set $V0 encryption.master-key $GLUSTERD_WORKDIR/$V0-master-key

## Turn on crypt xlator by setting features.encryption to on
TEST $CLI volume set $V0 encryption on
EXPECT 'on' volinfo_field $V0 'features.encryption'

## Start the volume
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Mount the volume
TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0;

## Testing writev, readv, ftruncate:
## Create fragmented files and compare them with the reference files

build_tester $(dirname $0)/frag.c
TEST $(dirname $0)/frag $M0/testfile /tmp/$V0-goodfile 262144 500

## Testing link, unlink, symlink, rename

TEST ln $M0/testfile $M0/testfile-link
TEST mv $M0/testfile $M0/testfile-renamed
TEST ln -s $M0/testfile-link $M0/testfile-symlink
TEST rm -f $M0/testfile-renamed

## Remount the volume
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0;

TEST diff -u $M0/testfile-symlink /tmp/$V0-goodfile
EXPECT ''

TEST rm -f $M0/testfile-symlink
TEST rm -f $M0/testfile-link

## Cleanup files

TEST rm -f /tmp/$V0-master-key
TEST rm -f /tmp/$V0-goodfile

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

## Reset crypt options
TEST $CLI volume reset $V0 encryption.block-size
TEST $CLI volume reset $V0 encryption.data-key-size

## Stop the volume
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

## Delete the volume
TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

TEST rm -rf $(dirname $0)/frag
cleanup;
