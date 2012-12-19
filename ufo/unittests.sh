#!/bin/bash

cd $(dirname $0)/test/unit
nosetests --exe --with-coverage --cover-package gluster --cover-erase $@
saved_status=$?
rm -f .coverage
exit $saved_status
