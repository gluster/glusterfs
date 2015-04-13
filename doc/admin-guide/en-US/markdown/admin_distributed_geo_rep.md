# Distributed Geo-Replication in glusterfs-3.5

This is a admin how-to guide for new dustributed-geo-replication being released as part of glusterfs-3.5

##### Note:
This article is targeted towards users/admins who want to try new geo-replication, without going much deeper into internals and technology used.

### How is it different from earlier geo-replication?

- Up until now, in geo-replication, only one of the nodes in master volume would participate in geo-replication. This meant that all the data syncing is taken care by only one node while other nodes in the cluster would sit idle (not participate in data syncing). With distributed-geo-replication, each node of the master volume takes the repsonsibility of syncing the data present in that node. In case of replicate configuration, one of them would 'Active'ly sync the data while other node of the replica pair would be 'Passive'. The 'Passive' node only becomes 'Active' when the 'Active' pair goes down. This way new geo-rep leverages all the nodes in the volume and remove the bottleneck of syncing from one single node. 
- New change detection mechanism is the other thing which has been improved with new geo-rep. So far geo-rep used to crawl through glusterfs file system to figure out the files that need to synced. And because crawling filesystem can be an expensive operation, this used to be a major bottleneck for performance. With distributed geo-rep, all the files that need to be synced are identified through changelog xlator. Changelog xlator journals all the fops that modifes the file and these journals are then consumed by geo-rep to effectively identify the files that need to be synced.
- A new syncing method tar+ssh, has been introduced to improve the performance of few specific data sets. You can switch between rsync and tar+ssh syncing method via CLI to suite your data set needs. This tar+ssh is better suited for data sets which have large number of small files.


### Using Distributed geo-replication:

#### Prerequisites:
- There should be a password-less ssh setup between at least one node in master volume to one node in slave volume. The geo-rep create command should be executed from this node which has password-less ssh setup to slave.

- Unlike previous version, slave **must** be a gluster volume. Slave can not be a directory. And both the master and slave volumes should have been created and started before creating geo-rep session.

#### Creating secret pem pub file
- Execute the below command from the node where you setup the password-less ssh to slave. This will create the secret pem pub file which would have information of RSA key of all the nodes in the master volume. And when geo-rep create command is executed, glusterd uses this file to establish a geo-rep specific ssh connections
```sh
gluster system:: execute gsec_create
```

#### Creating geo-replication session.
Create a geo-rep session between master and slave volume using the following command. The node in which this command is executed and the <slave_host> specified in the command should have password less ssh setup between them. The push-pem option actually uses the secret pem pub file created earlier and establishes geo-rep specific password less ssh between each node in master to each node of slave.
```sh
gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> create push-pem [force]
```

If the total available size in slave volume is less than the total size of master, the command will throw error message. In such cases 'force' option can be used.

In use cases where the rsa-keys of nodes in master volume is distributed to slave nodes through an external agent and slave side verifications like:
- if ssh port 22 is open in slave
- has proper passwordless ssh login setup
- slave volume is created and is empty
- if slave has enough memory
is taken care by the external agent, the following command can be used to create geo-replication:
```sh
gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> create no-verify [force]
```
In this case the master node rsa-key distribution to slave node does not happen and above mentioned slave verification is not performed and these two things has to be taken care externaly.

### Creating Non Root Geo-replication session

`mountbroker` is a new service of glusterd. This service allows an
unprivileged process to own a GlusterFS mount by registering a label
(and DSL (Domain-specific language) options ) with glusterd through a
glusterd volfile. Using CLI, you can send a mount request to glusterd to
receive an alias (symlink) of the mounted volume.

A request from the agent, the unprivileged slave agents use the
mountbroker service of glusterd to set up an auxiliary gluster mount for
the agent in a special environment which ensures that the agent is only
allowed to access with special parameters that provide administrative
level access to the particular volume.

**To setup an auxiliary gluster mount for the agent**:

1.  In all Slave nodes, Create a new group. For example, `geogroup`

2.  In all Slave nodes, Create a unprivileged account. For example, ` geoaccount`. Make it a member of ` geogroup`

3.  In all Slave nodes, Create a new directory owned by root and with permissions *0711.* For example, create mountbroker-root directory `/var/mountbroker-root`

4.  In any one of Slave node, Run the following commands to add options to glusterd vol file(`/etc/glusterfs/glusterd.vol`
    in rpm installations and `/usr/local/etc/glusterfs/glusterd.vol` in Source installation.

    gluster system:: execute mountbroker opt mountbroker-root /var/mountbroker-root
    gluster system:: execute mountbroker opt geo-replication-log-group geogroup
    gluster system:: execute mountbroker opt rpc-auth-allow-insecure on

5.  In any one of Slave node, Add Mountbroker user to glusterd vol file using,

    ```sh
    gluster system:: execute mountbroker user geoaccount slavevol
    ```

Where `slavevol` is Slave Volume name.

If you host multiple slave volumes on Slave, for each of them and add the following options to the volfile using

    ```sh
    gluster system:: execute mountbroker user geoaccount2 slavevol2
    gluster system:: execute mountbroker user geoaccount3 slavevol3
    ```

To add multiple volumes per mountbroker user,

    ```sh
    gluster system:: execute mountbroker user geoaccount1 slavevol11,slavevol12,slavevol13
    gluster system:: execute mountbroker user geoaccount2 slavevol21,slavevol22
    gluster system:: execute mountbroker user geoaccount3 slavevol31
    ```

6. Restart `glusterd` service on all the Slave nodes

7. Setup a passwdless SSH from one of the master node to the user on one of the slave node. For example, to geoaccount.

8. Create a geo-replication relationship between master and slave to the user by running the following command on the master node:
   For example,

    ```sh
    gluster volume geo-replication <master_volume> <mountbroker_user>@<slave_host>::<slave_volume> create push-pem [force]
    ```

9. In the slavenode, which is used to create relationship, run `/usr/libexec/glusterfs/set_geo_rep_pem_keys.sh` as a root with user name, master volume name, and slave volume names as the arguments.

    ```sh
    /usr/libexec/glusterfs/set_geo_rep_pem_keys.sh <mountbroker_user> <master_volume> <slave_volume>
    ```

### Create and mount meta volume
NOTE:
___
If shared meta volume is already created and mounted at '/var/run/gluster/shared_storage'
as part of nfs or snapshot, please jump into section 'Configure meta volume with goe-replication'.
___

A 3-way replicated common gluster meta-volume should be configured and is shared
by nfs, snapshot and geo-replication. The name of the meta-volume should be
'gluster_shared_storage' and should be mounted at '/var/run/gluster/shared_storage/'.

The meta volume needs to be configured with geo-replication to better handle
rename and other consistency issues in geo-replication during brick/node down
scenarios when master volume is configured with EC(Erasure Code)/AFR.
Following are the steps to configure meta volume

Create a 3 way replicated meta volume in the master cluster with all three bricks from different nodes as follows.

    ```sh
    gluster volume create gluster_shared_storage replica 3 <host1>:<brick_path> <host2>:<brick_path> <host3>:<brick_path>
    ```

Start the meta volume as follows.

    ```sh
    gluster volume start <meta_vol>
    ```

Mount the meta volume as follows in all the master nodes.
    ```sh
    mount -t glusterfs <master_host>:gluster_shared_storage /var/run/gluster/shared_storage
    ```

###Configure meta volume with geo-replication session as follows.

    ```sh
    gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> config use_meta_volume true
    # If Mountbroker Setup,
    gluster volume geo-replication <master_volume> <mountbroker_user>@<slave_host>::<slave_volume> config use_meta_volume true
    ```

#### Starting a geo-rep session
There is no change in this command from previous versions to this version.

    ```sh
    gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> start
    # If Mountbroker Setup,
    gluster volume geo-replication <master_volume> <mountbroker_user>@<slave_host>::<slave_volume> start
    ```

This command actually starts the session. Meaning the gsyncd monitor process will be started, which in turn spawns gsync worker processes whenever required. This also turns on changelog xlator (if not in ON state already), which starts recording all the changes on each of the glusterfs bricks. And if master is empty during geo-rep start, the change detection mechanism will be changelog. Else it’ll be xsync (the changes are identified by crawling through filesystem). Later when the initial data is syned to slave, change detection mechanism will be set to changelog

#### Status of geo-replication

gluster now has variants of status command.

    ```sh
    gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> status
    # If Mountbroker Setup,
    gluster volume geo-replication <master_volume> <mountbroker_user>@<slave_host>::<slave_volume> status
    ```

This displays the status of session from each brick of the master to each brick of the slave node.

If you want more detailed status, then run 'status detail'

    ```sh
    gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> status detail
    # If Mountbroker Setup,
    gluster volume geo-replication <master_volume> <mountbroker_user>@<slave_host>::<slave_volume> status detail
    ```

This command displays extra information like, total files synced, files that needs to be synced, deletes pending etc.

#### Stopping geo-replication session

This command stops all geo-rep relates processes i.e. gsyncd monitor and works processes. Note that changelog will **not** be turned off with this command.

    ```sh
    gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> stop [force]
    # If Mountbroker Setup,
    gluster volume geo-replication <master_volume> <mountbroker_user>@<slave_host>::<slave_volume> stop [force]
    ```

Force option is to be used, when one of the node (or glusterd in one of the node) is down. Once stopped, the session can be restarted any time. Note that upon restarting of the session, the change detection mechanism falls back to xsync mode. This happens even though you have changelog generating journals, while the geo-rep session is stopped.

#### Deleting geo-replication session

Now you can delete the glusterfs geo-rep session. This will delete all the config data associated with the geo-rep session.

    ```sh
    gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> delete
    # If Mountbroker Setup,
    gluster volume geo-replication <master_volume> <mountbroker_user>@<slave_host>::<slave_volume> delete
    ```

This deletes all the gsync conf files in each of the nodes. This returns failure, if any of the node is down. And unlike geo-rep stop, there is 'force' option with this.

#### Changing the config values

There are some configuration values which can be changed using the CLI. And you can see all the current config values with following command.

    ```sh
    gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> config
    # If Mountbroker Setup,
    gluster volume geo-replication <master_volume> <mountbroker_user>@<slave_host>::<slave_volume> config
    ```

But you can check only one of them, like log_file or change-detector

    ```sh
    gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> config log-file
    # If Mountbroker Setup,
    gluster volume geo-replication <master_volume> <mountbroker_user>@<slave_host>::<slave_volume> config log-file

    gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> config change-detector
    # If Mountbroker Setup,
    gluster volume geo-replication <master_volume> <mountbroker_user>@<slave_host>::<slave_volume> config change-detector

    gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> config working-dir
    # If Mountbroker Setup,
    gluster volume geo-replication <master_volume> <mountbroker_user>@<slave_host>::<slave_volume> config working-dir
    ```

To set a new value to this, just provide a new value. Note that, not all the config values are allowed to change. Some can not be modified.

    ```sh
    gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> config change-detector xsync
    # If Mountbroker Setup,
    gluster volume geo-replication <master_volume> <mountbroker_user>@<slave_host>::<slave_volume> config change-detector xsync
    ```

Make sure you provide the proper value to the config value. And if you have large number of small files data set, then you can use tar+ssh as syncing method. Note that, if geo-rep session is running, this restarts the gsyncd.

    ```sh
    gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> config use-tarssh true
    # If Mountbroker Setup,
    gluster volume geo-replication <master_volume> <mountbroker_user>@<slave_host>::<slave_volume> config use-tarssh true
    ```

Resetting these value to default is also simple.

    ```sh
    gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> config \!use-tarssh
    # If Mountbroker Setup,
    gluster volume geo-replication <master_volume> <mountbroker_user>@<slave_host>::<slave_volume> config \!use-tarssh
    ```

That makes the config key (tar-ssh in this case) to fall back to it's default value.
