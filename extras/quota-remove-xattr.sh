#!/bin/bash

# This script is used to remove xattrs set by quota translator on a path.
# It is generally invoked from quota-metadata-cleanup.sh, but can
# also be used stand-alone.

usage ()
{
    echo >&2 "usage: $0 <path>"
}

main ()
{
    [ $# -ne 1 ] && usage $0

    XATTR_KEY_VALUE_PAIRS=`getfattr -h -d -m 'trusted.glusterfs.quota' $1 2>/dev/null | sed -e '/^# file/d'`

    for i in $XATTR_KEY_VALUE_PAIRS; do
        XATTR_KEY=`echo $i | sed -e 's/\([^=]*\).*/\1/g'`
        setfattr -h -x $XATTR_KEY $1
    done
}

main $@
