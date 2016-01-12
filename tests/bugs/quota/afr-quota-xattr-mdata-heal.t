#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;
TEST glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
TEST $CLI volume quota $V0 enable
TEST $CLI volume quota $V0 limit-usage / 1MB
TEST mkdir $M0/d
TEST $CLI volume quota $V0 limit-usage /d 1MB
TEST touch $M0/d/a
echo abc > $M0/d/a

EXPECT_WITHIN $MARKER_UPDATE_TIMEOUT "512Bytes" quotausage "/"

#Set the acl xattrs directly on backend, for some reason on mount it gives error
acl_access_val="0x0200000001000600ffffffff04000400ffffffff10000400ffffffff20000400ffffffff"
acl_file_val="0x0000000400000001ffffffff0006000000000004ffffffff0004000000000010ffffffff0004000000000020ffffffff00040000"
TEST setfattr -n system.posix_acl_access -v $acl_access_val $B0/${V0}0/d
TEST setfattr -n trusted.SGI_ACL_FILE -v $acl_file_val $B0/${V0}0/d
TEST setfattr -n system.posix_acl_access -v $acl_access_val $B0/${V0}1/d
TEST setfattr -n trusted.SGI_ACL_FILE -v $acl_file_val $B0/${V0}1/d
TEST setfattr -n trusted.foo -v "baz" $M0/d
TEST setfattr -n trusted.foo -v "baz" $M0/d/a
TEST setfattr -n trusted.foo1 -v "baz1" $M0/d
TEST setfattr -n trusted.foo1 -v "baz1" $M0/d/a
TEST setfattr -n trusted.foo3 -v "unchanged" $M0/d
TEST setfattr -n trusted.foo3 -v "unchanged" $M0/d/a

TEST kill_brick $V0 $H0 $B0/${V0}0
#Induce metadata self-heal
TEST setfattr -n trusted.foo -v "bar" $M0/d
TEST setfattr -n trusted.foo -v "bar" $M0/d/a
TEST setfattr -x trusted.foo1 $M0/d
TEST setfattr -x trusted.foo1 $M0/d/a
TEST setfattr -n trusted.foo2 -v "bar2" $M0/d
TEST setfattr -n trusted.foo2 -v "bar2" $M0/d/a
d_quota_contri=$(getfattr -d -m . -e hex $B0/${V0}1/d | grep -E "trusted.glusterfs.quota.*.contri")
d_quota_dirty=$(getfattr -d -m . -e hex  $B0/${V0}1/d | grep -E "trusted.glusterfs.quota.dirty")
d_quota_limit=$(getfattr -d -m . -e hex  $B0/${V0}1/d | grep -E "trusted.glusterfs.quota.limit-set")
d_quota_size=$(getfattr -d -m . -e hex   $B0/${V0}1/d | grep -E "trusted.glusterfs.quota.size")

a_pgfid=$(getfattr -d -m . -e hex   $B0/${V0}1/d/a | grep -E "trusted.pgfid.")

#Change internal xattrs in the backend, later check that they are not healed
TEST setfattr -n trusted.glusterfs.quota.00000000-0000-0000-0000-000000000001.contri -v 0x0000000000000400 $B0/${V0}0/d
TEST setfattr -n trusted.glusterfs.quota.dirty -v 0x0000000000000400 $B0/${V0}0/d
TEST setfattr -n trusted.glusterfs.quota.limit-set -v 0x0000000000000400 $B0/${V0}0/d #This will be healed, this is external xattr
TEST setfattr -n trusted.glusterfs.quota.size -v 0x0000000000000400 $B0/${V0}0/d
TEST setfattr -n $(echo $a_pgfid | cut -f1 -d'=') -v "orphan" $B0/${V0}0/d/a

TEST $CLI volume set $V0 cluster.self-heal-daemon on
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

#Check external xattrs match
EXPECT "bar" echo $(getfattr -d -m. -e text $B0/${V0}0/d | grep trusted.foo)
EXPECT "bar" echo $(getfattr -d -m. -e text $B0/${V0}0/d/a | grep trusted.foo)
TEST ! getfattr -n trusted.foo1 $B0/${V0}0/d
TEST ! getfattr -n trusted.foo1 $B0/${V0}0/d/a
EXPECT "unchanged" echo $(getfattr -d -m. -e text $B0/${V0}0/d | grep trusted.foo3)
EXPECT "unchanged" echo $(getfattr -d -m. -e text $B0/${V0}0/d/a | grep trusted.foo3)
EXPECT "bar2" echo $(getfattr -d -m. -e text $B0/${V0}0/d | grep trusted.foo2)
EXPECT "bar2" echo $(getfattr -d -m. -e text $B0/${V0}0/d/a | grep trusted.foo2)
EXPECT "$d_quota_limit" echo $(getfattr -d -m . -e hex  $B0/${V0}0/d | grep "trusted.glusterfs.quota.limit-set")

EXPECT "bar" echo $(getfattr -d -m. -e text $B0/${V0}1/d | grep trusted.foo)
EXPECT "bar" echo $(getfattr -d -m. -e text $B0/${V0}1/d/a | grep trusted.foo)
TEST ! getfattr -n trusted.foo1 $B0/${V0}1/d
TEST ! getfattr -n trusted.foo1 $B0/${V0}1/d/a
EXPECT "unchanged" echo $(getfattr -d -m. -e text $B0/${V0}1/d | grep trusted.foo3)
EXPECT "unchanged" echo $(getfattr -d -m. -e text $B0/${V0}1/d/a | grep trusted.foo3)
EXPECT "bar2" echo $(getfattr -d -m. -e text $B0/${V0}1/d | grep trusted.foo2)
EXPECT "bar2" echo $(getfattr -d -m. -e text $B0/${V0}1/d/a | grep trusted.foo2)
EXPECT "$d_quota_limit" echo $(getfattr -d -m . -e hex  $B0/${V0}1/d | grep "trusted.glusterfs.quota.limit-set")

#Test that internal xattrs on B0 are not healed
EXPECT 0x0000000000000400 echo $(getfattr -d -m. -e hex $B0/${V0}0/d | grep trusted.glusterfs.quota.00000000-0000-0000-0000-000000000001.contri)
EXPECT 0x0000000000000400 echo $(getfattr -d -m. -e hex $B0/${V0}0/d | grep trusted.glusterfs.quota.dirty)
EXPECT "$d_quota_limit" echo $(getfattr -d -m. -e hex $B0/${V0}0/d | grep trusted.glusterfs.quota.limit-set) #This will be healed, this is external xattr
EXPECT 0x0000000000000400 echo $(getfattr -d -m. -e hex $B0/${V0}0/d | grep trusted.glusterfs.quota.size)
EXPECT "$acl_access_val" echo $(getfattr -d -m. -e hex $B0/${V0}0/d | grep system.posix_acl_access)
EXPECT "$acl_file_val" echo $(getfattr -d -m. -e hex $B0/${V0}0/d | grep trusted.SGI_ACL_FILE)
EXPECT "orphan" echo $(getfattr -d -m. -e text $B0/${V0}0/d/a | grep $(echo $a_pgfid | cut -f1 -d'='))

#Test that xattrs didn't go bad in source
EXPECT "$d_quota_contri" echo $(getfattr -d -m . -e hex $B0/${V0}1/d | grep -E "trusted.glusterfs.quota.*.contri")
EXPECT "$d_quota_dirty"  echo $(getfattr -d -m . -e hex  $B0/${V0}1/d | grep -E "trusted.glusterfs.quota.dirty")
EXPECT "$d_quota_limit"  echo $(getfattr -d -m . -e hex  $B0/${V0}1/d | grep -E "trusted.glusterfs.quota.limit-set")
EXPECT "$d_quota_size"   echo $(getfattr -d -m . -e hex   $B0/${V0}1/d | grep -E "trusted.glusterfs.quota.size")
EXPECT "$a_pgfid" echo $(getfattr -d -m . -e hex   $B0/${V0}1/d/a | grep -E "trusted.pgfid.")
EXPECT "$acl_access_val" echo $(getfattr -d -m. -e hex $B0/${V0}1/d | grep system.posix_acl_access)
EXPECT "$acl_file_val" echo $(getfattr -d -m. -e hex $B0/${V0}1/d | grep trusted.SGI_ACL_FILE)

#Do a lookup and it shouldn't trigger metadata self-heal and heal xattrs
EXPECT "bar" echo $(getfattr -d -m. -e text $B0/${V0}0/d | grep trusted.foo)
EXPECT "bar" echo $(getfattr -d -m. -e text $B0/${V0}0/d/a | grep trusted.foo)
TEST ! getfattr -n trusted.foo1 $B0/${V0}0/d
TEST ! getfattr -n trusted.foo1 $B0/${V0}0/d/a
EXPECT "unchanged" echo $(getfattr -d -m. -e text $B0/${V0}0/d | grep trusted.foo3)
EXPECT "unchanged" echo $(getfattr -d -m. -e text $B0/${V0}0/d/a | grep trusted.foo3)
EXPECT "bar2" echo $(getfattr -d -m. -e text $B0/${V0}0/d | grep trusted.foo2)
EXPECT "bar2" echo $(getfattr -d -m. -e text $B0/${V0}0/d/a | grep trusted.foo2)
EXPECT "$d_quota_limit" echo $(getfattr -d -m . -e hex  $B0/${V0}0/d | grep "trusted.glusterfs.quota.limit-set")

EXPECT "bar" echo $(getfattr -d -m. -e text $B0/${V0}1/d | grep trusted.foo)
EXPECT "bar" echo $(getfattr -d -m. -e text $B0/${V0}1/d/a | grep trusted.foo)
TEST ! getfattr -n trusted.foo1 $B0/${V0}1/d
TEST ! getfattr -n trusted.foo1 $B0/${V0}1/d/a
EXPECT "unchanged" echo $(getfattr -d -m. -e text $B0/${V0}1/d | grep trusted.foo3)
EXPECT "unchanged" echo $(getfattr -d -m. -e text $B0/${V0}1/d/a | grep trusted.foo3)
EXPECT "bar2" echo $(getfattr -d -m. -e text $B0/${V0}1/d | grep trusted.foo2)
EXPECT "bar2" echo $(getfattr -d -m. -e text $B0/${V0}1/d/a | grep trusted.foo2)
EXPECT "$d_quota_limit" echo $(getfattr -d -m . -e hex  $B0/${V0}1/d | grep "trusted.glusterfs.quota.limit-set")

#Test that internal xattrs on B0 are not healed
EXPECT 0x0000000000000400 echo $(getfattr -d -m. -e hex $B0/${V0}0/d | grep trusted.glusterfs.quota.00000000-0000-0000-0000-000000000001.contri)
EXPECT 0x0000000000000400 echo $(getfattr -d -m. -e hex $B0/${V0}0/d | grep trusted.glusterfs.quota.dirty)
EXPECT "$d_quota_limit" echo $(getfattr -d -m. -e hex $B0/${V0}0/d | grep trusted.glusterfs.quota.limit-set) #This will be healed, this is external xattr
EXPECT 0x0000000000000400 echo $(getfattr -d -m. -e hex $B0/${V0}0/d | grep trusted.glusterfs.quota.size)
EXPECT "orphan" echo $(getfattr -d -m. -e text $B0/${V0}0/d/a | grep $(echo $a_pgfid | cut -f1 -d'='))

#Test that xattrs didn't go bad in source
EXPECT "$d_quota_contri" echo $(getfattr -d -m . -e hex $B0/${V0}1/d | grep -E "trusted.glusterfs.quota.*.contri")
EXPECT "$d_quota_dirty"  echo $(getfattr -d -m . -e hex  $B0/${V0}1/d | grep -E "trusted.glusterfs.quota.dirty")
EXPECT "$d_quota_limit"  echo $(getfattr -d -m . -e hex  $B0/${V0}1/d | grep -E "trusted.glusterfs.quota.limit-set")
EXPECT "$d_quota_size"   echo $(getfattr -d -m . -e hex   $B0/${V0}1/d | grep -E "trusted.glusterfs.quota.size")
EXPECT "$a_pgfid" echo $(getfattr -d -m . -e hex   $B0/${V0}1/d/a | grep -E "trusted.pgfid.")

EXPECT "$acl_access_val" echo $(getfattr -d -m. -e hex $B0/${V0}0/d | grep system.posix_acl_access)
EXPECT "$acl_file_val" echo $(getfattr -d -m. -e hex $B0/${V0}0/d | grep trusted.SGI_ACL_FILE)
EXPECT "$acl_access_val" echo $(getfattr -d -m. -e hex $B0/${V0}1/d | grep system.posix_acl_access)
EXPECT "$acl_file_val" echo $(getfattr -d -m. -e hex $B0/${V0}1/d | grep trusted.SGI_ACL_FILE)
cleanup
