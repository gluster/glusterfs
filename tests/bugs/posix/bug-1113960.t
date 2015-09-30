#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

NUM_DIRS=50
NUM_FILES=10

create_dirs () {
        for (( i=1; i<=$NUM_DIRS; i+=1));do
                mkdir $1/olddir$i
                for (( j=0; j<$NUM_FILES; j+=1));do
                        echo "This is file $j in dir $i" > $1/olddir$i/file$j;
                done
        done
        echo "0" > $M0/status
}

move_dirs () {
        old_path="$1"
        new_path="$1"

        #Create a deep directory
        for (( i=$NUM_DIRS; i>=2; i-=1));do
            mv $1/olddir$i $1/olddir`expr $i - 1` > /dev/null 2>&1;
        done

        #Start renaming files and dirs so the paths change for the
        #posix_handle_path calculations

        for (( i=1; i<=$NUM_DIRS; i+=1));do
                old_path="$new_path/olddir$i"
                new_path="$new_path/longernamedir$i"
                mv $old_path $new_path;

                for (( j=0; j<$NUM_FILES; j+=1));do
                       mv $new_path/file$j $new_path/newfile$j > /dev/null 2>&1;
                done
        done
        echo "done" > $M0/status
}

ls_loop ()  {
        #Loop until the move_dirs function is done
        for (( i=0; i<=500; i+=1 )); do
                ls -lR $1 > /dev/null 2>&1
        done
}


cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2,3,4};

EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT '4' brick_count $V0

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Mount FUSE with caching disabled (read-write)
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0;

TEST create_dirs $M0

## Mount FUSE with caching disabled (read-write) again
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M1;

TEST glusterfs -s $H0 --volfile-id $V0 $M2;

(ls_loop $M1)&
ls_pid1=$!

(ls_loop $M2)&
ls_pid2=$!


#Start moving/renaming the directories so the paths change
TEST move_dirs $M0

EXPECT_WITHIN 180 "done" cat $M0/status

#Kill the ls processes

kill -SIGTERM $ls_pid1
kill -SIGTERM $ls_pid2


#Check if all bricks are still up
EXPECT '4' online_brick_count $V0

cleanup;
