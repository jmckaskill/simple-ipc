#include "rpc.h"
#include <math.h>

static const char test[] =
	"3:cmd 123 [ 23 3:abc ] nan inf -inf 1|\n 3:cde 0x1.abcdp+3 \n";

int main(int argc, const char *argv[])
{
	char buf[1024];
	int n = rpc_format(buf, sizeof(buf), "cmd %d %a %a \n", -123,
			   3121321321.1, NAN);
	printf("%.*s %a %a\n", n, buf, 3121321321.1, -0.0);

	srpc_parser p = { test, test + strlen(test) };
	for (;;) {
		union srpc_any v;
		enum srpc_type t;
		if (parse_any(&p, t, &v)) {
			printf("got error\n");
			return 2;
		}
		switch (t) {
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
			printf("UNKNOWN %d\n", t);
			break;
		}
	}
}
