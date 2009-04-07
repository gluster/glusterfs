# New ports collection makefile for: glusterfs
# Date created:        13 January 2009
# Whom:		       Harshavardhana
#
# $FreeBSD$
#

PORTNAME=      glusterfs
DISTVERSION=   2.0.0rc1
PORTVERSION=   2.0.0rc1
PORTREVISION=  1
CATEGORIES=    sysutils
MASTER_SITES=  ftp://ftp.zresearch.com/pub/gluster/glusterfs/2.0/2.0.0/ \
		http://europe.gluster.org/glusterfs/2.0/2.0.0/

MAINTAINER=    harsha@zresearch.com
COMMENT=       GlusterFS is a clustered file-system

BUILD_DEPENDS= fusefs-libs>2.6.3:${PORTSDIR}/sysutils/fusefs-libs
LIB_DEPENDS=   fuse.2:${PORTSDIR}/sysutils/fusefs-libs
RUN_DEPENDS=   ${LOCALBASE}/modules/fuse.ko:${PORTSDIR}/sysutils/fusefs-kmod

MAN8=          glusterfs.8
GNU_CONFIGURE= yes
USE_LDCONFIG=  yes
USE_AUTOTOOLS= libtool:15
CONFIGURE_ENV= CPPFLAGS="-I${LOCALBASE}/include" LDFLAGS="-L${LOCALBASE}/lib"
CONFIGURE_TARGET= --build=${ARCH}-portbld-freebsd${OSREL}

.include <bsd.port.pre.mk>

.if ${OSVERSION} < 700000
IGNORE=        requires FreeBSD 7 or above
.endif

.include <bsd.port.post.mk>
