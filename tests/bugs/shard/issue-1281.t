#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0

#Open a file and store descriptor in fd = 5
exec 5>$M0/foo

#Unlink the same file which is opened in prev step
TEST unlink $M0/foo

#Write something on the file using the open fd = 5
echo "issue-1281" >&5

#Write on the descriptor should be succesful
EXPECT 0 echo $?

#Close the fd = 5
exec 5>&-

cleanup
