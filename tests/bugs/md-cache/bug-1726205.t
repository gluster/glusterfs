#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd;

TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2,3};

TEST $CLI volume start $V0

TEST $CLI volume set $V0 group samba

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0

TEST touch $M0/file
TEST "setfattr -n "user.DosStream.Zone.Identifier:\$DATA" -v '\0' $M0/file"
TEST "getfattr -n "user.DosStream.Zone.Identifier:\$DATA" -e hex $M0/file | grep -q 0x00"

cleanup;
