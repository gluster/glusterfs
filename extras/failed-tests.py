#!/usr/bin/python

import blessings
import HTMLParser
import requests
from requests.packages.urllib3.exceptions import InsecureRequestWarning
import sys
import re
import argparse
from collections import defaultdict
from datetime import date, timedelta, datetime
from dateutil.parser import parse

# This tool goes though the Gluster regression links and checks for failures

BASE='https://build.gluster.org'
TERM=blessings.Terminal()
MAX_BUILDS=1000
summary=defaultdict(list)
VERBOSE=None
total_builds=0
failed_builds=0

def process_failure (url, cut_off_date):
    global failed_builds
    text = requests.get(url,verify=False).text
    accum = []
    for t in text.split('\n'):
        if t.find("BUILD_TIMESTAMP=") != -1 and cut_off_date != None:
            build_date = parse (t, fuzzy=True)
            if build_date.date() < cut_off_date:
                return 1
        elif t.find("Result: FAIL") != -1:
            failed_builds=failed_builds+1
            if VERBOSE == True: print TERM.red + ('FAILURE on %s' % BASE+url) + TERM.normal
            for t2 in accum:
                if VERBOSE == True: print t2.encode('utf-8')
                if t2.find("Wstat") != -1:
                     test_case = re.search('\./tests/.*\.t',t2)
                     if test_case:
                          summary[test_case.group()].append(url)
            accum = []
        elif t.find("Result: PASS") != -1:
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

def print_summary_html():
    print "<p><b>%d</b> of <b>%d</b> regressions failed</p>" % (failed_builds, total_builds)
    for k,v in summary.iteritems():
        if k == 'core':
            print "<p><font color='red'><b> Found cores :</b></font></p>"
            for cmp,lnk in zip(v[::2], v[1::2]):
                print "<p>&emsp;Component: %s</p>" % (cmp)
                print "<p>&emsp;Regression Link: %s</p>" % (lnk)
        else:
            print "<p><font color='red'><b> %s ;</b> Failed <b>%d</b> times</font></p>" % (k, len(v))
            for lnk in v:
                print "<p>&emsp;Regression Link: <a href=\"%s\">%s</a></p>" % (lnk, lnk)

def print_summary():
    print "%d of %d regressions failed" % (failed_builds, total_builds)
    for k,v in summary.iteritems():
        if k == 'core':
            print TERM.red + "Found cores:" + TERM.normal
            for cmp,lnk in zip(v[::2], v[1::2]):
                print "\tComponent: %s" % (cmp)
                print "\tRegression Link: %s" % (lnk)
        else:
            print TERM.red + "%s ; Failed %d times" % (k, len(v)) + TERM.normal
            for lnk in v:
                print "\tRegression Link: %s" % (lnk)

def get_summary (build_id, cut_off_date, reg_link):
    global total_builds
    for i in xrange(build_id, build_id-MAX_BUILDS, -1):
        url=BASE+reg_link+str(i)+"/consoleFull"
        ret = process_failure(url, cut_off_date)
        if ret == 1:
            total_builds = build_id - i
            return

if __name__ == '__main__':
    requests.packages.urllib3.disable_warnings(InsecureRequestWarning)
    parser = argparse.ArgumentParser()
    parser.add_argument("get-summary")
    parser.add_argument("last_no_of_days", default=1, type=int, help="Regression summary of last number of days")
    parser.add_argument("regression_link", default="centos", nargs='+', help="\"centos\" | \"netbsd\" | any other regression link")
    parser.add_argument("--verbose", default="false", action="store_true", help="Print a detailed report of each test case that is failed")
    parser.add_argument("--html_report", default="false", action="store_true", help="Print a brief report of failed regressions in html format")
    args = parser.parse_args()
    num_days=args.last_no_of_days
    cut_off_date=date.today() - timedelta(days=num_days)
    VERBOSE = args.verbose
    for reg in args.regression_link:
        if reg == 'centos':
            reg_link = '/job/rackspace-regression-2GB-triggered/'
        elif reg == 'netbsd':
            reg_link = '/job/rackspace-netbsd7-regression-triggered/'
        else:
            reg_link = reg
        build_id = int(requests.get(BASE+reg_link+"lastBuild/buildNumber", verify=False).text)
        get_summary(build_id, cut_off_date, reg_link)
    if args.html_report == True:
        print_summary_html()
    else:
        print_summary()
