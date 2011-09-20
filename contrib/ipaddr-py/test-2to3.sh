#!/bin/sh

# Converts the python2 ipaddr files to python3 and runs the unit tests
# with both python versions.

mkdir -p 2to3output && \
cp -f *.py 2to3output && \
( cd 2to3output && 2to3 . | patch -p0 ) && \
py3version=$(python3 --version 2>&1) && \
echo -e "\nTesting with ${py3version}" && \
python3 2to3output/ipaddr_test.py && \
rm -r 2to3output && \
pyversion=$(python --version 2>&1) && \
echo -e "\nTesting with ${pyversion}" && \
./ipaddr_test.py
