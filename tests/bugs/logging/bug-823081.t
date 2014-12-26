#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;
cmd_log_history="cmd_history.log"
V1=patchy2

TEST glusterd
TEST pidof glusterd

logdir=`gluster --print-logdir`
function set_tail ()
{
        vol=$1;
        tail_success="volume create $vol $H0:$B0/${vol}1 $H0:$B0/${vol}2 : SUCCESS"
        tail_failure="volume create $vol $H0:$B0/${vol}1 $H0:$B0/${vol}2 : FAILED : Volume $vol already exists"
        tail_success_force="volume create $vol $H0:$B0/${vol}1 $H0:$B0/${vol}2 force : SUCCESS"
        tail_failure_force="volume create $vol $H0:$B0/${vol}1 $H0:$B0/${vol}2 force : FAILED : Volume $vol already exists"
}

set_tail $V0;

TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};
tail=`tail -n 1 $logdir/$cmd_log_history | cut -d " " -f 5-`
TEST [[ \"$tail\" == \"$tail_success\" ]]

TEST ! $CLI volume create $V0 $H0:$B0/${V0}{1,2};
tail=`tail -n 1 $logdir/$cmd_log_history | cut -d " " -f 5-`
TEST [[ \"$tail\" == \"$tail_failure\" ]]

set_tail $V1;
TEST gluster volume create $V1 $H0:$B0/${V1}{1,2} force;
tail=`tail -n 1 $logdir/$cmd_log_history | cut -d " " -f 5-`
TEST [[ \"$tail\" == \"$tail_success_force\" ]]

TEST ! gluster volume create $V1 $H0:$B0/${V1}{1,2} force;
tail=`tail -n 1 $logdir/$cmd_log_history | cut -d " " -f 5-`
TEST [[ \"$tail\" == \"$tail_failure_force\" ]]

cleanup;
