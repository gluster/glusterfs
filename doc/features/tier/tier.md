##Tiering 

* ####Feature page:
http://www.gluster.org/community/documentation/index.php/Features/data-classification

* #####Design: goo.gl/bkU5qv

###Theory of operation


The tiering feature enables different storage types to be used by the same
logical volume. In Gluster 3.7, the two types are classified as "cold" and
"hot", and are represented as two groups of bricks. The hot group acts as
a cache for the cold group. The bricks within the two groups themselves are
arranged according to standard Gluster volume conventions, e.g. replicated,
distributed replicated, or dispersed.

A normal gluster volume can become a tiered volume by "attaching" bricks
to it. The attached bricks become the "hot" group. The bricks within the
original gluster volume are the "cold" bricks.

For example, the original volume may be dispersed on HDD, and the "hot"
tier could be distributed-replicated SSDs.

Once this new "tiered" volume is built, I/Os to it are subjected to cacheing
heuristics:

* All I/Os are forwarded to the hot tier.

* If a lookup fails to the hot tier, the I/O will be forwarded to the cold
tier. This is a "cache miss".

* Files on the hot tier that are not touched within some time are demoted
(moved) to the cold tier (see performance parameters, below).

* Files on the cold tier that are touched one or more times are promoted
(moved) to the hot tier. (see performance parameters, below).

This resembles implementations by Ceph and the Linux data management (DM)
component.

Performance enhancements being considered include:

* Biasing migration of large files over small.

* Only demoting when the hot tier is close to full.

* Write-back cache for database updates.

###Code organization

The design endevors to be upward compatible with future migration policies,
such as scheduled file migration, data classification, etc. For example,
the caching logic is self-contained and separate from the file migration. A
different set of migration policies could use the same underlying migration
engine. The I/O tracking and meta data store compontents are intended to be
reusable for things besides caching semantics.

####Libgfdb:

Libgfdb provides abstract mechanism to record extra/rich metadata
required for data maintenance, such as data tiering/classification.
It provides consumer with API for recording and querying, keeping
the consumer abstracted from the data store used beneath for storing data.
It works in a plug-and-play model, where data stores can be plugged-in.
Presently we have plugin for Sqlite3. In the future will provide recording
and querying performance optimizer. In the current implementation the schema
of metadata is fixed.

####Schema:

      GF_FILE_TB Table:
      This table has one entry per file inode. It holds the metadata required to
      make decisions in data maintenance.
      GF_ID (Primary key)	    : File GFID (Universal Unique IDentifier in the namespace)
      W_SEC, W_MSEC 		     : Write wind time in sec & micro-sec
      UW_SEC, UW_MSEC		    : Write un-wind time in sec & micro-sec
      W_READ_SEC, W_READ_MSEC    : Read wind time in sec & micro-sec
      UW_READ_SEC, UW_READ_MSEC  : Read un-wind time in sec & micro-sec
      WRITE_FREQ_CNTR INTEGER	: Write Frequency Counter
      READ_FREQ_CNTR INTEGER	 : Read Frequency Counter

      GF_FLINK_TABLE:
      This table has all the hardlinks to a file inode.
      GF_ID		 : File GFID               (Composite Primary Key)``|
      GF_PID		: Parent Directory GFID  (Composite Primary Key)   |-> Primary Key
      FNAME 		: File Base Name          (Composite Primary Key)__|
      FPATH 		: File Full Path (Its redundant for now, this will go)
      W_DEL_FLAG    : This Flag is used for crash consistancy, when a link is unlinked.
                  	  i.e Set to 1 during unlink wind and during unwind this record  is deleted
      LINK_UPDATE   : This Flag is used when a link is changed i.e rename.
                          Set to 1 when rename wind and set to 0 in rename unwind

Libgfdb API :
Refer libglusterfs/src/gfdb/gfdb_data_store.h

####ChangeTimeRecorder (CTR) Translator:

ChangeTimeRecorder(CTR) is server side xlator(translator) which sits
just above posix xlator. The main role of this xlator is to record the
access/write patterns on a file residing the brick. It records the
read(only data) and write(data and metadata) times and also count on
how many times a file is read or written. This xlator also captures
the hard links to a file(as its required by data tiering to move
files).

CTR Xlator is the consumer of libgfdb.

To Enable/Disable CTR Xlator:

    **gluster volume set <volume-name> features.ctr-enabled {on/off}**

To Enable/Disable Frequency Counter Recording in CTR Xlator:

    **gluster volume set <volume-name> features.record-counters {on/off}**


####Migration daemon:

When a tiered volume is created, a migration daemon starts. There is one daemon
for every tiered volume per node. The daemon sleeps and then periodically
queries the database for files to promote or demote. The query callbacks
assembles files in a list, which is then enumerated. The frequencies by
which promotes and demotes happen is subject to user configuration.

Selected files are migrated between the tiers using existing DHT migration
logic. The tier translator will leverage DHT rebalance performance
enhancements.

Configurable for Migration daemon:

    gluster volume set <volume-name> cluster.tier-demote-frequency <SECS>
  
    gluster volume set <volume-name> cluster.tier-promote-frequency <SECS>
    
    gluster volume set <volume-name> cluster.read-freq-threshold <SECS>

    gluster volume set <volume-name> cluster.write-freq-threshold <SECS>


####Tier Translator:

The tier translator is the root node in tiered volumes. The first subvolume
is the cold tier, and the second the hot tier. DHT logic for fowarding I/Os
is largely unchanged. Exceptions are handled according to the dht_methods_t
structure, which forks control according to DHT or tier type.

The major exception is DHT's layout is not utilized for choosing hashed
subvolumes. Rather, the hot tier is always the hashed subvolume.

Changes to DHT were made to allow "stacking", i.e. DHT over DHT:

* readdir operations remember the index of the "leaf node" in the volume graph
(client id), rather than a unique index for each DHT instance.

* Each DHT instance uses a unique extended attribute for tracking migration.

* In certain cases, it is legal for tiered volumes to have unpopulated inodes
(wheras this would be an error in DHT's case).

Currently tiered volume expansion (adding and removing bricks) is unsupported.

####glusterd:

The tiered volume tree is a composition of two other volumes. The glusterd
daemon builds it. Existing logic for adding and removing bricks is heavily
leveraged to attach and detach tiers, and perform statistics collection.

