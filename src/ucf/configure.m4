#
# Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
# See file LICENSE for terms.
#

ucf_modules=":builtin"
m4_include([src/ucf/base/configure.m4])
m4_include([src/ucf/builtin/configure.m4])
AC_DEFINE_UNQUOTED([ucf_MODULES], ["${ucf_modules}"], [UCF loadable modules])

AC_CONFIG_FILES([src/ucf/Makefile
                 src/ucf/api/ucf_version.h
                 src/ucf/base/ucf_version.c])
