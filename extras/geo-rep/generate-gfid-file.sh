#!/bin/bash
#Usage: generate-gfid-file.sh <master-volfile-server:master-volume> <path-to-get-gfid.sh> <output-file> [dirs-list-file]

function get_gfids()
{
    GET_GFID_CMD=$1
    OUTPUT_FILE=$2
    DIR_PATH=$3
    find "$DIR_PATH" -exec $GET_GFID_CMD {} \; >> $OUTPUT_FILE
}

function mount_client()
{
    local T; # temporary mount
    local i; # inode number

    VOLFILE_SERVER=$1;
    VOLUME=$2;
    GFID_CMD=$3;
    OUTPUT=$4;

    T=$(mktemp -d -t ${0##*/}.XXXXXX);

    glusterfs -s $VOLFILE_SERVER --volfile-id $VOLUME $T;

    i=$(stat -c '%i' $T);

    [ "x$i" = "x1" ] || fatal "could not mount volume $MASTER on $T";

    cd $T;
    rm -f $OUTPUT;
    touch $OUTPUT;

    if [ "$DIRS_FILE" = "." ]
    then
        get_gfids $GFID_CMD $OUTPUT "."
    else
        while read line
        do
            get_gfids $GFID_CMD $OUTPUT "$line"
        done < $DIRS_FILE
    fi;

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

    if [ "$#" -lt 4 ]
    then
        DIRS_FILE="."
    else
        DIRS_FILE=$4
    fi
    mount_client $VOLFILE_SERVER $VOLUME_NAME $GET_GFID_CMD $OUTPUT $DIRS_FILE
}

main "$@";
