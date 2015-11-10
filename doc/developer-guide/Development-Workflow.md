Development work flow of Gluster
================================

This document provides a detailed overview of the development model
followed by the GlusterFS project.

For a simpler overview visit
[Simplified develoment workflow](./Simplified Development Workflow.md).

Basics
------

The GlusterFS development model largely revolves around the features and
functionality provided by Git version control system, Gerrit code review
system and Jenkins continuous integration system. It is a primer for a
contributor to the project.

### Git

Git is a extremely flexible, distributed version control system.
GlusterFS' main git repository is at <http://git.gluster.org> and public
mirrors are at GlusterForge
(https://forge.gluster.org/glusterfs-core/glusterfs) and at GitHub
(https://github.com/gluster/glusterfs). The development repo is hosted
inside Gerrit and every code merge is instantly replicated to the public
mirrors.

A good introduction to Git can be found at
<http://www-cs-students.stanford.edu/~blynn/gitmagic/>.

### Gerrit

Gerrit is an excellent code review system which is developed with a git
based workflow in mind. The GlusterFS project code review system is
hosted at [review.gluster.org](http://review.gluster.org). Gerrit works
on "Change"s. A change is a set of modifications to various files in
your repository to accomplish a task. It is essentially one large git
commit with all the necessary changes which can be both built and
tested.

Gerrit usage is described later in 'Review Process' section.

### Jenkins

Jenkins is a Continuous Integration build system. Jenkins is hosted at
<http://build.gluster.org>. Jenkins is configured to work with Gerrit by
setting up hooks. Every "Change" which is pushed to Gerrit is
automatically picked up by Jenkins, built and smoke tested. Output of
all builds and tests can be viewed at
<http://build.gluster.org/job/smoke/>. Jenkins is also setup with a
'regression' job which is designed to execute test scripts provided as
part of the code change.

Preparatory Setup
-----------------

Here is a list of initial one-time steps before you can start hacking on
code.

### Register

Sign up for an account at <http://review.gluster.org> by clicking
'Register' on the right-hand top. You can use your gmail login as the
openID identity.

### Preferred email

On first login, add your git/work email to your identity. You will have
to click on the URL which is sent to your email and set up a proper Full
Name. Make sure you set your git/work email as your preferred email.
This should be the email address from which all your code commits are
associated.

### Set Username

Select yourself a username.

### Watch glusterfs

In Gerrit settings, watch the 'glusterfs' project. Tick on all the three
(New Changes, All Comments, Submitted Changes) types of notifications.

### Email filters

Set up a filter rule in your mail client to tag or classify mails with
the header

        List-Id: <gerrit-glusterfs.review.gluster.org>

as mails originating from the review system.

### SSH keys

Provide your SSH public key into Gerrit so that you can successfully
access the development git repo as well as push changes for
review/merge.

### Clone a working tree

Get yourself a working tree by cloning the development repository from
Gerrit

        sh$ git clone ssh://[username)@]git.gluster.org/glusterfs.git glusterfs

Branching policy
----------------

This section describes both, the branching policies on the public repo
as well as the suggested best-practice for local branching

### Master/release branches

In glusterfs.git, the master branch is the forward development branch.
This is where new features come in first. In fact this is where almost
every change (commit) comes in first. The master branch is always kept
in a buildable state and smoke tests pass.

Release trains (3.1.z, 3.2.z, 3.2.z) each have a branch originating from
master. Code freeze of each new release train is marked by the creation
of the release-3.y branch. At this point no new features are added to
the release-3.y branch. All fixes and commits first get into master.
From there, only bug fixes get backported to the relevant release
branches. From the release-3.y branch, actual release code snapshots
(e.g. glusterfs-3.2.2 etc.) are tagged (git annotated tag with 'git tag
-a') shipped as a tarball.

### Personal per-task branches

As a best practice, it is recommended you perform all code changes for a
task in a local branch in your working tree. The local branch should be
created from the upstream branch to which you intend to submit the
change. If you are submitting changes to master branch, first create a
local task branch like this -

        sh$ git checkout master
        sh$ git branch bug-XYZ && git checkout bug-XYZ
        ... <hack, commit>

If you are backporting a fix to a release branch, or making a new change
to a release branch, your commands would be slightly different. If you
are checking out a release branch in your local working tree for the
first time, make sure to set it up as a remote tracking branch like this
-

        sh$ git checkout -b release-3.2 origin/release-3.2

The above step is not necessary to be repeated. In the future if you
want to work to the release branch -

        sh$ git checkout release-3.2
        sh$ git branch bug-XYZ-release-3.2 && git checkout bug-XYZ-release-3.2
        ... <cherry-pick, hack, commit>

Building
--------

### Environment Setup

**For details about the required packages for the build environment
refer : [Building GlusterFS](./Building GlusterFS.md)**

Ubuntu:

To setup the build environment on an Ubuntu system, type the following
command to install the required packages:

        sudo apt-get -y install python-pyxattr libreadline-dev tar
        python-pastedeploy python-simplejson python-sphinx python-webob libssl-dev
        pkg-config python-dev python-eventlet python-netifaces libaio-dev libibverbs-dev
        libtool libxml2-dev liblvm2-dev make autoconf automake bison dos2unix flex libfuse-dev

CentOS/RHEL/Fedora:

On Fedora systems, install the required packages by following the
instructions in [CompilingRPMS](./Compiling RPMS.md).

### Creating build environment

Once the required packages are installed for your appropiate system,
generate the build configuration:

        sh$ ./autogen.sh
        sh$ ./configure --enable-fusermount

### Build and install

#### GlusterFS

Ubuntu:

Type the following to build and install GlusterFS on the system:

        sh$ make
        sh$ make install

CentOS/RHEL/Fedora:

In an rpm based system, there are two methods to build GlusterFS. One is
to use the method describe above for *Ubuntu*. The other is to build and
install RPMS as described in [CompilingRPMS](./Compiling RPMS.md).

#### GlusterFS UFO/SWIFT

To build and run Gluster UFO you can do the following:

1.  Build, create, and install the RPMS as described in
    [CompilingRPMS](./Compiling RPMS.md).
2.  Configure UFO/SWIFT as described in [Howto Using UFO SWIFT - A quick
    and dirty setup
    guide](http://www.gluster.org/2012/09/howto-using-ufo-swift-a-quick-and-dirty-setup-guide)

Commit policy
-------------

For a Gerrit based work flow, each commit should be an independent,
buildable and testable change. Typically you would have a local branch
per task, and most of the times that branch will have one commit.

If you have a second task at hand which depends on the changes of the
first one, then technically you can have it as a separate commit on top
of the first commit. But it is important that the first commit should be
a testable change by itself (if not, it is an indication that the two
commits are essentially part of a single change). Gerrit accommodates
these situations by marking Change 1 as a "dependency" of Change 2
(there is a 'Dependencies' tab in the Change page in Gerrit)
automatically when you push the changes for review from the same local
branch.

You will need to sign-off your commit (git commit -s) before sending the
patch for review. By signing off your patch, you agree to the terms
listed under "Developer's Certificate of Origin" section in the
CONTRIBUTING file available in the repository root.

Provide a meaningful commit message. Your commit message should be in
the following format

-   A short one line subject describing what the patch accomplishes
-   An empty line following the subject
-   Situation necessitating the patch
-   Description of the code changes
-   Reason for doing it this way (compared to others)
-   Description of test cases

### Test cases

Part of the workflow is to aggregate and execute pre-commit test cases
which accompany patches, cumulatively for every new patch. This
guarantees that tests which are working till the present are not broken
with the new patch. Every change submitted to Gerrit much include test
cases in

        tests/group/script.t

as part of the patch. This is so that code changes and accompanying test
cases are reviewed together. All new commits now come under the
following categories w.r.t test cases:

#### New 'group' directory and/or 'script.t'

This is typically when code is adding a new module and/or feature

#### Extend/Modify old test cases in existing scripts

This is typically when present behavior (default values etc.) of code is
changed

#### No test cases

This is typically when code change is trivial (e.g. fixing typos in
output strings, code comments)

#### Only test case and no code change

This is typically when we are adding test cases to old code (already
existing before this regression test policy was enforced)

More details on how to work with test case scripts can be found in

tests/README

Review process
--------------

### rfc.sh

After doing the local commit, it is time to submit the code for review.
There is a script available inside glusterfs.git called rfc.sh. You can
submit your changes for review by simply executing

        sh$ ./rfc.sh

This script does the following:

-   The first time it is executed, it downloads a git hook from
    <http://review.gluster.org/tools/hooks/commit-msg> and sets it up
    locally to generate a Change-Id: tag in your commit message (if it
    was not already generated.)
-   Rebase your commit against the latest upstream HEAD. This rebase
    also causes your commits to undergo massaging from the just
    downloaded commit-msg hook.
-   Prompt for a Bug Id for each commit (if it was not already provded)
    and include it as a "BUG:" tag in the commit log. You can just hit
    <enter> at this prompt if your submission is purely for review
    purposes.
-   Push the changes to review.gluster.org for review. If you had
    provided a bug id, it assigns the topic of the change as "bug-XYZ".
    If not it sets the topic as "rfc".

On a successful push, you will see a URL pointing to the change in
review.gluster.org

Auto verification
-----------------

The integration between Jenkins and Gerrit triggers an event in Jenkins
on every push of changes, to pick up the change and run build and smoke
test on it.

If the build and smoke tests execute successfuly, Jenkins marks the
change as '+0 Verified'. If they fail, '-1 Verified' is marked on the
change. This means passing the automated smoke test is a necessary
condition but not sufficient.

It is important to note that Jenkins verification is only a generic
verification of high level tests. More concentrated testing effort for
the patch is necessary with manual verification.

If auto verification fails, it is a good reason to skip code review till
a fixed change is pushed later. You can click on the build URL
automatically put as a comment to inspect the reason for auto
verification failure. In the Jenkins job page, you can click on the
'Console Output' link to see the exact point of failure.

Reviewing / Commenting
----------------------

Code review with Gerrit is relatively easy compared to other available
tools. Each change is presented as multiple files and each file can be
reviewed in Side-by-Side mode. While reviewing it is possible to comment
on each line by double-clicking on it and writing in your comments in
the text box. Such in-line comments are saved as drafts, till you
finally publish them as a Review from the 'Change page'.

There are many small and handy features in Gerrit, like 'starring'
changes you are interested to follow, setting the amount of context to
view in the side-by-side view page etc.

Incorporate, Amend, rfc.sh, Reverify
------------------------------------

Code review comments are notified via email. After incorporating the
changes in code, you can mark each of the inline comment as 'done'
(optional). After all the changes to your local files, amend the
previous commit with these changes with -

        sh$ git commit -a --amend

Push the amended commit by executing rfc.sh. If your previous push was
an "rfc" push (i.e, without a Bug Id) you will be prompted for a Bug Id
again. You can re-push an rfc change without any other code change too
by giving a Bug Id.

On the new push, Jenkins will re-verify the new change (independent of
what the verification result was for the previous push).

It is the Change-Id line in the commit log (which does not change) that
associates the new push as an update for the old push (even though they
had different commit ids) under the same Change. In the side-by-side
view page, it is possible to set knobs in the 'Patch History' tab to
view changes between patches as well. This is handy to inspect how
review comments were incorporated.

If further changes are found necessary, comments can be made on the new
patch as well, and the same cycle repeats.

If no further changes are necessary, the reviewer can mark the patch as
reviewed with a certain score depending on the depth of review and
confidence (+1 or +2). A -1 review indicates non-agreement for the
change to get merged upstream.

Regression tests and test cases
-------------------------------

All code changes which are not trivial (typo fixes, code comment
changes) must be accompanied with either a new test case script or
extend/modify an existing test case script. It is important to review
the test case in conjunction with the code change to analyse whether the
code change is actually verified by the test case.

Regression tests (i.e, execution of all test cases accumulated with
every commit) is not automatically triggered as the test cases can be
extensive and is quite expensive to execute for every change submission
in the review/resubmit cycle. Instead it is triggered by the
maintainers, after code review. Passing the regression test is a
necessary condition for merge along with code review points.

Submission Qualifiers
---------------------

For a change to get merged, there are two qualifiers which are enforced
by the Gerrit system. They are - A change should have at least one '+2
Reviewed', and a change should have at least one '+1 Verified'
(regression test). The project maintainer will merge the changes once a
patch meets these qualifiers.

Submission Disqualifiers
------------------------

There are three types of "negative votes".

-1 Verified

-1 Code-Review ("I would prefer that you didn't submit this")

-2 Code-Review ("Do not submit")

The implication and scope of each of the three are different. They
behave differently as changes are resubmitted as new patchsets.

### -1 Verified

Anybody voting -1 Verified will prevent \*that patchset only\* from
getting merged. The flag is automatically cleared on the next patchset
post. The intention is that this vote is based on the result of some
kind of testing. A voter is expected to explain the test case which
failed. Jenkins jobs (smoke, regression, ufounit) use this field for
voting -1/0/+1. When voting -1, Jenkins posts the link to the URL which
has the console output of the failed job.

### -1 Code-Review ("I would prefer that you didn't submit this")

This is an advisory vote based on the content of the patch. Typically
issues in source code (both design and implementation), source code
comments, log messages, license headers etc. found by human inspection.
The reviewer explains the specific issues by commenting against the most
relevant lines of source code in the patch. On a resubmission, -1 votes
are cleared automatically. It is the responsibility of the maintainers
to honor -1 Code-Review votes from reviewers (by not merging the
patches), and inspecting that -1 comments on previous submissions are
addressed in the new patchset. Generally this is the recommended
"negative" vote.

### -2 Code-Review ("Do not submit")

This is a stronger vote which actually prevents Gerrit from merging the
patch. The -2 vote persists even after resubmission and continues to
prevent the patch from getting merged, until the voter revokes the -2
vote (and then is further subjected to Submission Qualifiers). Typically
one would vote -2 if they are \*against the goal\* of what the patch is
trying to achieve (and not an issue with the patch, which can change on
resubmission). A reviewer would also vote -2 on a patch even if there is
agreement with the goal, but the issue in the code is of such a critical
nature that the reviewer personally wants to inspect the next patchset
and only then revoke the vote after finding the new patch satisfactory.
This prevents the merge of the patch in the mean time. Every registered
user has the right to exercise the -2 Code review vote, and cannot be
overridden by the maintainers.
