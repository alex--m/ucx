/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 *
 * See file LICENSE for terms.
 */

#ifndef UCF_POSIX_H_
#define UCF_POSIX_H_

#include <ucs/type/status.h>
#include <ucs/config/types.h>
#include <stddef.h>
#include <stdint.h>
#include <aio.h>

int     ucf_open(const char *pathname, int flags, mode_t mode);
int     ucf_creat(const char *pathname, mode_t mode);
int     ucf_openat(int dirfd, const char *pathname, int flags, mode_t mode);
int     ucf_close(int);
ssize_t ucf_pread(int, void *, size_t, off_t);
ssize_t ucf_pwrite(int, const void *, size_t, off_t);
ssize_t ucf_read(int, void *, size_t);
ssize_t ucf_write(int, const void *, size_t);
int     ucf_aio_read(struct aiocb *);
int     ucf_aio_write(struct aiocb *);

#endif
