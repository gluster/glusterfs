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
        echo "Please specify the brick path(s)";
        exit 1;
    fi

    which getfattr > /dev/null 2>&1;
    if [ $? -ne 0 ]; then
        echo "attr package missing";
        exit 1;
    fi

    which setfattr > /dev/null 2>&1;
    if [ $? -ne 0 ]; then
        echo "attr package missing";
        exit 1;
    fi

    for brick in "$@";
    do
        echo "xattr clean-up in progress: $brick";
        remove_xattrs "$brick";
        echo "$brick ready to be used as a glusterfs brick";
    done;
}

main "$@";