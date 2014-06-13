Regression tests framework for GlusterFS
========================================

## Prereq
- Build and install the version of glusterfs with your changes. Make
  sure the installed version is accessible from $PATH.

## Prereq for geo-rep regression tests.
- Passwordless ssh on the test system to itself
- arequal-checksum installed on the test-system.
  You can find the repo here - https://github.com/raghavendrabhat/arequal

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

- If you want to run individual tests located in `/usr/share/glusterfs/tests`
  as opposed to the full test-suite, invoke it as
  `/usr/share/glusterfs/run-tests.sh [pattern]*`, where pattern can be:
    - the trailing parts of the full path of a test,
      e.g. `tests/basic/mount.t`
    - the name of a file or directory, e.g `self-heal.t` or `basic/`
    - bug number, which will match against numbered bugs in the
      `tests/bugs/` directory.
    - a glob pattern (see `man 7 glob` for mor info on globs)

- To execute single ".t" file, use "prove -vf /path/to/.t"
- If some test cases fail, report to GlusterFS community at
  `gluster-devel@gluster.org`.

## Reminder
- BE WARNED THAT THE TEST CASES DELETE ``GLUSTERD_WORKDIR`` * !!!
