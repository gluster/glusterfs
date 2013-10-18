if ENABLE_QEMU_BLOCK
xlator_LTLIBRARIES = qemu-block.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

qemu_block_la_LDFLAGS = -module -avoid-version
qemu_block_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la $(GLIB_LIBS) -lz -lrt

qemu_block_la_SOURCES_qemu = \
	$(CONTRIBDIR)/qemu/qemu-coroutine.c \
	$(CONTRIBDIR)/qemu/qemu-coroutine-lock.c \
	$(CONTRIBDIR)/qemu/qemu-coroutine-sleep.c \
	$(CONTRIBDIR)/qemu/coroutine-ucontext.c \
	$(CONTRIBDIR)/qemu/block.c \
	$(CONTRIBDIR)/qemu/nop-symbols.c

qemu_block_la_SOURCES_qemu_util = \
	$(CONTRIBDIR)/qemu/util/aes.c \
	$(CONTRIBDIR)/qemu/util/bitmap.c \
	$(CONTRIBDIR)/qemu/util/bitops.c \
	$(CONTRIBDIR)/qemu/util/cutils.c \
	$(CONTRIBDIR)/qemu/util/error.c \
	$(CONTRIBDIR)/qemu/util/hbitmap.c \
	$(CONTRIBDIR)/qemu/util/iov.c \
	$(CONTRIBDIR)/qemu/util/module.c \
	$(CONTRIBDIR)/qemu/util/oslib-posix.c \
	$(CONTRIBDIR)/qemu/util/qemu-option.c \
	$(CONTRIBDIR)/qemu/util/qemu-error.c \
	$(CONTRIBDIR)/qemu/util/qemu-thread-posix.c \
	$(CONTRIBDIR)/qemu/util/unicode.c \
	$(CONTRIBDIR)/qemu/util/hexdump.c

qemu_block_la_SOURCES_qemu_block = \
	$(CONTRIBDIR)/qemu/block/snapshot.c \
	$(CONTRIBDIR)/qemu/block/qcow2-cache.c \
	$(CONTRIBDIR)/qemu/block/qcow2-cluster.c \
	$(CONTRIBDIR)/qemu/block/qcow2-refcount.c \
	$(CONTRIBDIR)/qemu/block/qcow2-snapshot.c \
	$(CONTRIBDIR)/qemu/block/qcow2.c \
	$(CONTRIBDIR)/qemu/block/qed-check.c \
	$(CONTRIBDIR)/qemu/block/qed-cluster.c \
	$(CONTRIBDIR)/qemu/block/qed-gencb.c \
	$(CONTRIBDIR)/qemu/block/qed-l2-cache.c \
	$(CONTRIBDIR)/qemu/block/qed-table.c \
	$(CONTRIBDIR)/qemu/block/qed.c

qemu_block_la_SOURCES_qemu_qobject = \
	$(CONTRIBDIR)/qemu/qobject/json-lexer.c \
	$(CONTRIBDIR)/qemu/qobject/json-parser.c \
	$(CONTRIBDIR)/qemu/qobject/json-streamer.c \
	$(CONTRIBDIR)/qemu/qobject/qbool.c \
	$(CONTRIBDIR)/qemu/qobject/qdict.c \
	$(CONTRIBDIR)/qemu/qobject/qerror.c \
	$(CONTRIBDIR)/qemu/qobject/qfloat.c \
	$(CONTRIBDIR)/qemu/qobject/qint.c \
	$(CONTRIBDIR)/qemu/qobject/qjson.c \
	$(CONTRIBDIR)/qemu/qobject/qlist.c \
	$(CONTRIBDIR)/qemu/qobject/qstring.c

qemu_block_la_SOURCES = \
	$(qemu_block_la_SOURCES_qemu) \
	$(qemu_block_la_SOURCES_qemu_util) \
	$(qemu_block_la_SOURCES_qemu_block) \
	$(qemu_block_la_SOURCES_qemu_qobject) \
	bdrv-xlator.c \
	coroutine-synctask.c \
	bh-syncop.c \
	monitor-logging.c \
	clock-timer.c \
	qemu-block.c \
	qb-coroutines.c

noinst_HEADERS_qemu = \
	$(CONTRIBDIR)/qemu/config-host.h \
	$(CONTRIBDIR)/qemu/qapi-types.h \
	$(CONTRIBDIR)/qemu/qmp-commands.h \
	$(CONTRIBDIR)/qemu/trace/generated-tracers.h \
	$(CONTRIBDIR)/qemu/include/config.h \
	$(CONTRIBDIR)/qemu/include/glib-compat.h \
	$(CONTRIBDIR)/qemu/include/qemu-common.h \
	$(CONTRIBDIR)/qemu/include/trace.h \
	$(CONTRIBDIR)/qemu/include/block/coroutine.h \
	$(CONTRIBDIR)/qemu/include/block/aio.h \
	$(CONTRIBDIR)/qemu/include/block/block.h \
	$(CONTRIBDIR)/qemu/include/block/block_int.h \
	$(CONTRIBDIR)/qemu/include/block/blockjob.h \
	$(CONTRIBDIR)/qemu/include/block/coroutine.h \
	$(CONTRIBDIR)/qemu/include/block/coroutine_int.h \
	$(CONTRIBDIR)/qemu/include/block/snapshot.h \
	$(CONTRIBDIR)/qemu/include/exec/cpu-common.h \
	$(CONTRIBDIR)/qemu/include/exec/hwaddr.h \
	$(CONTRIBDIR)/qemu/include/exec/poison.h \
	$(CONTRIBDIR)/qemu/include/fpu/softfloat.h \
	$(CONTRIBDIR)/qemu/include/migration/migration.h \
	$(CONTRIBDIR)/qemu/include/migration/qemu-file.h \
	$(CONTRIBDIR)/qemu/include/migration/vmstate.h \
	$(CONTRIBDIR)/qemu/include/monitor/monitor.h \
	$(CONTRIBDIR)/qemu/include/monitor/readline.h \
	$(CONTRIBDIR)/qemu/include/qapi/error.h \
	$(CONTRIBDIR)/qemu/include/qapi/qmp/json-lexer.h \
	$(CONTRIBDIR)/qemu/include/qapi/qmp/json-parser.h \
	$(CONTRIBDIR)/qemu/include/qapi/qmp/json-streamer.h \
	$(CONTRIBDIR)/qemu/include/qapi/qmp/qbool.h \
	$(CONTRIBDIR)/qemu/include/qapi/qmp/qdict.h \
	$(CONTRIBDIR)/qemu/include/qapi/qmp/qerror.h \
	$(CONTRIBDIR)/qemu/include/qapi/qmp/qfloat.h \
	$(CONTRIBDIR)/qemu/include/qapi/qmp/qint.h \
	$(CONTRIBDIR)/qemu/include/qapi/qmp/qjson.h \
	$(CONTRIBDIR)/qemu/include/qapi/qmp/qlist.h \
	$(CONTRIBDIR)/qemu/include/qapi/qmp/qobject.h \
	$(CONTRIBDIR)/qemu/include/qapi/qmp/qstring.h \
	$(CONTRIBDIR)/qemu/include/qapi/qmp/types.h \
	$(CONTRIBDIR)/qemu/include/qemu/aes.h \
	$(CONTRIBDIR)/qemu/include/qemu/atomic.h \
	$(CONTRIBDIR)/qemu/include/qemu/bitmap.h \
	$(CONTRIBDIR)/qemu/include/qemu/bitops.h \
	$(CONTRIBDIR)/qemu/include/qemu/bswap.h \
	$(CONTRIBDIR)/qemu/include/qemu/compiler.h \
	$(CONTRIBDIR)/qemu/include/qemu/error-report.h \
	$(CONTRIBDIR)/qemu/include/qemu/event_notifier.h \
	$(CONTRIBDIR)/qemu/include/qemu/hbitmap.h \
	$(CONTRIBDIR)/qemu/include/qemu/host-utils.h \
	$(CONTRIBDIR)/qemu/include/qemu/iov.h \
	$(CONTRIBDIR)/qemu/include/qemu/main-loop.h \
	$(CONTRIBDIR)/qemu/include/qemu/module.h \
	$(CONTRIBDIR)/qemu/include/qemu/notify.h \
	$(CONTRIBDIR)/qemu/include/qemu/option.h \
	$(CONTRIBDIR)/qemu/include/qemu/option_int.h \
	$(CONTRIBDIR)/qemu/include/qemu/osdep.h \
	$(CONTRIBDIR)/qemu/include/qemu/queue.h \
	$(CONTRIBDIR)/qemu/include/qemu/sockets.h \
	$(CONTRIBDIR)/qemu/include/qemu/thread-posix.h \
	$(CONTRIBDIR)/qemu/include/qemu/thread.h \
	$(CONTRIBDIR)/qemu/include/qemu/timer.h \
	$(CONTRIBDIR)/qemu/include/qemu/typedefs.h \
	$(CONTRIBDIR)/qemu/include/sysemu/sysemu.h \
	$(CONTRIBDIR)/qemu/include/sysemu/os-posix.h \
	$(CONTRIBDIR)/qemu/block/qcow2.h \
	$(CONTRIBDIR)/qemu/block/qed.h

noinst_HEADERS = \
	$(noinst_HEADERS_qemu) \
	qemu-block.h \
	qemu-block-memory-types.h \
	qb-coroutines.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
            -I$(CONTRIBDIR)/qemu \
            -I$(CONTRIBDIR)/qemu/include \
            -DGLUSTER_XLATOR

AM_CFLAGS = -fno-strict-aliasing -Wall $(GF_CFLAGS) $(GLIB_CFLAGS)

CLEANFILES =

endif
