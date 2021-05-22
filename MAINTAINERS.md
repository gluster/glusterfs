GlusterFS Maintainers
=====================

The intention of this file is not to establish who owns what portions of the
code base, but to provide a set of names that developers can consult when they
have a question about a particular subset and also to provide a set of names
to be CC'd when submitting a patch to obtain appropriate review.

In general, if you have a question about inclusion of a patch, you should
consult gluster-devel@gluster.org and not any specific individual privately.

## Descriptions of section entries:

	M: Main contact that knows and takes care of this area
	L: Mailing list that is relevant to this area
	W: Web-page with status/info
	Q: Patchwork web based patch tracking system site
	T: SCM tree type and location.  Type is one of: git, hg, quilt, stgit.
	S: Status, one of the following:
	   Supported:	Someone is actually paid to look after this.
	   Maintained:	Someone actually looks after it.
	   Stable:      No major issues in last 1 year or more. Hence no one
	                specifically focuses on this component.
	   Odd Fixes:	It has a maintainer but they don't have time to do
			much other than throw the odd patch in. See below.
	   Orphan:	No current maintainer [but maybe you could take the
			role as you write your new code].
	   Obsolete:	Old code. Something tagged obsolete generally means
			it has been replaced by a better system and you
			should be using that.
	F: Files and directories with wildcard patterns.
	   A trailing slash includes all files and subdirectory files.
	   F:	drivers/net/	all files in and below drivers/net
	   F:	drivers/net/*	all files in drivers/net, but not below
	   F:	*/net/*		all files in "any top level directory"/net
	   One pattern per line.  Multiple F: lines acceptable.
	X: Files and directories that are NOT maintained, same rules as F:
	   Files exclusions are tested before file matches.
	   Can be useful for excluding a specific subdirectory, for instance:
	   F:	net/
	   X:	net/ipv6/
	   matches all files in and below net excluding net/ipv6/
	K: Keyword perl extended regex pattern to match content in a
	   patch or file.  For instance:
	   K: of_get_profile
	      matches patches or files that contain "of_get_profile"
	   K: \b(printk|pr_(info|err))\b
	      matches patches or files that contain one or more of the words
	      printk, pr_info or pr_err
	   One regex pattern per line.  Multiple K: lines acceptable.
        P: Peer for a component


## General Project Architects

  - **M**: Amar Tumballi \<amar@kadalu.io\>
  - **M**: Xavier Hernandez  \<xhernandez@redhat.com\>
  - **M**: Pranith Karampuri \<pranith.karampuri@phonepe.com\>
  - **P**: Atin Mukherjee \<atin.mukherjee83@gmail.com\>

## xlators:

### Access Control List (ACL)
  - **S**: Stable
  - **F**: xlators/system/posix-acl/

### Arbiter
  - **M**: Ravishankar N \<ravishankar@redhat.com\>
  - **P**: Pranith Karampuri \<pranith.karampuri@phonepe.com\>
  - **S**: Maintained
  - **F**: xlators/features/arbiter/

### Automatic File Replication (AFR)
  - **M**: Pranith Karampuri \<pranith.karampuri@phonepe.com\>
  - **M**: Ravishankar N \<ravishankar@redhat.com\>
  - **P**: Karthik US \<ksubrahm@redhat.com\>
  - **S**: Maintained
  - **F**: xlators/cluster/afr/

### Barrier
  - **S**: Stable
  - **F**: xlators/features/barrier

### BitRot
  - **M**: Kotresh HR \<khiremat@redhat.com\>
  - **P**: Raghavendra Bhat \<rabhat@redhat.com\>
  - **S**: Maintained
  - **F**: xlators/features/bit-rot/

### Changelog
  - **M**: Aravinda Vishwanathapura \<aravinda@kadalu.io\>
  - **P**: Kotresh HR \<khiremat@redhat.com\>
  - **S**: Maintained
  - **F**: xlators/features/changelog/

### Distributed Hashing Table (DHT)
  - **M**: Mohit Agrawal \<moagrawa@redhat.com\>
  - **M**: Barak Sason Rofman \<bsasonro@redhat.com\>
  - **M**: Tamar Shacked \<tshacked@redhat.com\>
  - **P**: Csaba Henk \<chenk@redhat.com\>
  - **S**: Maintained
  - **F**: xlators/cluster/dht/

### Erasure Coding
  - **M**: Pranith Karampuri \<pranith.karampuri@phonepe.com\>
  - **M**: Xavier Hernandez  \<xhernandez@redhat.com\>
  - **P**: Ashish Pandey \<aspandey@redhat.com\>
  - **P**: Sheetal Pamecha \<spamecha@redhat.com\>
  - **S**: Maintained
  - **F**: xlators/cluster/ec/

### Error-gen
  - **S**: Stable
  - **F**: xlators/debug/error-gen/

### FUSE Bridge
  - **M**: Csaba Henk \<chenk@redhat.com\>
  - **P**: Niels de Vos \<ndevos@redhat.com\>
  - **S**: Maintained
  - **F**: xlators/mount/

### Index
  - **M**: Pranith Karampuri \<pranith.karampuri@phonepe.com\>
  - **P**: Ravishankar N \<ravishankar@redhat.com\>
  - **S**: Maintained
  - **F**: xlators/features/index/

### IO Cache
  - **P**: Mohammed Rafi KC \<rafi.kavungal@iternity.com\>
  - **S**: Maintained
  - **F**: xlators/performance/io-cache/

### IO Statistics
  - **S**: Stable
  - **F**: xlators/debug/io-stats/

### IO threads
  - **M**: Pranith Karampuri \<pranith.karampuri@phonepe.com\>
  - **P**: Ravishankar N \<ravishankar@redhat.com\>
  - **S**: Maintained
  - **F**: xlators/performance/io-threads/

### Leases
  - **S**: Stable
  - **F**: xlators/features/leases/

### Locks
  - **M**: Xavier Hernandez  \<xhernandez@redhat.com\>
  - **S**: Maintained
  - **F**: xlators/features/locks/

### Marker
  - **M**: Kotresh HR \<khiremat@redhat.com\>
  - **S**: Maintained
  - **F**: xlators/features/marker/

### Meta
  - **M**: Mohammed Rafi KC \<rafi.kavungal@iternity.com\>
  - **S**: Maintained
  - **F**: xlators/features/meta/

### Metadata-cache
  - **S**: Stable
  - **F**: xlators/performance/md-cache/

### Negative-lookup Cache
  - **P**: Pranith Karampuri \<pranith.karampuri@phonepe.com\>
  - **S**: Maintained
  - **F**: xlators/performance/nl-cache/

### gNFS
  - **M**: Jiffin Tony Thottan \<jthottan@redhat.com\>
  - **P**: Xie Changlong \<xiechanglong@cmss.chinamobile.com\>
  - **P**: Amar Tumballi \<amar@kadalu.io\>
  - **S**: Odd Fixes
  - **F**: xlators/nfs/server/

### Open-behind
  - **M**: Xavier Hernandez  \<xhernandez@redhat.com\>
  - **S**: Maintained
  - **F**: xlators/performance/open-behind/

### Posix:
  - **S**: Stable
  - **F**: xlators/storage/posix/

### Quick-read
  - **S**: Stable
  - **F**: xlators/performance/quick-read/

### Quota
  - **S**: Orphan
  - **F**: xlators/features/quota/

### Read-ahead
  - **P**: Csaba Henk \<chenk@redhat.com\>
  - **S**: Maintained
  - **F**: xlators/performance/read-ahead/

### Readdir-ahead
  - **S**: Orphan
  - **F**: xlators/performance/readdir-ahead/

### Sharding
  - **P**: Xavier Hernandez  \<xhernandez@redhat.com\>
  - **S**: Maintained
  - **F**: xlators/features/shard/

### Trash
  - **M**: Anoop C S \<anoopcs@redhat.com\>
  - **M**: Jiffin Tony Thottan \<jthottan@redhat.com\>
  - **S**: Maintained
  - **F**: xlators/features/trash/

### Upcall
  - **S**: Stable
  - **F**: xlators/features/upcall/

### Write-behind
  - **P**: Csaba Henk \<chenk@redhat.com\>
  - **S**: Maintained
  - **F**: xlators/performance/write-behind/

### Write Once Read Many
  - **P**: Karthik US \<ksubrahm@redhat.com\>
  - **S**: Maintained
  - **F**: xlators/features/read-only/

### Cloudsync
  - **S**: Orphan
  - **F**: xlators/features/cloudsync/


## Other bits of code:

### Doc
  - **S**: Odd-Fixes
  - **F**: doc/
  - **R**: https://github.com/gluster/gluster-docs

### Geo Replication
  - **M**: Aravinda Vishwanathapura \<aravinda@kadalu.io\>
  - **M**: Kotresh HR \<khiremat@redhat.com\>
  - **M**: Sunny Kumar \<sunkumar@redhat.com\>
  - **P**: Shwetha Acharya \<sacharya@redhat.com\>
  - **S**: Maintained
  - **F**: geo-replication/

### Glusterfind
  - **M**: Aravinda Vishwanathapura \<aravinda@kadalu.io\>
  - **P**: Shwetha Acharya \<sacharya@redhat.com\>
  - **S**: Maintained
  - **F**: tools/glusterfind/

### libgfapi
  - **S**: Stable
  - **F**: api/

### libglusterfs
  - **S**: Stable
  - **F**: libglusterfs/

### xxhash
  - **M**: Aravinda Vishwanathapura \<aravinda@kadalu.io\>
  - **M**: Kotresh HR \<khiremat@redhat.com\>
  - **P**: Yaniv Kaul \<ykaul@redhat.com\>
  - **S**: Maintained
  - **F**: contrib/xxhash/
  - **T**: https://github.com/Cyan4973/xxHash.git

### Management Daemon - glusterd
  - **M**: Mohit Agrawal \<moagrawa@redhat.com\>
  - **M**: Atin Mukherjee \<atin.mukherjee83@gmail.com\>
  - **P**: Srijan Sivakumar \<ssivakum@redhat.com\>
  - **S**: Maintained
  - **F**: cli/
  - **F**: xlators/mgmt/glusterd/

### Protocol
  - **M**: Niels de Vos \<ndevos@redhat.com\>
  - **P**: Mohammed Rafi KC \<rafi.kavungal@iternity.com\>
  - **S**: Maintained
  - **F**: xlators/protocol/

### Remote Procedure Call subsystem
  - **P**: Mohit Agrawal \<moagrawa@redhat.com\>
  - **S**: Maintained
  - **F**: rpc/rpc-lib/
  - **F**: rpc/xdr/

### Snapshot
  - **M**: Raghavendra Bhat \<raghavendra@redhat.com\>
  - **P**: Mohammed Rafi KC \<rafi.kavungal@iternity.com\>
  - **P**: Sunny Kumar \<sunkumar@redhat.com\>
  - **S**: Maintained
  - **F**: xlators/mgmt/glusterd/src/glusterd-snap*
  - **F**: extras/snap-scheduler.py

### Socket subsystem
  - **P**: Mohammed Rafi KC \<rafi.kavungal@iternity.com\>
  - **P**: Mohit Agrawal \<moagrawa@redhat.com\>
  - **S**: Maintained
  - **F**: rpc/rpc-transport/socket/

### Testing - .t framework
  - **S**: Stable
  - **F**: tests/

### Utilities
  - **M**: Aravinda Vishwanathapura \<aravinda@kadalu.io\>
  - **P**: Niels de Vos \<ndevos@redhat.com\>
  - **P**: Raghavendra Talur \<rtalur@redhat.com\>
  - **P**: Sachidanda Urs \<sacchi@kadalu.io\>
  - **S**: Maintained
  - **F**: extras/

### Events APIs
  - **M**: Aravinda Vishwanathapura \<aravinda@kadalu.io\>
  - **P**: Srijan Sivakumar \<ssivakum@redhat.com\>
  - **S**: Maintained
  - **F**: events/
  - **F**: libglusterfs/src/events*
  - **F**: libglusterfs/src/eventtypes*
  - **F**: extras/systemd/glustereventsd*


## Special Thanks

GlusterFS would not be possible without the contributions of:


  - **M**: Anand Avati \<avati@cs.stanford.edu\>
  - **M**: Vijay Bellur \<vbellur@redhat.com\>
  - **M**: Jeff Darcy \<jeff@pl.atyp.us\>
  - **M**: Basavanagowda Kanur
  - **M**: Vikas Gorur
  - **M**: Krishna Srinivas
  - **M**: Shreyas Siravara \<sshreyas@fb.com\>
  - **M**: Kaushal M \<kaushal@redhat.com\>
  - **M**: Nigel Babu
  - **M**: Prashanth Pai
  - **P**: Sanoj Unnikrishnan
  - **P**: Milind Changire \<mchangir@redhat.com\>
  - **P**: Sunil Kumar Acharya \<sheggodu@redhat.com\>
  - **M**: Samikshan Bairagya \<samikshan@gmail.com\>
  - **M**: Chris Hertel
  - **M**: M. Mohan Kumar \<mohan@in.ibm.com\>
  - **M**: Shishir Gowda \<gowda.shishir@gmail.com\>
  - **M**: Brian Foster \<bfoster@redhat.com\>
  - **M**: Dennis Schafroth \<dennis@schafroth.com\>
  - **M**: Harshavardhana \<harsha@harshavardhana.net\>
  - **M**: Krishnan Parthasarathi
  - **M**: Justin Clift \<justin@gluster.org\>
  - **M**: Venky Shankar \<vshankar@redhat.com\>
  - **M**: Shravan Chandrashekar \<shravantc99@gmail.com\>
  - **M**: Joseph Fernandes
  - **M**: Vijaikumar Mallikarjuna
  - **M**: Anand Subramanian
  - **M**: Bharata B Rao \<bharata@linux.vnet.ibm.com\>
  - **M**: Rajesh Joseph
  - **M**: Dan Lambright
  - **M**: Jay Vyas
  - **M**: Luis Pabon
  - **M**: Ira Cooper
  - **M**: Shwetha Panduranga
  - **M**: Nithya Balachandran
  - **M**: Raghavendra Gowdappa
  - **M**: Susant Palai
  - **M**: Krutika Dhananjay
  - **M**: Shyam Ranganathan \<srangana@redhat.com\>
  - **M**: Poornima G
  - **M**: Hari Gowtham
  - **M**: Sanju Rakonde

And other 200+ contributors who worked on the project.

More information on contributors can be checked by [who-wrote-glusterfs script](./extras/who-wrote-glusterfs). If you need very basic numbers, run `git log --oneline --format="%an : %ae" | sort | uniq -c | sort -n -r | less` :-)
