### Improved Node fail-over issues handling by using Gluster Meta Volume

In replica pairs one Geo-rep worker should be active and all
the other replica workers should be passive. When Active worker goes
down, Passive worker will become active. In previous releases, this logic
was based on node-uuid, but now it is based on Lock file in Meta
Volume. Now it is possible to decide Active/Passive more accurately
and multiple Active worker scenarios minimized.

Geo-rep works without Meta Volume also, this feature is backward
compatible. By default config option `use_meta_volume` is False. This
feature can be turned on with geo-rep config `use_meta_volume`
true. Without this feature Geo-rep works as it was working in previous
releases.

Issues if meta_volume is turned off:

1. Multiple workers becoming active and participate in
syncing. Duplicate efforts and all the issues related to concurrent
execution exists.

2. Failover only works at node level, if a brick process goes down but
node is alive then fail-back will not happen and delay in syncing.

3. Very difficult documented steps about placements of bricks in case
of replica 3. For example, first brick in each replica should not be
placed in same node. etc.

4. Consuming Changelogs from previously failed node when it comes
back, which may lead to issues like delayed syncing and data
inconsistencies in case of Renames.

**Fixes**: [1196632](https://bugzilla.redhat.com/show_bug.cgi?id=1196632),
[1217939](https://bugzilla.redhat.com/show_bug.cgi?id=1217939)


### Improved Historical Changelogs consumption

Support for consuming Historical Changelogs introduced in previous
releases, with this release this is more stable and improved. Use of
Filesystem crawl is minimized and limited only during initial sync.In
previous release, Node reboot or brick process going down was treated as
Changelog Breakage and Geo-rep was fallback to XSync for that
duration. With this release, Changelog session will be considered
broken only if Changelog is turned off. All the other scenarios
considered as safe.

This feature is also required by glusterfind.

**Fixes**: [1217944](https://bugzilla.redhat.com/show_bug.cgi?id=1217944)


### Improved Status and Checkpoint

Status got many improvements, Showing accurate details of Session
info, User info, Slave node to which master node is connected, Last
Synced Time etc. Initializing time is reduced, Status change will
happen as soon as geo-rep workers ready.(In previous releases
Initializing time was 60 sec)

**Fixes**: [1212410](https://bugzilla.redhat.com/show_bug.cgi?id=1212410)

### Worker Restart improvements

Workers going down and coming back is very common in geo-rep for
reasons like network failure, Slave node going down etc. When it comes
up it has to reprocess the changelogs again because worker died before
updating the last sync time. The batch size is now optimized such that
the amount of reprocess is minimized.

**Fixes**: [1210965](https://bugzilla.redhat.com/show_bug.cgi?id=1210965)


### Improved RENAME handling

When renamed filename hash falls to other brick, respective brick's
changelog records RENAME, but rest of the fops like CREATE, DATA are
recorded in first brick. Each Geo-rep worker per brick syncs data to
Slave Volume independently, These things go out of order and Master
and Slave Volume become inconsistent. With the help of DHT team,
RENAMEs are recorded where CREATE and DATA are recorded.

**Fixes**: [1141379](https://bugzilla.redhat.com/show_bug.cgi?id=1141379)


### Syncing xattrs and acls

Syncing both xattrs and acls to Slave cluster are now supported. These
can be disabled setting config options sync-xattrs or sync-acls to
false.

**Fixes**: [1187021](https://bugzilla.redhat.com/show_bug.cgi?id=1187021),
[1196690](https://bugzilla.redhat.com/show_bug.cgi?id=1196690)


### Identifying Entry failures

Logging improvements to identify exact reason for Entry failures, GFID
conflicts, I/O errors etc.  Safe errors are not logged in Mount logs
in Slave, Safe errors are post processed and only genuine errors are
logged in Master logs.

**Fixes**: [1207115](https://bugzilla.redhat.com/show_bug.cgi?id=1207115),
[1210562](https://bugzilla.redhat.com/show_bug.cgi?id=1210562)


### Improved rm -rf issues handling

Successive deletes and creates had issues, Handling these issues
minimized. (Not completely fixed since it depends on Open issues of
DHT)

**Fixes**: [1211037](https://bugzilla.redhat.com/show_bug.cgi?id=1211037)


### Non root Geo-replication simplified

Manual editing of Glusterd vol file is simplified by introducing
`gluster system:: mountbroker` command

**Fixes**: [1136312](https://bugzilla.redhat.com/show_bug.cgi?id=1136312)

### Logging Rsync performance on request basis

Rsync performance can be evaluated by enabling a config option. After
this Geo-rep starts recording rsync performance in log file, which can
be post processed to get meaningful metrics.

**Fixes**: [764827](https://bugzilla.redhat.com/show_bug.cgi?id=764827)

### Initial sync issues due to upper limit comparison during Filesystem Crawl

Bug fix, Fixed wrong logic in Xsync Change detection. Upper limit was
considered during xsync crawl. Geo-rep XSync was missing many files
considering Changelog will take care. But Changelog will not have
complete details of the files created before enabling Geo-replication.

When rsync/tarssh fails, geo-rep is now capable of identifying safe
errors and continue syncing by ignoring those issues. For example,
rsync fails to sync a file which is deleted in master during
sync. This can be ignored since the file is unlinked and no need to
try syncing.

**Fixes**: [1200733](https://bugzilla.redhat.com/show_bug.cgi?id=1200733)


### Changelog failures and Brick failures handling

When Brick process goes down, or any Changelog exception Geo-rep
worker was failing back to XSync crawl. Which was bad since Xsync
fails to identify Deletes and Renames. Now this is prevented, worker
goes to Faulty and wait for that Brick process to comeback.


**Fixes**: [1202649](https://bugzilla.redhat.com/show_bug.cgi?id=1202649)


### Archive Changelogs in working directory after processing

Archive Changelogs after processing not generate empty changelogs when
no data is available. This is great improvement in terms of reducing
the inode consumption in Brick.

**Fixes**: [1169331](https://bugzilla.redhat.com/show_bug.cgi?id=1169331)


### Virtual xattr to trigger sync

Since we use Historical Changelogs when Geo-rep worker restarts. Only
`SETATTR` will be recorded when we touch a file. In previous versions,
Re triggering a file sync is stop geo-rep, touch files and start
geo-replication. Now touch will not help since it records only `SETATTR`.
Virtual Xattr is introduced to retrigger the sync. No Geo-rep restart
required.

**Fixes**: [1176934](https://bugzilla.redhat.com/show_bug.cgi?id=1176934)


### SSH Keys overwrite issues during Geo-rep create

Parallel creates or multiple Geo-rep session creation was overwriting
the pem keys written by first one. This leads to connectivity issues
when Geo-rep is started.

**Fixes**: [1183229](https://bugzilla.redhat.com/show_bug.cgi?id=1183229)


### Ownership sync improvements

Geo-rep was failing to sync ownership information from master cluster
to Slave cluster.

**Fixes**: [1104954](https://bugzilla.redhat.com/show_bug.cgi?id=1104954)


### Slave node failover handling improvements

When slave node goes down, Master worker which is connected to that
brick will go to faulty. Now it tries to connect to another slave node
instead of waiting for that Slave node to come back.

**Fixes**: [1151412](https://bugzilla.redhat.com/show_bug.cgi?id=1151412)


### Support of ssh keys custom location

If ssh `authorized_keys` are configured in non standard location instead
of default `$HOME/.ssh/authorized_keys`. Geo-rep create was failing, now
this is supported.

**Fixes**: [1181117](https://bugzilla.redhat.com/show_bug.cgi?id=1181117)
