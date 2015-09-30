#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

## Create a distribute volume (B=2)
TEST $CLI volume create $V0 $H0:$B0/${V0}1 $H0:$B0/${V0}2;
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT '2' brick_count $V0


## Start the volume
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Mount the volume
TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0;

echo "123" >> $M0/file;

TEST ! setfattr -n "trusted.glusterfs.set-signature" -v "123" $M0/file;
TEST ! setfattr -n "trusted.glusterfs.get-signature" -v "123" $M0/file;

# sign xattr
TEST ! setfattr -n "trusted.bit-rot.signature" -v "123" $M0/file;
TEST ! setfattr -x "trusted.bit-rot.signature" $M0/file;

# versioning xattr
TEST ! setfattr -n "trusted.bit-rot.version" -v "123" $M0/file;
TEST ! setfattr -x "trusted.bit-rot.version" $M0/file;

# bad file xattr
TEST ! setfattr -n "trusted.bit-rot.bad-file" -v "123" $M0/file;
TEST ! setfattr -x "trusted.bit-rot.bad-file" $M0/file;

cleanup;
