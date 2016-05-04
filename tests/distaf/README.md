# DiSTAF

DiSTAF (or distaf) is a test framework for distributed systems like glusterfs.

This file contains information about how to contribute automated test cases to gluster. For information about overview and architecture, please refer to [README](https://github.com/gluster/distaf/blob/master/README.md). To know more about how to write tests and more about the distaf APIs please refer [HOWTO](https://github.com/gluster/distaf/blob/master/docs/HOWTO.md).

## Before contributing tests to gluster

This doc assumes that you are familiar with distaf and know how to write a test case in distaf. These tests are meant for running perodically in a CI environment. So avoid assuming any test environment and make sure you are aware of the environment where the tests are being run. For example do not assume that some non-standard packages will be present in the test environment.

### Installing distaf and distaflibs packages
DiSTAF is devided into packages. The core part of distaf (the framework which provides connection manager and core APIs) are part of [distaf.git](https://github.com/gluster/distaf) and are available through distaf package. The gluster libraries are part of [glusterfs.git](https://github.com/gluster/glusterfs/tree/master/tests/distaf) and are available through distaflibs namespace packages. Please note that, to install these packages you need to have [python-setuptools](https://pypi.python.org/pypi/setuptools) (Most likely will be available through yum/apt-get) and should be run with root privilages.

The distaf core package can be installed through below command. It will install the distaf core package from the git HEAD available from distaf.git.
```bash
pip install git+https://github.com/gluster/distaf@master
```

The libraries related to gluster, which enables to write gluster tests are available through the namespace package distaflibs. And as part of namespace package distaflibs, We have 2 sub-packages (and provision to add more sub-packages in future if need arises) `distaflibs-gluster` and `distaflibs-io`. These libraries and the tests are part of [glusterfs.git](https://github.com/gluster/glusterfs). If you have cloned the glusterfs.git, please follow the below steps to install both sub-packages of distaflibs.
```bash
cd <glusterfs.git>/tests/distaf/distaf_libs/distaflibs-gluster
python setup.py install # Installs distaflibs.gluster package
cd <glusterfs.git>/tests/distaf/distaf_libs/io_libs
python setup.py install # Installs distaflibs.iolibs package
```

## Writing the gluster tests

DiSTAF supports both python function and python class as the testcase. But for gluster, each test case should be a python class and should be a sub class of `GlusterBaseClass`. This base class takes care of few of the basic things like creating volume based on the `global_mode` flag in configuration file etc. So the test case should implement the `run()` method with the test case logic. Any extra setup part done in `run()` should be cleaned up in `teardown()` method, which also should be implemented by test, if necessary.

Also `GlusterBaseClass` exposes few of the variable which are accesible from the connection manager object `tc`. Please consider below example.
```python
from distaf.util import tc, testcase
from distaflibs.gluster.gluster_base_class import GlusterBaseClass
from distaflibs.gluster.mount_ops import mount_volume, umount_volume

@testcase("nfs-test-01")
class nfs_test_01(GlusterBaseClass):
    """
    runs_on_volumes: ALL
    run_on_protocol: [ nfs ]
    reuse_setup: True
    """
    def run(self):
        tc.logger.debug("The volume name is %s and is of the type %s", self.volname,
                         self.voltype)
        tc.logger.debug("This test mounts the volume at %s with %s mount protocol",
                         self.mountpoint, self.mount_proto)
        client = self.clients[0]
        ret, out, err = mount_volume(self.volname, self.mount_proto, self.mountpoint,
                                     mclient=client)
        if ret!= 0:
            tc.logger.error("The mount failed")
            return False
        return True

    def teardown(self):
        umount_volume(self.clients[0], self.mountpoint)
        return True
```

So the volume name and volume type are accessible with the variables `self.volname` and `self.voltype` respectively. Since the test calls the `mount_volume()` in `run()`, it is the responsibility of the test to call `umount_volume()` in `teardown()`.

NOTE: The `setup()` only creats the volume. All other test logic has to be built inside the `run()` method.

For any more questions/comments please send a mail to [gluster-devel](https://www.gluster.org/mailman/listinfo/gluster-devel)
