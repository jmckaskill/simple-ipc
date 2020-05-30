#ifndef _WIN32
#include "ipc-unix.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/un.h>
#include <sys/socket.h>

#define SCM_MAX_FDS 255

struct sockaddr *ipc_new_unix_addr(const char *path, int *psasz)
{
	struct sockaddr_un *un;
	size_t psz = strlen(path);
	socklen_t sasz = sizeof(*un) - sizeof(un->sun_path) + psz + 1;
	un = malloc(sasz);
	un->sun_family = AF_UNIX;
	memcpy(un->sun_path, path, psz + 1);
	*psasz = sasz;
	return (struct sockaddr *)un;
}

int ipc_unix_connect(const char *path)
{
	int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (fd < 0) {
		return -1;
	}

	int sasz;
	struct sockaddr *sa = ipc_new_unix_addr(path, &sasz);
	int err = connect(fd, sa, sasz);
	free(sa);
	if (err) {
		close(fd);
		return -1;
	}

	return fd;
}

int ipc_unix_listen(const char *path)
{
	int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (fd < 0) {
		return -1;
	}

	unlink(path);

	int sasz;
	struct sockaddr *sa = ipc_new_unix_addr(path, &sasz);
	int err = bind(fd, sa, sasz);
	free(sa);
	if (err || listen(fd, SOMAXCONN)) {
		close(fd);
		return -1;
	}

	return fd;
}

int ipc_unix_sendmsg(int fd, const char *buf, int sz, const int *fds, int fdn)
{
	union {
		struct cmsghdr hdr;
		char buf[CMSG_SPACE(SCM_MAX_FDS * sizeof(int))];
	} control;
	struct iovec iov = {
		.iov_base = (char *)buf,
		.iov_len = sz,
	};
	struct msghdr msg = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
	};

	if (fdn > SCM_MAX_FDS) {
		return -1;
	} else if (fdn) {
		msg.msg_controllen = CMSG_SPACE(fdn * sizeof(*fds));
		msg.msg_control = control.buf;
		struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		cmsg->cmsg_len = CMSG_LEN(fdn * sizeof(*fds));
		memcpy(CMSG_DATA(cmsg), fds, fdn * sizeof(*fds));
	}

	return (int)sendmsg(fd, &msg, 0);
}

int ipc_unix_recvmsg(int fd, char *buf, int sz, int *fds, int *fdn)
{
	union {
		struct cmsghdr hdr;
		char buf[CMSG_SPACE(SCM_MAX_FDS * sizeof(int))];
	} control;
	struct iovec iov = {
		.iov_base = buf,
		.iov_len = sz,
	};
	struct msghdr msg = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
	};

	if (fdn && *fdn) {
		msg.msg_controllen = sizeof(control);
		msg.msg_control = control.buf;
	}

	int r = recvmsg(fd, &msg, 0);
	if (r >= 0 && fdn && *fdn) {
		int n = 0;
		for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
		     cmsg = CMSG_NXTHDR(&msg, cmsg)) {
			if (cmsg->cmsg_level == SOL_SOCKET &&
			    cmsg->cmsg_type == SCM_RIGHTS) {
				unsigned char *p = CMSG_DATA(cmsg);
				unsigned char *e = p + cmsg->cmsg_len;
				for (; p + sizeof(int) < e; p += sizeof(int)) {
					int fd;
					memcpy(&fd, p, sizeof(fd));
					if (fd > 0 && n < *fdn) {
						fds[n++] = fd;
					} else if (fd > 0) {
						close(fd);
					}
				}
			}
		}
		*fdn = n;
	}
	return r;
}

#endif