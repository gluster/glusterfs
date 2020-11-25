#!/bin/bash
#usage: gsync-upgrade.sh <secondary-volfile-server:secondary-volume> <gfid-file>
#                        <path-to-gsync-sync-gfid> <ssh-identity-file>
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
#
#<ssh-identity-file>: file from which the identity (private key) for public key authentication is read.

SECONDARY_MOUNT='/tmp/glfs_secondary'

function SSH()
{
    HOST=$1
    SSHKEY=$2

    shift 2

    ssh -qi $SSHKEY \
        -oPasswordAuthentication=no \
        -oStrictHostKeyChecking=no \
        "$HOST" "$@";
}

function get_bricks()
{
    SSHKEY=$3

    SSH $1 $SSHKEY "gluster volume info $2" | grep -E 'Brick[0-9]+' | sed -e 's/[^:]*:\(.*\)/\1/g'
}

function cleanup_brick()
{
    HOST=$1
    BRICK=$2
    SSHKEY=$3

    # TODO: write a C program to receive a list of files and does cleanup on
    # them instead of spawning a new setfattr process for each file if
    # performance is bad.
    SSH -i $SSHKEY $HOST  "rm -rf $BRICK/.glusterfs/* && find $BRICK -exec setfattr -x trusted.gfid {} \;"
}

function cleanup_secondary()
{
    SSHKEY=$2

    VOLFILE_SERVER=`echo $1 | sed -e 's/\(.*\):.*/\1/'`
    VOLUME_NAME=`echo $1 | sed -e 's/.*:\(.*\)/\1/'`

    BRICKS=`get_bricks $VOLFILE_SERVER $VOLUME_NAME $SSHKEY`

    for i in $BRICKS; do
	HOST=`echo $i | sed -e 's/\(.*\):.*/\1/'`
	BRICK=`echo $i | sed -e 's/.*:\(.*\)/\1/'`
	cleanup_brick $HOST $BRICK $SSHKEY
    done

    SSH -i $SSHKEY $VOLFILE_SERVER "gluster --mode=script volume stop $VOLUME_NAME; gluster volume start $VOLUME_NAME";

}

function mount_client()
{
    local T; # temporary mount
    local i; # inode number
    GFID_FILE=$3
    SYNC_CMD=$4

    T=$(mktemp -d -t ${0##*/}.XXXXXX);

    glusterfs --aux-gfid-mount -s $1 --volfile-id $2 $T;

    i=$(stat -c '%i' $T);

    [ "x$i" = "x1" ] || fatal "could not mount volume $PRIMARY on $T";

    cd $T;

    $SYNC_CMD $GFID_FILE

    cd -;

    umount -l $T || fatal "could not umount $PRIMARY from $T";

    rmdir $T || warn "rmdir of $T failed";
}

function sync_gfids()
{
    SECONDARY=$1
    GFID_FILE=$2

    SECONDARY_VOLFILE_SERVER=`echo $SECONDARY | sed -e 's/\(.*\):.*/\1/'`
    SECONDARY_VOLUME_NAME=`echo $SECONDARY | sed -e 's/.*:\(.*\)/\1/'`

    if [ "x$SECONDARY_VOLFILE_SERVER" = "x" ]; then
        SECONDARY_VOLFILE_SERVER="localhost"
    fi

    mount_client $SECONDARY_VOLFILE_SERVER $SECONDARY_VOLUME_NAME $GFID_FILE $3
}

function upgrade()
{
    SECONDARY=$1
    GFID_FILE=$2
    SYNC_CMD=$3
    SSHKEY=$4

    cleanup_secondary $SECONDARY $SSHKEY
    sync_gfids $SECONDARY $GFID_FILE $SYNC_CMD
}

upgrade "$@"
