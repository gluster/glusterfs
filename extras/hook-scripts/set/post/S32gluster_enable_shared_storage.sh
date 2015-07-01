#!/bin/bash

key=`echo $3 | cut -d '=' -f 1`
val=`echo $3 | cut -d '=' -f 2`
if [ ! "$key" -eq "enable-shared-storage" -o "$key" -eq "cluster.enable-shared-storage" ]; then
    exit;
fi
if [ "$val" != 'enable' ]; then
    if [ "$val" != 'disable' ]; then
        exit;
    fi
fi

option=$val

key_val_pair1=`echo $4 | cut -d ',' -f 1`
key_val_pair2=`echo $4 | cut -d ',' -f 2`

key=`echo $key_val_pair1 | cut -d '=' -f 1`
val=`echo $key_val_pair1 | cut -d '=' -f 2`
if [ "$key" != "is_originator" ]; then
    exit;
fi
is_originator=$val;

key=`echo $key_val_pair2 | cut -d '=' -f 1`
val=`echo $key_val_pair2 | cut -d '=' -f 2`
if [ "$key" != "local_node_hostname" ]; then
    exit;
fi
local_node_hostname=$val;

# Read gluster peer status to find the peers
# which are in 'Peer in Cluster' mode and
# are connected.

number_of_connected_peers=0
while read -r line
do
    # Already got two connected peers. Including the current node
    # we have 3 peers which is enough to create a shared storage
    # with replica 3
    if [ "$number_of_connected_peers" == "2" ]; then
        break;
    fi

    key=`echo $line | cut -d ':' -f 1`
    if [ "$key" == "Hostname" ]; then
        hostname=`echo $line | cut -d ':' -f 2 | xargs`
    fi

    if [ "$key" == "State" ]; then
        peer_state=`echo $line | cut -d ':' -f 2 | cut -d '(' -f 1 | xargs`
        conn_state=`echo $line | cut -d '(' -f 2 | cut -d ')' -f 1 | xargs`

        if [ "$peer_state" == "Peer in Cluster" ]; then
            if [ "$conn_state" == "Connected" ]; then
                ((number_of_connected_peers++))
                connected_peer[$number_of_connected_peers]=$hostname
            fi
        fi
    fi

done < <(gluster peer status)

# Include current node in connected peer list
((number_of_connected_peers++))
connected_peer[$number_of_connected_peers]=$local_node_hostname

# forming the create vol command
create_cmd="gluster --mode=script --wignore volume create \
            gluster_shared_storage replica $number_of_connected_peers"

# Adding the brick names in the command
for i in "${connected_peer[@]}"
do
    create_cmd=$create_cmd" "$i:"$GLUSTERD_WORKDIR"/ss_brick
done

if [ "$option" == "disable" ]; then
    # Unmount the volume on all the nodes
    umount /var/run/gluster/shared_storage
    cat /etc/fstab  | grep -v "gluster_shared_storage /var/run/gluster/shared_storage/" > /var/run/gluster/fstab.tmp
    mv /var/run/gluster/fstab.tmp /etc/fstab
fi

if [ "$is_originator" == 1 ]; then
    if [ "$option" == "enable" ]; then
        # Create and start the volume
        $create_cmd
        gluster --mode=script --wignore volume start gluster_shared_storage
    fi

    if [ "$option" == "disable" ]; then
        # Stop and delete the volume
        gluster --mode=script --wignore volume stop gluster_shared_storage
        gluster --mode=script --wignore volume delete gluster_shared_storage
    fi
fi

function check_volume_status()
{
    status=`gluster volume info gluster_shared_storage  | grep Status | cut -d ':' -f 2 | xargs`
    echo $status
}

mount_cmd="mount -t glusterfs "$local_node_hostname":/gluster_shared_storage \
           /var/run/gluster/shared_storage"

if [ "$option" == "enable" ]; then
    retry=0;
    # Wait for volume to start before mounting
    status=$(check_volume_status)
    while [ "$status" != "Started" ]; do
        sleep 5;
        ((retry++))
        if [ "$retry" == 3 ]; then
            break;
        fi
        status = check_volume_status;
    done
    # Mount the volume on all the nodes
    umount /var/run/gluster/shared_storage
    mkdir -p /var/run/gluster/shared_storage
    $mount_cmd
    cp /etc/fstab /var/run/gluster/fstab.tmp
    echo "$local_node_hostname:/gluster_shared_storage /var/run/gluster/shared_storage/ glusterfs defaults        0 0" >> /var/run/gluster/fstab.tmp
    mv /var/run/gluster/fstab.tmp /etc/fstab
fi
