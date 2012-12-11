#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;

function georep_start_and_check()
{
    local master=$1
    local slave=$2

    $CLI volume geo-replication $master $slave start
}

function georep_stop()
{
    local master=$1
    local slave=$2

    $CLI volume geo-replication $master $slave stop
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/brick0 $H0:$B0/brick1
TEST $CLI volume start $V0

sleep 5

slave=`mktemp -d`
mkdir -p $slave

# check normal functionality of geo-replication
EXPECT_KEYWORD "successful" georep_start_and_check $V0 $slave
TEST georep_stop $V0 $slave

# now invoke replace brick
TEST $CLI volume replace-brick $V0 $H0:$B0/brick1 $H0:$B0/brick2 start

# check if CLI refuses to start geo replication
EXPECT_KEYWORD "failed" georep_start_and_check $V0 $slave

# commit replace brick operation
TEST $CLI volume replace-brick $V0 $H0:$B0/brick1 $H0:$B0/brick2 commit

# geo replication should work as usual
EXPECT_KEYWORD "successful" georep_start_and_check $V0 $slave
TEST georep_stop $V0 $slave

rm -rf $slave
cleanup
