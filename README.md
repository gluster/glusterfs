<h1 align="center"><img src="https://www.gluster.org/wp-content/uploads/2016/03/gluster-ant.png" width="320" height="240" alt="Gluster is a free and open source software scalable network filesystem."></h1>

<h4 align="center">
    <a href="https://gluster.slack.com/"><img src=https://img.shields.io/badge/CHATON-SLACK-green></a> 
    <a href="https://www.youtube.com/playlist?list=PLUjCssFKEMhXBqPAGtOA1lYtzS1eWaJuN"><img src=https://img.shields.io/badge/DEV%20SESSIONS-YOUTUBE-red></a> 
    <a href="https://twitter.com/gluster"><img src=https://img.shields.io/badge/FOLLOW-ON%20TWITTER-blue></a><br/><br/>
    <a href="https://docs.gluster.org/en/latest/Quick-Start-Guide/Quickstart/"><img src=https://img.shields.io/badge/%20%20-Quick%20Start%20Guide-yellowgreen?style=for-the-badge></a> 
    <a href="https://docs.gluster.org/en/latest/release-notes/"><img src=https://img.shields.io/badge/%20%20-Release%20Notes-%2341EDF5?style=for-the-badge></a> 
    <a href="https://lists.gluster.org/mailman/listinfo"><img src=https://img.shields.io/badge/%20%20-Mailing%20List-%23F59E41?style=for-the-badge></a> 
    <a href="https://www.gluster.org/community/"><img src=https://img.shields.io/badge/%20-COMMUNITY-%237D41F5?style=for-the-badge></a><br/><br/>
    <a href="https://docs.gluster.org/en/latest/Contributors-Guide/Index/"><img src=https://readme-typing-svg.herokuapp.com?color=%23000000&size=32&center=true&width=600&lines=Contribute+to+Gluster></a>
</h4>


---

<p align="left">
    <a href="https://github.com/gluster/Gluster-Builds/actions"><img src="https://github.com/gluster/Gluster-Builds/actions/workflows/Nightly_Build_Fedora_Latest.yml/badge.svg" alt="Build Status"></a>
    <a href="https://github.com/gluster/Gluster-Builds/actions"> <img src="https://github.com/gluster/Gluster-Builds/actions/workflows/Nightly_Build_Centos7.yml/badge.svg" alt="Coverage Status"></a>
    <a href="https://github.com/gluster/Gluster-Builds/actions"><img src="https://github.com/gluster/Gluster-Builds/actions/workflows/Nightly_Build_Centos8.yml/badge.svg"></a>
    <a href="https://github.com/gluster/Gluster-Builds/actions"><img src="https://github.com/gluster/Gluster-Builds/actions/workflows/nightly-build-debian.yml/badge.svg"></a>
    <a href="https://github.com/gluster/Gluster-Builds/actions"><img src="https://github.com/gluster/Gluster-Builds/actions/workflows/nightly-build-ubuntu.yml/badge.svg"></a>
    <a href="https://ci.centos.org/view/Gluster/job/gluster_build-rpms/"><img src="https://ci.centos.org/buildStatus/icon?job=gluster_build-rpms"></a>
    <a href="https://scan.coverity.com/projects/gluster-glusterfs"><img src="https://scan.coverity.com/projects/987/badge.svg"></a>
 </p>


# Gluster
  Gluster is a software defined distributed storage that can scale to several
  petabytes. It provides interfaces for object, block and file storage.

## Development
  The development workflow is documented in [Contributors guide](CONTRIBUTING.md)

## Documentation
  The Gluster documentation can be found at [Gluster Docs](http://docs.gluster.org).

## Deployment
  Quick instructions to build and install can be found in [INSTALL](INSTALL) file.

## Testing

  GlusterFS source contains some functional tests under `tests/` directory. All
  these tests are run against every patch submitted for review. If you want your
  patch to be tested, please add a `.t` test file as part of your patch submission.
  You can also submit a patch to only add a `.t` file for the test case you are
  aware of.

  To run these tests, on your test-machine, just run `./run-tests.sh`. Don't run
  this on a machine where you have 'production' glusterfs is running, as it would
  blindly kill all gluster processes in each runs.

  If you are sending a patch, and want to validate one or few specific tests, then
  run a single test by running the below command.

```
  bash# /bin/bash ${path_to_gluster}/tests/basic/rpc-coverage.t
```

  You can also use `prove` tool if available in your machine, as follows.

```
  bash# prove -vmfe '/bin/bash' ${path_to_gluster}/tests/basic/rpc-coverage.t
```


## Maintainers
  The list of Gluster maintainers is available in [MAINTAINERS](MAINTAINERS) file.

## License
  Gluster is dual licensed under [GPLV2](COPYING-GPLV2) and [LGPLV3+](COPYING-LGPLV3).

  Please visit the [Gluster Home Page](http://www.gluster.org/) to find out more about Gluster.
