#include "rpc.h"
#include <stdio.h>
#include <string.h>

int main()
{
	struct srpc_any any;
	any.type = SRPC_ARRAY;
	any.array.next = "12 34 56 ";
	any.array.end = any.array.next + strlen(any.array.next);

	char buf[128];
	int sz = srpc_format(buf, sizeof(buf), "000|%*s %a %d %p\r\n",
		       	3, "foo", 0.125, -25, &any);
	srpc_pack(buf, sz);
	fwrite(buf, 1, sz, stdout);

	static char test[] = "017|3:cmd 0x1.abcdp+3\r\n";
	srpc_parser p;
	int msgsz = srpc_unpack(&p, test, sizeof(test));
	printf("%zx %d\n", sizeof(test), msgsz);

	for (;;) {
		struct srpc_any v;
		if (srpc_any(&p, &v)) {
			printf("got error\n");
			return 2;
		}
		switch (v.type) {
		case SRPC_END:
			printf("END\n");
			return 0;
		case SRPC_INT:
			printf("INT %d\n", v.i);
			break;
		case SRPC_INT64:
			printf("INT %lld\n", v.i64);
			break;
		case SRPC_UINT64:
			printf("UINT64 %#llu\n", v.u64);
			break;
		case SRPC_DOUBLE:
			printf("DOUBLE %f %a\n", v.d, v.d);
			break;
		case SRPC_STRING:
			printf("STRING '%.*s'\n", v.string.n, v.string.s);
			break;
		case SRPC_BYTES:
			printf("BYTES '%.*s'\n", v.bytes.n, (char *)v.bytes.p);
			break;
		case SRPC_ARRAY:
			printf("ARRAY '%.*s'\n",
			       (int)(v.array.end - v.array.next), v.array.next);
			break;
		case SRPC_MAP:
			printf("MAP '%.*s'\n", (int)(v.map.end - v.map.next),
			       v.map.next);
			break;
		case SRPC_REFERENCE:
			printf("REF %zu\n", v.ref);
			break;
		default:
			printf("UNKNOWN %d\n", v.type);
			break;
		}
	}
	return 0;
}
