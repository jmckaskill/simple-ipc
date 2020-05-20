#define _GNU_SOURCE
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

struct parser {
	const char *next;
	const char *end;
};

static int parse_dec_int(const char **p, int negate, int *pv)
{
	char ch = **p;
	if (!negate && ch == '0') {
		// leading zeros are not supported so this can only be '0'
		*pv = 0;
		(*p)++;
		return 0;
	} else if (ch < '1' || ch > '9') {
		return -1;
	}

	unsigned u = 0;
	do {
		u = (u * 10) + (ch - '0');
		if (u - negate < 0) {
			// overflow
			// u can not be 0 as the lone 0 is handled above
			// if u < 0, then u is greater than INT_MAX
			// if u - 1 < 0 then u is greater than -INT_MIN
			return -1;
		}
		ch = *(++(*p));
	} while ('0' <= ch && ch <= '9' && u < INT_MAX / 10);

	*pv = (1 - 2 * negate) * (int)u;
	return 0;
}

static int parse_int(const char **p, int *pv)
{
	int negate = (**p == '-');
	*p += negate;

	return parse_dec_int(p, negate, pv) || *((*p)++) != ' ';
}

static const int8_t hex_lookup[] = {
	0,  1,	2,  3,	4,  5,	6,  7, // 30-37
	8,  9,	-1, -1, -1, -1, -1, -1, // 38-4F
	-1, -1, -1, -1, -1, -1, -1, -1, // 40-47
	-1, -1, -1, -1, -1, -1, -1, -1, // 48-4F
	-1, -1, -1, -1, -1, -1, -1, -1, // 50-57
	-1, -1, -1, -1, -1, -1, -1, -1, // 58-5F
	-1, 10, 11, 12, 13, 14, 15, -1, // 60-67
};

static int parse_uint64(const char **p, uint64_t *pv)
{
	// use -1 to ensure we get at least one character
	unsigned count = (unsigned)-1;
	uint64_t v = 0;

	// strip 0x prefix
	if (*((*p)++) != '0' || *((*p)++) != 'x') {
		return -1;
	}

	for (;;) {
		unsigned char ch = **p;
		ch -= 0x30;
		if (ch >= sizeof(hex_lookup) || hex_lookup[ch] < 0) {
			if (count >= (sizeof(uint64_t) * 2)) {
				return -1;
			}
			*pv = v;
			return *((*p)++) != ' ';
		}
		v = (v << 4) + hex_lookup[ch];
		(*p)++;
		count++;
	}
}

static int parse_hex_float(const char **p, int negate, uint64_t *pfrac,
			   unsigned *pfracbits, int *pexp)
{
	*pfrac = 0;
	*pfracbits = 0;
	*pexp = 0;

	// Expect 0x prefix
	if (*((*p)++) != '0' || *((*p)++) != 'x') {
		return -1;
	}

	switch (*((*p)++)) {
	case '0':
		// special case for 0.0 = 0x0p+0
		// negative zero and subnormals are not supported
		return *((*p)++) != 'p' || *((*p)++) != '+' ||
		       *((*p)++) != '0' || *((*p)++) != ' ';
	case '1':
		break;
	default:
		return -1;
	}

	// Unpack the fraction part if it's present
	if (**p == '.') {
		(*p)++;
		uint64_t v = 0;
		unsigned shift = 64;
		do {
			unsigned char ch = **p;
			ch -= 0x30;
			if (ch >= sizeof(hex_lookup) || hex_lookup[ch] < 0) {
				break;
			}
			(*p)++;
			shift -= 4;
			v |= ((uint64_t)hex_lookup[ch] << shift);
		} while (shift);
		*pfracbits = 64 - shift;
		*pfrac = v;
	}

	if (*((*p)++) != 'p') {
		return -1;
	}

	int expnegate;
	switch (*(*p)++) {
	case '+':
		expnegate = 0;
		break;
	case '-':
		expnegate = 1;
		break;
	default:
		return -1;
	}

	// Unpack the exponent and trailing space
	return parse_dec_int(p, expnegate, pexp) || *((*p)++) != ' ';
}

static int parse_float32(const char **p, float *pv)
{
	if ((*p)[0] == 'n' && (*p)[1] == 'a' && (*p)[2] == 'n' &&
	    (*p)[3] == ' ') {
		*p += 4;
		*pv = (float)NAN;
		return 0;
	}

	unsigned fracbits;
	uint64_t frac;
	unsigned uexp;
	int exp;

	int negate = (**p == '-');
	*p += negate;

	if ((*p)[0] == 'i' && (*p)[1] == 'n' && (*p)[2] == 'f' &&
	    (*p)[3] == ' ') {
		*p += 4;
		goto infinity;
	}

	if (parse_hex_float(p, negate, &frac, &fracbits, &exp)) {
		return -1;
	}

	// fractional part is 23 bits
	// round up if the 24 bit is set
	if (fracbits > 23 && (frac & (1 << (32 - 24)))) {
		frac += 1 << (32 - 24);
	}

	if (exp < -126) {
		// subnormal - round to zero
		frac = 0;
		uexp = 0;
	} else if (exp > 127) {
	infinity:
		// too large - round to infinity
		frac = 0;
		uexp = 0xFF;
	} else {
		// encode into signed bias form
		uexp = (unsigned)(127 + exp);
	}

	// now encode the result
	union {
		uint32_t u;
		float f;
	} u;

	u.u = (((uint32_t)negate) << 31) | (((uint32_t)uexp) << 23) |
	      (uint32_t)(frac >> (32 - 23));
	*pv = u.f;
	return 0;
}

static int parse_float64(const char **p, int negate, double *pv)
{
	unsigned fracbits;
	uint64_t frac;
	unsigned uexp;
	int exp;

	if (!negate && (*p)[0] == 'n' && (*p)[1] == 'a' && (*p)[2] == 'n' &&
	    (*p)[3] == ' ') {
		*p += 4;
		*pv = NAN;
		return 0;
	}

	if ((*p)[0] == 'i' && (*p)[1] == 'n' && (*p)[2] == 'f' &&
	    (*p)[3] == ' ') {
		*p += 4;
		goto infinity;
	}

	if (parse_hex_float(p, negate, &frac, &fracbits, &exp)) {
		return -1;
	}

	// fractional part is 52 bits
	// round up if the 53 bit is set
	if (fracbits > 52 && (frac & (1 << (64 - 53)))) {
		frac += 1 << 11;
	}

	if (exp < -1022) {
		// subnormal - round to zero
		frac = 0;
		uexp = 0;
	} else if (exp > 1023) {
	infinity:
		// too large - round to infinity
		frac = 0;
		uexp = 0x7FF;
	} else {
		// encode into signed bias form
		uexp = (unsigned)(1023 + exp);
	}

	// now encode the result
	union {
		uint64_t u;
		double f;
	} u;

	u.u = (((uint64_t)negate) << 63) | (((uint64_t)uexp) << 52) |
	      (frac >> (64 - 52));
	*pv = u.f;
	return 0;
}

static int parse_word(const char **p, const char **pv)
{
	*pv = *p;
	if (**p < 'a' || **p > 'z') {
		return -1;
	}
	(*p)++;
	while (('a' <= **p && **p <= 'z') || **p == '-') {
		(*p)++;
	}
	return *(*p)++ != ' ';
}

static int parse_szstring(struct parser *p, char delim, int *psz,
			  const char **pv)
{
	int sz;
	if (parse_dec_int(&p->next, 0, &sz)) {
		return -1;
	}
	if (*(p->next++) != delim || sz >= (p->end - p->next)) {
		return -1;
	}
	*psz = sz;
	*pv = p->next;
	p->next += sz;
	return *(p->next++) != ' ';
}

static int parse_string(struct parser *p, int *psz, const char **pv)
{
	return parse_szstring(p, ':', psz, pv);
}

static int parse_bytes(struct parser *p, int *psz, const unsigned char **pv)
{
	return parse_szstring(p, '|', psz, (const char **)pv);
}

static inline int is_word(const char *word, const char *test)
{
	return !strncmp(word, test, strlen(test));
}

enum any_type {
	ANY_EOL,
	ANY_INT,
	ANY_UINT64,
	ANY_DOUBLE,
	ANY_WORD,
	ANY_STRING,
	ANY_BYTES,
	ANY_REFERENCE,
	ANY_ARRAY,
	ANY_MAP,
};

struct any {
	union {
		int i;
		uint64_t u;
		double d;
		struct {
			int n;
			const char *s;
		} string;
		struct {
			int n;
			const unsigned char *p;
		} bytes;
		struct parser array, map;
	} value;
	enum any_type type;
};

static int do_parse_any(struct parser *p, struct any *pv, int *pdepth)
{
	int negate = 0;

test_char:
	switch (*p->next) {
	case '\n':
		p->next++;
		pv->type = ANY_EOL;
		return 0;

	case '-':
		// negative integer (-...) or float (-0x... or -inf)
		if (negate) {
			return -1;
		}
		p->next++;
		negate = 1;
		goto test_char;

	case '[':
	case '{':
		p->next++;
		(*pdepth)++;
		return *(p->next++) != ' ';

	case ']':
	case '}':
		p->next++;
		(*pdepth)--;
		return *(p->next++) != ' ';

	case 'n':
	case 'i':
	parse_double:
		// NAN or INF
		pv->type = ANY_DOUBLE;
		return parse_float64(&p->next, negate, &pv->value.d);

	default:
		if (p->next[1] == 'x') {
			goto parse_double;
		} else {
			// integer, bytes, or string
			if (parse_dec_int(&p->next, negate, &pv->value.i)) {
				return -1;
			}
			switch (*(p->next++)) {
			case '|':
				pv->type = ANY_BYTES;
				goto parse_string_data;
			case ':':
				pv->type = ANY_STRING;
			parse_string_data:
				pv->value.string.s = p->next;
				if (negate) {
					return -1;
				} else if (pv->value.i >
					   (p->end - p->next) + 1) {
					p->next = p->end;
					return -1;
				}
				p->next += pv->value.i;
				return (*(p->next++) != ' ');

			case '@':
				pv->type = ANY_REFERENCE;
				return (*(p->next++) != ' ');

			case ' ':
				pv->type = ANY_INT;
				return 0;

			default:
				return -1;
			}
		}
	}
}

int parse_any(struct parser *p, struct any *pv)
{
	int depth = 0;
	char ch = *p->next;
	if (do_parse_any(p, pv, &depth) || depth < 0) {
		return -1;
	} else if (!depth) {
		// normal type
		return 0;
	}

	// container type
	pv->type = (ch == '[') ? ANY_ARRAY : ANY_MAP;
	pv->value.array.next = p->next;

	do {
		struct any dummy;
		if (do_parse_any(p, &dummy, &depth)) {
			return -1;
		}
	} while (depth);

	pv->value.array.end = p->next - 2;

	return 0;
}

static int format_uint(char *p, unsigned v)
{
	// print in reverse order
	char buf[10]; // strlen("4294967296") = 10
	int i = sizeof(buf);
	do {
		buf[--i] = '0' + (v % 10);
		v /= 10;
	} while (v);

	// shift back
	memcpy(p, buf + i, sizeof(buf) - i);
	return sizeof(buf) - i;
}

static int format_uint64(char *p, uint64_t v)
{
	// print in reverse order
	char buf[20]; // strlen("18446744073709551616") = 20
	int i = sizeof(buf);
	do {
		buf[--i] = '0' + (v % 10);
		v /= 10;
	} while (v);

	// shift back
	memcpy(p, buf + i, sizeof(buf) - i);
	return sizeof(buf) - i;
}

static int format_int(char *p, int v)
{
	int negate = v < 0;
	if (negate) {
		*(p++) = '-';
		v = -v;
	}
	return negate + format_uint(p, (unsigned)v);
}

static int format_int64(char *p, int64_t v)
{
	int negate = v < 0;
	if (negate) {
		*(p++) = '-';
		v = -v;
	}
	return negate + format_uint64(p, (uint64_t)v);
}

static const char hex_chars[] = "0123456789abcdef";

static int format_double(char *p, double v)
{
	union {
		double f;
		uint64_t u;
	} u;
	u.f = v;

	int sign = (int)(u.u >> 63);
	unsigned uexp = (u.u >> 52) & 0x7FF;
	uint64_t frac = u.u << 12;

	int i = 0;
	if (uexp == 0) {
		// zero and subnormals - encode as zero
		if (sign) {
			p[i++] = '-';
		}
		p[i++] = '0';
		p[i++] = 'x';
		p[i++] = '0';
		p[i++] = 'p';
		p[i++] = '+';
		p[i++] = '0';
		return i;
	} else if (uexp == 0x7FF) {
		// infinity & nan
		if (frac) {
			p[i++] = 'n';
			p[i++] = 'a';
			p[i++] = 'n';
			return i;
		} else {
			if (sign) {
				p[i++] = '-';
			}
			p[i++] = 'i';
			p[i++] = 'n';
			p[i++] = 'f';
			return i;
		}
	} else {
		// normal
		if (sign) {
			p[i++] = '-';
		}
		int exp = (int)uexp - 1023;
		p[i++] = '0';
		p[i++] = 'x';
		p[i++] = '1';
		if (frac) {
			p[i++] = '.';
			do {
				p[i++] = hex_chars[frac >> 60];
				frac <<= 4;
			} while (frac);
		}
		p[i++] = 'p';
		if (exp > 0) {
			p[i++] = '+';
		}
		return i + format_int(p + i, exp);
	}
}

static int format_string(char *p, int bufsz, char delim, int n, const char *v)
{
	// format the size
	int hdr = format_int(p, n);
	*(p++) = delim;

	int ret = 1 + hdr + n;

	if (ret <= bufsz) {
		memcpy(p, v, n);
		p += n;
	}

	return ret;
}

static int format_arg(char *p, int bufsz, const char **pfmt, va_list ap)
{
	int n;
	const char *str;

	switch (*((*pfmt)++)) {
	case 'l':
		// %lld or %llu
		if (*((*pfmt)++) != 'l') {
			return -1;
		}
		switch (*((*pfmt)++)) {
		case 'd':
			return format_int64(p, va_arg(ap, int64_t));
		case 'u':
			return format_uint64(p, va_arg(ap, uint64_t));
		default:
			return -1;
		}
	case 'd':
		return format_int(p, va_arg(ap, int));
	case 'u':
		return format_uint(p, va_arg(ap, unsigned));
	case 'a':
		return format_double(p, va_arg(ap, double));
	case '%':
		// %% escaped percent
		*(p++) = '%';
		return 1;
	case '*':
		// %*s (string) and %*p (bytes)
		n = va_arg(ap, int);
		str = va_arg(ap, const char *);
		switch (*((*pfmt)++)) {
		case 's':
			return format_string(p, bufsz, ':', n, str);
		case 'p':
			return format_string(p, bufsz, '|', n, str);
		default:
			return -1;
		}
	case '.':
		// %.*s - raw copy
		if (*((*pfmt)++) != '*' || *((*pfmt)++) != 's') {
			return -1;
		}
		n = va_arg(ap, int);
		if (n <= bufsz) {
			str = va_arg(ap, const char *);
			memcpy(p, str, n);
		}
		return n;
	case 's':
		// %s - raw copy
		str = va_arg(ap, const char *);
		n = strlen(str);
		if (n <= bufsz) {
			memcpy(p, str, n);
		}
		return n;
	default:
		return -1;
	}
}

// INT64: -9223372036854775808
// DBL:   -0x1.123456789abcdp-1022

#define MAX_ATOM_SIZE 24

static int rpc_vformat(char *p, int bufsz, const char *fmt, va_list ap)
{
	int sz = 0;

	while (sz + MAX_ATOM_SIZE < bufsz) {
		char ch = *(fmt++);
		int n;

		switch (ch) {
		case '\0':
			p[sz] = '\0';
			return sz;
		case '%':
			n = format_arg(p + sz, bufsz - sz, &fmt, ap);
			if (n < 0) {
				// format error
				return -1;
			}
			sz += n;
			continue;
		default:
			// raw character
			p[sz++] = ch;
			continue;
		}
	}

	// ran out of buffer - lets see how much room we need

	for (;;) {
		char dummy[MAX_ATOM_SIZE + 1];
		int n;

		switch (*(fmt++)) {
		case '\0':
			sz++; // for terminating null
			return (sz > bufsz) ? sz : (bufsz + 1);
		case '%':
			// print to a dummy buffer to see how much room it actually took up
			n = format_arg(dummy, MAX_ATOM_SIZE, &fmt, ap);
			if (n < 0) {
				return -1;
			}
			sz += n;
			break;
		default:
			sz++;
			break;
		}
	}
}

static int rpc_format(char *buf, int bufsz, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int ret = rpc_vformat(buf, bufsz, fmt, ap);
	va_end(ap);
	return ret;
}

static const char test[] =
	"3:cmd 123 [ 23 3:abc ] nan inf -inf 1|\n 3:cde 0x1.abcdp+3 \n";

int main(int argc, const char *argv[])
{
	char buf[1024];
	int n = rpc_format(buf, sizeof(buf), "cmd %d %a %a \n", -123,
			   3121321321.1, NAN);
	printf("%.*s %a %a\n", n, buf, 3121321321.1, -0.0);

	struct parser p = { test, test + strlen(test) };
	for (;;) {
		struct any v;
		if (parse_any(&p, &v)) {
			printf("got error\n");
			return 2;
		}
		switch (v.type) {
		case ANY_EOL:
			printf("EOL\n");
			return 0;
		case ANY_INT:
			printf("INT %d\n", v.value.i);
			break;
		case ANY_UINT64:
			printf("UINT64 %#llx\n", v.value.u);
			break;
		case ANY_DOUBLE:
			printf("DOUBLE %f %a\n", v.value.d, v.value.d);
			break;
		case ANY_WORD:
			printf("WORD '%.*s'\n", v.value.string.n,
			       v.value.string.s);
			break;
		case ANY_STRING:
			printf("STRING '%.*s'\n", v.value.string.n,
			       v.value.string.s);
			break;
		case ANY_BYTES:
			printf("BYTES '%.*s'\n", v.value.bytes.n,
			       (char *)v.value.bytes.p);
			break;
		case ANY_ARRAY:
			printf("ARRAY '%.*s'\n",
			       (int)(v.value.array.end - v.value.array.next),
			       v.value.array.next);
			break;

		case ANY_MAP:
			printf("MAP '%.*s'\n",
			       (int)(v.value.array.end - v.value.array.next),
			       v.value.array.next);
			break;
		default:
			printf("UNKNOWN %d\n", v.type);
			return 3;
		}
	}
}
