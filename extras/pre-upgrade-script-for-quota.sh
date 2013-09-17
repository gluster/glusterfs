#!/bin/bash

#Make sure glusterd and the brick processes are running on all nodes in the
#cluster.
#This script must be run prior to upgrading the cluster to 3.5, that too on
#only one of the nodes in the cluster.

mkdir -p /tmp/quota-config-backup
for i in `gluster volume list`; do
        var=$(gluster volume info $i | grep 'features.quota'| cut -d" " -f2);
        if  [ -z "$var" ] || [ "$var" == "off" ]; then
                continue
        else
                gluster volume quota $i list > /tmp/quota-config-backup/vol_$i;
        fi;
done
