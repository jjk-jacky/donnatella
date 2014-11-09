
DISTCLEANFILES = src/command-docs.h

SUBDIRS = . docs/reference

DEF_COMMANDS = src/command.c \
			   src/provider-filter.c \
			   src/provider-mark.c \
			   src/provider-mru.c \
			   src/provider-register.c

ACLOCAL_AMFLAGS = -I m4

if USE_GIT_VERSION
_VERSION = `git describe --abbrev=4 --dirty`
DEFS += -DGIT_VERSION=\"$(_VERSION)\"
else
_VERSION = $(PACKAGE_VERSION)
endif

bin_PROGRAMS = donnatella donna-trigger

AM_CFLAGS = -D_GNU_SOURCE \
			-DDOCDIR='"$(docdir)"' \
			${WARNING_CFLAGS}

if ENABLE_GTK_DOC
BUILT_SOURCES = src/command-docs.h
endif

noinst_LTLIBRARIES = libdonnatella.la
libdonnatella_la_CFLAGS = ${AM_CFLAGS} @GTK_CFLAGS@
libdonnatella_la_LIBADD = @GTK_LIBS@ -lX11
libdonnatella_la_SOURCES = \
					 src/app.c \
					 src/app.h \
					 src/cellrenderertext.c \
					 src/cellrenderertext.h \
					 src/closures \
					 src/closures.c \
					 src/closures.def \
					 src/closures.h \
					 src/colorfilter.c \
					 src/colorfilter.h \
					 src/columntype.c \
					 src/columntype.h \
					 src/columntype-label.c \
					 src/columntype-label.h \
					 src/columntype-name.c \
					 src/columntype-name.h \
					 src/columntype-perms.c \
					 src/columntype-perms.h \
					 src/columntype-progress.c \
					 src/columntype-progress.h \
					 src/columntype-size.c \
					 src/columntype-size.h \
					 src/columntype-text.c \
					 src/columntype-text.h \
					 src/columntype-time.c \
					 src/columntype-time.h \
					 src/columntype-value.c \
					 src/columntype-value.h \
					 src/command.c \
					 src/command.h \
					 src/common.h \
					 src/conf.h \
					 src/context.c \
					 src/context.h \
					 src/contextmenu.c \
					 src/contextmenu.h \
					 src/debug.h \
					 src/embedder.c \
					 src/embedder.h \
					 src/filter.c \
					 src/filter.h \
					 src/filter-private.h \
					 src/history.c \
					 src/history.h \
					 src/imagemenuitem.c \
					 src/imagemenuitem.h \
					 src/ioengine-basic.c \
					 src/ioengine-exec.c \
					 src/macros.h \
					 src/misc.c \
					 src/misc.h \
					 src/node.c \
					 src/node.h \
					 src/pattern.c \
					 src/pattern.h \
					 src/provider.c \
					 src/provider.h \
					 src/provider-base.c \
					 src/provider-base.h \
					 src/provider-command.c \
					 src/provider-command.h \
					 src/provider-config.c \
					 src/provider-config.h \
					 src/provider-exec.c \
					 src/provider-exec.h \
					 src/provider-filter.c \
					 src/provider-filter.h \
					 src/provider-fs.c \
					 src/provider-fs.h \
					 src/provider-internal.c \
					 src/provider-internal.h \
					 src/provider-invalid.c \
					 src/provider-invalid.h \
					 src/provider-mark.c \
					 src/provider-mark.h \
					 src/provider-mru.c \
					 src/provider-mru.h \
					 src/provider-register.c \
					 src/provider-register.h \
					 src/provider-task.c \
					 src/provider-task.h \
					 src/renderer.h \
					 src/socket.c \
					 src/socket.h \
					 src/sort.c \
					 src/sort.h \
					 src/statusbar.c \
					 src/statusbar.h \
					 src/statusprovider.c \
					 src/statusprovider.h \
					 src/task.c \
					 src/task.h \
					 src/task-helpers.c \
					 src/task-helpers.h \
					 src/task-manager.h \
					 src/task-process.c \
					 src/task-process.h \
					 src/taskui.c \
					 src/taskui.h \
					 src/taskui-messages.c \
					 src/taskui-messages.h \
					 src/terminal.c \
					 src/terminal.h \
					 src/treeview.c \
					 src/treeview.h \
					 src/util.c \
					 src/util.h

donnatella_CFLAGS = ${AM_CFLAGS} @GTK_CFLAGS@
donnatella_LDADD = libdonnatella.la @GTK_LIBS@ -lX11
donnatella_SOURCES = src/donnatella.c

donna_trigger_CFLAGS = ${AM_CFLAGS} @GLIB_CFLAGS@
donna_trigger_LDADD = @GLIB_LIBS@
donna_trigger_SOURCES = src/donna-trigger/donna-trigger.c \
						src/socket.c \
						src/socket.h

if DONNA_DEBUG_ENABLED
AM_CFLAGS += -g -O0
libdonnatella_la_SOURCES += src/debug.c
endif

dist_doc_DATA = README.md \
				AUTHORS \
				COPYING \
				LICENSE \
				HISTORY

miscdir = @xdgdir@/${PACKAGE}
dist_misc_DATA = misc/donnatella.conf \
				 misc/donnatella.css \
				 misc/marks.conf

miscbindir = @xdgdir@/${PACKAGE}/bin
dist_miscbin_DATA = scripts/donna-functions \
					scripts/donna-sel_filter

desktopdir = ${datadir}/applications
dist_desktop_DATA = misc/donnatella.desktop

src/command-docs.h: $(DEF_COMMANDS)
	$(AM_V_GEN)./docs/gen-command-docs $(DEF_COMMANDS) > src/command-docs.h

install-data-hook:
	chmod ugo+x "$(DESTDIR)$(miscbindir)/donna-sel_filter"
