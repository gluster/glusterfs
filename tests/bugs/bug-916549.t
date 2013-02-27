#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;

TEST glusterd;
TEST $CLI volume create $V0 $H0:$B0/${V0}1;
TEST $CLI volume start $V0;

pid_file=$(ls /var/lib/glusterd/vols/$V0/run);
brick_pid=$(cat /var/lib/glusterd/vols/$V0/run/$pid_file);


kill -SIGKILL $brick_pid;
TEST $CLI volume start $V0 force;
TEST process_leak_count $(pidof glusterd);

cleanup;
