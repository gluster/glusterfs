### Guidelines For Maintainers

GlusterFS has maintainers, sub-maintainers and release maintainers to
manage the project's codebase. Sub-maintainers are the owners for
specific areas/components of the source tree. Maintainers operate across
all components in the source tree.Release maintainers are the owners for
various release branches (release-x.y) present in the GlusterFS
repository.

In the guidelines below, release maintainers and sub-maintainers are
also implied when there is a reference to maintainers unless it is
explicitly called out.

### Guidelines that Maintainers are expected to adhere to

​1. Ensure qualitative and timely management of patches sent for review.

​2. For merging patches into the repository, it is expected of
maintainers to:

		a> Merge patches of owned components only.
		b> Seek approvals from all maintainers before merging a patchset spanning multiple components.
		c> Ensure that regression tests pass for all patches before merging.
		d> Ensure that regression tests accompany all patch submissions.
		e> Ensure that documentation is updated for a noticeable change in user perceivable behavior or design.
		f> Encourage code unit tests from patch submitters to improve the overall quality of the codebase.
		g> Not merge patches written by themselves until there is a +2 Code Review vote by other reviewers.

​3. The responsibility of merging a patch into a release branch in
normal circumstances will be that of the release maintainer's. Only in
exceptional situations, maintainers & sub-maintainers will merge patches
into a release branch.

​4. Release maintainers will ensure approval from appropriate
maintainers before merging a patch into a release branch.

​5. Maintainers have a responsibility to the community, it is expected
of maintainers to:

		a> Facilitate the community in all aspects.
		b> Be very active and visible in the community.
		c> Be objective and consider the larger interests of the community  ahead of individual interests.
		d> Be receptive to user feedback.
		e> Address concerns & issues affecting users.
		f> Lead by example.

### Queries on Guidelines

Any questions or comments regarding these guidelines can be routed to
gluster-devel at gluster dot org.

### Patches in Gerrit

Gerrit can be used to list patches that need reviews and/or can get
merged. Some queries have been prepared for this, edit the search box in
Gerrit to make your own variation:

-   [3.5 open reviewed/verified (non
    rfc)](http://review.gluster.org/#/q/project:glusterfs+branch:release-3.5+status:open+%28label:Code-Review%253D%252B1+OR+label:Code-Review%253D%252B2+OR+label:Verified%253D%252B1%29+NOT+topic:rfc+NOT+label:Code-Review%253D-2,n,z)
-   [All open 3.5 patches (non
    rfc)](http://review.gluster.org/#/q/project:glusterfs+branch:release-3.5+status:open+NOT+topic:rfc,n,z)
-   [Open NFS (master
    branch)](http://review.gluster.org/#/q/project:glusterfs+branch:master+status:open+message:nfs,n,z)

An other option can be used in combination with the Gerrit queries, and
has support for filename/directory matching (the queries above do not).
Go to the [settings](http://review.gluster.org/#/settings/projects) in
your Gerrit profile, and enter filters like these:

![gerrit-watched-projects](https://cloud.githubusercontent.com/assets/10970993/7411584/1a26614a-ef57-11e4-99ed-ee96af22a9a1.png)
