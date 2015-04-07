# GlusterFS and NFS-Ganesha integration

Nfs-ganesha can  support NFS (v3, 4.0, 4.1 pNFS) and 9P (from the Plan9 operating system) protocols concurrently. It provides a FUSE-compatible File System Abstraction Layer(FSAL) to allow the file-system developers to plug in their own storage mechanism and access it from any NFS client.

With NFS-GANESHA, the NFS client talks to the NFS-GANESHA server instead, which is in the user address space already. NFS-GANESHA can access the FUSE filesystems directly through its FSAL without copying any data to or from the kernel, thus potentially improving response times. Of course the network streams themselves (TCP/UDP) will still be handled by the Linux kernel when using NFS-GANESHA.

Even GlusterFS has been integrated with NFS-Ganesha, in the recent past to export the volumes created via glusterfs, using “libgfapi”.  libgfapi is a new userspace library developed to access data in glusterfs. It performs I/O on gluster volumes directly without FUSE mount. It is a filesystem like api which runs/sits in the application process context(which is NFS-Ganesha here) and eliminates the use of fuse and the kernel vfs layer from the glusterfs volume access. Thus by integrating NFS-Ganesha and libgfapi, the speed and latency have been improved compared to FUSE mount access.

### 1.) Pre-requisites

  -  Before starting to setup NFS-Ganesha, a GlusterFS volume should be created.
  -  Disable kernel-nfs, gluster-nfs services on the system using the following commands
        - service nfs stop
        - gluster vol set <volname> nfs.disable ON (Note: this command has to be repeated for all the volumes in the trusted-pool)
  -  Usually the libgfapi.so* files are installed in “/usr/lib” or “/usr/local/lib”, based on whether you have installed glusterfs using rpm or sources. Verify if those libgfapi.so* files are linked in “/usr/lib64″ and “/usr/local/lib64″ as well. If not create the links for those .so files in those directories.

### 2.) Installing nfs-ganesha

##### i)  using rpm install

   - nfs-ganesha rpms are available in Fedora19 or later packages. So to install nfs-ganesha, run
        - *#yum install nfs-ganesha*
   - Using CentOS or EL, download the rpms from the below link :
        -  http://download.gluster.org/pub/gluster/glusterfs/nfs-ganesha

##### ii) using sources

   - cd /root
   - git clone git://github.com/nfs-ganesha/nfs-ganesha.git
   - cd nfs-ganesha/
   - git submodule update --init
   - git checkout -b next origin/next (Note : origin/next is the current development branch)
   - rm -rf ~/build; mkdir ~/build ; cd ~/build
   - cmake -DUSE_FSAL_GLUSTER=ON -DCURSES_LIBRARY=/usr/lib64 -DCURSES_INCLUDE_PATH=/usr/include/ncurses -DCMAKE_BUILD_TYPE=Maintainer   /root/nfs-ganesha/src/
   - make; make install
> Note: libcap-devel, libnfsidmap, dbus-devel, libacl-devel ncurses* packages
> may need to be installed prior to running this command. For Fedora, libjemalloc,
> libjemalloc-devel may also be required.

### 3.) Run nfs-ganesha server

   - To start nfs-ganesha manually, execute the following command:
        - *#ganesha.nfsd -f <location_of_nfs-ganesha.conf_file> -L <location_of_log_file> -N <log_level> -d

```sh
For example:
#ganesha.nfsd -f nfs-ganesha.conf -L nfs-ganesha.log -N NIV_DEBUG -d
where:
nfs-ganesha.log is the log file for the ganesha.nfsd process.
nfs-ganesha.conf is the configuration file
NIV_DEBUG is the log level.
```
   - To check if nfs-ganesha has started, execute the following command:
        -  *#ps aux | grep ganesha*
   - By default '/' will be exported

### 4.) Exporting GlusterFS volume via nfs-ganesha

#####step 1 :

To export any GlusterFS volume or directory inside volume, create the EXPORT block for each of those entries in a .conf file, for example export.conf.  The following paremeters are required to export any entry.
- *#cat export.conf*

```sh
EXPORT{
	Export_Id = 1 ;   # Export ID unique to each export
	Path = "volume_path";  # Path of the volume to be exported. Eg: "/test_volume"

	FSAL {
		name = GLUSTER;
		hostname = "10.xx.xx.xx";  # IP of one of the nodes in the trusted pool
		volume = "volume_name";	 # Volume name. Eg: "test_volume"
	}

	Access_type = RW;	 # Access permissions
	Squash = No_root_squash; # To enable/disable root squashing
	Disable_ACL = TRUE;	 # To enable/disable ACL
	Pseudo = "pseudo_path";	 # NFSv4 pseudo path for this export. Eg: "/test_volume_pseudo"
	Protocols = "3","4" ;	 # NFS protocols supported
	Transports = "UDP","TCP" ; # Transport protocols supported
	SecType = "sys";	 # Security flavors supported
}
```

#####step 2 :

Define/copy “nfs-ganesha.conf” file to a suitable location. This file is available in “/etc/glusterfs-ganesha” on installation of nfs-ganesha rpms or incase if using the sources, rename “/root/nfs-ganesha/src/FSAL/FSAL_GLUSTER/README” file to “nfs-ganesha.conf” file.

#####step 3 :

Now include the “export.conf” file in nfs-ganesha.conf. This can be done by adding the line below at the end of nfs-ganesha.conf.
   - %include “export.conf”

#####step 4 :

   - run ganesha server as mentioned in section 3
   - To check if the volume is exported, run
       - *#showmount -e localhost*

### 5.) Additional Notes

To switch back to gluster-nfs/kernel-nfs, kill the ganesha daemon and start those services using the below commands :

   - pkill ganesha
   - service nfs start (for kernel-nfs)
   - gluster v set <volname> nfs.disable off


### 6.) References

   - Setup and create glusterfs volumes :
http://www.gluster.org/community/documentation/index.php/QuickStart

   - NFS-Ganesha wiki : https://github.com/nfs-ganesha/nfs-ganesha/wiki

   - Sample configuration files
        - /root/nfs-ganesha/src/config_samples/gluster.conf
        - https://github.com/nfs-ganesha/nfs-ganesha/blob/master/src/config_samples/gluster.conf

   - https://forge.gluster.org/nfs-ganesha-and-glusterfs-integration/pages/Home

   - http://blog.gluster.org/2014/09/glusterfs-and-nfs-ganesha-integration/

