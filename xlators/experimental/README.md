# Purpose of this directory

This directory is created to host experimental gluster translators. A new
translator that is *experimental* in nature, would need to create its own
subdirectory under this directory, to host/publish its work.

Example:
  The first commit should include the following changes
    1. xlators/experimental/Makefile.am
      NOTE: Add foobar to the list of SUBDIRS here
    2. xlators/experimental/foobar
    3. xlators/experimental/foobar/Makefle.am
      NOTE: Can be empty initially in the first commit
    4. configure.ac
      NOTE: Include your experimental Makefile under AC_CONFIG_FILES
    5. xlators/experimental/foobar/README.md
      NOTE: The readme should cover details as required for the translator to be
      accepted as experimental, primarily including a link to the specification
      under the gluster-specs repository [1]. Later the readme should suffice
      as an entry point for developers and users alike, who wish to experiment
      with the xlator under development
    6. xlators/experimental/foobar/TODO.md
      NOTE: This is a list of TODO's identified during the development process
      that needs addressing over time. These include exceptions granted during
      the review process, for things not addressed when commits are merged into
      the repository

# Why is it provided

Quite often translator development that happens out of tree, does not get
enough eyeballs early in its development phase, has not undergone CI
(regression/continuous integration testing), and at times is not well integrated
with the rest of gluster stack.

Also, when such out of tree translators are submitted for acceptance, it is a
bulk commit that makes review difficult and inefficient. Such submissions also
have to be merged forward, and depending on the time spent in developing the
translator the master branch could have moved far ahead, making this a painful
activity.

Experimental is born out of such needs, to provide xlator developers,
  - Early access to CI
  - Ability to adapt to ongoing changes in other parts of gluster
  - More eye balls on the code and design aspects of the translator
  - TBD: What else?

and for maintainers,
  - Ability to look at smaller change sets in the review process
  - Ability to verify/check implementation against the specification provided

# General rules

1. If a new translator is added under here it should, at the very least, pass
compilation.

2. All translators under the experimental directory are shipped as a part of
gluster-experimental RPMs.
TBD: Spec file and other artifacts for the gluster-experimental RPM needs to be
fleshed out.

3. Experimental translators can leverage the CI framework as needed. Tests need
to be hosted under xlators/experimental/tests initially, and later moved to the
appropriate tests/ directory as the xlator matures. It is encouraged to provide
tests for each commit or series of commits, so that code and tests can be
inspected together.

4. If any experimental translator breaks CI, it is quarantined till demonstrable
proof towards the contrary is provided. This is applicable as tests are moved
out of experimental tests directory to the CI framework directory, as otherwise
experimental tests are not a part of regular CI regression runs.

5. An experimental translator need not function at all, as a result commits can
be merged pretty much at will as long as other rules as stated are not violated.

6. Experimental submissions will be assigned a existing maintainer, to aid
merging commits and ensure aspects of gluster code submissions are respected.
When an experimental xlator is proposed and the first commit posted
a mail to gluster-devel@gluster.org requesting attention, will assign the
maintainer buddy for the submission.
NOTE: As we scale, this may change.

6. More?

# Getting out of the experimental jail

So you now think your xlator is ready to leave experimental and become part of
mainline!
- TBD: guidelines pending.

# FAQs

1. How do I submit/commit experimental framework changes outside of my
experimental xlator?
  - Provide such framework changes as a separate commit
  - Conditionally ensure these are built or activated only when the experimental
  feature is activated, so as to prevent normal gluster workflow to function as
  before
  - TBD: guidelines and/or examples pending.

2. Ask your question either on gluster-devel@gluster.org or as a change request
to this file in gluster gerrit [2] for an answer that will be assimilated into
this readme.

# Links
[1] http://review.gluster.org/#/q/project:glusterfs-specs

[2] http://review.gluster.org/#/q/project:glusterfs
