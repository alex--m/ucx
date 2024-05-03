#
# Copyright (c) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
# See file LICENSE for terms.
#

noinst_LTLIBRARIES          = libucg_over_ucb.la
libucg_over_ucb_la_CFLAGS   = $(BASE_CFLAGS)
libucg_over_ucb_la_CPPFLAGS = $(BASE_CPPFLAGS)
libucg_over_ucb_la_LDFLAGS  = -ldl -version-info $(SOVERSION)

noinst_HEADERS = over_ucb.h

libucg_over_ucb_la_SOURCES = \
	over_ucb.c \
	over_ucb_plan.c
