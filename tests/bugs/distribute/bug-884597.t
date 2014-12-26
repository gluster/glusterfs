#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../dht.rc
. $(dirname $0)/../../volume.rc

cleanup;
BRICK_COUNT=3
function uid_gid_compare()
{
        val=1

        if [ "$1" == "$3" ]
        then
                if [ "$2" == "$4" ]
                        then
                                val=0
                fi
        fi
        echo "$val"
}


TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1 $H0:$B0/${V0}2
TEST $CLI volume start $V0

## Mount FUSE
TEST glusterfs --attribute-timeout=0 --entry-timeout=0 -s $H0 --volfile-id $V0 $M0;

i=1
NEW_UID=36
NEW_GID=36

TEST touch $M0/$i

chown $NEW_UID:$NEW_GID $M0/$i
## rename till file gets a linkfile

has_link=0
while [ $i -lt 100 ]
do
        mv $M0/$i $M0/$(( $i+1 ))
        if [ $? -ne 0 ]
        then
                break
        fi
        let i++
        file_has_linkfile $i
        has_link=$?
        if [ $has_link -eq 2 ]
        then
                break;
        fi
done

TEST [ $has_link -eq 2 ]

get_hashed_brick $i
cached=$?

# check if uid/gid on linkfile is created with correct uid/gid
BACKEND_UID=`stat -c %u $B0/${V0}$cached/$i`;
BACKEND_GID=`stat -c %g $B0/${V0}$cached/$i`;

EXPECT "0" uid_gid_compare $NEW_UID $NEW_GID $BACKEND_UID $BACKEND_GID

# remove linkfile from backend, and trigger a lookup heal. uid/gid should match
rm -rf $B0/${V0}$cached/$i

# without a unmount, we are not able to trigger a lookup based heal

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

## Mount FUSE
TEST glusterfs --attribute-timeout=0 --entry-timeout=0 -s $H0 --volfile-id $V0 $M0;

lookup=`ls -l $M0/$i 2>/dev/null`

# check if uid/gid on linkfile is created with correct uid/gid
BACKEND_UID=`stat -c %u $B0/${V0}$cached/$i`;
BACKEND_GID=`stat -c %g $B0/${V0}$cached/$i`;

EXPECT "0" uid_gid_compare $NEW_UID $NEW_GID $BACKEND_UID $BACKEND_GID
# create hardlinks. Make sure a linkfile gets created

i=1
NEW_UID=36
NEW_GID=36

TEST touch $M0/file
chown $NEW_UID:$NEW_GID $M0/file;

## ln till file gets a linkfile

has_link=0
while [ $i -lt 100 ]
do
        ln $M0/file $M0/link$i
        if [ $? -ne 0 ]
        then
                break
        fi
        file_has_linkfile link$i
        has_link=$?
        if [ $has_link -eq 2 ]
        then
                break;
        fi
        let i++
done

TEST [ $has_link -eq 2 ]

get_hashed_brick link$i
cached=$?

# check if uid/gid on linkfile is created with correct uid/gid
BACKEND_UID=`stat -c %u $B0/${V0}$cached/link$i`;
BACKEND_GID=`stat -c %g $B0/${V0}$cached/link$i`;

EXPECT "0" uid_gid_compare $NEW_UID $NEW_GID $BACKEND_UID $BACKEND_GID

## UID/GID creation as different user
i=1
NEW_UID=36
NEW_GID=36

TEST touch $M0/user_file1
TEST chown $NEW_UID:$NEW_GID $M0/user_file1;

## Give permission on volume, so that different users can perform rename

TEST chmod 0777 $M0

## Add a user known as ABC and perform renames
TEST `useradd -M ABC 2>/dev/null`

TEST cd $M0
## rename as different user till file gets a linkfile

has_link=0
while [ $i -lt 100 ]
do
        su -m ABC -c "mv $M0/user_file$i $M0/user_file$(( $i+1 ))"
        if [ $? -ne 0 ]
        then
                break
        fi
        let i++
        file_has_linkfile user_file$i
        has_link=$?
        if [ $has_link -eq 2 ]
        then
                break;
        fi
done

TEST [ $has_link -eq 2 ]

## del user ABC
TEST userdel ABC

get_hashed_brick user_file$i
cached=$?

# check if uid/gid on linkfile is created with correct uid/gid
BACKEND_UID=`stat -c %u $B0/${V0}$cached/user_file$i`;
BACKEND_GID=`stat -c %g $B0/${V0}$cached/user_file$i`;

EXPECT "0" uid_gid_compare $NEW_UID $NEW_GID $BACKEND_UID $BACKEND_GID
cleanup;
