#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;

## Layout-spread set to 3, but subvols up are 2. So layout should split 50-50
function get_layout()
{
        layout1=`getfattr -n trusted.glusterfs.dht -e hex $1 2>&1|grep dht |cut -d = -f2`
        layout2=`getfattr -n trusted.glusterfs.dht -e hex $2 2>&1|grep dht |cut -d = -f2`

        if [ $layout1 == "0x0000000100000000000000007ffffffe" ]
        then
                if [ $layout2 == "0x00000001000000007fffffffffffffff" ]
		then
			return 0
		else
			return 1
		fi
        fi

	if [ $layout2 == "0x0000000100000000000000007ffffffe" ]
        then
                if [ $layout1 == "0x00000001000000007fffffffffffffff" ]
		then
			return 0
		else
			return 1
		fi
        fi
	return 1
}

BRICK_COUNT=4

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1 $H0:$B0/${V0}2 $H0:$B0/${V0}3
## set subvols-per-dir option
TEST $CLI volume set $V0 subvols-per-directory 3
TEST $CLI volume start $V0

## Mount FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0 --entry-timeout=0 --attribute-timeout=0;

TEST ls -l $M0

## kill 2 bricks to bring down available subvol < spread count
kill -9 `cat /var/lib/glusterd/vols/$V0/run/$H0-d-backends-${V0}2.pid`;
kill -9 `cat /var/lib/glusterd/vols/$V0/run/$H0-d-backends-${V0}3.pid`;

mkdir $M0/dir1 2>/dev/null

get_layout $B0/${V0}0/dir1 $B0/${V0}1/dir1
EXPECT "0" echo $?

cleanup;
