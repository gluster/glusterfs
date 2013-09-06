Steps to recover from File split-brain.
======================================

Quick Start:
============
1. Get the path of the file that is in split-brain:  
>  It can be obtained either by  
>       a) The command `gluster volume heal info split-brain`.  
>       b) Identify the files for which file operations performed
           from the client keep failing with Input/Output error.

2. Close the applications that opened this file from the mount point.
In case of VMs, they need to be powered-off.

3. Decide on the correct copy:  
> This is done by observing the afr changelog extended attributes of the file on
the bricks using the getfattr command; then identifying the type of split-brain 
(data split-brain, metadata split-brain, entry split-brain or split-brain due to
gfid-mismatch); and finally determining which of the bricks contains the 'good copy'
of the file.  
>   `getfattr -d -m . -e hex <file-path-on-brick>`.  
It is also possible that one brick might contain the correct data while the
other might contain the correct metadata.

4. Reset the relevant extended attribute on the brick(s) that contains the
'bad copy' of the file data/metadata using the setfattr command.  
>   `setfattr -n <attribute-name> -v <attribute-value> <file-path-on-brick>`

5. Trigger self-heal on the file by performing lookup from the client:  
>   `ls -l <file-path-on-gluster-mount>`

Detailed Instructions for steps 3 through 5:  
===========================================
To understand how to resolve split-brain we need to know how to interpret the
afr changelog extended attributes.

Execute `getfattr -d -m . -e hex <file-path-on-brick>`

* Example:  
[root@store3 ~]# getfattr -d -e hex -m. brick-a/file.txt  
\#file: brick-a/file.txt  
security.selinux=0x726f6f743a6f626a6563745f723a66696c655f743a733000  
trusted.afr.vol-client-2=0x000000000000000000000000  
trusted.afr.vol-client-3=0x000000000200000000000000  
trusted.gfid=0x307a5c9efddd4e7c96e94fd4bcdcbd1b  

The extended attributes with `trusted.afr.<volname>-client-<subvolume-index>`
are used by afr to maintain changelog of the file.The values of the
`trusted.afr.<volname>-client-<subvolume-index>` are calculated by the glusterfs
client (fuse or nfs-server) processes. When the glusterfs client modifies a file
or directory, the client contacts each brick and updates the changelog extended 
attribute according to the response of the brick.

'subvolume-index' is nothing but (brick number - 1) in
`gluster volume info <volname>` output.

* Example:  
[root@pranithk-laptop ~]# gluster volume info vol  
 Volume Name: vol  
 Type: Distributed-Replicate  
 Volume ID: 4f2d7849-fbd6-40a2-b346-d13420978a01  
 Status: Created  
 Number of Bricks: 4 x 2 = 8  
 Transport-type: tcp  
 Bricks:  
 brick-a: pranithk-laptop:/gfs/brick-a  
 brick-b: pranithk-laptop:/gfs/brick-b  
 brick-c: pranithk-laptop:/gfs/brick-c  
 brick-d: pranithk-laptop:/gfs/brick-d  
 brick-e: pranithk-laptop:/gfs/brick-e  
 brick-f: pranithk-laptop:/gfs/brick-f  
 brick-g: pranithk-laptop:/gfs/brick-g  
 brick-h: pranithk-laptop:/gfs/brick-h  

In the example above:  
```
Brick             |    Replica set        |    Brick subvolume index
----------------------------------------------------------------------------
-/gfs/brick-a     |       0               |       0
-/gfs/brick-b     |       0               |       1
-/gfs/brick-c     |       1               |       2
-/gfs/brick-d     |       1               |       3
-/gfs/brick-e     |       2               |       4
-/gfs/brick-f     |       2               |       5
-/gfs/brick-g     |       3               |       6
-/gfs/brick-h     |       3               |       7
```

Each file in a brick maintains the changelog of itself and that of the files
present in all the other bricks in it's replica set as seen by that brick.

In the example volume given above, all files in brick-a will have 2 entries, 
one for itself and the other for the file present in it's replica pair, i.e.brick-b:  
trusted.afr.vol-client-0=0x000000000000000000000000 -->changelog for itself (brick-a)  
trusted.afr.vol-client-1=0x000000000000000000000000 -->changelog for brick-b as seen by brick-a  

Likewise, all files in brick-b will have:  
trusted.afr.vol-client-0=0x000000000000000000000000 -->changelog for brick-a as seen by brick-b  
trusted.afr.vol-client-1=0x000000000000000000000000 -->changelog for itself (brick-b)  

The same can be extended for other replica pairs.  

Interpreting Changelog (roughly pending operation count) Value:  
Each extended attribute has a value which is 24 hexa decimal digits.  
First 8 digits represent changelog of data. Second 8 digits represent changelog
of metadata. Last 8 digits represent Changelog of directory entries.  

Pictorially representing the same, we have:
```
0x 000003d7 00000001 00000000
        |      |       |
        |      |        \_ changelog of directory entries
        |       \_ changelog of metadata
         \ _ changelog of data
```
         

For Directories metadata and entry changelogs are valid.
For regular files data and metadata changelogs are valid.
For special files like device files etc metadata changelog is valid.
When a file split-brain happens it could be either data split-brain or
meta-data split-brain or both. When a split-brain happens the changelog of the
file would be something like this:  

* Example:(Lets consider both data, metadata split-brain on same file).  
[root@pranithk-laptop vol]# getfattr -d -m . -e hex /gfs/brick-?/a  
getfattr: Removing leading '/' from absolute path names  
\#file: gfs/brick-a/a  
trusted.afr.vol-client-0=0x000000000000000000000000  
trusted.afr.vol-client-1=0x000003d70000000100000000  
trusted.gfid=0x80acdbd886524f6fbefa21fc356fed57   
\#file: gfs/brick-b/a  
trusted.afr.vol-client-0=0x000003b00000000100000000  
trusted.afr.vol-client-1=0x000000000000000000000000  
trusted.gfid=0x80acdbd886524f6fbefa21fc356fed57  

###Observations:

####According to changelog extended attributes on file /gfs/brick-a/a:  
The first 8 digits of trusted.afr.vol-client-0 are all
zeros (0x00000000................), and the first 8 digits of
trusted.afr.vol-client-1 are not all zeros (0x000003d7................).
So the changelog on /gfs/brick-a/a implies that some data operations succeeded
on itself but failed on /gfs/brick-b/a.

The second 8 digits of trusted.afr.vol-client-0 are
all zeros (0x........00000000........), and the second 8 digits of
trusted.afr.vol-client-1 are not all zeros (0x........00000001........).
So the changelog on /gfs/brick-a/a implies that some metadata operations succeeded 
on itself but failed on /gfs/brick-b/a.

####According to Changelog extended attributes on file /gfs/brick-b/a:  
The first 8 digits of trusted.afr.vol-client-0 are not all
zeros (0x000003b0................), and the first 8 digits of
trusted.afr.vol-client-1 are all zeros (0x00000000................).
So the changelog on /gfs/brick-b/a implies that some data operations succeeded
on itself but failed on /gfs/brick-a/a.

The second 8 digits of trusted.afr.vol-client-0 are not
all zeros (0x........00000001........), and the second 8 digits of
trusted.afr.vol-client-1 are all zeros (0x........00000000........).
So the changelog on /gfs/brick-b/a implies that some metadata operations succeeded
on itself but failed on /gfs/brick-a/a.

Since both the copies have data, metadata changes that are not on the other
file, it is in both data and metadata split-brain.

Deciding on the correct copy:  
-----------------------------
The user may have to inspect stat,getfattr output of the files to decide which 
metadata to retain and contents of the file to decide which data to retain.
Continuing with the example above, lets say we want to retain the data
of /gfs/brick-a/a and metadata of /gfs/brick-b/a.

Resetting the relevant changelogs to resolve the split-brain:  
-------------------------------------------------------------
For resolving data-split-brain:  
We need to change the changelog extended attributes on the files as if some data
operations succeeded on /gfs/brick-a/a but failed on /gfs/brick-b/a. But
/gfs/brick-b/a should NOT have any changelog which says some data operations
succeeded on /gfs/brick-b/a but failed on /gfs/brick-a/a. We need to reset the
data part of the changelog on trusted.afr.vol-client-0 of /gfs/brick-b/a.

For resolving metadata-split-brain:  
We need to change the changelog extended attributes on the files as if some
metadata operations succeeded on /gfs/brick-b/a but failed on /gfs/brick-a/a.
But /gfs/brick-a/a should NOT have any changelog which says some metadata
operations succeeded on /gfs/brick-a/a but failed on /gfs/brick-b/a.
We need to reset metadata part of the changelog on
trusted.afr.vol-client-1 of /gfs/brick-a/a

So, the intended changes are:  
On /gfs/brick-b/a:  
For trusted.afr.vol-client-0  
0x000003b00000000100000000 to 0x000000000000000100000000  
(Note that the metadata part is still not all zeros)  
Hence execute
`setfattr -n trusted.afr.vol-client-0 -v 0x000000000000000100000000 /gfs/brick-b/a`

On /gfs/brick-a/a:  
For trusted.afr.vol-client-1  
0x0000000000000000ffffffff to 0x000003d70000000000000000  
(Note that the data part is still not all zeros)  
Hence execute  
`setfattr -n trusted.afr.vol-client-1 -v 0x000003d70000000000000000 /gfs/brick-a/a`

Thus after the above operations are done, the changelogs look like this:  
[root@pranithk-laptop vol]# getfattr -d -m . -e hex /gfs/brick-?/a  
getfattr: Removing leading '/' from absolute path names  
\#file: gfs/brick-a/a  
trusted.afr.vol-client-0=0x000000000000000000000000  
trusted.afr.vol-client-1=0x000003d70000000000000000  
trusted.gfid=0x80acdbd886524f6fbefa21fc356fed57  

\#file: gfs/brick-b/a  
trusted.afr.vol-client-0=0x000000000000000100000000  
trusted.afr.vol-client-1=0x000000000000000000000000  
trusted.gfid=0x80acdbd886524f6fbefa21fc356fed57  


Triggering Self-heal:
---------------------
Perform `ls -l <file-path-on-gluster-mount>` to trigger healing.

Fixing Directory entry split-brain:
----------------------------------
Afr has the ability to conservatively merge different entries in the directories
when there is a split-brain on directory.
If on one brick directory 'd' has entries '1', '2' and has entries '3', '4' on
the other brick then afr will merge all of the entries in the directory to have
'1', '2', '3', '4' entries in the same directory.
(Note: this may result in deleted files to re-appear in case the split-brain
happens because of deletion of files on the directory)
Split-brain resolution needs human intervention when there is at least one entry
which has same file name but different gfid in that directory.
Example:  
On brick-a the directory has entries '1' (with gfid g1), '2' and on brick-b
directory has entries '1' (with gfid g2) and '3'.
These kinds of directory split-brains need human intervention to resolve.
The user needs to remove either file '1' on brick-a or the file '1' on brick-b
to resolve the split-brain. In addition, the corresponding gfid-link file also
needs to be removed.The gfid-link files are present in the .glusterfs folder
in the top-level directory of the brick. If the gfid of the file is
0x307a5c9efddd4e7c96e94fd4bcdcbd1b (the trusted.gfid extended attribute got
from the getfattr command earlier),the gfid-link file can be found at
> /gfs/brick-a/.glusterfs/30/7a/307a5c9efddd4e7c96e94fd4bcdcbd1b

####Word of caution:
Before deleting the gfid-link, we have to ensure that there are no hard links
to the file present on that brick. If hard-links exist,they must be deleted as
well.
