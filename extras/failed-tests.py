#!/usr/bin/python

import blessings
import HTMLParser
import requests
import sys

BASE='https://build.gluster.org'
TERM=blessings.Terminal()

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
        self.process_failure(self.last_href)
        self.last_href = None
    def process_failure (self, url):
        text = requests.get(BASE+url+'Full',verify=False).text
        accum = []
        for t in text.split('\n'):
            if t == 'Result: FAIL':
                print TERM.red + ('FAILURE on %s' % BASE+url) + TERM.normal
                for t2 in accum:
					print t2.encode('utf-8')
                accum = []
            elif t == 'Result: PASS':
                accum = []
            else:
                accum.append(t)

def main (url):
    parser = FailureFinder()
    text = requests.get(url,verify=False).text
    parser.feed(text)


if len(sys.argv) < 2:
    main(BASE+'/job/rackspace-regression-2GB-triggered/')
else:
    for u in sys.argv[1:]:
        main(BASE+u)
