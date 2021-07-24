#
# Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
# See file LICENSE for terms.
#

ucb_modules=":builtin"
m4_include([src/ucb/base/configure.m4])
m4_include([src/ucb/builtin/configure.m4])
AC_DEFINE_UNQUOTED([ucb_MODULES], ["${ucb_modules}"], [UCB loadable modules])

AC_CONFIG_FILES([src/ucb/Makefile
                 src/ucb/api/ucb_version.h
                 src/ucb/base/ucb_version.c])
