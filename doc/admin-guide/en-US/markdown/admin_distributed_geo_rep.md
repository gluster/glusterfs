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
- Execute the below command from the node where you setup the password-less ssh to slave. This will create the secret pem pub file which would have information of RSA key of all the nodes in the master volume. And when geo-rep create command is executed, glusterd uses this file to establish a geo-rep specific ssh connections.
```sh
gluster system:: execute gsec_create
```

#### Creating geo-replication session.
Create a geo-rep session between master and slave volume using the following command. The node in which this command is executed and the <slave_host> specified in the command should have password less ssh setup between them. The push-pem option actually uses the secret pem pub file created earlier and establishes geo-rep specific password less ssh between each node in master to each node of slave.
```sh
gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> create push-pem [force]
```

If the total available size in slave volume is more than the total size of master, the command will throw error message. In such cases 'force' option can be used.

#### Starting a geo-rep session
There is no change in this command from previous versions to this version.
```sh
gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> start
```
This command actually starts the session. Meaning the gsyncd monitor process will be started, which in turn spawns gsync worker processes whenever required. This also turns on changelog xlator (if not in ON state already), which starts recording all the changes on each of the glusterfs bricks. And if master is empty during geo-rep start, the change detection mechanism will be changelog. Else it’ll be xsync (the changes are identified by crawling through filesystem). Later when the initial data is syned to slave, change detection mechanism will be set to changelog

#### Status of geo-replication

gluster now has variants of status command.

```sh
gluster volume geo-replication <master_volume> <slave_volume>::<slave_volume> status
```

This displays the status of session from each brick of the master to each brick of the slave node.

If you want more detailed status, then run 'status detail'

```sh
gluster volume geo-replication <master_volume> <slave_volume>::<slave_volume> status detail
```

This command displays extra information like, total files synced, files that needs to be synced, deletes pending etc.

#### Stopping geo-replication session

This command stops all geo-rep relates processes i.e. gsyncd monitor and works processes. Note that changelog will **not** be turned off with this command.

```sh
gluster volume geo-replication <master_volume> <slave_volume>::<slave_volume> stop [force]
```
Force option is to be used, when one of the node (or glusterd in one of the node) is down. Once stopped, the session can be restarted any time. Note that upon restarting of the session, the change detection mechanism falls back to xsync mode. This happens even though you have changelog generating journals, while the geo-rep session is stopped.

#### Deleting geo-replication session

Now you can delete the glusterfs geo-rep session. This will delete all the config data associated with the geo-rep session.

```sh
gluster volume geo-replication <master_volume> <slave_volume>::<slave_volume> delete
```

This deletes all the gsync conf files in each of the nodes. This returns failure, if any of the node is down. And unlike geo-rep stop, there is 'force' option with this.

#### Changing the config values

There are some configuration values which can be changed using the CLI. And you can see all the current config values with following command.

```sh
gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> config
```

But you can check only one of them, like log_file or change-detector

```sh
gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> config log-file
```
```sh
gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> config change-detector
```
```sh
gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> config working-dir
```
To set a new value to this, just provide a new value. Note that, not all the config values are allowed to change. Some can not be modified.

```sh
gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> config change-detector xsync
```
Make sure you provide the proper value to the config value. And if you have large number of small files data set, then you can use tar+ssh as syncing method. Note that, if geo-rep session is running, this restarts the gsyncd.

```sh
gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> config use-tarssh true
```
Resetting these value to default is also simple.

```sh
gluster volume geo-replication <master_volume> <slave_host>::<slave_volume> config \!use-tarssh
```
That makes the config key (tar-ssh in this case) to fall back to it’s default value.
