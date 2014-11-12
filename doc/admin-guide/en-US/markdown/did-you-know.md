#Did you know?

This document is an attempt to describe less-documented behaviours and features
of GlusterFS that an admin always wanted to know but was too shy or busy to
ask.

## Trusted Volfiles

Observant admins would have wondered why there are two similar volume files for
every volume, namely trusted-<VOLNAME>-fuse.vol and <VOLNAME>-fuse.vol. To
appreciate this one needs to know about the IP address/hostname based access
restriction schemes available in GlusterFS. They are "auth-allow" and
"auth-reject".  The "auth-allow" and "auth-reject" options take a comma
separated list of IP addresses/hostnames as value. "auth-allow" allows access
_only_ to clients running on machines whose IP address/hostname are on this
list.  It is highly likely for an admin to configure the "auth-allow" option
without including the list of nodes in the cluster. One would expect this to
work.  Previously, in this configuration (internal) clients such as
gluster-nfs, glustershd etc., running in the trusted storage pool, would be
denied access to the volume.  This is undesirable and counter-intuitive. The
work around was to add the IP address/hostnames of all the nodes in the trusted
storage pool to the "auth-allow" list. This is bad for a reasonably large
number of nodes. To fix this, an alternate authentication mechanism for nodes
in the storage pool was introduced. Following is a brief explanation of how
this works.

The volume file with trusted prefix in its name (i.e trusted-volfile) has a
username and password option in the client xlator. The trusted-volfile is used
_only_ by mount processes running in the trusted storage pool (hence the name).
The username and password, when present, allow "mount" (and other glusterfs)
processes to access the brick processes even if the node they are running on is
not explicitly added in "auth-allow" addresses. 'Regular' mount processes,
running on nodes outside the trusted storage pool, use the non-trusted-volfile.
The important thing to note is that "trusted" in this context only implied
belonging to the trusted storage pool.

