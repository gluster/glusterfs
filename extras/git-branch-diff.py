#!/bin/python2

"""
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
"""

"""
  ABOUT:
  This script helps in visualizing backported and missed commits between two
  different branches, tags or commit ranges. In the list of missed commits,
  it will help you identify patches which are posted for reviews on gerrit server.

  USAGE:
    $ ./extras/git-branch-diff.py --help
    usage: git-branch-diff.py [-h] [-s SOURCE] -t TARGET [-a AUTHOR] [-p PATH]
                              [-o OPTIONS]

    git wrapper to diff local or remote branches/tags/commit-ranges

    optional arguments:
      -h, --help            show this help message and exit
      -s SOURCE, --source SOURCE
                            source pattern, it could be a branch, tag or a commit
                            range
      -t TARGET, --target TARGET
                            target pattern, it could be a branch, tag or a commit
                            range
      -a AUTHOR, --author AUTHOR
                            default: git config name/email, to provide multiple
                            specify comma separated values
      -p PATH, --path PATH  show source and target diff w.r.t given path, to
                            provide multiple specify space in between them
      -o OPTIONS, --options OPTIONS
                            add other git options such as --after=<>, --before=<>
                            etc. experts use;

  SAMPLE EXECUTIONS:
  $ ./extras/git-branch-diff.py -t origin/release-3.8

  $ ./extras/git-branch-diff.py -s local_branch -t origin/release-3.7

  $ ./extras/git-branch-diff.py -s 4517bf8..e66add8 -t origin/release-3.7
  $ ./extras/git-branch-diff.py -s HEAD..c4efd39 -t origin/release-3.7

  $ ./extras/git-branch-diff.py -t v3.7.11 --author="author@redhat.com"
  $ ./extras/git-branch-diff.py -t v3.7.11 --author="authorX, authorY, authorZ"

  $ ./extras/git-branch-diff.py -t origin/release-3.8 --path="xlators/"
  $ ./extras/git-branch-diff.py -t origin/release-3.8 --path="./xlators ./rpc"

  $ ./extras/git-branch-diff.py -t origin/release-3.6 --author="*"
  $ ./extras/git-branch-diff.py -t origin/release-3.6 --author="All"
  $ ./extras/git-branch-diff.py -t origin/release-3.6 --author="Null"

  $ ./extras/git-branch-diff.py -t v3.7.11 --options="--after=2015-03-01 \
                                                      --before=2016-01-30"

  DECLARATION:
  While backporting commit to another branch only subject of the patch may
  remain unchanged, all others such as commit message,  commit Id, change Id,
  bug Id, may be changed. This script works by taking commit subject as the
  key value for comparing two git branches, which can be local or remote.

  Note: This script may ignore commits which have altered their commit subjects
  while backporting patches. Also this script doesn't have any intelligence to
  detect squashed commits.

  AUTHOR:
  Prasanna Kumar Kalever <prasanna.kalever@redhat.com>
"""

from __future__ import print_function
import os
import sys
import argparse
import commands
import subprocess
import requests

class GitBranchDiff:
    def __init__ (self):
        " color symbols"
        self.tick  = u'\033[1;32m[ \u2714 ]\033[0m'
        self.cross = u'\033[1;31m[ \u2716 ]\033[0m'
        self.green_set = u'\033[1;34m'
        self.yello_set = u'\033[4;33m'
        self.color_unset = '\033[0m'

        self.parse_cmd_args()

        " replace default values with actual values from command args"
        self.g_author = self.argsdict['author']
        self.s_pattern  = self.argsdict['source']
        self.t_pattern  = self.argsdict['target']
        self.r_path     = self.argsdict['path']
        self.options    = ' '.join(self.argsdict['options'])

        self.gerrit_server = "http://review.gluster.org"

    def check_dir_exist (self, os_path):
        " checks whether given path exist"
        path_list = os_path.split()
        for path in path_list:
            if not os.path.exists(path):
                raise argparse.ArgumentTypeError("'%s' path %s is not valid"
                                                 %(os_path, path))
        return os_path

    def check_pattern_exist (self):
        " defend to check given branch[s] exit"
        status_sbr, op = commands.getstatusoutput('git log ' +
                                                  self.s_pattern)
        status_tbr, op = commands.getstatusoutput('git log ' +
                                                  self.t_pattern)
        if status_sbr != 0:
            print("Error: --source=" + self.s_pattern + " doesn't exit\n")
            self.parser.print_help()
            exit(status_sbr)
        elif status_tbr != 0:
            print("Error: --target=" + self.t_pattern + " doesn't exit\n")
            self.parser.print_help()
            exit(status_tbr)

    def check_author_exist (self):
        " defend to check given author exist, format incase of multiple"
        contrib_list = ['', '*', 'all', 'All', 'ALL', 'null', 'Null', 'NULL']
        if self.g_author in contrib_list:
            self.g_author = ""
        else:
            ide_list = self.g_author.split(',')
            for ide in ide_list:
                cmd4 = 'git log ' + self.s_pattern + ' --author=' + ide
                c_list = subprocess.check_output(cmd4, shell = True)
                if len(c_list) is 0:
                    print("Error: --author=%s doesn't exit" %self.g_author)
                    print("see '%s --help'" %__file__)
                    exit(1)
            if len(ide_list) > 1:
                self.g_author = "\|".join(ide_list)

    def connected_to_gerrit (self):
        "check if gerrit server is reachable"
        try:
            r = requests.get(self.gerrit_server, timeout=3)
            return True
        except requests.Timeout as err:
            " request timed out"
            print("Warning: failed to get list of open review commits on " \
                            "gerrit.\n" \
                  "hint: Request timed out! gerrit server could possibly " \
                  "slow ...\n")
            return False
        except requests.RequestException as err:
            " handle other errors"
            print("Warning: failed to get list of open review commits on " \
                            "gerrit\n" \
                  "hint: check with internet connection ...\n")
            return False

    def parse_cmd_args (self):
        " command line parser"
        author = subprocess.check_output('git config user.email',
                                                  shell = True).rstrip('\n')
        source = "remotes/origin/master"
        options  = [' --pretty=format:"%h %s" ']
        path = subprocess.check_output('git rev-parse --show-toplevel',
                                            shell = True).rstrip('\n')
        self.parser = argparse.ArgumentParser(description = 'git wrapper to '
                                              'diff local or remote branches/'
                                              'tags/commit-ranges')
        self.parser.add_argument('-s',
                                 '--source',
                                 help = 'source pattern, it could be a branch,'
                                        ' tag or a commit range',
                                 default = source,
                                 dest = 'source')
        self.parser.add_argument('-t',
                                 '--target',
                                 help = 'target pattern, it could be a branch,'
                                        ' tag or a commit range',
                                 required = True,
                                 dest = 'target')
        self.parser.add_argument('-a',
                                 '--author',
                                 help = 'default: git config name/email, '
                                        'to provide multiple specify comma'
                                        ' seperated values',
                                 default = author,
                                 dest = 'author')
        self.parser.add_argument('-p',
                                 '--path',
                                 type = self.check_dir_exist,
                                 help = 'show source and target diff w.r.t '
                                        'given path, to provide multiple '
                                        'specify space in between them',
                                 default = path,
                                 dest = 'path')
        self.parser.add_argument('-o',
                                 '--options',
                                 help = 'add other git options such as '
                                        '--after=<>, --before=<> etc. '
                                        'experts use;',
                                 default = options,
                                 dest = 'options',
                                 action='append')
        self.argsdict = vars(self.parser.parse_args())

    def print_output (self):
        " display the result list"
        print("\n------------------------------------------------------------\n")
        print(self.tick + " Successfully Backported changes:")
        print('      {' + 'from: ' + self.s_pattern + \
              '  to: '+ self.t_pattern + '}\n')
        for key, value in self.s_dict.iteritems():
            if value in self.t_dict.itervalues():
                print("[%s%s%s] %s" %(self.yello_set,
                                      key,
                                      self.color_unset,
                                      value))
        print("\n------------------------------------------------------------\n")
        print(self.cross + " Missing patches in " + self.t_pattern + ':\n')
        if self.connected_to_gerrit():
            cmd3 = "git review -r origin -l"
            review_list = subprocess.check_output(cmd3, shell = True).split('\n')
        else:
            review_list = []

        for key, value in self.s_dict.iteritems():
            if value not in self.t_dict.itervalues():
                if any(value in s for s in review_list):
                    print("[%s%s%s] %s %s(under review)%s" %(self.yello_set,
                                                            key,
                                                            self.color_unset,
                                                            value,
                                                            self.green_set,
                                                            self.color_unset))
                else:
                    print("[%s%s%s] %s" %(self.yello_set,
                                          key,
                                          self.color_unset,
                                          value))
        print("\n------------------------------------------------------------\n")

    def main (self):
        self.check_pattern_exist()
        self.check_author_exist()

        " actual git commands"
        cmd1 = 'git log' + self.options + ' ' + self.s_pattern + \
               ' --author=\'' + self.g_author + '\' ' + self.r_path

        " could be backported by anybody so --author doesn't apply here"
        cmd2 = 'git log' + self.options + ' ' + self.t_pattern + \
               ' ' + self.r_path

        s_list = subprocess.check_output(cmd1, shell = True).split('\n')
        t_list = subprocess.check_output(cmd2, shell = True)

        if len(t_list) is 0:
            print("No commits in the target: %s" %self.t_pattern)
            print("see '%s --help'" %__file__)
            exit()
        else:
            t_list = t_list.split('\n')

        self.s_dict = dict()
        self.t_dict = dict()

        for item in s_list:
            self.s_dict.update(dict([item.split(' ', 1)]))
        for item in t_list:
            self.t_dict.update(dict([item.split(' ', 1)]))

        self.print_output()


if __name__ == '__main__':
    run = GitBranchDiff()
    run.main()
