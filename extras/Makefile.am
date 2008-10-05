
docdir = $(datadir)/doc/$(PACKAGE_NAME)
EmacsModedir = $(docdir)/examples
EmacsMode_DATA = glusterfs-mode.el

SUBDIRS = init.d

EXTRA_DIST = specgen.scm glusterfs.vim glusterfs-mode.el

CLEANFILES = 

install-data-am:
	[ "${GF_HOST_OS}" == "GF_DARWIN_HOST_OS" ] && cp glusterfs-server.plist /Library/LaunchDaemons/com.zresearch.glusterfs.plist || echo ""
