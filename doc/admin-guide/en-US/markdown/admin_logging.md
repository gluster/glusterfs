# GlusterFS service Logs and locations

Below lists the component, services, and functionality based logs in the GlusterFS Server. As per the File System Hierarchy Standards (FHS) all the log files are placed in the `/var/log` directory.
⁠

##glusterd:

glusterd logs are located at `/var/log/glusterfs/etc-glusterfs-glusterd.vol.log`. One glusterd log file per server. This log file also contains the snapshot and user logs.

##gluster cli command:
gluster cli logs  are located at `/var/log/glusterfs/cmd_history.log`   Gluster commands executed on a node in a GlusterFS Trusted Storage Pool is logged in the `.cmd_log_history` file.

##bricks:
Bricks logs are located at `/var/log/glusterfs/bricks/<path extraction of brick path>.log` .    One log file per brick on the server

##rebalance:
rebalance logs are located at `/var/log/glusterfs/VOLNAME-rebalance.log` .    One log file per volume on the server.

##self heal deamon:
self heal deamon are logged at `/var/log/glusterfs/glustershd.log`.    One log file per server

##quota:

`/var/log/glusterfs/quotad.log` are log of the quota daemons running on each node.
`/var/log/glusterfs/quota-crawl.log` Whenever quota is enabled, a file system crawl is performed and the corresponding log is stored in this file.
`/var/log/glusterfs/quota-mount- VOLNAME.log` An auxiliary FUSE client is mounted in <gluster-run-dir>/VOLNAME of the glusterFS and the corresponding client logs found in this file.

        One log file per server (and per volume from quota-mount.

##Gluster NFS:

`/var/log/glusterfs/nfs.log `  One log file per server

##SAMBA Gluster:

`/var/log/samba/glusterfs-VOLNAME-<ClientIP>.log` .     If the client mounts this on a glusterFS server node, the actual log file or the mount point may not be found. In such a case, the mount outputs of all the glusterFS type mount operations need to be considered.

##Ganesha NFS :
`/var/log/nfs-ganesha.log`

##FUSE Mount:
`/var/log/glusterfs/<mountpoint path extraction>.log `

##Geo-replication:

`/var/log/glusterfs/geo-replication/<master>`
`/var/log/glusterfs/geo-replication-slaves `

##gluster volume heal VOLNAME info command:
`/var/log/glusterfs/glfsheal-VOLNAME.log` . One log file per server on which the command is executed.

##gluster-swift:
`/var/log/messages`

##SwiftKrbAuth:
`/var/log/httpd/error_log `
