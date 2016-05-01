#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../nfs.rc
. $(dirname $0)/../dht.rc
. $(dirname $0)/../nfs.rc

cleanup;

QDD=$(dirname $0)/quota
# compile the test write program and run it
build_tester $(dirname $0)/quota.c -o $QDD

TESTS_EXPECTED_IN_LOOP=19

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2  $H0:$B0/${V0}{1,2,3,4};

EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT '4' brick_count $V0

TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $GFS -s $H0 --volfile-id $V0 $M0;

TEST mkdir -p $M0/test_dir/in_test_dir

## --------------------------------------------------------------------------
## Verify quota commands and check if quota-deem-statfs is enabled by default
## --------------------------------------------------------------------------
TEST $CLI volume quota $V0 enable
EXPECT 'on' volinfo_field $V0 'features.quota'
EXPECT 'on' volinfo_field $V0 'features.inode-quota'
EXPECT 'on' volinfo_field $V0 'features.quota-deem-statfs'

#Wait for the auxiliarymount to come up
sleep 3

TEST $CLI volume quota $V0 limit-usage /test_dir 100MB
# Checking for auxiliary mount
EXPECT "0"  get_aux

TEST $CLI volume quota $V0 limit-usage /test_dir/in_test_dir 150MB

EXPECT "150.0MB" quota_hard_limit "/test_dir/in_test_dir";
EXPECT "80%" quota_soft_limit "/test_dir/in_test_dir";

TEST $CLI volume quota $V0 remove /test_dir/in_test_dir

EXPECT "100.0MB" quota_hard_limit "/test_dir";

TEST $CLI volume quota $V0 limit-usage /test_dir 10MB
EXPECT "10.0MB" quota_hard_limit "/test_dir";
EXPECT "80%" quota_soft_limit "/test_dir";

TEST $CLI volume quota $V0 soft-timeout 0
TEST $CLI volume quota $V0 hard-timeout 0

## ------------------------------
## Verify quota enforcement
## -----------------------------

# Try to create a 12MB file which should fail
TEST ! $QDD $M0/test_dir/1.txt 256 48
TEST rm $M0/test_dir/1.txt

# wait for marker's accounting to complete
EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "0Bytes" quotausage "/test_dir"

TEST $QDD $M0/test_dir/2.txt 256 32
EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "8.0MB" quotausage "/test_dir"

# Checking internal xattr
# This confirms that pgfid is also filtered
TEST ! "getfattr -d -e hex -m . $M0/test_dir/2.txt | grep pgfid ";
# just check for quota xattr are visible or not
TEST ! "getfattr -d -e hex -m . $M0/test_dir | grep quota";

# setfattr should fail
TEST ! setfattr -n trusted.glusterfs.quota.limit-set -v 10 $M0/test_dir;

# remove xattr should fail
TEST ! setfattr -x trusted.glusterfs.quota.limit-set $M0/test_dir;

TEST rm $M0/test_dir/2.txt
EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "0Bytes" quotausage "/test_dir"

## rename tests
TEST $QDD $M0/test_dir/2 256 32
EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "8.0MB" quotausage "/test_dir"
TEST mv $M0/test_dir/2 $M0/test_dir/0
EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "8.0MB" quotausage "/test_dir"
TEST rm $M0/test_dir/0
EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "0Bytes" quotausage "/test_dir"

## rename tests under different directories
TEST mkdir -p $M0/1/2;
TEST $CLI volume quota $V0 limit-usage /1/2 100MB 70%;

# The corresponding write(3) should fail with EDQUOT ("Disk quota exceeded")
TEST ! $QDD $M0/1/2/file 256 408

TEST mkdir -p $M0/1/3;
TEST $QDD $M0/1/3/file 256 408

#The corresponding rename(3) should fail with EDQUOT ("Disk quota exceeded")
TEST ! mv $M0/1/3/ $M0/1/2/3_mvd;

## ---------------------------

## ------------------------------
## Check if presence of nfs mount results in ESTALE errors for I/O
#  on a fuse mount. Note: Quota command internally uses a fuse mount,
#  though this may change.
## -----------------------------

##Wait for connection establishment between nfs server and brick process
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;

TEST mount_nfs $H0:/$V0 $N0 nolock;
TEST $CLI volume quota $V0 limit-usage /test_dir 100MB

TEST $CLI volume quota $V0 limit-usage /test_dir/in_test_dir 150MB

EXPECT "150.0MB" quota_hard_limit "/test_dir/in_test_dir";
## -----------------------------


###################################################
## ------------------------------------------------
## <Test quota functionality in add-brick scenarios>
## ------------------------------------------------
###################################################
QUOTALIMIT=100
QUOTALIMITROOT=2048
TESTDIR="addbricktest"

rm -rf $M0/*;

## <Create directories and test>
## -----------------------------
# 41-42
TEST mkdir $M0/$TESTDIR
TEST mkdir $M0/$TESTDIR/dir{1..10};


# 43-52
## <set limits>
## -----------------------------
TEST $CLI volume quota $V0 limit-usage / "$QUOTALIMITROOT"MB;
for i in {1..10}; do
        TEST_IN_LOOP $CLI volume quota $V0 limit-usage /$TESTDIR/dir$i \
                          "$QUOTALIMIT"MB;
done
## </Enable quota and set limits>

#53-62
for i in `seq 1 9`; do
        TEST_IN_LOOP $QDD "$M0/$TESTDIR/dir1/10MBfile$i" 256 40
done

# 63-64
## <Add brick and start rebalance>
## -------------------------------
TEST $CLI volume add-brick $V0 $H0:$B0/brick{3,4}
TEST $CLI volume rebalance $V0 start;

## Wait for rebalance
EXPECT_WITHIN $REBALANCE_TIMEOUT "0" rebalance_completed

## <Try creating data beyond limit>
## --------------------------------
for i in `seq 1 200`; do
        $QDD of="$M0/$TESTDIR/dir1/1MBfile$i" 256 4\
           2>&1 | egrep -v '(No space left|Disc quota exceeded)'
done

# 65
## <Test whether quota limit crossed more than 10% of limit>
## ---------------------------------------------------------
USED_KB=`du -ks $M0/$TESTDIR/dir1 | cut -f1`;
USED_MB=$(($USED_KB/1024));
TEST [ $USED_MB -le $((($QUOTALIMIT * 110) / 100)) ]

# 66-67
## <Test the xattrs healed to new brick>
## -------------------------------------
TEST getfattr -d -m "trusted.glusterfs.quota.limit-set" -e hex \
              --absolute-names $B0/brick{3,4}/$TESTDIR/dir{1..10};
# Test on root.
TEST getfattr -d -m "trusted.glusterfs.quota.limit-set" -e hex \
              --absolute-names $B0/brick{3,4};

## -------------------------------------------------
## </Test quota functionality in add-brick scenarios>
## -------------------------------------------------

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0

## ---------------------------
## Test quota volume options
## ---------------------------
TEST $CLI volume reset $V0
EXPECT 'on' volinfo_field $V0 'features.quota'
EXPECT 'on' volinfo_field $V0 'features.inode-quota'
EXPECT 'on' volinfo_field $V0 'features.quota-deem-statfs'

TEST $CLI volume reset $V0 force
EXPECT 'on' volinfo_field $V0 'features.quota'
EXPECT 'on' volinfo_field $V0 'features.inode-quota'
EXPECT 'on' volinfo_field $V0 'features.quota-deem-statfs'

TEST $CLI volume reset $V0 features.quota-deem-statfs
EXPECT 'on' volinfo_field $V0 'features.quota-deem-statfs'

TEST $CLI volume set $V0 features.quota-deem-statfs off
EXPECT 'off' volinfo_field $V0 'features.quota-deem-statfs'

TEST $CLI volume set $V0 features.quota-deem-statfs on
EXPECT 'on' volinfo_field $V0 'features.quota-deem-statfs'

TEST $CLI volume quota $V0 disable
EXPECT 'off' volinfo_field $V0 'features.quota'
EXPECT 'off' volinfo_field $V0 'features.inode-quota'
EXPECT '' volinfo_field $V0 'features.quota-deem-statfs'

# aux mount should be removed
TEST $CLI volume stop $V0;
EXPECT "1" get_aux

rm -f $QDD
cleanup;
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=1332045
