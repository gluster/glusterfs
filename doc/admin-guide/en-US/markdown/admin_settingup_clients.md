#Accessing Data - Setting Up GlusterFS Client

You can access gluster volumes in multiple ways. You can use Gluster
Native Client method for high concurrency, performance and transparent
failover in GNU/Linux clients. You can also use NFS v3 to access gluster
volumes. Extensive testing has be done on GNU/Linux clients and NFS
implementation in other operating system, such as FreeBSD, and Mac OS X,
as well as Windows 7 (Professional and Up) and Windows Server 2003.
Other NFS client implementations may work with gluster NFS server.

You can use CIFS to access volumes when using Microsoft Windows as well
as SAMBA clients. For this access method, Samba packages need to be
present on the client side.

##Gluster Native Client

The Gluster Native Client is a FUSE-based client running in user space.
Gluster Native Client is the recommended method for accessing volumes
when high concurrency and high write performance is required.

This section introduces the Gluster Native Client and explains how to
install the software on client machines. This section also describes how
to mount volumes on clients (both manually and automatically) and how to
verify that the volume has mounted successfully.

###Installing the Gluster Native Client

Before you begin installing the Gluster Native Client, you need to
verify that the FUSE module is loaded on the client and has access to
the required modules as follows:

1.  Add the FUSE loadable kernel module (LKM) to the Linux kernel:

    `# modprobe fuse`

2.  Verify that the FUSE module is loaded:

    `# dmesg | grep -i fuse `
    `fuse init (API version 7.13)`

### Installing on Red Hat Package Manager (RPM) Distributions

To install Gluster Native Client on RPM distribution-based systems

1.  Install required prerequisites on the client using the following
    command:

    `$ sudo yum -y install openssh-server wget fuse fuse-libs openib libibverbs`

2.  Ensure that TCP and UDP ports 24007 and 24008 are open on all
    Gluster servers. Apart from these ports, you need to open one port
    for each brick starting from port 49152 (instead of 24009 onwards as
    with previous releases). The brick ports assignment scheme is now
    compliant with IANA guidelines. For example: if you have
    five bricks, you need to have ports 49152 to 49156 open.

    You can use the following chains with iptables:

    `$ sudo iptables -A RH-Firewall-1-INPUT -m state --state NEW -m tcp -p tcp --dport 24007:24008 -j ACCEPT `
    `$ sudo iptables -A RH-Firewall-1-INPUT -m state --state NEW -m tcp -p tcp --dport 49152:49156 -j ACCEPT`

    > **Note**
    >
    > If you already have iptable chains, make sure that the above
    > ACCEPT rules precede the DROP rules. This can be achieved by
    > providing a lower rule number than the DROP rule.

3.  Download the latest glusterfs, glusterfs-fuse, and glusterfs-rdma
    RPM files to each client. The glusterfs package contains the Gluster
    Native Client. The glusterfs-fuse package contains the FUSE
    translator required for mounting on client systems and the
    glusterfs-rdma packages contain OpenFabrics verbs RDMA module for
    Infiniband.

    You can download the software at [GlusterFS download page][1].

4.  Install Gluster Native Client on the client.

    `$ sudo rpm -i glusterfs-3.3.0qa30-1.x86_64.rpm `
    `$ sudo rpm -i glusterfs-fuse-3.3.0qa30-1.x86_64.rpm `
    `$ sudo rpm -i glusterfs-rdma-3.3.0qa30-1.x86_64.rpm`

    > **Note**
    >
    > The RDMA module is only required when using Infiniband.

### Installing on Debian-based Distributions

To install Gluster Native Client on Debian-based distributions

1.  Install OpenSSH Server on each client using the following command:

    `$ sudo apt-get install openssh-server vim wget`

2.  Download the latest GlusterFS .deb file and checksum to each client.

    You can download the software at [GlusterFS download page][1].

3.  For each .deb file, get the checksum (using the following command)
    and compare it against the checksum for that file in the md5sum
    file.

    `$ md5sum GlusterFS_DEB_file.deb `

    The md5sum of the packages is available at: [GlusterFS download page][2]

4.  Uninstall GlusterFS v3.1 (or an earlier version) from the client
    using the following command:

    `$ sudo dpkg -r glusterfs `

    (Optional) Run `$ sudo dpkg -purge glusterfs `to purge the
    configuration files.

5.  Install Gluster Native Client on the client using the following
    command:

    `$ sudo dpkg -i GlusterFS_DEB_file `

    For example:

    `$ sudo dpkg -i glusterfs-3.3.x.deb `

6.  Ensure that TCP and UDP ports 24007 and 24008 are open on all
    Gluster servers. Apart from these ports, you need to open one port
    for each brick starting from port 49152 (instead of 24009 onwards as
    with previous releases). The brick ports assignment scheme is now
    compliant with IANA guidelines. For example: if you have
    five bricks, you need to have ports 49152 to 49156 open.

    You can use the following chains with iptables:

    `$ sudo iptables -A RH-Firewall-1-INPUT -m state --state NEW -m tcp -p tcp --dport 24007:24008 -j ACCEPT `
    `$ sudo iptables -A RH-Firewall-1-INPUT -m state --state NEW -m tcp -p tcp --dport 49152:49156 -j ACCEPT`

    > **Note**
    >
    > If you already have iptable chains, make sure that the above
    > ACCEPT rules precede the DROP rules. This can be achieved by
    > providing a lower rule number than the DROP rule.

### Performing a Source Installation

To build and install Gluster Native Client from the source code

1.  Create a new directory using the following commands:

    `# mkdir glusterfs `
    `# cd glusterfs`

2.  Download the source code.

    You can download the source at [][1].

3.  Extract the source code using the following command:

    `# tar -xvzf SOURCE-FILE `

4.  Run the configuration utility using the following command:

    `# ./configure `

        GlusterFS configure summary
        ===========================
        FUSE client : yes
        Infiniband verbs : yes
        epoll IO multiplex : yes
        argp-standalone : no
        fusermount : no
        readline : yes

    The configuration summary shows the components that will be built
    with Gluster Native Client.

5.  Build the Gluster Native Client software using the following
    commands:

    `# make `
    `# make install`

6.  Verify that the correct version of Gluster Native Client is
    installed, using the following command:

    `# glusterfs –-version`

##Mounting Volumes

After installing the Gluster Native Client, you need to mount Gluster
volumes to access data. There are two methods you can choose:

-   [Manually Mounting Volumes](#manual-mount)
-   [Automatically Mounting Volumes](#auto-mount)

> **Note**
>
> Server names selected during creation of Volumes should be resolvable
> in the client machine. You can use appropriate /etc/hosts entries or
> DNS server to resolve server names to IP addresses.

<a name="manual-mount" />
### Manually Mounting Volumes

-   To mount a volume, use the following command:

    `# mount -t glusterfs HOSTNAME-OR-IPADDRESS:/VOLNAME MOUNTDIR`

    For example:

    `# mount -t glusterfs server1:/test-volume /mnt/glusterfs`

    > **Note**
    >
    > The server specified in the mount command is only used to fetch
    > the gluster configuration volfile describing the volume name.
    > Subsequently, the client will communicate directly with the
    > servers mentioned in the volfile (which might not even include the
    > one used for mount).
    >
    > If you see a usage message like "Usage: mount.glusterfs", mount
    > usually requires you to create a directory to be used as the mount
    > point. Run "mkdir /mnt/glusterfs" before you attempt to run the
    > mount command listed above.

**Mounting Options**

You can specify the following options when using the
`mount -t glusterfs` command. Note that you need to separate all options
with commas.

backupvolfile-server=server-name

volfile-max-fetch-attempts=number of attempts

log-level=loglevel

log-file=logfile

transport=transport-type

direct-io-mode=[enable|disable]

use-readdirp=[yes|no]

For example:

`# mount -t glusterfs -o backupvolfile-server=volfile_server2,use-readdirp=no,volfile-max-fetch-attempts=2,log-level=WARNING,log-file=/var/log/gluster.log server1:/test-volume /mnt/glusterfs`

If `backupvolfile-server` option is added while mounting fuse client,
when the first volfile server fails, then the server specified in
`backupvolfile-server` option is used as volfile server to mount the
client.

In `volfile-max-fetch-attempts=X` option, specify the number of
attempts to fetch volume files while mounting a volume. This option is
useful when you mount a server with multiple IP addresses or when
round-robin DNS is configured for the server-name..

If `use-readdirp` is set to ON, it forces the use of readdirp
mode in fuse kernel module

<a name="auto-mount" />
### Automatically Mounting Volumes

You can configure your system to automatically mount the Gluster volume
each time your system starts.

The server specified in the mount command is only used to fetch the
gluster configuration volfile describing the volume name. Subsequently,
the client will communicate directly with the servers mentioned in the
volfile (which might not even include the one used for mount).

-   To mount a volume, edit the /etc/fstab file and add the following
    line:

    `HOSTNAME-OR-IPADDRESS:/VOLNAME MOUNTDIR glusterfs defaults,_netdev 0 0 `

    For example:

    `server1:/test-volume /mnt/glusterfs glusterfs defaults,_netdev 0 0`

**Mounting Options**

You can specify the following options when updating the /etc/fstab file.
Note that you need to separate all options with commas.

log-level=loglevel

log-file=logfile

transport=transport-type

direct-io-mode=[enable|disable]

use-readdirp=no

For example:

`HOSTNAME-OR-IPADDRESS:/VOLNAME MOUNTDIR glusterfs defaults,_netdev,log-level=WARNING,log-file=/var/log/gluster.log 0 0 `

### Testing Mounted Volumes

To test mounted volumes

-   Use the following command:

    `# mount `

    If the gluster volume was successfully mounted, the output of the
    mount command on the client will be similar to this example:

    `server1:/test-volume on /mnt/glusterfs type fuse.glusterfs (rw,allow_other,default_permissions,max_read=131072`

-   Use the following command:

    `# df`

    The output of df command on the client will display the aggregated
    storage space from all the bricks in a volume similar to this
    example:

    `# df -h /mnt/glusterfs Filesystem Size Used Avail Use% Mounted on server1:/test-volume 28T 22T 5.4T 82% /mnt/glusterfs`

-   Change to the directory and list the contents by entering the
    following:

    `# cd MOUNTDIR `
    `# ls`

-   For example,

    `# cd /mnt/glusterfs `
    `# ls`

#NFS

You can use NFS v3 to access to gluster volumes. Extensive testing has
be done on GNU/Linux clients and NFS implementation in other operating
system, such as FreeBSD, and Mac OS X, as well as Windows 7
(Professional and Up), Windows Server 2003, and others, may work with
gluster NFS server implementation.

GlusterFS now includes network lock manager (NLM) v4. NLM enables
applications on NFSv3 clients to do record locking on files on NFS
server. It is started automatically whenever the NFS server is run.

You must install nfs-common package on both servers and clients (only
for Debian-based) distribution.

This section describes how to use NFS to mount Gluster volumes (both
manually and automatically) and how to verify that the volume has been
mounted successfully.

##Using NFS to Mount Volumes
--------------------------

You can use either of the following methods to mount Gluster volumes:

-   [Manually Mounting Volumes Using NFS](#manual-nfs)
-   [Automatically Mounting Volumes Using NFS](#auto-nfs)

**Prerequisite**: Install nfs-common package on both servers and clients
(only for Debian-based distribution), using the following command:

`$ sudo aptitude install nfs-common `

<a name="manual-nfs" />
### Manually Mounting Volumes Using NFS

**To manually mount a Gluster volume using NFS**

-   To mount a volume, use the following command:

    `# mount -t nfs -o vers=3 HOSTNAME-OR-IPADDRESS:/VOLNAME MOUNTDIR`

    For example:

    `# mount -t nfs -o vers=3 server1:/test-volume /mnt/glusterfs`

    > **Note**
    >
    > Gluster NFS server does not support UDP. If the NFS client you are
    > using defaults to connecting using UDP, the following message
    > appears:
    >
    > `requested NFS version or transport protocol is not supported`.

    **To connect using TCP**

-   Add the following option to the mount command:

    `-o mountproto=tcp `

    For example:

    `# mount -o mountproto=tcp -t nfs server1:/test-volume /mnt/glusterfs`

**To mount Gluster NFS server from a Solaris client**

-   Use the following command:

    `# mount -o proto=tcp,vers=3 nfs://HOSTNAME-OR-IPADDRESS:38467/VOLNAME MOUNTDIR`

    For example:

    ` # mount -o proto=tcp,vers=3 nfs://server1:38467/test-volume /mnt/glusterfs`

<a name="auto-nfs" />
### Automatically Mounting Volumes Using NFS

You can configure your system to automatically mount Gluster volumes
using NFS each time the system starts.

**To automatically mount a Gluster volume using NFS**

-   To mount a volume, edit the /etc/fstab file and add the following
    line:

    `HOSTNAME-OR-IPADDRESS:/VOLNAME MOUNTDIR nfs defaults,_netdev,vers=3 0 0`

    For example,

    `server1:/test-volume /mnt/glusterfs nfs defaults,_netdev,vers=3 0 0`

    > **Note**
    >
    > Gluster NFS server does not support UDP. If the NFS client you are
    > using defaults to connecting using UDP, the following message
    > appears:
    >
    > `requested NFS version or transport protocol is not supported.`

    To connect using TCP

-   Add the following entry in /etc/fstab file :

    `HOSTNAME-OR-IPADDRESS:/VOLNAME MOUNTDIR nfs defaults,_netdev,mountproto=tcp 0 0`

    For example,

    `server1:/test-volume /mnt/glusterfs nfs defaults,_netdev,mountproto=tcp 0 0`

**To automount NFS mounts**

Gluster supports \*nix standard method of automounting NFS mounts.
Update the /etc/auto.master and /etc/auto.misc and restart the autofs
service. After that, whenever a user or process attempts to access the
directory it will be mounted in the background.

### Testing Volumes Mounted Using NFS

You can confirm that Gluster directories are mounting successfully.

**To test mounted volumes**

-   Use the mount command by entering the following:

    `# mount`

    For example, the output of the mount command on the client will
    display an entry like the following:

    `server1:/test-volume on /mnt/glusterfs type nfs (rw,vers=3,addr=server1)`

-   Use the df command by entering the following:

    `# df`

    For example, the output of df command on the client will display the
    aggregated storage space from all the bricks in a volume.

        # df -h /mnt/glusterfs 
        Filesystem              Size Used Avail Use% Mounted on 
        server1:/test-volume    28T  22T  5.4T  82%  /mnt/glusterfs

-   Change to the directory and list the contents by entering the
    following:

    `# cd MOUNTDIR`
    `# ls`

#CIFS

You can use CIFS to access to volumes when using Microsoft Windows as
well as SAMBA clients. For this access method, Samba packages need to be
present on the client side. You can export glusterfs mount point as the
samba export, and then mount it using CIFS protocol.

This section describes how to mount CIFS shares on Microsoft
Windows-based clients (both manually and automatically) and how to
verify that the volume has mounted successfully.

> **Note**
>
> CIFS access using the Mac OS X Finder is not supported, however, you
> can use the Mac OS X command line to access Gluster volumes using
> CIFS.

##Using CIFS to Mount Volumes

You can use either of the following methods to mount Gluster volumes:

-   [Exporting Gluster Volumes Through Samba](#export-samba)
-   [Manually Mounting Volumes Using CIFS](#cifs-manual)
-   [Automatically Mounting Volumes Using CIFS](#cifs-auto)

You can also use Samba for exporting Gluster Volumes through CIFS
protocol.

<a name="export-samba" />
### Exporting Gluster Volumes Through Samba

We recommend you to use Samba for exporting Gluster volumes through the
CIFS protocol.

**To export volumes through CIFS protocol**

1.  Mount a Gluster volume.

2.  Setup Samba configuration to export the mount point of the Gluster
    volume.

    For example, if a Gluster volume is mounted on /mnt/gluster, you
    must edit smb.conf file to enable exporting this through CIFS. Open
    smb.conf file in an editor and add the following lines for a simple
    configuration:

    [glustertest]

    comment = For testing a Gluster volume exported through CIFS

    path = /mnt/glusterfs

    read only = no

    guest ok = yes

Save the changes and start the smb service using your systems init
scripts (/etc/init.d/smb [re]start).

> **Note**
>
> To be able mount from any server in the trusted storage pool, you must
> repeat these steps on each Gluster node. For more advanced
> configurations, see Samba documentation.

<a name="cifs-manual" />
### Manually Mounting Volumes Using CIFS

You can manually mount Gluster volumes using CIFS on Microsoft
Windows-based client machines.

**To manually mount a Gluster volume using CIFS**

1.  Using Windows Explorer, choose **Tools \> Map Network Drive…** from
    the menu. The **Map Network Drive**window appears.

2.  Choose the drive letter using the **Drive** drop-down list.

3.  Click **Browse**, select the volume to map to the network drive, and
    click **OK**.

4.  Click **Finish.**

The network drive (mapped to the volume) appears in the Computer window.

Alternatively, to manually mount a Gluster volume using CIFS by going to 
**Start \> Run** and entering Network path manually.

<a name="cifs-auto" />
### Automatically Mounting Volumes Using CIFS

You can configure your system to automatically mount Gluster volumes
using CIFS on Microsoft Windows-based clients each time the system
starts.

**To automatically mount a Gluster volume using CIFS**

The network drive (mapped to the volume) appears in the Computer window
and is reconnected each time the system starts.

1.  Using Windows Explorer, choose **Tools \> Map Network Drive…** from
    the menu. The **Map Network Drive**window appears.

2.  Choose the drive letter using the **Drive** drop-down list.

3.  Click **Browse**, select the volume to map to the network drive, and
    click **OK**.

4.  Click the **Reconnect** at logon checkbox.

5.  Click **Finish.**

### Testing Volumes Mounted Using CIFS

You can confirm that Gluster directories are mounting successfully by
navigating to the directory using Windows Explorer.

  []: http://bits.gluster.com/gluster/glusterfs/3.3.0qa30/x86_64/
  [1]: http://www.gluster.org/download/
  [2]: http://download.gluster.com/pub/gluster/glusterfs
