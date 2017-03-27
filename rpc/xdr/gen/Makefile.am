XDRGENFILES = glusterfs3-xdr.x cli1-xdr.x nlm4-xdr.x nsm-xdr.x \
	rpc-common-xdr.x glusterd1-xdr.x acl3-xdr.x portmap-xdr.x \
	mount3udp.x changelog-xdr.x glusterfs-fops.x
XDRHEADERS = $(XDRGENFILES:.x=.h)
XDRSOURCES = $(XDRGENFILES:.x=.c)

CLEANFILES = $(XDRSOURCES) $(XDRHEADERS)

# trick automake into doing BUILT_SOURCES magic
BUILT_SOURCES = $(XDRHEADERS) $(XDRSOURCES)

xdrsrc=$(top_srcdir)/rpc/xdr/src

# make's dependency resolution may mean that it decides to run
# rpcgen again (unnecessarily), but as the .c file already exists,
# rpcgen will exit with an error, resulting in a build error. We
# could use a '-' (i.e. -@rpcgen ...) and suffer with noisy warnings
# in the build. Or we do this crufty thing instead.
$(XDRSOURCES): $(XDRGENFILES)
	if [ ! -e $@ -o $(@:.c=.x) -nt $@ ]; then \
		rpcgen -c -o $@ $(@:.c=.x) && cp $@ $(xdrsrc)/ ;\
	fi

# d*mn sed in netbsd6 doesn't do -i (inline)
# (why are we still running smoke on netbsd6 and not netbsd7?)
$(XDRHEADERS): $(XDRGENFILES)
	if [ ! -e $@ -o $(@:.h=.x) -nt $@ ]; then \
		rpcgen -h -o $(@:.h=.tmp) $(@:.h=.x) && \
		sed -e '/#ifndef/ s/-/_/g' -e '/#define/ s/-/_/g' \
			-e '/#endif/ s/-/_/' -e 's/TMP_/H_/g' \
			$(@:.h=.tmp) > $@ && rm -f $(@:.h=.tmp) && \
		cp $@ $(xdrsrc)/ ; \
	fi


# link .x files when doing out-of-tree builds
# have to use .PHONY here to force it; all versions of make
# will think the file already exists "here" by virtue of the
# VPATH. And we have to have the .x file in $cwd in order to
# have rpcgen generate "nice" #include directives
# i.e. (nice):
# #include "acl3-xdr.h"
# versus (not nice):
# #include "../../../../foo/src/rpc/xdr/src/acl3-xdr.h"
.PHONY : $(XDRGENFILES)
$(XDRGENFILES):
	@if [ ! -e $@ ]; then ln -s $(xdrsrc)/$@ . ; fi;

