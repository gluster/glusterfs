# Table of Contents
1. Disk Filesystems - [video](https://youtu.be/kD3A_vpVfNk) - [slides](https://www.slideshare.net/PranithKarampuri/glusterfs-session-1-disk-filesystems)  
This session covers the concepts of disk filesystems that are important to understand glusterfs.  

2. 1 Layer above Disk Filesystems(Posix) - [video](https://youtu.be/eNoargRqOHQ) - [slides](https://www.slideshare.net/PranithKarampuri/glusterfs-session-2-1-layer-above-disk-filesystems)  
This session covers how posix xlator converts File Operations(FOPs) in glusterfs to system calls on the underlying disk filesystems.  
Following internal data structures are discussed as part of the session.
    - inode_t
    - dentry_t
    - fd_t  
3. Xlator interface - [video](https://youtu.be/EnYAzpR336I) - [slides](https://www.slideshare.net/PranithKarampuri/gluster-dev-session-3-xlator-interface)  
This session covers xlator interface in glusterfs which is how all functionality/modules are divided into in glusterfs.  
4. Programming Model - [video](https://youtu.be/tmSpZT2nAVo) - [slides](https://www.slideshare.net/PranithKarampuri/glusterfs-session-4-call-frame-and-programming-model-247038588)  
This session covers programming model used in glusterfs along with the data structures used to achieve it.  
5. inode_t, fd_t lifecycles - [video](https://youtu.be/Sl7ZHYpDe14) - [slides](https://www.slideshare.net/PranithKarampuri/glusterfs-session-5-inode-t-fdt-lifecycles)  
This session covers lifecycles of inode_t and fd_t data structures. Debugging ref leaks for these structures is also briefly covered.  
6. Understanding Gluster's communication layer - [video](https://youtu.be/MkQSWvvNj-c) - [slides](https://www.slideshare.net/PranithKarampuri/gluster-dev-session-6-understanding-glusters-network-communication-layer)  
This session covers Gluster's communication layer. How XDR is used to serialize/deserize data in rpc calls from pov of both client and server xlators.  
7. Client-Server interactions - [video](https://youtu.be/jlQUPZYX3NE) - [slides](https://www.slideshare.net/PranithKarampuri/glusterfs-session-7-client-server-interactions)  
Client, server interactions for connecting/disconnecting/reconnecting are covered in this session  
8. Memory tracking in glusterfs, io-threads xlator - [video](https://youtu.be/0Ymz1ZYK4tc) - [slides](https://www.slideshare.net/PranithKarampuri/glusterfs-session-8-memory-tracking-infra-iothreads)  
