#include "rpc.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <math.h>

static const char test[] =
	"3:cmd -123 [ 23 3:abc ] nan inf -inf 1|\n 3:cde abcdp3;\n";

int main(int argc, const char *argv[])
{
	struct sockaddr_un un;
	un.sun_family = AF_UNIX;
	strcpy(un.sun_path, "sock");

	int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	connect(fd, (struct sockaddr *)&un, sizeof(un));

	send(fd, test, strlen(test), 0);

	char buf[1024];
	int n = srpc_format(buf, sizeof(buf), "3:cmd %d %f %f;\n", -123,
			    3121321321.1, NAN);
	send(fd, buf, n, 0);
	return 0;
}
