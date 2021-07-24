#
# Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
# See file LICENSE for terms.
#

#
# Enable UCB - Batch communication component
#

ucb_modules=""

m4_sinclude([src/ucb/configure.m4]) # m4_sinclude() silently ignores errors

AC_ARG_ENABLE([ucb],
              AS_HELP_STRING([--enable-ucb],
                             [Enable batch communication operations (experimental component), default: NO]),
              [],
              [enable_ucb=no])
AS_IF([test "x$enable_ucb" != xno],
      [ucb_modules=":builtin"
       AC_DEFINE([ENABLE_UCB], [1],
                 [Enable batch communication support (UCB)])
       AC_MSG_NOTICE([Building with batch communication support (UCB)])
      ])
AS_IF([test -f ${ac_confdir}/src/ucb/Makefile.am],
      [AC_SUBST([UCB_SUBDIR], [src/ucb])])

AM_CONDITIONAL([HAVE_UCB], [test "x$enable_ucb" != xno])
