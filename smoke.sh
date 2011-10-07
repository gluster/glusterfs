#!/bin/bash

set -e;

M=/mnt;
P=/build;
H=$(hostname);
T=600;
V=patchy;


function cleanup()
{
    killall -15 glusterfs glusterfsd glusterd glusterd 2>&1 || true;
    killall -9 glusterfs glusterfsd glusterd glusterd 2>&1 || true;
    umount -l $M 2>&1 || true;
    rm -rf /etc/glusterd $P/export;
}

function start_fs()
{
    mkdir -p $P/export;
    chmod 0755 $P/export;

    glusterd;
    gluster --mode=script volume create $V replica 2 $H:$P/export/export{1,2,3,4};
    gluster volume start $V;
    glusterfs -s $H --volfile-id $V $M;
#    mount -t glusterfs $H:/$V $M;
}


function run_tests()
{
    cd $M;

    (sleep 1; dbench -s -t 60 10 >/dev/null) &

    (sleep 1; /opt/qa/tools/posix_compliance.sh) &

    wait %2
    wait %3

    rm -rf clients;

    cd -;
}


function watchdog ()
{
    # insurance against hangs during the test

    sleep $1;

    echo "Kicking in watchdog after $1 secs";

    cleanup;
}


function finish ()
{
    cleanup;
    kill %1;
}

function main ()
{
    cleanup;

    watchdog $T &

    trap finish EXIT;

    set -x;

    start_fs;

    run_tests;
}

main "$@";
