# A guide for contributors

While you have gone through 'how to contribute' guides, if you are
not sure what to work on, but really want to help the project, you
have now landed on the right document :-)

### Basic

Instead of planning to fix **all** the below issues in one patch,
we recommend you to have a a constant, continuous flow of improvements
for the project. We recommend you to pick 1 file (or just few files) at
a time to address below issues.
Pick any `.c` (or `.h`) file, and you can send a patch which fixes **any**
of the below themes. Ideally, fix all such occurrences in the file, even
though, the reviewers would review even a single line change patch
from you.

1. Check for variable definitions, and if there is an array definition,
which is very large at the top of the function, see if you can re-scope
the variable to relevant sections (if it helps).

Most of the time, some of these arrays may be used for 'error' handling,
and it is possible to use them only in that scope.

Reference: https://review.gluster.org/20846/


2. Check for complete string initialization at the beginning of a function.
Ideally, there is no reason to initialize a string. Fix it across the file.

Example:

`char new_path_name[PATH_MAX] = {0};` to `char new_path_name[PATH_MAX];`


3. Change `calloc()` to `malloc()` wherever it makes sense.

In a case of allocating a structures, where you expect certain (or most of)
variables to be 0 (or NULL), it makes sense to use calloc(). But otherwise,
there is an extra cost to `memset()` the whole object after allocating it.
While it is not a significant improvement in performance, code which gets
hit 1000s of times in a second, it would add some value.

Reference: https://review.gluster.org/20878/


4. You can consider using `snprintf()`, instead of `strncpy()` while dealing
with strings.

strncpy() won't null terminate if the dest buffer isn't big enough; snprintf()
does. While most of the string operations in the code is on array, and larger
size than required, strncpy() does an extra copy of 0s at the end of
string till the size of the array. It makes sense to use `snprintf()`,
which doesn't suffer from that behavior.

Also check the return value from snprintf() for buffer overflow and handle
accordingly

Reference: https://review.gluster.org/20925/


5. Now, pick a `.h` file, and see if a structure is very large, and see
if re-aligning them as per [coding-standard](./coding-standard.md) gives any size benefit,
if yes, go ahead and change it. Make sure you check all the structures
in the file for similar pattern.

Reference: [Check this section](https://github.com/gluster/glusterfs/blob/master/doc/developer-guide/coding-standard.md#structure-members-should-be-aligned-based-on-the-padding-requirements


### If you are up for more :-)

Good progress! Glad you are interested to know more. We are surely interested
in next level of contributions from you!

#### Coverity

Visit [Coverity Dashboard](https://scan.coverity.com/projects/gluster-glusterfs?tab=overview).

Now, if the number of defect is not 0, you have an opportunity to contribute.

You get all the detail on why the particular defect is mentioned there, and
most probable hint on how to fix it. Do it!

Reference: https://review.gluster.org/21394/

Use the same reference Id (789278) as the patch, so we can capture it is in
single bugzilla.

#### Clang-Scan

Clang-Scan is a tool which scans the .c files and reports the possible issues,
similar to coverity, but a different tool. Over the years we have seen, they
both report very different set of issues, and hence there is a value in fixing it.

GlusterFS project gets tested with clang-scan job every night, and the report is
posted in the [job details page](https://build.gluster.org/job/clang-scan/lastCompletedBuild/clangScanBuildBugs/).
As long as the number is not 0 in the report here, you have an opportunity to
contribute! Similar to coverity dashboard, click on 'Details' to find out the
reason behind that report, and send a patch.

Reference: https://review.gluster.org/21025

Again, you can use reference Id (1622665) for these patches!


### I am good with programming, I would like to do more than above!

#### Locked regions / Critical sections

In the file you open, see if the lock is taken only to increment or decrement
a flag, counter etc. If yes, then recommend you to convert it to ATOMIC locks.
It is simple activity, but, if you know programing, you would know the benefit
here.

NOTE: There may not always a possibility to do this! You may have to check
with developers first before going ahead.

Reference: https://review.gluster.org/21221/


#### ASan (address sanitizer)

[The job](https://build.gluster.org/job/asan/) runs regression with asan builds,
and you can also run glusterfs with asan on your workload to identify the leaks.
If there are any leaks reported, feel free to check it, and send us patch.

You can also run `valgrind` and let us know what it reports.

Reference: https://review.gluster.org/21397


#### Porting to different architecture

This is something which we are not focusing right now, happy to collaborate!

Reference: https://review.gluster.org/21276


#### Fix 'TODO/FIXME' in codebase

There are few cases of pending features, or pending validations, which are
pending from sometime. You can pick them in the given file, and choose to
fix it.


### I don't know C, but I am interested to contribute in some way!

You are most welcome! Our community is open for your contribution! First thing
which comes to our mind is **documentation**. Next is, **testing** or validation.

If you have some hardware, and want to run some performance comparisons with
different version, or options, and help us to tune better is also a great help.


#### Documentation

1. We have some documentation in [glusterfs repo](../), go through these, and
see if you can help us to keep up-to-date.

2. The https://docs.gluster.org is powered by https://github.com/gluster/glusterdocs
repo. You can check out the repo, and help in keeping that up-to-date.

3. [Our website](https://gluster.org) is maintained by https://github.com/gluster/glusterweb
repo. Help us to keep this up-to-date, and add content there.

4. Write blogs about Gluster, and your experience, and make world know little
more about Gluster, and your use-case, and how it helped to solve the problem.


#### Testing

1. There is a regression test suite in glusterfs, which runs with every patch, and is
triggered by just running `./run-tests.sh` from the root of the project repo.

You can add more test case to match your use-case, and send it as a patch, so you
can make sure all future patches in glusterfs would keep your usecase intact.

2. [Glusto-Tests](https://github.com/gluster/glusto-tests): This is another testing
framework written for gluster, and makes use of clustered setup to test different
use-cases, and helps to validate many bugs.


#### Ansible

Gluster Organization has rich set of ansible roles, which are actively maintained.
Feel free to check them out here - https://github.com/gluster/gluster-ansible


#### Monitoring

We have prometheus repo, and are actively working on adding more metrics. Add what
you need @ https://github.com/gluster/gluster-prometheus


#### Health-Report

This is a project, where at any given point in time, you want to run some set of
commands locally, and get an output to analyze the status, it can be added.
Contribute @ https://github.com/gluster/gluster-health-report


### All these C/bash/python is old-school, I want something in containers.

We have something for you too :-)

Please visit our https://github.com/gluster/gcs repo for checking how you can help,
and how gluster can help you in container world.


### Note

For any queries, best way is to contact us through mailing-list, <mailto:gluster-devel@gluster.org>
