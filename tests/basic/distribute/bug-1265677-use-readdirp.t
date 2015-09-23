#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This test checks use-readdirp disable/enable for dht

cleanup
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0..1}
TEST $CLI volume heal $V0 disable
TEST $CLI volume set $V0 nfs.disable yes
TEST $CLI volume set $V0 dht.force-readdirp yes
TEST $CLI volume set $V0 performance.readdir-ahead off
TEST $CLI volume set $V0 performance.force-readdirp no
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 --use-readdirp=no $M0;
TEST mkdir $M0/d
TEST touch $M0/d/{1..10}

TEST $CLI volume profile $V0 start
#Clear all the fops till now
TEST $CLI volume profile $V0 info

EXPECT "^10$" echo $(ls $M0/d | wc -l)
EXPECT_NOT "^0$" echo $($CLI volume profile $V0 info incremental | grep -w READDIRP | wc -l)
EXPECT "^10$" echo $(ls $M0/d | wc -l)
EXPECT "^0$" echo $($CLI volume profile $V0 info incremental | grep -w READDIR | wc -l)

TEST $CLI volume set $V0 dht.force-readdirp no
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "0" mount_get_option_value $M0 $V0-dht use-readdirp

EXPECT "^10$" echo $(ls $M0/d | wc -l)
EXPECT "^0$" echo $($CLI volume profile $V0 info incremental | grep -w READDIRP | wc -l)
EXPECT "^10$" echo $(ls $M0/d | wc -l)
EXPECT_NOT "^0$" echo $($CLI volume profile $V0 info incremental | grep -w READDIR | wc -l)

cleanup
