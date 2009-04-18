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
namespace_host=localhost

# This is the new mount point with 'cluster/distribute' volume
distribute_mount=/mnt/glusterfs

function execute_on()
{
    local node="$1"
    local cmd="$2"

    if [ "$node" = "localhost" ]; then
        $cmd
    else
        ssh "$node" sh -c "$cmd"
    fi
}

execute_on $namespace_host "cd ${namespace_export} && find ." |
(cd ${distribute_mount} && xargs -d '\n' stat -c '%n')
