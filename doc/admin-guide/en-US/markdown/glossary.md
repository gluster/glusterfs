Glossary
========

Brick
:   A Brick is the GlusterFS basic unit of storage, represented by an
    export directory on a server in the trusted storage pool. A Brick is
    expressed by combining a server with an export directory in the
    following format:

    `SERVER:EXPORT`

    For example:

    `myhostname:/exports/myexportdir/`

Cluster
:   A cluster is a group of linked computers, working together closely
    thus in many respects forming a single computer.

Distributed File System
:   A file system that allows multiple clients to concurrently access
    data over a computer network.

Filesystem
:   A method of storing and organizing computer files and their data.
    Essentially, it organizes these files into a database for the
    storage, organization, manipulation, and retrieval by the computer's
    operating system.

    Source: [Wikipedia][]

FUSE
:   Filesystem in Userspace (FUSE) is a loadable kernel module for
    Unix-like computer operating systems that lets non-privileged users
    create their own file systems without editing kernel code. This is
    achieved by running file system code in user space while the FUSE
    module provides only a "bridge" to the actual kernel interfaces.

    Source: [Wikipedia][1]

Geo-Replication
:   Geo-replication provides a continuous, asynchronous, and incremental
    replication service from site to another over Local Area Networks
    (LAN), Wide Area Network (WAN), and across the Internet.

glusterd
:   The Gluster management daemon that needs to run on all servers in
    the trusted storage pool.

Metadata
:   Metadata is data providing information about one or more other
    pieces of data.

Namespace
:   Namespace is an abstract container or environment created to hold a
    logical grouping of unique identifiers or symbols. Each Gluster
    volume exposes a single namespace as a POSIX mount point that
    contains every file in the cluster.

Open Source
:   Open source describes practices in production and development that
    promote access to the end product's source materials. Some consider
    open source a philosophy, others consider it a pragmatic
    methodology.

    Before the term open source became widely adopted, developers and
    producers used a variety of phrases to describe the concept; open
    source gained hold with the rise of the Internet, and the attendant
    need for massive retooling of the computing source code.

    Opening the source code enabled a self-enhancing diversity of
    production models, communication paths, and interactive communities.
    Subsequently, a new, three-word phrase "open source software" was
    born to describe the environment that the new copyright, licensing,
    domain, and consumer issues created.

    Source: [Wikipedia][2]

Petabyte
:   A petabyte (derived from the SI prefix peta- ) is a unit of
    information equal to one quadrillion (short scale) bytes, or 1000
    terabytes. The unit symbol for the petabyte is PB. The prefix peta-
    (P) indicates a power of 1000:

    1 PB = 1,000,000,000,000,000 B = 10005 B = 1015 B.

    The term "pebibyte" (PiB), using a binary prefix, is used for the
    corresponding power of 1024.

    Source: [Wikipedia][3]

POSIX
:   Portable Operating System Interface (for Unix) is the name of a
    family of related standards specified by the IEEE to define the
    application programming interface (API), along with shell and
    utilities interfaces for software compatible with variants of the
    Unix operating system. Gluster exports a fully POSIX compliant file
    system.

RAID
:   Redundant Array of Inexpensive Disks (RAID) is a technology that
    provides increased storage reliability through redundancy, combining
    multiple low-cost, less-reliable disk drives components into a
    logical unit where all drives in the array are interdependent.

RRDNS
:   Round Robin Domain Name Service (RRDNS) is a method to distribute
    load across application servers. RRDNS is implemented by creating
    multiple A records with the same name and different IP addresses in
    the zone file of a DNS server.

Trusted Storage Pool
:   A storage pool is a trusted network of storage servers. When you
    start the first server, the storage pool consists of that server
    alone.

Userspace
:   Applications running in user space don’t directly interact with
    hardware, instead using the kernel to moderate access. Userspace
    applications are generally more portable than applications in kernel
    space. Gluster is a user space application.

Volfile
:   Volfile is a configuration file used by glusterfs process. Volfile
    will be usually located at `/var/lib/glusterd/vols/VOLNAME`.

Volume
:   A volume is a logical collection of bricks. Most of the gluster
    management operations happen on the volume.

  [Wikipedia]: http://en.wikipedia.org/wiki/Filesystem
  [1]: http://en.wikipedia.org/wiki/Filesystem_in_Userspace
  [2]: http://en.wikipedia.org/wiki/Open_source
  [3]: http://en.wikipedia.org/wiki/Petabyte
