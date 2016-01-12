#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}0 $H0:$B0/${V0}1 $H0:$B0/${V0}2 $H0:$B0/${V0}3
TEST $CLI volume start $V0

TEST glusterfs -s $H0 --volfile-id $V0 $M0;
TEST mkdir -p $M0/limit_one/limit_two/limit_three $M0/limit_four  \
              $M0/limit_one/limit_five

TEST $CLI volume set $V0 server.root-squash on
TEST $CLI volume quota $V0 enable

TEST $CLI volume quota $V0 limit-usage / 1GB
TEST $CLI volume quota $V0 limit-usage /limit_one 1GB
TEST $CLI volume quota $V0 limit-usage /limit_one/limit_two 1GB
TEST $CLI volume quota $V0 limit-usage /limit_one/limit_two/limit_three 1GB
TEST $CLI volume quota $V0 limit-usage /limit_four 1GB
TEST $CLI volume quota $V0 limit-usage /limit_one/limit_five 1GB

cleanup;
