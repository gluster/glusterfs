#!/bin/bash
#usage: secondary-upgrade.sh <volfile-server:volname> <gfid-file>
#                        <path-to-gsync-sync-gfid>
#<secondary-volfile-server>: a machine on which gluster cli can fetch secondary volume info.
#                        secondary-volfile-server defaults to localhost.
#
#<gfid-file>: a file containing paths and their associated gfids
#            on primary. The paths are relative to primary mount point
#            (not absolute). An example extract of <gfid-file> can be,
#
#            <extract>
#            22114455-57c5-46e9-a783-c40f83a72b09 /dir
#            25772386-3eb8-4550-a802-c3fdc938ca80 /dir/file
#            </extract>

function get_bricks()
{
    gluster volume info $1 | grep -E 'Brick[0-9]+' | sed -e 's/[^:]*:\(.*\)/\1/g'
}

function cleanup_brick()
{
    HOST=$1
    BRICK=$2

    # TODO: write a C program to receive a list of files and does cleanup on
    # them instead of spawning a new setfattr process for each file if
    # performance is bad.
    ssh $HOST "rm -rf $BRICK/.glusterfs/* && find $BRICK -exec setfattr -x trusted.gfid {} \; 2>/dev/null"
}

function cleanup_secondary()
{
    VOLUME_NAME=`echo $1 | sed -e 's/.*:\(.*\)/\1/'`

    BRICKS=`get_bricks $VOLUME_NAME`

    for i in $BRICKS; do
	HOST=`echo $i | sed -e 's/\(.*\):.*/\1/'`
	BRICK=`echo $i | sed -e 's/.*:\(.*\)/\1/'`
	cleanup_brick $HOST $BRICK
    done

    # Now restart the volume
    gluster --mode=script volume stop $VOLUME_NAME;
    gluster volume start $VOLUME_NAME;
}

function mount_client()
{
    local T; # temporary mount
    local i; # inode number

    VOLUME_NAME=$2;
    GFID_FILE=$3
    SYNC_CMD=$4

    T=$(mktemp -d -t ${0##*/}.XXXXXX);

    glusterfs --aux-gfid-mount -s $1 --volfile-id $VOLUME_NAME $T;

    i=$(stat -c '%i' $T);

    cd $T;

    $SYNC_CMD $GFID_FILE

    cd -;

    umount $T || fatal "could not umount $PRIMARY from $T";

    rmdir $T || warn "rmdir of $T failed";
}

function sync_gfids()
{
    SECONDARY=$1
    GFID_FILE=$2
    SYNC_CMD=$3

    SECONDARY_VOLFILE_SERVER=`echo $SECONDARY | sed -e 's/\(.*\):.*/\1/'`
    SECONDARY_VOLUME_NAME=`echo $SECONDARY | sed -e 's/.*:\(.*\)/\1/'`

    if [ "x$SECONDARY_VOLFILE_SERVER" = "x" ]; then
        SECONDARY_VOLFILE_SERVER="localhost"
    fi

    mount_client $SECONDARY_VOLFILE_SERVER $SECONDARY_VOLUME_NAME $GFID_FILE $SYNC_CMD
}

function upgrade()
{
    SECONDARY=$1
    GFID_FILE=$2
    SYNC_CMD=$3

    cleanup_secondary $SECONDARY

    sync_gfids $SECONDARY $GFID_FILE $SYNC_CMD
}

upgrade "$@"
