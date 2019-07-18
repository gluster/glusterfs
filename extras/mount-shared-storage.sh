#!/bin/bash
#Post reboot there is a chance in which mounting of shared storage will fail
#This will impact starting of features like NFS-Ganesha. So this script will
#try to mount the shared storage if it fails

exitStatus=0

while IFS= read -r glm
do
	IFS=$' \t' read -r -a arr <<< "$glm"

	#Validate storage type is glusterfs
	if [ "${arr[2]}" == "glusterfs" ]
	then

		#check whether shared storage is mounted
		#if it is mounted then mountpoint -q will return a 0 success code
		if mountpoint -q "${arr[1]}"
		then
			echo "${arr[1]} is already mounted"
			continue
		fi

		mount -t glusterfs -o "${arr[3]}" "${arr[0]}" "${arr[1]}"
		#wait for few seconds
		sleep 10

		#recheck mount got succeed
		if mountpoint -q "${arr[1]}"
		then
			echo "${arr[1]} has been mounted"
			continue
		else
			echo "${arr[1]} failed to mount"
			exitStatus=1
		fi
	fi
done <<< "$(sed '/^#/ d' </etc/fstab | grep 'glusterfs')"
exit $exitStatus
