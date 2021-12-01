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
    - Memory tracking infra and how statedumps are used to debug memory leaks is covered  
    - io-threads xlator implementation is covered.  
9. Index xlator - [video](https://youtu.be/WYQKsNYXmrM) - [slides](https://www.slideshare.net/PranithKarampuri/glusterfs-session-9-index-xlator)  
Index xlator design and implementation are covered in this session.  
10. Locks xlator inodelks - [video](https://youtu.be/1AIMbxmAKwc) - [slides](https://www.slideshare.net/PranithKarampuri/glusterfs-session-10-locks-xlator-inodelks)  
    - Gives brief introduction about the purpose of this xlator  
    - Introduces and code walkthrough of inodelk part of locks xlator  
11. Locks xlator entrylks - [video](https://youtu.be/BCgm5hNWFbE) - [slides](https://www.slideshare.net/PranithKarampuri/glusterfs-session-11-locks-xlator-entrylks)  
    - Introduces and code walkthrough of entrylk part of locks xlator  
    - Explains the connection between entrylk and inodelk in deletion code paths  
12. Locks xlator posixlk - [video](https://youtu.be/E7J_W50iRDw) - [slides](https://www.slideshare.net/PranithKarampuri/glusterfs-session-12-locks-xlator-posixlk)
    - What is the behavior of posixlks
    - Code walkthrough
13. Replication xlator introduction - [video](https://youtu.be/cW4CHLHf_jY) - [slides](https://www.slideshare.net/PranithKarampuri/glusterfs-session-13-replication-introduction)  
    - Replicate xlator graph placement  
    - On-disk data representation  
    - On-disk data manipulation with xattrop  
    - Common functioning of fops  
    - Lookup  
    - Open  
    - readdir/readdirp  
    - Flush  
    - Statfs  
14. Replication update transaction - [video](https://youtu.be/ku6nF7WWHh8) - [slides](https://www.slideshare.net/PranithKarampuri/glusterfs-session-14-replication-update-fops)  
    - Update transaction  
    - Data/Metadata operation  
    - Optimizations to reduce the network communication  
15. Replication optimizations & Read transaction - [video](https://youtu.be/dZq8J_bsDAY) - [slides](https://www.slideshare.net/PranithKarampuri/glusterfs-session-15-replication-read-transactions)  
    - read transaction  
    - Handling of faults by the transaction  
    - Load balancing strategies  
16. Self heal daemon of replication - [video](https://youtu.be/CnDw1uosGiI) - [slides](https://www.slideshare.net/PranithKarampuri/glusterfs-session-16-selfheal-daemon-for-replication)
    - Intro to self-heal daemon (shd)
    - Types of crawls in shd
    - Code walkthrough
17. Self heal daemon - data, metadata, entry self-heals - [video](https://youtu.be/aiBFO_ggSVA) - [slides](https://www.slideshare.net/PranithKarampuri/glusterfs-session-17-self-heal-daemon-data-metadata-entry-healing)
    - Types of heal needed for a given file/directory
    - Code walkthrough of data, metadata, entry self-heals
18. Intro to FUSE and its trade offs - [video](https://youtu.be/str7pR9sF-E) - [slides](https://www.slideshare.net/PranithKarampuri/glusterfs-session-18-intro-to-fuse-and-its-trade-offs)  
    - various parts of fuse code in the glusterfs source tree
    - the story FUSE version macros tell
    - the tale of FUSE and fuse (historical context, terminology)
    - to libfuse or not to libfuse?
    - FUSE proto breakdown
    - mount and INIT
