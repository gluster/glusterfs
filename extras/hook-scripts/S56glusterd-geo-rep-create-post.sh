#!/bin/bash

key_val_pair1=`echo $2 | cut -d ' ' -f 1`
key_val_pair2=`echo $2 | cut -d ' ' -f 2`
key_val_pair3=`echo $2 | cut -d ' ' -f 3`

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
if [ "$key" != "slave_ip" ]; then
    exit;
fi
if [ "$val" == "" ]; then
    exit;
fi
slave_ip=`echo $val`

if [ -f $pub_file ]; then
    scp $pub_file $slave_ip:$pub_file_tmp
    ssh $slave_ip "mv $pub_file_tmp $pub_file"
    ssh $slave_ip "gluster system:: copy file /geo-replication/common_secret.pem.pub > /dev/null"
    ssh $slave_ip "gluster system:: execute add_secret_pub > /dev/null"
fi
