#
# Copyright (c) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
# See file LICENSE for terms.
#

SUBDIRS = base over_ucg

lib_LTLIBRARIES    = libucf.la
libucf_la_CFLAGS   = $(BASE_CFLAGS)
libucf_la_CPPFLAGS = $(BASE_CPPFLAGS)
libucf_la_LDFLAGS  = -ldl -version-info $(SOVERSION)
libucf_ladir       = $(includedir)/ucf
libucf_la_SOURCES  =
libucf_la_LIBADD   = \
	../ucs/libucs.la \
	../uct/libuct.la \
	../ucp/libucp.la \
	../ucg/libucg.la \
	base/libucf_base.la \
	over_ucg/libucf_over_ucg.la

nobase_dist_libucf_la_HEADERS = \
	api/ucf_def.h \
	api/ucf_version.h \
	api/ucf_posix.h \
	api/ucf.h
