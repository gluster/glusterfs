#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

pwd_dir=$(dirname $0)
cold_dir=$(mktemp -d -u -p ${PWD})
tmp_dir=$(mktemp -d -u -p ${PWD})
TEST mkdir -p ${cold_dir} ${tmp_dir};

#TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2,3,4,5};

TEST $CLI volume create $V0 $H0:$B0/${V0}

TEST $CLI volume set $V0 tier-storetype filesystem;
TEST $CLI volume set $V0 quick-read off;
TEST $CLI volume set $V0 stat-prefetch off;
TEST $CLI volume set $V0 storage.reserve 5;

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $CLI volume set $V0 tier on;
TEST $CLI volume set $V0 write-behind off;

## Mount FUSE
TEST $GFS -s $H0 --volfile-id $V0 $M1;


# Create source files
dd if=/dev/urandom of=${tmp_dir}/testfile bs=128k count=30;
dd if=/dev/urandom of=${tmp_dir}/testfile1 bs=128k count=10;
dd if=/dev/urandom of=${tmp_dir}/testfile2 bs=128k count=21;
cp ${tmp_dir}/testfile ${tmp_dir}/file4;
cp ${tmp_dir}/testfile1 ${tmp_dir}/file5;
#echo hello >> ${tmp_dir}/file4;

cat ${tmp_dir}/testfile2 >> ${tmp_dir}/file5;
dd if=${tmp_dir}/testfile2 conv=nocreat,notrunc of=${tmp_dir}/file4 seek=9 skip=9 bs=100k count=4

mkdir -p $M1/test{,2};
TEST cp ${tmp_dir}/* $M1/test/;

echo hello > $M1/test2/testfile

sleep 2;

TEST ! bash $pwd_dir/../../../extras/migrate-to-cold.sh $M1 ${cold_dir} $B0/${V0} test/testfile
TEST ! bash $pwd_dir/../../../extras/migrate-to-cold.sh $M1 ${cold_dir} $B0/${V0} test/testfile1

TEST $CLI volume set $V0 tier-cold-mountpoint ${cold_dir};

sleep 2;

# Only after tier module is properly setup (specially with cold mount, this would work)
#
TEST bash $pwd_dir/../../../extras/migrate-to-cold.sh $M1 ${cold_dir} $B0/${V0} test/testfile
TEST bash $pwd_dir/../../../extras/migrate-to-cold.sh $M1 ${cold_dir} $B0/${V0} test/testfile1
TEST bash $pwd_dir/../../../extras/migrate-to-cold.sh $M1 ${cold_dir} $B0/${V0} test2/testfile

#umount $M1;
TEST $CLI volume set $V0 brick-log-level DEBUG;

#TEST $GFS -s $H0 --volfile-id $V0 $M1;
sleep 3;

#echo hello >> $M1/test/testfile;

TEST dd if=${tmp_dir}/testfile2  conv=nocreat,notrunc  of=${M1}/test/testfile seek=9 skip=9 bs=100k count=4
cat $M1/test/testfile2 >> $M1/test/testfile1;

ls -l $B0/$V0*/.glusterfs/tier

src_md5sum1=$(md5sum ${tmp_dir}/file4 | awk '{ print $1 }');
dst_md5sum1=$(md5sum $M1/test/testfile | awk '{ print $1 }');

TEST [ "$src_md5sum1" = "$dst_md5sum1" ]

src_md5sum2=$(md5sum ${tmp_dir}/file5 | awk '{ print $1 }');
dst_md5sum2=$(md5sum $M1/test/testfile1 | awk '{ print $1 }');

TEST [ "$src_md5sum2" = "$dst_md5sum2" ]

sleep 1;

ls -lR $M1/;
getfattr -d -m . $B0/$V0/test/*;
ls -l $B0/$V0*/.glusterfs/tier

TEST rm -rf $M1/test2;

sleep 1;

ls -lR ${cold_dir}/;
ls -l $B0/$V0*/.glusterfs/tier

pkill -USR2 gluster;

TEST rm -rf ${cold_dir};
TEST rm -rf ${tmp_dir};
cleanup;
