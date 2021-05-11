#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../cluster.rc

cleanup;

# Start glusterd on 2 nodes
TEST launch_cluster 2

# Create a volume
TEST $CLI_1 volume create ${V0}_1 $H1:$B1/${V0}_1_{1,2,3};
TEST $CLI_1 volume start ${V0}_1

# Negative scenarios for ganesha.enable option
TEST ! $CLI_1 volume set ${V0}_1 ganesha.enable invalid-value
TEST ! $CLI_1 volume set ${V0}_1 ganesha.enable enable

# Coverage for bitrot feature
TEST ! $CLI_1 volume bitrot ${V0}_1 disable
TEST $CLI_1 volume stop ${V0}_1 
TEST ! $CLI_1 volume bitrot ${V0}_1 enable

TEST $CLI_1 volume start ${V0}_1
TEST $CLI_1 volume bitrot ${V0}_1 enable
TEST $CLI_1 volume bitrot ${V0}_1 disable

# Neagtive scenario for gfproxy, fsm log
TEST ! $CLI_1 volume set ${V0}_1 config.gfproxyd invalid-value
TEST ! $CLI_1 system:: fsm log localhost

# Create 2 volumes for vol list code coverage
TEST $CLI_1 volume create ${V0}_2 $H1:$B1/${V0}_2_{1,2,3};
TEST $CLI_1 volume list

# Check get option for individual volume
TEST ! $CLI_1 volume get ${V0}_3 all
TEST $CLI_1 volume get ${V0}_1 cluster.max-op-version
TEST $CLI_1 volume get ${V0}_1 cluster.op-version
TEST $CLI_1 volume get ${V0}_1 config.memory-accounting
TEST $CLI_1 volume get ${V0}_1 config.transport

# Create a 2 node cluster for peer code coverage
TEST $CLI_1 peer probe $H2

TEST $CLI_1 get-state

pid=$(ps aux | grep 'glusterd.pid' | grep -v 'grep' | awk '{print $2}')
TEST generate_statedump $pid

# Replace-brick negative scenarios
TEST ! $CLI_1 volume replace-brick ${V0}_2 $H1:$B1/${V0}_2_{1} ${V0}_2 $H1:$B1/${V0}_2_{4} commit force
TEST $CLI_1 volume start ${V0}_2
TEST ! $CLI_1 volume replace-brick ${V0}_2 $H1:$B1/${V0}_2_{1} ${V0}_2 $H1:$B1/${V0}_2_{4} commit force
TEST ! $CLI_1 volume replace-brick ${V0}_2 $H1:$B1/${V0}_2_{1} ${V0}_2 $H2:$B1/${V0}_2_{4} commit force
TEST ! $CLI_1 volume replace-brick ${V0}_2 $H1:$B1/${V0}_2_{1} ${V0}_2 $H1:$B1/${V0}_2_{1} commit force

cleanup;
