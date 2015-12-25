#!/bin/sh

# This script can be used to cleanup the 'cluster/distribute' translator's 
# stale link files. One may choose to run this only when number of subvolumes
# to distribute volume gets increased (or decreased) 
# 
# This script has to be run on the servers, which are exporting the data to 
# GlusterFS
#
# (c) 2010 Gluster Inc <http://www.gluster.com/> 

set -e

# Change the below variable as per the setup.
export_directory="/export/glusterfs"

clean_dir()
{
    # Clean the 'link' files on backend
    find "${export_directory}" -type f -perm /01000 -exec rm -v '{}' \;
}

main()
{
    clean_dir ;
}

main "$@"
