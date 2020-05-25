#include "rpc.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <math.h>

static const char test[] =
	"000 3:cmd 123 [ 23 3:abc ] nan inf -inf 1|\n 3:cde 0x1.abcdp+3;\n";

int main(int argc, const char *argv[])
{
	struct sockaddr_un un;
	un.sun_family = AF_UNIX;
	strcpy(un.sun_path, "sock");

	int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	connect(fd, (struct sockaddr *)&un, sizeof(un));

	char buf[1024];
	strcpy(buf, test);
	srpc_pack(buf, strlen(test));
	send(fd, buf, strlen(test), 0);

	int n = srpc_format(buf, sizeof(buf), "000 3:cmd %d %a %a;\n", -123,
			    3121321321.1, NAN);
	srpc_pack(buf, n);
	send(fd, buf, n, 0);
	return 0;
}
