#!/bin/bash
#

. $(dirname $0)/../include.rc
. $(dirname $0)/../nfs.rc
. $(dirname $0)/../volume.rc

BRICK_SAMPLES="$(gluster --print-logdir)/samples/glusterfsd__d_backends_${V0}0.samp"
NFS_SAMPLES="$(gluster --print-logdir)/samples/glusterfs_nfsd_${V0}.samp"
FUSE_SAMPLES="/var/log/glusterfs/samples/glusterfs_${V0}.samp"

function check_path {
        op=$1
        path=$2
        file=$3
        grep $op $file | awk -F, '{print $11}' | grep $path 2>&1 > /dev/null
        if [ $? -eq 0 ]; then
          echo "Y"
        else
          echo "N"
        fi
}

function print_cnt() {
  local FOP_TYPE=$1
  local SAMP_FILE=$2
  local FOP_CNT=$(grep ,${FOP_TYPE} ${SAMP_FILE} | wc -l)
  echo $FOP_CNT
}

# Verify we got non-zero counts for stats/lookup/readdir
check_samples() {
        STAT_CNT=$(print_cnt STAT $BRICK_SAMPLES)
        if [ "$STAT_CNT" -le "0" ]; then
                echo "STAT count is zero"
                return
        fi

        LOOKUP_CNT=$(print_cnt LOOKUP $BRICK_SAMPLES)
        if [ "$LOOKUP_CNT" -le "0" ]; then
                echo "LOOKUP count is zero"
                return
        fi

        READDIR_CNT=$(print_cnt READDIR $BRICK_SAMPLES)
        if [ "$READDIR_CNT" -le "0" ]; then
                echo "READDIR count is zero"
                return
        fi

        echo "OK"
}

cleanup;
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 nfs.disable off
TEST $CLI volume set $V0 diagnostics.latency-measurement on
TEST $CLI volume set $V0 diagnostics.count-fop-hits on
TEST $CLI volume set $V0 diagnostics.stats-dump-interval 5
TEST $CLI volume set $V0 diagnostics.fop-sample-buf-size 65535
TEST $CLI volume set $V0 diagnostics.fop-sample-interval 1
TEST $CLI volume set $V0 diagnostics.stats-dnscache-ttl-sec 3600
TEST $CLI volume start $V0

>${NFS_SAMPLES}
>${BRICK_SAMPLES}
>${FUSE_SAMPLES}

#################
# Basic Samples #
#################

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0

for i in {1..5}
do
        dd if=/dev/zero of=${M0}/testfile$i bs=4k count=1
        rm ${M0}/testfile$i
done

TEST ls -l $M0
EXPECT_WITHIN 6 "OK" check_samples
sleep 2

################################
# Paths in the samples #
################################

TEST mount_nfs $H0:$V0 $N0

>$FUSE_SAMPLES

for dir in "$N0" "$M0"; do
  ls $dir &> /dev/null
  touch $dir/file1
  stat $dir/file1 &> /dev/null
  echo "some data" > $dir/file1
  dd if=/dev/zero of=$dir/file2 bs=1M count=10 conv=fsync
  dd if=/dev/zero of=$dir/file1 bs=1M count=1
  cat $dir/file2 &> /dev/null
  mkdir -p $dir/dir1
  rmdir $dir/dir1
  rm $dir/file1
  rm $dir/file2
done;

EXPECT_WITHIN 10 "Y" check_path CREATE /file1 $BRICK_SAMPLES
EXPECT_WITHIN 10 "Y" check_path LOOKUP /file1 $BRICK_SAMPLES
EXPECT_WITHIN 10 "Y" check_path SETATTR /file1 $BRICK_SAMPLES
EXPECT_WITHIN 10 "Y" check_path WRITE /file1 $BRICK_SAMPLES
EXPECT_WITHIN 10 "Y" check_path FINODELK /file1 $BRICK_SAMPLES
EXPECT_WITHIN 10 "Y" check_path ENTRYLK / $BRICK_SAMPLES
EXPECT_WITHIN 10 "Y" check_path FLUSH /file2 $BRICK_SAMPLES
EXPECT_WITHIN 10 "Y" check_path TRUNCATE /file1 $BRICK_SAMPLES
EXPECT_WITHIN 10 "Y" check_path MKDIR /dir1 $BRICK_SAMPLES
EXPECT_WITHIN 10 "Y" check_path RMDIR /dir1 $BRICK_SAMPLES
EXPECT_WITHIN 10 "Y" check_path UNLINK /file1 $BRICK_SAMPLES
EXPECT_WITHIN 10 "Y" check_path UNLINK /file2 $BRICK_SAMPLES


EXPECT_WITHIN 10 "Y" check_path CREATE /file1 $NFS_SAMPLES
EXPECT_WITHIN 10 "Y" check_path LOOKUP /file1 $NFS_SAMPLES
EXPECT_WITHIN 10 "Y" check_path ACCESS /file1 $NFS_SAMPLES
EXPECT_WITHIN 10 "Y" check_path SETATTR /file1 $NFS_SAMPLES
EXPECT_WITHIN 10 "Y" check_path WRITE /file1 $NFS_SAMPLES
EXPECT_WITHIN 10 "Y" check_path FLUSH /file2 $NFS_SAMPLES
EXPECT_WITHIN 10 "Y" check_path ACCESS /file2 $NFS_SAMPLES
EXPECT_WITHIN 10 "Y" check_path READ /file2 $NFS_SAMPLES
EXPECT_WITHIN 10 "Y" check_path TRUNCATE /file1 $NFS_SAMPLES
EXPECT_WITHIN 10 "Y" check_path MKDIR /dir1 $NFS_SAMPLES
EXPECT_WITHIN 10 "Y" check_path RMDIR /dir1 $NFS_SAMPLES
EXPECT_WITHIN 10 "Y" check_path UNLINK /file1 $NFS_SAMPLES
EXPECT_WITHIN 10 "Y" check_path UNLINK /file2 $NFS_SAMPLES


EXPECT_WITHIN 10 "Y" check_path CREATE /file1 $FUSE_SAMPLES
EXPECT_WITHIN 10 "Y" check_path LOOKUP /file1 $FUSE_SAMPLES
EXPECT_WITHIN 10 "Y" check_path OPEN /file1 $FUSE_SAMPLES
EXPECT_WITHIN 10 "Y" check_path SETATTR /file1 $FUSE_SAMPLES
EXPECT_WITHIN 10 "Y" check_path WRITE /file1 $FUSE_SAMPLES
EXPECT_WITHIN 10 "Y" check_path FLUSH /file2 $FUSE_SAMPLES
EXPECT_WITHIN 10 "Y" check_path FSYNC /file2 $FUSE_SAMPLES
EXPECT_WITHIN 10 "Y" check_path OPEN /file2 $FUSE_SAMPLES
EXPECT_WITHIN 10 "Y" check_path READ /file2 $FUSE_SAMPLES
EXPECT_WITHIN 10 "Y" check_path TRUNCATE /file1 $FUSE_SAMPLES
EXPECT_WITHIN 10 "Y" check_path MKDIR /dir1 $FUSE_SAMPLES
EXPECT_WITHIN 10 "Y" check_path RMDIR /dir1 $FUSE_SAMPLES
EXPECT_WITHIN 10 "Y" check_path UNLINK /file1 $FUSE_SAMPLES
EXPECT_WITHIN 10 "Y" check_path UNLINK /file2 $FUSE_SAMPLES

######################
# Errors in samples  #
#####################

# With a very low sample rate, we should still audit creates & unlinks 1:1
TEST $CLI volume set $V0 diagnostics.fop-sample-interval 1000
TEST $CLI volume set $V0 diagnostics.fop-sample-enable-audit on

>${NFS_SAMPLES}
>${BRICK_SAMPLES}
>${FUSE_SAMPLES}

mkdir -pv $M0/1/2/3/4
touch $M0/1/2/3/4/{a,b,c}
dd if=/dev/zero of=$M0/1/2/3/4/d bs=1k count=10
dd if=/dev/zero of=$M0/1/2/3/4/d bs=1M count=10
rm -rfv $M0/*
sleep 6

TEST grep "MKDIR.*/1/2/3/4" $FUSE_SAMPLES
TEST grep "CREATE.*/1/2/3/4" $FUSE_SAMPLES
TEST grep "RMDIR.*/1/2" $FUSE_SAMPLES
TEST grep "UNLINK.*/1/2/3/4/a" $FUSE_SAMPLES
TEST grep "TRUNCATE.*/1/2/3/4/d" $FUSE_SAMPLES

TEST [ $(print_cnt MKDIR $FUSE_SAMPLES) -eq "4" ]
TEST [ $(print_cnt CREATE $FUSE_SAMPLES) -eq "4" ]
TEST [ $(print_cnt RMDIR $FUSE_SAMPLES) -eq "4" ]
TEST [ $(print_cnt UNLINK $FUSE_SAMPLES) -eq "4" ]
TEST [ $(print_cnt TRUNCATE $FUSE_SAMPLES) -eq "1" ]

cleanup;
