#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([donnatella], [0.00], [jjk@jjacky.com])
AC_CONFIG_SRCDIR([src/app.h])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])

AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE

AM_INIT_AUTOMAKE([-Wall -Werror -Wno-portability foreign subdir-objects])
AM_SILENT_RULES([yes])

# for automake >= 1.12
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])

# Checks for programs.
AC_PROG_CC

LT_INIT

# Option to make doc
m4_ifdef([GTK_DOC_CHECK], [
GTK_DOC_CHECK([1.19],[--flavour no-tmpl])
AC_PATH_PROG([XSLTPROC], [xsltproc])
  if test -z "$XSLTPROC"; then
    if test "$enable_man" = yes ; then
        AC_MSG_ERROR([xsltproc is required for --enable-gtk-doc])
    fi
  fi
],[
AM_CONDITIONAL([ENABLE_GTK_DOC], false)
])

# Option to use git version
AC_ARG_ENABLE([git-version],
              AS_HELP_STRING([--enable-git-version],
                             [enable the use of git version]),
                             [wantgitver=$enableval],
                             [wantgitver=no])

# XDG dir (for default conf/css)
AC_ARG_WITH([xdg-dir],
            AS_HELP_STRING([--with-xdg-dir],
                           [set default configuration/css dir]),
                           [xdgdir=$withval],
                           [xdgdir=${sysconfdir}/xdg])
AC_SUBST(xdgdir)

# Extra compiler warning flags
AC_ARG_ENABLE([warning-flags],
              AS_HELP_STRING([--enable-warning-flags],
                             [enable extra compiler warning flags]),
                             [warningflags=$enableval], [warningflags=no])

# Debug messages
AC_ARG_ENABLE([debug],
    AS_HELP_STRING([--enable-debug],
                   [enable debug messages support]),
    if test "x$enableval" = "xyes"; then
        with_debug=yes
    elif test "x$enableval" = "xno"; then
        with_debug=no
    else
        AC_MSG_ERROR([Invalid value given to --enable-debug; must be yes or no])
    fi
    ,
    with_debug=no)
AM_CONDITIONAL([DONNA_DEBUG_ENABLED], [test "x$with_debug" = "xyes"])
if test "x$with_debug" = "xyes"; then
    AC_DEFINE([DONNA_DEBUG_ENABLED], 1, [Define to 1 to enable debug messages])
fi

# Automagic breakpoint on critical messages when under GDB/trace
AC_ARG_ENABLE([autobreak-on-critical],
    AS_HELP_STRING([--enable-autobreak-on-critical],
                   [enable automagic break on critical messages]),
    if test "x$enableval" = "xyes"; then
        with_autobreak=yes
    elif test "x$enableval" = "xno"; then
        with_autobreak=no
    else
        AC_MSG_ERROR([Invalid value given to --enable-autobreak-on-critical; must be yes or no])
    fi
    ,
    with_autobreak=no)
AM_CONDITIONAL([DONNA_DEBUG_AUTOBREAK], [test "x$with_autobreak" = "xyes"])
if test "x$with_autobreak" = "xyes"; then
    AC_DEFINE([DONNA_DEBUG_AUTOBREAK], 1, [Define to 1 to autobreak on critical messages])
fi

# Checks for libraries.
PKG_CHECK_MODULES(GTK, [gtk+-3.0], , AC_MSG_ERROR([GTK+3 is required]))

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h locale.h stdlib.h string.h strings.h unistd.h utime.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_UID_T
AC_C_INLINE
AC_TYPE_MODE_T
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_CHOWN
AC_FUNC_GETGROUPS
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_FUNC_STRNLEN
AC_CHECK_FUNCS([endgrent endpwent memmove memset mkdir realpath select setlocale stpcpy strcasecmp strchr strncasecmp strrchr strstr utime])

# git version
AC_MSG_CHECKING([if git version must be used])
if test "x$wantgitver" = "xyes"; then
    AC_MSG_RESULT([yes])
    AC_CHECK_PROGS([GIT], [git])
    if test "x$GIT" = "x"; then
        AC_MSG_ERROR([Cannot use git version: git not found])
    fi
    AC_CHECK_FILE([.git/], hasgitdir=yes)
    if test "x$hasgitdir" = "xyes"; then
        usegitver=yes
        gitver=-git
        AC_DEFINE([USE_GIT_VERSION], 1, [Define to 1 to use GIT version])
    else
        AC_MSG_ERROR([Cannot use git version: .git not found])
    fi
else
    AC_MSG_RESULT([no])
    usegitver=no
    gitver=
fi
AM_CONDITIONAL(USE_GIT_VERSION, test "x$usegitver" = "xyes")

# warning flags
WARNING_CFLAGS="-Wall"
AC_MSG_CHECKING([for extra compiler warning flags])
if test "x$warningflags" = "xyes"; then
    AC_MSG_RESULT([yes])
    CFLAGS_ADD([-Wextra], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wshadow], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wpointer-arith], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wcast-align], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wwrite-strings], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wmissing-prototypes], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wmissing-declarations], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wredundant-decls], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wnested-externs], [WARNING_CFLAGS])
    CFLAGS_ADD([-Winline], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wno-long-long], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wuninitialized], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wconversion], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wstrict-prototypes], [WARNING_CFLAGS])

    CFLAGS_ADD([-Wclobbered], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wempty-body], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wformat-nonliteral], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wformat-security], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wignored-qualifiers], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wlogical-op], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wmissing-field-initializers], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wmissing-parameter-type], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wold-style-declaration], [WARNING_CFLAGS])
    CFLAGS_ADD([-Woverride-init], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wsign-compare], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wstrict-aliasing], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wstrict-overflow=5], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wtype-limits], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wunused-but-set-parameter], [WARNING_CFLAGS])
    CFLAGS_ADD([-Wno-unused-parameter], [WARNING_CFLAGS])
else
    AC_MSG_RESULT([no])
fi

AC_CONFIG_FILES([Makefile docs/reference/Makefile docs/reference/version.xml])
AC_OUTPUT

echo "
    ${PACKAGE} version ${PACKAGE_VERSION}${gitver}

 Build information:
   source code location         : ${srcdir}
   prefix                       : ${prefix}
   compiler warning flags       : ${WARNING_CFLAGS}

   debug messages               : ${with_debug}
   autobreak on critical        : ${with_autobreak}

   build html documentation     : ${enable_gtk_doc}

 Install paths:
   binaries                     : $(eval echo $(eval echo ${bindir}))
   default configuration/css    : $(eval echo $(eval echo ${xdgdir}))
   html documentation           : $(eval echo $(eval echo $(eval echo ${HTML_DIR})))
   man page                     : $(eval echo $(eval echo ${mandir}))
"
