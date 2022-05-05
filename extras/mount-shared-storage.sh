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

		#Check whether shared storage is already mounted, systemd.automount will be ignored
		if grep -q -P '^(?!systemd).*'"${arr[1]}" /proc/mounts
		then
			echo "${arr[1]} is already mounted"
			continue
		fi

		#Wait for few seconds prior to mount command
		#This solves possible issues with systemd boot process
		#Allowing usage of glusterfssharedstorage.service from GlusterFS Debian package
		sleep 5
		mount -t glusterfs -o "${arr[3]}" "${arr[0]}" "${arr[1]}"
		#Also wait a few seconds after the mount command
		sleep 5

		#Re-check whether shared storage has been successfully mounted
		if grep -q -P '^(?!systemd).*'"${arr[1]}" /proc/mounts
		then
			echo "${arr[1]} has been mounted"
			continue
		else
			echo "${arr[1]} failed to mount"
			exitStatus=1
		fi
	fi
done <<< "$(sed '/^#/ d' </etc/fstab | grep ' glusterfs ')"
exit $exitStatus
