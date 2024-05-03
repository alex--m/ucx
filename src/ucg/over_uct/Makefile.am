#
# Copyright (c) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
# See file LICENSE for terms.
#

noinst_LTLIBRARIES          = libucg_over_uct.la
libucg_over_uct_la_CFLAGS   = $(BASE_CFLAGS)
libucg_over_uct_la_CPPFLAGS = $(BASE_CPPFLAGS) -Wno-address-of-packed-member # TODO: remove the suppression

noinst_HEADERS = \
	over_uct.h \
	over_uct_comp.inl

libucg_over_uct_la_SOURCES = \
	over_uct.c \
	over_uct_execute.c \
	over_uct_optimize.c \
	over_uct_pack.c \
	over_uct_phase.c \
	over_uct_plan.c \
	over_uct_print.c
