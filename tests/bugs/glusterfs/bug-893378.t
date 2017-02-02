#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;
BRICK_COUNT=3

function file_has_linkfile()
{
        i=0
        j=0
        while [ $i -lt $BRICK_COUNT ]
        do
                stat=`stat $B0/${V0}$i/$1 2>/dev/null`
                if [ $? -eq 0 ]
                then
                        let j++
                        let "BRICK${j}=$i"

                fi
                let i++
        done
        return $j
}

function get_cached_brick()
{
        i=1
        while [ $i -lt 3 ]
        do
                test=`getfattr -n trusted.glusterfs.dht.linkto -e text $B0/${V0}$BRICK$i 2>&1`
                if [ $? -eq 1 ]
                then
                        cached=$BRICK"$i"
                        i=$(( $i+3 ))
                fi
                let i++
        done

        return $cached
}

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1 $H0:$B0/${V0}2
TEST $CLI volume start $V0

## Mount FUSE
TEST glusterfs --attribute-timeout=0 --entry-timeout=0 -s $H0 --volfile-id $V0 $M0;

## create a linkfile on subvolume 0
TEST touch $M0/1
TEST mv $M0/1 $M0/2

file_has_linkfile 2
has_link=$?
if [ $has_link -eq 2 ]
then
        get_cached_brick
        CACHED=$?
        # Kill a brick process
	kill_brick $V0 $H0 $B0/${V0}$CACHED
fi

## trigger a lookup
ls -l $M0/2 2>/dev/null

## fail dd if file exists.

dd if=/dev/zero of=$M0/2 bs=1 count=1 conv=excl 2>/dev/null
EXPECT "1" echo $?

cleanup;
