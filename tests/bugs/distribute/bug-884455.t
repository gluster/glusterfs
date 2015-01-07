#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../dht.rc

cleanup;

function layout_compare()
{
        res=0

        if [ "$1" == "$2" ]
        then
                res=1
        fi
        if [ "$1" == "$3" ]
        then
                res=1
        fi
        if [ "$2" == "$3" ]
        then
                res=1
        fi

        echo $res
}

function get_layout()
{
        layout1=`getfattr -n trusted.glusterfs.dht -e hex $1 2>&1|grep dht |cut -d = -f2`
        layout2=`getfattr -n trusted.glusterfs.dht -e hex $2 2>&1|grep dht |cut -d = -f2`
        layout3=`getfattr -n trusted.glusterfs.dht -e hex $3 2>&1|grep dht |cut -d = -f2`

        ret=$(layout_compare $layout1 $layout2 $layout3)

        if [ $ret -ne 0 ]
        then
                echo 1
        else
                echo 0
        fi

}

BRICK_COUNT=3

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1
## set subvols-per-dir option
TEST $CLI volume set $V0 subvols-per-directory 2
TEST $CLI volume start $V0

## Mount FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0;

TEST mkdir $M0/dir{1..10} 2>/dev/null;

## Add-brick n run rebalance to force re-write of layout
TEST $CLI volume add-brick $V0 $H0:$B0/${V0}2
sleep 5;

## trigger dir self heal on client
TEST ls -l $M0 2>/dev/null;

TEST $CLI volume rebalance $V0 start force

EXPECT_WITHIN $REBALANCE_TIMEOUT "0" rebalance_completed

## check for layout overlaps.
EXPECT "0" get_layout $B0/${V0}0 $B0/${V0}1 $B0/${V0}2
EXPECT "0" get_layout $B0/${V0}0/dir1 $B0/${V0}1/dir1 $B0/${V0}2/dir1
EXPECT "0" get_layout $B0/${V0}0/dir2 $B0/${V0}1/dir2 $B0/${V0}2/dir2
EXPECT "0" get_layout $B0/${V0}0/dir3 $B0/${V0}1/dir3 $B0/${V0}2/dir3
EXPECT "0" get_layout $B0/${V0}0/dir4 $B0/${V0}1/dir4 $B0/${V0}2/dir4
EXPECT "0" get_layout $B0/${V0}0/dir5 $B0/${V0}1/dir5 $B0/${V0}2/dir5
EXPECT "0" get_layout $B0/${V0}0/dir6 $B0/${V0}1/dir6 $B0/${V0}2/dir6
EXPECT "0" get_layout $B0/${V0}0/dir7 $B0/${V0}1/dir7 $B0/${V0}2/dir7
EXPECT "0" get_layout $B0/${V0}0/dir8 $B0/${V0}1/dir8 $B0/${V0}2/dir8
EXPECT "0" get_layout $B0/${V0}0/dir9 $B0/${V0}1/dir9 $B0/${V0}2/dir9
EXPECT "0" get_layout $B0/${V0}0/dir10 $B0/${V0}1/dir10 $B0/${V0}2/dir10

cleanup;
