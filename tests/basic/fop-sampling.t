#!/bin/bash
#

. $(dirname $0)/../include.rc
. $(dirname $0)/../nfs.rc
. $(dirname $0)/../volume.rc

BRICK_SAMPLES="$(gluster --print-logdir)/samples/glusterfsd__d_backends_${V0}0.samp"
NFS_SAMPLES="$(gluster --print-logdir)/samples/glusterfs_nfsd.samp"

function check_path {
        op=$1
        path=$2
        file=$3
        grep $op $file | awk -F, '{print $NF}' | grep $path 2>&1 > /dev/null
        if [ $? -eq 0 ]; then
          echo "Y"
        else
          echo "N"
        fi
}

function print_cnt() {
  local FOP_TYPE=$1
  local FOP_CNT=$(grep ,${FOP_TYPE} ${BRICK_SAMPLES} | wc -l)
  echo $FOP_CNT
}

# Verify we got non-zero counts for stats/lookup/readdir
check_samples() {
        STAT_CNT=$(print_cnt STAT)
        if [ "$STAT_CNT" -le "0" ]; then
                echo "STAT count is zero"
                return
        fi

        LOOKUP_CNT=$(print_cnt LOOKUP)
        if [ "$LOOKUP_CNT" -le "0" ]; then
                echo "LOOKUP count is zero"
                return
        fi

        READDIR_CNT=$(print_cnt READDIR)
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

#################
# Basic Samples #
#################
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0

for i in {1..5}
do
        dd if=/dev/zero of=${M0}/testfile$i bs=4k count=1
done

TEST ls -l $M0
EXPECT_WITHIN 6 "OK" check_samples

sleep 2

################################
# Paths in the samples #
################################

TEST mount_nfs $H0:$V0 $N0

ls $N0 &> /dev/null
touch $N0/file1
stat $N0/file1 &> /dev/null
echo "some data" > $N0/file1
dd if=/dev/zero of=$N0/file2 bs=1M count=10 conv=fsync
dd if=/dev/zero of=$N0/file1 bs=1M count=1
cat $N0/file2 &> /dev/null
mkdir -p $N0/dir1
rmdir $N0/dir1
rm $N0/file1
rm $N0/file2

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

cleanup;
