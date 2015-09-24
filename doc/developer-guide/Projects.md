This page contains a list of project ideas which will be suitable for
students (for GSOC, internship etc.)

Projects with mentors
---------------------

### gfsck - A GlusterFS filesystem check

-   A tool to check filesystem integrity and repairing
-   I'm currently working on it
-   Owner: Xavier Hernandez (Datalab)

### Sub-directory mount support for native GlusterFS mounts

Allow clients to directly mount directories inside a GlusterFS volume,
like how NFS clients can mount directories inside an NFS export.

Mentor: Kaushal <kshlmster at gmail dot com>

### GlusterD services high availablity

GlusterD should restart the processes it manages, bricks, nfs server,
self-heal daemon and quota daemon, whenever it detects they have died.

Mentor : Atin Mukherjee <atin.mukherjee83@gmail.com>

### Language bindings for libgfapi

-   API/library for accessing gluster volumes

### oVirt gui for stats

Have pretty graphs and tables in ovirt for the GlusterFS top and profile
commands.

### Monitoring integrations - munin others

The more monitoring support we have for GlusterFS the better.

### More compression algorithms for compression xlator

The on-wire compression translator should be extended to support more
compression algorithms. Ideally it should be pluggable.

### Cinder GlusterFS backup driver

Write a driver for cinder, a part of openstack, to allow backup onto
GlusterFS volumes

### rsockets - sockets for rdma transport

Coding for RDMA using the familiar socket api should lead to a more
robust rdma transport

### Data import tool

Create a tool which will allow importing already existing data in the
brick directories into the gluster volume. This is most likely going to
be a special rebalance process.

### Rebalance improvements

Improve rebalance performance.

### Meta translator

The meta xlator provides a /proc like interface to GlusterFS xlators.
This could be improved upon and the meta xlator could be made a standard
part of the volume graph.

### Geo-replication using rest-api

Might be suitable for geo replication over WAN.

### Quota using underlying FS' quota

GlusterFS quota is currently maintained completely in GlusterFSs
namespace using xattrs. We could make use of the quota capabilities of
the underlying fs (XFS) for better performance.

### Snapshot pluggability

Snapshot should be able to make use of snapshot support provided by
btrfs for example.

### Compression at rest

Lessons learnt while implementing encryption at rest can be used with
the compression at rest.

### File-level deduplication

GlusterFS works on files. So why not have dedup at the level files as
well.

### Composition xlator for small files

Merge small files into a designated large file using our own custom
semantics. This can improve our small file performance.