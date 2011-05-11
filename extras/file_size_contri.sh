#!/bin/bash

# This script checks whether the contribution and disk-usage of a file is same.

CONTRIBUTION_HEX=`getfattr -h -e hex -d -m trusted.glusterfs.quota.*.contri $1 2>&1 | sed -e '/^#/d' | sed -e '/^getfattr/d' | sed -e '/^$/d' | cut -d'=' -f 2`

BLOCKS=`stat -c %b $1`
SIZE=$(($BLOCKS * 512))

CONTRIBUTION=`printf "%d" $CONTRIBUTION_HEX`

if [ $CONTRIBUTION -ne $SIZE ]; then
    printf "contribution of %s:%d\n" $1 $CONTRIBUTION
    echo "size of $1: $SIZE"
    echo "==================================================="
fi
 
