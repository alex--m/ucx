#
# Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
# See file LICENSE for terms.
#

#
# TODO: test for Core-Direct support in UCT
#
coredirect_happy=yes

AM_CONDITIONAL([HAVE_CD], [test "x$coredirect_happy" = "xyes"])

AS_IF([test "x$coredirect_happy" = "xyes"],
      [AS_MESSAGE([core-direct batches are supported])
       AC_CONFIG_FILES([src/ucb/coredirect/Makefile])],
      [:])

