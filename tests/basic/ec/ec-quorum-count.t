 #!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../ec.rc

cleanup
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 6 redundancy 2 $H0:$B0/${V0}{0..5}
TEST $CLI volume create $V1 $H0:$B0/${V1}{0..5}
TEST $CLI volume set $V0 disperse.eager-lock-timeout 5
TEST $CLI volume set $V0 performance.flush-behind off
TEST $CLI volume set $V0 disperse.background-heals 0
TEST $CLI volume set $V0 disperse.heal-wait-qlength 0

#Should fail on non-disperse volume
TEST ! $CLI volume set $V1 disperse.quorum-count 5

#Should succeed on a valid range
TEST ! $CLI volume set $V0 disperse.quorum-count 0
TEST ! $CLI volume set $V0 disperse.quorum-count -0
TEST ! $CLI volume set $V0 disperse.quorum-count abc
TEST ! $CLI volume set $V0 disperse.quorum-count 10abc
TEST ! $CLI volume set $V0 disperse.quorum-count 1
TEST ! $CLI volume set $V0 disperse.quorum-count 2
TEST ! $CLI volume set $V0 disperse.quorum-count 3
TEST $CLI volume set $V0 disperse.quorum-count 4
TEST $CLI volume start $V0
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0

#Test that the option is reflected in the mount
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "^4$" ec_option_value $V0 $M0 0 quorum-count
TEST $CLI volume reset $V0 disperse.quorum-count
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "^0$" ec_option_value $V0 $M0 0 quorum-count
TEST $CLI volume set $V0 disperse.quorum-count 6
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "^6$" ec_option_value $V0 $M0 0 quorum-count

TEST touch $M0/a
TEST touch $M0/data
TEST setfattr -n trusted.def -v def $M0/a
TEST touch $M0/src
TEST touch $M0/del-me
TEST mkdir $M0/dir1
TEST dd if=/dev/zero of=$M0/read-file bs=1M count=1 oflag=direct
TEST dd if=/dev/zero of=$M0/del-file bs=1M count=1 oflag=direct
TEST gf_rm_file_and_gfid_link $B0/${V0}0 del-file
#modify operations should fail as the file is not in quorum
TEST ! dd if=/dev/zero of=$M0/del-file bs=1M count=1 oflag=direct
TEST kill_brick $V0 $H0 $B0/${V0}0
#Read should succeed even when quorum-count is not met
TEST dd if=$M0/read-file of=/dev/null iflag=direct
TEST ! touch $M0/a2
TEST ! mkdir $M0/dir2
TEST ! mknod  $M0/b2 b 4 5
TEST ! ln -s $M0/a $M0/symlink
TEST ! ln $M0/a $M0/link
TEST ! mv $M0/src $M0/dst
TEST ! rm -f $M0/del-me
TEST ! rmdir $M0/dir1
TEST ! dd if=/dev/zero of=$M0/a bs=1M count=1 conv=notrunc
TEST ! dd if=/dev/zero of=$M0/data bs=1M count=1 conv=notrunc
TEST ! truncate -s 0 $M0/a
TEST ! setfattr -n trusted.abc -v abc $M0/a
TEST ! setfattr -x trusted.def $M0/a
TEST ! chmod +x $M0/a
TEST ! fallocate -l 2m -n $M0/a
TEST ! fallocate -p -l 512k $M0/a
TEST $CLI volume start $V0 force
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count ${V0}

# reset the option and check whether the default redundancy count is
# accepted or not.
TEST $CLI volume reset $V0 disperse.quorum-count
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "^0$" ec_option_value $V0 $M0 0 quorum-count
TEST touch $M0/a1
TEST touch $M0/data1
TEST setfattr -n trusted.def -v def $M0/a1
TEST touch $M0/src1
TEST touch $M0/del-me1
TEST mkdir $M0/dir11
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST touch $M0/a21
TEST mkdir $M0/dir21
TEST mknod  $M0/b21 b 4 5
TEST ln -s $M0/a1 $M0/symlink1
TEST ln $M0/a1 $M0/link1
TEST mv $M0/src1 $M0/dst1
TEST rm -f $M0/del-me1
TEST rmdir $M0/dir11
TEST dd if=/dev/zero of=$M0/a1 bs=1M count=1 conv=notrunc
TEST dd if=/dev/zero of=$M0/data1 bs=1M count=1 conv=notrunc
TEST truncate -s 0 $M0/a1
TEST setfattr -n trusted.abc -v abc $M0/a1
TEST setfattr -x trusted.def $M0/a1
TEST chmod +x $M0/a1
TEST fallocate -l 2m -n $M0/a1
TEST fallocate -p -l 512k $M0/a1
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0

TEST touch $M0/a2
TEST touch $M0/data2
TEST setfattr -n trusted.def -v def $M0/a1
TEST touch $M0/src2
TEST touch $M0/del-me2
TEST mkdir $M0/dir12
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST ! touch $M0/a22
TEST ! mkdir $M0/dir22
TEST ! mknod  $M0/b22 b 4 5
TEST ! ln -s $M0/a2 $M0/symlink2
TEST ! ln $M0/a2 $M0/link2
TEST ! mv $M0/src2 $M0/dst2
TEST ! rm -f $M0/del-me2
TEST ! rmdir $M0/dir12
TEST ! dd if=/dev/zero of=$M0/a2 bs=1M count=1 conv=notrunc
TEST ! dd if=/dev/zero of=$M0/data2 bs=1M count=1 conv=notrunc
TEST ! truncate -s 0 $M0/a2
TEST ! setfattr -n trusted.abc -v abc $M0/a2
TEST ! setfattr -x trusted.def $M0/a2
TEST ! chmod +x $M0/a2
TEST ! fallocate -l 2m -n $M0/a2
TEST ! fallocate -p -l 512k $M0/a2
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count ${V0}

# Set quorum-count to 5 and kill 1 brick and the fops should pass
TEST $CLI volume set $V0 disperse.quorum-count 5
EXPECT_WITHIN $CONFIG_UPDATE_TIMEOUT "^5$" ec_option_value $V0 $M0 0 quorum-count
TEST touch $M0/a3
TEST touch $M0/data3
TEST setfattr -n trusted.def -v def $M0/a3
TEST touch $M0/src3
TEST touch $M0/del-me3
TEST mkdir $M0/dir13
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST touch $M0/a31
TEST mkdir $M0/dir31
TEST mknod  $M0/b31 b 4 5
TEST ln -s $M0/a3 $M0/symlink3
TEST ln $M0/a3 $M0/link3
TEST mv $M0/src3 $M0/dst3
TEST rm -f $M0/del-me3
TEST rmdir $M0/dir13
TEST dd if=/dev/zero of=$M0/a3 bs=1M count=1 conv=notrunc
TEST dd if=/dev/zero of=$M0/data3 bs=1M count=1 conv=notrunc
TEST truncate -s 0 $M0/a3
TEST setfattr -n trusted.abc -v abc $M0/a3
TEST setfattr -x trusted.def $M0/a3
TEST chmod +x $M0/a3
TEST fallocate -l 2m -n $M0/a3
TEST fallocate -p -l 512k $M0/a3
TEST dd if=/dev/urandom of=$M0/heal-file bs=1M count=1 oflag=direct
cksum_before_heal="$(md5sum $M0/heal-file | awk '{print $1}')"
TEST $CLI volume start $V0 force
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count ${V0}
TEST kill_brick $V0 $H0 $B0/${V0}4
TEST kill_brick $V0 $H0 $B0/${V0}5
cksum_after_heal=$(dd if=$M0/heal-file iflag=direct | md5sum | awk '{print $1}')
TEST [[ $cksum_before_heal == $cksum_after_heal ]]
cleanup;
