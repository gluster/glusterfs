#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../dht.rc

function confirm_all_linkto_files ()
{
   inpath=$1
   for infile in $inpath/*
   do
      echo $infile
      ret1=$(is_dht_linkfile $infile)
      if [ "$ret1" -eq 0 ]; then
        echo "$infile is not a linkto file"
        echo 0
        return
      fi
   done
   echo  1
}

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

#Create a distributed volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1..2};
TEST $CLI volume start $V0

# Mount FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0

#Create files and rename them in order to create linkto files
TEST mkdir -p $M0/dir0/dir1
TEST touch $M0/dir0/dir1/file-{1..50}

for i in {1..50}; do
    mv $M0/dir0/dir1/file-$i $M0/dir0/dir1/nfile-$i;
done

#Remove the second brick to force the creation of linkto files
#on the removed brick

TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}2 start
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" remove_brick_status_completed_field "$V0 $H0:$B0/${V0}2"
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}2 stop

EXPECT "1" confirm_all_linkto_files $B0/${V0}2/dir0/dir1

#Modify the xattrs of the linkto files on the removed brick to point to itself.

target=$(cat $M0/.meta/graphs/active/$V0-dht/subvolumes/1/name)

setfattr -n trusted.glusterfs.dht.linkto -v "$target\0" $B0/${V0}2/dir0/dir1/nfile*


TEST rm -rf $M0/dir0

cleanup;
