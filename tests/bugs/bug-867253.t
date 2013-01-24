#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;

function file_count()
{
        val=1

        if [ "$1" == "0" ]
        then
                if [ "$2" == "0" ]
                then
                        val=0
                fi
        fi
        echo $val
}

BRICK_COUNT=2

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1
TEST $CLI volume start $V0

sleep 5;
## Mount nfs, with nocache option
TEST mount -o vers=3,nolock,noac -t nfs $H0:/$V0 $M0;

touch $M0/files{1..1000};

# Kill a brick process
kill -9 `cat /var/lib/glusterd/vols/$V0/run/$H0-d-backends-${V0}0.pid`;

echo 3 >/proc/sys/vm/drop_caches;

ls -l $M0 >/dev/null;

NEW_FILE_COUNT=`echo $?`;

TEST $CLI volume start $V0 force

# Kill a brick process
kill -9 `cat /var/lib/glusterd/vols/$V0/run/$H0-d-backends-${V0}1.pid`;

echo 3 >/proc/sys/vm/drop_caches;

ls -l $M0 >/dev/null;

NEW_FILE_COUNT1=`echo $?`;

EXPECT "0" file_count $NEW_FILE_COUNT $NEW_FILE_COUNT1

TEST umount -l $M0

cleanup
