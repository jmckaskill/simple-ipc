#include "rpc.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <threads.h>

static int handler_thread(void *arg)
{
	int fd = (int)(uintptr_t)arg;
	for (;;) {
		srpc_parser_t p;
		char buf[4096];
		int r = recv(fd, buf, sizeof(buf), 0);
		if (r <= 0) {
			break;
		} else if (srpc_init(&p, buf, r)) {
			fprintf(stderr, "got init error\n");
			break;
		}

		for (;;) {
			srpc_any_t v;
			if (srpc_any(&p, &v)) {
				fprintf(stderr, "got error\n");
				break;
			} else if (v.type == SRPC_END) {
				fprintf(stderr, "END\n");
				break;
			}
			switch (v.type) {
			case SRPC_NEGATIVE_INT:
				fprintf(stderr, "INT -%llu\n",
					(unsigned long long)v.n);
				break;
			case SRPC_POSITIVE_INT:
				fprintf(stderr, "INT %llu\n",
					(unsigned long long)v.n);
				break;
			case SRPC_DOUBLE:
				fprintf(stderr, "DOUBLE %f %a\n", v.d, v.d);
				break;
			case SRPC_STRING:
				fprintf(stderr, "STRING '%.*s'\n", v.string.n,
					v.string.s);
				break;
			case SRPC_BYTES:
				fprintf(stderr, "BYTES '%.*s'\n", v.bytes.n,
					(char *)v.bytes.p);
				break;
			case SRPC_ARRAY:
				fprintf(stderr, "ARRAY '%.*s'\n",
					(int)(v.array.end - v.array.next),
					v.array.next);
				break;
			case SRPC_MAP:
				fprintf(stderr, "MAP '%.*s'\n",
					(int)(v.map.end - v.map.next),
					v.map.next);
				break;
			case SRPC_REFERENCE:
				fprintf(stderr, "REF '%.*s'\n", v.reference.n,
					(char *)v.reference.p);
				break;
			default:
				fprintf(stderr, "UNKNOWN %d\n", v.type);
				break;
			}
		}
	}
	close(fd);
	return 0;
}

int main()
{
	struct sockaddr_un un;
	un.sun_family = AF_UNIX;
	strcpy(un.sun_path, "sock");

	unlink("sock");

	int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (fd < 0) {
		perror("socket");
		return 2;
	}
	if (bind(fd, (struct sockaddr *)&un, sizeof(un))) {
		perror("bind");
		return 3;
	}
	if (listen(fd, SOMAXCONN)) {
		perror("listen");
		return 4;
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
