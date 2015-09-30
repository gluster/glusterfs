##gluster volume heal <volume-name> statistics

##Description
In case of index self-heal, self-heal daemon reads the entries from the
local bricks, from /brick-path/.glusterfs/indices/xattrop/ directory.
So based on the entries read by self heal daemon, it will attempt self-heal.
Executing this command will list the  crawl statistics of self heal done for
each brick.

For each brick, it will list:
1. Starting time of crawl done for that brick.
2. Ending time of crawl done for that brick.
3. No of entries for which self-heal is successfully attempted.
4. No of split-brain entries found while self-healing.
5. No of entries for which heal failed.



Example:
a) Create a gluster volume with replica count 2.
b) Create 10 files.
c) kill brick_1 of this replica.
d) Overwrite all 10 files.
e) Kill the other brick (brick_2), and bring back (brick_1) up.
f) Overwrite all 10 files.

Now we have 10 files, which are in split brain. Self heal daemon will crawl for
both the bricks, and will count 10 files from each brick.
It will report 10 files under split-brain with respect to given brick.

Gathering crawl statistics on volume volume1 has been successful
------------------------------------------------

Crawl statistics for brick no 0
Hostname of brick 192.168.122.1

Starting time of crawl: Tue May 20 19:13:11 2014

Ending time of crawl: Tue May 20 19:13:12 2014

Type of crawl: INDEX
No. of entries healed: 0
No. of entries in split-brain: 10
No. of heal failed entries: 0
------------------------------------------------

Crawl statistics for brick no 1
Hostname of brick 192.168.122.1

Starting time of crawl: Tue May 20 19:13:12 2014

Ending time of crawl: Tue May 20 19:13:12 2014

Type of crawl: INDEX
No. of entries healed: 0
No. of entries in split-brain: 10
No. of heal failed entries: 0

------------------------------------------------


As the output shows, self-heal daemon detects 10 files in split-brain with
resept to given brick.




##gluster volume heal <volume-name> statistics heal-count
It lists the number of entries present in
/<brick>/.glusterfs/indices/xattrop from each-brick.


1. Create a replicate volume.
2. Kill one brick of a replicate volume1.
3. Create 10 files.
4. Execute above command.

--------------------------------------------------------------------------------
Gathering count of entries to be healed on volume volume1 has been successful

Brick 192.168.122.1:/brick_1
Number of entries: 10

Brick 192.168.122.1:/brick_2
No gathered input for this brick
-------------------------------------------------------------------------------






##gluster volume heal <volume-name> statistics heal-count replica \
  ip_addr:/brick_location

To list the number of entries to be healed from a particular replicate
subvolume, listing any one child of that replicate subvolume in the command,
will list the entries for all the childrens of that replicate subvolume.

Example:           dht
             /              \
            /                \
         replica-1         replica-2
         /    \               /    \
     child-1  child-2   child-3   child-4
    /brick1   /brick2   /brick3   /brick4

gluster volume heal <vol-name> statistics heal-count ip:/brick1
will list  count only for child-1 and child-2.

gluster volume heal <vol-name> statistics heal-count ip:/brick3
will list count only for child-3 and child-4.



1. Create a volume same as mentioned in the above graph.
2. Kill Brick-2.
3. Create some files.
4. If we are interested in knowing the number of files to be healed from each
   brick of replica-1 only, mention any one child of that replica.

gluster volume heal volume1 statistics heal-count replica 192.168.122.1:/brick2

output:
-------
Gathering count of entries to be healed per replica on volume volume1 has \
been successful

Brick 192.168.122.1:/brick_1
Number of entries: 10                                <--10 files

Brick 192.168.122.1:/brick_2
No gathered input for this brick                     <-Brick is down

Brick 192.168.122.1:/brick_3
No gathered input for this brick                     <--No result, as we are not
                                                        interested.

Brick 192.168.122.1:/brick_4                         <--No result, as we are not
No gathered input for this brick                        interested.


