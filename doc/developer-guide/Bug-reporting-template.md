Template for bug description
----------------------------
This template should be in-line to the [Bug reporting guidelines](./Bug Reporting Guidelines.md).
The template is replacement for the default description template present in [Bugzilla](https://bugzilla.redhat.com)

    work in progress

------------------------------------------------------------------------

Description of problem:

Version of GlusterFS package installed:

Location from which the packages are used:

GlusterFS Cluster Information:

-   Number of volumes
-   Volume Names
-   Volume on which the particular issue is seen [ if applicable ]
-   Type of volumes
-   Volume options if available
-   Output of `gluster volume info`
-   Output of `gluster volume status`
-   Get the statedump of the volume with the problem

`   $ gluster volume statedump `<vol-name>

-   Client Information
    -   OS Type:
    -   Mount type:
    -   OS Version:

How reproducible:

Steps to Reproduce:

-   1.
-   2.
-   3.

Actual results:

Expected results:

Logs Information:

-   Provide possible issues, warnings, errors as a comment to the bug
    -   Look for issues/warnings/errors in self-heal logs, rebalance logs, glusterd logs, brick logs, mount logs/nfs logs/smb logs
    -   Add the entire logs as attachment, if it is very large to paste as a comment

Additional info:

  [Bug\_reporting\_guidelines]: Bug_reporting_guidelines "wikilink"
  [Bugzilla]: https://bugzilla.redhat.com
