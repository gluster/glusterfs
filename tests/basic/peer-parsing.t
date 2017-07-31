#!/bin/bash

. $(dirname $0)/../include.rc

PEER_DIR="$GLUSTERD_WORKDIR"/peers
TEST mkdir -p $PEER_DIR

declare -i HOST_NUM=100

create_random_peer_files() {
        for i in $(seq 0 9); do
                local peer_uuid=$(uuidgen)
                # The rules for quoting and variable substitution in
                # here documents would force this to be even less
                # readable that way.
                (
                        echo "state=1"
                        echo "uuid=$peer_uuid"
                        echo "hostname=127.0.0.$HOST_NUM"
                ) > $PEER_DIR/$peer_uuid
                HOST_NUM+=1
        done
}

create_non_peer_file() {
        echo "random stuff" > $PEER_DIR/not_a_peer_file
}

create_malformed_peer_file() {
        echo "more random stuff" > $PEER_DIR/$(uuidgen)
}

# We create lots of files, in batches, to ensure that our bogus ones are
# properly interspersed with the valid ones.

TEST create_random_peer_files
TEST create_non_peer_file
TEST create_random_peer_files
TEST create_malformed_peer_file
TEST create_random_peer_files

# There should be 30 peers, not counting the two bogus files.
TEST glusterd
N_PEERS=$($CLI peer status | grep ^Uuid: | wc -l)
TEST [ "$N_PEERS" = "30" ]

# For extra credit, check the logs for messages about bogus files.

cleanup



