#include "rpc.c"
#include <ctype.h>

static void test_hex()
{
	assert(is_valid_hex('/') == 0);
	assert(is_valid_hex('0') && hex_value('0') == 0x0);
	assert(is_valid_hex('1') && hex_value('1') == 0x1);
	assert(is_valid_hex('2') && hex_value('2') == 0x2);
	assert(is_valid_hex('3') && hex_value('3') == 0x3);
	assert(is_valid_hex('4') && hex_value('4') == 0x4);
	assert(is_valid_hex('5') && hex_value('5') == 0x5);
	assert(is_valid_hex('6') && hex_value('6') == 0x6);
	assert(is_valid_hex('7') && hex_value('7') == 0x7);
	assert(is_valid_hex('8') && hex_value('8') == 0x8);
	assert(is_valid_hex('9') && hex_value('9') == 0x9);
	assert(is_valid_hex(':') == 0);
	assert(is_valid_hex('A') == 0);
	assert(is_valid_hex('@') == 0);
	assert(is_valid_hex('a') && hex_value('a') == 0xa);
	assert(is_valid_hex('b') && hex_value('b') == 0xb);
	assert(is_valid_hex('c') && hex_value('c') == 0xc);
	assert(is_valid_hex('d') && hex_value('d') == 0xd);
	assert(is_valid_hex('e') && hex_value('e') == 0xe);
	assert(is_valid_hex('f') && hex_value('f') == 0xf);
	assert(is_valid_hex('g') == 0);
}

static void do_format(const char *expect, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

static void do_format(const char *expect, const char *fmt, ...)
{
	char buf[256], buf2[256];
	va_list ap, aq;
	va_start(ap, fmt);

	va_copy(aq, ap);
	vsprintf(buf2, fmt, aq);
	va_end(aq);

	int n = srpc_vformat(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	fprintf(stderr, "got: %i '%s', expect: %zu '%s', input: '%s' -> '%s'\n",
		n, n >= 0 ? buf : "", strlen(expect), expect, fmt, buf2);
	assert(n == strlen(expect) && !strcmp(expect, buf));
}

static double subnormal()
{
	union {
		uint64_t u;
		double d;
	} u;
	u.u = 20;
	return u.d;
}

static void test_format()
{
	// bool
	do_format("T", "%o", true);
	do_format("F", "%o", false);
	// uint
	do_format("0", "%u", 0);
	do_format("ff", "%u", 0xff);
	do_format("1p8", "%u", 0x100);
	do_format("180", "%u", 0x180);
	do_format("1pc", "%u", 0x1000);
	do_format("1p1f", "%u", 0x80000000);
	// int
	do_format("-ff", "%i", -0xff);
	do_format("-7p1c", "%i", -0x70000000);
	// double
	do_format("1abcdp-e", "%f", 0x1abcdp-14);
	do_format("nan", "%f", NAN);
	do_format("inf", "%f", INFINITY);
	do_format("-inf", "%f", -INFINITY);
	do_format("0", "%f", -0.0);
	do_format("80", "%f", 128.0);
	do_format("1p8", "%f", 256.0);
	do_format("0", "%g", -subnormal());
	// string
	do_format("3:abc", "%s", "abc");
	do_format("3:abc", "%*s", 3, "abc");
	// bytes
	do_format("3|123", "%*p", 3, "123");
	// any
	srpc_any_t any;
	any.type = SRPC_DOUBLE;
	any.d = 0x1abcdp-14;
	do_format("1abcdp-e", "%p", &any);
}

static void test_parse()
{
	srpc_parser_t p;
	p.next =
		"T F 0 ff 1p8 180 1pc 1p1f -ff -7p1c 1abcdp-e nan inf -inf 0 80 1p8 3:abc 3|123 1abcdp-e ";
	p.end = p.next + strlen(p.next);

	bool b;
	assert(!srpc_bool(&p, &b) && b == true);
	assert(!srpc_bool(&p, &b) && b == false);

	unsigned u;
	assert(!srpc_uint(&p, &u) && u == 0);
	assert(!srpc_uint(&p, &u) && u == 0xff);
	assert(!srpc_uint(&p, &u) && u == 0x100);
	assert(!srpc_uint(&p, &u) && u == 0x180);
	assert(!srpc_uint(&p, &u) && u == 0x1000);
	assert(!srpc_uint(&p, &u) && u == 0x80000000);

	int i;
	assert(!srpc_int(&p, &i) && i == -0xff);
	assert(!srpc_int(&p, &i) && i == -0x70000000);

	double d;
	assert(!srpc_double(&p, &d) && d == 0x1abcdp-14);
	assert(!srpc_double(&p, &d) && isnan(d));
	assert(!srpc_double(&p, &d) && isinf(d) && d > 0);
	assert(!srpc_double(&p, &d) && isinf(d) && d < 0);
	assert(!srpc_double(&p, &d) && d == 0.0);
	assert(!srpc_double(&p, &d) && d == 128.0);
	assert(!srpc_double(&p, &d) && d == 256.0);

	const char *string;
	assert(!srpc_string(&p, &i, &string) && i == 3 &&
	       !strncmp(string, "abc", 3));

	const unsigned char *bytes;
	assert(!srpc_bytes(&p, &i, &bytes) && i == 3 &&
	       !strncmp((char *)bytes, "123", 3));

	srpc_any_t any;
	assert(!srpc_any(&p, &any) && any.type == SRPC_DOUBLE &&
	       any.d == 0x1abcdp-14);

	assert(!srpc_any(&p, &any) && any.type == SRPC_END);
	assert(p.next == p.end && !*p.next);
}

int main()
{
	test_hex();
	test_format();
	test_parse();
	return 0;
}
