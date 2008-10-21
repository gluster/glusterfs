#!/bin/sh

# This script needs to be present on glusterfs mount, (ie, on every node which wants to run benchmark)

ifilename="/dev/zero"
ofilename="testdir/testfile.$(hostname)"
result="output/output.$(hostname)"
blocksize=128k
count=8

mkdir -p testdir;
mkdir -p output;
echo > ${result}
while [ ! -e start-test ]; do
    sleep 1;
done;


for i in $(seq 1 5); do 
    # write
    dd if=${ifilename} of=${ofilename} bs=${blocksize} count=${count} 2>&1 | tail -n 1 | cut -f 8,9 -d ' ' >> ${result} ;
    # read
    #dd if=${ofilename} of=/dev/null bs=${blocksize} count=${count} 2>&1 | tail -n 1 | cut -f 8,9 -d ' ' >> ${result} ;
done

rm -f start-test
