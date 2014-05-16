#!/bin/bash

#Make sure glusterd and the brick processes are running on all nodes in the
#cluster.
#This script must be run prior to upgradation, that too on
#only one of the nodes in the cluster.

BACKUP_DIR=/var/tmp/glusterfs/quota-config-backup

mkdir -p $BACKUP_DIR
for i in `gluster volume list`; do
        var=$(gluster volume info $i | grep 'features.quota'| cut -d" " -f2);
        if  [ -z "$var" ] || [ "$var" = "off" ]; then
                continue
        else
                gluster volume quota $i list > $BACKUP_DIR/vol_$i;
        fi;
done
