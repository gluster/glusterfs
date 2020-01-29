#!/usr/bin/python

import sys
from generator import fop_subs, generate

FN_METADATA_CHILD_GENERIC = """
int32_t
metadisp_@NAME@ (call_frame_t *frame, xlator_t *this,
                 @LONG_ARGS@)
{
  METADISP_TRACE("@NAME@ metadata");
  STACK_WIND (frame, default_@NAME@_cbk,
              METADATA_CHILD(this), METADATA_CHILD(this)->fops->@NAME@,
              @SHORT_ARGS@);
  return 0;
}
"""

FN_GENERIC_TEMPLATE = """
int32_t
metadisp_@NAME@ (call_frame_t *frame, xlator_t *this,
                          @LONG_ARGS@)
{
  METADISP_TRACE("@NAME@ generic");
  STACK_WIND (frame, default_@NAME@_cbk,
                          DATA_CHILD(this), DATA_CHILD(this)->fops->@NAME@,
                          @SHORT_ARGS@);
  return 0;
}
"""

FN_DATAFD_TEMPLATE = """
int32_t
metadisp_@NAME@ (call_frame_t *frame, xlator_t *this,
                          @LONG_ARGS@)
{
  METADISP_TRACE("@NAME@ datafd");
  xlator_t *child = NULL;
  child = DATA_CHILD(this);
  STACK_WIND (frame, default_@NAME@_cbk,
                          child, child->fops->@NAME@,
                          @SHORT_ARGS@);
  return 0;
}
"""

FN_DATALOC_TEMPLATE = """
int32_t
metadisp_@NAME@ (call_frame_t *frame, xlator_t *this,
                          @LONG_ARGS@)
{
  METADISP_TRACE("@NAME@ dataloc");
  loc_t backend_loc = {
      0,
  };
  if (build_backend_loc(loc->gfid, loc, &backend_loc)) {
      goto unwind;
  }
  xlator_t *child = NULL;
  child = DATA_CHILD(this);
  STACK_WIND (frame, default_@NAME@_cbk,
                          child, child->fops->@NAME@,
                          @SHORT_ARGS@);
  return 0;

unwind:
  STACK_UNWIND_STRICT(lookup, frame, -1, EINVAL, NULL, NULL, NULL, NULL);
  return 0;
}
"""

FOPS_LINE_TEMPLATE = "\t.@NAME@ = metadisp_@NAME@,"

skipped = [
    "readdir",
    "readdirp",
    "lookup",
    "fsync",
    "stat",
    "open",
    "create",
    "unlink",
    "setattr",
    # TODO: implement "inodelk",
]


def gen_fops():
    done = skipped

    #
    # these are fops that wind to the DATA_CHILD
    #
    # NOTE: re-written in order from google doc:
    #          https://docs.google.com/document/d/1KEwVtSNvDhs4qb63gWx2ulCp5GJjge77NGJk4p_Ms4Q
    for name in [
        "writev",
        "readv",
        "ftruncate",
        "zerofill",
        "discard",
        "seek",
        "fstat",
    ]:
        done = done + [name]
        print(generate(FN_DATAFD_TEMPLATE, name, fop_subs))

    for name in ["truncate"]:
        done = done + [name]
        print(generate(FN_DATALOC_TEMPLATE, name, fop_subs))

    # these are fops that operate solely on dentries, folders,
    # or extended attributes. Therefore, they must always
    # wind to METADATA_CHILD and should never perform
    # any path rewriting
    #
    # NOTE: re-written in order from google doc:
    #          https://docs.google.com/document/d/1KEwVtSNvDhs4qb63gWx2ulCp5GJjge77NGJk4p_Ms4Q
    for name in [
        "mkdir",
        "symlink",
        "link",
        "rename",
        "mknod",
        "opendir",
        # "readdir,  # special-cased
        # "readdirp, # special-cased
        "fsyncdir",
        # "setattr", # special-cased
        "readlink",
        "fentrylk",
        "access",
        # TODO: these wind to both,
        # data for backend-attributes and metadata for the rest
        "xattrop",
        "setxattr",
        "getxattr",
        "removexattr",
        "fgetxattr",
        "fsetxattr",
        "fremovexattr",
    ]:

        done = done + [name]
        print(generate(FN_METADATA_CHILD_GENERIC, name, fop_subs))

    print("struct xlator_fops fops = {")
    for name in done:
        print(generate(FOPS_LINE_TEMPLATE, name, fop_subs))

    print("};")


for l in open(sys.argv[1], "r").readlines():
    if l.find("#pragma generate") != -1:
        print("/* BEGIN GENERATED CODE - DO NOT MODIFY */")
        gen_fops()
        print("/* END GENERATED CODE */")
    else:
        print(l[:-1])
