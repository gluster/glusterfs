#Managing GlusterFS Volumes

This section describes how to perform common GlusterFS management
operations, including the following:

- [Tuning Volume Options](#tuning-options)
- [Expanding Volumes](#expanding-volumes)
- [Shrinking Volumes](#shrinking-volumes)
- [Migrating Volumes](#migrating-volumes)
- [Rebalancing Volumes](#rebalancing-volumes)
- [Stopping Volumes](#stopping-volumes)
- [Deleting Volumes](#deleting-volumes)
- [Triggering Self-Heal on Replicate](#self-heal)

<a name="tuning-options" />
##Tuning Volume Options

You can tune volume options, as needed, while the cluster is online and
available.

> **Note**
>
> It is recommended that you to set server.allow-insecure option to ON if
> there are too many bricks in each volume or if there are too many
> services which have already utilized all the privileged ports in the
> system. Turning this option ON allows ports to accept/reject messages
> from insecure ports. So, use this option only if your deployment
> requires it.

Tune volume options using the following command:

    `# gluster volume set `

For example, to specify the performance cache size for test-volume:

    # gluster volume set test-volume performance.cache-size 256MB
    Set volume successful

The following table lists the Volume options along with its
description and default value:

> **Note**
>
> The default options given here are subject to modification at any
> given time and may not be the same for all versions.


Option | Description | Default Value | Available Options
--- | --- | --- | ---
auth.allow | IP addresses of the clients which should be allowed to access the volume. | \* (allow all) | Valid IP address which includes wild card patterns including \*, such as 192.168.1.\*
auth.reject | IP addresses of the clients which should be denied to access the volume. | NONE (reject none)  | Valid IP address which includes wild card patterns including \*, such as 192.168.2.\*
client.grace-timeout | Specifies the duration for the lock state to be maintained on the client after a network disconnection. | 10 | 10 - 1800 secs
cluster.self-heal-window-size | Specifies the maximum number of blocks per file on which self-heal would happen simultaneously. | 16 | 0 - 1025 blocks 
cluster.data-self-heal-algorithm | Specifies the type of self-heal. If you set the option as "full", the entire file is copied from source to destinations. If the option is set to "diff" the file blocks that are not in sync are copied to destinations. Reset uses a heuristic model. If the file does not exist on one of the subvolumes, or a zero-byte file exists (created by entry self-heal) the entire content has to be copied anyway, so there is no benefit from using the "diff" algorithm. If the file size is about the same as page size, the entire file can be read and written with a few operations, which will be faster than "diff" which has to read checksums and then read and write. | reset | full/diff/reset 
cluster.min-free-disk | Specifies the percentage of disk space that must be kept free. Might be useful for non-uniform bricks | 10% | Percentage of required minimum free disk space 
cluster.stripe-block-size | Specifies the size of the stripe unit that will be read from or written to. | 128 KB (for all files) | size in bytes
cluster.self-heal-daemon | Allows you to turn-off proactive self-heal on replicated | On | On/Off
cluster.ensure-durability | This option makes sure the data/metadata is durable across abrupt shutdown of the brick. | On | On/Off
diagnostics.brick-log-level | Changes the log-level of the bricks. | INFO | DEBUG/WARNING/ERROR/CRITICAL/NONE/TRACE
diagnostics.client-log-level  |  Changes the log-level of the clients. | INFO | DEBUG/WARNING/ERROR/CRITICAL/NONE/TRACE
diagnostics.latency-measurement | Statistics related to the latency of each operation would be tracked. | Off | On/Off 
diagnostics.dump-fd-stats | Statistics related to file-operations would be tracked. | Off | On 
feature.read-only | Enables you to mount the entire volume as read-only for all the clients (including NFS clients) accessing it. | Off | On/Off 
features.lock-heal | Enables self-healing of locks when the network disconnects. | On | On/Off 
features.quota-timeout | For performance reasons, quota caches the directory sizes on client. You can set timeout indicating the maximum duration of directory sizes in cache, from the time they are populated, during which they are considered valid | 0 |  0 - 3600 secs
geo-replication.indexing | Use this option to automatically sync the changes in the filesystem from Master to Slave. | Off | On/Off 
network.frame-timeout | The time frame after which the operation has to be declared as dead, if the server does not respond for a particular operation. | 1800 (30 mins) | 1800 secs
network.ping-timeout | The time duration for which the client waits to check if the server is responsive. When a ping timeout happens, there is a network disconnect between the client and server. All resources held by server on behalf of the client get cleaned up. When a reconnection happens, all resources will need to be re-acquired before the client can resume its operations on the server. Additionally, the locks will be acquired and the lock tables updated. This reconnect is a very expensive operation and should be avoided. | 42 Secs | 42 Secs
nfs.enable-ino32 | For 32-bit nfs clients or applications that do not support 64-bit inode numbers or large files, use this option from the CLI to make Gluster NFS return 32-bit inode numbers instead of 64-bit inode numbers. | Off | On/Off
nfs.volume-access | Set the access type for the specified sub-volume. | read-write |  read-write/read-only
nfs.trusted-write | If there is an UNSTABLE write from the client, STABLE flag will be returned to force the client to not send a COMMIT request. In some environments, combined with a replicated GlusterFS setup, this option can improve write performance. This flag allows users to trust Gluster replication logic to sync data to the disks and recover when required. COMMIT requests if received will be handled in a default manner by fsyncing. STABLE writes are still handled in a sync manner. | Off | On/Off 
nfs.trusted-sync | All writes and COMMIT requests are treated as async. This implies that no write requests are guaranteed to be on server disks when the write reply is received at the NFS client. Trusted sync includes trusted-write behavior. | Off | On/Off
nfs.export-dir | This option can be used to export specified comma separated subdirectories in the volume. The path must be an absolute path. Along with path allowed list of IPs/hostname can be associated with each subdirectory. If provided connection will allowed only from these IPs. Format: \<dir\>[(hostspec[hostspec...])][,...]. Where hostspec can be an IP address, hostname or an IP range in CIDR notation. **Note**: Care must be taken while configuring this option as invalid entries and/or unreachable DNS servers can introduce unwanted delay in all the mount calls. | No sub directory exported. | Absolute path with allowed list of IP/hostname
nfs.export-volumes | Enable/Disable exporting entire volumes, instead if used in conjunction with nfs3.export-dir, can allow setting up only subdirectories as exports. | On | On/Off 
nfs.rpc-auth-unix | Enable/Disable the AUTH\_UNIX authentication type. This option is enabled by default for better interoperability. However, you can disable it if required. | On | On/Off 
nfs.rpc-auth-null | Enable/Disable the AUTH\_NULL authentication type. It is not recommended to change the default value for this option. | On | On/Off 
nfs.rpc-auth-allow\<IP- Addresses\> | Allow a comma separated list of addresses and/or hostnames to connect to the server. By default, all clients are disallowed. This allows you to define a general rule for all exported volumes. | Reject All | IP address or Host name
nfs.rpc-auth-reject\<IP- Addresses\> | Reject a comma separated list of addresses and/or hostnames from connecting to the server. By default, all connections are disallowed. This allows you to define a general rule for all exported volumes. | Reject All | IP address or Host name
nfs.ports-insecure | Allow client connections from unprivileged ports. By default only privileged ports are allowed. This is a global setting in case insecure ports are to be enabled for all exports using a single option. | Off | On/Off 
nfs.addr-namelookup | Turn-off name lookup for incoming client connections using this option. In some setups, the name server can take too long to reply to DNS queries resulting in timeouts of mount requests. Use this option to turn off name lookups during address authentication. Note, turning this off will prevent you from using hostnames in rpc-auth.addr.\* filters. | On | On/Off 
nfs.register-with-portmap | For systems that need to run multiple NFS servers, you need to prevent more than one from registering with portmap service. Use this option to turn off portmap registration for Gluster NFS. | On | On/Off 
nfs.port \<PORT- NUMBER\> | Use this option on systems that need Gluster NFS to be associated with a non-default port number. | NA | 38465- 38467
nfs.disable | Turn-off volume being exported by NFS | Off | On/Off
performance.write-behind-window-size | Size of the per-file write-behind buffer. | 1MB | Write-behind cache size 
performance.io-thread-count | The number of threads in IO threads translator. | 16 | 0-65 
performance.flush-behind | If this option is set ON, instructs write-behind translator to perform flush in background, by returning success (or any errors, if any of previous writes were failed) to application even before flush is sent to backend filesystem. | On | On/Off 
performance.cache-max-file-size | Sets the maximum file size cached by the io-cache translator. Can use the normal size descriptors of KB, MB, GB,TB or PB (for example, 6GB). Maximum size uint64. | 2 \^ 64 -1 bytes | size in bytes
performance.cache-min-file-size | Sets the minimum file size cached by the io-cache translator. Values same as "max" above | 0B | size in bytes
performance.cache-refresh-timeout | The cached data for a file will be retained till 'cache-refresh-timeout' seconds, after which data re-validation is performed. | 1s | 0-61  
performance.cache-size | Size of the read cache. | 32 MB |  size in bytes
server.allow-insecure | Allow client connections from unprivileged ports. By default only privileged ports are allowed. This is a global setting in case insecure ports are to be enabled for all exports using a single option. | On | On/Off 
server.grace-timeout | Specifies the duration for the lock state to be maintained on the server after a network disconnection. | 10 | 10 - 1800 secs 
server.statedump-path | Location of the state dump file. | tmp directory of the brick |  New directory path
storage.health-check-interval | Number of seconds between health-checks done on the filesystem that is used for the brick(s). Defaults to 30 seconds, set to 0 to disable. | tmp directory of the brick |  New directory path

You can view the changed volume options using command:
 
    ` # gluster volume info `

<a name="expanding-volumes" />
##Expanding Volumes

You can expand volumes, as needed, while the cluster is online and
available. For example, you might want to add a brick to a distributed
volume, thereby increasing the distribution and adding to the capacity
of the GlusterFS volume.

Similarly, you might want to add a group of bricks to a distributed
replicated volume, increasing the capacity of the GlusterFS volume.

> **Note**
>
> When expanding distributed replicated and distributed striped volumes,
> you need to add a number of bricks that is a multiple of the replica
> or stripe count. For example, to expand a distributed replicated
> volume with a replica count of 2, you need to add bricks in multiples
> of 2 (such as 4, 6, 8, etc.).

**To expand a volume**

1.  On the first server in the cluster, probe the server to which you
    want to add the new brick using the following command:

    `# gluster peer probe `

    For example:

        # gluster peer probe server4
        Probe successful

2.  Add the brick using the following command:

    `# gluster volume add-brick `

    For example:

        # gluster volume add-brick test-volume server4:/exp4
        Add Brick successful

3.  Check the volume information using the following command:

    `# gluster volume info `

    The command displays information similar to the following:

        Volume Name: test-volume
        Type: Distribute
        Status: Started
        Number of Bricks: 4
        Bricks:
        Brick1: server1:/exp1
        Brick2: server2:/exp2
        Brick3: server3:/exp3
        Brick4: server4:/exp4

4.  Rebalance the volume to ensure that all files are distributed to the
    new brick.

    You can use the rebalance command as described in ?.

<a name="shrinking-volumes" />
##Shrinking Volumes

You can shrink volumes, as needed, while the cluster is online and
available. For example, you might need to remove a brick that has become
inaccessible in a distributed volume due to hardware or network failure.

> **Note**
>
> Data residing on the brick that you are removing will no longer be
> accessible at the Gluster mount point. Note however that only the
> configuration information is removed - you can continue to access the
> data directly from the brick, as necessary.

When shrinking distributed replicated and distributed striped volumes,
you need to remove a number of bricks that is a multiple of the replica
or stripe count. For example, to shrink a distributed striped volume
with a stripe count of 2, you need to remove bricks in multiples of 2
(such as 4, 6, 8, etc.). In addition, the bricks you are trying to
remove must be from the same sub-volume (the same replica or stripe
set).

**To shrink a volume**

1.  Remove the brick using the following command:

    `# gluster volume remove-brick ` `start`

    For example, to remove server2:/exp2:

        # gluster volume remove-brick test-volume server2:/exp2 force

        Removing brick(s) can result in data loss. Do you want to Continue? (y/n)

2.  Enter "y" to confirm the operation. The command displays the
    following message indicating that the remove brick operation is
    successfully started:

        Remove Brick successful 

3.  (Optional) View the status of the remove brick operation using the
    following command:

    `# gluster volume remove-brick `` status`

    For example, to view the status of remove brick operation on
    server2:/exp2 brick:

        # gluster volume remove-brick test-volume server2:/exp2 status
                                        Node  Rebalanced-files  size  scanned       status
                                   ---------  ----------------  ----  -------  -----------
        617c923e-6450-4065-8e33-865e28d9428f               34   340      162   in progress

4.  Check the volume information using the following command:

    `# gluster volume info `

    The command displays information similar to the following:

        # gluster volume info
        Volume Name: test-volume
        Type: Distribute
        Status: Started
        Number of Bricks: 3
        Bricks:
        Brick1: server1:/exp1
        Brick3: server3:/exp3
        Brick4: server4:/exp4

5.  Rebalance the volume to ensure that all files are distributed to the
    new brick.

    You can use the rebalance command as described in ?.

<a name="migrating-volumes" />
##Migrating Volumes

You can migrate the data from one brick to another, as needed, while the
cluster is online and available.

**To migrate a volume**

1.  Make sure the new brick, server5 in this example, is successfully
    added to the cluster.

2.  Migrate the data from one brick to another using the following
    command:

    ` # gluster volume replace-brick  start`

    For example, to migrate the data in server3:/exp3 to server5:/exp5
    in test-volume:

        # gluster volume replace-brick test-volume server3:/exp3  server5:exp5 start
        Replace brick start operation successful

    > **Note**
    >
    > You need to have the FUSE package installed on the server on which
    > you are running the replace-brick command for the command to work.

3.  To pause the migration operation, if needed, use the following
    command:

    `# gluster volume replace-brick  pause `

    For example, to pause the data migration from server3:/exp3 to
    server5:/exp5 in test-volume:

        # gluster volume replace-brick test-volume server3:/exp3 server5:exp5 pause
        Replace brick pause operation successful

4.  To abort the migration operation, if needed, use the following
    command:

    ` # gluster volume replace-brick abort `

    For example, to abort the data migration from server3:/exp3 to
    server5:/exp5 in test-volume:

        # gluster volume replace-brick test-volume server3:/exp3 server5:exp5 abort
        Replace brick abort operation successful

5.  Check the status of the migration operation using the following
    command:

    ` # gluster volume replace-brick status `

    For example, to check the data migration status from server3:/exp3
    to server5:/exp5 in test-volume:

        # gluster volume replace-brick test-volume server3:/exp3 server5:/exp5 status
        Current File = /usr/src/linux-headers-2.6.31-14/block/Makefile 
        Number of files migrated = 10567
        Migration complete

    The status command shows the current file being migrated along with
    the current total number of files migrated. After completion of
    migration, it displays Migration complete.

6.  Commit the migration of data from one brick to another using the
    following command:

    ` # gluster volume replace-brick commit `

    For example, to commit the data migration from server3:/exp3 to
    server5:/exp5 in test-volume:

        # gluster volume replace-brick test-volume server3:/exp3 server5:/exp5 commit
        replace-brick commit successful

7.  Verify the migration of brick by viewing the volume info using the
    following command:

    `# gluster volume info `

    For example, to check the volume information of new brick
    server5:/exp5 in test-volume:

        # gluster volume info test-volume
        Volume Name: testvolume
        Type: Replicate
        Status: Started
        Number of Bricks: 4
        Transport-type: tcp
        Bricks:
        Brick1: server1:/exp1
        Brick2: server2:/exp2
        Brick3: server4:/exp4
        Brick4: server5:/exp5

        The new volume details are displayed.

    The new volume details are displayed.

    In the above example, previously, there were bricks; 1,2,3, and 4
    and now brick 3 is replaced by brick 5.

<a name="rebalancing-volumes" />
##Rebalancing Volumes

After expanding or shrinking a volume (using the add-brick and
remove-brick commands respectively), you need to rebalance the data
among the servers. New directories created after expanding or shrinking
of the volume will be evenly distributed automatically. For all the
existing directories, the distribution can be fixed by rebalancing the
layout and/or data.

This section describes how to rebalance GlusterFS volumes in your
storage environment, using the following common scenarios:

-   **Fix Layout** - Fixes the layout changes so that the files can actually
    go to newly added nodes.

-   **Fix Layout and Migrate Data** - Rebalances volume by fixing the layout
    changes and migrating the existing data.

###Rebalancing Volume to Fix Layout Changes

Fixing the layout is necessary because the layout structure is static
for a given directory. In a scenario where new bricks have been added to
the existing volume, newly created files in existing directories will
still be distributed only among the old bricks. The
`# gluster volume rebalance fix-layout start `command will fix the
layout information so that the files can also go to newly added nodes.
When this command is issued, all the file stat information which is
already cached will get revalidated.

A fix-layout rebalance will only fix the layout changes and does not
migrate data. If you want to migrate the existing data,
use`# gluster volume rebalance  start ` command to rebalance data among
the servers.

**To rebalance a volume to fix layout changes**

-   Start the rebalance operation on any one of the server using the
    following command:

    `# gluster volume rebalance fix-layout start`

    For example:

        # gluster volume rebalance test-volume fix-layout start
        Starting rebalance on volume test-volume has been successful

###Rebalancing Volume to Fix Layout and Migrate Data

After expanding or shrinking a volume (using the add-brick and
remove-brick commands respectively), you need to rebalance the data
among the servers.

**To rebalance a volume to fix layout and migrate the existing data**

-   Start the rebalance operation on any one of the server using the
    following command:

    `# gluster volume rebalance start`

    For example:

        # gluster volume rebalance test-volume start
        Starting rebalancing on volume test-volume has been successful

-   Start the migration operation forcefully on any one of the server
    using the following command:

    `# gluster volume rebalance start force`

    For example:

        # gluster volume rebalance test-volume start force
        Starting rebalancing on volume test-volume has been successful

###Displaying Status of Rebalance Operation

You can display the status information about rebalance volume operation,
as needed.

-   Check the status of the rebalance operation, using the following
    command:

    `# gluster volume rebalance  status`

    For example:

        # gluster volume rebalance test-volume status
                                        Node  Rebalanced-files  size  scanned       status
                                   ---------  ----------------  ----  -------  -----------
        617c923e-6450-4065-8e33-865e28d9428f               416  1463      312  in progress

    The time to complete the rebalance operation depends on the number
    of files on the volume along with the corresponding file sizes.
    Continue checking the rebalance status, verifying that the number of
    files rebalanced or total files scanned keeps increasing.

    For example, running the status command again might display a result
    similar to the following:

        # gluster volume rebalance test-volume status
                                        Node  Rebalanced-files  size  scanned       status
                                   ---------  ----------------  ----  -------  -----------
        617c923e-6450-4065-8e33-865e28d9428f               498  1783      378  in progress

    The rebalance status displays the following when the rebalance is
    complete:

        # gluster volume rebalance test-volume status
                                        Node  Rebalanced-files  size  scanned       status
                                   ---------  ----------------  ----  -------  -----------
        617c923e-6450-4065-8e33-865e28d9428f               502  1873      334   completed

###Stopping Rebalance Operation

You can stop the rebalance operation, as needed.

-   Stop the rebalance operation using the following command:

    `# gluster volume rebalance  stop`

    For example:

        # gluster volume rebalance test-volume stop
                                        Node  Rebalanced-files  size  scanned       status
                                   ---------  ----------------  ----  -------  -----------
        617c923e-6450-4065-8e33-865e28d9428f               59   590      244       stopped
        Stopped rebalance process on volume test-volume 

<a name="stopping-volumes" />
##Stopping Volumes

1.  Stop the volume using the following command:

    `# gluster volume stop `

    For example, to stop test-volume:

        # gluster volume stop test-volume
        Stopping volume will make its data inaccessible. Do you want to continue? (y/n)

2.  Enter `y` to confirm the operation. The output of the command
    displays the following:

        Stopping volume test-volume has been successful

<a name="" />
##Deleting Volumes

1.  Delete the volume using the following command:

    `# gluster volume delete `

    For example, to delete test-volume:

        # gluster volume delete test-volume
        Deleting volume will erase all information about the volume. Do you want to continue? (y/n)

2.  Enter `y` to confirm the operation. The command displays the
    following:

        Deleting volume test-volume has been successful

<a name="self-heal" />
##Triggering Self-Heal on Replicate

In replicate module, previously you had to manually trigger a self-heal
when a brick goes offline and comes back online, to bring all the
replicas in sync. Now the pro-active self-heal daemon runs in the
background, diagnoses issues and automatically initiates self-healing
every 10 minutes on the files which requires*healing*.

You can view the list of files that need *healing*, the list of files
which are currently/previously *healed*, list of files which are in
split-brain state, and you can manually trigger self-heal on the entire
volume or only on the files which need *healing*.

-   Trigger self-heal only on the files which requires *healing*:

    `# gluster volume heal `

    For example, to trigger self-heal on files which requires *healing*
    of test-volume:

        # gluster volume heal test-volume
        Heal operation on volume test-volume has been successful

-   Trigger self-heal on all the files of a volume:

    `# gluster volume heal ` `full`

    For example, to trigger self-heal on all the files of of
    test-volume:

        # gluster volume heal test-volume full
        Heal operation on volume test-volume has been successful

-   View the list of files that needs *healing*:

    `# gluster volume heal ` `info`

    For example, to view the list of files on test-volume that needs
    *healing*:

        # gluster volume heal test-volume info
        Brick :/gfs/test-volume_0
        Number of entries: 0
         
        Brick :/gfs/test-volume_1
        Number of entries: 101 
        /95.txt
        /32.txt
        /66.txt
        /35.txt
        /18.txt
        /26.txt
        /47.txt
        /55.txt
        /85.txt
        ...

-   View the list of files that are self-healed:

    `# gluster volume heal ` `info healed`

    For example, to view the list of files on test-volume that are
    self-healed:

        # gluster volume heal test-volume info healed
        Brick :/gfs/test-volume_0 
        Number of entries: 0

        Brick :/gfs/test-volume_1 
        Number of entries: 69
        /99.txt
        /93.txt
        /76.txt
        /11.txt
        /27.txt
        /64.txt
        /80.txt
        /19.txt
        /41.txt
        /29.txt
        /37.txt
        /46.txt
        ...

-   View the list of files of a particular volume on which the self-heal
    failed:

    `# gluster volume heal ` `info failed`

    For example, to view the list of files of test-volume that are not
    self-healed:

        # gluster volume heal test-volume info failed
        Brick :/gfs/test-volume_0
        Number of entries: 0 

        Brick server2:/gfs/test-volume_3 
        Number of entries: 72
        /90.txt
        /95.txt
        /77.txt
        /71.txt
        /87.txt
        /24.txt
        ...

-   View the list of files of a particular volume which are in
    split-brain state:

    `# gluster volume heal ` `info split-brain`

    For example, to view the list of files of test-volume which are in
    split-brain state:

        # gluster volume heal test-volume info split-brain
        Brick server1:/gfs/test-volume_2 
        Number of entries: 12
        /83.txt
        /28.txt
        /69.txt
        ...

        Brick :/gfs/test-volume_2
        Number of entries: 12
        /83.txt
        /28.txt
        /69.txt
        ...


