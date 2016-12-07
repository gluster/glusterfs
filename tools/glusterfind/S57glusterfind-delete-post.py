#!/usr/bin/python
import os
import shutil
from errno import ENOENT
from subprocess import Popen, PIPE
from argparse import ArgumentParser


DEFAULT_GLUSTERD_WORKDIR = "/var/lib/glusterd"


def handle_rm_error(func, path, exc_info):
    if exc_info[1].errno == ENOENT:
        return

    raise exc_info[1]


def get_glusterd_workdir():
    p = Popen(["gluster", "system::", "getwd"],
              stdout=PIPE, stderr=PIPE)

    out, _ = p.communicate()

    if p.returncode == 0:
        return out.strip()
    else:
        return DEFAULT_GLUSTERD_WORKDIR


def get_args():
    parser = ArgumentParser(description="Volume delete post hook script")
    parser.add_argument("--volname")
    return parser.parse_args()


def main():
    args = get_args()
    glusterfind_dir = os.path.join(get_glusterd_workdir(), "glusterfind")

    # Check all session directories, if any directory found for
    # the deleted volume, cleanup all the session directories
    try:
        ls_glusterfind_dir = os.listdir(glusterfind_dir)
    except OSError:
        ls_glusterfind_dir = []

    for session in ls_glusterfind_dir:
        # don't blow away the keys directory
        if session == ".keys":
            continue

        # Possible session directory
        volume_session_path = os.path.join(glusterfind_dir,
                                           session,
                                           args.volname)
        if os.path.exists(volume_session_path):
            shutil.rmtree(volume_session_path, onerror=handle_rm_error)

        # Try to Remove directory, if any other dir exists for different
        # volume, then rmdir will fail with ENOTEMPTY which is fine
        try:
            os.rmdir(os.path.join(glusterfind_dir, session))
        except (OSError, IOError):
            pass


if __name__ == "__main__":
    main()
