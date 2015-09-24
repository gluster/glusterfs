Bugs often get fixed in master before release branches. When a bug is
fixed in the master branch, it might be desirable or necessary in a
stable branch. To put the fix in stable branch we need to backport the
fix to stable branch.

Anyone in the community can suggest a backport. If you are interested to
suggest a backport, please check the [Backport
Wishlist](./Backport Wishlist.md).

This page describes the steps needed to backport simple changes. Changes
that do not apply cleanly will need some manual modifications and using
`git cherry-pick` may not always be the easiest solution.

1.  Git clone the GlusterFS code

                git clone ssh://username@review.gluster.org/glusterfs

2.  Create and checkout a new branch for your work, based on the branch
    for the backport version

                git checkout -t -b bug-123456/release-3.5 origin/release-3.5

3.  Cherry pick the change from master.

                $ git cherry-pick -x a0b1c2d3e4f5
 -   verify that the change has been merged in the master branch.

4.  Update/correct the commit message.

                $ git commit -s --amend --date="$(date)"
[This is one example](https://github.com/gluster/glusterfs/commit/40407afb529f6e5fa2f79e9778c2f527122d75eb) of the commit message that has a good description for a backport. Notice the indention of the patch-metadata like BUG, Change-ID and Reviewed-on tags. There is also the original commit-id that was cherry picked from the master branch.
 -make sure to quote the review tags
 -update the BUG reference, point to the BUG that is used for this
particular release-branch
 -add a Signed-off-by tag

5.  Run `./rfc.sh` to post the backport for review.

                ./rfc.sh
After submitting patch(es), make sure to move the bug to the *POST*
status.
