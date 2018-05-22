#! /bin/bash

volloc="/var/lib/glusterd/thin-arbiter"
mkdir -p $volloc

cp -f extras/thin-arbiter/thin-arbiter.vol $volloc/thin-arbiter.vol
tafile="$volloc/thin-arbiter.vol"

volfile_set_brick_path () {
    while read -r line
    do
        dir=`echo "$line" | cut -d' ' -f 2`
        if [ "$dir" = "directory" ]
        then
            bpath=`echo "$line" | cut -d' ' -f 3`
            sed -i -- 's?'$bpath'?'$1'?g' $tafile
            return
        fi
    done < $tafile
}

tapath="/mnt/thin-arbiter"
echo "Volume file to be used to start thin-arbiter process is :"
echo "$tafile"
echo " "
echo "Default thin-arbiter path is : $tapath"
echo -n "Do you want to change path for thin arbiter volumes. (y/N): "
echo " "
read moveon

if [ "${moveon}" = 'N' ] || [ "${moveon}" = 'n' ]; then
	echo "Default brick path, $tapath, has been set"
    echo "for all thin arbiter volumes using this node"
    echo " "
else
	echo -n "Enter brick path for thin arbiter volumes: "
	read tapath
	echo "Entered brick path : $tapath "
	echo "Please note that this brick path will be used for ALL"
    echo "VOLUMES using this node to host thin-arbiter brick"
    echo " "
fi

mkdir -p $tapath/.glusterfs/indices
volfile_set_brick_path "$tapath"

echo "Directory path to be used for thin-arbiter volume is: $tapath"
echo " "

echo "========================================================"

echo "Installing and starting service for thin-arbiter process"

cp extras/thin-arbiter/gluster-ta-volume.service /etc/systemd/system/

chmod 0777 /etc/systemd/system/gluster-ta-volume.service

systemctl daemon-reload
systemctl enable gluster-ta-volume
systemctl stop gluster-ta-volume
systemctl start gluster-ta-volume

if [ $? == 0 ]
then
    echo "thin-arbiter process is setup and running"
else
    echo "Failed to setup thin arbiter"
fi
