#!/bin/sh

#
# This is a template script which can be used to migrate the GlusterFS
# storage infrastructure from 'cluster/unify' to 'cluster/distribute'

# This script needs to be executed on the machine where namespace volume 
# of 'cluster/unify' translator resides. And also, one need to mount the 
# new 'cluster/distribute' volume with "option lookup-unhashed yes" on 
# the same machine.
# If the namespace volume was replicated (ie, afr'ed), then this can be 
# executed just on one of the namespace machines..

# Only the variables defined below needs to be changed to appropriate path

# This is export from old 'cluster/unify' volume's namespace volume.
namespace_export=/exports/export-ns

# This is the new mount point with 'cluster/distribute' volume
distribute_mount=/mnt/glusterfs

cd ${namespace_export};
find . -exec stat ${distribute_mount}/{} \;
