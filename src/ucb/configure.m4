#
# Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
# See file LICENSE for terms.
#

ucb_modules=""
m4_include([src/ucb/coredirect/configure.m4])
AC_DEFINE_UNQUOTED([ucb_MODULES], ["${ucb_modules}"], [UCB loadable modules])

AC_CONFIG_FILES([src/ucb/Makefile
                 src/ucb/base/Makefile
                 src/ucb/api/ucb_version.h
                 src/ucb/base/ucb_version.c])
