#!/usr/bin/env python2

import os
import sys
from optparse import OptionParser

import xattr

def handle_textencoding(attr):
    ### required for Python's handling of NULL strings.
    attr_null_replace = (attr.encode('hex').decode('hex')).replace('\x00',
                                                                   '\\000')
    return attr_null_replace

def getfattr(path, option):
    attr = xattr.getxattr(path, option.name)
    encoded_attr = attr

    if option.encoding == "text":
        ## special case handle it.
        encoded_attr = handle_textencoding(attr)
    else:
        encoded_attr = attr.encode(option.encoding)

    if option.onlyvalues:
        print (encoded_attr)
        return

    print_getfattr (path, option, encoded_attr)
    return

def print_getfattr (path, option, encoded_attr=None):
    if encoded_attr:
        if option.encoding == "hex":
            print ("%s=0x%s" % (option.name, encoded_attr))
        elif option.encoding == "base64":
            print ("%s=0s%s" % (option.name, encoded_attr))
        else:
            print ("%s=\"%s\"" % (option.name, encoded_attr))
    else:
        print option.name

    return

def print_header (path, absnames):
    if absnames:
        print ("# file: %s" % path)
    else:
        print ("getfattr: Removing leading '/' from absolute path names")
        print ("# file: %s" % path[1:])

if __name__ == '__main__':
    usage = "usage: %prog [-n name|-d] [-e en] [-m pattern] path...."
    parser = OptionParser(usage=usage)
    parser.add_option("-n", action="store", dest="name", type="string",
                      help="Dump the value of the named extended attribute"
                      " extended attribute.")
    parser.add_option("-d", action="store_true", dest="dump",
                      help="Dump the values of all extended attributes"
                      " associated with pathname.")
    parser.add_option("-e", action="store", dest="encoding", type="string",
                      default="base64",
                      help="Encode values after retrieving"
                      " them. Valid values of [en] are `text`, `hex`,"
                      " and `base64`. Values encoded as text strings are"
                      " enclosed in double quotes (\"), while strings"
                      " encoded as hexidecimal and base64 are prefixed with"
                      " 0x and 0s, respectively.")
    parser.add_option("-m", action="store", dest="pattern", type="string",
                      help="Only include attributes with names matching the"
                      " regular expression pattern. The default value for"
                      " pattern is \"^user\\.\", which includes all the"
                      " attributes in the user namespace. Specify \"-\" for"
                      " including all attributes. Refer to attr(5) for a more"
                      " detailed discussion of namespaces.")
    parser.add_option("--absolute-names", action="store_true", dest="absnames",
                      help="Do not strip leading slash characters ('/')."
                      " The default behaviour is to strip leading slash characters.")
    parser.add_option("--only-values", action="store_true", dest="onlyvalues",
                      help="Dump out the raw extended attribute value(s)"
                      " without encoding them.")

    (option, args) = parser.parse_args()
    if not args:
        print ("Usage: getfattr [-hRLP] [-n name|-d] [-e en] [-m pattern]"
               " path...")
        print ("Try `getfattr --help' for more information.")
        sys.exit(1)

    if option.dump and option.name:
        print ("-d and -n are mutually exclusive...")
        sys.exit(1)

    if option.pattern and option.name:
        print ("-m and -n are mutually exclusive...")
        sys.exit(1)

    if option.encoding:
        if (not (option.encoding.strip() == "hex" or
                 option.encoding.strip() == "base64" or
                 option.encoding.strip() == "text")):
            print ("unrecognized encoding parameter... %s, please use"
                   " `text`, `base64` or `hex`" % option.encoding)
            sys.exit(1)

    args[0] = os.path.abspath(args[0])

    if option.name:
        print_header(args[0], option.absnames)
        try:
            getfattr(args[0], option)
        except KeyError as err:
            print ("Invalid key %s" % err)
            sys.exit(1)
        except IOError as err:
            print (err)
            sys.exit(1)

    if option.pattern:
        print_header(args[0], option.absnames)
        try:
            xattrs = xattr.listxattr(args[0])
            for attr in xattrs:
                if option.dump:
                    option.name = attr.encode('utf-8')
                    getfattr(args[0], option)
                else:
                    option.name = attr.encode('utf-8')
                    print_getfattr(args[0], option, None)

        except IOError as err:
            print (err)
            sys.exit(1)
