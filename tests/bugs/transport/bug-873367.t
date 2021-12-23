#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../traps.rc
. $(dirname $0)/../../ssl.rc

cleanup;

mkdir -p $B0/1
mkdir -p $M0

TEST create_self_signed_certs

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/1
TEST $CLI volume set $V0 server.ssl on
TEST $CLI volume set $V0 client.ssl on
TEST $CLI volume set $V0 ssl.certificate-depth 6
TEST $CLI volume set $V0 ssl.cipher-list HIGH
TEST $CLI volume set $V0 auth.ssl-allow Anyone
TEST $CLI volume start $V0

TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0
echo some_data > $M0/data_file
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

# If the bug is not fixed, the next mount will fail.

TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0
EXPECT some_data cat $M0/data_file

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
