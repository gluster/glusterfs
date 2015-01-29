Introduction
========
The tool gfind_missing_files.sh can be used to find the missing files in a
GlusterFS geo-replicated slave volume. The tool uses a multi-threaded crawler
operating on the backend .glusterfs of a brickpath which is passed as one of
the parameters to the tool. It does a stat on each entry in the slave volume
mount to check for the presence of a file. The tool uses the aux-gfid-mount
thereby avoiding path conversions and potentially saving time.

This tool should be run on every node and each brickpath in a geo-replicated
master volume to find the missing files on the slave volume.

The script gfind_missing_files.sh is a wrapper script that in turn uses the
gcrawler binary to do the backend crawling. The script detects the gfids of
the missing files and runs the gfid-to-path conversion script to list out the
missing files with their full pathnames.

Usage
=====
```sh
$bash gfind_missing_files.sh <BRICK_PATH> <SLAVE_HOST> <SLAVE_VOL> <OUTFILE>
            BRICK_PATH -   Full path of the brick
            SLAVE_HOST -   Hostname of gluster volume
            SLAVE_VOL  -   Gluster volume name
            OUTFILE   -    Output file which contains gfids of the missing files
```

The gfid-to-path conversion uses a quicker algorithm for converting gfids to
paths and it is possible that in some cases all missing gfids may not be
converted to their respective paths.

Example output(126733 missing files)
===================================
```sh
$ionice -c 2 -n 7 ./gfind_missing_files.sh /bricks/m3 acdc slave-vol ~/test_results/m3-4.txt
Calling crawler...
Crawl Complete.
gfids of skipped files are available in the file /root/test_results/m3-4.txt
Starting gfid to path conversion
Path names of skipped files are available in the file /root/test_results/m3-4.txt_pathnames
WARNING: Unable to convert some GFIDs to Paths, GFIDs logged to /root/test_results/m3-4.txt_gfids
Use bash gfid_to_path.sh <brick-path> /root/test_results/m3-4.txt_gfids to convert those GFIDs to Path
Total Missing File Count : 126733
```
In such cases, an additional step is needed to convert those gfids to paths.
This can be used as shown below:
```sh
 $bash gfid_to_path.sh <BRICK_PATH> <GFID_FILE>
             BRICK_PATH - Full path of the brick.
             GFID_FILE  - OUTFILE_gfids got from gfind_missing_files.sh
```
Things to keep in mind when running the tool
============================================
1. Running this tool can result in a crawl of the backend filesystem at each
   brick which can be intensive. To ensure there is no impact on ongoing I/O on
   RHS volumes, we recommend that this tool be run at a low I/O scheduling class
   (best-effort) and priority.
```sh
$ionice -c 2 -p <pid of gfind_missing_files.sh>
```

2. We do not recommend interrupting the tool when it is running
   (e.g. by doing CTRL^C). It is better to wait for the tool to finish
    execution. In case it is interupted, manually unmount the Slave Volume.
```sh
    umount <MOUNT_POINT>
```
