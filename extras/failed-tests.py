#!/usr/bin/python

import blessings
import HTMLParser
import requests
import sys
from collections import defaultdict
from datetime import date, timedelta, datetime
from dateutil.parser import parse

## This tool goes though the Gluster regression links and checks for failures
#
# Usage: failed-tests.py [<regression links,..> | get-summary \
#        <last number of days> <regression link>]
#
# When no options specified, goes through centos regression
# @build.gluster.org/job/rackspace-regression-2GB-triggered/ and gets the
# summary of last 30 builds
# When other regression links (Eg:/job/rackspace-netbsd7-regression-triggered/)
# are specified it goes through those links and prints the summary of last 30
# builds in those links
# When get-summary is specified, it goes through the link specified and gets the
# summary of the builds that have happened in the last number of days specified.

BASE='https://build.gluster.org'
TERM=blessings.Terminal()
MAX_BUILDS=100
summary=defaultdict(list)

def process_failure (url, cut_off_date):
    text = requests.get(url,verify=False).text
    accum = []
    for t in text.split('\n'):
        if t.find("BUILD_TIMESTAMP=") != -1 and cut_off_date != None:
            build_date = parse (t, fuzzy=True)
            if build_date.date() < cut_off_date:
                return 1
        elif t == 'Result: FAIL':
            print TERM.red + ('FAILURE on %s' % BASE+url) + TERM.normal
            for t2 in accum:
                print t2.encode('utf-8')
                if t2.find("Wstat") != -1:
                     summary[t2.split(" ")[0]].append(url)
            accum = []
        elif t == 'Result: PASS':
            accum = []
        elif t.find("cur_cores=/") != -1:
            summary["core"].append([t.split("/")[1]])
            summary["core"].append(url)
        else:
            accum.append(t)
    return 0

class FailureFinder (HTMLParser.HTMLParser):
    def __init__ (*args):
        apply(HTMLParser.HTMLParser.__init__,args)
        self = args[0]
        self.last_href = None
    def handle_starttag (self, tag, attrs):
        if tag == 'a':
            return self.is_a_tag (attrs)
        if tag == 'img':
            return self.is_img_tag (attrs)
    def is_a_tag (self, attrs):
        attrs_dict = dict(attrs)
        try:
            if attrs_dict['class'] != 'build-status-link':
                return
        except KeyError:
            return
        self.last_href = attrs_dict['href']
    def is_img_tag (self, attrs):
        if self.last_href == None:
            return
        attrs_dict = dict(attrs)
        try:
            if attrs_dict['alt'].find('Failed') == -1:
                return
        except KeyError:
            return
        process_failure(BASE+self.last_href, None)
        self.last_href = None

def main (url):
    parser = FailureFinder()
    text = requests.get(url,verify=False).text
    parser.feed(text)

def print_summary():
    for k,v in summary.iteritems():
        if k == 'core':
            print TERM.red + "Found cores:" + TERM.normal
            for cmp,lnk in zip(v[::2], v[1::2]):
                print "\tComponent: %s" % (cmp)
                print "\tRegression Link: %s" % (lnk)
        else:
            print TERM.red + "%s ; Failed %d times" % (k, len(v)) + TERM.normal
            for lnk in v:
                print "\tRegression Links: %s" % (lnk)

def get_summary (build_id, cut_off_date, reg_link):
    for i in xrange(build_id, build_id-MAX_BUILDS, -1):
        url=BASE+reg_link+str(i)+"/consoleFull"
        ret = process_failure(url, cut_off_date)
        if ret == 1:
            return

if __name__ == '__main__':
    if len(sys.argv) < 2:
        main(BASE+'/job/rackspace-regression-2GB-triggered/')
    elif sys.argv[1].find("get-summary") != -1:
        if len(sys.argv) < 4:
            print "Usage: failed-tests.py get-summary <last_no_of_days> <regression_link>"
            sys.exit(0)
        num_days=int(sys.argv[2])
        cut_off_date=date.today() - timedelta(days=num_days)
        reg_link = sys.argv[3]
        build_id = int(requests.get(BASE+reg_link+"lastBuild/buildNumber", verify=False).text)
        get_summary(build_id, cut_off_date, reg_link)
    else:
        for u in sys.argv[1:]:
            main(BASE+u)
    print_summary()
