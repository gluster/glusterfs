#Monitoring your GlusterFS Workload

You can monitor the GlusterFS volumes on different parameters.
Monitoring volumes helps in capacity planning and performance tuning
tasks of the GlusterFS volume. Using these information, you can identify
and troubleshoot issues.

You can use Volume Top and Profile commands to view the performance and
identify bottlenecks/hotspots of each brick of a volume. This helps
system administrators to get vital performance information whenever
performance needs to be probed.

You can also perform statedump of the brick processes and nfs server
process of a volume, and also view volume status and volume information.

##Running GlusterFS Volume Profile Command

GlusterFS Volume Profile command provides an interface to get the
per-brick I/O information for each File Operation (FOP) of a volume. The
per brick information helps in identifying bottlenecks in the storage
system.

This section describes how to run GlusterFS Volume Profile command by
performing the following operations:

-   [Start Profiling](#start-profiling)
-   [Displaying the I/0 Information](#displaying-io)
-   [Stop Profiling](#stop-profiling)

<a name="start-profiling" />
###Start Profiling

You must start the Profiling to view the File Operation information for
each brick.

To start profiling, use following command:

`# gluster volume profile  start `

For example, to start profiling on test-volume:

    # gluster volume profile test-volume start
    Profiling started on test-volume

When profiling on the volume is started, the following additional
options are displayed in the Volume Info:

    diagnostics.count-fop-hits: on
    diagnostics.latency-measurement: on

<a name="displaying-io" />
###Displaying the I/0 Information

You can view the I/O information of each brick by using the following command:

`# gluster volume profile  info`

For example, to see the I/O information on test-volume:

    # gluster volume profile test-volume info
    Brick: Test:/export/2
    Cumulative Stats:

    Block                     1b+           32b+           64b+
    Size:
           Read:                0              0              0
           Write:             908             28              8

    Block                   128b+           256b+         512b+
    Size:
           Read:                0               6             4
           Write:               5              23            16

    Block                  1024b+          2048b+        4096b+
    Size:
           Read:                 0              52           17
           Write:               15             120          846

    Block                   8192b+         16384b+      32768b+
    Size:
           Read:                52               8           34
           Write:              234             134          286

    Block                                  65536b+     131072b+
    Size:
           Read:                               118          622
           Write:                             1341          594


    %-latency  Avg-      Min-       Max-       calls     Fop
              latency   Latency    Latency  
    ___________________________________________________________
    4.82      1132.28   21.00      800970.00   4575    WRITE
    5.70       156.47    9.00      665085.00   39163   READDIRP
    11.35      315.02    9.00     1433947.00   38698   LOOKUP
    11.88     1729.34   21.00     2569638.00    7382   FXATTROP
    47.35   104235.02 2485.00     7789367.00     488   FSYNC

    ------------------

    ------------------

    Duration     : 335

    BytesRead    : 94505058

    BytesWritten : 195571980

<a name="stop-profiling" />
###Stop Profiling

You can stop profiling the volume, if you do not need profiling
information anymore.

Stop profiling using the following command:

    `# gluster volume profile  stop`

For example, to stop profiling on test-volume:

    `# gluster volume profile  stop`

    `Profiling stopped on test-volume`

##Running GlusterFS Volume TOP Command

GlusterFS Volume Top command allows you to view the glusterfs bricks’
performance metrics like read, write, file open calls, file read calls,
file write calls, directory open calls, and directory real calls. The
top command displays up to 100 results.

This section describes how to run and view the results for the following
GlusterFS Top commands:

-   [Viewing Open fd Count and Maximum fd Count](#open-fd-count)
-   [Viewing Highest File Read Calls](#file-read)
-   [Viewing Highest File Write Calls](#file-write)
-   [Viewing Highest Open Calls on Directories](#open-dir)
-   [Viewing Highest Read Calls on Directory](#read-dir)
-   [Viewing List of Read Performance on each Brick](#read-perf)
-   [Viewing List of Write Performance on each Brick](#write-perf)

<a name="open-fd-count" />
###Viewing Open fd Count and Maximum fd Count

You can view both current open fd count (list of files that are
currently the most opened and the count) on the brick and the maximum
open fd count (count of files that are the currently open and the count
of maximum number of files opened at any given point of time, since the
servers are up and running). If the brick name is not specified, then
open fd metrics of all the bricks belonging to the volume will be
displayed.

-   View open fd count and maximum fd count using the following command:

    `# gluster volume top  open [brick ] [list-cnt ]`

    For example, to view open fd count and maximum fd count on brick
    server:/export of test-volume and list top 10 open calls:

    `# gluster volume top  open brick  list-cnt `

    `Brick: server:/export/dir1 `

    `Current open fd's: 34 Max open fd's: 209 `

                     ==========Open file stats========

        open            file name
        call count     

        2               /clients/client0/~dmtmp/PARADOX/
                        COURSES.DB

        11              /clients/client0/~dmtmp/PARADOX/
                        ENROLL.DB

        11              /clients/client0/~dmtmp/PARADOX/
                        STUDENTS.DB

        10              /clients/client0/~dmtmp/PWRPNT/
                        TIPS.PPT

        10              /clients/client0/~dmtmp/PWRPNT/
                        PCBENCHM.PPT

        9               /clients/client7/~dmtmp/PARADOX/
                        STUDENTS.DB

        9               /clients/client1/~dmtmp/PARADOX/
                        STUDENTS.DB

        9               /clients/client2/~dmtmp/PARADOX/
                        STUDENTS.DB

        9               /clients/client0/~dmtmp/PARADOX/
                        STUDENTS.DB

        9               /clients/client8/~dmtmp/PARADOX/
                        STUDENTS.DB

<a name="file-read" />
###Viewing Highest File Read Calls

You can view highest read calls on each brick. If brick name is not
specified, then by default, list of 100 files will be displayed.

-   View highest file Read calls using the following command:

    `# gluster volume top  read [brick ] [list-cnt ] `

    For example, to view highest Read calls on brick server:/export of
    test-volume:

    `# gluster volume top  read brick  list-cnt `

    `Brick:` server:/export/dir1

                  ==========Read file stats========

        read              filename
        call count

        116              /clients/client0/~dmtmp/SEED/LARGE.FIL

        64               /clients/client0/~dmtmp/SEED/MEDIUM.FIL

        54               /clients/client2/~dmtmp/SEED/LARGE.FIL

        54               /clients/client6/~dmtmp/SEED/LARGE.FIL

        54               /clients/client5/~dmtmp/SEED/LARGE.FIL

        54               /clients/client0/~dmtmp/SEED/LARGE.FIL

        54               /clients/client3/~dmtmp/SEED/LARGE.FIL

        54               /clients/client4/~dmtmp/SEED/LARGE.FIL

        54               /clients/client9/~dmtmp/SEED/LARGE.FIL

        54               /clients/client8/~dmtmp/SEED/LARGE.FIL

<a name="file-write" />
###Viewing Highest File Write Calls

You can view list of files which has highest file write calls on each
brick. If brick name is not specified, then by default, list of 100
files will be displayed.

-   View highest file Write calls using the following command:

    `# gluster volume top  write [brick ] [list-cnt ] `

    For example, to view highest Write calls on brick server:/export of
    test-volume:

    `# gluster volume top  write brick  list-cnt `

    `Brick: server:/export/dir1 `

                       ==========Write file stats========
        write call count   filename

        83                /clients/client0/~dmtmp/SEED/LARGE.FIL

        59                /clients/client7/~dmtmp/SEED/LARGE.FIL

        59                /clients/client1/~dmtmp/SEED/LARGE.FIL

        59                /clients/client2/~dmtmp/SEED/LARGE.FIL

        59                /clients/client0/~dmtmp/SEED/LARGE.FIL

        59                /clients/client8/~dmtmp/SEED/LARGE.FIL

        59                /clients/client5/~dmtmp/SEED/LARGE.FIL

        59                /clients/client4/~dmtmp/SEED/LARGE.FIL

        59                /clients/client6/~dmtmp/SEED/LARGE.FIL

        59                /clients/client3/~dmtmp/SEED/LARGE.FIL

<a name="open-dir" />
###Viewing Highest Open Calls on Directories

You can view list of files which has highest open calls on directories
of each brick. If brick name is not specified, then the metrics of all
the bricks belonging to that volume will be displayed.

-   View list of open calls on each directory using the following
    command:

    `# gluster volume top  opendir [brick ] [list-cnt ] `

    For example, to view open calls on brick server:/export/ of
    test-volume:

    `# gluster volume top  opendir brick  list-cnt `

    `Brick: server:/export/dir1 `

                 ==========Directory open stats========

        Opendir count     directory name

        1001              /clients/client0/~dmtmp

        454               /clients/client8/~dmtmp

        454               /clients/client2/~dmtmp
         
        454               /clients/client6/~dmtmp

        454               /clients/client5/~dmtmp

        454               /clients/client9/~dmtmp

        443               /clients/client0/~dmtmp/PARADOX

        408               /clients/client1/~dmtmp

        408               /clients/client7/~dmtmp

        402               /clients/client4/~dmtmp

<a name="read-dir" />
###Viewing Highest Read Calls on Directory

You can view list of files which has highest directory read calls on
each brick. If brick name is not specified, then the metrics of all the
bricks belonging to that volume will be displayed.

-   View list of highest directory read calls on each brick using the
    following command:

    `# gluster volume top  readdir [brick ] [list-cnt ] `

    For example, to view highest directory read calls on brick
    server:/export of test-volume:

    `# gluster volume top  readdir brick  list-cnt `

    `Brick: `

        ==========Directory readdirp stats========

        readdirp count           directory name

        1996                    /clients/client0/~dmtmp

        1083                    /clients/client0/~dmtmp/PARADOX

        904                     /clients/client8/~dmtmp

        904                     /clients/client2/~dmtmp

        904                     /clients/client6/~dmtmp

        904                     /clients/client5/~dmtmp

        904                     /clients/client9/~dmtmp

        812                     /clients/client1/~dmtmp

        812                     /clients/client7/~dmtmp

        800                     /clients/client4/~dmtmp

<a name="read-perf" />
###Viewing List of Read Performance on each Brick

You can view the read throughput of files on each brick. If brick name
is not specified, then the metrics of all the bricks belonging to that
volume will be displayed. The output will be the read throughput.

           ==========Read throughput file stats========

    read         filename                         Time
    through
    put(MBp
    s)
     
    2570.00    /clients/client0/~dmtmp/PWRPNT/      -2011-01-31
               TRIDOTS.POT                      15:38:36.894610  
    2570.00    /clients/client0/~dmtmp/PWRPNT/      -2011-01-31
               PCBENCHM.PPT                     15:38:39.815310                                                             
    2383.00    /clients/client2/~dmtmp/SEED/        -2011-01-31
               MEDIUM.FIL                       15:52:53.631499
                                                  
    2340.00    /clients/client0/~dmtmp/SEED/        -2011-01-31
               MEDIUM.FIL                       15:38:36.926198
                                                  
    2299.00   /clients/client0/~dmtmp/SEED/         -2011-01-31
              LARGE.FIL                         15:38:36.930445
                                                 
    2259.00   /clients/client0/~dmtmp/PARADOX/      -2011-01-31
              COURSES.X04                       15:38:40.549919

    2221.00   /clients/client0/~dmtmp/PARADOX/      -2011-01-31
              STUDENTS.VAL                      15:52:53.298766
                                                  
    2221.00   /clients/client3/~dmtmp/SEED/         -2011-01-31
              COURSES.DB                        15:39:11.776780

    2184.00   /clients/client3/~dmtmp/SEED/         -2011-01-31
              MEDIUM.FIL                        15:39:10.251764
                                                  
    2184.00   /clients/client5/~dmtmp/WORD/         -2011-01-31
              BASEMACH.DOC                      15:39:09.336572                                          

This command will initiate a dd for the specified count and block size
and measures the corresponding throughput.

-   View list of read performance on each brick using the following
    command:

    `# gluster volume top  read-perf [bs  count ] [brick ] [list-cnt ]`

    For example, to view read performance on brick server:/export/ of
    test-volume, 256 block size of count 1, and list count 10:

    `# gluster volume top  read-perf bs 256 count 1 brick list-cnt `

    `Brick: server:/export/dir1 256 bytes (256 B) copied, Throughput: 4.1 MB/s `

               ==========Read throughput file stats========

        read         filename                         Time
        through
        put(MBp
        s)

        2912.00   /clients/client0/~dmtmp/PWRPNT/    -2011-01-31
                   TRIDOTS.POT                   15:38:36.896486
                                                   
        2570.00   /clients/client0/~dmtmp/PWRPNT/    -2011-01-31
                   PCBENCHM.PPT                  15:38:39.815310
                                                   
        2383.00   /clients/client2/~dmtmp/SEED/      -2011-01-31
                   MEDIUM.FIL                    15:52:53.631499
                                                   
        2340.00   /clients/client0/~dmtmp/SEED/      -2011-01-31
                   MEDIUM.FIL                    15:38:36.926198

        2299.00   /clients/client0/~dmtmp/SEED/      -2011-01-31
                   LARGE.FIL                     15:38:36.930445
                                                              
        2259.00  /clients/client0/~dmtmp/PARADOX/    -2011-01-31
                  COURSES.X04                    15:38:40.549919
                                                   
        2221.00  /clients/client9/~dmtmp/PARADOX/    -2011-01-31
                  STUDENTS.VAL                   15:52:53.298766
                                                   
        2221.00  /clients/client8/~dmtmp/PARADOX/    -2011-01-31
                 COURSES.DB                      15:39:11.776780
                                                   
        2184.00  /clients/client3/~dmtmp/SEED/       -2011-01-31
                  MEDIUM.FIL                     15:39:10.251764
                                                   
        2184.00  /clients/client5/~dmtmp/WORD/       -2011-01-31
                 BASEMACH.DOC                    15:39:09.336572
                                                   
<a name="write-perf" />
###Viewing List of Write Performance on each Brick

You can view list of write throughput of files on each brick. If brick
name is not specified, then the metrics of all the bricks belonging to
that volume will be displayed. The output will be the write throughput.

This command will initiate a dd for the specified count and block size
and measures the corresponding throughput. To view list of write
performance on each brick:

-   View list of write performance on each brick using the following
    command:

    `# gluster volume top  write-perf [bs  count ] [brick ] [list-cnt ] `

    For example, to view write performance on brick server:/export/ of
    test-volume, 256 block size of count 1, and list count 10:

    `# gluster volume top  write-perf bs 256 count 1 brick list-cnt `

    `Brick`: server:/export/dir1

    `256 bytes (256 B) copied, Throughput: 2.8 MB/s `

               ==========Write throughput file stats========

        write                filename                 Time
        throughput
        (MBps)
         
        1170.00    /clients/client0/~dmtmp/SEED/     -2011-01-31
                   SMALL.FIL                     15:39:09.171494

        1008.00    /clients/client6/~dmtmp/SEED/     -2011-01-31
                   LARGE.FIL                      15:39:09.73189

        949.00    /clients/client0/~dmtmp/SEED/      -2011-01-31
                  MEDIUM.FIL                     15:38:36.927426

        936.00   /clients/client0/~dmtmp/SEED/       -2011-01-31
                 LARGE.FIL                        15:38:36.933177    
        897.00   /clients/client5/~dmtmp/SEED/       -2011-01-31
                 MEDIUM.FIL                       15:39:09.33628

        897.00   /clients/client6/~dmtmp/SEED/       -2011-01-31
                 MEDIUM.FIL                       15:39:09.27713

        885.00   /clients/client0/~dmtmp/SEED/       -2011-01-31
                  SMALL.FIL                      15:38:36.924271

        528.00   /clients/client5/~dmtmp/SEED/       -2011-01-31
                 LARGE.FIL                        15:39:09.81893

        516.00   /clients/client6/~dmtmp/ACCESS/    -2011-01-31
                 FASTENER.MDB                    15:39:01.797317

##Displaying Volume Information

You can display information about a specific volume, or all volumes, as
needed.

-   Display information about a specific volume using the following
    command:

    `# gluster volume info ``VOLNAME`

    For example, to display information about test-volume:

        # gluster volume info test-volume
        Volume Name: test-volume
        Type: Distribute
        Status: Created
        Number of Bricks: 4
        Bricks:
        Brick1: server1:/exp1
        Brick2: server2:/exp2
        Brick3: server3:/exp3
        Brick4: server4:/exp4

-   Display information about all volumes using the following command:

    `# gluster volume info all`

        # gluster volume info all

        Volume Name: test-volume
        Type: Distribute
        Status: Created
        Number of Bricks: 4
        Bricks:
        Brick1: server1:/exp1
        Brick2: server2:/exp2
        Brick3: server3:/exp3
        Brick4: server4:/exp4

        Volume Name: mirror
        Type: Distributed-Replicate
        Status: Started
        Number of Bricks: 2 X 2 = 4
        Bricks:
        Brick1: server1:/brick1
        Brick2: server2:/brick2
        Brick3: server3:/brick3
        Brick4: server4:/brick4

        Volume Name: Vol
        Type: Distribute
        Status: Started
        Number of Bricks: 1
        Bricks:
        Brick: server:/brick6

##Performing Statedump on a Volume

Statedump is a mechanism through which you can get details of all
internal variables and state of the glusterfs process at the time of
issuing the command.You can perform statedumps of the brick processes
and nfs server process of a volume using the statedump command. The
following options can be used to determine what information is to be
dumped:

-   **mem** - Dumps the memory usage and memory pool details of the
    bricks.

-   **iobuf** - Dumps iobuf details of the bricks.

-   **priv** - Dumps private information of loaded translators.

-   **callpool** - Dumps the pending calls of the volume.

-   **fd** - Dumps the open fd tables of the volume.

-   **inode** - Dumps the inode tables of the volume.

**To display volume statedump**

-   Display statedump of a volume or NFS server using the following
    command:

    `# gluster volume statedump  [nfs] [all|mem|iobuf|callpool|priv|fd|inode]`

    For example, to display statedump of test-volume:

        # gluster volume statedump test-volume
        Volume statedump successful

    The statedump files are created on the brick servers in the` /tmp`
    directory or in the directory set using `server.statedump-path`
    volume option. The naming convention of the dump file is
    `<brick-path>.<brick-pid>.dump`.

-   By defult, the output of the statedump is stored at
    ` /tmp/<brickname.PID.dump>` file on that particular server. Change
    the directory of the statedump file using the following command:

    `# gluster volume set  server.statedump-path `

    For example, to change the location of the statedump file of
    test-volume:

        # gluster volume set test-volume server.statedump-path /usr/local/var/log/glusterfs/dumps/
        Set volume successful

    You can view the changed path of the statedump file using the
    following command:

    `# gluster volume info `

##Displaying Volume Status

You can display the status information about a specific volume, brick or
all volumes, as needed. Status information can be used to understand the
current status of the brick, nfs processes, and overall file system.
Status information can also be used to monitor and debug the volume
information. You can view status of the volume along with the following
details:

-   **detail** - Displays additional information about the bricks.

-   **clients** - Displays the list of clients connected to the volume.

-   **mem** - Displays the memory usage and memory pool details of the
    bricks.

-   **inode** - Displays the inode tables of the volume.

-   **fd** - Displays the open fd (file descriptors) tables of the
    volume.

-   **callpool** - Displays the pending calls of the volume.

**To display volume status**

-   Display information about a specific volume using the following
    command:

    `# gluster volume status [all| []] [detail|clients|mem|inode|fd|callpool]`

    For example, to display information about test-volume:

        # gluster volume status test-volume
        STATUS OF VOLUME: test-volume
        BRICK                           PORT   ONLINE   PID
        --------------------------------------------------------
        arch:/export/1                  24009   Y       22445
        --------------------------------------------------------
        arch:/export/2                  24010   Y       22450

-   Display information about all volumes using the following command:

    `# gluster volume status all`

        # gluster volume status all
        STATUS OF VOLUME: volume-test
        BRICK                           PORT   ONLINE   PID
        --------------------------------------------------------
        arch:/export/4                  24010   Y       22455

        STATUS OF VOLUME: test-volume
        BRICK                           PORT   ONLINE   PID
        --------------------------------------------------------
        arch:/export/1                  24009   Y       22445
        --------------------------------------------------------
        arch:/export/2                  24010   Y       22450

-   Display additional information about the bricks using the following
    command:

    `# gluster volume status  detail`

    For example, to display additional information about the bricks of
    test-volume:

        # gluster volume status test-volume details
        STATUS OF VOLUME: test-volume
        -------------------------------------------
        Brick                : arch:/export/1      
        Port                 : 24009              
        Online               : Y                  
        Pid                  : 16977              
        File System          : rootfs              
        Device               : rootfs              
        Mount Options        : rw                  
        Disk Space Free      : 13.8GB              
        Total Disk Space     : 46.5GB              
        Inode Size           : N/A                
        Inode Count          : N/A                
        Free Inodes          : N/A

        Number of Bricks: 1
        Bricks:
        Brick: server:/brick6

-   Display the list of clients accessing the volumes using the
    following command:

    `# gluster volume status  clients`

    For example, to display the list of clients connected to
    test-volume:

        # gluster volume status test-volume clients
        Brick : arch:/export/1
        Clients connected : 2
        Hostname          Bytes Read   BytesWritten
        --------          ---------    ------------
        127.0.0.1:1013    776          676
        127.0.0.1:1012    50440        51200

-   Display the memory usage and memory pool details of the bricks using
    the following command:

    `# gluster volume status  mem`

    For example, to display the memory usage and memory pool details of
    the bricks of test-volume:

        Memory status for volume : test-volume
        ----------------------------------------------
        Brick : arch:/export/1
        Mallinfo
        --------
        Arena    : 434176
        Ordblks  : 2
        Smblks   : 0
        Hblks    : 12
        Hblkhd   : 40861696
        Usmblks  : 0
        Fsmblks  : 0
        Uordblks : 332416
        Fordblks : 101760
        Keepcost : 100400

        Mempool Stats
        -------------
        Name                               HotCount ColdCount PaddedSizeof AllocCount MaxAlloc
        ----                               -------- --------- ------------ ---------- --------
        test-volume-server:fd_t                0     16384           92         57        5
        test-volume-server:dentry_t           59       965           84         59       59
        test-volume-server:inode_t            60       964          148         60       60
        test-volume-server:rpcsvc_request_t    0       525         6372        351        2
        glusterfs:struct saved_frame           0      4096          124          2        2
        glusterfs:struct rpc_req               0      4096         2236          2        2
        glusterfs:rpcsvc_request_t             1       524         6372          2        1
        glusterfs:call_stub_t                  0      1024         1220        288        1
        glusterfs:call_stack_t                 0      8192         2084        290        2
        glusterfs:call_frame_t                 0     16384          172       1728        6

-   Display the inode tables of the volume using the following command:

    `# gluster volume status  inode`

    For example, to display the inode tables of the test-volume:

        # gluster volume status test-volume inode
        inode tables for volume test-volume
        ----------------------------------------------
        Brick : arch:/export/1
        Active inodes:
        GFID                                            Lookups            Ref   IA type
        ----                                            -------            ---   -------
        6f3fe173-e07a-4209-abb6-484091d75499                  1              9         2
        370d35d7-657e-44dc-bac4-d6dd800ec3d3                  1              1         2

        LRU inodes: 
        GFID                                            Lookups            Ref   IA type
        ----                                            -------            ---   -------
        80f98abe-cdcf-4c1d-b917-ae564cf55763                  1              0         1
        3a58973d-d549-4ea6-9977-9aa218f233de                  1              0         1
        2ce0197d-87a9-451b-9094-9baa38121155                  1              0         2

-   Display the open fd tables of the volume using the following
    command:

    `# gluster volume status  fd`

    For example, to display the open fd tables of the test-volume:

        # gluster volume status test-volume fd

        FD tables for volume test-volume
        ----------------------------------------------
        Brick : arch:/export/1
        Connection 1:
        RefCount = 0  MaxFDs = 128  FirstFree = 4
        FD Entry            PID                 RefCount            Flags              
        --------            ---                 --------            -----              
        0                   26311               1                   2                  
        1                   26310               3                   2                  
        2                   26310               1                   2                  
        3                   26311               3                   2                  
         
        Connection 2:
        RefCount = 0  MaxFDs = 128  FirstFree = 0
        No open fds
         
        Connection 3:
        RefCount = 0  MaxFDs = 128  FirstFree = 0
        No open fds

-   Display the pending calls of the volume using the following command:

    `# gluster volume status  callpool`

    Each call has a call stack containing call frames.

    For example, to display the pending calls of test-volume:

        # gluster volume status test-volume

        Pending calls for volume test-volume
        ----------------------------------------------
        Brick : arch:/export/1
        Pending calls: 2
        Call Stack1
         UID    : 0
         GID    : 0
         PID    : 26338
         Unique : 192138
         Frames : 7
         Frame 1
          Ref Count   = 1
          Translator  = test-volume-server
          Completed   = No
         Frame 2
          Ref Count   = 0
          Translator  = test-volume-posix
          Completed   = No
          Parent      = test-volume-access-control
          Wind From   = default_fsync
          Wind To     = FIRST_CHILD(this)->fops->fsync
         Frame 3
          Ref Count   = 1
          Translator  = test-volume-access-control
          Completed   = No
          Parent      = repl-locks
          Wind From   = default_fsync
          Wind To     = FIRST_CHILD(this)->fops->fsync
         Frame 4
          Ref Count   = 1
          Translator  = test-volume-locks
          Completed   = No
          Parent      = test-volume-io-threads
          Wind From   = iot_fsync_wrapper
          Wind To     = FIRST_CHILD (this)->fops->fsync
         Frame 5
          Ref Count   = 1
          Translator  = test-volume-io-threads
          Completed   = No
          Parent      = test-volume-marker
          Wind From   = default_fsync
          Wind To     = FIRST_CHILD(this)->fops->fsync
         Frame 6
          Ref Count   = 1
          Translator  = test-volume-marker
          Completed   = No
          Parent      = /export/1
          Wind From   = io_stats_fsync
          Wind To     = FIRST_CHILD(this)->fops->fsync
         Frame 7
          Ref Count   = 1
          Translator  = /export/1
          Completed   = No
          Parent      = test-volume-server
          Wind From   = server_fsync_resume
          Wind To     = bound_xl->fops->fsync


