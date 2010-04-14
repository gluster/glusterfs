import os, sys, re, string

def check_duplicate_entry(args):
    """Check duplicate entries in incoming arguments"""
    _tmp = []
    for server in args:
        if server not in _tmp:
            _tmp.append (server)
        else:
            print "Duplicate arguments detected (%s)" % server
            raise ValueError

    return

def args2dict(args):

    keyvalue = {}
    for arg in args:
        if int(arg.find(':')) == -1:
            continue
        first = arg.split(':')[0]
        keyvalue[first] = []

    for arg in args:
        if int(arg.find(':')) == -1:
            continue
        first = arg.split(':')[0]
        if arg.split(':')[1] not in keyvalue[first]:
            if arg.split(':')[1][0] != '/':
                print "Absolute export path required for %s" % arg
                raise ValueError
            keyvalue[first].append (arg.split(':')[1])

    return keyvalue

def args2array(args):

    array = []

    for arg in args:
        if int(arg.find(':')) == -1:
            continue
        array.append(arg)

    return array

def list_export_vols(configdir, volumename):

    list_export = []
    if os.path.isdir(configdir):
        for line in os.listdir(configdir):
            if re.match(r'[a-zA-Z0-9_]\S+%s-export.vol' % volumename, line):
                list_export.append(line)

    return list_export

def get_old_server_args(exports, configdir):

    list_args = []
    for export in exports:
        array = gfParser("%s/%s" % (configdir, export))
        for dt in array:
            if dt.has_key('option'):
                if re.match("\w+tory", dt['option']):
                    list_args.append(export.split('-')[0] + ":" + dt['option'].split()[1])

    return list_args

def gfParser (volfile):

    volfile_rl = open (volfile).readlines()
    volume_array = []
    for line in volfile_rl:
        line = line.strip()
        volfile_dict = {}
        if re.match(r"[a-zA-Z0-9_]+", line):
            volfile_dict[line.split()[0]] = string.join (line.split()[1:], ' ') if line.split() > 1 else " "
            volume_array.append(volfile_dict)

    return volume_array
