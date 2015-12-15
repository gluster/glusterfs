####This document explains how to create a template for your new xlator.

`$ python ./generate_xlator.py <XLATOR_DIRECTORY> <XLATOR_NAME> <FOP_PREFIX>`
        * XLATOR_DIRECTORY: Directory path where the new xlator folder will reside
        * XLATOR_NAME: Name of the xlator you wish to create
        * FOP_PREFIX: This is the fop prefix that you wish to prefix every fop definition in your xlator, fop prefix is generally different than xlator name, if the xlator name is too long.

Eg: `python ./generate_xlator.py /home/u1/glusterfs/xlators/features compression cmpr`
This command will create the following files with some initial contents like copyright, fops definition etc.
Note that there shouldn't be a "/" specified at the end of the <XLATOR_DIRECTORY>
        `* /home/u1/glusterfs/xlators/features/compression/Makefile.am
        * /home/u1/glusterfs/xlators/features/compression/src/Makefile.am
        * /home/u1/glusterfs/xlators/features/compression/src/compression.c
        * /home/u1/glusterfs/xlators/features/compression/src/compression.h
        * /home/u1/glusterfs/xlators/features/compression/src/compression-mem-types.h
        * /home/u1/glusterfs/xlators/features/compression/src/compression-messages.h`

By default all the fops and functions are generated, if you wish to not implement certain fops and functions, comment those lines (by adding '#' at the start of the line) in libglusterfs/src/generate_xlator.py

Few other manual steps required to get the new xlator completely functional:
* Change configure.ac
* Change `<XLATOR_DIRECTORY>/Makefile.am` to include the new xlator directory.
  Eg:  `/home/u1/glusterfs/xlators/features/Makefile.am`
* Change vol file or glusterd volgen to include the new xlator in volfile
