#
# Copyright (c) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
# See file LICENSE for terms.
#

if HAVE_UCB

SUBDIRS = base builtin

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
	base/libucb_base.la \
	builtin/libucb_builtin.la

nobase_dist_libucb_la_HEADERS = \
	api/ucb_def.h \
	api/ucb_version.h \
	api/ucb.h

endif # HAVE_UCB
