#!/bin/bash

cd $(dirname $0)/tests/unit
nosetests -v --exe --with-coverage --cover-package \
          syncdaemon --cover-erase --cover-html --cover-branches $@

saved_status=$?
rm -f .coverage
exit $saved_status
