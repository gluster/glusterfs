#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;

volname="StartMigrationDuringRebalanceTest"
TEST glusterd
TEST pidof glusterd;

TEST $CLI volume info;
TEST $CLI volume create $volname $H0:$B0/${volname}{1,2};
TEST $CLI volume start $volname;
TEST $CLI volume rebalance $volname start;

cleanup;



