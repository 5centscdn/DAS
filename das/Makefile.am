# $Id$

LIBS = -lpthread

sbin_PROGRAMS = dasd

dasd_SOURCES = das.c common.c rtmp.c

noinst_HEADERS = \
        common.h

dasd_LDADD = \
	$(top_builddir)/lib/librtmp/librtmp.a \
	$(top_builddir)/lib/json-c/.libs/libjson.a

