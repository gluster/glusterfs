#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

# Create, start and mount the volume.
TEST glusterd;
TEST $CLI volume create $V0 $H0:$B0/$V0;
TEST $CLI volume start $V0;
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0

# Compile the test program
TEST $CC -Wall $(dirname $0)/bug-1175711.c -o $(dirname $0)/bug-1175711

# Create directory and some entries inside them.
mkdir -p $M0/dir-bug-1175711
mkdir -p $M0/dir-bug-1175711/DT_DIR
touch $M0/dir-bug-1175711/DT_REG

# Invoke the test program and pass path of directory to it.
TEST $(dirname $0)/bug-1175711 $M0/dir-bug-1175711

# Unmount, stop and delete the volume
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;

cleanup;
