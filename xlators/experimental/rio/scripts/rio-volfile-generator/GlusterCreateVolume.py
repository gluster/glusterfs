from argparse import ArgumentParser, RawDescriptionHelpFormatter
import subprocess
from VolFileParser import *
import sys
import os

PROG_DESCRIPTION="Script to create gluster rio volumes..."

def execute(cmd, failure_msg="", success_msg="", exit_code=-1):
    """
        Wrapper to execute CLI commands
    """
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    out, err = p.communicate()
    if p.returncode == 0:
        if success_msg:
            print success_msg
        return out
    else:
        if failure_msg:
            print failure_msg
        else:
            print "Cmd: %s failed with error %s" % (" ".join(cmd), out)
        sys.exit(exit_code)

def get_args():
    """
       CLI Parser...
    """
    parser = ArgumentParser(formatter_class=RawDescriptionHelpFormatter,
                            description=PROG_DESCRIPTION)
    parser.add_argument("vol_name", help="Volume name", metavar="VOLUME_NAME")
    parser.add_argument("mds_count", help="MDS count", metavar="MDS_COUNT", type=int)
    parser.add_argument("ds_count", help="DS count", metavar="DS_COUNT", type=int)
    parser.add_argument("brick_list", help="Brick list as one string", metavar="BRICKS")
    parser.add_argument("--force", help="force volume creation", action="store_true")
    try:
        return parser.parse_args()
    except:
        print "ERROR: Argument Parsing... Did you send brick paths as single string ?"
        sys.exit(1)

def gluster_create_volume():
    """
        Parses and create rio volume....
    """

    args = get_args()
    brick_list = args.brick_list.split()
    if (args.mds_count + args.ds_count) != len(brick_list):
        print "Error:Number of bricks passed doesn't match mds and ds count"
        print brick_list
        sys.exit(1)

    cmd = ["gluster", "volume", "create", args.vol_name]
    cmd += brick_list
    cmd += ["force"] if args.force else []

    execute(cmd, "Volume Creation Failed!! Please check log file", "Volume Creation Success")

    vol_files = []
    client_volfiles = []
    os.chdir("/var/lib/glusterd/vols/" + args.vol_name)
    for i in os.listdir("."):
        if i.startswith(args.vol_name) and i.endswith(".vol") \
                and i.find("rebalance") == -1 and i.find("tcp") == -1:
           vol_files.append(i)

        if i.endswith(".vol") and i.find("tcp-fuse") != -1:
            client_volfiles.append(i)

    vol_files.sort()
    i = 0
    for orig_volfile in vol_files:
        new_volfile = orig_volfile + ".gen"
        generate_rio_server_volfile(args.vol_name, brick_list, args.mds_count, args.ds_count, orig_volfile, new_volfile, i)
        i += 1

    print "Generated rio brick vol files......."
    for clnt_volfile in client_volfiles:
        new_client_volfile = clnt_volfile + ".gen"
        generate_rio_tcp_volfile(args.vol_name, brick_list, args.mds_count, args.ds_count, clnt_volfile, new_client_volfile)
    print "Generated rio tcp client vol files......."

def main():
    """
        Main function...
    """
    try:
        gluster_create_volume()
    except KeyboardInterrupt:
        sys.stderr.write("\nExiting...\n")
        sys.exit(1)

if __name__ == "__main__":
    main()
