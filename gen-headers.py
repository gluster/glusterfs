#!/usr/bin/python

import sys
try:
    import json
except ImportError:
    import simplejson as json
from string import Template


def getLogBook(logFile='error-codes.json'):
    fp = open(logFile)
    return json.load(fp)


def genCHeader(logBook,
               infile='gf-error-codes.h.template',
               outfile='gf-error-codes.h'):
    fp = open('gf-error-codes.h.template')
    s = fp.read()
    fp.close()
    template = Template(s)

    defineLines = []
    caseLines = []
    for name, value in logBook.iteritems():
        nameDef = "GF_%s" % (name.upper(),)
        code = value['code']
        msgNameDef = "%s_MSG" % (nameDef,)
        msg = value['message']['en']

        defineLines.append("#define %-20s %d" % (nameDef, code))
        defineLines.append("#define %-20s %s" % (msgNameDef,
                                                 json.dumps(msg)))
        caseLines.append("        case %s: return _(%s);" % \
                             (nameDef, msgNameDef))

    d = {'DEFINES': "\n".join(defineLines),
         'CASES': "\n".join(caseLines)}
    #print template.substitute(d)

    fp = open(outfile, 'w')
    fp.write(template.substitute(d))
    fp.close()


if __name__ == "__main__":
    try:
        logBook = getLogBook()
        genCHeader(logBook)
        sys.exit(0)
    except IOError, e:
        print str(e)
        sys.exit(-1)
