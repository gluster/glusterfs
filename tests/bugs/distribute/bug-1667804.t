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

EXPECT "0" echo $(ls $B0/${V0}2/dir0/dir1 | wc -l)
TEST rm -rf $M0/dir0


TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

umount $M0

TEST $CLI volume create $V0 $H0:$B0/${V0}{1..3};
TEST $CLI volume start $V0

# Mount FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0
cd $M0
TEST mkdir dir1
cd dir1
for i in {1..10}; do echo "Test file" > file-$i; mv file-$i newfile-$i; mv newfile-$i green-$i; done
cd

# Expect 2 linkfiles on 2nd node
EXPECT "2" echo $(stat -c %a $B0/${V0}2/dir1/* | grep 1000 | wc -l)

TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}3 start
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" remove_brick_status_completed_field "$V0 $H0:$B0/${V0}3"
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}3 stop

# Expect 0 link file on remove brick node
EXPECT "0" echo $(ls $B0/${V0}3/dir1 | wc -l)

# Expect 9 link file on 2nd node
EXPECT "0" echo $(stat -c %a $B0/${V0}2/dir1/* | grep 1000 | wc -l)

TEST rm -rf $M0/dir1

cleanup;
