#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info
TEST $CLI volume create $V0 replica 2 $H0:$B0/brick0 $H0:$B0/brick1
TEST $CLI volume start $V0
sleep 5

mount -t nfs -o vers=3,nolock `hostname`:/$V0 $N0

cd $N0
mkdir test_hardlink_self_heal;
cd test_hardlink_self_heal;

for i in `seq 1 5`;
do
    mkdir dir.$i;
    for j in `seq 1 10`;
    do
        dd if=/dev/zero of=dir.$i/file.$j bs=1k count=$j > /dev/null 2>&1;
    done;
done;

cd ..
kill  `cat /var/lib/glusterd/vols/$V0/run/$H0-d-backends-brick0.pid`
sleep 2


cd test_hardlink_self_heal;

RET=0
for i in `seq 1 5`;
do
    for j in `seq 1 10`;
    do
        ln dir.$i/file.$j dir.$i/link_file.$j > /dev/null 2>&1;
        RET=$?
        if [ $RET -ne 0 ]; then
           break;
        fi
    done ;
    if [ $RET -ne 0 ]; then
        break;
    fi
done;

cd
umount $N0

EXPECT "0" echo $RET;

cleanup;
