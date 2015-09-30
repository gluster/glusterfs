libgfchangelog: "GlusterFS changelog" consumer library
======================================================

This document puts forward the intended need for GlusterFS changelog consumer library (a.k.a. libgfchangelog) for consuming changlogs produced by the Changelog translator. Further, it mentions the proposed design and the API exposed by it. A brief explanation of changelog translator can also be found as a commit message in the upstream source tree and the review link can be [accessed here] [1].

Initial consumer of changelogs would be Geo-Replication (release 3.5). Possible consumers in the future could be backup utilities, GlusterFS self-heal, bit-rot detection, AV scanners. All these utilities have one thing in common - to get a list of changed entities (created/modified/deleted) in the file system. Therefore, the need arises to provide such functionality in the form of a shared library that applications can link against and query for changes (See API section). There is no plan as of now to provide language bindings as such, but for shell script friendliness: 'gfind' command line utility (which would be dynamically linked with libgfchangelog) would be helpful. As of now, development for this utility is still not commenced.

The next section gives a brief introduction about how changelogs are organized and managed. Then we propose couple of designs for libgfchangelog. API set is not covered in this document (maybe later).

Changelogs
==========

Changelogs can be thought as a running history for an entity in the file system from the time the entity came into existance. The goal is to capture all possible transitions the entity underwent till the time it got purged. The transition namespace is broken up into three categories with each category represented by a specific changelog format. Changes are recorded in a flat file in the filesystem and are rolled over after a specific time interval. All three types of categories are recorded in a single changelog file (sequentially) with a type for each entry. Having a single file reduces disk seeks and fragmentation and less number of files to deal with. Stratergy for pruning of old logs is still undecided.


Changelog Transition Namespace
------------------------------

As mentioned before the transition namespace is categorized into three types:
  - TYPE-I   : Data operation
  - TYPE-II  : Metadata operation
  - TYPE-III : Entry operation

One could visualize the transition of an file system entity as a state machine transitioning from one type to another. For TYPE-I and TYPE-II operations there is no state transition as such, but TYPE-III operation involves a state change from the file systems perspective. We can now classify file operations (fops) into one of the three types:
  - Data operation: write(), writev(), truncate(), ftruncate()
  - Metadata operation: setattr(), fsetattr(), setxattr(), fsetxattr(), removexattr(), fremovexattr()
  - Entry operation: create(), mkdir(), mknod(), symlink(), link(), rename(), unlink(), rmdir()

Changelog Entry Format
----------------------

In order to record the type of operation and entity underwent, a type identifier is used. Normally, the entity on which the operation is performed would be identified by the pathname, which is the most common way of addressing in a file system, but we choose to use GlusterFS internal file identifier (GFID) instead (as GlusterFS supports GFID based backend and the pathname field may not always be valid and other reasons which are out of scope of this this document). Therefore, the format of the record for the three types of operation can be summarized as follows:

  - TYPE-I   : GFID of the file
  - TYPE-II  : GFID of the file
  - TYPE-III : GFID + FOP + MODE + UID + GID + PARGFID/BNAME [PARGFID/BNAME]

GFID's are analogous to inodes. TYPE-I and TYPE-II fops record the GFID of the entity on which the operation was performed: thereby recording that there was an data/metadata change on the inode. TYPE-III fops record at the minimum a set of six or seven records (depending on the type of operation), that is sufficient to identify what type of operation the entity underwent. Normally this record inculdes the GFID of the entity, the type of file operation (which is an integer [an enumerated value which is used in GluterFS]) and the parent GFID and the basename (analogous to parent inode and basename).

Changelogs can be either in ascii or binary format, the difference being the format of the records that is persisted. In a binary changelog the gfids are recorded in it's native format ie. 16 byte record and the fop number as a 4 byte integer. In an ascii changelog, the gfids are stored in their canonical form and the fop number is stringified and persisted. Null charater is used as the record serarator and changelogs. This makes it hard to read changelogs from the command line, but the packed format is needed to support file names with spaces and special characters. Below is a snippet of a changelog along side it's hexdump.

```
00000000  47 6c 75 73 74 65 72 46  53 20 43 68 61 6e 67 65  |GlusterFS Change|
00000010  6c 6f 67 20 7c 20 76 65  72 73 69 6f 6e 3a 20 76  |log | version: v|
00000020  31 2e 31 20 7c 20 65 6e  63 6f 64 69 6e 67 20 3a  |1.1 | encoding :|
00000030  20 32 0a 45 61 36 39 33  63 30 34 65 2d 61 66 39  | 2.Ea693c04e-af9|
00000040  65 2d 34 62 61 35 2d 39  63 61 37 2d 31 63 34 61  |e-4ba5-9ca7-1c4a|
00000050  34 37 30 31 30 64 36 32  00 32 33 00 33 33 32 36  |47010d62.23.3326|
00000060  31 00 30 00 30 00 66 36  35 34 32 33 32 65 2d 61  |1.0.0.f654232e-a|
00000070  34 32 62 2d 34 31 62 33  2d 62 35 61 61 2d 38 30  |42b-41b3-b5aa-80|
00000080  33 62 33 64 61 34 35 39  33 37 2f 6c 69 62 76 69  |3b3da45937/libvi|
00000090  72 74 5f 64 72 69 76 65  72 5f 6e 65 74 77 6f 72  |rt_driver_networ|
000000a0  6b 2e 73 6f 00 44 61 36  39 33 63 30 34 65 2d 61  |k.so.Da693c04e-a|
000000b0  66 39 65 2d 34 62 61 35  2d 39 63 61 37 2d 31 63  |f9e-4ba5-9ca7-1c|
000000c0  34 61 34 37 30 31 30 64  36 32 00 45 36 65 39 37  |4a47010d62.E6e97|
```

As you can see, there is an *entry* operation (journal record starting with an "E"). Records for this operation are:
  - GFID  : a693c04e-af9e-4ba5-9ca7-1c4a-47010d62
  - FOP   : 23  (create)
  - Mode  : 33261
  - UID   : 0
  - GID   : 0
  - PARGFID/BNAME: f654232e-a42b-41b3-b5aa-803b3da45937

**NOTE**: In case of a rename operation, there would be an additional record (for the target PARGFID/BNAME).

libgfchangelog
--------------

NOTE: changelogs generated by the changelog translator are rolled over [with the timestamp as the suffix] after a specific interval, after which a new change is started. The current changelog [changelog file without the timestamp as the suffix] should never be processed unless it's rolled over. The rolled over logs should be treated read-only.

Capturing changes performed on a file system is useful for applications that rely on file system scan (crawl) to figure out such information. Backup utilities, automatic file healing in a replicated environment, bit-rot detection and the likes are some of the end user applications that require a set of changed entities in a file system to act on. Goal of libgfchangelog is to provide the application (consumer) a fast and easy to use common query interface (API). The consumer need not worry about the changelog format, nomenclature of the changelog files etc.

Now we list functionality and some of the features.

Functionality
-------------

Changelog Processing: Processing involes reading changelog file(s), converting the entries into human-readable (or application understandable) format (in case of binary log format).
Book-keeping: Keeping track of how much the application has consumed the changelog (ie. changes during the time slice start-time -> end-time).
Serve API request: Update the consumer by providing the set of changes.

Processing could be done in two ways:

* Pre-processing (pre-processing from the library POV):
Once a changelog file is rolled over (by the changelog translator), a set of post processing operations are performed. These operations could include conversion of a binary log file to an understandable format, collate a bunch of logs into a larger sampling period or just keep a private copy of the changelog (in ascii format). Extra disk space is consumed to store this private copy. The library would then be free to consume these logs and serve application requests.

* On-demand:
The processing of the changelogs is trigerred when an application requests for changes. Downside of this being additional time spent on decoding the logs and data accumulation during application request time (but no additional disk space is used over the time period).

After processing, the changelog is ready to be consumed by the application. The function of processing is to convert the logs into human/application readable format (an example is shown below):

```
E a7264fe2-dd6b-43e1-8786-a03b42cc2489 CREATE 33188 0 0 00000000-0000-0000-0000-000000000001%2Fservices1
M a7264fe2-dd6b-43e1-8786-a03b42cc2489 NULL
M 00000000-0000-0000-0000-000000000001 NULL
D a7264fe2-dd6b-43e1-8786-a03b42cc2489
```

Features
--------

The following points mention some of the features that the library could provide.

  - Consumer could choose the update type when it registers with the library. 'types' could be:
    - Streaming: The consumer is updated via stream of changes, ie. the library would just replay the logs
    - Consolidated: The consumer is provided with a consolidated view of the changelog, eg. if <gfid> had an DATA and a METADATA operation, it would be presented as a single update. Similarly for ENTRY operations.
    - Raw: This mode provides the consumer with the pathnames of the changelog files itself (after processing). The changelogs should be strictly treated as read-only. This gives the flexibility to the consumer to extract updates using thier own preferred way (eg. using command line tools like sed, awk, sort | uniq etc.).
  - Application may choose to adopt a synchronous (blocking) or an asynchronous (callback) notification mechanism.
  - Provide a unified view of changelogs from multiple peers (replication scenario) or a global changelog view of the entire cluster.


** The first cut of the library supports:**
  - Raw access mode
  - Synchronous programming model
  - Per brick changelog consumption ie. no unified/globally aggregated changelog

[1]:http://review.gluster.org/5127
