#Troubleshooting GlusterFS

This section describes how to manage GlusterFS logs and most common
troubleshooting scenarios related to GlusterFS.

##Contents
* [Managing GlusterFS Logs](#logs)
* [Troubleshooting Geo-replication](#georep)
* [Troubleshooting POSIX ACLs](#posix-acls)
* [Troubleshooting Hadoop Compatible Storage](#hadoop)
* [Troubleshooting NFS](#nfs)
* [Troubleshooting File Locks](#file-locks)

<a name="logs" />
##Managing GlusterFS Logs

###Rotating Logs

Administrators can rotate the log file in a volume, as needed.

**To rotate a log file**

    `# gluster volume log rotate `

For example, to rotate the log file on test-volume:

    # gluster volume log rotate test-volume
    log rotate successful

> **Note**
> When a log file is rotated, the contents of the current log file
> are moved to log-file- name.epoch-time-stamp.

<a name="georep" />
##Troubleshooting Geo-replication

This section describes the most common troubleshooting scenarios related
to GlusterFS Geo-replication.

###Locating Log Files

For every Geo-replication session, the following three log files are
associated to it (four, if the slave is a gluster volume):

-   **Master-log-file** - log file for the process which monitors the Master
    volume
-   **Slave-log-file** - log file for process which initiates the changes in
    slave
-   **Master-gluster-log-file** - log file for the maintenance mount point
    that Geo-replication module uses to monitor the master volume
-   **Slave-gluster-log-file** - is the slave's counterpart of it

**Master Log File**

To get the Master-log-file for geo-replication, use the following
command:

`gluster volume geo-replication  config log-file`

For example:

`# gluster volume geo-replication Volume1 example.com:/data/remote_dir config log-file `

**Slave Log File**

To get the log file for Geo-replication on slave (glusterd must be
running on slave machine), use the following commands:

1.  On master, run the following command:

    `# gluster volume geo-replication Volume1 example.com:/data/remote_dir config session-owner 5f6e5200-756f-11e0-a1f0-0800200c9a66 `

    Displays the session owner details.

2.  On slave, run the following command:

    `# gluster volume geo-replication /data/remote_dir config log-file /var/log/gluster/${session-owner}:remote-mirror.log `

3.  Replace the session owner details (output of Step 1) to the output
    of the Step 2 to get the location of the log file.

    `/var/log/gluster/5f6e5200-756f-11e0-a1f0-0800200c9a66:remote-mirror.log`

###Rotating Geo-replication Logs

Administrators can rotate the log file of a particular master-slave
session, as needed. When you run geo-replication's ` log-rotate`
command, the log file is backed up with the current timestamp suffixed
to the file name and signal is sent to gsyncd to start logging to a new
log file.

**To rotate a geo-replication log file**

-   Rotate log file for a particular master-slave session using the
    following command:

    `# gluster volume geo-replication  log-rotate`

    For example, to rotate the log file of master `Volume1` and slave
    `example.com:/data/remote_dir` :

        # gluster volume geo-replication Volume1 example.com:/data/remote_dir log rotate
        log rotate successful

-   Rotate log file for all sessions for a master volume using the
    following command:

    `# gluster volume geo-replication  log-rotate`

    For example, to rotate the log file of master `Volume1`:

        # gluster volume geo-replication Volume1 log rotate
        log rotate successful

-   Rotate log file for all sessions using the following command:

    `# gluster volume geo-replication log-rotate`

    For example, to rotate the log file for all sessions:

        # gluster volume geo-replication log rotate
        log rotate successful

###Synchronization is not complete

**Description**: GlusterFS Geo-replication did not synchronize the data
completely but still the geo- replication status displayed is OK.

**Solution**: You can enforce a full sync of the data by erasing the
index and restarting GlusterFS Geo- replication. After restarting,
GlusterFS Geo-replication begins synchronizing all the data. All files
are compared using checksum, which can be a lengthy and high resource
utilization operation on large data sets.


###Issues in Data Synchronization

**Description**: Geo-replication display status as OK, but the files do
not get synced, only directories and symlink gets synced with the
following error message in the log:

    [2011-05-02 13:42:13.467644] E [master:288:regjob] GMaster: failed to
    sync ./some\_file\`

**Solution**: Geo-replication invokes rsync v3.0.0 or higher on the host
and the remote machine. You must verify if you have installed the
required version.

###Geo-replication status displays Faulty very often

**Description**: Geo-replication displays status as faulty very often
with a backtrace similar to the following:

    2011-04-28 14:06:18.378859] E [syncdutils:131:log\_raise\_exception]
    \<top\>: FAIL: Traceback (most recent call last): File
    "/usr/local/libexec/glusterfs/python/syncdaemon/syncdutils.py", line
    152, in twraptf(\*aa) File
    "/usr/local/libexec/glusterfs/python/syncdaemon/repce.py", line 118, in
    listen rid, exc, res = recv(self.inf) File
    "/usr/local/libexec/glusterfs/python/syncdaemon/repce.py", line 42, in
    recv return pickle.load(inf) EOFError

**Solution**: This error indicates that the RPC communication between
the master gsyncd module and slave gsyncd module is broken and this can
happen for various reasons. Check if it satisfies all the following
pre-requisites:

-   Password-less SSH is set up properly between the host and the remote
    machine.
-   If FUSE is installed in the machine, because geo-replication module
    mounts the GlusterFS volume using FUSE to sync data.
-   If the **Slave** is a volume, check if that volume is started.
-   If the Slave is a plain directory, verify if the directory has been
    created already with the required permissions.
-   If GlusterFS 3.2 or higher is not installed in the default location
    (in Master) and has been prefixed to be installed in a custom
    location, configure the `gluster-command` for it to point to the
    exact location.
-   If GlusterFS 3.2 or higher is not installed in the default location
    (in slave) and has been prefixed to be installed in a custom
    location, configure the `remote-gsyncd-command` for it to point to
    the exact place where gsyncd is located.

###Intermediate Master goes to Faulty State

**Description**: In a cascading set-up, the intermediate master goes to
faulty state with the following log:

    raise RuntimeError ("aborting on uuid change from %s to %s" % \\
    RuntimeError: aborting on uuid change from af07e07c-427f-4586-ab9f-
    4bf7d299be81 to de6b5040-8f4e-4575-8831-c4f55bd41154

**Solution**: In a cascading set-up the Intermediate master is loyal to
the original primary master. The above log means that the
geo-replication module has detected change in primary master. If this is
the desired behavior, delete the config option volume-id in the session
initiated from the intermediate master.

<a name="posix-acls" />
##Troubleshooting POSIX ACLs

This section describes the most common troubleshooting issues related to
POSIX ACLs.

    setfacl command fails with “setfacl: \<file or directory name\>: Operation not supported” error

You may face this error when the backend file systems in one of the
servers is not mounted with the "-o acl" option. The same can be
confirmed by viewing the following error message in the log file of the
server "Posix access control list is not supported".

**Solution**: Remount the backend file system with "-o acl" option.

<a name="hadoop" />
##Troubleshooting Hadoop Compatible Storage

###Time Sync

**Problem**: Running MapReduce job may throw exceptions if the time is out-of-sync on
the hosts in the cluster.

**Solution**: Sync the time on all hosts using ntpd program.

<a name="nfs" />
##Troubleshooting NFS

This section describes the most common troubleshooting issues related to
NFS .

###mount command on NFS client fails with “RPC Error: Program not registered”

    Start portmap or rpcbind service on the NFS server.

This error is encountered when the server has not started correctly.
On most Linux distributions this is fixed by starting portmap:

`$ /etc/init.d/portmap start`

On some distributions where portmap has been replaced by rpcbind, the
following command is required:

`$ /etc/init.d/rpcbind start `

After starting portmap or rpcbind, gluster NFS server needs to be
restarted.

###NFS server start-up fails with “Port is already in use” error in the log file.

Another Gluster NFS server is running on the same machine.

This error can arise in case there is already a Gluster NFS server
running on the same machine. This situation can be confirmed from the
log file, if the following error lines exist:

    [2010-05-26 23:40:49] E [rpc-socket.c:126:rpcsvc_socket_listen] rpc-socket: binding socket failed:Address already in use
    [2010-05-26 23:40:49] E [rpc-socket.c:129:rpcsvc_socket_listen] rpc-socket: Port is already in use 
    [2010-05-26 23:40:49] E [rpcsvc.c:2636:rpcsvc_stage_program_register] rpc-service: could not create listening connection 
    [2010-05-26 23:40:49] E [rpcsvc.c:2675:rpcsvc_program_register] rpc-service: stage registration of program failed 
    [2010-05-26 23:40:49] E [rpcsvc.c:2695:rpcsvc_program_register] rpc-service: Program registration failed: MOUNT3, Num: 100005, Ver: 3, Port: 38465 
    [2010-05-26 23:40:49] E [nfs.c:125:nfs_init_versions] nfs: Program init failed 
    [2010-05-26 23:40:49] C [nfs.c:531:notify] nfs: Failed to initialize protocols

To resolve this error one of the Gluster NFS servers will have to be
shutdown. At this time, Gluster NFS server does not support running
multiple NFS servers on the same machine.

###mount command fails with “rpc.statd” related error message

If the mount command fails with the following error message:

    mount.nfs: rpc.statd is not running but is required for remote locking.
    mount.nfs: Either use '-o nolock' to keep locks local, or start statd.

For NFS clients to mount the NFS server, rpc.statd service must be
running on the clients. Start rpc.statd service by running the following command:

`$ rpc.statd `

###mount command takes too long to finish.

**Start rpcbind service on the NFS client**

The problem is that the rpcbind or portmap service is not running on the
NFS client. The resolution for this is to start either of these services
by running the following command:

`$ /etc/init.d/portmap start`

On some distributions where portmap has been replaced by rpcbind, the
following command is required:

`$ /etc/init.d/rpcbind start`

###NFS server glusterfsd starts but initialization fails with “nfsrpc- service: portmap registration of program failed” error message in the log.

NFS start-up can succeed but the initialization of the NFS service can
still fail preventing clients from accessing the mount points. Such a
situation can be confirmed from the following error messages in the log
file:

    [2010-05-26 23:33:47] E [rpcsvc.c:2598:rpcsvc_program_register_portmap] rpc-service: Could notregister with portmap 
    [2010-05-26 23:33:47] E [rpcsvc.c:2682:rpcsvc_program_register] rpc-service: portmap registration of program failed
    [2010-05-26 23:33:47] E [rpcsvc.c:2695:rpcsvc_program_register] rpc-service: Program registration failed: MOUNT3, Num: 100005, Ver: 3, Port: 38465
    [2010-05-26 23:33:47] E [nfs.c:125:nfs_init_versions] nfs: Program init failed
    [2010-05-26 23:33:47] C [nfs.c:531:notify] nfs: Failed to initialize protocols
    [2010-05-26 23:33:49] E [rpcsvc.c:2614:rpcsvc_program_unregister_portmap] rpc-service: Could not unregister with portmap
    [2010-05-26 23:33:49] E [rpcsvc.c:2731:rpcsvc_program_unregister] rpc-service: portmap unregistration of program failed
    [2010-05-26 23:33:49] E [rpcsvc.c:2744:rpcsvc_program_unregister] rpc-service: Program unregistration failed: MOUNT3, Num: 100005, Ver: 3, Port: 38465

1.  **Start portmap or rpcbind service on the NFS server**

    On most Linux distributions, portmap can be started using the
    following command:

    `$ /etc/init.d/portmap start `

    On some distributions where portmap has been replaced by rpcbind,
    run the following command:

    `$ /etc/init.d/rpcbind start `

    After starting portmap or rpcbind, gluster NFS server needs to be
    restarted.

2.  **Stop another NFS server running on the same machine**

    Such an error is also seen when there is another NFS server running
    on the same machine but it is not the Gluster NFS server. On Linux
    systems, this could be the kernel NFS server. Resolution involves
    stopping the other NFS server or not running the Gluster NFS server
    on the machine. Before stopping the kernel NFS server, ensure that
    no critical service depends on access to that NFS server's exports.

    On Linux, kernel NFS servers can be stopped by using either of the
    following commands depending on the distribution in use:

    `$ /etc/init.d/nfs-kernel-server stop`

    `$ /etc/init.d/nfs stop`

3.  **Restart Gluster NFS server**

###mount command fails with NFS server failed error.

mount command fails with following error

    *mount: mount to NFS server '10.1.10.11' failed: timed out (retrying).*

Perform one of the following to resolve this issue:

1.  **Disable name lookup requests from NFS server to a DNS server**

    The NFS server attempts to authenticate NFS clients by performing a
    reverse DNS lookup to match hostnames in the volume file with the
    client IP addresses. There can be a situation where the NFS server
    either is not able to connect to the DNS server or the DNS server is
    taking too long to responsd to DNS request. These delays can result
    in delayed replies from the NFS server to the NFS client resulting
    in the timeout error seen above.

    NFS server provides a work-around that disables DNS requests,
    instead relying only on the client IP addresses for authentication.
    The following option can be added for successful mounting in such
    situations:

    `option rpc-auth.addr.namelookup off `

    > **Note**: Remember that disabling the NFS server forces authentication
    > of clients to use only IP addresses and if the authentication
    > rules in the volume file use hostnames, those authentication rules
    > will fail and disallow mounting for those clients.

    **OR**

2.  **NFS version used by the NFS client is other than version 3**

    Gluster NFS server supports version 3 of NFS protocol. In recent
    Linux kernels, the default NFS version has been changed from 3 to 4.
    It is possible that the client machine is unable to connect to the
    Gluster NFS server because it is using version 4 messages which are
    not understood by Gluster NFS server. The timeout can be resolved by
    forcing the NFS client to use version 3. The **vers** option to
    mount command is used for this purpose:

    `$ mount  -o vers=3 `

###showmount fails with clnt\_create: RPC: Unable to receive

Check your firewall setting to open ports 111 for portmap
requests/replies and Gluster NFS server requests/replies. Gluster NFS
server operates over the following port numbers: 38465, 38466, and
38467.

###Application fails with "Invalid argument" or "Value too large for defined data type" error.

These two errors generally happen for 32-bit nfs clients or applications
that do not support 64-bit inode numbers or large files. Use the
following option from the CLI to make Gluster NFS return 32-bit inode
numbers instead: nfs.enable-ino32 \<on|off\>

Applications that will benefit are those that were either:

-   built 32-bit and run on 32-bit machines such that they do not
    support large files by default
-   built 32-bit on 64-bit systems

This option is disabled by default so NFS returns 64-bit inode numbers
by default.

Applications which can be rebuilt from source are recommended to rebuild
using the following flag with gcc:

` -D_FILE_OFFSET_BITS=64`

<a name="file-locks" />
##Troubleshooting File Locks

In GlusterFS 3.3 you can use `statedump` command to list the locks held
on files. The statedump output also provides information on each lock
with its range, basename, PID of the application holding the lock, and
so on. You can analyze the output to know about the locks whose
owner/application is no longer running or interested in that lock. After
ensuring that the no application is using the file, you can clear the
lock using the following `clear lock` commands.

1.  **Perform statedump on the volume to view the files that are locked
    using the following command:**

    `# gluster volume statedump  inode`

    For example, to display statedump of test-volume:

        # gluster volume statedump test-volume
        Volume statedump successful

    The statedump files are created on the brick servers in the` /tmp`
    directory or in the directory set using `server.statedump-path`
    volume option. The naming convention of the dump file is
    `<brick-path>.<brick-pid>.dump`.

    The following are the sample contents of the statedump file. It
    indicates that GlusterFS has entered into a state where there is an
    entry lock (entrylk) and an inode lock (inodelk). Ensure that those
    are stale locks and no resources own them.

        [xlator.features.locks.vol-locks.inode]
        path=/
        mandatory=0
        entrylk-count=1
        lock-dump.domain.domain=vol-replicate-0
        xlator.feature.locks.lock-dump.domain.entrylk.entrylk[0](ACTIVE)=type=ENTRYLK_WRLCK on basename=file1, pid = 714782904, owner=ffffff2a3c7f0000, transport=0x20e0670, , granted at Mon Feb 27 16:01:01 2012

        conn.2.bound_xl./gfs/brick1.hashsize=14057
        conn.2.bound_xl./gfs/brick1.name=/gfs/brick1/inode
        conn.2.bound_xl./gfs/brick1.lru_limit=16384
        conn.2.bound_xl./gfs/brick1.active_size=2
        conn.2.bound_xl./gfs/brick1.lru_size=0
        conn.2.bound_xl./gfs/brick1.purge_size=0

        [conn.2.bound_xl./gfs/brick1.active.1]
        gfid=538a3d4a-01b0-4d03-9dc9-843cd8704d07
        nlookup=1
        ref=2
        ia_type=1
        [xlator.features.locks.vol-locks.inode]
        path=/file1
        mandatory=0
        inodelk-count=1
        lock-dump.domain.domain=vol-replicate-0
        inodelk.inodelk[0](ACTIVE)=type=WRITE, whence=0, start=0, len=0, pid = 714787072, owner=00ffff2a3c7f0000, transport=0x20e0670, , granted at Mon Feb 27 16:01:01 2012

2.  **Clear the lock using the following command:**

    `# gluster volume clear-locks`

    For example, to clear the entry lock on `file1` of test-volume:

        # gluster volume clear-locks test-volume / kind granted entry file1
        Volume clear-locks successful
        vol-locks: entry blocked locks=0 granted locks=1

3.  **Clear the inode lock using the following command:**

    `# gluster volume clear-locks`

    For example, to clear the inode lock on `file1` of test-volume:

        # gluster  volume clear-locks test-volume /file1 kind granted inode 0,0-0
        Volume clear-locks successful
        vol-locks: inode blocked locks=0 granted locks=1

    You can perform statedump on test-volume again to verify that the
    above inode and entry locks are cleared.


