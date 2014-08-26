# SwiftOnFile

SwiftOnFile project enables GlusterFS volume to be used as backend for Openstack
Swift - a distributed object store. This allows objects PUT over Swift's RESTful
API to be accessed as files over filesystem interface and vice versa i.e files
created over filesystem interface (NFS/FUSE/native) can be accessed as objects
over Swift's RESTful API.

SwiftOnFile project was formerly known as `gluster-swift` and also as `UFO
(Unified File and Object)` before that. More information about SwiftOnFile can
be found [here](https://github.com/swiftonfile/swiftonfile/blob/master/doc/markdown/quick_start_guide.md).
There are differences in working of gluster-swift (now obsolete) and swiftonfile
projects. The older gluster-swift code and relevant documentation can be found
in [icehouse branch](https://github.com/swiftonfile/swiftonfile/tree/icehouse)
of swiftonfile repo.

## SwiftOnFile vs gluster-swift

|                                                                                      Gluster-Swift                                                                                      |                                                                                               SwiftOnFile                                                                                               |
|:---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------:|:-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------:|
| One GlusterFS volume maps to and stores only one Swift account. Mountpoint Hierarchy: `container/object`                                                                                | One GlusterFS volume or XFS partition can have multiple accounts. Mountpoint Hierarchy: `acc/container/object`                                                                                          |
| Over-rides account server, container server and object server. We need to keep in sync with upstream Swift and often may need code changes or workarounds to support new Swift features | Implements only object-server. Very less need to catch-up to Swift as new features at proxy,container and account level would very likely be compatible with SwiftOnFile as it's just a storage policy. |
| Does not use DBs for accounts and container.A container listing involves a filesystem crawl.A HEAD on account/container gives inaccurate or stale results without FS crawl.             | Uses Swift's DBs to store account and container information. An account or container listing does not involve FS crawl. Accurate info on HEAD to account/container – ability to support account quotas. |
| GET on a container and account lists actual files in filesystem.                                                                                                                        | GET on a container and account only lists objects PUT over Swift. Files created over filesystem interface do not appear in container and object listings.                                               |
| Standalone deployment required and does not integrate with existing Swift cluster.                                                                                                      | Integrates with any existing Swift deployment as a Storage Policy.                                                                                                                                      |

