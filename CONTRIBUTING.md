# GlusterFS project Contribution guidelines

## Development Workflow

We follow most of the details as per the [document here](https://help.github.com/en/github/collaborating-with-issues-and-pull-requests). If you are not aware of the github workflow, it is recommended to go through them before continuing here.


#### Get the Repository setup

0. Fork Repository
   - Fork [GlusterFS repository](https://github.com/gluster/glusterfs/fork).

1. Clone Repository
   - Clone the glusterfs repo freshly from github using below steps.

```
   git clone git@github.com:${username}/glusterfs.git
   cd glusterfs/
   git remote add upstream git@github.com:gluster/glusterfs.git
```

About two tasks are one time for the life time. You can continue to use the same repository for all the work in future.

#### Development & Other flows

0. Issue:
   - Make sure there is an issue filed for the task you are working on.
   - If it is not filed, open the issue with all the description.
   - If it is a bug fix, add label "Type:Bug".
   - If it is an RFC, provide all the documentation, and request for "DocApproved", and "SpecApproved" label.

1. Code:
   - Start coding
   - Build and test locally
   - Make sure clang-format is installed and is run on the patch.

2. Keep up-to-date
   - GlusterFS is a large project with many developers, so there would be one or the other patch everyday.
   - It is critical for developer to be up-to-date with `devel` repo to be Conflict-Free when PR is opened.
   - Git provides many options to keep up-to-date, below is one of them
```
   git fetch upstream
   git rebase upstream/devel
```
   - It is recommended you keep pushing to your repo every day, so you don't loose any work.
     - It can be done by `./rfc.sh` (or `git push origin HEAD:issueNNN`)

2. Commit Message / PR description:
   - The name of the branch on your personal fork can start with issueNNNN, followed by anything of your choice.
   - PRs continue to have the title of format "component: \<title\>", like it is practiced now.
   - When you open a PR, having a reference Issue for the commit is mandatory in GlusterFS.
   - Commit message can have, either `Fixes: #NNNN` or `Updates: #NNNN` in a separate line in the commit message.
     - Here, NNNN is the Issue ID in glusterfs repository.
   - Each commit needs the author to have the "Signed-off-by: Name \<email\>" line.
     - Can do this by `-s` option for `git commit`.
   - If the PR is not ready for review, apply the label `work-in-progress`.
     - Check the availability of "Draft PR" is present for you, if yes, use that instead.

3. Tests:
   - All the required smoke tests would be auto-triggered.
     - Developers get a chance to retrigger the smoke tests using **"/recheck smoke"** as comment.
   - The "regression" tests would be triggered by a comment **"/run regression"** from developers in the [@gluster-maintainers](https://github.com/orgs/gluster/teams/gluster-maintainers) group.
     - Ask for help as comment in PR if you have any questions about the process!

4. Review Process:
   - `+2` : is equivalent to "Approve" from the people in the maintainer's group.
   - `+1` : can be given by a maintainer/reviewer by explicitly stating that in the comment.
   - `-1` : provide details on required changes and pick "Request Changes" while submitting your review.
   - `-2` : done by adding the `DO-NOT-MERGE` label.

   - Any further discussions can happen as comments in the PR.

5. Making changes:
   - There are 2 approaches to submit changes done after addressing review comments.
     - Commit changes as a new commit on top of the original commits in the branch, and push the changes to same branch (issueNNNN)
     - Commit changes into the same commit with `--amend` option, and do a push to the same branch with `--force` option.

6. Merging:
   - GlusterFS project follows 'Squash and Merge' method
     - This is mainly to preserve the historic Gerrit method of one patch in `git log` for one URL link.
     - This also makes every merge a complete patch, which has passed all tests.
   - The merging of the patch is expected to be done by the maintainers.
     - It can be done when all the tests (smoke and regression) pass.
     - When the PR has 'Approved' flag from corresponding maintainer.
   - If you feel there is delay, feel free to add a comment, discuss the same in Slack channel, or send email.

## By contributing to this project, the contributor would need to agree to below.

### Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I
    have the right to submit it under the open source license
    indicated in the file; or

(b) The contribution is based upon previous work that, to the best
    of my knowledge, is covered under an appropriate open source
    license and I have the right under that license to submit that
    work with modifications, whether created in whole or in part
    by me, under the same open source license (unless I am
    permitted to submit under a different license), as indicated
    in the file; or

(c) The contribution was provided directly to me by some other
    person who certified (a), (b) or (c) and I have not modified
    it.

(d) I understand and agree that this project and the contribution
    are public and that a record of the contribution (including all
    personal information I submit with it, including my sign-off) is
    maintained indefinitely and may be redistributed consistent with
    this project or the open source license(s) involved.

