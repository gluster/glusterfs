#!/bin/bash

# Script to copy the pem keys from the user's home directory
# to $GLUSTERD_WORKDIR/geo-replication and then copy
# the keys to other nodes in the cluster and add them to the
# respective authorized keys. The script takes as argument the
# user name and assumes that the user will be present in all
# the nodes in the cluster. Not to be used for root user

function main()
{
    user=$1
    if [ "$user" == "" ];  then
        echo "Please enter the user's name"
        exit 1;
    fi

    if [ "$user" == "root" ]; then
        echo "This script is not needed for root"
        exit 1;
    fi

    home_dir=`getent passwd $user | cut -d ':' -f 6`;

    if [ "$home_dir" == "" ]; then
        echo "No user $user found"
        exit 1;
    fi

    if [ -f $home_dir/common_secret.pem.pub ]; then
        cp $home_dir/common_secret.pem.pub ${GLUSTERD_WORKDIR}/geo-replication/
        gluster system:: copy file /geo-replication/common_secret.pem.pub
        gluster system:: execute add_secret_pub $user
    else
        echo "$home_dir/common_secret.pem.pub not present. Please run geo-replication command on master with push-pem option to generate the file"
        exit 1;
    fi
    exit 0;
}

main "$@";
