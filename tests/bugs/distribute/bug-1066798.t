#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TESTS_EXPECTED_IN_LOOP=200

## Start glusterd
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

## Lets create volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};

## Verify volume is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';
TEST glusterfs -s $H0 --volfile-id=$V0 $M0

############################################################
#TEST_PLAN#
#Create a file
#Store the hashed brick information
#Create hard links to it
#Remove the hashed brick
#Check now all the hardlinks are migrated in to "OTHERBRICK"
#Check also in mount point for all the files
#check there is no failures and skips for migration
############################################################

TEST touch $M0/file1;

file_perm=`ls -l $M0/file1 | grep file1 | awk '{print $1}'`;

if [ -f $B0/${V0}1/file1 ]
then
        HASHED=$B0/${V0}1
        OTHER=$B0/${V0}2
else
        HASHED=$B0/${V0}2
        OTHER=$B0/${V0}1
fi

#create hundred hard links
for i in {1..50};
do
TEST_IN_LOOP ln $M0/file1 $M0/link$i;
done


TEST $CLI volume remove-brick $V0 $H0:${HASHED} start
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" remove_brick_status_completed_field "$V0" "$H0:${HASHED}";

#check consistency in mount point
#And also check all the links are migrated to OTHER
for i in {1..50}
do
TEST_IN_LOOP [ -f ${OTHER}/link${i} ];
TEST_IN_LOOP [ -f ${M0}/link${i} ];
done;

#check in OTHER that all the files has proper permission (Means no
#linkto files)

for i in {1..50}
do
link_perm=`ls -l $OTHER | grep -w link${i} | awk '{print $1}'`;
TEST_IN_LOOP [ "${file_perm}" == "${link_perm}" ]

done

#check that remove-brick status should not have any failed or skipped files

var=`$CLI volume remove-brick $V0 $H0:${HASHED} status | grep completed`

TEST [ `echo $var | awk '{print $5}'` = "0"  ]
TEST [ `echo $var | awk '{print $6}'` = "0"  ]

cleanup
#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=000000
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=000000
