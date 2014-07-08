#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume start $V0

# mount with auxillary gfid mount
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0 --aux-gfid-mount

# create file with specific gfid
uuid=`uuidgen`
EXPECT "File creation OK" python2 $(dirname $0)/../utils/gfid-access.py $M0 ROOT file0 $uuid file

# check gfid
EXPECT "$uuid" getfattr -m . --only-values -n glusterfs.gfid.string $M0/file0

# unmount and mount again so as to start with a fresh inode table
# or use another mount...
TEST umount $M0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0 --aux-gfid-mount

# touch the file again (gfid-access.py handles errno)
EXPECT "File creation OK" python2 $(dirname $0)/../utils/gfid-access.py $M0 ROOT file0 $uuid file

cleanup;
