#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../nfs.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc

# This function returns a value "Y" if user can execute
# the given command. Else it will return "N"
# @arg-1 : Name of the user
# @arg-2 : Path of the file
# @arg-3 : command to be executed
function check_if_permitted () {
        local usr=$1
        local path=$2
        local cmd=$3
        local var
        local ret
        var=$(su - $usr -c "$cmd $path")
        ret=$?

        if [ "$cmd" == "cat" ]
        then
                if [ "$var" == "Test" ]
                then
                        echo "Y"
                else
                        echo "N"
                fi
        else
                if [ "$ret" == "0" ]
                then
                        echo "Y"
                else
                        echo "N"
                fi
        fi
}

# Create a directory in /tmp to specify which directory to make
# as home directory for user
home_dir=$(mktemp -d)
chmod 777 $home_dir

function get_new_user() {
        local temp=$(uuidgen | tr -dc 'a-zA-Z' | head -c 8)
        id $temp
        if [ "$?" == "0" ]
        then
                get_new_user
        else
                echo $temp
        fi
}

function create_user() {
        local user=$1
        local group=$2

        if [ "$group" == "" ]
        then
                /usr/sbin/useradd -d $home_dir/$user $user
        else
                /usr/sbin/useradd -d $home_dir/$user -G $group $user
        fi

        return $?
}

cleanup;

TEST setup_lvm 1
TEST glusterd

TEST $CLI volume create $V0 $H0:$L1
TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume start $V0

# Mount the volume as both fuse and nfs mount
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available
TEST glusterfs -s $H0 --volfile-id $V0 $M0
TEST mount_nfs $H0:/$V0 $N0 nolock

# Create 2 user
user1=$(get_new_user)
create_user $user1
user2=$(get_new_user)
create_user $user2

# create a file for which only user1 has access
echo "Test" > $M0/README
chown $user1 $M0/README
chmod 700 $M0/README

# enable uss and take a snapshot
TEST $CLI volume set $V0 uss enable
TEST $CLI snapshot config activate-on-create on
TEST $CLI snapshot create snap1 $V0 no-timestamp

# try to access the file using user1 account.
# It should succeed with both normal mount and snapshot world.
# There is time delay in which snapd might not have got the notification
# from glusterd about snapshot create hence using "EXPECT_WITHIN"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" check_if_permitted $user1 $M0/README cat
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" check_if_permitted $user1 $N0/README cat
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" check_if_permitted $user1 $M0/.snaps/snap1/README cat
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" check_if_permitted $user1 $N0/.snaps/snap1/README cat


# try to access the file using user2 account
# It should fail from both normal mount and snapshot world
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" check_if_permitted $user2 $M0/README cat
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" check_if_permitted $user2 $N0/README cat
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" check_if_permitted $user2 $M0/.snaps/snap1/README cat
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" check_if_permitted $user2 $N0/.snaps/snap1/README cat

# We need to test another scenario where user belonging to one group
# tries to access files from user belonging to another group
# instead of using the already created users and making the test case look complex
# I thought of using two different users.

# The test case written below does the following things
# 1) Create 2 users (user{3,4}), belonging to 2 different groups (group{3,4})
# 2) Take a snapshot "snap2"
# 3) Create a file for which only users belonging to group3 have
# permission to read
# 4) Test various combinations of Read-Write, Fuse-NFS mount, User{3,4,5}
#    from both normal mount, and USS world.

echo "Test" > $M0/file3

chmod 740 $M0/file3

group3=$(get_new_user)
groupadd $group3

group4=$(get_new_user)
groupadd $group4

user3=$(get_new_user)
create_user $user3 $group3

user4=$(get_new_user)
create_user $user4 $group4

user5=$(get_new_user)
create_user $user5

chgrp $group3 $M0/file3

TEST $CLI snapshot create snap2 $V0 no-timestamp

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" check_if_permitted $user3 $M0/file3 cat
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" check_if_permitted $user3 $M0/.snaps/snap2/file3 cat
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" check_if_permitted $user3 $M0/file3 "echo Hello >"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" check_if_permitted $user3 $M0/.snaps/snap2/file3 "echo Hello >"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" check_if_permitted $user3 $N0/file3 cat
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" check_if_permitted $user3 $N0/.snaps/snap2/file3 cat
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" check_if_permitted $user3 $N0/file3 "echo Hello >"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" check_if_permitted $user3 $N0/.snaps/snap2/file3 "echo Hello >"


EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" check_if_permitted $user4 $M0/file3 cat
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" check_if_permitted $user4 $M0/.snaps/snap2/file3 cat
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" check_if_permitted $user4 $M0/file3 "echo Hello >"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" check_if_permitted $user4 $M0/.snaps/snap2/file3 "echo Hello >"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" check_if_permitted $user4 $N0/file3 cat
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" check_if_permitted $user4 $N0/.snaps/snap2/file3 cat
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" check_if_permitted $user4 $N0/file3 "echo Hello >"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" check_if_permitted $user4 $N0/.snaps/snap2/file3 "echo Hello >"

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" check_if_permitted $user5 $M0/file3 cat
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" check_if_permitted $user5 $M0/.snaps/snap2/file3 cat
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" check_if_permitted $user5 $M0/file3 "echo Hello >"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" check_if_permitted $user5 $M0/.snaps/snap2/file3 "echo Hello >"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" check_if_permitted $user5 $N0/file3 cat
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" check_if_permitted $user5 $N0/.snaps/snap2/file3 cat
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" check_if_permitted $user5 $N0/file3 "echo Hello >"
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" check_if_permitted $user5 $N0/.snaps/snap2/file3 "echo Hello >"

# cleanup
/usr/sbin/userdel -f -r $user1
/usr/sbin/userdel -f -r $user2
/usr/sbin/userdel -f -r $user3
/usr/sbin/userdel -f -r $user4
/usr/sbin/userdel -f -r $user5

#cleanup all the home directory which is created as part of this test case
if [ -d "$home_dir" ]
then
        rm -rf $home_dir
fi


groupdel $group3
groupdel $group4

TEST $CLI snapshot delete all

cleanup;


