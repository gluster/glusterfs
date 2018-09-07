#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc
. $(dirname $0)/../../common-utils.rc

# Test overview:
# Test all combinations of src-hashed/src-cached/dst-hashed/dst-cached

hashdebugxattr="dht.file.hashed-subvol."

function get_brick_index {
        local inpath=$1
        brickroot=$(getfattr -m . -n trusted.glusterfs.pathinfo $inpath | tr ' ' '\n' | sed -n 's/<POSIX(\(.*\)):.*:.*>.*/\1/p')
        echo ${brickroot:(-1)}
}

function get_brick_path_for_subvol {
        local in_subvol=$1
        local in_brickpath

        in_brickpath=$(cat "$M0/.meta/graphs/active/$in_subvol/options/remote-subvolume")
        echo $in_brickpath

}

#Checks that file exists only on hashed and/or cached
function file_existence_check
{
        local in_file_path=$1
        local in_hashed=$2
        local in_cached=$3
        local in_client_subvol
        local in_brickpath
        local ret

        for i in {0..3}
        do
               in_client_subvol="$V0-client-$i"
               in_brickpath=$(cat "$M0/.meta/graphs/active/$in_client_subvol/options/remote-subvolume")
               stat "$in_brickpath/$in_file_path" 2>/dev/null
               ret=$?
               # Either the linkto or the data file must exist on the hashed
               if [ "$in_client_subvol" == "$in_hashed" ]; then
                        if [ $ret -ne 0 ]; then
                            return 1
                        fi
                        continue
               fi

               # If the cached is non-null, we expect the file to exist on it
               if [ "$in_client_subvol" == "$in_cached" ]; then
                        if [ $ret -ne 0 ]; then
                            return 1
                        fi
                        continue
               fi

               if [ $ret -eq 0 ]; then
                  return 2
               fi
        done
        return 0
}


# Check if file exists on any of the bricks of the volume
function file_does_not_exist
{
        local inpath=$1
        for i in `seq 0 3`
        do
                file_path=$B0/$V0-$i/$inpath
                if [ -f "$file_path" ]; then
                        echo "1"
                        return 1
                fi
        done
        return 0
}


# Input: filename dirpath
function get_hash_subvol
{
      hash_subvol=$(getfattr --only-values -n "$hashdebugxattr$1" $2 2>/dev/null)
}



# Find the first filename that hashes to a subvol
# other than $1

function first_filename_with_diff_hashsubvol
{
        local in_subvol=$1
        local in_path=$2
        local file_pattern=$3
        local in_hash_subvol

        for i in {1..100}
        do
               dstfilename="$file_pattern$i"
               in_hash_subvol=$(get_hash_subvol "$dstfilename" "$in_path")
               echo $in_hash_subvol
               if [ "$in_subvol" != "$in_hash_subvol" ]; then
                        return 0
               fi
        done
        return 1
}

# Find the first filename that hashes to the same subvol
# as $1
function first_filename_with_same_hashsubvol
{
        local in_subvol=$1
        local in_path=$2
        local in_hash_subvol
        local file_pattern=$3

        for i in {1..100}
        do
               dstfilename="$file_pattern$i"
               get_hash_subvol "$dstfilename" "$in_path"
               in_hash_subvol=$hash_subvol
#               echo $in_hash_subvol
               if [ "$in_subvol" == "$in_hash_subvol" ]; then
                        return 0
               fi
        done
        return 1
}

function file_is_linkto
{
    local  brick_filepath=$1

    test=$(stat $brick_filepath 2>&1)
    if [ $? -ne 0 ]; then
        echo "2"
        return
    fi

    test=$(getfattr -n trusted.glusterfs.dht.linkto -e text $brick_filepath 2>&1)

    if [ $? -eq 0 ]; then
       echo "1"
    else
       echo "0"
    fi
}




cleanup

TEST glusterd
TEST pidof glusterd


# We need at least 4 bricks to test all combinations of hashed and
# cached files

TEST $CLI volume create $V0 $H0:$B0/$V0-{0..3}
TEST $CLI volume start $V0

# Mount using FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0


################################################################
# The first set of tests are those where the Dst file does not exist
# dst-cached = NULL
#
###############################################################

################### Test 1 ####################################
#
# src-hashed = src-cached = dst-hashed
# dst-cached = null
# src-file = src-1

echo " **** Test 1 **** "

src_file="src-1"

TEST mkdir $M0/test-1
TEST touch $M0/test-1/$src_file

TEST get_hash_subvol $src_file $M0/test-1
src_hashed=$hash_subvol
#echo "Hashed subvol for $src_file: " $src_hashed

# Find a file name that hashes to the same subvol as $src_file
TEST first_filename_with_same_hashsubvol "$src_hashed" "$M0/test-1" "dst-"
#echo "dst-file name: " $dstfilename
dst_hashed=$src_hashed

src_hash_brick=$(get_brick_path_for_subvol $src_hashed)

echo "Renaming $src_file to $dstfilename"

TEST mv $M0/test-1/$src_file $M0/test-1/$dstfilename

# Expected:
# dst file is accessible from the mount point
# dst file exists only on the hashed brick.
# no linkto files on any bricks
# src files do not exist


TEST stat $M0/test-1/$dstfilename 2>/dev/null
TEST file_existence_check test-1/$dstfilename $src_hashed
TEST file_does_not_exist test-1/$src_file
EXPECT "0" file_is_linkto $src_hash_brick/test-1/$dstfilename


################### Test 2 ####################################

# src-hashed = src-cached != dst-hashed
# dst-cached = null

echo " **** Test 2 **** "

src_file="src-1"

TEST mkdir $M0/test-2
TEST touch $M0/test-2/$src_file

TEST get_hash_subvol $src_file $M0/test-2
src_hashed=$hash_subvol
#echo "Hashed subvol for $src_file: " $src_hashed

# Find a file name that hashes to a diff hashed subvol than $src_file
TEST first_filename_with_diff_hashsubvol "$src_hashed" "$M0/test-2" "dst-"
echo "dst-file name: " $dstfilename
TEST get_hash_subvol $dstfilename $M0/test-2
dst_hashed=$hash_subvol

src_hash_brick=$(get_brick_path_for_subvol $src_hashed)
dst_hash_brick=$(get_brick_path_for_subvol $dst_hashed)

echo "Renaming $src_file to $dstfilename"

TEST mv $M0/test-2/$src_file $M0/test-2/$dstfilename


# Expected:
# dst file is accessible from the mount point
# dst data file on src_hashed and dst linkto file on dst_hashed
# src files do not exist


TEST stat $M0/test-2/$dstfilename 2>/dev/null
TEST file_existence_check test-2/$dstfilename $dst_hashed $src_hashed
TEST file_does_not_exist test-2/$src_file
EXPECT "1" file_is_linkto $dst_hash_brick/test-2/$dstfilename
EXPECT "0" file_is_linkto $src_hash_brick/test-2/$dstfilename

################### Test 3 ####################################

# src-hashed = dst-hashed != src-cached

echo " **** Test 3 **** "

src_file0="abc-1"

# 1. Create src file with src_cached != src_hashed
TEST mkdir $M0/test-3
TEST touch $M0/test-3/$src_file0

TEST get_hash_subvol $src_file0 $M0/test-3
src_cached=$hash_subvol
#echo "Hashed subvol for $src_file0: " $src_cached

# Find a file name that hashes to a diff hashed subvol than $src_file0
TEST first_filename_with_diff_hashsubvol "$src_cached" "$M0/test-3" "src-"
echo "dst-file name: " $dstfilename
src_file=$dstfilename

TEST mv $M0/test-3/$src_file0 $M0/test-3/$src_file

TEST get_hash_subvol $src_file $M0/test-3
src_hashed=$hash_subvol


# 2. Rename src to dst
TEST first_filename_with_same_hashsubvol "$src_hashed" "$M0/test-3" "dst-"
#echo "dst-file name: " $dstfilename

src_hash_brick=$(get_brick_path_for_subvol $src_hashed)
src_cached_brick=$(get_brick_path_for_subvol $src_cached)

echo "Renaming $src_file to $dstfilename"

TEST mv $M0/test-3/$src_file $M0/test-3/$dstfilename


# Expected:
# dst file is accessible from the mount point
TEST stat $M0/test-3/$dstfilename 2>/dev/null

# src file does not exist
TEST file_does_not_exist test-3/$src_file

# dst linkto file on src_hashed and dst data file on src_cached
TEST file_existence_check test-3/$dstfilename $src_hashed $src_cached

EXPECT "1" file_is_linkto $src_hash_brick/test-3/$dstfilename
EXPECT "0" file_is_linkto $src_cached_brick/test-3/$dstfilename



################### Test 4 ####################################

# src-cached = dst-hashed != src-hashed

echo " **** Test 4 **** "

src_file0="abc-1"

# 1. Create src file with src_cached != src_hashed
TEST mkdir $M0/test-4
TEST touch $M0/test-4/$src_file0

TEST get_hash_subvol $src_file0 $M0/test-4
src_cached=$hash_subvol
#echo "Hashed subvol for $src_file0: " $src_cached

# Find a file name that hashes to a diff hashed subvol than $src_file0
TEST first_filename_with_diff_hashsubvol "$src_cached" "$M0/test-4" "src-"
src_file=$dstfilename

TEST mv $M0/test-4/$src_file0 $M0/test-4/$src_file

TEST get_hash_subvol $src_file $M0/test-4
src_hashed=$hash_subvol


# 2. Rename src to dst
TEST first_filename_with_same_hashsubvol "$src_cached" "$M0/test-4" "dst-"
#echo "dst-file name: " $dstfilename

src_hash_brick=$(get_brick_path_for_subvol $src_hashed)
src_cached_brick=$(get_brick_path_for_subvol $src_cached)

echo "Renaming $src_file to $dstfilename"

TEST mv $M0/test-4/$src_file $M0/test-4/$dstfilename

# Expected:
# dst file is accessible from the mount point
TEST stat $M0/test-4/$dstfilename 2>/dev/null

# src file does not exist
TEST file_does_not_exist test-4/$src_file

# dst linkto file on src_hashed and dst data file on src_cached
TEST file_existence_check test-4/$dstfilename $src_cached

EXPECT "0" file_is_linkto $src_cached_brick/test-4/$dstfilename


################### Test 5 ####################################

# src-cached != src-hashed
# src-hashed != dst-hashed
# src-cached != dst-hashed


echo " **** Test 5 **** "

# 1. Create src and dst files

TEST mkdir $M0/test-5

TEST first_filename_with_same_hashsubvol "$V0-client-0" "$M0/test-5" "abc-"
src_file0=$dstfilename

TEST touch $M0/test-5/$src_file0

TEST get_hash_subvol $src_file0 $M0/test-5
src_cached=$hash_subvol
#echo "Hashed subvol for $src_file0: " $src_cached

# Find a file name that hashes to a diff hashed subvol than $src_file0
TEST first_filename_with_same_hashsubvol "$V0-client-1" "$M0/test-5" "src-"
src_file=$dstfilename

TEST mv $M0/test-5/$src_file0 $M0/test-5/$src_file

TEST get_hash_subvol $src_file $M0/test-5
src_hashed=$hash_subvol

TEST first_filename_with_same_hashsubvol "$V0-client-2" "$M0/test-5" "dst-"
#echo "dst-file name: " $dstfilename

dst_hash_brick=$(get_brick_path_for_subvol "$V0-client-2")
src_cached_brick=$(get_brick_path_for_subvol $src_cached)


# 2. Rename src to dst
echo "Renaming $src_file to $dstfilename"

TEST mv $M0/test-5/$src_file $M0/test-5/$dstfilename


# 3. Validate

# Expected:
# dst file is accessible from the mount point
TEST stat $M0/test-5/$dstfilename 2>/dev/null

# src file does not exist
TEST file_does_not_exist test-5/$src_file

# dst linkto file on src_hashed and dst data file on src_cached

EXPECT "0" file_is_linkto $src_cached_brick/test-5/$dstfilename
EXPECT "1" file_is_linkto $dst_hash_brick/test-5/$dstfilename


########################################################################
#
# The Dst file exists
#
########################################################################

################### Test 6 ####################################

# src_hash = src_cached
# dst_hash = dst_cached
# dst_hash = src_hash


TEST mkdir $M0/test-6

# 1. Create src and dst files

TEST first_filename_with_same_hashsubvol "$V0-client-0" "$M0/test-6" "src-"
src_file=$dstfilename

TEST touch $M0/test-6/$src_file

TEST first_filename_with_same_hashsubvol "$V0-client-0" "$M0/test-6" "dst-"
dst_file=$dstfilename

TEST touch $M0/test-6/$dst_file


# 2. Rename src to dst

TEST mv $M0/test-6/$src_file $M0/test-6/$dst_file


# 3. Validate

dst_hash_brick=$(get_brick_path_for_subvol "$V0-client-0")

TEST stat $M0/test-6/$dst_file 2>/dev/null
TEST file_existence_check test-6/$dst_file "$V0-client-0"
TEST file_does_not_exist test-6/$src_file
EXPECT "0" file_is_linkto $dst_hash_brick/test-6/$dst_file


################### Test 7 ####################################

# src_hash = src_cached
# dst_hash = dst_cached
# dst_hash != src_hash


echo " **** Test 7 **** "

TEST mkdir $M0/test-7

# 1. Create src and dst files

TEST first_filename_with_same_hashsubvol "$V0-client-0" "$M0/test-7" "src-"
src_file=$dstfilename

TEST touch $M0/test-7/$src_file

TEST first_filename_with_same_hashsubvol "$V0-client-1" "$M0/test-7" "dst-"
dst_file=$dstfilename

TEST touch $M0/test-7/$dst_file


# 2. Rename src to dst

TEST mv $M0/test-7/$src_file $M0/test-7/$dst_file


# 3. Validate

dst_hash_brick=$(get_brick_path_for_subvol "$V0-client-1")
src_hash_brick=$(get_brick_path_for_subvol "$V0-client-0")

TEST stat $M0/test-7/$dst_file 2>/dev/null
TEST file_existence_check test-7/$dst_file "$V0-client-1" "$V0-client-0"
TEST file_does_not_exist test-7/$src_file

EXPECT "0" file_is_linkto $src_hash_brick/test-7/$dst_file
EXPECT "1" file_is_linkto $dst_hash_brick/test-7/$dst_file


################### Test 8 ####################################

# src_hash = src_cached
# dst_hash != dst_cached
# dst_hash != src_hash
# dst_cached != src_hash

echo " **** Test 8 **** "

TEST mkdir $M0/test-8


# 1. Create src and dst files

TEST first_filename_with_same_hashsubvol "$V0-client-0" "$M0/test-8" "src-"
src_file=$dstfilename
TEST touch $M0/test-8/$src_file

TEST first_filename_with_same_hashsubvol "$V0-client-1" "$M0/test-8" "dst0-"
dst_file0=$dstfilename
TEST touch $M0/test-8/$dst_file0

TEST first_filename_with_same_hashsubvol "$V0-client-2" "$M0/test-8" "dst-"
dst_file=$dstfilename

mv $M0/test-8/$dst_file0  $M0/test-8/$dst_file


# 2. Rename the file

mv $M0/test-8/$src_file  $M0/test-8/$dst_file


# 3. Validate

dst_hash_brick=$(get_brick_path_for_subvol "$V0-client-2")
src_hash_brick=$(get_brick_path_for_subvol "$V0-client-0")

TEST stat $M0/test-8/$dst_file 2>/dev/null
TEST file_existence_check test-8/$dst_file "$V0-client-2" "$V0-client-0"
TEST file_does_not_exist test-8/$src_file

EXPECT "0" file_is_linkto $src_hash_brick/test-8/$dst_file
EXPECT "1" file_is_linkto $dst_hash_brick/test-8/$dst_file

################### Test 9 ####################################

# src_hash = src_cached = dst_hash
# dst_hash != dst_cached

echo " **** Test 9 **** "

TEST mkdir $M0/test-9


# 1. Create src and dst files


TEST first_filename_with_same_hashsubvol "$V0-client-0" "$M0/test-9" "src-"
src_file=$dstfilename
TEST touch $M0/test-9/$src_file


TEST first_filename_with_same_hashsubvol "$V0-client-1" "$M0/test-9" "dst0-"
dst0_file=$dstfilename
TEST touch $M0/test-9/$dst0_file

TEST first_filename_with_same_hashsubvol "$V0-client-0" "$M0/test-9" "dst-"
dst_file=$dstfilename

TEST mv $M0/test-9/$dst0_file $M0/test-9/$dst_file

# 2. Rename the file

mv $M0/test-9/$src_file  $M0/test-9/$dst_file


# 3. Validate

dst_hash_brick=$(get_brick_path_for_subvol "$V0-client-0")

TEST stat $M0/test-9/$dst_file 2>/dev/null
TEST file_existence_check test-9/$dst_file "$V0-client-0"
TEST file_does_not_exist test-9/$src_file
EXPECT "0" file_is_linkto $dst_hash_brick/test-9/$dst_file


################### Test 10 ####################################

# src_hash = src_cached = dst_cached
# dst_hash != dst_cached

echo " **** Test 10 **** "

TEST mkdir $M0/test-10


# 1. Create src and dst files

TEST first_filename_with_same_hashsubvol "$V0-client-0" "$M0/test-10" "src-"
src_file=$dstfilename
TEST touch $M0/test-10/$src_file

TEST first_filename_with_same_hashsubvol "$V0-client-0" "$M0/test-10" "dst0-"
dst0_file=$dstfilename
TEST touch $M0/test-10/$dst0_file

TEST first_filename_with_same_hashsubvol "$V0-client-1" "$M0/test-10" "dst-"
dst_file=$dstfilename

mv $M0/test-10/$dst0_file  $M0/test-10/$dst_file


# 2. Rename the file

mv $M0/test-10/$src_file  $M0/test-10/$dst_file


# 3. Validate

dst_hash_brick=$(get_brick_path_for_subvol "$V0-client-1")
dst_cached_brick=$(get_brick_path_for_subvol "$V0-client-0")

TEST stat $M0/test-10/$dst_file 2>/dev/null
TEST file_existence_check test-10/$dst_file "$V0-client-1" "$V0-client-0"
TEST file_does_not_exist test-10/$src_file
EXPECT "1" file_is_linkto $dst_hash_brick/test-10/$dst_file
EXPECT "0" file_is_linkto $dst_cached_brick/test-10/$dst_file


################### Test 11 ####################################

# src_hash != src_cached
# dst_hash = dst_cached = src_cached

echo " **** Test 11 **** "

TEST mkdir $M0/test-11


# 1. Create src and dst files

TEST first_filename_with_same_hashsubvol "$V0-client-0" "$M0/test-11" "src0-"
src0_file=$dstfilename
TEST touch $M0/test-11/$src0_file

TEST first_filename_with_same_hashsubvol "$V0-client-1" "$M0/test-11" "src-"
src_file=$dstfilename

mv $M0/test-11/$src0_file  $M0/test-11/$src_file


TEST first_filename_with_same_hashsubvol "$V0-client-0" "$M0/test-11" "dst-"
dst_file=$dstfilename
TEST touch $M0/test-11/$dst_file


# 2. Rename the file

mv $M0/test-11/$src_file  $M0/test-11/$dst_file


# 3. Validate

dst_hash_brick=$(get_brick_path_for_subvol "$V0-client-0")

TEST stat $M0/test-11/$dst_file 2>/dev/null
TEST file_existence_check test-11/$dst_file "$V0-client-0"
TEST file_does_not_exist test-11/$src_file
EXPECT "0" file_is_linkto $dst_hash_brick/test-11/$dst_file


################### Test 12 ####################################

# src_hash != src_cached
# dst_hash = dst_cached = src_hash

echo " **** Test 12 **** "

TEST mkdir $M0/test-12


# 1. Create src and dst files

TEST first_filename_with_same_hashsubvol "$V0-client-0" "$M0/test-12" "src0-"
src0_file=$dstfilename
TEST touch $M0/test-12/$src0_file

TEST first_filename_with_same_hashsubvol "$V0-client-1" "$M0/test-12" "src-"
src_file=$dstfilename

mv $M0/test-12/$src0_file  $M0/test-12/$src_file


TEST first_filename_with_same_hashsubvol "$V0-client-1" "$M0/test-12" "dst-"
dst_file=$dstfilename
TEST touch $M0/test-12/$dst_file


# 2. Rename the file

mv $M0/test-12/$src_file  $M0/test-12/$dst_file


# 3. Validate

dst_hash_brick=$(get_brick_path_for_subvol "$V0-client-1")
dst_cached_brick=$(get_brick_path_for_subvol "$V0-client-0")

TEST stat $M0/test-12/$dst_file 2>/dev/null
TEST file_existence_check test-12/$dst_file "$V0-client-1" "$V0-client-0"
TEST file_does_not_exist test-12/$src_file
EXPECT "1" file_is_linkto $dst_hash_brick/test-12/$dst_file
EXPECT "0" file_is_linkto $dst_cached_brick/test-12/$dst_file

################### Test 13 ####################################

# src_hash != src_cached
# dst_hash = dst_cached
# dst_hash != src_cached
# dst_hash != src_hash

echo " **** Test 13 **** "

TEST mkdir $M0/test-13


# 1. Create src and dst files

TEST first_filename_with_same_hashsubvol "$V0-client-0" "$M0/test-13" "src0-"
src0_file=$dstfilename
TEST touch $M0/test-13/$src0_file

TEST first_filename_with_same_hashsubvol "$V0-client-1" "$M0/test-13" "src-"
src_file=$dstfilename

mv $M0/test-13/$src0_file  $M0/test-13/$src_file


TEST first_filename_with_same_hashsubvol "$V0-client-2" "$M0/test-13" "dst-"
dst_file=$dstfilename
TEST touch $M0/test-13/$dst_file

# 2. Rename the file

mv $M0/test-13/$src_file  $M0/test-13/$dst_file


# 3. Validate

dst_hash_brick=$(get_brick_path_for_subvol "$V0-client-2")
dst_cached_brick=$(get_brick_path_for_subvol "$V0-client-0")

TEST stat $M0/test-13/$dst_file 2>/dev/null
TEST file_existence_check test-13/$dst_file "$V0-client-2" "$V0-client-0"
TEST file_does_not_exist test-13/$src_file
EXPECT "1" file_is_linkto $dst_hash_brick/test-13/$dst_file
EXPECT "0" file_is_linkto $dst_cached_brick/test-13/$dst_file


################### Test 14 ####################################

# src_hash != src_cached
# dst_hash = src_hash
# dst_cached = src_cached

echo " **** Test 14 **** "

TEST mkdir $M0/test-14


# 1. Create src and dst files

TEST first_filename_with_same_hashsubvol "$V0-client-0" "$M0/test-14" "src0-"
src0_file=$dstfilename
TEST touch $M0/test-14/$src0_file

TEST first_filename_with_same_hashsubvol "$V0-client-1" "$M0/test-14" "src-"
src_file=$dstfilename

mv $M0/test-14/$src0_file  $M0/test-14/$src_file


TEST first_filename_with_same_hashsubvol "$V0-client-0" "$M0/test-14" "dst0-"
dst0_file=$dstfilename
TEST touch $M0/test-14/$dst0_file

TEST first_filename_with_same_hashsubvol "$V0-client-1" "$M0/test-14" "dst-"
dst_file=$dstfilename

mv $M0/test-14/$dst0_file  $M0/test-14/$dst_file


# 2. Rename the file

mv $M0/test-14/$src_file  $M0/test-14/$dst_file


# 3. Validate

dst_hash_brick=$(get_brick_path_for_subvol "$V0-client-1")
dst_cached_brick=$(get_brick_path_for_subvol "$V0-client-0")

TEST stat $M0/test-14/$dst_file 2>/dev/null
TEST file_existence_check test-14/$dst_file "$V0-client-1" "$V0-client-0"
TEST file_does_not_exist test-14/$src_file
EXPECT "1" file_is_linkto $dst_hash_brick/test-14/$dst_file
EXPECT "0" file_is_linkto $dst_cached_brick/test-14/$dst_file

################### Test 15 ####################################

# src_hash != src_cached
# dst_hash != src_hash
# dst_hash != src_cached
# dst_cached = src_cached

echo " **** Test 15 **** "

TEST mkdir $M0/test-15


# 1. Create src and dst files

TEST first_filename_with_same_hashsubvol "$V0-client-0" "$M0/test-15" "src0-"
src0_file=$dstfilename
TEST touch $M0/test-15/$src0_file

TEST first_filename_with_same_hashsubvol "$V0-client-1" "$M0/test-15" "src-"
src_file=$dstfilename

mv $M0/test-15/$src0_file  $M0/test-15/$src_file


TEST first_filename_with_same_hashsubvol "$V0-client-0" "$M0/test-15" "dst0-"
dst0_file=$dstfilename
TEST touch $M0/test-15/$dst0_file

TEST first_filename_with_same_hashsubvol "$V0-client-2" "$M0/test-15" "dst-"
dst_file=$dstfilename

mv $M0/test-15/$dst0_file  $M0/test-15/$dst_file


# 2. Rename the file

mv $M0/test-15/$src_file  $M0/test-15/$dst_file

# 3. Validate

dst_hash_brick=$(get_brick_path_for_subvol "$V0-client-2")
dst_cached_brick=$(get_brick_path_for_subvol "$V0-client-0")

TEST stat $M0/test-15/$dst_file 2>/dev/null
TEST file_existence_check test-15/$dst_file "$V0-client-2" "$V0-client-0"
TEST file_does_not_exist test-15/$src_file
EXPECT "1" file_is_linkto $dst_hash_brick/test-15/$dst_file
EXPECT "0" file_is_linkto $dst_cached_brick/test-15/$dst_file



################### Test 16 ####################################

# src_hash != src_cached
# dst_hash = src_cached
# dst_cached = src_hash

echo " **** Test 16 **** "

TEST mkdir $M0/test-16


# 1. Create src and dst files

TEST first_filename_with_same_hashsubvol "$V0-client-0" "$M0/test-16" "src0-"
src0_file=$dstfilename
TEST touch $M0/test-16/$src0_file

TEST first_filename_with_same_hashsubvol "$V0-client-1" "$M0/test-16" "src-"
src_file=$dstfilename

mv $M0/test-16/$src0_file  $M0/test-16/$src_file


TEST first_filename_with_same_hashsubvol "$V0-client-1" "$M0/test-16" "dst0-"
dst0_file=$dstfilename
TEST touch $M0/test-16/$dst0_file

TEST first_filename_with_same_hashsubvol "$V0-client-0" "$M0/test-16" "dst-"
dst_file=$dstfilename

mv $M0/test-16/$dst0_file  $M0/test-16/$dst_file


# 2. Rename the file

mv $M0/test-16/$src_file  $M0/test-16/$dst_file

# 3. Validate

dst_hash_brick=$(get_brick_path_for_subvol "$V0-client-0")

TEST stat $M0/test-16/$dst_file 2>/dev/null
TEST file_existence_check test-16/$dst_file "$V0-client-0"
TEST file_does_not_exist test-16/$src_file
EXPECT "0" file_is_linkto $dst_hash_brick/test-16/$dst_file


################### Test 17 ####################################

# src_hash != src_cached
# dst_hash != dst_cached
# dst_hash != src_hash != src_cached
# dst_cached = src_hash


echo " **** Test 17 **** "

TEST mkdir $M0/test-17


# 1. Create src and dst files

TEST first_filename_with_same_hashsubvol "$V0-client-0" "$M0/test-17" "src0-"
src0_file=$dstfilename
TEST touch $M0/test-17/$src0_file

TEST first_filename_with_same_hashsubvol "$V0-client-1" "$M0/test-17" "src-"
src_file=$dstfilename

mv $M0/test-17/$src0_file  $M0/test-17/$src_file


TEST first_filename_with_same_hashsubvol "$V0-client-1" "$M0/test-17" "dst0-"
dst0_file=$dstfilename
TEST touch $M0/test-17/$dst0_file

TEST first_filename_with_same_hashsubvol "$V0-client-2" "$M0/test-17" "dst-"
dst_file=$dstfilename

mv $M0/test-17/$dst0_file  $M0/test-17/$dst_file


# 2. Rename the file

mv $M0/test-17/$src_file  $M0/test-17/$dst_file

# 3. Validate

dst_hash_brick=$(get_brick_path_for_subvol "$V0-client-2")
dst_cached_brick=$(get_brick_path_for_subvol "$V0-client-0")

TEST stat $M0/test-17/$dst_file 2>/dev/null
TEST file_existence_check test-17/$dst_file "$V0-client-2" "$V0-client-0"
TEST file_does_not_exist test-17/$src_file
EXPECT "1" file_is_linkto $dst_hash_brick/test-17/$dst_file
EXPECT "0" file_is_linkto $dst_cached_brick/test-17/$dst_file


################### Test 18 ####################################

# src_hash != src_cached
# dst_hash != dst_cached
# dst_hash != src_hash != src_cached != dst_cached


echo " **** Test 18 **** "

TEST mkdir $M0/test-18


# 1. Create src and dst files

TEST first_filename_with_same_hashsubvol "$V0-client-0" "$M0/test-18" "src0-"
src0_file=$dstfilename
TEST touch $M0/test-18/$src0_file

TEST first_filename_with_same_hashsubvol "$V0-client-1" "$M0/test-18" "src-"
src_file=$dstfilename

mv $M0/test-18/$src0_file  $M0/test-18/$src_file


TEST first_filename_with_same_hashsubvol "$V0-client-2" "$M0/test-18" "dst0-"
dst0_file=$dstfilename
TEST touch $M0/test-18/$dst0_file

TEST first_filename_with_same_hashsubvol "$V0-client-3" "$M0/test-18" "dst-"
dst_file=$dstfilename

mv $M0/test-18/$dst0_file  $M0/test-18/$dst_file


# 2. Rename the file

mv $M0/test-18/$src_file  $M0/test-18/$dst_file

# 3. Validate

dst_hash_brick=$(get_brick_path_for_subvol "$V0-client-3")
dst_cached_brick=$(get_brick_path_for_subvol "$V0-client-0")

TEST stat $M0/test-18/$dst_file 2>/dev/null
TEST file_existence_check test-18/$dst_file "$V0-client-3" "$V0-client-0"
TEST file_does_not_exist test-18/$src_file
EXPECT "1" file_is_linkto $dst_hash_brick/test-18/$dst_file
EXPECT "0" file_is_linkto $dst_cached_brick/test-18/$dst_file


# Cleanup
cleanup

