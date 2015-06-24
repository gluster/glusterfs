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
    master_vol=$2
    slave_vol=$3
    GLUSTERD_WORKDIR=$(gluster system:: getwd)

    if [ "$user" == "" ];  then
        echo "Please enter the user's name"
        exit 1;
    fi

    if [ "$master_vol" == "" ]; then
        echo "Invalid master volume name"
        exit 1;
    fi

    if [ "$slave_vol" == "" ]; then
        echo "Invalid slave volume name"
        exit 1;
    fi

    COMMON_SECRET_PEM_PUB=${master_vol}_${slave_vol}_common_secret.pem.pub

    if [ "$user" == "root" ]; then
        echo "This script is not needed for root"
        exit 1;
    fi

    home_dir=`getent passwd $user | cut -d ':' -f 6`;

    if [ "$home_dir" == "" ]; then
        echo "No user $user found"
        exit 1;
    fi

    if [ -f $home_dir/${COMMON_SECRET_PEM_PUB} ]; then
        cp $home_dir/${COMMON_SECRET_PEM_PUB} ${GLUSTERD_WORKDIR}/geo-replication/
        gluster system:: copy file /geo-replication/${COMMON_SECRET_PEM_PUB}
        gluster system:: execute add_secret_pub $user geo-replication/${master_vol}_${slave_vol}_common_secret.pem.pub
    else
        echo "$home_dir/common_secret.pem.pub not present. Please run geo-replication command on master with push-pem option to generate the file"
        exit 1;
    fi
    exit 0;
}

main "$@";
