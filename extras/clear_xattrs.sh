#!/bin/bash

# Clear the trusted.gfid xattr in the brick tree

# This script must be run only on a stopped brick/volume
# Stop the volume to make sure no rebalance/replace-brick
# operations are on-going

# Not much error checking
remove_xattrs ()
{
    find "$1" -exec setfattr -h -x "trusted.gfid" '{}' \;  > /dev/null 2>&1;
    find "$1" -exec setfattr -h -x "trusted.glusterfs.volume-id" '{}' \;  > /dev/null 2>&1;
}

main ()
{
    if [ -z "$1" ]; then
        echo "Usage: $0 <brick_path(s)>";
        exit 1;
    fi

    export PATH;
    which getfattr > /dev/null 2>&1;
    if [ $? -ne 0 ]; then
        echo "attr package missing";
        exit 2;
    fi

    which setfattr > /dev/null 2>&1;
    if [ $? -ne 0 ]; then
        echo "attr package missing";
        exit 2;
    fi

    for brick in "$@";
    do
        stat "$brick" > /dev/null 2>&1;
        if [ $? -ne 0 ]; then
            echo "brick: $brick does not exist";
            exit 3;
        fi
        if [ ! -d "$brick" ]; then
            echo "$brick: not a directory";
            exit 4;
        fi
        echo "xattr clean-up in progress: $brick";
        remove_xattrs "$brick";
        echo "$brick ready to be used as a glusterfs brick";
    done;
}

main "$@";