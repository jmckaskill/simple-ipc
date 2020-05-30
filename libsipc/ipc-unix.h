#pragma once

struct sockaddr;
struct sockaddr *ipc_new_unix_addr(const char *path, int *psasz);

// returns file descriptor or -ve on error
int ipc_unix_connect(const char *path);
int ipc_unix_listen(const char *path);

// returns zero on success, non-zero on error
int ipc_unix_sendmsg(int fd, const char *buf, int sz, const int *fds, int fdn);

// returns # of bytes received
// 0 on close
// -ve on error - check errno
// fdn is an inout value
int ipc_unix_recvmsg(int fd, char *buf, int sz, int *fds, int *fdn);
