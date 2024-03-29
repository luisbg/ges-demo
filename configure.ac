AC_PREREQ(2.60)
dnl initialize autoconf
dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, cvs and prerelease does Werror too
AC_INIT([ges-demoy],
    [0.1],
    NULL,
    ges-demo)
AM_MAINTAINER_MODE

AC_CONFIG_SRCDIR([src/ges-demo.c])
AM_CONFIG_HEADER([config.h])
AC_CONFIG_AUX_DIR([build-aux])

dnl initialize automake
AM_INIT_AUTOMAKE([1.11.1 no-dist-gzip dist-xz dist-bzip2 tar-ustar])

dnl use pretty build output with automake >= 1.11
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])],
  [AM_DEFAULT_VERBOSITY=1
   AC_SUBST(AM_DEFAULT_VERBOSITY)])
m4_define([_LT_AC_TAGCONFIG],[])

dnl find a compiler
AC_PROG_CC
AM_PROG_CC_C_O
AC_STDC_HEADERS
AM_PROG_LIBTOOL

dnl instruct libtool machinery that the package supports DLLs
AC_LIBTOOL_WIN32_DLL

AC_CANONICAL_HOST

# keep one single GST_REQ, core/base should be same versions anyway
GST_REQ=0.10
GES_REQ=0.10
GTK2_REQ=2.14.0

PKG_CHECK_MODULES([GST], \
    [gstreamer-1.0 >= $GST_REQ
    gstreamer-base-1.0 >= $GST_REQ
    gstreamer-pbutils-1.0 >= $GST_REQ
    gst-editing-services-1.0 >= $GES_REQ
    gstreamer-video-1.0 >= $GST_REQ])

AC_SUBST(GST_CFLAGS)
AC_SUBST(GST_LIBS)

PKG_CHECK_MODULES(GTK2, gtk+-2.0 >= GTK2_REQ)

AC_SUBST(GTK2_CFLAGS)
AC_SUBST(GTK2_LIBS)

dnl our libraries and install dirs use major.minor as a version
GST_MAJORMINOR=$PACKAGE_VERSION_MAJOR.$PACKAGE_VERSION_MINOR
dnl we override it here if we need to for the release candidate of new series
GST_MAJORMINOR=0.10
AC_SUBST(GST_MAJORMINOR)

AM_PROG_LIBTOOL

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 -I common/m4")
AC_CONFIG_MACRO_DIR([m4])

ROOT_UNINSTALLED_DIR="`pwd`"
AC_DEFINE_UNQUOTED(SHARE_UNINSTALLED_DIR,"`pwd`/$srcdir/data",[path to source data dir])
AC_DEFINE_UNQUOTED(SHARE_UNINSTALLED_BUILDDIR,"`pwd`/data",[path to built source data dir])
AC_SUBST(ROOT_UNINSTALLED_DIR)

dnl set license and copyright notice
GST_LICENSE="LGPL"
AC_DEFINE_UNQUOTED(GST_LICENSE, "$GST_LICENSE", [GStreamer license])
AC_SUBST(GST_LICENSE)

dnl keep this alphabetic per directory, please
AC_CONFIG_FILES([
Makefile
data/Makefile
src/Makefile
])
AC_OUTPUT

echo "

Configuration
	Version                    : ${VERSION}
	Source code location       : ${srcdir}
	Prefix                     : ${prefix}
	Compiler                   : ${CC}

snappy configured. Type 'make' to build.
"
