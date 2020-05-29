#include "ipc.h"
#include "common.c"
#include "ipc-unix.h"
#include "ipc-windows.h"
#include <math.h>
#include <string.h>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

static const char test[] =
	"R 3:cmd -123 [ 23 3:abc ] nan inf -inf 1|\n 3:cde abcdp3\n";

int main(int argc, const char *argv[])
{
	sipc_parser_t p;
	sipc_init(&p, test, (int)strlen(test));
	print_message(&p);

	char buf[1024];

#ifdef _WIN32
	void *pipe = ipc_win_connect("\\\\.\\pipe\\ipc-test", false);
	if (pipe == INVALID_HANDLE_VALUE) {
		fprintf(stderr, "failed to connect %d\n", GetLastError());
		return 2;
	}

	fprintf(stderr, "sending %s\n", test);
	DWORD written;
	if (!WriteFile(pipe, test, (DWORD)strlen(test), &written, NULL)) {
		fprintf(stderr, "write failed%d\n", GetLastError());
	}

	WriteFile(pipe, test, (DWORD)strlen(test), &written, NULL);

	void *read, *write;
	if (!CreatePipe(&read, &write, NULL, 0)) {
		fprintf(stderr, "failed to create pipe %d\n", GetLastError());
		return 3;
	}

	int n = ipc_append_handle(pipe, buf, sizeof(buf), write);
	if (n < 0) {
		return 4;
	}
	CloseHandle(write);
	n += sipc_format(buf + n, sizeof(buf) - n, "R 3:cmd %d %f %f\n", -123,
		    312132.1f, NAN);
	fprintf(stderr, "sending %s\n", buf);
	if (!WriteFile(pipe, buf, n, &written, NULL)) {
		fprintf(stderr, "write failed%d\n", GetLastError());
	}
	fprintf(stderr, "waiting\n");
	ReadFile(read, buf, sizeof(buf), &written, NULL);
	buf[written] = '\0';
	fprintf(stderr, "wait complete: %s\n", buf);

#else
	int fd = ipc_unix_connect("sock");
	if (fd < 0) {
		perror("connect");
		return 2;
	}

	send(fd, test, strlen(test), 0);

	int fds[2];
	pipe(fds);

	int n = sipc_format(buf, sizeof(buf), "R 3:cmd %d %f %f\n", -123,
			    312132.1f, NAN);
	ipc_unix_sendmsg(fd, buf, n, &fds[1], 1);
	close(fds[1]);
	read(fds[0], buf, sizeof(buf));
#endif
	return 0;
}
