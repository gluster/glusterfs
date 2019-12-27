#!/bin/bash

SCRIPT_TIMEOUT=250

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../dht.rc


# create 2 subvols
# create a dir
# create a file
# change layout
# remove the file
# execute create from a different mount
# Without the patch, the file will be present on both of the bricks

cleanup

function get_layout () {

layout=`getfattr -n trusted.glusterfs.dht -e hex $1 2>&1 | grep dht | gawk -F"=" '{print $2}'`

echo $layout

}

function set_layout()
{
    setfattr -n  "trusted.glusterfs.dht" -v $1 $2
}

TEST glusterd
TEST pidof glusterd

BRICK1=$B0/${V0}-0
BRICK2=$B0/${V0}-1

TEST $CLI volume create $V0 $H0:$BRICK1 $H0:$BRICK2
TEST $CLI volume start $V0

# Mount FUSE and create symlink
TEST glusterfs -s $H0 --volfile-id $V0 $M0
TEST mkdir $M0/dir
TEST touch $M0/dir/file
TEST ! stat "$BRICK1/dir/file"
TEST stat "$BRICK2/dir/file"

layout1="$(get_layout "$BRICK1/dir")"
layout2="$(get_layout "$BRICK2/dir")"

TEST set_layout $layout1 "$BRICK2/dir"
TEST set_layout $layout2 "$BRICK1/dir"

TEST rm $M0/dir/file -f
TEST gluster v set $V0 client-log-level DEBUG

#Without the patch in place, this client will create the file in $BRICK2
#which will lead to two files being on both the bricks when a new client
#create the file with the same name
TEST touch $M0/dir/file

TEST glusterfs -s $H0 --volfile-id $V0 $M1
TEST touch $M1/dir/file

TEST stat "$BRICK1/dir/file"
TEST ! stat "$BRICK2/dir/file"

cleanup
