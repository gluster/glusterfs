# Introduction

GlusterFS supports using RDMA protocol for communication between glusterfs clients and glusterfs bricks.
GlusterFS clients include FUSE client, libgfapi clients(Samba and NFS-Ganesha included), gNFS server and other glusterfs processes that communicate with bricks like self-heal daemon, quotad, rebalance process etc.

NOTE: As of now only FUSE client and gNFS server would support RDMA transport.


NOTE:  
NFS client to gNFS Server/NFS Ganesha Server communication would still happen over tcp.  
CIFS Clients/Windows Clients to Samba Server communication would still happen over tcp.

# Setup
Please refer to these external documentation to setup RDMA on your machines  
http://pkg-ofed.alioth.debian.org/howto/infiniband-howto.html  
http://people.redhat.com/dledford/infiniband_get_started.html  

## Creating Trusted Storage Pool
All the servers in the Trusted Storage Pool must have RDMA devices if either RDMA or TCP,RDMA volumes are created in the storage pool.  
The peer probe must be performed using IP/hostname assigned to the RDMA device.

## Ports and Firewall
Process glusterd will listen on both tcp and rdma if rdma device is found. Port used for rdma is 24008. Similarly, brick processes will also listen on two ports for a volume created with transport "tcp,rdma".

Make sure you update the firewall to accept packets on these ports.

# Gluster Volume Create

A volume can support one or more transport types for communication between clients and brick processes. There are three types of supported transport, which are, tcp, rdma, and tcp,rdma.

Example: To create a distributed volume with four storage servers over InfiniBand:

`# gluster volume create test-volume transport rdma server1:/exp1 server2:/exp2 server3:/exp3 server4:/exp4`  
Creation of test-volume has been successful  
Please start the volume to access data.

# Changing Transport of Volume
To change the supported transport types of a existing volume, follow the procedure:  
NOTE: This is possible only if the volume was created with IP/hostname assigned to RDMA device.  

  1. Unmount the volume on all the clients using the following command:  
`# umount mount-point`  
  2. Stop the volumes using the following command:  
`# gluster volume stop volname`  
  3. Change the transport type.  
For example, to enable both tcp and rdma execute the followimg command:  
`# gluster volume set volname config.transport tcp,rdma`  
  4. Mount the volume on all the clients.  
For example, to mount using rdma transport, use the following command:  
`# mount -t glusterfs -o transport=rdma server1:/test-volume /mnt/glusterfs`

NOTE:  
config.transport option does not have a entry in help of gluster cli.  
`#gluster vol set help | grep config.transport`  
However, the key is a valid one. 

# Mounting a Volume using RDMA

You can use the mount option "transport" to specify the transport type that FUSE client must use to communicate with bricks. If the volume was created with only one transport type, then that becomes the default when no value is specified. In case of tcp,rdma volume, tcp is the default.

For example, to mount using rdma transport, use the following command:  
`# mount -t glusterfs -o transport=rdma server1:/test-volume /mnt/glusterfs`

# Transport used by auxillary processes
All the auxillary processes like self-heal daemon, rebalance process etc use the default transport.In case you have a tcp,rdma volume it will use tcp.  
In case of rdma volume, rdma will be used.  
Configuration options to select transport used by these processes when volume is tcp,rdma are not yet available and will be coming in later releases.



