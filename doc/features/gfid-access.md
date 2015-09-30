#Gfid-access Translator
The 'gfid-access' translator provides access to data in glusterfs using a
virtual path. This particular translator is designed to provide direct access to
files in glusterfs using its gfid. 'GFID' is glusterfs's inode number for a file
to identify it uniquely. As of now, Geo-replication is the only consumer of this
translator. The changelog translator logs the 'gfid' with corresponding file
operation in journals which are consumed by Geo-Replication to replicate the
files using gfid-access translator very efficiently.

###Implications and Usage
A new virtual directory called '.gfid' is exposed in the aux-gfid mount
point when gluster volume is mounted with 'aux-gfid-mount' option.
All the gfids of files are exposed in one level under the '.gfid' directory.
No matter at what level the file resides, it is accessed using its
gfid under this virutal directory as shown in example below. All access
protocols work seemlessly, as the complexities are handled internally.

###Testing
1. Mount glusterfs client with '-o aux-gfid-mount' as follows.

      mount -t glusterfs -o aux-gfid-mount <node-ip>:<volname> <mountpoint>

      Example:

        #mount -t glusterfs -o aux-gfid-mount rhs1:master /master-aux-mnt

2. Get the 'gfid' of a file using normal mount or aux-gfid-mount and do some
   operations as follows.

      getfattr -n glusterfs.gfid.string <file>

      Example:

        #getfattr -n glusterfs.gfid.string  /master-aux-mnt/file
        # file: file
        glusterfs.gfid.string="796d3170-0910-4853-9ff3-3ee6b1132080"

        #cat /master-aux-mnt/file
        sample data

        #stat /master-aux-mnt/file
           File: `file'
           Size: 12             Blocks: 1          IO Block: 131072 regular file
         Device: 13h/19d        Inode: 11525625031905452160  Links: 1
         Access: (0644/-rw-r--r--)  Uid: (    0/    root)   Gid: (    0/   root)
         Access: 2014-05-23 20:43:33.239999863 +0530
         Modify: 2014-05-23 17:36:48.224999989 +0530
         Change: 2014-05-23 20:44:10.081999938 +0530


3. Access files using virtual path as follows.

      /mountpoint/.gfid/<actual-canonical-gfid-of-the-file\>'

      Example:

        #cat /master-aux-mnt/.gfid/796d3170-0910-4853-9ff3-3ee6b1132080
        sample data
        #stat /master-aux-mnt/.gfid/796d3170-0910-4853-9ff3-3ee6b1132080
           File: `.gfid/796d3170-0910-4853-9ff3-3ee6b1132080'
           Size: 12             Blocks: 1          IO Block: 131072 regular file
         Device: 13h/19d        Inode: 11525625031905452160  Links: 1
         Access: (0644/-rw-r--r--)  Uid: (    0/    root)   Gid: (    0/   root)
         Access: 2014-05-23 20:43:33.239999863 +0530
         Modify: 2014-05-23 17:36:48.224999989 +0530
         Change: 2014-05-23 20:44:10.081999938 +0530

   We can notice that 'cat' command on the 'file' using path and using virtual
   path displays the same data. Similarly 'stat' command on the 'file' and using
   virtual path with gfid gives same Inode Number confirming that its same file.

###Nature of changes
This feature is introduced with 'gfid-access' translator.
