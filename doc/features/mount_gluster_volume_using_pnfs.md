# How to export gluster volumes using pNFS?

The Parallel Network File System (pNFS) is part of the NFS v4.1 protocol that
allows compute clients to access storage devices directly and in parallel.
The pNFS cluster consists of MDS(Meta-Data-Server) and DS (Data-Server).
The client sends all the read/write requests directly to DS and all other
operations are handle by the MDS. pNFS support is implemented as part of
glusterFS+NFS-ganesha integration.

### 1.) Pre-requisites

  - Create a GlusterFS volume

  - Install nfs-ganesha (refer section 5)

  - Disable kernel-nfs, gluster-nfs services on the system using the following commands
       - service nfs stop
       - gluster vol set <volname> nfs.disable ON (Note: this command has to be repeated for all the volumes in the trusted-pool)

### 2.) Configure nfs-ganesha for pNFS

  - Disable nfs-ganesha and tear down HA cluster via gluster cli (pNFS did not need to disturb HA setup)
       - gluster features.ganesha disable

  - For the optimal working of pNFS, ganesha servers should run on every node in the trusted pool manually(refer section 5)
       - *#ganesha.nfsd -f <location_of_nfs-ganesha.conf_file> -L <location_of_log_file> -N <log_level> -d*

  - Check whether volume is exported via nfs-ganesha in all the nodes.
       - *#showmount -e localhost*

### 3.) Mount volume via pNFS

Mount the volume using any nfs-ganesha server in the trusted pool.By default, nfs version 4.1 will use pNFS protocol for gluster volumes
   - *#mount -t nfs4 -o minorversion=1 <ip of server>:/<volume name> <mount path>*

### 4.) Points to be noted

   - Current architecture supports only single MDS and mulitple DS. The server with which client mounts will act as MDS and all severs including MDS can act as DS.

   - If any of the DS goes down , then MDS will handle those I/O's.

   - Hereafter, all the subsequent nfs clients need to use same server for mounting that volume via pNFS. i.e more than one MDS for a volume is not prefered

   - pNFS support is only tested with distributed, replicated or distribute-replicate volumes

   - It is tested and verfied with RHEL 6.5 , fedora 20, fedora 21 nfs clients. It is always better to use latest nfs-clients

### 5.) References

   - Setup and create glusterfs volumes : http://www.gluster.org/community/documentation/index.php/QuickStart

   - NFS-Ganesha wiki : https://github.com/nfs-ganesha/nfs-ganesha/wiki

   - For installing, running NFS-Ganesha and exporting a volume :
      - read doc/features/glusterfs_nfs-ganesha_integration.md
      - http://blog.gluster.org/2014/09/glusterfs-and-nfs-ganesha-integration/
