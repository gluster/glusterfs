#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../ec.rc

EC_READMASK_XATTR="glusterfs.ec.readmask"

# Parsing XML file to get count of READ calls made on bricks
get_brick_reads() {
    local xml_file="$1"
    local -n result_array=$2  # Reference to output array

    local index=0
    local brick_reads=0
    local inside_fop=0
    local read_hits=0
    local brick_found=0

    while IFS= read -r line; do
        if [[ $line =~ "<brickName>" ]]; then
            if ((brick_found)); then
                result_array[index]=$brick_reads
                ((index++))
            fi
            brick_found=1
            brick_reads=0
        fi

        if [[ $line =~ "<fop>" ]]; then
            inside_fop=1
            read_hits=0
        fi

        if [[ $inside_fop -eq 1 && $line =~ "<name>READ</name>" ]]; then
            read_hits=1
        fi

        if [[ $inside_fop -eq 1 && $line =~ "<hits>" ]]; then
            if ((read_hits)); then
                brick_reads=$(echo "$line" | sed -E 's/.*<hits>([0-9]+)<\/hits>.*/\1/')
            fi
        fi

        if [[ $line =~ "</fop>" ]]; then
            inside_fop=0
        fi
    done < "$xml_file"

    if ((brick_found)); then
        result_array[index]=$brick_reads
    fi
}

#Function to compare read_count arrays and verify that only specified bricks have modified read values
check_array() {
    local -n arr=$1  
    local -a indices=("${@:2}") 

    local length1=${#arr[@]}

    local -A changed_indices

    for i in "${!arr[@]}"; do
        if [[ "${arr[i]}" -ne "0" ]]; then
            changed_indices[$i]=1
        fi
    done

    for i in "${!changed_indices[@]}"; do
        if [[ ! " ${indices[@]} " =~ " $i " ]]; then
            echo "Unexpected change at index $i"
            return 1
        fi
    done

    echo "Only specified indices changed"
    return 0
}

validate_read() {
    local mask="$1"
    local space_sep_values="${mask//:/ }"

    $CLI volume profile $V0 info incremental > /dev/null

    # Set readmask to bricks 0, 1, 3, 5, 8, 9
    if ! setfattr -n "$EC_READMASK_XATTR" -v "$mask" "$M0/newfile"; then
        echo "Failed to set readmask xattr"
        return 1
    fi

    dd if="$M0/newfile" of=/dev/null iflag=direct bs=4M

    sleep 1

    $CLI volume profile $V0 info incremental --xml > "$tmpdir/after_mask.xml"
    local -a brick_reads_array
    get_brick_reads "$tmpdir/after_mask.xml" brick_reads_array
    echo "After readmask: ${brick_reads_array[@]}"

    check_array brick_reads_array "${space_sep_values[@]}"
    return $?
}

#Setup
cleanup
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

TEST mkdir -p $B0/${V0}{0,1,2,3,4,5,6,7,8,9}
TEST $CLI volume create $V0 disperse 10 redundancy 4 $H0:$B0/${V0}{0,1,2,3,4,5,6,7,8,9}

EXPECT "$V0" volinfo_field $V0 'Volume Name'
EXPECT 'Created' volinfo_field $V0 'Status'
EXPECT '10' brick_count $V0

TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Started' volinfo_field $V0 'Status'

# Mount FUSE with caching disabled
TEST $GFS -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "10" ec_child_up_count $V0 0

TEST $CLI volume profile $V0 start


# Create file
TEST dd if=/dev/urandom of=$M0/newfile bs=4M count=5

# Read without setting readmask xattr should not fail
TEST dd if=$M0/newfile of=/dev/null iflag=direct bs=4M

# Create temporary directory
tmpdir=$(mktemp -d -t ${0##*/}.XXXXXX)


# Test 1: Read with mask set to bricks 0, 1, 3, 5, 8, 9
TEST validate_read "0:1:3:5:8:9"

# Test 2: Read with mask set to bricks 4, 5, 6, 7, 8, 9
TEST validate_read "4:5:6:7:8:9"

# Test 3: setfattr wont set invalid read_masks
TEST ! setfattr -n $EC_READMASK_XATTR -v "1:sm:snb:adi:as" $M0/newfile

TEST ! setfattr -n $EC_READMASK_XATTR -v "0:1::2ab" $M0/newfile

# Test 4: setfattr wont set read_mask in case insufficient bricks are provided
TEST ! setfattr -n $EC_READMASK_XATTR -v "0:1:2:3" $M0/newfile
TEST ! setfattr -n $EC_READMASK_XATTR -v "4:5" $M0/newfile

rm -rf "$tmpdir"
cleanup;