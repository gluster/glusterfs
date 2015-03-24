# glusterfind - A tool to find Modified files/dirs

A tool which helps to get full/incremental list of files/dirs from GlusterFS Volume using Changelog/Find. In Gluster volumes, detecting the modified files is challenging. Readdir on a directory leads to multiple network calls since files in a directory are distributed accross nodes.

This tool should be run in one of the node, which will get Volume info and gets the list of nodes and brick paths. For each brick, it spawns the process and runs crawler command in respective node. Crawler will be run in brick FS(xfs, ext4 etc) and not in Gluster Mount. Crawler generates output file with the list of files modified after last run or after the session creation.

## Session Management

Create a glusterfind session to remember the time when last sync or processing complete. For example, your backup application runs every day and gets incremental results on each run. The tool maintains session in `$GLUSTERD_WORKDIR/glusterfind/`, for each session it creates and directory and creates a sub directory with Volume name. (Default working directory is /var/lib/glusterd, in some systems this location may change. To find Working dir location run `grep working-directory /etc/glusterfs/glusterd.vol` or `grep working-directory /usr/local/etc/glusterfs/glusterd.vol` if source install)

For example, if the session name is "backup" and volume name is "datavol", then the tool creates `$GLUSTERD_WORKDIR/glusterfind/backup/datavol`. Now onwards we refer this directory as `$SESSION_DIR`.

    create => pre => post => [delete]

Once the session is created, we can run the tool with two steps Pre and Post. To collect the list of modified files after the create time or last run time, we need to call pre command. Pre command finds the modified files and generates output file. Consumer can check the exit code of pre command and start processing those files. As a post processing step run the post command to update the session time as per latest run.

For example, backup utility runs Pre command and gets the list of files/directories changed. Sync those files to backup target and inform to glusterfind by calling Post command.

At the end of Pre command, `$SESSION_DIR/status.pre` status file will get created. Pre status file stores the time when current crawl is started, and get all the files/dirs modified till that time. Once Post is called, `$SESSION_DIR/status.pre` will be renamed to `$SESSION_DIR/status`. content of this file will be used as start time for the next crawl.

During Pre, we can force the tool to do full find instead of incremental find. Tool uses `find` command in brick backend to get list of files/dirs.

When `glusterfind create`, in that node it generates ssh key($GLUSTERD_WORKDIR/glusterfind.secret.pem) and distributes to all Peers via Glusterd. Once ssh key is distributed in Trusted pool, tool can run ssh commands and copy files from other Volume nodes.

When `glusterfind pre` is run, it internally runs `gluster volume info` to get list of nodes and respective brick paths. For each brick, it calls respective node agents via ssh to find the modified files/dirs which are local them. Once each node agents generates output file, glusterfind collects all the files via scp and merges it into given output file.

When `glusterfind post` is run, it renames `$SESSION_DIR/status.pre` file to `$SESSION_DIR/status`.

## Changelog Mode and GFID to Path conversion

Incremental find uses Changelogs to get the list of GFIDs modified/created. Any application expects file path instead of GFID. Their is no standard/easy way to convert from GFID to Path.

If we set build-pgfid option in Volume GlusterFS starts recording each files parent directory GFID as xattr in file on any ENTRY fop.

    trusted.pgfid.<GFID>=NUM_LINKS

To convert from GFID to path, we can mount Volume with aux-gfid-mount option, and get Path information by a getfattr query.

    getfattr -n glusterfs.ancestry.path -e text /mnt/datavol/.gfid/<GFID>

This approach is slow, for a requested file gets parent GFID via xattr and reads that directory to gets the file which is having same inode number as of GFID file. To improve the performance, glusterfind uses build-pgfid option, but instead of using getfattr on mount it gets the details from brick backend. glusterfind collects all parent GFIDs at once and starts crawling each directory. Instead of processing one GFID to Path conversion, it gets inode numbers of all input GFIDs and filter while reading parent directory.

Above method is fast compared to `find -samefile` since it crawls only required directories to find files with same inode number as GFID file. But pgfid information only available when a lookup is made or any ENTRY fop to a file after enabling build-pgfid. The files created before build-pgfid enable will not get converted to path from GFID with this approach.

Tool collects the list of GFIDs failed to convert with above method and does a full crawl to convert it to path. Find command is used to crawl entire namespace. Instead of calling find command for every GFID, glusterfind uses an efficient way to convert all GFID to path with single call to `find` command.

## Usage

### Create the session

    glusterfind create SESSION_NAME VOLNAME [--force]
    glusterfind create --help

Where, SESSION_NAME is any name without space to identify when run second time. When a node is added to Volume then the tool expects ssh keys to be copied to new node(s) also. Run Create command with `--force` to distribute keys again.

Examples,

    glusterfind create --help
    glusterfind create backup datavol
    glusterfind create antivirus_scanner datavol
    glusterfind create backup datavol --force

### Pre Command

    glusterfind pre SESSION_NAME VOLUME_NAME OUTFILE
    glusterfind pre --help

We need not specify Volume name since session already has the details. List of files will be populated in OUTFILE.

To trigger the full find, call the pre command with `--full` argument. Multiple crawlers are available for incremental find, we can choose crawl type with `--crawl` argument.

Examples,

    glusterfind pre backup datavol /root/backup.txt
    glusterfind pre backup datavol /root/backup.txt --full

    # Changelog based crawler, works only for incremental
    glusterfind pre backup datavol /root/backup.txt --crawler=changelog

    # Find based crawler, works for both full and incremental
    glusterfind pre backup datavol /root/backup.txt --crawler=brickfind

Output file contains list of files/dirs relative to the Volume mount, if we need to prefix with any path to have absolute path then,

    glusterfind pre backup datavol /root/backup.txt --file-prefix=/mnt/datavol/

### List Command

To get the list of sessions and respective session time,

    glusterfind list [--session SESSION_NAME] [--volume VOLUME_NAME]

Examples,

    glusterfind list
    glusterfind list --session backup

Example output,

    SESSION                   VOLUME                    SESSION TIME
    ---------------------------------------------------------------------------
    backup                    datavol                   2015-03-04 17:35:34

### Post Command

    glusterfind post SESSION_NAME VOLUME_NAME

Examples,

    glusterfind post backup datavol

### Delete Command

    glusterfind delete SESSION_NAME VOLUME_NAME

Examples,

    glusterfind delete backup datavol


## Adding more Crawlers

Adding more crawlers is very simple, Add an entry in `$GLUSTERD_WORKDIR/glusterfind.conf`. glusterfind can choose your crawler using `--crawl` argument.

    [crawlers]
    changelog=/usr/libexec/glusterfs/glusterfind/changelog.py
    brickfind=/usr/libexec/glusterfs/glusterfind/brickfind.py

For example, if you have a multithreaded brick crawler, say `parallelbrickcrawl` add it to the conf file.

    [crawlers]
    changelog=/usr/libexec/glusterfs/glusterfind/changelog.py
    brickfind=/usr/libexec/glusterfs/glusterfind/brickfind.py
    parallelbrickcrawl=/root/parallelbrickcrawl

Custom crawler can be executable script/binary which accepts volume name, brick path, output_file and start time(and optional debug flag)

For example,

    /root/parallelbrickcrawl SESSION_NAME VOLUME BRICK_PATH OUTFILE START_TIME [--debug]

Where `START_TIME` is in unix epoch format, `START_TIME` will be zero for full find.

## Known Issues

1. Deleted files will not get listed, since we can't convert GFID to Path if file/dir is deleted.
2. Only new name will get listed if Renamed.
3. All hardlinks will get listed.
