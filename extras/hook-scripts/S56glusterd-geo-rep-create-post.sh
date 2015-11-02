#!/bin/bash

#key_val_pair is the arguments passed to the script in the form of
#key value pair

key_val_pair1=`echo $2 | cut -d ',' -f 1`
key_val_pair2=`echo $2 | cut -d ',' -f 2`
key_val_pair3=`echo $2 | cut -d ',' -f 3`
key_val_pair4=`echo $2 | cut -d ',' -f 4`
key_val_pair5=`echo $2 | cut -d ',' -f 5`
key_val_pair6=`echo $2 | cut -d ',' -f 6`

mastervol=`echo $1 | cut -d '=' -f 2`
if [ "$mastervol" == "" ]; then
    exit;
fi

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
pub_file_bname="$(basename $pub_file)"
pub_file_dname="$(dirname $pub_file)"
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

key=`echo $key_val_pair5 | cut -d '=' -f 1`
val=`echo $key_val_pair5 | cut -d '=' -f 2`
if [ "$key" != "slave_vol" ]; then
    exit;
fi
if [ "$val" == "" ]; then
    exit;
fi
slavevol=`echo $val`

key=`echo $key_val_pair6 | cut -d '=' -f 1`
val=`echo $key_val_pair6 | cut -d '=' -f 2`
if [ "$key" != "ssh_port" ]; then
    exit;
fi
if [ "$val" == "" ]; then
    exit;
fi
SSH_PORT=`echo $val`

if [ -f $pub_file ]; then
    # For a non-root user copy the pub file to the user's home directory
    # For a root user copy the pub files to priv_dir->geo-rep.
    if [ "$slave_user" != "root" ]; then
        slave_user_home_dir=`ssh -p ${SSH_PORT} $slave_user@$slave_ip "getent passwd $slave_user | cut -d ':' -f 6"`
        scp -P ${SSH_PORT} $pub_file $slave_user@$slave_ip:$slave_user_home_dir/common_secret.pem.pub_tmp
        ssh -p ${SSH_PORT} $slave_user@$slave_ip "mv $slave_user_home_dir/common_secret.pem.pub_tmp $slave_user_home_dir/${mastervol}_${slavevol}_common_secret.pem.pub"
    else
        scp -P ${SSH_PORT} $pub_file $slave_ip:$pub_file_tmp
        ssh -p ${SSH_PORT} $slave_ip "mv $pub_file_tmp ${pub_file_dname}/${mastervol}_${slavevol}_${pub_file_bname}"
        ssh -p ${SSH_PORT} $slave_ip "gluster system:: copy file /geo-replication/${mastervol}_${slavevol}_common_secret.pem.pub > /dev/null"
        ssh -p ${SSH_PORT} $slave_ip "gluster system:: execute add_secret_pub root geo-replication/${mastervol}_${slavevol}_common_secret.pem.pub > /dev/null"
    fi
fi
