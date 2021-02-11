#
# Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
# See file LICENSE for terms.
#

#
# Enable UCF - File collective operations component
#

ucf_modules=""

m4_sinclude([src/ucf/configure.m4]) # m4_sinclude() silently ignores errors

AC_ARG_ENABLE([ucf],
              AS_HELP_STRING([--enable-ucf],
                             [Enable the parallel file operations (experimental component), default: NO]),
              [],
              [enable_ucf=no])
AS_IF([test "x$enable_ucf" != xno],
      [ucf_modules=":builtin"
       AC_DEFINE([ENABLE_UCF], [1],
                 [Enable parallel file operations support (UCF)])
       AC_MSG_NOTICE([Building with parallel file operations support (UCF)])
      ])
AS_IF([test -f ${ac_confdir}/src/ucf/Makefile.am],
      [AC_SUBST([UCF_SUBDIR], [src/ucf])])

AM_CONDITIONAL([HAVE_UCF], [test "x$enable_ucf" != xno])
