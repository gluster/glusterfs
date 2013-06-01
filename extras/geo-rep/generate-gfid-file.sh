#!/bin/bash
#Usage: generate-gfid-file.sh <master-volfile-server:master-volume> <path-to-get-gfid.sh> <output-file>

function get_gfids()
{
    GET_GFID_CMD=$1
    OUTPUT_FILE=$2
    find . -exec $GET_GFID_CMD {} \; > $OUTPUT_FILE
}

function mount_client()
{
    local T; # temporary mount
    local i; # inode number

    VOLFILE_SERVER=$1;
    VOLUME=$2;
    GFID_CMD=$3;
    OUTPUT=$4;

    T=$(mktemp -d);

    glusterfs -s $VOLFILE_SERVER --volfile-id $VOLUME $T;

    i=$(stat -c '%i' $T);

    [ "x$i" = "x1" ] || fatal "could not mount volume $MASTER on $T";

    cd $T;

    get_gfids $GFID_CMD $OUTPUT

    cd -;

    umount $T || fatal "could not umount $MASTER from $T";

    rmdir $T || warn "rmdir of $T failed";
}


function main()
{
    SLAVE=$1
    GET_GFID_CMD=$2
    OUTPUT=$3

    VOLFILE_SERVER=`echo $SLAVE | sed -e 's/\(.*\):.*/\1/'`
    VOLUME_NAME=`echo $SLAVE | sed -e 's/.*:\(.*\)/\1/'`

    mount_client $VOLFILE_SERVER $VOLUME_NAME $GET_GFID_CMD $OUTPUT
}

main "$@";
