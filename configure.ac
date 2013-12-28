# $Id$
AC_INIT([DaS], [1.0], [m5l2764k@gmail.com], [das])
AC_PREREQ([2.59])
AM_INIT_AUTOMAKE([1.10 -Wall no-define])
LT_INIT([disable-static])


AC_PATH_PROG([RPMBUILD], [rpmbuild], [notfound])
AC_PATH_PROG([RPM], [rpm], [notfound])
AC_PATH_PROG([DEBUILD], [dpkg-buildpackage], [notfound])

# --enable-rpm copies the ni executable and the spec file to the dist package,
# for use in building RPM packages. Requires rpm and rpmbuild.
AC_ARG_ENABLE([rpm],
    [AS_HELP_STRING([--enable-rpm=@<:@yes/no@:>@],
        [Configure for building RPM package @<:@default=no@:>@ (requires rpm and rpmbuild)])],
    [AS_IF([test "x$enable_rpm" = xyes],
        [AS_IF([test "x$RPMBUILD" = xnotfound],
            [AC_MSG_ERROR([rpmbuild is required for --enable-rpm])],
        [test "x$RPM" = xnotfound],
            [AC_MSG_ERROR([rpm is required for --enable-rpm])])])],
    [enable_rpm=no])
AM_CONDITIONAL(BUILDING_RPM, test "x$enable_rpm" = xyes)

# --enable-deb copies only the ni executable to the dist package, for use in
# building DEB packages.
AC_ARG_ENABLE([deb],
    [AS_HELP_STRING([--enable-deb=@<:@yes/no@:>@],
        [Configure for building Debian package @<:@default=no@:>@ (requires dpkg-buildpackage)])],
    [AS_IF([test "x$enable_deb" = xyes],
        [AS_IF([test "x$DEBUILD" = xnotfound],
            [AC_MSG_ERROR([dpkg-buildpackage (apt-get install dpkg-dev debhelper) is required for --enable-deb])],
        [test "x$enable_rpm" = xyes],
            [AC_MSG_ERROR([--enable-rpm and --enable-deb are mutually exclusive])])])],
    [enable_deb=no])
AM_CONDITIONAL([BUILDING_DEB], [test "x$enable_deb" = xyes])

AC_CONFIG_SUBDIRS([lib/json-c])
AC_CONFIG_FILES([das.spec Makefile etc/Makefile das/Makefile lib/Makefile lib/librtmp/Makefile])
AC_OUTPUT
