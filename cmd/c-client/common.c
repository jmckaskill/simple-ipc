#include "ipc.h"
#include <stdio.h>

static void print_message(sipc_parser_t *p)
{
	for (;;) {
		enum sipc_msg_type msg = sipc_start(p);
		if (!msg) {
			break;
		}
		fprintf(stderr, "msg start %c\n", msg);

		for (;;) {
			sipc_any_t v;
			if (sipc_any(p, &v)) {
				fprintf(stderr, "got error\n");
				break;
			} else if (v.type == SIPC_END) {
				fprintf(stderr, "END\n");
				break;
			}
			switch (v.type) {
			case SIPC_NEGATIVE_INT:
				fprintf(stderr, "INT -%llu\n",
					(unsigned long long)v.n);
				break;
			case SIPC_POSITIVE_INT:
				fprintf(stderr, "INT %llu\n",
					(unsigned long long)v.n);
				break;
			case SIPC_DOUBLE:
				fprintf(stderr, "DOUBLE %f %a\n", v.d, v.d);
				break;
			case SIPC_STRING:
				fprintf(stderr, "STRING '%.*s'\n", v.string.n,
					v.string.s);
				break;
			case SIPC_BYTES:
				fprintf(stderr, "BYTES '%.*s'\n", v.bytes.n,
					(char *)v.bytes.p);
				break;
			case SIPC_ARRAY:
				fprintf(stderr, "ARRAY '%.*s'\n",
					(int)(v.array.end - v.array.next),
					v.array.next);
				break;
			case SIPC_MAP:
				fprintf(stderr, "MAP '%.*s'\n",
					(int)(v.map.end - v.map.next),
					v.map.next);
				break;
			default:
				fprintf(stderr, "UNKNOWN %d\n", v.type);
				break;
			}
		}
	}
}