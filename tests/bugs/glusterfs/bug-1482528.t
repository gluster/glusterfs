#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup
#Basic checks
TEST glusterd
TEST pidof glusterd

#Create a distributed volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1..2}
TEST $CLI volume start $V0

# Mount FUSE without selinux:
TEST glusterfs -s $H0 --volfile-id $V0 $@ $M0

TEST touch $M0/default.txt
EXPECT "644" stat -c %a $M0/default.txt

TEST chmod 0444 $M0/default.txt
EXPECT "444" stat -c %a $M0/default.txt

TEST mkdir $M0/default
EXPECT "755" stat -c %a $M0/default

TEST chmod 0444 $M0/default
EXPECT "444" stat -c %a $M0/default

TEST mkfifo $M0/mkfifo
EXPECT "644" stat -c %a $M0/mkfifo

TEST mknod $M0/dmknod b 4 5
EXPECT "644" stat -c %a $M0/dmknod

#Set the create-directory-mask and create-mask options
TEST $CLI volume set $V0 storage.create-directory-mask 0444
TEST $CLI volume set $V0 storage.create-mask 0444

TEST mkdir $M0/create-directory
EXPECT "444" stat -c %a $M0/create-directory

TEST touch $M0/create-mask.txt
EXPECT "444" stat -c %a $M0/create-mask.txt

TEST chmod 0777 $M0/create-mask.txt
EXPECT "444" stat -c %a $M0/create-mask.txt

TEST chmod 0400 $M0/create-mask.txt
EXPECT "400" stat -c %a $M0/create-mask.txt

TEST chmod 0777 $M0/create-directory
EXPECT "444" stat -c %a $M0/create-directory

TEST chmod 0400 $M0/create-directory
EXPECT "400" stat -c %a $M0/create-directory

TEST mkfifo $M0/cfifo
EXPECT "444" stat -c %a $M0/cfifo

TEST chmod 0777 $M0/cfifo
EXPECT "444" stat -c %a $M0/cfifo

TEST mknod $M0/cmknod b 4 5
EXPECT "444" stat -c %a $M0/cmknod

#set force-create-mode and force-directory-mode options
TEST $CLI volume set $V0 storage.force-create-mode 0777
TEST $CLI volume set $V0 storage.force-directory-mode 0333

TEST touch $M0/force-create-mode.txt
EXPECT "777" stat -c %a $M0/force-create-mode.txt

TEST mkdir $M0/force-directory
EXPECT "777" stat -c %a $M0/force-directory

TEST chmod 0222 $M0/force-create-mode.txt
EXPECT "777" stat -c %a $M0/force-create-mode.txt

TEST chmod 0222 $M0/force-directory
EXPECT "333" stat -c %a $M0/force-directory

TEST mkdir $M0/link
TEST ln -s $M0/force-create-mode.txt $M0/link
EXPECT "777" stat -c %a $M0/link/force-create-mode.txt

TEST ln $M0/force-create-mode.txt $M0/link/fc.txt
EXPECT "777" stat -c %a $M0/link/fc.txt

TEST setfacl -m o:r $M0/force-create-mode.txt
EXPECT "777" stat -c %a $M0/force-create-mode.txt

TEST ln -s $M0/force-directory $M0/link
EXPECT "777" stat -c %a $M0/link/force-directory

TEST mkfifo $M0/ffifo
EXPECT "777" stat -c %a $M0/ffifo

TEST mknod $M0/mknod b 4 5
EXPECT "777" stat -c %a $M0/mknod
