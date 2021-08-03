# Tiering

This feature is developed with a focus on 'archival' behavior. The translator
has plugins which handle the access to archived data.

The expectation here is, the data is migrated out to cold (or archival) storage
by an external entity. This translator only provides access to the archival
data. Notice that, here, the hot (or primary) storage always has entries,
and metadata. Only data would be moved between hot and cold tiers.

## Enabling the feature

This translator can be turned on or off with a CLI option `tier on` or `tier
off` (full CLI would be `gluster volume set $VOLNAME tier on`). This option
internally adds a volume in the brick volfile of type `features/tier`.

Note that, as this translator adds the feature of 'migrating data' to a cold
tier, we wouldn't recommend removing (or disabling) the feature without making
sure all migrated (or archived) data is imported back to the local brick first.

It is advised to set all the required options for tiering feature (some
documented below), before enabling the feature.

## Cold tier (or archive storage)

The translator is written in a way where cold tier (or archive storage) can be
any type of endpoint. All the code is organized in the [plugins/](./plugins)
directory. The interface between these plugins and translator code is done with
a shared object, similar to translator code.

In the first version, we have implemented only the 'filesystem' plugin. I.e.
the cold tier is treated as a directory (for all practical purpose, a directory
in a mountpoint).

## Note on Current implementation:

This version of the tiering code is tested with Distribute volume (i.e., without
any replication or erasure code volume), although no issues are known with
those other types of volumes.


## Options

* `tier-storetype`

This option is mandatory with the `tier` option. Currently available options
are:
- filesystem

`gluster volume set $VOLNAME tier-storetype filesystem`


* `tier-cold-mountpoint`

This option is mandatory when the 'filesystem' plugin is in use. The current
expectation is this directory is available when the volume is started.

`gluster volume set $VOLNAME tier-cold-mountpoint /mnt/coldtier/`

This path can be of NFS, GlusterFS or any local filesystem.

NOTE: we recommend using `chattr -i` option on the cold mountpoint, so that when
cold mount is not available, we can prevent glusterfs process from writing to
`/` (root) of the node.


* `tier-stub-size`

This option is optional. When this is provided, instead of discarding all of the
data blocks in the hot tier, we allow data with this length to be kept in hot tier.
This which would be used to serve locally even when file is remote.

`gluster volume set $VOLNAME tier-stub-size 256KB'

Note that in this version, the same option should be set even in the storage
layer so the feature works smoothly.


* `tier-plugin-migrate-thread`

By default this option is 'enabled', which means there would be a background thread
which migrates the data from COLD to HOT tier, when it gets modified.

The migration behavior can be externally managed by a complete data migration
command through virtual xattr (listed below). We recommend using that if this
option is turned 'off'. The reason to turn off the thread is mostly to avoid
application performance being degraded due to the thread migrating the data in
the backend too aggressively.


* `tier-threshold-block-count`

If the above option is 'enabled', then the automatic file migration gets triggered
only upon this many blocks being modified locally. It doesn't matter how many
different write() FOPs are done, it only matters on which block.

By default, the count is set to 20. This is to prevent migration of the complete
file upon just making a change to a few blocks.


* `tier-cold-block-size`

This option can be used to increase or decrease the block-size which is counted
in the bitmap. Currently its kept at 1MB to balance between bitmap file size, and
also minimum data migration when a single write call reaches the volume. This
can be tuned based on what is the need in the system.


## Special commands on filesystem

* `setfattr -n tier.mark-file-as-remote -v $mtime:/remote/file/path $file`

With this command we can mark $file as remote (present in the cold tier).  This
should be done after migrating data from hot to cold (mostly done in external
tools like [migrate-to-cold.sh](../../../extras/migrate-to-cold.sh).

Note that `mtime` above is expected to contain even nanosecond output (e.g.
`stat -c "%.Y"`).

Path here is optional, but we use it in the script, so there is no namespace
conflict even if same cold tier is used from all bricks, and renames are done
on files.


* `setfattr -n tier.promote-file-as-hot -v true $file`

When this command is passed on $file, you trigger the migration from cold
tier to hot.


* `getfattr -e text -n tier.remote-read-count $file`

This command retrieves the count of 'read' FOPs done when the file is remote.


* `getfattr -e text -n tier.migrated-block-count $file`

This command retrieves the count of 'total remote read-blocks which are
written to local file' when there is any write on the file.


## Differences between Cloudsync and Tier

This translator works very similar to [cloudsync](../cloudsync) translator, but
with a few differences.

1. Position in translator graph:
  - Tier xlator is designed to be placed very close to posix (i.e., storage layer),
    and is in the brick graph of glusterfs. Cloudsync was designed to be on client
    side.
2. Remote type:
  - CloudSync assumed the remote type would be more of a 'cloud' solution like
    object storage like S3. Tiering is designed more for remote tier to be slower
    NFS/GlusterFS or any other filesystems (or even object storage).
3. Performance:
  - With cloudsync, the performance of the volume and also the plugins were never
    the key focus. But with tiering, performance is the main focus.
