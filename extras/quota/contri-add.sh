#!/bin/bash

# This script adds contributions of files/directories in backend to volume
# size.
# It can also be used to debug by passing dir as first argument, in which case
# it will just add contributions from immediate children of a directory and
# displays only if added contributions from immediate children is different
# from size stored in directory.
# For Eg., find <backend-directory> -type d -exec ./contri-add.sh dir \{} \;
# will list all the directories which have descrepancies in their
# size/contributions.

usage ()
{
    echo >&2 "usage: $0 <file|dir> <list-of-backend-directories>"
}

add_contributions ()
{
    local var=0
    local count=0

    SIZE=`getfattr -h -e hex -n trusted.glusterfs.quota.size $2 2>&1 | sed -e '/^#/d' | sed -e '/^getfattr/d' | sed -e '/^$/d' | cut -d'=' -f 2`
    CONTRI=`getfattr -h -e hex -d -m trusted.glusterfs.quota.*.contri $2 2>&1 | sed -e '/^#/d' | sed -e '/^getfattr/d' | sed -e '/^$/d' | cut -d'=' -f 2`

    if [ $1 == "file" ]; then
        PATHS=`find  $2 ! -type d | sed -e "\|^$2$|d" | sed -e '/^[ \t]*$/d'`
    else
        PATHS=`find  $2 -maxdepth 1 | sed -e "\|^$2$|d" | sed -e '/^[ \t]*$/d'`
    fi

    if [ -z "$PATHS" ]; then
        return 0
    fi

    CONTRIBUTIONS=`echo $PATHS | xargs getfattr -h -e hex -d -m trusted.glusterfs.quota.*.contri 2>&1 | sed -e '/^#/d' | sed -e '/^getfattr/d' | sed -e '/^$/d' | cut -d'=' -f 2 | sed -e 's/^[ \t]*\([^ \t]*\)/\1/g'`

    if [ -n "$CONTRIBUTIONS" ]; then
        for i in $CONTRIBUTIONS; do
            count=$(($count + 1))
            var=$(($var + $i))
        done
    fi

    if [ $1 == "file" ] || [ $var -ne $(($SIZE)) ] || [ $(($SIZE)) -ne $(($CONTRI)) ]; then
        if [ $1 == "dir" ]; then
            TMP_PATH=`echo $2 | sed -e "s/\/home\/export\/[0-9]*/\/mnt\/raghu/g"`
            stat $TMP_PATH > /dev/null
        fi

        echo "file count $count"
        echo "added contribution of $2=$var"
        echo "size stored in xattrs on $2=$(($SIZE))"
        echo "contribution of $2 to its parent directory=$(($CONTRI))"
        echo "=============================================================="
    fi
}


main ()
{
    [ $# -lt 1 ] && usage

    TYPE=$1

    shift 1

    for i in $@; do
        add_contributions $TYPE $i
    done
}

main $@