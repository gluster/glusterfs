#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd;

TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2,3};

TEST $CLI volume start $V0

TEST $CLI volume set $V0 performance.md-cache-timeout 600
TEST $CLI volume set $V0 performance.cache-samba-metadata on

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0

TEST touch $M0/file1

TEST "setfattr -n user.DOSATTRIB -v 0sAAOW $M0/file1"
TEST "getfattr -n user.DOSATTRIB $M0/file1 -e base64 | grep -q 0sAAOW"

TEST "setfattr -n user.DOSATTRIB -v 0x00ff $M0/file1"
TEST "getfattr -n user.DOSATTRIB $M0/file1 -e hex | grep -q 0x00ff"

cleanup;
