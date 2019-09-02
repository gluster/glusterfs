## Git Commit Good Practice

The following document is based on experience doing code development, bug troubleshooting and code review across a number of projects using Git. The document is mostly borrowed from [Open Stack](https://wiki.openstack.org/wiki/GitCommitMessages), but made more meaningful in the context of GlusterFS project.

This topic can be split into two areas of concern

* The structured set/split of the code changes
* The information provided in the commit message

### Executive Summary
The points and examples that will be raised in this document ought to clearly demonstrate the value in splitting up changes into a sequence of individual commits, and the importance in writing good commit messages to go along with them. If these guidelines were widely applied it would result in a significant improvement in the quality of the GlusterFS Git history. Both a carrot & stick will be required to effect changes. This document intends to be the carrot by alerting people to the benefits, while anyone doing Gerrit code review can act as the stick ;-P

In other words, when reviewing a change in Gerrit:
* Do not simply look at the correctness of the code.
* Review the commit message itself and request improvements to its content.
* Look out for commits which are mixing multiple logical changes and require the submitter to split them into separate commits.
* Ensure whitespace changes are not mixed in with functional changes.
* Ensure no-op code refactoring is done separately from functional changes.

And so on.

It might be mentioned that Gerrit's handling of patch series is not entirely perfect. Let that not become a valid reason to avoid creating patch series. The tools being used should be subservient to developers needs, and since they are open source they can be fixed / improved. Software source code is "read mostly, write occassionally" and thus the most important criteria is to improve the long term maintainability by the large pool of developers in the community, and not to sacrifice too much for the sake of the single author who may never touch the code again.

And now the long detailed guidelines & examples of good & bad practice

### Structural split of changes
The cardinal rule for creating good commits is to ensure there is only one "logical change" per commit. There are many reasons why this is an important rule:

* The smaller the amount of code being changed, the quicker & easier it is to review & identify potential flaws.
* If a change is found to be flawed later, it may be necessary to revert the broken commit. This is much easier to do if there are not other unrelated code changes entangled with the original commit.
* When troubleshooting problems using Git's bisect capability, small well defined changes will aid in isolating exactly where the code problem was introduced.
* When browsing history using Git annotate/blame, small well defined changes also aid in isolating exactly where & why a piece of code came from.

#### Things to avoid when creating commits
With the above points in mind, there are some commonly encountered examples of bad things to avoid

* Mixing whitespace changes with functional code changes.

The whitespace changes will obscure the important functional changes, making it harder for a reviewer to correctly determine whether the change is correct. Solution: Create 2 commits, one with the whitespace changes, one with the functional changes. Typically the whitespace change would be done first, but that need not be a hard rule.

* Mixing two unrelated functional changes.

Again the reviewer will find it harder to identify flaws if two unrelated changes are mixed together. If it becomes necessary to later revert a broken commit, the two unrelated changes will need to be untangled, with further risk of bug creation.

* Sending large new features in a single giant commit.

It may well be the case that the code for a new feature is only useful when all of it is present. This does not, however, imply that the entire feature should be provided in a single commit. New features often entail refactoring existing code. It is highly desirable that any refactoring is done in commits which are separate from those implementing the new feature. This helps reviewers and test suites validate that the refactoring has no unintentional functional changes.

Even the newly written code can often be split up into multiple pieces that can be independently reviewed. For example, changes which add new internal fops or library functions, can be in self-contained commits. Again this leads to easier code review. It also allows other developers to cherry-pick small parts of the work, if the entire new feature is not immediately ready for merge. This will encourage the author & reviewers to think about the generic library functions' design, and not simply pick a design that is easier for their currently chosen internal implementation.

The basic rule to follow is

If a code change can be split into a sequence of patches/commits, then it should be split. Less is not more. More is more.

##### Examples of bad practice

TODO: Pick glusterfs specific example.


##### Examples of good practice


### Information in commit messages
As important as the content of the change, is the content of the commit message describing it. When writing a commit message there are some important things to remember

* Do not assume the reviewer understands what the original problem was.

When reading bug reports, after a number of back & forth comments, it is often as clear as mud, what the root cause problem is. The commit message should have a clear statement as to what the original problem is. The bug is merely interesting historical background on /how/ the problem was identified. It should be possible to review a proposed patch for correctness without needing to read the bug ticket.

* Do not assume the reviewer has access to external web services/site.

In 6 months time when someone is on a train/plane/coach/beach/pub troubleshooting a problem & browsing Git history, there is no guarantee they will have access to the online bug tracker, or online blueprint documents. The great step forward with distributed SCM is that you no longer need to be "online" to have access to all information about the code repository. The commit message should be totally self-contained, to maintain that benefit.

* Do not assume the code is self-evident/self-documenting.

What is self-evident to one person, might be clear as mud to another person. Always document what the original problem was and how it is being fixed, for any change except the most obvious typos, or whitespace only commits.

* Describe why a change is being made.

A common mistake is to just document how the code has been written, without describing /why/ the developer chose to do it that way. By all means describe the overall code structure, particularly for large changes, but more importantly describe the intent/motivation behind the changes.

* Read the commit message to see if it hints at improved code structure.

Often when describing a large commit message, it becomes obvious that a commit should have in fact been split into 2 or more parts. Don't be afraid to go back and rebase the change to split it up into separate commits.

* Ensure sufficient information to decide whether to review.

When Gerrit sends out email alerts for new patch submissions there is minimal information included, principally the commit message and the list of files changes. Given the high volume of patches, it is not reasonable to expect all reviewers to examine the patches in detail. The commit message must thus contain sufficient information to alert the potential reviewers to the fact that this is a patch they need to look at.

* The first commit line is the most important.

In Git commits the first line of the commit message has special significance. It is used as email subject line, git annotate messages, gitk viewer annotations, merge commit messages and many more places where space is at a premium. As well as summarizing the change itself, it should take care to detail what part of the code is affected. eg if it is 'afr', 'dht' or any translator. Or in some cases, it can be touching all these components, but the commit message can be 'coverity:', 'txn-framework:', 'new-fop: ', etc.

* Describe any limitations of the current code.

If the code being changed still has future scope for improvements, or any known limitations then mention these in the commit message. This demonstrates to the reviewer that the broader picture has been considered and what tradeoffs have been done in terms of short term goals vs. long term wishes.

* Do not include patch set-specific comments.

In other words, if you rebase your change please don't add "Patch set 2: rebased" to your commit message. That isn't going to be relevant once your change has merged. Please do make a note of that in Gerrit as a comment on your change, however. It helps reviewers know what changed between patch sets. This also applies to comments such as "Added unit tests", "Fixed localization problems", or any other such patch set to patch set changes that don't affect the overall intent of your commit.

**The main rule to follow is:**

The commit message must contain all the information required to fully understand & review the patch for correctness. Less is not more. More is more.


#### Including external references

The commit message is primarily targeted towards human interpretation, but there is always some metadata provided for machine use. In the case of GlusterFS this includes at least the 'Change-id', "bug"/"feature" ID references and "Signed-off-by" tag (generated by 'git commit -s').

The 'Change-id' line is a unique hash describing the change, which is generated by a Git commit hook. This should not be changed when rebasing a commit following review feedback, since it is used by Gerrit, to track versions of a patch.

The 'bug' line can reference a bug in a few ways. Gerrit creates a link to the bug when viewing the patch on review.gluster.org so that reviewers can quickly access the bug/issue on Bugzilla or Github.

**Fixes: bz#1601166** -- use 'Fixes: bz#NNNNN' if the commit is intended to fully fix and close the bug being referenced.
**Fixes: #411** -- use 'Fixes: #NNN' if the patch fixes the github issue completely.

**Updates: bz#1193929** -- use 'Updates: bz#NNNN' if the commit is only a partial fix and more work is needed.
**Updates: #175** -- use 'Updates: #NNNN' if the commit is only a partial fix and more work is needed for the feature completion.

We encourage the use of `Co-Authored-By: name <name@example.com>` in commit messages to indicate people who worked on a particular patch. It's a convention for recognizing multiple authors, and our projects would encourage the stats tools to observe it when collecting statistics.

### Summary of Git commit message structure

* Provide a brief description of the change in the first line.
* The first line should be limited to 50 characters and should not end with a period.

* Insert a single blank line after the first line.

* Provide a detailed description of the change in the following lines, breaking paragraphs where needed.

* Subsequent lines should be wrapped at 72 characters.

Put the 'Change-id', 'Fixes bz#NNNNN' and 'Signed-off-by: <>' lines at the very end.

TODO: Add good examples
