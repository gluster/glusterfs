#!/usr/bin/python

##
 #
 # Copyright (c) 2011 Gluster, Inc. <http://www.gluster.com>
 # This file is part of GlusterFS.
 #
 # Licensed under the Apache License, Version 2.0
 # (the "License"); you may not use this file except in compliance with
 # the License. You may obtain a copy of the License at
 #
 # http://www.apache.org/licenses/LICENSE-2.0
 #
 # Unless required by applicable law or agreed to in writing, software
 # distributed under the License is distributed on an "AS IS" BASIS,
 # WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 # implied. See the License for the specific language governing
 # permissions and limitations under the License.
 #
 ##

import getopt
import glob
import sys, os
import shutil
import subprocess, shlex

def usage():
    print "usage: python build-deploy-jar.py [-b/--build] -d/--dir <hadoop-home> [-c/--core] [-m/--mapred] [-h/--henv]"

def addSlash(s):
    if not (s[-1] == '/'):
        s = s + '/'

    return s

def whereis(program):
    abspath = None
    for path in (os.environ.get('PATH', '')).split(':'):
        abspath = os.path.join(path, program)
        if os.path.exists(abspath) and not os.path.isdir(abspath):
            return abspath

    return None

def getLatestJar(targetdir):
    glusterfsJar = glob.glob(targetdir + "*.jar")
    if len(glusterfsJar) == 0:
        print "No GlusterFS jar file found in %s ... exiting" % (targetdir)
        return None

    # pick up the latest jar file - just in case ...
    stat = latestJar = None
    ctime = 0

    for jar in glusterfsJar:
        stat = os.stat(jar)
        if stat.st_ctime > ctime:
           latestJar = jar
           ctime = stat.st_ctime

    return latestJar

# build the glusterfs hadoop plugin using maven
def build_jar(targetdir):
    location = whereis('mvn')

    if location == None:
        print "Cannot find maven to build glusterfs hadoop jar"
        print "please install maven or if it's already installed then fix your PATH environ"
        return None

    # do a clean packaging
    if os.path.exists(targetdir) and os.path.isdir(targetdir):
        print "Cleaning up directories ... [ " + targetdir + " ]"
        shutil.rmtree(targetdir)

    print "Building glusterfs jar ..."
    process = subprocess.Popen(['package'], shell=True,
                               executable=location, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    process.wait()
    if not process.returncode == 0:
        print "Building glusterfs jar failed ... exiting"
        return None

    latestJar = getLatestJar(targetdir)
    return latestJar

def rcopy(f, host, libdir):
    print "   * doing remote copy to host %s" % (host)
    scpCmd = "scp %s %s:%s" % (f, host, libdir)

    os.system(scpCmd);

def deployInSlave(f, confdir, libdir, cc, cm, he):
    slavefile = confdir + "slaves"

    ccFile = confdir + "core-site.xml"
    cmFile = confdir + "mapred-site.xml"
    heFile = confdir + "hadoop-env.sh"

    sf = open(slavefile, 'r')
    for host in sf:
        host = host.rstrip('\n')
        print "  >>> Deploying %s on %s ..." % (os.path.basename(f), host)
        rcopy(f, host, libdir)

        if cc:
            print "  >>> Deploying [%s] on %s ..." % (os.path.basename(ccFile), host)
            rcopy(ccFile, host, confdir)

        if cm:
            print "  >>> Deploying [%s] on %s ..." % (os.path.basename(cmFile), host)
            rcopy(cmFile, host, confdir)

        if he:
            print "  >>> Deploying [%s] on %s ..." % (os.path.basename(heFile), host)
            rcopy(heFile, host, confdir);

        print "<<< Done\n"

    sf.close()

def deployInMaster(f, confdir, libdir):
    import socket
    masterfile = confdir + "masters"

    mf = open(masterfile, 'r')
    for host in mf:
        host = host.rstrip('\n')
        print "  >>> Deploying %s on %s ..." % (os.path.basename(f), host)
        h = host
        try:
            socket.inet_aton(host)
            h = socket.getfqdn(host)
        except socket.error:
            pass

        if h == socket.gethostname() or h == 'localhost':
            # local cp
            print "   * doing local copy"
            shutil.copy(f, libdir)
        else:
            # scp the file
            rcopy(f, h, libdir)

        print "<<< Done\n"

    mf.close()

if __name__ == '__main__':
    opt = args = []
    try:
        opt, args = getopt.getopt(sys.argv[1:], "bd:cmh", ["build", "dir=", "core", "mapred", "henv"]);
    except getopt.GetoptError, err:
        print str(err)
        usage()
        sys.exit(1)

    needbuild = hadoop_dir = copyCore = copyMapred = copyHadoopEnv = None

    for k, v in opt:
        if k in ("-b", "--build"):
            needbuild = True
        elif k in ("-d", "--dir"):
            hadoop_dir = v
        elif k in ("-c", "--core"):
            copyCore = True
        elif k in ("-m", "--mapred"):
            copyMapred = True
        elif k in ("-h", "--henv"):
            copyHadoopEnv = True
        else:
            pass

    if hadoop_dir == None:
        print 'hadoop directory missing'
        usage()
        sys.exit(1)

    os.chdir(os.path.dirname(sys.argv[0]) + '/..')
    targetdir = './target/'

    if needbuild:
        jar = build_jar(targetdir)
        if jar == None:
            sys.exit(1)
    else:
        jar = getLatestJar(targetdir)
        if jar == None:
            print "Maybe you want to build it ? with -b option"
            sys.exit(1)

    print ""
    print "*** Deploying %s *** " % (jar)

    # copy jar to local hadoop distribution (master)
    hadoop_home = addSlash(hadoop_dir)
    if not (os.path.exists(hadoop_home) and os.path.isdir(hadoop_home)):
        print "path " + hadoop_home + " does not exist or is not adiretory";
        sys.exit(1);

    hadoop_conf = hadoop_home + "conf/"
    hadoop_lib = hadoop_home + "lib/"

    print " >>> Scanning hadoop master file for host(s) to deploy"
    deployInMaster(jar, hadoop_conf, hadoop_lib)

    print ""
    print " >>> Scanning hadoop slave file for host(s) to deploy"
    deployInSlave(jar, hadoop_conf, hadoop_lib, copyCore, copyMapred, copyHadoopEnv)
