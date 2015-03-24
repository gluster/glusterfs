The following document explains the usage of volume heal info and split-brain
resolution commands.

##`gluster volume heal <VOLNAME> info [split-brain]` commands
###volume heal info
Usage: `gluster volume heal <VOLNAME> info`

This lists all the files that need healing (either their path or
GFID is printed).
###Interpretting the output
All the files that are listed in the output of this command need healing to be
done. Apart from this, there are 2 special cases that may be associated with
an entry -  
a) Is in split-brain  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp; A file in data/metadata split-brain will 
be listed with " - Is in split-brain" appended after its path/gfid. Eg., 
"/file4" in the output provided below. But for a gfid split-brain,
 the parent directory of the file is shown to be in split-brain and the file 
itself is shown to be needing heal. Eg., "/dir" in the output provided below 
which is in split-brain because of gfid split-brain of file "/dir/a".  
b) Is possibly undergoing heal  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp; A file is said to be possibly undergoing
 heal because it is possible that the file was undergoing heal when heal status
was being determined but it cannot be said for sure. It could so have happened
that self-heal daemon and glfsheal process that is trying to get heal information
are competing for the same lock leading to such conclusion. Another possible case
 could be multiple glfsheal processes running simultaneously (e.g., multiple users
 ran heal info command at the same time), competing for same lock.

The following is an example of heal info command's output.
###Example
Consider a replica volume "test" with 2 bricks b1 and b2;
self-heal daemon off, mounted at /mnt.

`gluster volume heal test info`
~~~
Brick \<hostname:brickpath-b1>  
<gfid:aaca219f-0e25-4576-8689-3bfd93ca70c2> - Is in split-brain
<gfid:39f301ae-4038-48c2-a889-7dac143e82dd> - Is in split-brain
<gfid:c3c94de2-232d-4083-b534-5da17fc476ac> - Is in split-brain
<gfid:6dc78b20-7eb6-49a3-8edb-087b90142246> 

Number of entries: 4

Brick <hostname:brickpath-b2>
/dir/file2 
/dir/file1 - Is in split-brain
/dir - Is in split-brain
/dir/file3 
/file4 - Is in split-brain
/dir/a 


Number of entries: 6
~~~

###Analysis of the output
It can be seen that  
A) from brick b1 4 entries need healing:   
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;1) file with gfid:6dc78b20-7eb6-49a3-8edb-087b90142246 needs healing  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;2) "aaca219f-0e25-4576-8689-3bfd93ca70c2",
"39f301ae-4038-48c2-a889-7dac143e82dd" and "c3c94de2-232d-4083-b534-5da17fc476ac"
 are in split-brain

B) from brick b2 6 entries need healing-  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;1) "a", "file2" and "file3" need healing  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;2) "file1", "file4" & "/dir" are in split-brain  

###volume heal info split-brain
Usage: `gluster volume heal <VOLNAME> info split-brain`
This command shows all the files that are in split-brain.
##Example
`gluster volume heal test info split-brain`
~~~
Brick <hostname:brickpath-b1>
<gfid:aaca219f-0e25-4576-8689-3bfd93ca70c2>
<gfid:39f301ae-4038-48c2-a889-7dac143e82dd>
<gfid:c3c94de2-232d-4083-b534-5da17fc476ac>
Number of entries in split-brain: 3

Brick <hostname:brickpath-b2>
/dir/file1
/dir
/file4
Number of entries in split-brain: 3
~~~
Note that, similar to heal info command, for gfid split-brains (same filename but different gfid) 
their parent directories are listed to be in split-brain.

##Resolution of split-brain using CLI
Once the files in split-brain are identified, their resolution can be done
from the command line. Note that entry/gfid split-brain resolution is not supported.  
Split-brain resolution commands let the user resolve split-brain in 3 ways.
###Select the bigger-file as source
This command is useful for per file healing where it is known/decided that the
file with bigger size is to be considered as source.   
1.`gluster volume heal <VOLNAME> split-brain bigger-file <FILE>`  
`<FILE>` can be either the full file name as seen from the root of the volume
(or) the gfid-string representation of the file, which sometimes gets displayed
in the heal info command's output.  
Once this command is executed, the replica containing the FILE with bigger
size is found out and heal is completed with it as source.

###Example :
Consider the above output of heal info split-brain command.

Before healing the file, notice file size and md5 checksums :  
~~~
On brick b1:
# stat b1/dir/file1 
  File: ‘b1/dir/file1’
  Size: 17              Blocks: 16         IO Block: 4096   regular file
Device: fd03h/64771d    Inode: 919362      Links: 2
Access: (0644/-rw-r--r--)  Uid: (    0/    root)   Gid: (    0/    root)
Access: 2015-03-06 13:55:40.149897333 +0530
Modify: 2015-03-06 13:55:37.206880347 +0530
Change: 2015-03-06 13:55:37.206880347 +0530
 Birth: -

# md5sum b1/dir/file1 
040751929ceabf77c3c0b3b662f341a8  b1/dir/file1

On brick b2:
# stat b2/dir/file1 
  File: ‘b2/dir/file1’
  Size: 13              Blocks: 16         IO Block: 4096   regular file
Device: fd03h/64771d    Inode: 919365      Links: 2
Access: (0644/-rw-r--r--)  Uid: (    0/    root)   Gid: (    0/    root)
Access: 2015-03-06 13:54:22.974451898 +0530
Modify: 2015-03-06 13:52:22.910758923 +0530
Change: 2015-03-06 13:52:22.910758923 +0530
 Birth: -
# md5sum b2/dir/file1 
cb11635a45d45668a403145059c2a0d5  b2/dir/file1
~~~
Healing file1 using the above command -  
`gluster volume heal test split-brain bigger-file /dir/file1`  
Healed /dir/file1.

After healing is complete, the md5sum and file size on both bricks should be the same.
~~~
On brick b1:
# stat b1/dir/file1 
  File: ‘b1/dir/file1’
  Size: 17              Blocks: 16         IO Block: 4096   regular file
Device: fd03h/64771d    Inode: 919362      Links: 2
Access: (0644/-rw-r--r--)  Uid: (    0/    root)   Gid: (    0/    root)
Access: 2015-03-06 14:17:27.752429505 +0530
Modify: 2015-03-06 13:55:37.206880347 +0530
Change: 2015-03-06 14:17:12.880343950 +0530
 Birth: -
# md5sum b1/dir/file1 
040751929ceabf77c3c0b3b662f341a8  b1/dir/file1

On brick b2:
# stat b2/dir/file1 
  File: ‘b2/dir/file1’
  Size: 17              Blocks: 16         IO Block: 4096   regular file
Device: fd03h/64771d    Inode: 919365      Links: 2
Access: (0644/-rw-r--r--)  Uid: (    0/    root)   Gid: (    0/    root)
Access: 2015-03-06 14:17:23.249403600 +0530
Modify: 2015-03-06 13:55:37.206880000 +0530
Change: 2015-03-06 14:17:12.881343955 +0530
 Birth: -

# md5sum b2/dir/file1 
040751929ceabf77c3c0b3b662f341a8  b2/dir/file1
~~~
###Select one replica as source for a particular file
2.`gluster volume heal <VOLNAME> split-brain source-brick <HOSTNAME:BRICKNAME> <FILE>`  
`<HOSTNAME:BRICKNAME>` is selected as source brick,
FILE present in the source brick is taken as source for healing.

###Example :
Notice the md5 checksums and file size before and after heal.

Before heal :
~~~
On brick b1:

 stat b1/file4 
  File: ‘b1/file4’
  Size: 4               Blocks: 16         IO Block: 4096   regular file
Device: fd03h/64771d    Inode: 919356      Links: 2
Access: (0644/-rw-r--r--)  Uid: (    0/    root)   Gid: (    0/    root)
Access: 2015-03-06 13:53:19.417085062 +0530
Modify: 2015-03-06 13:53:19.426085114 +0530
Change: 2015-03-06 13:53:19.426085114 +0530
 Birth: -
# md5sum b1/file4
b6273b589df2dfdbd8fe35b1011e3183  b1/file4

On brick b2:

# stat b2/file4 
  File: ‘b2/file4’
  Size: 4               Blocks: 16         IO Block: 4096   regular file
Device: fd03h/64771d    Inode: 919358      Links: 2
Access: (0644/-rw-r--r--)  Uid: (    0/    root)   Gid: (    0/    root)
Access: 2015-03-06 13:52:35.761833096 +0530
Modify: 2015-03-06 13:52:35.769833142 +0530
Change: 2015-03-06 13:52:35.769833142 +0530
 Birth: -
# md5sum b2/file4
0bee89b07a248e27c83fc3d5951213c1  b2/file4
~~~
`gluster volume heal test split-brain source-brick test-host:/test/b1 gfid:c3c94de2-232d-4083-b534-5da17fc476ac`  
Healed gfid:c3c94de2-232d-4083-b534-5da17fc476ac.

After healing :
~~~
On brick b1:
# stat b1/file4 
  File: ‘b1/file4’
  Size: 4               Blocks: 16         IO Block: 4096   regular file
Device: fd03h/64771d    Inode: 919356      Links: 2
Access: (0644/-rw-r--r--)  Uid: (    0/    root)   Gid: (    0/    root)
Access: 2015-03-06 14:23:38.944609863 +0530
Modify: 2015-03-06 13:53:19.426085114 +0530
Change: 2015-03-06 14:27:15.058927962 +0530
 Birth: -
# md5sum b1/file4
b6273b589df2dfdbd8fe35b1011e3183  b1/file4

On brick b2:
# stat b2/file4
 File: ‘b2/file4’
  Size: 4               Blocks: 16         IO Block: 4096   regular file
Device: fd03h/64771d    Inode: 919358      Links: 2
Access: (0644/-rw-r--r--)  Uid: (    0/    root)   Gid: (    0/    root)
Access: 2015-03-06 14:23:38.944609000 +0530
Modify: 2015-03-06 13:53:19.426085000 +0530
Change: 2015-03-06 14:27:15.059927968 +0530
 Birth: -
# md5sum b2/file4
b6273b589df2dfdbd8fe35b1011e3183  b2/file4
~~~
Note that, as mentioned earlier, entry split-brain and gfid split-brain healing
 are not supported using CLI. However, they can be fixed using the method described
 [here](https://github.com/gluster/glusterfs/blob/master/doc/debugging/split-brain.md).
###Example:
Trying to heal /dir would fail as it is in entry split-brain.  
`gluster volume heal test split-brain source-brick test-host:/test/b1 /dir`  
Healing /dir failed:Operation not permitted.  
Volume heal failed.  

3.`gluster volume heal <VOLNAME> split-brain source-brick <HOSTNAME:BRICKNAME>`
Consider a scenario where many files are in split-brain such that one brick of
replica pair is source. As the result of the above command all split-brained
files in `<HOSTNAME:BRICKNAME>` are selected as source and healed to the sink.

###Example:
Consider a volume having three entries "a, b and c" in split-brain.
~~~
`gluster volume heal test split-brain source-brick test-host:/test/b1`
Healed gfid:944b4764-c253-4f02-b35f-0d0ae2f86c0f.
Healed gfid:3256d814-961c-4e6e-8df2-3a3143269ced.
Healed gfid:b23dd8de-af03-4006-a803-96d8bc0df004.
Number of healed entries: 3
~~~

## An overview of working of heal info commands
When these commands are invoked, a "glfsheal" process is spawned which reads 
the entries from `/<brick-path>/.glusterfs/indices/xattrop/` directory of all 
the bricks that are up (that it can connect to) one after another. These 
entries are GFIDs of files that might need healing. Once GFID entries from a 
brick are obtained, based on the lookup response of this file on each 
participating brick of replica-pair & trusted.afr.* extended attributes it is 
found out if the file needs healing, is in split-brain etc based on the 
requirement of each command and displayed to the user.


##Resolution of split-brain from the mount point
A set of getfattr and setfattr commands have been provided to detect the data and metadata split-brain status of a file and resolve split-brain, if any, from mount point.

Consider a volume "test", having bricks b0, b1, b2 and b3.

~~~
# gluster volume info test
 
Volume Name: test
Type: Distributed-Replicate
Volume ID: 00161935-de9e-4b80-a643-b36693183b61
Status: Started
Number of Bricks: 2 x 2 = 4
Transport-type: tcp
Bricks:
Brick1: test-host:/test/b0
Brick2: test-host:/test/b1
Brick3: test-host:/test/b2
Brick4: test-host:/test/b3
~~~

Directory structure of the bricks is as follows:

~~~
# tree -R /test/b?
/test/b0
├── dir
│   └── a
└── file100

/test/b1
├── dir
│   └── a
└── file100

/test/b2
├── dir
├── file1
├── file2
└── file99

/test/b3
├── dir
├── file1
├── file2
└── file99
~~~

Some files in the volume are in split-brain.
~~~
# gluster v heal test info split-brain
Brick test-host:/test/b0/
/file100
/dir
Number of entries in split-brain: 2

Brick test-host:/test/b1/
/file100
/dir
Number of entries in split-brain: 2

Brick test-host:/test/b2/
/file99
<gfid:5399a8d1-aee9-4653-bb7f-606df02b3696>
Number of entries in split-brain: 2

Brick test-host:/test/b3/
<gfid:05c4b283-af58-48ed-999e-4d706c7b97d5>
<gfid:5399a8d1-aee9-4653-bb7f-606df02b3696>
Number of entries in split-brain: 2
~~~
###To know data/metadata split-brain status of a file:
~~~
getfattr -n replica.split-brain-status <path-to-file>
~~~
The above command executed from mount provides information if a file is in data/metadata split-brain. Also provides the list of afr children to analyze to get more information about the file.
This command is not applicable to gfid/directory split-brain.

###Example:
1) "file100" is in metadata split-brain. Executing the above mentioned command for file100 gives :
~~~
# getfattr -n replica.split-brain-status file100
# file: file100
replica.split-brain-status="data-split-brain:no    metadata-split-brain:yes    Choices:test-client-0,test-client-1"
~~~

2) "file1" is in data split-brain.
~~~
# getfattr -n replica.split-brain-status file1
# file: file1
replica.split-brain-status="data-split-brain:yes    metadata-split-brain:no    Choices:test-client-2,test-client-3"
~~~

3) "file99" is in both data and metadata split-brain.
~~~
# getfattr -n replica.split-brain-status file99
# file: file99
replica.split-brain-status="data-split-brain:yes    metadata-split-brain:yes    Choices:test-client-2,test-client-3"
~~~

4) "dir" is in directory split-brain but as mentioned earlier, the above command is not applicable to such split-brain. So it says that the file is not under data or metadata split-brain.
~~~
# getfattr -n replica.split-brain-status dir
# file: dir
replica.split-brain-status="The file is not under data or metadata split-brain"
~~~

5) "file2" is not in any kind of split-brain.
~~~
# getfattr -n replica.split-brain-status file2
# file: file2
replica.split-brain-status="The file is not under data or metadata split-brain"
~~~

### To analyze the files in data and metadata split-brain
Trying to do operations (say cat, getfattr etc) from the mount on files in split-brain, gives an input/output error. To enable the users analyze such files, a setfattr command is provided.

~~~
# setfattr -n replica.split-brain-choice -v "choiceX" <path-to-file>
~~~
Using this command, a particular brick can be chosen to access the file in split-brain from.

###Example:
1) "file1" is in data-split-brain. Trying to read from the file gives input/output error.
~~~
# cat file1
cat: file1: Input/output error
~~~
Split-brain choices provided for file1 were test-client-2 and test-client-3.

Setting test-client-2 as split-brain choice for file1 serves reads from b2 for the file.
~~~
# setfattr -n replica.split-brain-choice -v test-client-2 file1
~~~
Now, read operations on the file can be done.
~~~
# cat file1
xyz
~~~
Similarly, to inspect the file from other choice, replica.split-brain-choice is to be set to test-client-3.

Trying to inspect the file from a wrong choice errors out.

To undo the split-brain-choice that has been set, the above mentioned setfattr command can be used 
with "none" as the value for extended attribute.

###Example:
~~~
1) setfattr -n replica.split-brain-choice -v none file1
~~~
Now performing cat operation on the file will again result in input/output error, as before.
~~~
# cat file
cat: file1: Input/output error
~~~

Once the choice for resolving split-brain is made, source brick is supposed to be set for the healing to be done.
This is done using the following command:

~~~
#  setfattr -n replica.split-brain-heal-finalize -v <heal-choice> <path-to-file>
~~~

##Example
~~~
# setfattr -n replica.split-brain-heal-finalize -v test-client-2 file1
~~~
The above process can be used to resolve data and/or metadata split-brain on all the files.

NOTE:  
1) If "fopen-keep-cache" fuse mount option is disabled then inode needs to be invalidated each time before selecting a new replica.split-brain-choice to inspect a file. This can be done by using:
~~~
# sefattr -n inode-invalidate -v 0 <path-to-file>
~~~

2) The above mentioned process for split-brain resolution from mount will not work on nfs mounts as it doesn't provide xattrs support.
