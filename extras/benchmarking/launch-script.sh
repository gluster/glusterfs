#!/bin/sh

# This script is to launch the script in parallel across all the nodes.

mount_point="/mnt/glusterfs"
path_to_script="$mount_point}/benchmark/local-script.sh"

num_hosts=8

for i in $(seq 1 $num_hosts); do 
    ssh node$i path_to_script &
done

sleep 3;

touch ${mount_point}/benchmark/start-test


