#include "common.c"
#include "ipc.h"
#include "unix.h"
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <threads.h>

static int handler_thread(void *arg)
{
	int fd = (int)(uintptr_t)arg;
	for (;;) {
		int fds[1], fdn = 1;
		char buf[4096];
		int r = ipc_unix_recvmsg(fd, buf, sizeof(buf), fds, &fdn);
		if (r <= 0) {
			break;
		}
		print_message(buf, r);
		if (fdn) {
			fprintf(stderr, "have fd %d\n", fds[0]);
			usleep(4*1000*1000);
			close(fds[0]);
		}
	}
	close(fd);
	return 0;
}

int main()
{
	int fd = ipc_unix_listen("sock");
	if (fd < 0) {
		perror("listen");
		return 2;
	}

	for (;;) {
		int cfd = accept(fd, NULL, NULL);
		if (cfd < 0) {
			perror("accept");
			break;
		}
		thrd_t thread;
		if (thrd_create(&thread, &handler_thread,
				(void *)(uintptr_t)cfd)) {
			perror("thread");
			break;
		}
		thrd_detach(thread);
	}
	return 0;
}
