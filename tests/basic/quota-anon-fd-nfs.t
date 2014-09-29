#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume set $V0 network.inode-lru-limit 1

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST mount -t nfs localhost:/$V0 $N0
sleep 10
deep=/0/1/2/3/4/5/6/7/8/9
TEST mkdir -p $N0/$deep

TEST dd if=/dev/zero of=$N0/$deep/file bs=1K count=1M

TEST $CLI volume quota $V0 enable
TEST $CLI volume quota $V0 limit-usage / 2GB
TEST $CLI volume quota $V0 soft-timeout 0

sleep 10
TEST dd if=/dev/zero of=$N0/$deep/newfile_1 bs=500 count=1M
TEST ! dd if=/dev/zero of=$N0/$deep/newfile_2 bs=1000 count=1M

cleanup;
