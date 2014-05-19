#zerofill API for GlusterFS
zerofill() API would allow creation of pre-allocated and zeroed-out files on GlusterFS volumes by offloading the zeroing part to server and/or storage (storage offloads use SCSI WRITESAME).
## Description

Zerofill writes zeroes to a file in the specified range. This fop will be useful when a whole file needs to be initialized with zero (could be useful for zero filled VM disk image provisioning or during scrubbing of VM disk images).

Client/application can issue this FOP for zeroing out. Gluster server will zero out required range of bytes ie server offloaded zeroing. In the absence of this fop, client/application has to repetitively issue write (zero) fop to the server, which is very inefficient method because of the overheads involved in RPC calls and acknowledgements.

WRITESAME is a SCSI T10 command that takes a block of data as input and writes the same data to other blocks and this write is handled completely within the storage and hence is known as offload . Linux ,now has support for SCSI WRITESAME command which is exposed to the user in the form of BLKZEROOUT ioctl. BD Xlator can exploit BLKZEROOUT ioctl to implement this fop. Thus zeroing out operations can be completely offloaded to the storage device,
making it highly efficient.

The fop takes two arguments offset and size. It zeroes out 'size' number of bytes in an opened file starting from 'offset' position.
This feature adds zerofill support to the following areas:
> - libglusterfs
- io-stats
- performance/md-cache,open-behind
- quota
- cluster/afr,dht,stripe
- rpc/xdr
- protocol/client,server
- io-threads
- marker
- storage/posix
- libgfapi

Client applications can exploit this fop by using glfs_zerofill introduced in libgfapi.FUSE support to this fop has not been added as there is no system call for this fop.
