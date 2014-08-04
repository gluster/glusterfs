Meta translator
===============

Introduction
------------

Meta xlator provides an interface similar to the Linux procfs, for GlusterFS
runtime and configuration. This document lists some useful information about
GlusterFS internals that could be accessed via the meta xlator. This is not
exhaustive at the moment. Contributors are welcome to improve this.

Note: Meta xlator is loaded automatically in the client graph, ie. in the
mount process' graph.

### GlusterFS native mount version

>[root@trantor codebase]# cat $META/version
>{
>  "Package Version": "3.7dev"
>}

### Listing of some files under the `meta` folder

>[root@trantor codebase]# mount -t glusterfs trantor:/vol /mnt/fuse
>[root@trantor codebase]# ls $META
>cmdline  frames  graphs  logging  mallinfo  master  measure_latency  process_uuid  version

### GlusterFS' process identifier

>[root@trantor codebase]# cat $META/process_uuid
>trantor-11149-2014/07/25-18:48:50:468259
>
This identifier appears in connection establishment log messages.
For eg.,

>[2014-07-25 18:48:49.017927] I [server-handshake.c:585:server_setvolume] 0-vol-server: accepted client from trantor-11087-2014/07/25-18:48:48:779656-vol-client-0-0-0 (version: 3.7dev)
>

### GlusterFS command line

>[root@trantor codebase]# cat $META/cmdline
>{
>  "Cmdlinestr": "/usr/local/sbin/glusterfs --volfile-server=trantor --volfile-id=/vol /mnt/fuse"
>}

### GlusterFS volume graph

The following directory structure reveals the way xlators are stacked in a
graph like fashion. Each (virtual) file under a xlator directory provides
runtime information of that xlator.  For eg. 'name' contains the name of the
xlator.

```
/mnt/fuse/.meta/graphs/active
|-- meta-autoload
|   |-- history
|   |-- meminfo
|   |-- name
|   |-- options
|   |-- private
|   |-- profile
|   |-- subvolumes
|   |   `-- 0 -> ../../vol
|   |-- type
|   `-- view
|-- top -> meta-autoload
|-- vol
|   |-- history
|   |-- meminfo
|   |-- name
|   |-- options
|   |   |-- count-fop-hits
|   |   `-- latency-measurement
|   |-- private
|   |-- profile
|   |-- subvolumes
|   |   `-- 0 -> ../../vol-md-cache
|   |-- type
|   `-- view
|-- vol-client-0
|   |-- history
|   |-- meminfo
|   |-- name
|   |-- options
|   |   |-- client-version
|   |   |-- clnt-lk-version
|   |   |-- fops-version
|   |   |-- password
|   |   |-- ping-timeout
|   |   |-- process-uuid
|   |   |-- remote-host
|   |   |-- remote-subvolume
|   |   |-- send-gids
|   |   |-- transport-type
|   |   |-- username
|   |   |-- volfile-checksum
|   |   `-- volfile-key
|   |-- private
|   |-- profile
|   |-- subvolumes
|   |-- type
|   `-- view
|-- vol-client-1
|   |-- history
|   |-- meminfo
|   |-- name
|   |-- options
|   |   |-- client-version
|   |   |-- clnt-lk-version
|   |   |-- fops-version
|   |   |-- password
|   |   |-- ping-timeout
|   |   |-- process-uuid
|   |   |-- remote-host
|   |   |-- remote-subvolume
|   |   |-- send-gids
|   |   |-- transport-type
|   |   |-- username
|   |   |-- volfile-checksum
|   |   `-- volfile-key
|   |-- private
|   |-- profile
|   |-- subvolumes
|   |-- type
|   `-- view
|-- vol-dht
|   |-- history
|   |-- meminfo
|   |-- name
|   |-- options
|   |-- private
|   |-- profile
|   |-- subvolumes
|   |   |-- 0 -> ../../vol-client-0
|   |   `-- 1 -> ../../vol-client-1
|   |-- type
|   `-- view
|-- volfile
|-- vol-io-cache
|   |-- history
|   |-- meminfo
|   |-- name
|   |-- options
|   |-- private
|   |-- profile
|   |-- subvolumes
|   |   `-- 0 -> ../../vol-read-ahead
|   |-- type
|   `-- view
|-- vol-md-cache
|   |-- history
|   |-- meminfo
|   |-- name
|   |-- options
|   |-- private
|   |-- profile
|   |-- subvolumes
|   |   `-- 0 -> ../../vol-open-behind
|   |-- type
|   `-- view
|-- vol-open-behind
|   |-- history
|   |-- meminfo
|   |-- name
|   |-- options
|   |-- private
|   |-- profile
|   |-- subvolumes
|   |   `-- 0 -> ../../vol-quick-read
|   |-- type
|   `-- view
|-- vol-quick-read
|   |-- history
|   |-- meminfo
|   |-- name
|   |-- options
|   |-- private
|   |-- profile
|   |-- subvolumes
|   |   `-- 0 -> ../../vol-io-cache
|   |-- type
|   `-- view
|-- vol-read-ahead
|   |-- history
|   |-- meminfo
|   |-- name
|   |-- options
|   |-- private
|   |-- profile
|   |-- subvolumes
|   |   `-- 0 -> ../../vol-write-behind
|   |-- type
|   `-- view
`-- vol-write-behind
    |-- history
    |-- meminfo
    |-- name
    |-- options
    |-- private
    |-- profile
    |-- subvolumes
    |   `-- 0 -> ../../vol-dht
    |-- type
    `-- view

```
