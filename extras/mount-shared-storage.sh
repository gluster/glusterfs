#!/bin/bash
#Post reboot there is a chance in which mounting of shared storage will fail
#This will impact starting of features like NFS-Ganesha. So this script will
#try to mount the shared storage if it fails
#TODO : Do it for other glusterfs clients in /etc/fstab

volume="gluster_shared_storage"
mp="/var/run/gluster/shared_storage"
#check if there is fstab entry for shared storage
gfc=$(sed -e 's/#.$//' </etc/fstab | grep -c $volume)
if [ $gfc -eq 0 ]
then
	exit 0
fi

#check whether shared storage is mounted
#if it is mounted then mount has inode value 1
inode=$(ls -id $mp | awk '{print $1}')

if [ $inode -eq 1 ]
then
	exit 0
fi

mount -t glusterfs localhost:/$volume $mp
#wait for few seconds
sleep 5

#recheck mount got succeed
inode=$(ls -id $mp | awk '{print $1}')
if [ $inode -eq 1 ]
then
	exit 0
else
	exit 1
fi
