#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

is_sticky_set () {
        echo $1
        if [ -k $1 ];
        then
                echo "yes"
        else
                echo "no"
        fi
}

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

#Create a distributed volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1..3};
TEST $CLI volume start $V0

# Mount FUSE
TEST glusterfs --acl -s $H0 --volfile-id $V0 $M0

TEST mkdir $M0/dir1

echo "Testing pacls on rebalance" > $M0/dir1/FILE1
FPATH1=`find $B0/ -name FILE1`

# Rename the file to create a linkto, for rebalance to
# act on the file

TEST mv $M0/dir1/FILE1 $M0/dir1/FILE2
FPATH2=`find $B0/ -perm 1000 -name FILE2`

setfacl -m user:root:rwx $M0/dir1/FILE2

# Run rebalance without the force option to skip
# the file migration
TEST $CLI volume rebalance $V0 start

EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" rebalance_status_field $V0

#Check that the file has been skipped,i.e., the linkto still exists
EXPECT "yes" is_sticky_set $FPATH2


#The linkto file should not have any posix acls set
COUNT=`getfacl $FPATH2 |grep -c "user:root:rwx"`
EXPECT "0" echo $COUNT

cleanup;
