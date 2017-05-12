#!/bin/bash
. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../dht.rc
cleanup;

function get_getfattr {
        local path=$1
        echo `getfattr -n user.foo $path` | cut -f2 -d"=" | sed -e 's/^"//'  -e 's/"$//'
}

function set_fattr {
        for i in `seq 1 10`
        do
                setfattr -n user.foo -v "newabc" ./tmp${i}
                if [ "$?" = "0" ]
                 then
                    succ=$((succ+1))
                else
                    fail=$((fail+1))
                fi
        done
}



TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}{0,1,2,3}
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 --entry-timeout=0 --attribute-timeout=0 $M0;

cd $M0
TEST mkdir tmp{1..10}

TEST kill_brick $V0 $H0 $B0/${V0}3
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} "3" online_brick_count

succ=fail=0
## set user.foo xattr with value newabc after kill one brick
set_fattr
TEST $CLI volume start $V0 force
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} "4" online_brick_count

cd -
TEST umount $M0
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 --entry-timeout=0 --attribute-timeout=0 $M0;

cd $M0
## At this point dht code will heal xattr on down brick only for those dirs
## hashed subvol was up at the time of update xattr
TEST stat ./tmp{1..10}


## Count the user.foo xattr value with newabc on brick and compare with succ value
count=`getfattr -n user.foo $B0/${V0}3/tmp{1..10} | grep "user.foo" | grep -iw "newabc" | wc -l`
EXPECT "$succ" echo $count


cd -
cleanup
exit
