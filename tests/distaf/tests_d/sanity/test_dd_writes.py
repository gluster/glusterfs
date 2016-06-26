from distaf.util import tc, testcase, globl_configs
from distaflibs.gluster.gluster_base_class import GlusterBaseClass
from distaflibs.gluster.mount_ops import mount_volume, umount_volume
import os
import random
import yaml

@testcase("test_dd_writes")
class TestDdWrites(GlusterBaseClass):
    """
        runs_on_volumes: [ distribute, replicate, dist_rep ]
        runs_on_protocol: [ glusterfs, nfs ]
        reuse_setup: True
    """
    def __init__(self, globl_configs):
        GlusterBaseClass.__init__(self, globl_configs)
        self.filename = "dd_testfile"
        io_config_file = os.path.join(os.path.dirname
                                      (os.path.realpath(__file__)),
                                      "io_config.yml")
        dd_writes_config = yaml.load(open(io_config_file))
        # Num of dd's to start per client/per file
        self.num_of_dd_writes_process = (dd_writes_config['gluster']['tests']
                                         ['io']['dd_writes']
                                         ['num_of_process_per_file'])

        # Num of files to create
        self.num_of_files = (dd_writes_config['gluster']['tests']
                             ['io']['dd_writes']['num_of_files'])

        # Input file for dd command
        self.dd_input_file = (dd_writes_config['gluster']['tests']
                              ['io']['dd_writes']['input_file'])

        # Block size
        self.block_size = (dd_writes_config['gluster']['tests']
                           ['io']['dd_writes']['block_size'])
        if "random" in self.block_size:
            self.block_size = ["1k", "10k", "256k", "512k" ,"1M",
                              "10M", "100M", "256M", "512M", "1G"]

        # dd count
        self.dd_count = (dd_writes_config['gluster']['tests']
                         ['io']['dd_writes']['count'])

        # dd CONV
        if (dd_writes_config['gluster']['tests']['io']
            ['dd_writes']).has_key('conv'):
            self.dd_conv = (dd_writes_config['gluster']['tests']
                             ['io']['dd_writes']['conv'])
        else:
            self.dd_conv = None

        # dd OFLAG
        if (dd_writes_config['gluster']['tests']['io']
            ['dd_writes']).has_key('oflag'):
            self.dd_oflag = (dd_writes_config['gluster']['tests']
                             ['io']['dd_writes']['oflag'])
        else:
            self.dd_oflag = None


    def setup(self):
        ret = GlusterBaseClass.setup(self)
        if not ret:
            return False

        if self.mounts:
            for mount_obj in self.mounts:
                ret = mount_obj.mount()
                if not ret:
                    tc.logger.error("Mounting Volume %s failed on %s:%s" %
                                    (mount_obj.volname,
                                     mount_obj.client_system,
                                     mount_obj.mountpoint))
                    return False
        return True


    def run(self):
        rc = True
        dd_cmd = "dd if=%s count=%s " % (self.dd_input_file, self.dd_count)
        if self.dd_conv is not None:
            dd_cmd += "conv=%s " % self.dd_conv

        if self.dd_oflag is not None:
            dd_cmd += "oflag=%s " % self.dd_oflag

        all_mounts_cmd_runs = []
        for mount_obj in self.mounts:
            all_cmd_runs = []
            for i in range(1, (self.num_of_files + 1)):
                cmd_runs = []
                for j in range(1, (self.num_of_dd_writes_process + 1)):
                    cmd = dd_cmd + ("of=%s bs=%s" %
                                    (os.path.join(mount_obj.mountpoint,
                                                  self.filename + "_" + str(i)),
                                     random.choice(self.block_size)))
                    tc.logger.info("Executing Command: %s", cmd)
                    ret = tc.run_async(mount_obj.client_system, cmd)
                    cmd_runs.append(ret)
                all_cmd_runs.append(cmd_runs)
            all_mounts_cmd_runs.append(all_cmd_runs)

        for all_cmd_runs in all_mounts_cmd_runs:
            for cmd_runs in all_cmd_runs:
                for each_cmd_run in cmd_runs:
                    each_cmd_run.wait()

        rc = True
        for i, all_cmd_runs in enumerate(all_mounts_cmd_runs):
            for j, cmd_runs in enumerate(all_cmd_runs):
                for k, each_cmd_run in enumerate(cmd_runs):
                    ret, _, _ = each_cmd_run.value()
                    if ret != 0:
                        tc.logger.error("DD Writes failed on:  %s/%s/%s:%d" %
                                        (self.mounts[i].client_system,
                                         self.mounts[i].mountpoint,
                                         (self.filename + "_" + str(j)), k))
                        rc = False
        if not rc:
            tc.logger.error("DD Write failed on atleast one file")
            return False
        else:
            tc.logger.info("DD Write successfully passed on all the files")
            return True

    def teardown(self):
        for mount_obj in self.mounts:
            cleanup_mount_cmd = "rm -rf %s/*" % mount_obj.mountpoint
            ret, out, err = tc.run(mount_obj.client_system,
                                   cleanup_mount_cmd)
        for mount_obj in self.mounts:
            mount_obj.unmount()
        ret = GlusterBaseClass.teardown(self)
        if not ret:
            return False
        return True

