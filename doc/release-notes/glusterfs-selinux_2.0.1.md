# Release notes for glusterfs-selinux 2.0-1

This is a bugfix and improvement release.


## Important fixes in this release
- [#rhbz1955415](https://bugzilla.redhat.com/1955415) glusterfs-selinux package should own the files created by it
- [#20](https://github.com/gluster/glusterfs-selinux/issues/20) Fixing verification failure for ghost
- [#rhbz1779052](https://bugzilla.redhat.com/show_bug.cgi?id=1779052) Adds rule to allow glusterd to access RDMA socket


## Issues addressed in this release
- [#rhbz1955415](https://bugzilla.redhat.com/1955415) glusterfs-selinux package should own the files created by it
- [#22](https://github.com/gluster/glusterfs-selinux/pull/22) Fixed mixed use of tabs and spaces (rpmlint warning)
- [#20](https://github.com/gluster/glusterfs-selinux/issues/20) Fixing verification failure for ghost file
- [#rhbz1779052](https://bugzilla.redhat.com/show_bug.cgi?id=1779052) Adds rule to allow glusterd to access RDMA socket
- [#15](https://github.com/gluster/glusterfs-selinux/issues/15) Modifying the path provided for glustereventsd<span>.py
