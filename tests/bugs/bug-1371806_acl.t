#!/bin/bash
. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;
TEST useradd tmpuser

function set_facl_user {
        for i in `seq 1 10`
        do
                setfacl -m u:tmpuser:rw ./tmp${i}
                if [ "$?" = "0" ]
                 then
                    succ=$((succ+1))
                else
                    fail=$((fail+1))
                fi
        done
}

function set_facl_default {
        for i in `seq 1 10`
        do
                setfacl -m d:o:rw ./tmp${i}
                if [ "$?" = "0" ]
                 then
                    succ1=$((succ1+1))
                else
                    fail1=$((fail1+1))
                fi
        done
}




TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}{0,1,2,3,4,5}
TEST $CLI volume set $V0 diagnostics.client-log-level DEBUG
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=$V0 --acl --volfile-server=$H0 --entry-timeout=0 $M0;

cd $M0
TEST mkdir tmp{1..10}
TEST setfacl -m u:tmpuser:rwx ./tmp{1..10}
count=`getfacl -p $M0/tmp{1..10} | grep -c "user:tmpuser:rwx"`
EXPECT "10" echo $count
TEST setfacl -m d:o:rwx ./tmp{1..10}
count=`getfacl -p $M0/tmp{1..10} | grep -c "default:other::rwx"`
EXPECT "10" echo $count
count=`getfacl -p $B0/${V0}5/tmp{1..10} | grep -c "user:tmpuser:rwx"`
EXPECT "10" echo $count
count=`getfacl -p $B0/${V0}5/tmp{1..10} | grep -c "default:other::rwx"`
EXPECT "10" echo $count


TEST kill_brick $V0 $H0 $B0/${V0}5
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} "5" online_brick_count

succ=fail=0
## Update acl attributes on dir after kill one brick
set_facl_user
succ1=fail1=0
set_facl_default

TEST $CLI volume start $V0 force
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} "6" online_brick_count

cd -
TEST umount $M0
TEST glusterfs --volfile-id=$V0 --acl --volfile-server=$H0 --entry-timeout=0 $M0;

cd $M0
## At this point dht will heal xatts on down brick only for those hashed_subvol
## was up at the time of updated xattrs
TEST stat ./tmp{1..10}

## Compare succ value with updated acl attributes
count=`getfacl -p $B0/${V0}5/tmp{1..10} | grep -c "user:tmpuser:rw-"`
EXPECT "$succ" echo $count


count=`getfacl -p $B0/${V0}5/tmp{1..10} | grep -c "default:other::rw-"`
EXPECT "$succ1" echo $count

cd -
userdel --force tmpuser
cleanup
