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
