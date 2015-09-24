Bugs often get fixed in master before release branches.

When a bug is fixed in the master branch it might be desirable or
necessary to backport the fix to a stable branch.

This page is intended to help organize support (and prioritization) for
backporting bug fixes of importance to the community.

### GlusterFs 3.6

Requested Backports for 3.6.0
-----------------------------

The tracker bug for 3.6.0 :
<https://bugzilla.redhat.com/show_bug.cgi?id=glusterfs-3.6.0>

Please add 'glusterfs-3.6.0' in the 'Blocks' field of bugs to propose
inclusion in GlusterFS 3.6.0.

### GlusterFs 3.5

Requested Backports for 3.5.3
-----------------------------

Current [list of bugs planned for
inclusion](https://bugzilla.redhat.com/showdependencytree.cgi?hide_resolved=0&id=glusterfs-3.5.3).

-   File a new bug for backporting a patch to 3.5.3:
    [<https://bugzilla.redhat.com/enter_bug.cgi?product=GlusterFS&blocked=glusterfs-3.5.3&version=3.5.2&short_desc=backport%20request%20for%20>...
    new glusterfs-3.5.3 backport request]

### GlusterFs 3.4

Requested Backports for 3.4.6
-----------------------------

The tracker bug for 3.4.6 :
<https://bugzilla.redhat.com/show_bug.cgi?id=glusterfs-3.4.6>

Please add 'glusterfs-3.4.6' in the 'Blocks' field of bugs to propose
inclusion in GlusterFS 3.4.6.

<https://bugzilla.redhat.com:443/show_bug.cgi?id=1116150>
<https://bugzilla.redhat.com:443/show_bug.cgi?id=1117851>

Requested Backports for 3.4.4
-----------------------------

<https://bugzilla.redhat.com/show_bug.cgi?id=859581> - "self-heal
process can sometimes create directories instead of symlinks for the
root gfid file in .glusterfs"

<https://bugzilla.redhat.com/show_bug.cgi?id=1041109> - "structure needs
cleaning" message appear when accessing files.

<https://bugzilla.redhat.com/show_bug.cgi?id=1073023> - glusterfs mount
crash after remove brick, detach peer and termination

Requested Backports for 3.4.3
-----------------------------

<https://bugzilla.redhat.com/show_bug.cgi?id=859581> - "self-heal
process can sometimes create directories instead of symlinks for the
root gfid file in .glusterfs"

<https://bugzilla.redhat.com/show_bug.cgi?id=1041109> - "structure needs
cleaning" message appear when accessing files.

<https://bugzilla.redhat.com/show_bug.cgi?id=977492> - large NFS writes
to Gluster slow down then stop

<https://bugzilla.redhat.com/show_bug.cgi?id=1073023> - glusterfs mount
crash after remove brick, detach peer and termination

Requested Backports for 3.3.3
-----------------------------

[Enable fusermount by default, make nightly autobuilding
work](https://bugzilla.redhat.com/1058666)

Requested Backports for 3.4.2
-----------------------------

Please enter bugzilla ID or patch URL here:

​1) Until RDMA handling is improved, we should output a warning when
using RDMA volumes -
<https://bugzilla.redhat.com/show_bug.cgi?id=1017176>

​2) Unable to shrink volumes without dataloss -
<https://bugzilla.redhat.com/show_bug.cgi?id=1024369>

​3) cluster/dht: Allow non-local clients to function with nufa volumes.
- <http://review.gluster.org/5414>

Requested Backports for 3.4.1
-----------------------------

Please enter bugzilla ID or patch URL here.

<https://bugzilla.redhat.com/show_bug.cgi?id=812230> - "quota context
not set in inode"

<https://bugzilla.redhat.com/show_bug.cgi?id=893778> - "NFS crash bug"

A note for whoever reviews this list: These are the fixes for issues
that have caused actual service disruption in our production
installation and thus are absolutely required for us (-- Lubomir
Rintel):

<https://bugzilla.redhat.com/show_bug.cgi?id=994392> - "Setting ACL
entries fails with glusterfs-3.4.0"

<https://bugzilla.redhat.com/show_bug.cgi?id=991622> - "fd leaks
observed while running dbench with "open-behind" volume option set to
"on" on a replicate volume"

These are issues that we've stumbled upon during the git log review and
that seemed scary enough for us to cherry-pick them to avoid risk,
despite not being actually hit. Hope that helps deciding whether it's
worthwhile cherry-picking them (-- Lubomir Rintel):

<https://bugzilla.redhat.com/show_bug.cgi?id=961691> "CLI crash upon
executing "gluster peer status" command"

<https://bugzilla.redhat.com/show_bug.cgi?id=965995> "quick-read and
open-behind xlator: Make options (volume\_options ) structure NULL
terminated."

<https://bugzilla.redhat.com/show_bug.cgi?id=958691> "nfs-root-squash:
rename creates a file on a file residing inside a sticky bit set
directory"

<https://bugzilla.redhat.com/show_bug.cgi?id=982919> "DHT : files are
stored on directory which doesn't have hash range(hash layout)"

<https://bugzilla.redhat.com/show_bug.cgi?id=976189> "statedump crashes
in ioc\_inode\_dump"

<https://bugzilla.redhat.com/show_bug.cgi?id=982174> "cli crashes when
setting diagnostics.client-log-level is set to trace"

<https://bugzilla.redhat.com/show_bug.cgi?id=989579> "glusterfsd crashes
on smallfile benchmark"

<http://review.gluster.org/5821>, "tests: call 'cleanup' at the end of
each test", <https://bugzilla.redhat.com/show_bug.cgi?id=1004756>,
backport of 983975

<http://review.gluster.org/5822>, "glusterfs-api.pc.in contains an
rpath", <https://bugzilla.redhat.com/show_bug.cgi?id=1004751>, backport
of 1002220

<http://review.gluster.org/5824> "glusterd.service (systemd), ensure
glusterd starts before any local gluster mounts",
<https://bugzilla.redhat.com/show_bug.cgi?id=1004796>, backport of
1004795

<https://bugzilla.redhat.com/show_bug.cgi?id=819130> meta, check that
glusterfs.spec.in has all relevant updates

<https://bugzilla.redhat.com/show_bug.cgi?id=1012400> - Glusterd would
not store all the volumes when a global options were set leading to peer
rejection

Requested Backports
-------------------

-   Please backport [gfapi: Closed the logfile fd and initialize to NULL
    in glfs\_fini](http://review.gluster.org/#/c/6552) into release-3.5
    - Done
-   Please backport [cluster/dht: Make sure loc has
    gfid](http://review.gluster.org/5178) into release-3.4
-   Please backport [Bug 887098](http://goo.gl/QjeMP) into release-3.3
    (FyreFoX) - Done
-   Please backport [Bug 856341](http://goo.gl/9cGAC) into release-3.2
    and release-3.3 (the-me o/b/o Debian) - Done for release-3.3
-   Please backport [Bug 895656](http://goo.gl/ZNs3J) into release-3.2
    and release-3.3 (semiosis, x4rlos) - Done for release-3.3
-   Please backport [Bug 918437](http://goo.gl/1QRyw) into release-3.3
    (tjstansell) - Done
-   Please backport into [Bug
    884597](https://bugzilla.redhat.com/show_bug.cgi?id=884597)
    release-3.3 (nocko) - Done

Unaddressed bugs
----------------

-   [Bug 838784](https://bugzilla.redhat.com/show_bug.cgi?id=838784)
-   [Bug 893778](https://bugzilla.redhat.com/show_bug.cgi?id=893778)
-   [Bug 913699](https://bugzilla.redhat.com/show_bug.cgi?id=913699);
    possibly related to [Bug
    884597](https://bugzilla.redhat.com/show_bug.cgi?id=884597)