#include "ipc.h"
#include "common.c"
#include "unix.h"
#include <math.h>
#include <unistd.h>
#include <string.h>

static const char test[] =
	"R 3:cmd -123 [ 23 3:abc ] nan inf -inf 1|\n 3:cde abcdp3\n";

int main(int argc, const char *argv[])
{
	print_message(test, strlen(test));

	int fd = ipc_unix_connect("sock");
	if (fd < 0) {
		perror("connect");
		return 2;
	}

	send(fd, test, strlen(test), 0);

	int fds[2];
	pipe(fds);

	char buf[1024];
	int n = sipc_format(buf, sizeof(buf), "R 3:cmd %d %f %f\n", -123,
			    312132.1f, NAN);
	ipc_unix_sendmsg(fd, buf, n, &fds[1], 1);
	close(fds[1]);
	read(fds[0], buf, sizeof(buf));
	return 0;
}
