#Steps to upgrade from previous verison of geo-replication to new distributed-geo-replication
-------------------------------------------------------------------------------
Here are the steps to upgrade your existing geo-replication setup to new distributed geo-replication in glusterfs-3.5. The new version leverges all the nodes in your master volume and provides better performace.

#### Note:
 - Since new version of geo-rep very much different from the older one, this has to be done offline.
 - New version supports only syncing between two gluster volumes via ssh+gluster.
 - This doc deals with upgrading geo-rep. So upgrading the volumes are not covered in detail here.

### Below are the steps to upgrade.
---------------------------------------

- Stop the geo-replication session in older version ( < 3.5) using the below command
```sh
gluster volume geo-replication <master_vol> <slave_host>::<slave_vol> stop
```
- Now upgrade the master and slave volumes separately. The steps to upgrade volumes is pretty simple. You should unmount the volumes from client, stop the volume and glusterd. Upgrade to new version using yum upgrade or just installing new rpms, then restart glusterd and start the gluster volume. And if you are using quota feature, please follow the steps provided there.

- Now since the new geo-replication requires gfids of master and slave volume to be same, generate a file containing the gfids of all the files in master

```sh
cd /usr/share/glusterfs/scripts/ ;
bash generate-gfid-file.sh localhost:<master_vol> $PWD/get-gfid.sh /tmp/master_gfid_file.txt ;
scp /tmp/master_gfid_file.txt root@<slave_host>:/tmp
```
- Now go to the slave host and aplly the gfid to the slave volume.

```sh
cd /usr/share/glusterfs/scripts/
bash slave-upgrade.sh localhost:<slave_vol> /tmp/master_gfid_file.txt $PWD/gsync-sync-gfid
```
This will ask you for password of all the nodes in slave cluster. Please provide them, if asked.
- Also note that this will restart your slave gluster volume (stop and start)

- Now create and start the geo-rep session between master and slave. For instruction on creating new geo-rep seesion please refer distributed-geo-rep admin guide.

```sh
gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> create push-pem force
gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> start
```

- Now your session is upgraded to use distributed-geo-rep
