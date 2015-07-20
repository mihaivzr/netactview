## Process this file with automake to produce Makefile.in
## Created by Anjuta

SUBDIRS = src data po

EXTRA_DIST = $(netactviewdoc_DATA) \
	README\
	COPYING\
	AUTHORS\
	ChangeLog\
	INSTALL\
	NEWS \
	intltool-extract.in 	\
	intltool-merge.in 	\
	intltool-update.in \
	autogen.sh
	
DISTCLEANFILES = \
	intltool-extract	\
	intltool-merge		\
	intltool-update

# Copy all the spec files. Of cource, only one is actually used.
dist-hook:
	for specfile in *.spec; do \
		if test -f $$specfile; then \
			cp -p $$specfile $(distdir); \
		fi \
	done

