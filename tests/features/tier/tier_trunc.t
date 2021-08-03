#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

cold_dir=$(mktemp -d -u -p ${PWD})
mkdir -p ${cold_dir}

#TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2,3,4,5};

TEST $CLI volume create $V0 $H0:$B0/${V0}

TEST $CLI volume set $V0 tier on;
TEST $CLI volume set $V0 tier-storetype filesystem;
TEST $CLI volume set $V0 tier-cold-mountpoint ${cold_dir};
TEST $CLI volume set $V0 quick-read off;
TEST $CLI volume set $V0 stat-prefetch off;

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Mount FUSE
TEST $GFS -s $H0 --volfile-id $V0 $M1;

TEST dd if=/dev/urandom of=$M1/testfile bs=128k count=25
TEST dd if=/dev/urandom of=$M1/testfile1 bs=128k count=25
TEST dd if=/dev/urandom of=$M1/testfile2 bs=128k count=20

TEST umount $M1;

TEST $GFS -s $H0 --volfile-id $V0 $M1;

TEST md5sum $M1/testfile;
TEST md5sum $M1/testfile1;

mtime=`stat -c "%.Y" $M1/testfile`
mtime1=`stat -c "%.Y" $M1/testfile1`
mtime2=`stat -c "%.Y" $M1/testfile2`

sleep 1;

TEST cp $M1/testfile* ${cold_dir}/;

#post processing of upload
TEST setfattr -n tier.mark-file-as-remote -v $mtime $M1/testfile;

TEST setfattr -n tier.mark-file-as-remote -v $mtime1 $M1/testfile1;
TEST setfattr -n tier.mark-file-as-remote -v $mtime2 $M1/testfile2;

sleep 1;

TEST ln $M1/testfile $M1/linked-file
TEST stat $M1/testfile

# Delete - Make sure the file is deleted in all places

TEST rm $M1/testfile;

TEST ! stat $M1/testfile;
TEST ! stat $B0/$V0/testfile;
TEST stat ${cold_dir}/testfile;

TEST rm $M1/linked-file

TEST ! stat $M1/linked-file;
TEST ! stat ${cold_dir}/testfile;


# Truncate (make sure file is not present in cold tier,
# and the mount point shows 0 byte file md5sum)
TEST stat ${cold_dir}/testfile1;

TEST truncate -s 0 $M1/testfile1;

TEST ! stat ${cold_dir}/testfile1;

hsh=$(md5sum $M1/testfile1 | awk '{ print $1 }');

TEST [ "d41d8cd98f00b204e9800998ecf8427e" == "$hsh" ];


# Truncate with size
TEST stat ${cold_dir}/testfile2;

TEST truncate -s 130k $M1/testfile2;

stat $M1/testfile2;
stat $B0/$V0/testfile2;
stat ${cold_dir}/testfile2;

# So, we need this to be 'deleted' in plugin. It needs to be properly handled
#TEST ! stat ${cold_dir}/testfile2;

TEST rm -rf ${cold_dir};

cleanup;
