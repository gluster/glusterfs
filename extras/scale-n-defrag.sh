#!/bin/sh

# This script runs over the GlusterFS mountpoint (from just one client)
# to handle the distribution of 'data', after the distribute translator's 
# subvolumes count changes.
#
# (c) 2009 Gluster Inc, <http://www.gluster.com/>
# 
# 
# Make sure the following variables are properly initialized

MOUNTPOINT=/tmp/testdir
directory_to_be_scaled="${MOUNTPOINT}/"

logdir=$(dirname $0)
cd $logdir
LOGDIR=$(pwd)
cd -

# The below command is enough to make sure the new layout will be scaled across new 
# nodes.
find ${directory_to_be_scaled} -type d -exec setfattr -x "trusted.glusterfs.dht" {} \;

# Now do a lookup on files so the scaling/re-hashing is done
find ${directory_to_be_scaled}  > /dev/null


# copy the defrag (to copy data across for new nodes (for linkfiles))
# 


cd ${directory_to_be_scaled};
for dir in *; do
        echo "Defragmenting directory ${directory_to_be_scaled}/$dir ($LOGDIR/defrag-store-$dir.log)"
        $LOGDIR/defrag.sh $dir >> $LOGDIR/defrag-store-$dir.log 2>&1
        echo Completed directory ${directory_to_be_scaled}/$dir
done
