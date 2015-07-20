
appicondir = $(datadir)/pixmaps
appicon_DATA = netactview.png

desktopdir = $(datadir)/applications
desktop_in_files=netactview.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

man_MANS = netactview.1


EXTRA_DIST = \
	$(desktop_in_files) \
	$(appicon_DATA) \
	$(man_MANS)
	
CLEANFILES = \
	$(desktop_DATA)

