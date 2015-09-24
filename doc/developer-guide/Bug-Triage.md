Bug Triage Guidelines
=====================

-   Triaging of bugs is an important task; when done correctly, it can
    reduce the time between reporting a bug and the availability of a
    fix enormously.

-   Triager should focus on new bugs, and try to define the problem
    easily understandable and as accurate as possible. The goal of the
    triagers is to reduce the time that developers need to solve the bug
    report.

-   A triager is like an assistant that helps with the information
    gathering and possibly the debugging of a new bug report. Because a
    triager helps preparing a bug before a developer gets involved, it
    can be a very nice role for new community members that are
    interested in technical aspects of the software.

-   Triagers will stumble upon many different kind of issues, ranging
    from reports about spelling mistakes, or unclear log messages to
    memory leaks causing crashes or performance issues in environments
    with several hundred storage servers.

Nobody expects that triagers can prepare all bug reports. Therefore most
developers will be able to assist the triagers, answer questions and
suggest approaches to debug and data to gather. Over time, triagers get
more experienced and will rely less on developers.

**Bug triage can be summarised as below points:**

-   Is there enough information in the bug description?
-   Is it a duplicate bug?
-   Is it assigned to correct component of GlusterFS?
-   Are the Bugzilla fields correct?
-   Is the bug summary is correct?
-   Assigning bugs or Adding people to the "CC" list
-   Fix the Severity And Priority.
-   Todo, If the bug present in multiple GlusterFS versions.
-   Add appropriate Keywords to bug.

The detailed discussion about the above points are below.

Weekly meeting about Bug Triaging
---------------------------------

We try to meet every week in \#gluster-meeting on Freenode. The meeting
date and time for the next meeting is normally updated in the
[agenda](https://public.pad.fsfe.org/p/gluster-bug-triage).

Getting Started: Find reports to triage
---------------------------------------

There are many different techniques and approaches to find reports to
triage. One easy way is to use these pre-defined Bugzilla reports (a
report is completely structured in the URL and can manually be
modified):

-   New **bugs** that do not have the 'Triaged' keyword [Bugzilla
    link](https://bugzilla.redhat.com/buglist.cgi?bug_status=NEW&f1=keywords&keywords=Triaged%2CFutureFeature&keywords_type=nowords&list_id=3014117&o1=nowords&product=GlusterFS&query_format=advanced&v1=Triaged)
-   New **features** that do not have the 'Triaged' keyword (identified
    by FutureFeature keyword, probably of interest only to project
    leaders) [Bugzilla
    link](https://bugzilla.redhat.com/buglist.cgi?bug_status=NEW&f1=keywords&f2=keywords&list_id=3014699&o1=nowords&o2=allwords&product=GlusterFS&query_format=advanced&v1=Triaged&v2=FutureFeature)
-   New glusterd bugs: [Bugzilla
    link](https://bugzilla.redhat.com/buglist.cgi?bug_status=NEW&product=GlusterFS&f1=keywords&o1=nowords&v1=Triaged&component=glusterd)
-   New Replication(afr) bugs: [Bugzilla
    link](https://bugzilla.redhat.com/buglist.cgi?bug_status=NEW&component=replicate&f1=keywords&list_id=2816133&o1=nowords&product=GlusterFS&query_format=advanced&v1=Triaged)
-   New distribute(DHT) bugs: [Bugzilla
    links](https://bugzilla.redhat.com/buglist.cgi?bug_status=NEW&component=distribute&f1=keywords&list_id=2816148&o1=nowords&product=GlusterFS&query_format=advanced&v1=Triaged)

-   New bugs against version 3.6:
    [<https://bugzilla.redhat.com/buglist.cgi?bug_status=NEW&product=GlusterFS&f1=keywords&f2=version&o1=nowords&o2=regexp&v1=Triaged&v2>=\^3.6
    Bugzilla link]
-   New bugs against version 3.5:
    [<https://bugzilla.redhat.com/buglist.cgi?bug_status=NEW&product=GlusterFS&f1=keywords&f2=version&o1=nowords&o2=regexp&v1=Triaged&v2>=\^3.5
    Bugzilla link]
-   New bugs against version 3.4:
    [<https://bugzilla.redhat.com/buglist.cgi?bug_status=NEW&product=GlusterFS&f1=keywords&f2=version&o1=nowords&o2=regexp&v1=Triaged&v2>=\^3.4
    Bugzilla link]

-   [<https://bugzilla.redhat.com/page.cgi?id=browse.html&product=GlusterFS&product_version>=&bug\_status=all&tab=recents
    bugzilla tracker] (can include already Triaged bugs)

-   [Untriaged NetBSD
    bugs](https://bugzilla.redhat.com/buglist.cgi?bug_status=NEW&keywords=Triaged&keywords_type=nowords&op_sys=NetBSD&product=GlusterFS)
-   [Untriaged FreeBSD
    bugs](https://bugzilla.redhat.com/buglist.cgi?bug_status=NEW&keywords=Triaged&keywords_type=nowords&op_sys=FreeBSD&product=GlusterFS)
-   [Untriaged Mac OS
    bugs](https://bugzilla.redhat.com/buglist.cgi?bug_status=NEW&keywords=Triaged&keywords_type=nowords&op_sys=Mac%20OS&product=GlusterFS)

In addition to manually checking Bugzilla for bugs to triage, it is also
possible to receive emails when new
bugs are filed or existing bugs get updated.

If at any point you feel like you do not know what to do with a certain
report, please first ask [irc or mailing
lists](http://www.gluster.org/community/index.html) before changing
something.

Is there enough information?
----------------------------

To make a report useful, the same rules apply as for
[bug reporting guidelines](./Bug Reporting Guidelines.md).

It's hard to generalize what makes a good report. For "average"
reporters is definitely often helpful to have good steps to reproduce,
GlusterFS software version , and information about the test/production
environment, Linux/GNU distribution.

If the reporter is a developer, steps to reproduce can sometimes be
omitted as context is obvious. *However, this can create a problem for
contributors that need to find their way, hence it is strongly advised
to list the steps to reproduce an issue.*

Other tips:

-   There should be only one issue per report. Try not to mix related or
    similar looking bugs per report.

-   It should be possible to call the described problem fixed at some
    point. "Improve the documentation" or "It runs slow" could never be
    called fixed, while "Documentation should cover the topic Embedding"
    or "The page at <http://en.wikipedia.org/wiki/Example> should load
    in less than five seconds" would have a criterion. A good summary of
    the bug will also help others in finding existing bugs and prevent
    filing of duplicates.

-   If the bug is a graphical problem, you may want to ask for a
    screenshot to attach to the bug report. Make sure to ask that the
    screenshot should not contain any confidential information.

Is it a duplicate?
------------------

Some reports in Bugzilla have already been reported before so you can
[search for an already existing
report](https://bugzilla.redhat.com/query.cgi?format=advanced). We do
not recommend to spend too much time on it; if a bug is filed twice,
someone else will mark it as a duplicate later. If the bug is a
duplicate, mark it as a duplicate in the resolution box below the
comment field by setting the **CLOSED DUPLICATE** status, and shortly
explain your action in a comment for the reporter. When marking a bug as
a duplicate, it is required to reference the original bug.

If you think that you have found a duplicate but you are not totally
sure, just add a comment like "This bug looks related to bug XXXXX" (and
replace XXXXX by the bug number) so somebody else can take a look and
help judging.

You can also take a look at
https://bugzilla.redhat.com/page.cgi?id=browse.html&product=GlusterFS&product_version>=&bug\_status=all&tab=duplicates's
list of existing duplicates

Is it assigned to correct component of GlusterFS?
-------------------------------------------------

Make sure the bug is assigned on right component. Below are the list of
GlusterFs components in bugzilla.

-   access control - Access control translator
-   BDB - Berkeley DB backend storage
-   booster - LD\_PRELOAD'able access client
-   build - Compiler, package management and platform specific warnings
    and errors
-   cli -gluster command line
-   core - Core features of the filesystem
-   distribute - Distribute translator (previously DHT)
-   errorgen - Error Gen Translator
-   fuse -mount/fuse translator and patched fuse library
-   georeplication - Gluster Geo-Replication
-   glusterd - Management daemon
-   HDFS - Hadoop application support over GlusterFS
-   ib-verbs - Infiniband verbs transport
-   io-cache - IO buffer caching translator
-   io-threads - IO threads performance translator
-   libglusterfsclient- API interface to access glusterfs volumes
    programatically
-   locks - POSIX and internal locks
-   logging - Centralized logging, log messages, log rotation etc
-   nfs- NFS component in GlusterFS
-   nufa- Non-Uniform Filesystem Scheduler Translator
-   object-storage - Object Storage
-   porting - Porting GlusterFS to different operating systems and
    platforms
-   posix - POSIX (API) based backend storage
-   protocol -Client and Server protocol translators
-   quick-read- Quick Read Translator
-   quota - Volume & Directory quota translator
-   rdma- RDMA transport
-   read-ahead - Read ahead (file) performance translator
-   replicate- Replication translator (previously AFR)
-   rpc - RPC Layer
-   scripts - Build scripts, mount scripts, etc.
-   stat-prefetch - Stat prefetch translator
-   stripe - Striping (RAID-0) cluster translator
-   trace- Trace translator
-   transport - Socket (IPv4, IPv6, unix, ib-sdp) and generic transport
    code
-   unclassified - Unclassified - to be reclassified as other components
-   unify - Unify translator and schedulers
-   write-behind- Write behind performance translator
-   libgfapi - APIs for GlusterFS
-   tests- GlusterFS Test Framework
-   gluster-hadoop - Hadoop support on GlusterFS
-   gluster-hadoop-install - Automated Gluster volume configuration for
    Hadoop Environments
-   gluster-smb - gluster smb
-   puppet-gluster - A puppet module for GlusterFS

Tips for searching:

-   As it is often hard for reporters to find the right place (product
    and component) where to file a report, also search for duplicates
    outside same product and component of the bug report you are
    triaging.
-   Use common words and try several times with different combinations,
    as there could be several ways to describe the same problem. If you
    choose the proper and common words, and you try several times with
    different combinations of those, you ensure to have matching
    results.
-   Drop the ending of a verb (e.g. search for "delet" so you get
    reports for both "delete" and "deleting"), and also try similar
    words (e.g. search both for "delet" and "remov").
-   Search using the date range delimiter: Most of the bug reports are
    recent, so you can try to increase the search speed using date
    delimiters by going to "Search by Change History" on the [search
    page](https://bugzilla.redhat.com/query.cgi?format=advanced).
    Example: search from "2011-01-01" or "-730d" (to cover the last two
    years) to "Now".

Are the fields correct?
-----------------------

### Summary

Sometimes the summary does not summarize the bug itself well. You may
want to update the bug summary to make the report distinguishable. A
good title may contain:

-   A brief explanation of the root cause (if it was found)
-   Some of the symptoms people are experiencing

### Adding people to the "CC" or changing the "Assigned to" field

Normally, developers and potential assignees of an area are already
CC'ed by default, but sometimes reports describe general issues or are
filed against common bugzilla products. Only if you know developers who
work in the area covered by the bug report, and if you know that these
developers accept getting CCed or assigned to certain reports, you can
add that person to the CC field or even assign the bug report to
her/him.

To get an idea who works in which area, check To know component owners ,
you can check the "MAINTAINERS" file in root of glusterfs code directory
or querying changes in [Gerrit](http://review.gluster.org) (see
[Simplified dev workflow](./Simplified Development Workflow.md))

### Severity And Priority

Please see below for information on the available values and their
meanings.

#### Severity

This field is a pull-down of the external weighting of the bug report's
importance and can have the following values:

  Severity     |Definition
  -------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------
  urgent       |catastrophic issues which severely impact the mission-critical operations of an organization. This may mean that the operational servers, development systems or customer applications are down or not functioning and no procedural workaround exists.
  high         |high-impact issues in which the customer's operation is disrupted, but there is some capacity to produce
  medium       |partial non-critical functionality loss, or issues which impair some operations but allow the customer to perform their critical tasks. This may be a minor issue with limited loss or no loss of functionality and limited impact to the customer's functionality
  low          |general usage questions, recommendations for product enhancement, or development work
  unspecified  |importance not specified

#### Priority

This field is a pull-down of the internal weighting of the bug report's
importance and can have the following values:

  Priority     |Definition
  -------------|------------------------
  urgent       |extremely important
  high         |very important
  medium       |average importance
  low          |not very important
  unspecified  |importance not specified


### Bugs present in multiple Versions

During triaging you might come across a particular bug which is present
across multiple version of GlusterFS. Here are the course of actions:

-   We should have separate bugs for each release (We should
    clone bugs if required)
-   Bugs in released versions should be depended on bug for mainline
    (master branch) if the bug is applicable for mainline.
    -   This will make sure that the fix would get merged in master
        branch first then the fix can get ported to other stable
        releases.

*Note: When a bug depends on other bugs, that means the bug cannot be
fixed unless other bugs are fixed (depends on), or this bug stops other
bugs being fixed (blocks)*

Here are some examples:

-   A bug is raised for GlusterFS 3.5 and the same issue is present in
    mainline (master branch) and GlusterFS 3.6
    -   Clone the original bug for mainline.
    -   Clone another for 3.6.
    -   And have the GlusterFS 3.6 bug and GlusterFS 3.5 bug 'depend on'
        the 'mainline' bug

-   A bug is already present for mainline, and the same issue is seen in
    GlusterFS 3.5.
    -   Clone the original bug for GlusterFS 3.5.
    -   And have the cloned bug (for 3.5) 'depend on' the 'mainline'
        bug.

### Keywords

Many predefined searches for Bugzilla include keywords. One example are
the searches for the triaging. If the bug is 'NEW' and 'Triaged' is no
set, you (as a triager) can pick it and use this page to triage it. When
the bug is 'NEW' and 'Triaged' is in the list of keyword, the bug is
ready to be picked up by a developer.

**Triaged**
:   Once you are done with triage add the **Triaged** keyword to the
    bug, so that others will know the triaged state of the bug. The
    predefined search at the top of this page will then not list the
    Triaged bug anymore. Instead, the bug should have moved to [this
    list](https://bugzilla.redhat.com/buglist.cgi?bug_status=NEW&keywords=Triaged&product=GlusterFS).

**EasyFix**
:   By adding the **EasyFix** keyword, the bug gets added to the [list
    of bugs that should be simple to fix](./Easy Fix Bugs.md).
    Adding this keyword is encouraged for simple and well defined bugs
    or feature enhancements.

**Patch**
:   When a patch for the problem has been attached or included inline,
    add the **Patch** keyword so that it is clear that some preparation
    for the development has been done already. If course, it would have
    been nicer if the patch was sent to Gerrit for review, but not
    everyone is ready to pass the Gerrit hurdle when they report a bug.
 
You can also add the **Patch** keyword when a bug has been fixed in
    mainline and the patch(es) has been identified. Add a link to the
    Gerrit change(s) so that backporting to a stable release is made
    simpler.

**Documentation**
:   Add the **Documentation** keyword when a bug has been reported for
    the documentation. This helps editors and writers in finding the
    bugs that they can resolve.

**Tracking**
:   This keyword is used for bugs which are used to track other bugs for
    a particular release. For example [3.6 tracker
    bug](https://bugzilla.redhat.com/showdependencytree.cgi?maxdepth=2&hide_resolved=1&id=glusterfs-3.6.0)

**FutureFeature**
:   This keyword is used for bugs which are used to request for a
    feature enhancement ( RFE - Requested Feature Enhancement) for
    future releases of GlusterFS. If you open a bug by requesting a
    feature which you would like to see in next versions of GlusterFS
    please report with this keyword.

Add yourself to the CC list
---------------------------

By adding yourself to the CC list of bug reports that you change, you
will receive followup emails with all comments and changes by anybody on
that individual report. This helps learning what further investigations
others make. You can change the settings in Bugzilla on which actions
you want to receive mail.

Bugs For Group Triage
---------------------

If you come across a bug/ bugs or If you think any bug should to go
thorough the bug triage group, please set NEEDINFO for bugs@gluster.org
on the bug.

Resolving bug reports
---------------------

See the [Bug report life cycle](./Bug report Life Cycle.md) for
the meaning of the bug status and resolutions.

Example of Triaged Bugs
-----------------------

This Bugzilla
[filter](https://bugzilla.redhat.com/buglist.cgi?bug_status=NEW&keywords=Triaged&keywords_type=anywords&list_id=2739593&product=GlusterFS&query_format=advanced)
will list NEW, Triaged Bugs
