#
# Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
# See file LICENSE for terms.
#

ucf_modules=":over_ucg"
# TODO: m4_include([src/ucf/over_uct/configure.m4])
AC_DEFINE_UNQUOTED([ucf_MODULES], ["${ucf_modules}"], [UCF loadable modules])

AC_CONFIG_FILES([src/ucf/Makefile
                 src/ucf/base/Makefile
                 src/ucf/over_ucg/Makefile
                 src/ucf/api/ucf_version.h
                 src/ucf/base/ucf_version.c])
