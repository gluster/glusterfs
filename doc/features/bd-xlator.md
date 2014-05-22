#Block device translator

Block device translator (BD xlator) is a translator added to GlusterFS  which provides block backend for GlusterFS. This replaces the existing bd_map translator in GlusterFS that provided similar but very limited functionality. GlusterFS expects the underlying brick to be formatted with a POSIX compatible file system. BD xlator changes that and allows for having bricks that are raw block devices like LVM which needn’t have any file systems on them. Hence with BD xlator, it becomes possible to build a GlusterFS volume comprising of bricks that are logical volumes (LV).

##bd

BD xlator maps underlying LVs to files and hence the LVs appear as files to GlusterFS clients. Though BD volume externally appears very similar to the usual Posix volume, not all operations are supported or possible for the files on a BD volume. Only those operations that make sense for a block device are supported and the exact semantics are described in subsequent sections.

While Posix volume takes a file system directory as brick, BD volume needs a volume group (VG) as brick. In the usual use case of BD volume, a file created on BD volume will result in an LV being created in the brick VG. In addition to a VG, BD volume also needs a file system directory that should be specified at the volume creation time. This directory is necessary for supporting the notion of directories and directory hierarchy for the BD volume. Metadata about LVs (size, mapping info) is stored in this directory.

BD xlator was mainly developed to use block devices directly as VM images when GlusterFS is used as storage for KVM virtualization. Some of the salient points of BD xlator are

* Since BD supports file level snapshots and clones by leveraging the snapshot and clone capabilities of LVM, it can be used to fully off-load snapshot and cloning operations from QEMU to the storage (GlusterFS) itself.

* BD understands dm-thin LVs and hence can support files that are backed by thinly provisioned LVs. This capability of BD xlator translates to having thinly provisioned raw VM images.

* BD enables thin LVs from a thin pool to be used from multiple nodes that have visibility to GlusterFS BD volume. Thus thin pool can be used as a VM image repository allowing access/visibility to it from multiple nodes.

* BD supports true zerofill by using BLKZEROOUT ioctl on underlying block devices. Thus BD allows SCSI WRITESAME to be used on underlying block device if the device supports it.

Though BD xlator is primarily intended to be used with block devices, it does provide full Posix xlator compatibility for files that are created on BD volume but are not backed by or mapped to a block device. Such files which don’t have a block device mapping exist on the Posix directory that is specified during BD volume creation. BD xlator is available from GlusterFS-3.5 release.

###Compiling BD translator

BD xlator needs lvm2 development library. –enable-bd-xlator option can be used with `./configure` script to explicitly enable BD translator. The following snippet from the output of configure script shows that BD xlator is enabled for compilation.


#####GlusterFS configure summary

 …
 Block Device xlator  : yes


###Creating a BD volume

BD supports hosting of both linear LV and thin LV within the same volume. However seperate examples are provided below. As noted above, the prerequisite for a BD volume is VG which is created from a loop device here, but it can be any other device too.


* Creating BD volume with linear LV backend

* Create a loop device


    [root@node ~]# dd if=/dev/zero of=bd-loop count=1024 bs=1M

    [root@node ~]# losetup /dev/loop0 bd-loop


* Prepare a brick by creating a VG

    [root@node ~]# pvcreate /dev/loop0

    [root@node ~]# vgcreate bd-vg /dev/loop0


* Create the BD volume

* Create a POSIX directory first


    [root@node ~]# mkdir /bd-meta

It is recommended that this directory is created on an LV in the brick VG itself so that both data and metadata live together on the same device.


* Create and mount the volume

    [root@node ~]# gluster volume create bd node:/bd-meta?bd-vg force


The general syntax for specifying the brick is `host:/posix-dir?volume-group-name` where “?” is the separator.



    [root@node ~]# gluster volume start bd
    [root@node ~]# gluster volume info bd
    Volume Name: bd
    Type: Distribute
    Volume ID: cb042d2a-f435-4669-b886-55f5927a4d7f
    Status: Started
    Xlator 1: BD
    Capability 1: offload_copy
    Capability 2: offload_snapshot
    Number of Bricks: 1
    Transport-type: tcp
    Bricks:
    Brick1: node:/bd-meta
    Brick1 VG: bd-vg



    [root@node ~]# mount -t glusterfs node:/bd /mnt

* Create a file that is backed by an LV

    [root@node ~]# ls /mnt

    [root@node ~]#

Since the volume is empty now, so is the underlying VG.

    [root@node ~]# lvdisplay bd-vg
    [root@node ~]#

Creating a file that is mapped to an LV is a 2 step operation. First the file should be created on the mount point and a specific extended attribute should be set to map the file to LV.

    [root@node ~]# touch /mnt/lv
    [root@node ~]# setfattr -n “user.glusterfs.bd” -v “lv” /mnt/lv

Now an LV got created in the VG brick and the file /mnt/lv maps to this LV. Any read/write to this file ends up as read/write to the underlying LV.

    [root@node ~]# lvdisplay bd-vg
    — Logical volume —
    LV Path                 /dev/bd-vg/6ff0f25f-2776-4d19-adfb-df1a3cab8287
    LV Name                 6ff0f25f-2776-4d19-adfb-df1a3cab8287
    VG Name                 bd-vg
    LV UUID                 PjMPcc-RkD5-RADz-6ixG-UYsk-oclz-vL0nv6
    LV Write Access         read/write
    LV Creation host, time node, 2013-11-26 16:15:45 +0530
    LV Status               available
    open                    0
    LV Size                 4.00 MiB
    Current LE              1
    Segments                1
    Allocation              inherit
    Read ahead sectors      0
    Block device            253:6

The file gets created with default LV size which is 1 LE which is 4MB in this case.

    [root@node ~]# ls -lh /mnt/lv
    -rw-r–r–. 1 root root 4.0M Nov 26 16:15 /mnt/lv

truncate can be used to set the required file size.

    [root@node ~]# truncate /mnt/lv -s 256M
    [root@node ~]# lvdisplay bd-vg
    — Logical volume —
    LV Path               /dev/bd-vg/6ff0f25f-2776-4d19-adfb-df1a3cab8287
    LV Name               6ff0f25f-2776-4d19-adfb-df1a3cab8287
    VG Name               bd-vg
    LV UUID               PjMPcc-RkD5-RADz-6ixG-UYsk-oclz-vL0nv6
    LV Write Access       read/write
    LV Creation host, time node, 2013-11-26 16:15:45 +0530
    LV Status             available
    # open                   0
    LV Size               256.00 MiB
    Current LE             64
    Segments                1
    Allocation            inherit
    Read ahead sectors     0
    Block device          253:6


    [root@node ~]# ls -lh /mnt/lv
    -rw-r–r–. 1 root root 256M Nov 26 16:15 /mnt/lv

    currently LV size has been set to        256

The size of the file/LV can be specified during creation/mapping time itself like this:

    setfattr -n “user.glusterfs.bd” -v “lv:256MB” /mnt/lv

2. Creating BD volume with thin LV backend

* Create a loop device


    [root@node ~]# dd if=/dev/zero of=bd-loop-thin count=1024 bs=1M

    [root@node ~]# losetup /dev/loop0 bd-loop-thin


* Prepare a brick by creating a VG and thin pool


    [root@node ~]# pvcreate /dev/loop0

    [root@node ~]# vgcreate bd-vg-thin /dev/loop0


* Create a thin pool


    [root@node ~]# lvcreate –thin bd-vg-thin -L 1000M

    Rounding up size to full physical extent 4.00 MiB
    Logical volume “lvol0″ created

lvdisplay shows the thin pool

    [root@node ~]# lvdisplay bd-vg-thin
    — Logical volume —
    LV Name                      lvol0
    VG Name                      bd-vg-thin
    LV UUID                      HVa3EM-IVMS-QG2g-oqU6-1UxC-RgqS-g8zhVn
    LV Write Access              read/write
    LV Creation host, time node, 2013-11-26 16:39:06 +0530
    LV Pool transaction ID       0
    LV Pool metadata             lvol0_tmeta
    LV Pool data                 lvol0_tdata
    LV Pool chunk size           64.00 KiB
    LV Zero new blocks           yes
    LV Status                    available
    # open                       0
    LV Size                      1000.00 MiB
    Allocated pool data          0.00%
    Allocated metadata           0.88%
    Current LE                   250
    Segments                     1
    Allocation                     inherit
    Read ahead sectors     auto
    Block device                253:9

* Create the BD volume

* Create a POSIX directory first


    [root@node ~]# mkdir /bd-meta-thin

* Create and mount the volume

    [root@node ~]# gluster volume create bd-thin node:/bd-meta-thin?bd-vg-thin force

    [root@node ~]# gluster volume start bd-thin


    [root@node ~]# gluster volume info bd-thin
    Volume Name: bd-thin
    Type: Distribute
    Volume ID: 27aa7eb0-4ffa-497e-b639-7cbda0128793
    Status: Started
    Xlator 1: BD
    Capability 1: thin
    Capability 2: offload_copy
    Capability 3: offload_snapshot
    Number of Bricks: 1
    Transport-type: tcp
    Bricks:
    Brick1: node:/bd-meta-thin
    Brick1 VG: bd-vg-thin


    [root@node ~]# mount -t glusterfs node:/bd-thin /mnt

* Create a file that is backed by a thin LV


    [root@node ~]# ls /mnt

    [root@node ~]#

Creating a file that is mapped to a thin LV is a 2 step operation. First the file should be created on the mount point and a specific extended attribute should be set to map the file to a thin LV.

    [root@node ~]# touch /mnt/thin-lv
 
    [root@node ~]# setfattr -n “user.glusterfs.bd” -v “thin:256MB” /mnt/thin-lv

Now /mnt/thin-lv is a thin provisioned file that is backed by a thin LV and size has been set to 256.

    [root@node ~]# lvdisplay bd-vg-thin
    — Logical volume —
    LV Name             lvol0
    VG Name             bd-vg-thin
    LV UUID             HVa3EM-IVMS-QG2g-oqU6-1UxC-RgqS-g8zhVn
    LV Write Access     read/write
    LV Creation host, time node, 2013-11-26 16:39:06 +0530
    LV Pool transaction ID 1
    LV Pool metadata    lvol0_tmeta
    LV Pool data        lvol0_tdata
    LV Pool chunk size  64.00 KiB
    LV Zero new blocks  yes
    LV Status           available
    # open               0
    LV Size             000.00 MiB
    Allocated pool data 0.00%
    Allocated metadata  0.98%
    Current LE          250
    Segments            1
    Allocation          inherit
    Read ahead sectors  auto
    Block device         253:9




    — Logical volume —
    LV Path             dev/bd-vg-thin/081b01d1-1436-4306-9baf-41c7bf5a2c73
    LV Name             081b01d1-1436-4306-9baf-41c7bf5a2c73
    VG Name             bd-vg-thin
    LV UUID             coxpTY-2UZl-9293-8H2X-eAZn-wSp6-csZIeB
    LV Write Access     read/write
    LV Creation host, time node, 2013-11-26 16:43:19 +0530
    LV Pool name        lvol0
    LV Status           available
    # open               0
    LV Size             256.00 MiB
    Mapped size          0.00%
    Current LE           64
    Segments             1
    Allocation          inherit
    Read ahead sectors   auto
    Block device                 253:10





As can be seen from above, creation of a file resulted in creation of a thin LV in the brick.


###Improvisation on BD translator:

First version of BD xlator  ( block backend) had few limitations  such as

* Creation of directories not supported
* Supports only single brick
* Does not use extended attributes (and client gfid) like posix xlator
* Creation of special files (symbolic links, device nodes etc) not
  supported

Basic limitation of not allowing directory creation was blocking
oVirt/VDSM to consume BD xlator as part of Gluster domain since VDSM
creates multi-level directories when GlusterFS is used as storage
backend for storing VM images.

To overcome these limitations a new BD xlator with following
improvements are implemented.

* New hybrid BD xlator that handles both regular files and block device
  files
* The volume will have both POSIX and BD bricks. Regular files are
  created on POSIX bricks, block devices are created on the BD brick (VG)
* BD xlator leverages exiting POSIX xlator for most POSIX calls and
  hence sits above the POSIX xlator
* Block device file is differentiated from regular file by an extended
  attribute
* The xattr 'user.glusterfs.bd' (BD_XATTR) plays a role in mapping a
  posix file to Logical Volume (LV).
* When a client sends a request to set BD_XATTR on a posix file, a new
  LV is created and mapped to posix file. So every block device will
  have a representative file in POSIX brick with 'user.glusterfs.bd'
  (BD_XATTR) set.
* Here after all operations on this file results in LV related
  operations.

For example, opening a file that has BD_XATTR set results in opening
the LV block device, reading results in reading the corresponding LV
block device.

When BD xlator gets request to set BD_XATTR via setxattr call, it
creates a LV and information about this LV is placed in the xattr of the
posix file. xattr "user.glusterfs.bd" used to identify that posix file
is mapped to BD.

Usage:
Server side:

    [root@host1 ~]# gluster volume create bdvol host1:/storage/vg1_info?vg1 host2:/storage/vg2_info?vg2

It creates a distributed gluster volume 'bdvol' with Volume Group vg1
using posix brick /storage/vg1_info in host1 and Volume Group vg2 using
/storage/vg2_info in host2.


    [root@host1 ~]# gluster volume start bdvol

Client side:

    [root@node ~]# mount -t glusterfs host1:/bdvol /media
    [root@node ~]# touch /media/posix

It creates regular posix file 'posix' in either host1:/vg1 or host2:/vg2 brick

    [root@node ~]# mkdir /media/image

    [root@node ~]# touch /media/image/lv1


It also creates regular posix file 'lv1' in either host1:/vg1 or
host2:/vg2 brick

    [root@node ~]# setfattr -n "user.glusterfs.bd" -v "lv" /media/image/lv1

    [root@node ~]#


Above setxattr results in creating a new LV in corresponding brick's VG
and it sets 'user.glusterfs.bd' with value 'lv:<default-extent-size''


    [root@node ~]# truncate -s5G /media/image/lv1


It results in resizig LV 'lv1'to 5G

New BD xlator code is placed in `xlators/storage/bd` directory.

Also add volume-uuid to the VG so that same VG cannot be used for other
bricks/volumes. After deleting a gluster volume, one has to manually
remove the associated tag using vgchange <vg-name> --deltag
`<trusted.glusterfs.volume-id:<volume-id>>`


#### Exposing volume capabilities

With multiple storage translators (posix and bd) being supported in GlusterFS, it becomes
necessary to know the volume type so that user can issue appropriate calls that are relevant
only to the a given volume type. Hence there needs to be a way to expose the type of
the storage translator of the volume to the user.

BD xlator is capable of providing server offloaded file copy, server/storage offloaded
zeroing of a file etc. This capabilities should be visible to the client/user, so that these
features can be exploited.

BD xlator exports capability information through gluster volume info (and --xml) output. For eg:

`snip of gluster volume info output for a BD based volume`

        Xlator 1: BD
        Capability 1: thin

`snip of gluster volume info --xml output for a BD based volume`

        <xlators>
          <xlator>
                <name>BD</name>
                <capabilities>
                        <capability>thin</capability>
                </capabilities>
          </xlator>
        </xlators>

But this capability information should also exposed through some other means so that a host
which is not part of Gluster peer could also avail this capabilities.

* Type

BD translator supports both regular files and block device, i,e., one can create files on
GlusterFS volume backed by BD translator and this file could end up as regular posix file or
a logical volume (block device) based on the user''s choice. User can do a setxattr on the
created file to convert it to a logical volume.

Users of BD backed volume like QEMU would like to know that it is working with BD type of volume
so that it can issue an additional setxattr call after creating a VM image on GlusterFS backend.
This is necessary to ensure that the created VM image is backed by LV instead of file.

There are different ways to expose this information (BD type of volume) to user.
One way is to export it via a `getxattr` call. That said, When a client issues getxattr("volume_type")
on a root gfid, bd xlator will return 1 implying its BD xlator. But posix xlator will return ENODATA
and client code can interpret this as posix xlator. Also capability list can be returned via
getxattr("caps") for root gfid.

* Capabilities

BD xlator supports new features such as server offloaded file copy, thin provisioned VM images etc.

There is no standard way of exploiting these features from client side (such as syscall
to exploit server offloaded copy). So these features need to be exported to the client so that
they can be used. BD xlator latest version exports these capabilities information through
gluster volume info (and --xml) output. But if a client is not part of GlusterFS peer
it can''t run volume info command to get the list of capabilities of a given GlusterFS volume.
For example, GlusterFS block driver in qemu need to get the capability list so that these features are used.



Parts of this documentation were originally published here
#http://raobharata.wordpress.com/2013/11/27/glusterfs-block-device-translator/
