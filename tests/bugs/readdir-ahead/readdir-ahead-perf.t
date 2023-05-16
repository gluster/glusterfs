#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../dht.rc

cleanup;

count_files () {
        ls $1 | wc -l
}

TEST glusterd

TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{1,2,3,4,5,6,7,8,9};

#Disable readdirp completely
TEST $CLI volume set $V0 dht.force-readdirp no
TEST $CLI volume set $V0 performance.force-readdirp no

#Start the volume
TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status';
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} "9" online_brick_count

#Mount the volume
TEST glusterfs --use-readdirp=no --volfile-id=$V0 --volfile-server=$H0 $M0;

#Create a newdir
TEST mkdir $M0/newdir

#Create 1600 2k files
for i in {1..1600}; do dd if=/dev/zero of=$M0/newdir/file$i bs=64 count=32; done 

#Run readdir for newdir
TEST [ $(count_files $M0/newdir) = "1600" ]

statedump_file=$(generate_mount_statedump $V0);
totcnt=$(grep -i latency $statedump_file | grep -iw readdir | grep dht | awk -F " " '{print $2}' | awk -F ":" '{print $2}')
TEST rm -rf $staedump_file

#Now enable performance-readdir-ahead
TEST $CLI volume set $V0 performance.readdir-ahead on

TEST umount $M0
#Again mount the volume
TEST glusterfs --use-readdirp=no --volfile-id=$V0 --volfile-server=$H0 $M0;

#Again readdir for newdir
TEST [ $(count_files $M0/newdir) = "1600" ]

#Because force-readdir is not disabled for readdir-ahead so readdir-ahead wind
#a readdirp call instead of readdir
statedump_file=$(generate_mount_statedump $V0);
newcnt=$(grep -i latency $statedump_file | grep -iw readdirp | grep dht | awk -F " " '{print $2}' | awk -F ":" '{print $2}')
TEST rm -rf $statedump_file

TEST umount $M0
#Again mount the volume
TEST glusterfs --use-readdirp=no --volfile-id=$V0 --volfile-server=$H0 $M0;

#Now disable readdirp for readdir-ahead sothat readdir-ahead wind a readdir call
TEST $CLI volume set $V0 readdir-ahead.force-readdirp no

#Again readdir for newdir
TEST [ $(count_files $M0/newdir) = "1600" ]

statedump_file=$(generate_mount_statedump $V0);
newcnt_readdir=$(grep -i latency $statedump_file | grep -iw readdir | grep dht | awk -F " " '{print $2}' | awk -F ":" '{print $2}')
TEST rm -rf $statedump_file

TEST [ $((newcnt * 30)) -le $totcnt ]
TEST [ $((newcnt_readdir * 30)) -le $totcnt ]

cleanup



