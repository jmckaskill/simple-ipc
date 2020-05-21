#define _GNU_SOURCE
#include "rpc.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <assert.h>

static int parse_dec_uint(const char **pp, unsigned *pv)
{
	const char *p = *pp;
	char ch = *p;
	if (ch == '0') {
		// leading zeros are not supported so this can only be '0'
		*pv = 0;
		*pp = p + 1;
		return 0;
	} else if (ch < '1' || ch > '9') {
		return -1;
	}

	unsigned u = 0;
	do {
		u = (u * 10) + (ch - '0');
		if (u < 0) {
			// protect against + (ch - '0') causing overflow
			// the < UINT_MAX / 10 below protects (u * 10)
			return -1;
		}
		ch = *(++p);
	} while ('0' <= ch && ch <= '9' && u < UINT_MAX / 10);

	*pv = u;
	*pp = p;
	return 0;
}

static int parse_dec_uint64(const char **pp, uint64_t *pv)
{
	const char *p = *pp;
	char ch = *p;
	if (ch == '0') {
		// leading zeros are not supported so this can only be '0'
		*pv = 0;
		*pp = p + 1;
		return 0;
	} else if (ch < '1' || ch > '9') {
		return -1;
	}

	uint64_t u = 0;
	do {
		u = (u * 10) + (ch - '0');
		if (u < 0) {
			return -1;
		}
		ch = *(++p);
	} while ('0' <= ch && ch <= '9' && u < UINT64_MAX / 10);

	*pv = u;
	*pp = p;
	return 0;
}

int srpc_parse_int(srpc_parser *p, int *pv)
{
	int negate = (*p->next == '-');
	p->next += negate;

	unsigned u;
	if (parse_dec_uint(&p->next, &u)) {
		return -1;
	}
	if (*(p->next++) != ' ' || (int)(u - negate) < 0) {
		return -1;
	}
	*pv = (1 - 2 * negate) * (int)u;
	return 0;
}

int srpc_parse_uint(srpc_parser *p, unsigned *pv)
{
	return parse_dec_uint(&p->next, pv) || *(p->next++) != ' ';
}

int srpc_parse_int64(srpc_parser *p, int64_t *pv)
{
	int negate = (*p->next == '-');
	p->next += negate;

	uint64_t u;
	if (parse_dec_uint64(&p->next, &u)) {
		return -1;
	}
	if (*(p->next++) != ' ' || (int64_t)(u - negate) < 0) {
		return -1;
	}
	*pv = (1 - 2 * negate) * (int64_t)u;
	return 0;
}

int srpc_parse_uint64(srpc_parser *p, uint64_t *pv)
{
	return parse_dec_uint64(&p->next, pv) || *(p->next++) != ' ';
}

int srpc_reference(srpc_parser *p, uintptr_t *pv)
{
#if UINTPTR_MAX > UINT_MAX
	uint64_t u64;
	int err = parse_dec_uint64(&p->next, &u64);
	*pv = (uintptr_t)u64;
#else
	unsigned u;
	int err = parse_dec_uint(&p->next, &u);
	*pv = (uintptr_t)u;
#endif
	return err || (*p->next++) != '@' || (*p->next++) != ' ';
}

static const uint8_t hex_lookup[] = {
	0x00, 0x00, 0x00, 0x00, // Valid 00-1F
	0x00, 0x00, 0xFF, 0x03, // Valid 20-3F
	0x00, 0x00, 0x00, 0x00, // Valid 40-5F, Lookup 00-07 (not used)
	0x7E, 0x00, 0x00, 0x00, // Valid 60-7F, Lookup 08-0F (not used)
	0x00, 0x00, 0x00, 0x00, // Valid 80-9F, Lookup 10-17 (not used)
	0x00, 0x00, 0x00, 0x00, // Valid A0-BF, Lookup 18-1F (not used)
	0x00, 0x00, 0x00, 0x00, // Valid C0-DF, Lookup 20-27 (not used)
	0x00, 0x00, 0x00, 0x00, // Valid E0-FF, Lookup 28-2F (not used)
	0x10, 0x32, 0x54, 0x76, // Valid not used, Lookup 30-37
	0x98, 0x00, 0x00, 0x00, // Valid not used, Lookup 38-3f (not used)
	0x00, 0x00, 0x00, 0x00, // Valid not used, Lookup 40-47 (not used)
	0x00, 0x00, 0x00, 0x00, // Valid not used, Lookup 48-4F (not used)
	0x00, 0x00, 0x00, 0x00, // Valid not used, Lookup 50-57 (not used)
	0x00, 0x00, 0x00, 0x00, // Valid not used, Lookup 58-5f (not used)
	0xa0, 0xcb, 0xed, 0x0f, // Valid not used, Lookup 60-67
};

static inline int is_valid_hex(char ch)
{
	// Top 5 bits indicates index. Bottom 3 bits indicates
	// which bit within that to use.
	unsigned char uch = (unsigned char)ch;
	return hex_lookup[uch >> 3] & (1 << (uch & 7));
}

static inline int hex_value(char ch)
{
	// Top 7 bits indicates index. Botom bit indicates which
	// nibble within the lookup to use. Character has already
	// been verified to be a valid hex byte.
	return (hex_lookup[8+(ch >> 1)] >> ((ch & 1) << 2)) & 15;
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
			char ch = **p;
			if (!is_valid_hex(ch)) {
				break;
			}
			(*p)++;
			shift -= 4;
			v |= ((uint64_t)hex_value(ch) << shift);
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

	// Unpack the exponent
	unsigned uexp;
	if (parse_dec_uint(p, &uexp) || *((*p)++) != ' ') {
		return -1;
	}
	if ((int)(uexp - expnegate) < 0) {
		return -1; // overflow on conversion to int
	}
	*pexp = (1 - 2 * expnegate) * (int)uexp;
	return 0;
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

int srpc_float32(srpc_parser *p, float *pv)
{
	return parse_float32(&p->next, pv);
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

int srpc_float64(srpc_parser *p, double *pv)
{
	int negate = (*p->next == '-');
	p->next += negate;
	return parse_float64(&p->next, negate, pv);
}

static int parse_szstring(srpc_parser *p, char delim, int *psz, const char **pv)
{
	unsigned sz;
	if (parse_dec_uint(&p->next, &sz)) {
		return -1;
	}
	if ((int)sz < 0 || *(p->next++) != delim || sz >= (p->end - p->next)) {
		return -1;
	}
	*psz = (int)sz;
	*pv = p->next;
	p->next += sz;
	return *(p->next++) != ' ';
}

int srpc_string(srpc_parser *p, int *pn, const char **ps)
{
	return parse_szstring(p, ':', pn, ps);
}

int srpc_bytes(srpc_parser *p, int *pn, const unsigned char **pp)
{
	return parse_szstring(p, ';', pn, (const char **)pp);
}

int srpc_next(srpc_parser *p, struct srpc_any *pv)
{
	int negate = 0;
	uint64_t num;

test_char:
	switch (*p->next) {
	case '\n':
		p->next++;
	case '\0':
		pv->type = SRPC_END;
		return 0;

	case '-':
		// negative integer (-...) or float (-0x... or -inf)
		if (negate) {
			return -1;
		}
		p->next++;
		negate = 1;
		goto test_char;

	case '{':
		pv->type = SRPC_MAP;
		goto consume_space;
	case '[':
		pv->type = SRPC_ARRAY;
		goto consume_space;
	case '}':
		pv->type = SRPC_END;
		goto consume_space;
	case ']':
		pv->type = SRPC_END;
	consume_space:
		p->next++;
		return *(p->next++) != ' ';

	case '0':
		if (p->next[1] != 'x') {
			num = 0;
			p->next++;
			goto parse_number;
		}
		// fallthrough
	case 'n': // nan
	case 'i': // inf
		pv->type = SRPC_DOUBLE;
		return parse_float64(&p->next, negate, &pv->d);

	default:
		if (parse_dec_uint64(&p->next, &num)) {
			return -1;
		}
		// fallthrough
	parse_number:
		// integer, bytes, or string
		switch (*(p->next++)) {
		case ';':
			pv->type = SRPC_BYTES;
			goto parse_string_data;
		case ':':
			pv->type = SRPC_STRING;
		parse_string_data:
			if (negate || num > INT_MAX) {
				return -1;
			} else if (num > (p->end - p->next) + 1) {
				p->next = p->end;
				return -1;
			}
			pv->string.s = p->next;
			pv->string.n = (int)num;
			p->next += pv->string.n;
			return (*(p->next++) != ' ');

		case '@':
			pv->ref = (uintptr_t)num;
			pv->type = SRPC_REFERENCE;
			return negate || (num > (uint64_t)UINTPTR_MAX) ||
			       (*(p->next++) != ' ');

		case ' ':
			if (num < (uint64_t)INT_MAX) {
				pv->i = (1 - 2 * negate) * (int)num;
				pv->type = SRPC_INT;
				return 0;
			} else if (num < (uint64_t)INT64_MAX) {
				pv->i64 = (1 - 2 * negate) * (int64_t)num;
				pv->type = SRPC_INT64;
				return 0;
			} else {
				pv->u64 = num;
				pv->type = SRPC_UINT64;
				return negate;
			}

		default:
			return -1;
		}
	}
}

int srpc_any(srpc_parser *p, struct srpc_any *pv)
{
	char ch = *p->next;
	if (srpc_next(p, pv)) {
		return -1;
	}

	if (pv->type == SRPC_ARRAY || pv->type == SRPC_MAP) {
		pv->array.next = p->next;
		int depth = 1;
		enum srpc_type t;
		do {
			struct srpc_any dummy;
			if (srpc_next(p, &dummy)) {
				return -1;
			}
			switch (dummy.type) {
			case SRPC_ARRAY:
			case SRPC_MAP:
				depth++;
				break;
			case SRPC_END:
				depth--;
				break;
			}
		} while (depth);
		pv->array.end = p->next - 2;
	}

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
	int hdr = format_uint(p, (unsigned)n);
	p += hdr;
	*(p++) = delim;

	int ret = 1 + hdr + n;

	if (ret <= bufsz) {
		memcpy(p, v, n);
		p += n;
	}

	return ret;
}

static int format_array(char *p, int bufsz, char open, char close, const char *start, const char *end)
{
	int n = (int)(end-start);
	int need = 2 + n + 1;
	if (need <= bufsz) {
		*(p++) = open;
		*(p++) = ' ';
		memcpy(p, start, n);
		p += n;
		*(p++) = close;
	}
	return need;
}

static int format_arg(char *p, int bufsz, const char **pfmt, va_list ap)
{
	int n;
	const char *str;
	const struct srpc_any *any;

	switch (*((*pfmt)++)) {
	case 'p':
		// any
		any = va_arg(ap, const struct srpc_any*);
		switch (any->type) {
		case SRPC_INT:
			return format_int(p, any->i);
		case SRPC_INT64:
			return format_int64(p, any->i64);
		case SRPC_UINT64:
			return format_uint64(p, any->u64);
		case SRPC_DOUBLE:
			return format_double(p, any->d);
		case SRPC_STRING:
			return format_string(p, bufsz, ':', any->string.n, any->string.s);
		case SRPC_BYTES:
			return format_string(p, bufsz, ';', any->bytes.n, (const char*)any->bytes.p);
		case SRPC_ARRAY:
			return format_array(p, bufsz, '[', ']', any->array.next, any->array.end);
		case SRPC_MAP:
			return format_array(p, bufsz, '{', '}', any->map.next, any->map.end);
		case SRPC_REFERENCE:
			n = format_uint(p, any->ref);
			*(p++) = '@';
			return n + 1;
		default:
			return -1;
		}
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
			return format_string(p, bufsz, ';', n, str);
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

int srpc_vformat(char *p, int bufsz, const char *fmt, va_list ap)
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

int srpc_format(char *buf, int bufsz, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int ret = srpc_vformat(buf, bufsz, fmt, ap);
	va_end(ap);
	return ret;
}

void srpc_pack(char *buf, int sz)
{
	assert(6 <= sz && sz <= 4096 && buf[sz - 2] == '\r' &&
	       buf[sz - 1] == '\n' && buf[3] == '|');
	buf[0] = hex_chars[sz >> 12];
	buf[1] = hex_chars[(sz >> 8) & 15];
	buf[2] = hex_chars[sz & 15];
}

int srpc_unpack(srpc_parser *p, char *buf, int sz)
{
	if (sz < 6) {
		// need more data to be able to parse the header
		return 0;
	}

	int v1 = is_valid_hex(buf[0]);
	int v2 = is_valid_hex(buf[1]);
	int v3 = is_valid_hex(buf[2]);
	int v4 = (buf[3] == '|');
	if (!(v1 && v2 && v3 && v4)) {
		// invalid or extended header
		return -1;
	}

	int msgsz = (hex_value(buf[0]) << 8) | (hex_value(buf[1]) << 4) |
		    hex_value(buf[2]);

	if (sz < msgsz) {
		return 0;
	} else if (msgsz < 6 || buf[msgsz - 2] != '\r' ||
		   buf[msgsz - 1] != '\n') {
		return -1;
	}
	// we have a valid complete message
	buf[msgsz - 2] = ' ';
	buf[msgsz - 1] = '\0';
	p->next = &buf[4];
	p->end = &buf[msgsz - 1];
	return msgsz;
}

