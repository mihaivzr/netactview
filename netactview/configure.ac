dnl Process this file with autoconf to produce a configure script.
dnl Created by Anjuta application wizard.

AC_INIT(netactview, 0.6.4, http://sourceforge.net/projects/netactview/support)

AM_INIT_AUTOMAKE([dist-bzip2 no-dist-gzip])
AM_CONFIG_HEADER(config.h)
AM_MAINTAINER_MODE

AC_ISC_POSIX
AC_PROG_CC
AM_PROG_CC_STDC
AC_HEADER_STDC


AC_PROG_LIBTOOL

dnl ***************************************************************************
dnl Internatinalization
dnl ***************************************************************************
IT_PROG_INTLTOOL([0.35.0])
GETTEXT_PACKAGE=netactview
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [GETTEXT package name])
AM_GLIB_GNU_GETTEXT


PKG_CHECK_MODULES(NETACTVIEW, [gtk+-2.0 >= 2.8 libglade-2.0 gnome-vfs-2.0 >= 2.4 glib-2.0 >= 2.8 libgnome-2.0 gconf-2.0 libgtop-2.0 >= 2.12])
AC_SUBST(NETACTVIEW_CFLAGS)
AC_SUBST(NETACTVIEW_LIBS)


AC_CHECK_PROG([have_gksu], [gksu], [yes], [no])

if test "$have_gksu" = yes; then
AC_DEFINE(HAVE_GKSU, 1, [Defined if gksu program is available])
AC_PATH_PROG(gksu_path, [gksu])
AC_DEFINE_UNQUOTED(GKSU_PATH, "$gksu_path", [The gksu executable path])
fi


AC_OUTPUT([
Makefile
data/Makefile
src/Makefile
po/Makefile.in
])


echo "

Configuration:

	Source code location:     ${srcdir}
	Compiler:		  ${CC}
	Have gksu:                ${have_gksu}

"
