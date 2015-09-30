##Ovirt Integration with glusterfs

oVirt is an opensource virtualization management platform. You can use oVirt to manage
hardware nodes, storage and network resources, and to deploy and monitor virtual machines
running in your data center.  oVirt serves as the bedrock for Red Hat''s Enterprise Virtualization product,
and is the "upstream" project where new features are developed in advance of their inclusion
in that supported product offering.

To know more about ovirt please visit http://www.ovirt.org/ and to configure
#http://www.ovirt.org/Quick_Start_Guide#Install_oVirt_Engine_.28Fedora.29%60

For the installation step of ovirt, please refer
#http://www.ovirt.org/Quick_Start_Guide#Install_oVirt_Engine_.28Fedora.29%60

When oVirt integrated with gluster, glusterfs can be used in below forms:

* As a storage domain to host VM disks.

There are mainly two ways to exploit glusterfs as a storage domain.
	- POSIXFS_DOMAIN ( >=oVirt 3.1 )
	- GLUSTERFS_DOMAIN ( >=oVirt 3.3)

The former one has performance overhead and is not an ideal way to consume images hosted in glusterfs volumes.
When used by this method, qemu uses glusterfs `mount point` to access VM images and invite FUSE overhead.
The libvirt treats this as a file type disk in its xml schema.

The latter is the recommended way of using glusterfs with ovirt as a storage domain. This provides better
and efficient way to access images hosted under glusterfs volumes.When qemu accessing glusterfs volume using this method,
it make use of `libgfapi` implementation of glusterfs and this method is called native integration.
Here the glusterfs is added as a block backend to qemu and libvirt treat this as a `network` type disk.

For more details on this, please refer # http://www.ovirt.org/Features/GlusterFS_Storage_Domain
However there are 2 bugs which block usage of this feature.

https://bugzilla.redhat.com/show_bug.cgi?id=1022961
https://bugzilla.redhat.com/show_bug.cgi?id=1017289

Please check above bugs for latest status.

* To manage gluster trusted pools.

oVirt web admin console can be used to -
	- add new / import existing gluster cluster
	- add/delete volumes
	- add/delete bricks
	- set/reset volume options
	- optimize volume for virt store
	- Rebalance and Remove bricks
	- Monitor gluster deployment - node, brick, volume status,
	  Enhanced service monitoring (Physical node resources as well Quota, geo-rep and self-heal status) through Nagios integration(>=oVirt 3.4)



When configuing ovirt to manage only gluster cluster/trusted pool, you need to select `gluster` as an input for
`Application mode` in OVIRT ENGINE CONFIGURATION option of `engine-setup` command.
Refer # http://www.ovirt.org/Quick_Start_Guide#Install_oVirt_Engine_.28Fedora.29%60

If you want to use gluster as both ( as a storage domain to host VM disks and to manage gluster trusted pools)
you need to input `both` as a value for `Application mode` in engine-setup command.

Once you have successfully installed oVirt Engine as mentioned above, you will be provided with instructions
to access oVirt''s web console.

Below example shows how to configure gluster nodes in fedora.


#Configuring gluster nodes.

On the machine designated as your host, install any supported distribution( ex:Fedora/CentOS/RHEL...etc).
A minimal installation is sufficient.

Refer # http://www.ovirt.org/Quick_Start_Guide#Install_Hosts


##Connect to Ovirt Engine

Log In to Administration Console

Ensure that you have the administrator password configured during installation of  oVirt engine.

- To connect to oVirt webadmin console


Open a browser and navigate to https://domain.example.com/webadmin. Substitute domain.example.com with the URL provided during installation

If this is your first time connecting to the administration console, oVirt Engine will issue
security certificates for your browser. Click the link labelled this certificate to trust the
ca.cer certificate. A pop-up displays, click Open to launch the Certificate dialog.
Click `Install Certificate` and select to place the certificate in Trusted Root Certification Authorities store.


The console login screen displays. Enter admin as your User Name, and enter the Password that
you provided during installation. Ensure that your domain is set to Internal. Click Login.


You have now successfully logged in to the oVirt web administration console. Here, you can configure and manage all your gluster resources.

To manage gluster trusted pool:

- Create a cluster with "Enable gluster service" - turned on. (Turn on "Enable virt service" if the same nodes are used as hypervisor as well)
- Add hosts which have already been set up as in step Configuring gluster nodes.
- Create a volume, and click on "Optimize for virt store",This sets the volume tunables optimize volume to be used as an image store

To use this volume as a storage domain:

Please refer `User interface` section of www.ovirt.org/Features/GlusterFS_Storage_Domain
