#!/usr/bin/python2

from __future__ import print_function
import blessings
import requests
from requests.packages.urllib3.exceptions import InsecureRequestWarning
import re
import argparse
from collections import defaultdict
from datetime import timedelta, datetime
from pystache import render

# This tool goes though the Gluster regression links and checks for failures

BASE = 'https://build.gluster.org'
TERM = blessings.Terminal()
MAX_BUILDS = 1000
summary = defaultdict(list)
VERBOSE = None


def process_failure(url, node):
    text = requests.get(url, verify=False).text
    accum = []
    for t in text.split('\n'):
        if t.find("Result: FAIL") != -1:
            for t2 in accum:
                if VERBOSE:
                    print(t2.encode('utf-8'))
                if t2.find("Wstat") != -1:
                    test_case = re.search('\./tests/.*\.t', t2)
                    if test_case:
                        summary[test_case.group()].append((url, node))
            accum = []
        elif t.find("cur_cores=/") != -1:
            summary["core"].append([t.split("/")[1]])
            summary["core"].append(url)
        else:
            accum.append(t)


def print_summary(failed_builds, total_builds, html=False):
    # All the templates
    count = [
            '{{failed}} of {{total}} regressions failed',
            '<p><b>{{failed}}</b> of <b>{{total}}</b> regressions failed</p>'
    ]
    regression_link = [
            '\tRegression Link: {{link}}\n'
            '\tNode: {{node}}',
            '<p>&emsp;Regression Link: {{link}}</p>'
            '<p>&emsp;Node: {{node}}</p>'
    ]
    component = [
            '\tComponent: {{comp}}',
            '<p>&emsp;Component: {{comp}}</p>'
    ]
    failure_count = [
            ''.join([
                TERM.red,
                '{{test}} ; Failed {{count}} times',
                TERM.normal
            ]),
            (
                '<p><font color="red"><b>{{test}};</b> Failed <b>{{count}}'
                '</b> times</font></p>'
            )
    ]

    template = 0
    if html:
        template = 1
    print(render(
            count[template],
            {'failed': failed_builds, 'total': total_builds}
    ))
    for k, v in summary.iteritems():
        if k == 'core':
            print(''.join([TERM.red, "Found cores:", TERM.normal]))
            for comp, link in zip(v[::2], v[1::2]):
                print(render(component[template], {'comp': comp}))
                print(render(
                        regression_link[template],
                        {'link': link[0], 'node': link[1]}
                ))
        else:
            print(render(failure_count[template], {'test': k, 'count': len(v)}))
            for link in v:
                print(render(
                        regression_link[template],
                        {'link': link[0], 'node': link[1]}
                ))


def get_summary(cut_off_date, reg_link):
    '''
    Get links to the failed jobs
    '''
    success_count = 0
    failure_count = 0
    for page in xrange(0, MAX_BUILDS, 100):
        build_info = requests.get(''.join([
                BASE,
                reg_link,
                'api/json?depth=1&tree=allBuilds'
                '[url,result,timestamp,builtOn]',
                '{{{0},{1}}}'.format(page, page+100)
        ]), verify=False).json()
        for build in build_info.get('allBuilds'):
            if datetime.fromtimestamp(build['timestamp']/1000) < cut_off_date:
                # stop when timestamp older than cut off date
                return failure_count, failure_count + success_count
            if build['result'] in [None, 'SUCCESS']:
                # pass when build is a success or ongoing
                success_count += 1
                continue
            if VERBOSE:
                print(''.join([
                    TERM.red,
                    'FAILURE on {0}'.format(build['url']),
                    TERM.normal
                ]))
            url = ''.join([build['url'], 'consoleText'])
            failure_count += 1
            process_failure(url, build['builtOn'])
    return failure_count, failure_count + success_count


def main(num_days, regression_link, html_report):
    cut_off_date = datetime.today() - timedelta(days=num_days)
    failure = 0
    total = 0
    for reg in regression_link:
        if reg == 'centos':
            reg_link = '/job/centos6-regression/'
        elif reg == 'netbsd':
            reg_link = '/job/netbsd7-regression/'
        else:
            reg_link = reg
        counts = get_summary(cut_off_date, reg_link)
        failure += counts[0]
        total += counts[1]
    print_summary(failure, total, html_report)


if __name__ == '__main__':
    requests.packages.urllib3.disable_warnings(InsecureRequestWarning)
    parser = argparse.ArgumentParser()
    parser.add_argument("get-summary")
    parser.add_argument(
            "last_no_of_days",
            default=1,
            type=int,
            help="Regression summary of last number of days"
    )
    parser.add_argument(
            "regression_link",
            default="centos",
            nargs='+',
            help="\"centos\" | \"netbsd\" | any other regression link"
    )
    parser.add_argument(
            "--verbose",
            default=False,
            action="store_true",
            help="Print a detailed report of each test case that is failed"
    )
    parser.add_argument(
            "--html-report",
            default=False,
            action="store_true",
            help="Print a brief report of failed regressions in html format"
    )
    args = parser.parse_args()
    VERBOSE = args.verbose
    main(
        num_days=args.last_no_of_days,
        regression_link=args.regression_link,
        html_report=args.html_report
    )
