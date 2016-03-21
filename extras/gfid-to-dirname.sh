#!/bin/bash

function read_symlink()
{
        DOT_GLUSTERFS_PATH=$BRICK_PATH/.glusterfs
        gfid_string=$1
        symlink_path="$DOT_GLUSTERFS_PATH/${gfid_string:0:2}/${gfid_string:2:2}/$gfid_string"
        #remove trailing '/'
        symlink_path=${symlink_path%/}
        linkname=$(readlink $symlink_path)
        if [ $? -ne 0 ]; then
                echo "readlink of $symlink_path returned an error." >&2
                exit -1
        fi
        echo $linkname
}

main()
{
        if [ $# -lt 2 ] ;then
                echo "Usage: $0 <brick-path> <gfid-string-of-directory>"
                echo "Example: $0 /bricks/brick1 1b835012-1ae5-4f0d-9db4-64de574d891c"
                exit -1
        fi

        BRICK_PATH=$1
        name=$(read_symlink $2)
        if [ $? -ne 0 ]; then
                exit -1
        fi

        while [ ${name:12:36} != "00000000-0000-0000-0000-000000000001" ]
        do
                LOCATION=`basename $name`/$LOCATION
                GFID_STRING=${name:12:36}
                name=$(read_symlink $GFID_STRING)
                if [ $? -ne 0 ]; then
                        exit -1
                fi
        done

        LOCATION=`basename $name`/$LOCATION
        echo "Location of the directory corresponding to gfid:$2 is $BRICK_PATH/$LOCATION"
}

main "$@"
