#!/bin/bash
. $(dirname $0)/../../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --acl --volfile-server=$H0 $M0;

cd $M0
TEST mkdir test
TEST setfacl -m d:m:rwx test
TEST setfacl -m d:u::rwx test
TEST setfacl -m d:g::rwx test
TEST setfacl -m d:o::rwx test

TEST mkdir test/new
EXPECT "user::rwx" echo $(getfacl test/new | grep -E "^user")
EXPECT "group::rwx" echo $(getfacl test/new | grep -E "^group")
EXPECT "mask::rwx" echo $(getfacl test/new | grep -E "^mask")
EXPECT "other::rwx" echo $(getfacl test/new | grep -E "^other")
EXPECT "default:user::rwx" echo $(getfacl test/new | grep -E "^default:user")
EXPECT "default:group::rwx" echo $(getfacl test/new | grep -E "^default:group")
EXPECT "default:mask::rwx" echo $(getfacl test/new | grep -E "^default:mask")
EXPECT "default:other::rwx" echo $(getfacl test/new | grep -E "^default:other")
cd -

cleanup
