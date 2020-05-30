#include "../c-client/common.c"
#include "ipc.h"
#include "ipc-unix.h"
#include "ipc-windows.h"
#include <string.h>

#ifdef _MSC_VER
#include "tinycthread/source/tinycthread.h"
#else
#include <threads.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#endif

static int handler_thread(void *arg)
{
	fprintf(stderr, "in thread\n");
#ifdef _WIN32
	void *pipe = arg;
	DWORD read;
	char buf[4096];
	for (;;) {
		fprintf(stderr, "try read\n");
		if (!ReadFile(pipe, buf, sizeof(buf), &read, NULL)) {
			fprintf(stderr, "read failed %d\n", GetLastError());
			break;
		}
		sipc_parser_t p;
		buf[read] = '\0';
		fprintf(stderr, "read %s\n", buf);
		if (sipc_init(&p, buf, read)) {
			fprintf(stderr, "failed to parse message\n");
			continue;
		}
		void *handles[1];
		int hnum = ipc_parse_handles(&p, handles, 1);
		if (hnum < 0) {
			fprintf(stderr, "failed to retrieve handles\n");
			continue;
		}
		print_message(&p);
		if (hnum) {
			fprintf(stderr, "have handle %p\n", handles[0]);
			fprintf(stderr, "waiting\n");
			struct timespec duration = { .tv_sec = 4 };
			thrd_sleep(&duration, NULL);
			fprintf(stderr, "wait complete\n");
			WriteFile(handles[0], "done", 4, &read, NULL);
			CloseHandle(handles[0]);
		}
	}
	CloseHandle(pipe);
#else
	int fd = (int)(uintptr_t)arg;
	for (;;) {
		int fds[1], fdn = 1;
		char buf[4096];
		int r = ipc_unix_recvmsg(fd, buf, sizeof(buf), fds, &fdn);
		if (r <= 0) {
			break;
		}
		if (fdn) {
			fprintf(stderr, "have fd %d\n", fds[0]);
			struct timespec duration = {
				.tv_sec = 4,
			};
			thrd_sleep(&duration, NULL);
			write(fds[0], "hello", 5);
			close(fds[0]);
		}
		sipc_parser_t p;
		if (sipc_init(&p, buf, r)) {
			fprintf(stderr, "failed to parse message\n");
			continue;
		}
		print_message(&p);
	}
	close(fd);
#endif
	return 0;
}

int main()
{
#ifdef _WIN32
	for (;;) {
		void *pipe = ipc_win_accept("\\\\.\\pipe\\ipc-test", false);
		if (pipe == INVALID_HANDLE_VALUE) {
			fprintf(stderr, "accept failed %d\n", GetLastError());
			continue;
		}
		thrd_t thread;
		if (thrd_create(&thread, &handler_thread, pipe) !=
		    thrd_success) {
			fprintf(stderr, "failed to create thread %d\n",
				GetLastError());
			CloseHandle(pipe);
			continue;
		}
		thrd_detach(thread);
	}
#else
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
				(void *)(uintptr_t)cfd) != thrd_success) {
			perror("thread");
			break;
		}
		thrd_detach(thread);
	}
#endif
	return 0;
}
