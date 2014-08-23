#!/usr/bin/env python2

import os
import sys
from optparse import OptionParser

import xattr

def convert(string):
    tmp_string = string
    if (string[0] == '0' and
        (string[1] == 's' or
         string[1] == 'S')):
        tmp_string = string.strip('%s%s' %
                                  (string[0],
                                   string[1]))
        return tmp_string.decode('base64')

    if (string[0] == '0' and
        (string[1] == 'x' or
         string[1] == 'X')):
        tmp_string = string.split('%s%s' %
                                  (string[0],
                                   string[1]))
        return tmp_string[1].decode('hex')

    return tmp_string

if __name__ == '__main__':
    usage = "usage: %prog [-n name] [-v value] [-x name]"
    parser = OptionParser(usage=usage)
    parser.add_option("-n", action="store", dest="name", type="string",
                      help="Specifies the name of the extended attribute to set.")
    parser.add_option("-v", action="store", dest="value", type="string",
                      help="Specifies the new value of the extended attribute."
                      " There are three methods available for encoding the value."
                      " If the given string is enclosed in double quotes, the"
                      " inner string is treated as text. In that case,"
                      " backslashes and double quotes have special meanings"
                      " and need to be escaped by a preceding backslash. Any"
                      " control characters can be encoded as a backslash"
                      " followed by three digits as its ASCII code in octal."
                      " If the given string begins with 0x or 0X, it expresses"
                      " a hexadecimal number. If the given string begins with"
                      " 0s or 0S, base64 encoding is expected.")
    parser.add_option("-x", action="store", dest="xname", type="string",
                      help="Remove the named extended attribute entirely.")

    (option,args) = parser.parse_args()
    if not args:
        print ("Usage: setfattr {-n name} [-v value] file...")
        print ("       setfattr {-x name} file...")
        print ("Try `setfattr --help' for more information.")
        sys.exit(1)

    if option.name and option.xname:
        print ("-n and -x are mutually exclusive...")
        sys.exit(1)

    if option.name:
        if option.value is None:
            print ("-n option requires -v value...")

    args[0] = os.path.abspath(args[0])

    if option.name and option.value:
        try:
            xattr.setxattr(args[0], option.name, convert(option.value))
        except Exception as err:
            print (err)
            sys.exit(1)

    if option.xname:
        try:
            xattr.removexattr(args[0], option.xname)
        except Exception as err:
            print (err)
            sys.exit(1)
