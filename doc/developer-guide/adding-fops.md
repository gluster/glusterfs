Adding a new FOP
================

Steps to be followed when adding a new FOP to GlusterFS:

1. Edit `glusterfs.h` and add a `GF_FOP_*` constant.
2. Edit `xlator.[ch]` and:
    * add the new prototype for fop and callback.
    * edit `xlator_fops` structure.
3. Edit `xlator.c` and add to fill_defaults.
4. Edit `protocol.h` and add struct necessary for the new FOP.
5. Edit `defaults.[ch]` and provide default implementation.
6. Edit `call-stub.[ch]` and provide stub implementation.
7. Edit `common-utils.c` and add to gf_global_variable_init().
8. Edit client-protocol and add your FOP.
9. Edit server-protocol and add your FOP.
10. Implement your FOP in any translator for which the default implementation
    is not sufficient.
