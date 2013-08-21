Regression tests framework for GlusterFS
========================================

## Prereq
- Build and install the version of glusterfs with your changes. Make
  sure the installed version is accessible from $PATH.

## How-To
- To mount glusterfs, NEVER use 'mount -t glusterfs', instead use
  'glusterfs -s ' method. This is because with the patch build setup
  doesnot install the /sbin/mount.glusterfs necessary, where as the
  glusterfs binary will be accessible with $PATH, and will pick the
  right version.
- (optional) Set environment variables to specify location of
  export directories and mount points. Unless you have special
  requirements, the defaults should just work. The variables
  themselves can be found at the top of tests/include.rc. All
  of them can be overriden with environment variables.

## Usage
- Execute `/usr/share/glusterfs/run-tests.sh` as root.

- If some test cases fail, report to GlusterFS community at
  `gluster-devel@nongnu.org`.

## Reminder
- BE WARNED THAT THE TEST CASES DELETE /var/lib/glusterd/* !!!