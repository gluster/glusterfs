#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

function get_fd_count {
        local vol=$1
        local host=$2
        local brick=$3
        local fname=$4
        local gfid_str=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $brick/$fname))
        local statedump=$(generate_brick_statedump $vol $host $brick)
        local count=$(grep "gfid=$gfid_str" $statedump -A2 | grep fd-count | cut -f2 -d'=' | tail -1)
        rm -f $statedump
        echo $count
}
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
