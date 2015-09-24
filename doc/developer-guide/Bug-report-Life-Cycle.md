This page describes the life of a bug report.

-   When a bug is first reported, it is given the **NEW** status.
-   Once a developer has started, or is planning to work on a bug, the
    status **ASSIGNED** is set. The "Assigned to" field should mention a
    specific developer.
-   If an initial
    [patch](https://en.wikipedia.org/wiki/Patch_(computing)) for a bug
    has been put into the [Gerrit code review
    tool](http://review.gluster.org), the status **POST** should be set
    manually. The status **POST** should only be used when all patches
    for a specific bug have been posted for review.
-   After a review of the patch, and passing any automated regression
    tests, the patch will get merged by one of the maintainers. When the
    patch has been merged into the git repository, a comment is added to
    the bug. Only when all needed patches have been merged, the assigned
    engineer will need to change the status to **MODIFIED**.
-   Once a package is available with fix for the bug, the status should
    be moved to **ON\_QA**.
    -   The **Fixed in version** field should get the name/release of
        the package that contains the fix. Packages for multiple
        distributions will mostly get available within a few days after
        the *make dist* tarball was created.
    -   This tells the bug reporter that a package is available with fix
        for the bug and that they should test the package.
    -   The release maintainer need to do this change to bug status,
        scripts are available (ask *ndevos*).
-   The status **VERIFIED** is set if a QA tester or the reporter
    confirmed the fix after fix is merged and new build with the fix
    resolves the issue.
-   In case the version does not fix the reported bug, the status should
    be moved back to **ASSIGNED** with a clear note on what exactly
    failed.
-   When a report has been solved it is given **CLOSED** status. This
    can mean:
    -   **CLOSED/CURRENTRELEASE** when a code change that fixes the
        reported problem has been merged in
        [Gerrit](http://review.gluster.org).
    -   **CLOSED/WONTFIX** when the reported problem or suggestion is
        valid, but any fix of the reported problem or implementation of
        the suggestion would be barred from approval by the project's
        Developers/Maintainers (or product managers, if existing).
    -   **CLOSED/WORKSFORME** when the problem can not be reproduced,
        when missing information has not been provided, or when an
        acceptable workaround exists to achieve a similar outcome as
        requested.
    -   **CLOSED/CANTFIX** when the problem is not a bug, or when it is
        a change that is outside the power of GlusterFS development. For
        example, bugs proposing changes to third-party software can not
        be fixed in the GlusterFS project itself.
    -   **CLOSED/DUPLICATE** when the problem has been reported before,
        no matter if the previous report has been already resolved or
        not.

If a bug report was marked as *CLOSED* or *VERIFIED* and it turns out
that this was incorrect, the bug can be changed to the status *ASSIGNED*
or *NEW*.