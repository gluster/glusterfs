#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fileio.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume set $V0 performance.open-behind off
TEST $CLI volume set $V0 performance.flush-behind off
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0 --direct-io-mode=enable
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M1 --attribute-timeout=0 --entry-timeout=0 --direct-io-mode=enable

TEST touch $M0/a

exec 4>"$M0/a"
exec 5>"$M1/a"
EXPECT "2" get_fd_count $V0 $H0 $B0/${V0}0 a

exec 4>&-
EXPECT "1" get_fd_count $V0 $H0 $B0/${V0}0 a

exec 5>&-
EXPECT "0" get_fd_count $V0 $H0 $B0/${V0}0 a

cleanup
