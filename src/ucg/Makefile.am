#
# Copyright (c) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
# See file LICENSE for terms.
#

SUBDIRS = base over_ucp over_uct over_ucb

lib_LTLIBRARIES    = libucg.la
libucg_la_CFLAGS   = $(BASE_CFLAGS)
libucg_la_CPPFLAGS = $(BASE_CPPFLAGS) --param=max-vartrack-size=2000000000
libucg_la_LDFLAGS  = -ldl -version-info $(SOVERSION)
libucg_ladir       = $(includedir)/ucg
libucg_la_SOURCES  =
libucg_la_LIBADD   = \
	../ucs/libucs.la \
	../uct/libuct.la \
	../ucp/libucp.la \
	../ucb/libucb.la \
	base/libucg_base.la \
	over_ucp/libucg_over_ucp.la \
	over_uct/libucg_over_uct.la \
	over_ucb/libucg_over_ucb.la

nobase_dist_libucg_la_HEADERS = \
	api/ucg_def.h \
	api/ucg_version.h \
	api/ucg_minimal.h \
	api/ucg_pubsub.h \
	api/ucg_mpi.h \
	api/ucg.h

noinst_HEADERS = \
	api/ucg_plan_component.h
