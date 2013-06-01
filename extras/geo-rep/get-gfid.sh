#!/bin/bash

ATTR_STR=`getfattr -h $1 -n glusterfs.gfid.string`
GLFS_PATH=`echo $ATTR_STR | sed -e 's/# file: \(.*\) glusterfs.gfid.string*/\1/g'`
GFID=`echo $ATTR_STR | sed -e 's/.*glusterfs.gfid.string="\(.*\)"/\1/g'`

echo "$GFID $GLFS_PATH"
