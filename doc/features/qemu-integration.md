Using GlusterFS volumes to host VM images and data was sub-optimal due to the FUSE overhead involved in accessing gluster volumes via GlusterFS native client. However this has changed now with two specific enhancements:

- A new library called libgfapi is now available as part of GlusterFS that  provides POSIX-like C APIs for accessing gluster volumes. libgfapi support is available from GlusterFS-3.4 release.
- QEMU (starting from QEMU-1.3) will have GlusterFS block driver that uses libgfapi and hence there is no FUSE overhead any longer when QEMU works with VM images on gluster volumes.

GlusterFS with its pluggable translator model can serve as a flexible storage backend for QEMU. QEMU has to just talk to GlusterFS and GlusterFS will hide different file systems and storage types underneath. Various GlusterFS storage features like replication and striping will automatically be available for QEMU. Efforts are also on to add block device backend in Gluster via Block Device (BD) translator that will expose underlying block devices as files to QEMU. This allows GlusterFS to be a single storage backend for both file and block based storage types.

###GlusterFS specifcation in QEMU

VM image residing on gluster volume can be specified on QEMU command line using URI format

    gluster[+transport]://[server[:port]]/volname/image[?socket=...]



* `gluster` is the protocol.

* `transport` specifies the transport type used to connect to gluster management daemon (glusterd). Valid transport types are `tcp, unix and rdma.` If a transport type isn’t specified, then tcp type is assumed.

* `server` specifies the server where the volume file specification for the given volume resides. This can be either hostname, ipv4 address or ipv6 address. ipv6 address needs to be within square brackets [ ]. If transport type is unix, then server field should not be specified. Instead the socket field needs to be populated with the path to unix domain socket.

* `port` is the port number on which glusterd is listening. This is optional and if not specified, QEMU will send 0 which will make gluster to use the default port. If the transport type is unix, then port should not be specified.

* `volname` is the name of the gluster volume which contains the VM image.

* `image` is the path to the actual VM image that resides on gluster volume.


###Examples:

    gluster://1.2.3.4/testvol/a.img
    gluster+tcp://1.2.3.4/testvol/a.img
    gluster+tcp://1.2.3.4:24007/testvol/dir/a.img
    gluster+tcp://[1:2:3:4:5:6:7:8]/testvol/dir/a.img
    gluster+tcp://[1:2:3:4:5:6:7:8]:24007/testvol/dir/a.img
    gluster+tcp://server.domain.com:24007/testvol/dir/a.img
    gluster+unix:///testvol/dir/a.img?socket=/tmp/glusterd.socket
    gluster+rdma://1.2.3.4:24007/testvol/a.img



NOTE: (GlusterFS URI description and above examples are taken from QEMU documentation)

###Configuring QEMU with GlusterFS backend

While building QEMU from source, in addition to the normal configuration options, ensure that  –enable-glusterfs options are  specified explicitly with ./configure script to get glusterfs support in qemu.

Starting with QEMU-1.6, pkg-config is used to configure the GlusterFS backend in QEMU. If you are using GlusterFS compiled and installed from sources, then the GlusterFS package config file (glusterfs-api.pc) might not be present at the standard path and you will have to explicitly add the path by executing this command before running the QEMU configure script:

    export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig/

Without this, GlusterFS driver will not be compiled into QEMU even when GlusterFS is present in the system.

* Creating a VM image on GlusterFS backend

qemu-img command can be used to create VM images on gluster backend. The general syntax for image creation looks like this:

For ex:

    qemu-img create gluster://server/volname/path/to/image size

## How to setup the environment:

This usecase ( using glusterfs backend for VM disk store), is known as 'Virt-Store' usecase. Steps for the entire procedure could be split to:

*    Steps to be done on gluster volume side
*    Steps to be done on Hypervisor side


##Steps to be done on gluster side

These are the steps that needs to be done on the gluster side. Precisely this involves

    Creating "Trusted Storage Pool"
    Creating a volume
    Tuning the volume for virt-store
    Tuning glusterd to accept requests from QEMU
    Tuning glusterfsd to accept requests from QEMU
    Setting ownership on the volume
    Starting the volume

* Creating "Trusted Storage Pool"

Install glusterfs rpms on the NODE. You can create a volume with a single node. You can also scale up the cluster, as we call as `Trusted Storage Pool`, by adding more nodes to the cluster

    gluster peer probe <hostname>

* Creating a volume

It is highly recommended to have replicate volume or distribute-replicate volume for virt-store usecase, as it would add high availability and fault-tolerance. Remember the plain distribute works equally well

    gluster volume create replica 2 <brick1> .. <brickN>

where,  `<brick1> is <hostname>:/<path-of-dir>  `


Note: It is recommended to create sub-directories inside brick and that could be used to create a volume.For example, say, /home/brick1 is the mountpoint of XFS, then you can create a sub-directory inside it /home/brick1/b1 and use it while creating a volume.You can also use space available in root filesystem for bricks. Gluster cli, by default, throws warning in that case. You can override it by using force option

    gluster volume create replica 2 <brick1> .. <brickN> force

If you are new to GlusterFS, you can take a look at QuickStart (http://www.gluster.org/community/documentation/index.php/QuickStart) guide.

* Tuning the volume for virt-store

There are recommended settings available for virt-store. This provide good performance characteristics when enabled on the volume that was used for virt-store

Refer to  http://www.gluster.org/community/documentation/index.php/Virt-store-usecase#Tunables for recommended tunables and for applying them on the volume, http://www.gluster.org/community/documentation/index.php/Virt-store-usecase#Applying_the_Tunables_on_the_volume


* Tuning glusterd to accept requests from QEMU

glusterd receives the request only from the applications that run with port number less than 1024 and it blocks otherwise. QEMU uses port number greater than 1024 and to make glusterd accept requests from QEMU, edit the glusterd vol file, /etc/glusterfs/glusterd.vol and add the following,

    option rpc-auth-allow-insecure on

Note: If you have installed glusterfs from source, you can find glusterd vol file at `/usr/local/etc/glusterfs/glusterd.vol`

Restart glusterd after adding that option to glusterd vol file

    service glusterd restart

* Tuning glusterfsd to accept requests from QEMU

Enable the option `allow-insecure` on the particular volume

    gluster volume set <volname> server.allow-insecure on

IMPORTANT : As of now(april 2,2014)there is a bug, as allow-insecure is not dynamically set on a volume.You need to restart the volume for the change to take effect


* Setting ownership on the volume

Set the ownership of qemu:qemu on to the volume

    gluster volume set <vol-name> storage.owner-uid 107
    gluster volume set <vol-name> storage.owner-gid 107

* Starting the volume

Start the volume

    gluster volume start <vol-name>

## Steps to be done on Hypervisor Side:

To create a raw image,

    qemu-img create gluster://1.2.3.4/testvol/dir/a.img 5G

To create a qcow2 image,

    qemu-img create -f qcow2 gluster://server.domain.com:24007/testvol/a.img 5G





## Booting VM image from GlusterFS backend

A VM image 'a.img' residing on gluster volume testvol can be booted using QEMU like this:


    qemu-system-x86_64 -drive file=gluster://1.2.3.4/testvol/a.img,if=virtio

In addition to VM images, gluster drives can also be used as data drives:

    qemu-system-x86_64 -drive file=gluster://1.2.3.4/testvol/a.img,if=virtio -drive file=gluster://1.2.3.4/datavol/a-data.img,if=virtio

Here 'a-data.img' from datavol gluster volume appears as a 2nd drive for the guest.

It is also possible to make use of libvirt to define a disk and use it with qemu:


### Create libvirt XML to define Virtual Machine

virt-install is python wrapper which is mostly used to create VM using set of params. How-ever virt-install doesn't support any network filesystem [ https://bugzilla.redhat.com/show_bug.cgi?id=1017308 ]

Create a libvirt VM xml - http://libvirt.org/formatdomain.html where  the disk section is formatted in such a way, qemu driver for glusterfs is being used. This can be seen in the following example xml description


    <disk type='network' device='disk'>
        <driver name='qemu' type='raw' cache='none'/>
        <source protocol='gluster' name='distrepvol/vm3.img'>
        <host name='10.70.37.106' port='24007'/>
        </source>
    <target dev='vda' bus='virtio'/>
    <address type='pci' domain='0x0000' bus='0x00' slot='0x04' function='0x0'/>
    </disk>





* Define the VM from the XML file that was created earlier


    virsh define <xml-file-description>

* Verify that the VM is created successfully


    virsh list --all

* Start the VM


    virsh start <VM>

* Verification

You can verify the disk image file that is being used by VM

    virsh domblklist <VM-Domain-Name/ID>

The above should show the volume name and image name. Here is the example,


    [root@test ~]# virsh domblklist vm-test2
    Target     Source
    ------------------------------------------------
    vda        distrepvol/test.img
    hdc        -


Reference:

For more details on this feature implementation and its advantages, please refer:

http://raobharata.wordpress.com/2012/10/29/qemu-glusterfs-native-integration/

http://www.gluster.org/community/documentation/index.php/Libgfapi_with_qemu_libvirt
