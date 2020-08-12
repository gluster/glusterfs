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
