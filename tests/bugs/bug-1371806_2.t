#!/bin/bash
. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../dht.rc
cleanup;

function get_getfattr {
        local path=$1
        echo `getfattr -n user.foo $path` | cut -f2 -d"=" | sed -e 's/^"//'  -e 's/"$//'
}

function remove_mds_xattr {

       for i in `seq 1 10`
       do
               setfattr -x trusted.glusterfs.dht.mds $1/tmp${i} 2> /dev/null
       done
}




TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}{0,1,2,3}
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 --entry-timeout=0 --attribute-timeout=0 $M0;
cd $M0
TEST mkdir tmp{1..10}

##Remove internal mds xattr from all directory
remove_mds_xattr $B0/${V0}0
remove_mds_xattr $B0/${V0}1
remove_mds_xattr $B0/${V0}2
remove_mds_xattr $B0/${V0}3

##First set user.foo xattr with value abc on all dirs

TEST setfattr -n user.foo -v "abc" ./tmp{1..10}
EXPECT "abc" get_getfattr ./tmp{1..10}
EXPECT "abc" get_getfattr $B0/${V0}0/tmp{1..10}
EXPECT "abc" get_getfattr $B0/${V0}1/tmp{1..10}
EXPECT "abc" get_getfattr $B0/${V0}2/tmp{1..10}
EXPECT "abc" get_getfattr $B0/${V0}3/tmp{1..10}

cd -
TEST umount $M0

cd -
cleanup
exit
