#!/usr/bin/env python

import argparse
import requests
import json
import subprocess
import re


def add_to_committable(br_commit_hash, br_commit_message, br_commit_chid,
                       br_commit_grlink, m_commit_hash, m_commit_message,
                       m_commit_grlink, status, notes):
    commit_table.append([br_commit_hash, br_commit_message, br_commit_chid,
                         br_commit_grlink, m_commit_hash, m_commit_message,
                         m_commit_grlink, status, notes]
                        )
    return


def updatemaster_in_committable(commit_hash, m_commit_hash,
                                m_commit_message, m_commit_grlink,
                                status, row=None):
    if (row is not None):
        row[4] = m_commit_hash
        row[5] = m_commit_message
        row[6] = m_commit_grlink
        row[7] = status
    else:
        print "TODO: Implement finding row using commit hash"
    return


def print_commit_table(format):
    if format is None:
        for row in commit_table:
            print row
    elif format == "markdown":
        print ("FB Branch commit hash | FB Branch commit message |"
               " FB Branch Change ID | FB Branch Gerrit link | "
               "Master branch commit hash | Master branch commit message |"
               " Master branch gerrit link | Status | Notes")
        print "--- | --- | --- | --- | --- | --- | --- | --- | ---"
        for row in commit_table:
            print (str(row[0])[:10] + "|" +
                   str(row[1]) + "|" +
                   str(row[2]) + "|" +
                   str(row[3]) + "|" +
                   str(row[4])[:10] + "|" +
                   str(row[5]) + "|" +
                   str(row[6]) + "|" +
                   str(row[7]) + "|" +
                   str(row[8]))
    else:
        print "Unknown print format"
    return


def find_in_gerrit(branch, change_id):
    k = requests.get('https://review.gluster.org/changes/glusterfs~{}~{}'.
                     format(
                            branch,
                            change_id
                            )
                     )
    output = k.text
    cleaned_output = '\n'.join(output.split('\n')[1:])
    try:
        parsed_output = json.loads(cleaned_output)
    except:
        return None, None
    change_number = parsed_output.get("_number")
    if change_number is not None:
        return change_number, parsed_output.get("status")
    else:
        return None, None


def get_commits(start, end):
    crange = start + '..' + end
    commit = subprocess.check_output(
        ['git',
         'log',
         '--pretty=oneline',
         crange]
    )
    for line in commit.splitlines():
        commit_hash = line.split(' ')[0]
        startpoint = len(commit_hash) + 1
        commit_message = line[startpoint:]
        commit_chid, commit_grlink = get_gerrit_details(commit_hash)
        # There are backports/merges frmo 3.8 to the fb branch, which are
        # false positives, so check and ignore such commits
        cnum, status = find_in_gerrit("release-3.8-fb", commit_chid)
        if cnum is not None and status != "ABANDONED":
            add_to_committable(commit_hash, commit_message,
                               commit_chid, commit_grlink,
                               None, None,
                               None, None,
                               None)
    return


def update_status_from_master():
    for row in commit_table:
        m_commit_hash = None
        m_commit_message = None
        m_commit_grlink = None
        commit_hash, message, commit_chid = row[:3]
        cmd = ("git log origin/master --pretty=oneline "
               + "--grep=\"^Change-Id: "
               + str(commit_chid) + "\"")
        p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE)
        out, err = p.communicate()
        if out is not None and out != "":
            m_commit_hash = out.split(' ')[0]
            startpoint = len(m_commit_hash) + 1
            m_commit_message = out[startpoint:].rstrip()
            discard, m_commit_grlink = get_gerrit_details(m_commit_hash)
            updatemaster_in_committable(commit_hash, m_commit_hash,
                                        m_commit_message, m_commit_grlink,
                                        "5:MERGED", row)
    return


def get_gerrit_details(commit_hash):
    # change-id: <hex>
    regex1 = re.compile(r'(^change-id:\s)(\S*)', re.IGNORECASE)
    # reviewed-on: <link>
    regex2 = re.compile(r'(^reviewed-on:\s)(\S*)', re.IGNORECASE)
    cmessage = subprocess.check_output(
        ['git',
         'show',
         '--no-patch',
         '--format=%B',
         str(commit_hash)]
    )
    for line in cmessage.split('\n'):
        matches = re.search(regex1, line)
        if matches:
            cchangeid = matches.group(2)
            cgrlink = None
            for line in cmessage.split('\n'):
                matches = re.search(regex2, line)
                if matches:
                    cgrlink = matches.group(2)
                    break
            return cchangeid, cgrlink
    return None, None


def update_status_from_open_reviews():
    for row in commit_table:
        (br_commit_hash, br_commit_message, br_commit_chid,
         br_commit_grlink, m_commit_hash, m_commit_message,
         m_commit_grlink, status) = row[:8]
        if status is None:
            m_change_number, gstatus = find_in_gerrit("master", br_commit_chid)
            if m_change_number is not None:
                row[6] = "https://review.gluster.org/" + str(m_change_number)
                if gstatus != "MERGED":
                    row[7] = "1:UNDERREVIEW"
                else:
                    row[7] = "5:MERGED"
    return


def update_status_from_exception_table():
    # <TODO>
    # Intention is the code here would have an exception table based on
    # comments from the maintainers. This table will list,
    # fb-Change-ID, master-Change_ID, status, notes
    # for example, if a change is not needed due to reasons, then a record
    # will show,
    # [<fb-Change-ID>, <None>, "SKIP", <notes>]
    # Another example could be, a change is ported as part of a bigger change,
    # will show,
    # [<fb-Change-ID>, <master-Change_ID>, <None>, <notes>]
    # NOTE: in the above example we can derive state, based on if the review
    # against master is merged or not.
    #
    # This exception table is generated using posted markdown contents from
    # this script, and inviting people to update the MD (say in hackMD), and
    # weekly import content from that MD into the table in the script. Hence,
    # generating a new MD and posting that up for further work.
    for row in commit_table:
        for erow in exception_table:
            if erow[0] == row[0][:10]:
                row[7] = "4:SKIP"
    return


def update_status_final():
    # TODO
    # run through the list and update missing status as TOBEPORTED
    for row in commit_table:
        if row[7] is None:
                row[7] = "2:TOBEPORTED"
    return


def main(workdir):
    # Get commits from stated branch and range
    get_commits(rel38fb_start_commit, "origin/release-3.8-fb")

    # Update table with MERGED status from master branch
    update_status_from_master()

    # Update table with UNDERREVIEW from gerrit
    update_status_from_open_reviews()

    # Update table with exceptions captured from community
    update_status_from_exception_table()

    # Update table with default TOBEPORTED for all rows missing a status
    update_status_final()

    # Print the table out in MD format
    print_commit_table("markdown")

    return


# TODO The option below is not supported
parser = argparse.ArgumentParser(
    description='FB Forward port status generator')
parser.add_argument(
    '--workdir', '-w',
    action='store_const',
    const="/tmp/",
    help="Provide a working directory for the generator, to clone required"
         " github repositories and generate the output.",
)
args = parser.parse_args()

rel38fb_start_commit = "d1ac991503b0153b12406d16ce99cd22dadfe0f7"

# This is a list of lists, with each sublist having the following fields,
# [3.8-fbcmhash, 3.8-fbmessage, 3.8-fbchID, 3.8-fbgrlink, maschhash,
# masmessage, masgrlink, status, notes]
#
# 3.8-fbcmhash - git commit hash from release-3.8-fb branch
# 3.8-fbmessage - commit message in the release-3.8-fb branch
# 3.8-fbchID - Gerrit Change-ID in the release-3.8-fb branch
# 3.8-fbgrlink - Gerrit review link in the release-3.8-fb branch
# maschhash, masmessage, masgrlink - Same as above on master branch
# status - 5:MERGED/1:UNDERREVIEW/3:EXCEPTION/2:TOBEPORTED/4:SKIP
# notes - typically for exceptions
commit_table = []

exception_table = [
    ["69509ee7d2"],
    ["035a9b742d"],
    ["6455c52a33"],
    ["07b32d43b0"],
    ["627611998b"],
    ["0f0d00e8a5"],
    ["49d0f911bd"],
    ["9aca3f636b"],
    ["60c6b1729b"],
    ["493746d10f"],
    ["9f9da37e3a"],
    ["233156d6fc"],
    ["bc02e5423d"],
    ["9d240c8bff"],
    ["35cfc2853a"],
    ["13317ddf8a"],
    ["c48979df06"],
    ["11afb5954e"],
    ["401d1ee7e3"],
    ["e537c79909"],
    ["4625432603"],
    ["5823eec46f"],
    ["c1a1472168"],
    ["5f6586ca9c"],
    ["8c50512d12"],
    ["9255f94bc2"],
    ["2f34312030"],
    ["35cfc2853a"],
    ["13317ddf8a"],
    ["c48979df06"],
    ["4625432603"]
]

main(args.workdir)
