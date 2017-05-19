#!/bin/bash
#Post reboot there is a chance in which mounting of shared storage will fail
#This will impact starting of features like NFS-Ganesha. So this script will
#try to mount the shared storage if it fails
#TODO : Do it for other glusterfs clients in /etc/fstab

ms="var-run-gluster-shared_storage.mount"
volume="gluster_shared_storage"
failed=$(systemctl --failed | grep -c $ms)
if [ $failed -eq 1 ]
then
        if systemctl restart $ms
        then
                #Restart worked just wait for sometime to make it reflect
                sleep 5
        else
                #Restart failed, no point in further continuing
                exit 1
        fi
fi

# If we've reached this point, there wasn't a failed mountpoint
# BUT we need to check for whether this haven't been called before the attempts
# to the filesystem mounts, thus we need to check whether there is a glusterfs
# in fstab and aren't mountedmount

#In the logs I've seen ~4-5secs between the initial mount/start and the unmount

gfc=$(sed -e 's/#.$//' </etc/fstab | grep -c $volume)
gfm=$(grep -i $volume /proc/mounts | wc -l)

if [ $gfm -lt $gfc ]
then
        exit 1
fi

exit 0
