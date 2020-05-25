#include "rpc.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int main()
{
	struct sockaddr_un un;
	un.sun_family = AF_UNIX;
	strcpy(un.sun_path, "sock");

	unlink("sock");

	int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	bind(fd, (struct sockaddr *)&un, sizeof(un));

	for (;;) {
		char buf[4096];
		int r = recv(fd, buf, sizeof(buf), 0);
		srpc_parser p;
		int used = 0;
		while (used < r) {
			int n = srpc_unpack(&p, buf + used, r - used);
			if (n <= 0) {
				fprintf(stderr, "got unpack error\n");
				break;
			}
			used += n;

			for (;;) {
				struct srpc_any v;
				if (srpc_any(&p, &v)) {
					fprintf(stderr, "got error\n");
					break;
				} else if (v.type == SRPC_END) {
					fprintf(stderr, "END\n");
					break;
				}
				switch (v.type) {
				case SRPC_INT:
					fprintf(stderr, "INT %d\n", v.i);
					break;
				case SRPC_INT64:
					fprintf(stderr, "INT %lld\n", v.llong);
					break;
				case SRPC_UINT64:
					fprintf(stderr, "UINT64 %llu\n",
						v.ullong);
					break;
				case SRPC_DOUBLE:
					fprintf(stderr, "DOUBLE %f %a\n", v.d,
						v.d);
					break;
				case SRPC_STRING:
					fprintf(stderr, "STRING '%.*s'\n",
						v.string.n, v.string.s);
					break;
				case SRPC_BYTES:
					fprintf(stderr, "BYTES '%.*s'\n",
						v.bytes.n, (char *)v.bytes.p);
					break;
				case SRPC_ARRAY:
					fprintf(stderr, "ARRAY '%.*s'\n",
						(int)(v.array.end -
						      v.array.next),
						v.array.next);
					break;
				case SRPC_MAP:
					fprintf(stderr, "MAP '%.*s'\n",
						(int)(v.map.end - v.map.next),
						v.map.next);
					break;
				case SRPC_REFERENCE:
					fprintf(stderr, "REF %zu\n", v.ref);
					break;
				default:
					fprintf(stderr, "UNKNOWN %d\n", v.type);
					break;
				}
			}
		}
	}
	return 0;
}
