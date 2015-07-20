## Process this file with automake to produce Makefile.in

## Created by Anjuta

glade_images = netactview-icon.png auto-refresh-icon.png

gladedir = $(datadir)/netactview/glade
glade_DATA = $(glade_images) netactview.glade

INCLUDES = \
	-DPREFIX=\""$(prefix)"\" \
	-DBINDIR=\""$(bindir)"\" \
	-DLOCALEDIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	-DSRCDIR=\""$(srcdir)"\" \
	-DDATADIR=\""$(datadir)"\" \
	-DSYSCONFDIR=\"$(sysconfdir)\" \
	-DLIBDIR=\"$(libdir)\" \
	$(NETACTVIEW_CFLAGS)

AM_CFLAGS =\
	 -Wall\
	 -g

bin_PROGRAMS = netactview

netactview_SOURCES = \
	main.c \
	mainwindow.c \
	mainwindow.h \
	net.c \
	net.h \
	process.c \
	process.h \
	nactv-debug.h \
	definitions.h \
	utils.h \
	utils.c \
	filter.c \
	filter.h

netactview_LDFLAGS = 

netactview_LDADD = $(NETACTVIEW_LIBS)

EXTRA_DIST = $(glade_DATA)
