#
# Copyright (c) NVIDIA CORPORATION & AFFILIATES, 2001-2018. ALL RIGHTS RESERVED.
# See file LICENSE for terms.
#

uct_modules=""
m4_include([src/uct/cuda/configure.m4])
m4_include([src/uct/ib/configure.m4])
m4_include([src/uct/rocm/configure.m4])
m4_include([src/uct/sm/configure.m4])
m4_include([src/uct/ugni/configure.m4])

AC_DEFINE_UNQUOTED([uct_MODULES], ["${uct_modules}"], [UCT loadable modules])

AC_CONFIG_FILES([src/uct/Makefile
                 src/uct/ucx-uct.pc])

#
# TCP flags
#
AC_CHECK_DECLS([IPPROTO_TCP, SOL_SOCKET, SO_KEEPALIVE,
                TCP_KEEPCNT, TCP_KEEPIDLE, TCP_KEEPINTVL],
               [],
               [tcp_keepalive_happy=no],
               [[#include <netinet/tcp.h>]
                [#include <netinet/in.h>]])
AS_IF([test "x$tcp_keepalive_happy" != "xno"],
      [AC_DEFINE([UCT_TCP_EP_KEEPALIVE], 1, [Enable TCP keepalive configuration])]);

#
# Shared-memory Collectives Support
#
AC_ARG_WITH([sm_coll],
            [AS_HELP_STRING([--with-sm-coll], [Compile with shared-memory collectives support])],
            [], [with_sm_coll=no])

AC_ARG_WITH([sm_coll_extra],
            [AS_HELP_STRING([--with-sm-coll-extra], [Compile with extra shared-memory collectives])],
            [], [with_sm_coll_extra=no])

AS_IF([test "x$with_sm_coll" != xno],
      [AC_DEFINE([HAVE_SM_COLL], 1, [Shared-memory collectives])
       AS_IF([test "x$with_sm_coll_extra" != xno],
             [AC_DEFINE([HAVE_SM_COLL_EXTRA], 1, [Extra shared-memory collectives])])])
AM_CONDITIONAL([HAVE_SM_COLL], [test "x$with_sm_coll" != xno])

#
# UD Multicast Collectives Support
#
AC_ARG_WITH([mcast_coll],
            [AS_HELP_STRING([--with-mcast-coll], [Compile with UD multicast collectives support])],
            [], [with_mcast_coll=no])

AS_IF([test "x$with_mcast_coll" != xno],
      [AC_DEFINE([HAVE_MCAST_COLL], 1, [UD Multicast collectives])])
AM_CONDITIONAL([HAVE_MCAST_COLL], [test "x$with_mcast_coll" != xno])