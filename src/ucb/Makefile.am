#
# Copyright (c) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
# See file LICENSE for terms.
#

SUBDIRS = base

lib_LTLIBRARIES    = libucb.la
libucb_la_CFLAGS   = $(BASE_CFLAGS)
libucb_la_CPPFLAGS = $(BASE_CPPFLAGS)
libucb_la_LDFLAGS  = -ldl -version-info $(SOVERSION)
libucb_ladir       = $(includedir)/ucb
libucb_la_SOURCES  =
libucb_la_LIBADD   = \
	../ucs/libucs.la \
	../uct/libuct.la \
	../ucp/libucp.la \
	base/libucb_base.la

if HAVE_CD
SUBDIRS += coredirect
libucb_la_LIBADD += coredirect/libucb_coredirect.la
endif

nobase_dist_libucb_la_HEADERS = \
	api/ucb_def.h \
	api/ucb_version.h \
	api/ucb.h
