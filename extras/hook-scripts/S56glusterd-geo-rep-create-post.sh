#!/bin/bash

key_val_pair1=`echo $2 | cut -d ',' -f 1`
key_val_pair2=`echo $2 | cut -d ',' -f 2`
key_val_pair3=`echo $2 | cut -d ',' -f 3`
key_val_pair4=`echo $2 | cut -d ',' -f 4`

key=`echo $key_val_pair1 | cut -d '=' -f 1`
val=`echo $key_val_pair1 | cut -d '=' -f 2`
if [ "$key" != "is_push_pem" ]; then
    exit;
fi
if [ "$val" != '1' ]; then
    exit;
fi

key=`echo $key_val_pair2 | cut -d '=' -f 1`
val=`echo $key_val_pair2 | cut -d '=' -f 2`
if [ "$key" != "pub_file" ]; then
    exit;
fi
if [ "$val" == "" ]; then
    exit;
fi
pub_file=`echo $val`
pub_file_tmp=`echo $val`_tmp

key=`echo $key_val_pair3 | cut -d '=' -f 1`
val=`echo $key_val_pair3 | cut -d '=' -f 2`
if [ "$key" != "slave_user" ]; then
    exit;
fi
if [ "$val" == "" ]; then
    exit;
fi
slave_user=`echo $val`

key=`echo $key_val_pair4 | cut -d '=' -f 1`
val=`echo $key_val_pair4 | cut -d '=' -f 2`
if [ "$key" != "slave_ip" ]; then
    exit;
fi
if [ "$val" == "" ]; then
    exit;
fi
slave_ip=`echo $val`

if [ -f $pub_file ]; then
    # For a non-root user copy the pub file to the user's home directory
    # For a root user copy the pub files to priv_dir->geo-rep.
    if [ "$slave_user" != "root" ]; then
        slave_user_home_dir=`ssh $slave_user@$slave_ip "getent passwd $slave_user | cut -d ':' -f 6"`
        scp $pub_file $slave_user@$slave_ip:$slave_user_home_dir/common_secret.pem.pub_tmp
        ssh $slave_user@$slave_ip "mv $slave_user_home_dir/common_secret.pem.pub_tmp $slave_user_home_dir/common_secret.pem.pub"
    else
        scp $pub_file $slave_ip:$pub_file_tmp
        ssh $slave_ip "mv $pub_file_tmp $pub_file"
        ssh $slave_ip "gluster system:: copy file /geo-replication/common_secret.pem.pub > /dev/null"
        ssh $slave_ip "gluster system:: execute add_secret_pub > /dev/null"
    fi
fi
