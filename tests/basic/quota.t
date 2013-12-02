#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TESTS_EXPECTED_IN_LOOP=19

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2  $H0:$B0/${V0}{1,2,3,4};

function hard_limit()
{
        local QUOTA_PATH=$1;
        $CLI volume quota $V0 list $QUOTA_PATH | grep "$QUOTA_PATH" | awk '{print $2}'
}

function soft_limit()
{
        local QUOTA_PATH=$1;
        $CLI volume quota $V0 list $QUOTA_PATH | grep "$QUOTA_PATH" | awk '{print $3}'
}

function usage()
{
        local QUOTA_PATH=$1;
        $CLI volume quota $V0 list $QUOTA_PATH | grep "$QUOTA_PATH" | awk '{print $4}'
}

EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT '4' brick_count $V0

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST glusterfs -s $H0 --volfile-id $V0 $M0;

TEST mkdir -p $M0/test_dir/in_test_dir

## ------------------------------
## Verify quota commands
## ------------------------------
TEST $CLI volume quota $V0 enable

TEST $CLI volume quota $V0 limit-usage /test_dir 100MB

TEST $CLI volume quota $V0 limit-usage /test_dir/in_test_dir 150MB

EXPECT "150.0MB" hard_limit "/test_dir/in_test_dir";
EXPECT "80%" soft_limit "/test_dir/in_test_dir";

TEST $CLI volume quota $V0 remove /test_dir/in_test_dir

EXPECT "100.0MB" hard_limit "/test_dir";

TEST $CLI volume quota $V0 limit-usage /test_dir 10MB
EXPECT "10.0MB" hard_limit "/test_dir";
EXPECT "80%" soft_limit "/test_dir";

TEST $CLI volume quota $V0 soft-timeout 0
TEST $CLI volume quota $V0 hard-timeout 0

## ------------------------------
## Verify quota enforcement
## -----------------------------

TEST ! dd if=/dev/urandom of=$M0/test_dir/1.txt bs=1M count=12
TEST rm $M0/test_dir/1.txt

# wait for marker's accounting to complete
EXPECT_WITHIN 10 "0Bytes" usage "/test_dir"

TEST dd if=/dev/urandom of=$M0/test_dir/2.txt bs=1M count=8
EXPECT_WITHIN 20 "8.0MB" usage "/test_dir"
TEST rm $M0/test_dir/2.txt
EXPECT_WITHIN 10 "0Bytes" usage "/test_dir"

## rename tests
TEST dd if=/dev/urandom of=$M0/test_dir/2 bs=1M count=8
EXPECT_WITHIN 20 "8.0MB" usage "/test_dir"
TEST mv $M0/test_dir/2 $M0/test_dir/0
EXPECT_WITHIN 10 "8.0MB" usage "/test_dir"
TEST rm $M0/test_dir/0
EXPECT_WITHIN 10 "0Bytes" usage "/test_dir"

## ---------------------------

## ------------------------------
## Check if presence of nfs mount results in ESTALE errors for I/O
#  on a fuse mount. Note: Quota command internally uses a fuse mount,
#  though this may change.
## -----------------------------

TEST mount -t nfs -o nolock,soft,intr $H0:/$V0 $N0;
TEST $CLI volume quota $V0 limit-usage /test_dir 100MB

TEST $CLI volume quota $V0 limit-usage /test_dir/in_test_dir 150MB

EXPECT "150.0MB" hard_limit "/test_dir/in_test_dir";
## -----------------------------


###################################################
## ------------------------------------------------
## <Test quota functionality in add-brick senarios>
## ------------------------------------------------
###################################################
QUOTALIMIT=1024
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
        TEST_IN_LOOP dd if=/dev/urandom of="$M0/$TESTDIR/dir1/100MBfile$i" \
                        bs=1M count=100;
done

# 63-64
## <Add brick and start rebalance>
## -------------------------------
TEST $CLI volume add-brick $V0 $H0:$B0/brick{3,4}
TEST $CLI volume rebalance $V0 start;


## <Try creating data beyond limit>
## --------------------------------
for i in `seq 1 200`; do
        dd if=/dev/urandom of="$M0/$TESTDIR/dir1/10MBfile$i" bs=1M count=10 \
           &>/dev/null
done

# 65
## <Test whether quota limit crossed more than 10% of limit>
## ---------------------------------------------------------
USED_KB=`du -s $M0/$TESTDIR/dir1 | cut -f1`;
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
## </Test quota functionality in add-brick senarios>
## -------------------------------------------------

TEST $CLI volume quota $V0 disable
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

umount -l $N0

cleanup;
