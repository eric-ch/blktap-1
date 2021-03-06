/*
 * Copyright (c) 2016, Citrix Systems, Inc.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the copyright holder nor the names of its 
 *     contributors may be used to endorse or promote products derived from 
 *     this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "tap-ctl.h"

int
_tap_ctl_stats_connect_and_send(pid_t pid, int minor)
{
	struct timeval timeout = { .tv_sec = 10, .tv_usec = 0 };
	tapdisk_message_t message;
	int sfd, err;

	err = tap_ctl_connect_id(pid, &sfd);
	if (err)
		return err;

	memset(&message, 0, sizeof(message));
	message.type   = TAPDISK_MESSAGE_STATS;
	message.cookie = minor;

	err = tap_ctl_write_message(sfd, &message, &timeout);
	if (err) {
		close(sfd);
		return err;
	}

	return sfd;
}

ssize_t
tap_ctl_stats(pid_t pid, int minor, char *buf, size_t size)
{
	tapdisk_message_t message;
	int sfd, err;
	size_t len;

	sfd = _tap_ctl_stats_connect_and_send(pid, minor);
	if (sfd < 0)
		return sfd;

	err = tap_ctl_read_message(sfd, &message, NULL);
	if (err)
		goto out;

	len = message.u.info.length;
	if (size < len + 1)
		len = size - 1;

	err = tap_ctl_read_raw(sfd, buf, len, NULL);
	if (err)
		goto out;

	buf[len] = 0;

out:
	close(sfd);
	return err;
}

int
tap_ctl_stats_fwrite(pid_t pid, int minor, FILE *stream)
{
	tapdisk_message_t message;
	int sfd = -1, prot, flags, err;
	size_t len, bufsz;
	char *buf = MAP_FAILED;

	prot  = PROT_READ|PROT_WRITE;
	flags = MAP_ANONYMOUS|MAP_PRIVATE;

	err = sysconf(_SC_PAGE_SIZE);
	if (err == -1) {
		err = -errno;
		goto out;
	}

	bufsz = err;

	buf = mmap(NULL, bufsz, prot, flags, -1, 0);
	if (buf == MAP_FAILED) {
		err = -ENOMEM;
		goto out;
	}

	sfd = _tap_ctl_stats_connect_and_send(pid, minor);
	if (sfd < 0) {
		err = sfd;
		goto out;
	}

	err = tap_ctl_read_message(sfd, &message, NULL);
	if (err)
		goto out;

	len = message.u.info.length;
	while (len) {
		fd_set rfds;
		size_t in, out;
		int n;

		FD_ZERO(&rfds);
		FD_SET(sfd, &rfds);

		n = select(sfd + 1, &rfds, NULL, NULL, NULL);
		if (n < 0) {
			err = n;
			goto out;
		}

		in = read(sfd, buf, bufsz);
		if (in <= 0) {
			err = in;
			goto out;
		}

		len -= in;

		out = fwrite(buf, in, 1, stream);
		if (out != 1) {
			err = -errno;
			goto out;
		}
	}

	if (fwrite("\n", 1, 1, stream) != 1) {
		err = -EIO;
	}

out:
	if (sfd >= 0)
		close(sfd);
	if (buf != MAP_FAILED)
		munmap(buf, bufsz);

	return err;
}
