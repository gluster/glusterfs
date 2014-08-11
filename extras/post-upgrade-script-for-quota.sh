#!/bin/bash

#The post-upgrade script must be executed after all the nodes in the cluster
#have upgraded.
#Also, all the clients accessing the given volume must also be upgraded
#before the script is run.
#Make sure glusterd and the brick processes are running on all nodes in the
#cluster post upgrade.
#Execute this script on the node where the pre-upgrade script for quota was run.

VOL=$1;

BACKUP_DIR=/var/tmp/glusterfs/quota-config-backup

function set_limits {
        local var=$(gluster volume info $1 | grep 'features.quota'| cut -d" " -f2);

        if  [ -z "$var" ] || [ "$var" = "off" ]; then
                if [ $2 -eq '0' ]; then
                        echo "Volume $1 does not have quota enabled. " \
                             "Exiting ..."
                        exit 1
                fi
        else
                gluster volume set $1 default-soft-limit 80%
                if [ $? -ne '0' ]; then
                        echo "Post-upgrade process failed." \
                             "Please address the error and run " \
                             "post-upgrade-script.sh on volume $VOL again."
                        exit 1
                fi

                gluster volume start $1 force
                sleep 3;

                local path_array=( $(cat $BACKUP_DIR/vol_$1 | tail -n +3 | awk '{print $1}') )
                local limit_array=( $(cat $BACKUP_DIR/vol_$1 | tail -n +3 | awk '{print $2}') )
                local len=${#path_array[@]}

                for ((j=0; j<$len; j++))
                    do
                            gluster volume quota $1 limit-usage ${path_array[$j]} ${limit_array[j]};
                            if [ $? -eq 0 ]; then
                                    echo "Setting limit (${limit_array[j]}) on " \
                                         "path ${path_array[$j]} has been " \
                                         "successful"
                            fi
                    done
        fi;
}

if [ -z $1 ]; then
        echo "Please provide volume name or 'all'";
        exit 1;
fi

if [ "$1" = "all" ]; then
        for VOL in `gluster volume list`;
            do
                    set_limits $VOL '1';
            done

else
        set_limits $1 '0';
fi
