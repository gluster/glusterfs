Release Process for GlusterFS
=============================

Create tarball
--------------

1.  Add the release-notes to the docs/release-notes/ directory in the
    sources
2.  after merging the release-notes, create a tag like v3.6.2
3.  push the tag to git.gluster.org
4.  create the tarball with the [release job in
    Jenkins](http://build.gluster.org/job/release/)

Notify packagers
----------------

Notify the packagers that we need packages created. Provide the link to the
source tarball from the Jenkins release job to the [packagers
mailinglist](mailto:packaging@gluster.org). A list of the people involved in
the package maintenance for the different distributions is in the `MAINTAINERS`
file in the sources.

Create a new Tracker Bug for the next release
---------------------------------------------

The tracker bugs are used as guidance for blocker bugs and should get created when a release is made. To create one

- file a [new bug in Bugzilla](https://bugzilla.redhat.com/enter_bug.cgi?product=GlusterFS)
- base the contents on previous tracker bugs, like the one for [glusterfs-3.5.5](https://bugzilla.redhat.com/show_bug.cgi?id=glusterfs-3.5.5)
- set the '''Alias''' (it is a text-field) of the bug to 'glusterfs-a.b.c' where a.b.c is the next minor version
- save the new bug
- you should now be able to use the 'glusterfs-a.b.c' to access the bug, use the alias to replace the BZ# in URLs, or '''blocks''' fields
- bugs that were not fixed in this release, but were added to the tracker should be moved to the new tracker


Create Release Announcement
---------------------------

Create the Release Announcement (this is often done while people are
making the packages). The contents of the release announcement can be
based on the release notes, or should at least have a pointer to them.

Examples:

-   [blog](http://blog.gluster.org/2014/11/glusterfs-3-5-3beta2-is-now-available-for-testing/)
-   [release
    notes](https://github.com/gluster/glusterfs/blob/v3.5.3/doc/release-notes/3.5.3.md)

Send Release Announcement
-------------------------

Once the Fedora/EL RPMs are ready (and any others that are ready by
then), send the release announcement:

-   Gluster Mailing lists
    -   gluster-announce, gluster-devel, gluster-users
-   Gluster Blog
-   Gluster Twitter account
-   Gluster Facebook page
-   Gluster LinkedIn group - Justin has access
-   Gluster G+

Close Bugs
----------

Close the bugs that have all their patches included in the release.
Leave a note in the bug report with a pointer to the release
announcement.

Other things to consider
------------------------

-   Translations? - Are there strings needing translation?
