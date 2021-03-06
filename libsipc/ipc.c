#define _GNU_SOURCE
#include "ipc.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <assert.h>

#ifdef _MSC_VER
#pragma warning(disable : 26451) // overzealous arithmetic overflow check
#include <intrin.h>
#pragma intrinsic(_BitScanForward64)
#pragma intrinsic(_BitScanReverse64)
static int leading_zeros_64(uint64_t v)
{
	unsigned long ret = 0;
	_BitScanReverse64(&ret, v);
	return 63 - (int)ret;
}
static int trailing_zeros_64(uint64_t v)
{
	unsigned long ret = 0;
	_BitScanForward64(&ret, v);
	return (int)ret;
}
#elif defined(__GNUC__) || defined(__clang__)
static int leading_zeros_64(uint64_t v)
{
	return __builtin_clzll(v);
}
static int trailing_zeros_64(uint64_t v)
{
	return __builtin_ctzll(v);
}
#endif

static const uint8_t hex_valid[] = {
	0x00, 0x00, 0x00, 0x00, // 00-1F
	0x00, 0x00, 0xFF, 0x03, // 20-3F
	0x00, 0x00, 0x00, 0x00, // 40-5F
	0x7E, 0x00, 0x00, 0x00, // 60-7F
	0x00, 0x00, 0x00, 0x00, // 80-9F
	0x00, 0x00, 0x00, 0x00, // A0-BF
	0x00, 0x00, 0x00, 0x00, // C0-DF
	0x00, 0x00, 0x00, 0x00, // E0-FF
};
static const uint8_t hex_lookup[] = {
	0x00, 0x0a, 0x0b, 0x0c, //
	0x0d, 0x0e, 0x0f, 0x00, //
	0x00, 0x00, 0x00, 0x00, //
	0x00, 0x00, 0x00, 0x00, //
	0x00, 0x01, 0x02, 0x03, //
	0x04, 0x05, 0x06, 0x07, //
	0x08, 0x09, 0x00, 0x00, //
	0x00, 0x00, 0x00, 0x00, //
};
// '0' - 30h - 00110000b & 11111b = 10000b - 10h
// '1' - 31h - 00110001b & 11111b = 10001b - 11h
// '2' - 32h - 00110010b & 11111b = 10010b - 12h
// '3' - 33h - 00110011b & 11111b = 10011b - 13h
// '4' - 34h - 00110100b & 11111b = 10100b - 14h
// '5' - 35h - 00110101b & 11111b = 10101b - 15h
// '6' - 36h - 00110110b & 11111b = 10110b - 16h
// '7' - 37h - 00110111b & 11111b = 10111b - 17h
// '8' - 38h - 00111000b & 11111b = 11000b - 18h
// '9' - 39h - 00111001b & 11111b = 11001b - 19h
// 'a' - 61h - 01100001b & 11111b = 00001b - 01h
// 'b' - 62h - 01100010b & 11111b = 00010b - 02h
// 'c' - 63h - 01100011b & 11111b = 00011b - 03h
// 'd' - 64h - 01100100b & 11111b = 00100b - 04h
// 'e' - 65h - 01100101b & 11111b = 00101b - 05h
// 'f' - 66h - 01100110b & 11111b = 00110b - 06h

static inline int is_valid_hex(char ch)
{
	// Top 5 bits indicates index. Bottom 3 bits indicates
	// which bit within that to use.
	unsigned char uch = (unsigned char)ch;
	return hex_valid[uch >> 3] & (1 << (uch & 7));
}

static inline int hex_value(char ch)
{
	// Index given by ch & 11111b. See lookup above
	// ch must be a valid character per is_valid_hex
	return hex_lookup[ch & 0x1F];
}

// parse_uint parses a lowercase hex string and puts the result in pv
// returns 0 on success
// -ve on error
// +ve overflow - the return count is the number of excess bits
static int parse_hex(sipc_parser_t *p, uint64_t *pv)
{
	char ch = *(p->next++);
	if (ch == '0') {
		// Leading zeros are not supported so this can only be '0'.
		// We don't need to check the next byte. That is done for us
		// by whatever is calling us.
		*pv = 0;
		return 0;
	} else if (!is_valid_hex(ch)) {
		return -1;
	}

	uint64_t v = hex_value(ch);
	while (is_valid_hex(*p->next)) {
		uint64_t n = v << 4;
		if (n < v) {
			// We can't fit it in the return value.
			// Now count the number of excess bits.
			int ret = 0;
			do {
				ret += 4;
				p->next++;
			} while (is_valid_hex(*p->next));
			return ret;
		}
		v = n | hex_value(*p->next);
		p->next++;
	}

	*pv = v;
	return 0;
}

static int parse_exponent(sipc_parser_t *p, int overflow, int *pexp)
{
	assert(*p->next == 'p');
	p->next++; // leading 'p'

	int expneg = (*p->next == '-');
	p->next += expneg;

	uint64_t uexp;
	int expoverflow = parse_hex(p, &uexp);
	if (expoverflow < 0) {
		return -1;
	}

	if (expoverflow || uexp + overflow > INT_MAX) {
		// the exponent field has overflowed
		*pexp = expneg ? INT_MIN : INT_MAX;
		return 0;
	}

	*pexp = (1 - 2 * expneg) * (int)(uexp + overflow);
	return 0;
}

static bool have_nan(sipc_parser_t *p)
{
	if (p->next[0] == ' ' && p->next[1] == 'n' && p->next[2] == 'a' &&
	    p->next[3] == 'n') {
		p->next += 4;
		return true;
	}
	return false;
}

static bool have_inf(sipc_parser_t *p)
{
	if (p->next[0] == 'i' && p->next[1] == 'n' && p->next[2] == 'f') {
		p->next += 3;
		return true;
	}
	return false;
}

// parse_real parses a real
// pnegate is set to 1 if the real is negative, set to NULL to generate an error
// psig is filled with the significand
// pexp is filled with the signed exponent
// returns 0 on success, non-zero on error
static int parse_real(sipc_parser_t *p, int *pnegate, uint64_t *psig, int *pexp)
{
	// consume leading ' '
	if (*p->next != ' ') {
		return -1;
	}
	p->next++;

	// consume leading '-'
	if (pnegate) {
		*pnegate = (*p->next == '-');
		p->next += *pnegate;
	}

	if (have_inf(p)) {
		*psig = 1;
		*pexp = INT_MAX;
		return 0;
	}

	// keep track of how much the significand overflowed so we can
	// offset the exponent by that amount
	int overflow = parse_hex(p, psig);
	if (overflow < 0) {
		return -1;
	}

	if (*p->next == 'p') {
		// real with exponent
		if (!(*psig & 1)) {
			return -1;
		}
		return parse_exponent(p, overflow, pexp);
	} else {
		// real without an exponent
		// this is allowed if the value is zero or the last byte is non-zero
		*pexp = 0;
		if (*psig) {
			return !(*psig & 0xff);
		} else {
			return pnegate && *pnegate;
		}
	}
}

int sipc_int64(sipc_parser_t *p, int64_t *pv)
{
	int negate = 0;
	int exp;
	uint64_t sig;
	if (parse_real(p, &negate, &sig, &exp)) {
		return -1;
	}
	if (sig) {
		if (exp < 0 || leading_zeros_64(sig) < exp) {
			return -1;
		}
		sig <<= exp;
		if ((int64_t)(sig - negate) < 0) {
			return -1;
		}
		*pv = (1 - 2 * (int64_t)negate) * (int64_t)sig;
		return 0;
	} else {
		*pv = 0;
		return 0;
	}
}

int sipc_uint64(sipc_parser_t *p, uint64_t *pv)
{
	int exp;
	uint64_t sig;
	if (parse_real(p, NULL, &sig, &exp)) {
		return -1;
	}
	if (exp < 0 || (sig && leading_zeros_64(sig) < exp)) {
		return -1;
	}
	*pv = sig << (unsigned)exp;
	return 0;
}

int sipc_int(sipc_parser_t *p, int *pv)
{
	int64_t v;
	if (sipc_int64(p, &v) || v < INT_MIN || v > INT_MAX) {
		return -1;
	}
	*pv = (int)v;
	return 0;
}

int sipc_uint(sipc_parser_t *p, unsigned *pv)
{
	uint64_t v;
	if (sipc_uint64(p, &v) || v > UINT_MAX) {
		return -1;
	}
	*pv = (unsigned)v;
	return 0;
}

static double build_double(int negate, uint64_t sig, int exp)
{
	if (!sig) {
		return 0.0;
	}
	// convert to fractional form
	// have a x 2^b
	// want 1.a x 2^b
	int clz = leading_zeros_64(sig);
	sig <<= clz + 1;
	exp += (sizeof(sig) * CHAR_BIT - 1) - clz;

	// fractional part is 52 bits
	// round up if the 53 bit is set
	if (sig & (1 << (sizeof(sig) * CHAR_BIT - 53))) {
		sig += 1 << (sizeof(sig) * CHAR_BIT - 53);
	}

	unsigned uexp;
	if (exp < -1022) {
		// subnormal - round to zero
		sig = 0;
		uexp = 0;
	} else if (exp > 1023) {
		// too large - round to infinity
		sig = 0;
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
	      (sig >> (64 - 52));
	return u.f;
}

int sipc_double(sipc_parser_t *p, double *pv)
{
	if (have_nan(p)) {
		*pv = NAN;
		return 0;
	}

	uint64_t sig;
	int exp;
	int negate;
	if (parse_real(p, &negate, &sig, &exp)) {
		return -1;
	}
	*pv = build_double(negate, sig, exp);
	return 0;
}

int sipc_float(sipc_parser_t *p, float *pv)
{
	double d;
	if (sipc_double(p, &d)) {
		return -1;
	}
	*pv = (float)d;
	return 0;
}

static int parse_szstring(sipc_parser_t *p, char delim, int *psz,
			  const char **pv)
{
	uint64_t sz;
	if (*p->next != ' ') {
		return -1;
	}
	p->next++;
	if (parse_hex(p, &sz)) {
		return -1;
	}
	// note: >= as we need to make sure we leave at least one
	// byte being the \n after reading the string as the \n
	// acts as a parsing terminator
	if (*(p->next++) != delim || sz >= (uint64_t)(p->end - p->next)) {
		return -1;
	}
	*psz = (int)sz;
	*pv = p->next;
	p->next += sz;
	return 0;
}

int sipc_string(sipc_parser_t *p, int *pn, const char **ps)
{
	return parse_szstring(p, ':', pn, ps);
}

int sipc_bytes(sipc_parser_t *p, int *pn, const unsigned char **pp)
{
	return parse_szstring(p, '|', pn, (const char **)pp);
}

int sipc_bool(sipc_parser_t *p, bool *pv)
{
	if (*p->next != ' ') {
		return -1;
	}
	p->next++;

	switch (*p->next) {
	case 'T':
		*pv = true;
		break;
	case 'F':
		*pv = false;
		break;
	default:
		return -1;
	}
	p->next++;

	return 0;
}

int sipc_next(sipc_parser_t *p, sipc_any_t *pv)
{
	uint64_t sig;
	int negate = 0;
	int overflow;
	int exp;

	switch (p->next[0]) {
	case '\n':
		pv->type = SIPC_END;
		p->next++;
		return 0;
	case ' ':
		break;
	default:
		return -1;
	}

	switch (p->next[1]) {
	case '-':
	case 'i': // for inf
		if (parse_real(p, &negate, &sig, &exp)) {
			return -1;
		} else if (exp && (exp < 0 || exp > leading_zeros_64(sig))) {
			pv->type = SIPC_DOUBLE;
			pv->d = build_double(1, sig, exp);
			return 0;
		} else {
			assert(negate);
			pv->type = SIPC_NEGATIVE_INT;
			pv->n = sig << exp;
			return 0;
		}

	case 'T':
		pv->type = SIPC_BOOL;
		pv->b = true;
		goto single_char_atom;
	case 'F':
		pv->type = SIPC_BOOL;
		pv->b = false;
		goto single_char_atom;
	case '{':
		pv->type = SIPC_MAP;
		goto single_char_atom;
	case '}':
		pv->type = SIPC_MAP_END;
		goto single_char_atom;
	case '[':
		pv->type = SIPC_ARRAY;
		goto single_char_atom;
	case ']':
		pv->type = SIPC_ARRAY_END;
	single_char_atom:
		p->next += 2;
		return 0;

	case 'n':
		pv->type = SIPC_DOUBLE;
		pv->d = NAN;
		return !have_nan(p);

	default:
		p->next++; // space
		overflow = parse_hex(p, &sig);
		if (overflow < 0) {
			return -1;
		}
		switch (*p->next) {
		case 'p':
			if (!(sig & 1)) {
				return -1;
			} else if (parse_exponent(p, overflow, &exp)) {
				return -1;
			}

			if (exp < 0 || exp > leading_zeros_64(sig)) {
				pv->type = SIPC_DOUBLE;
				pv->d = build_double(0, sig, exp);
			} else {
				pv->type = SIPC_POSITIVE_INT;
				pv->n = sig << exp;
			}
			return 0;
		case '|':
			pv->type = SIPC_BYTES;
			goto have_szstring;
		case ':':
			pv->type = SIPC_STRING;
		have_szstring:
			p->next++; // : or |
			// note: >= as we need to make sure we leave at least one
			// byte being the \n after reading the string as the \n
			// acts as a parsing terminator
			if (overflow || sig >= (uint64_t)(p->end - p->next)) {
				return -1;
			}
			pv->string.n = (int)sig;
			pv->string.s = p->next;
			p->next += (int)sig;
			return 0;
		default:
			// no exponent - this form is allowed for 0 or if the bottom byte is non-zero
			pv->type = SIPC_POSITIVE_INT;
			pv->n = sig;
			return sig && !(sig & 0xff);
		}
	}
}

int sipc_any(sipc_parser_t *p, sipc_any_t *pv)
{
	if (sipc_next(p, pv)) {
		return -1;
	}

	if (pv->type == SIPC_ARRAY || pv->type == SIPC_MAP) {
		pv->array.next = p->next;
		// bitfield of whether a given depth is an array (1) or map (0)
		uint32_t is_array = (pv->type == SIPC_ARRAY) ? 1 : 0;
		int depth = 1;
		do {
			sipc_any_t dummy;
			if (sipc_next(p, &dummy)) {
				return -1;
			}
			switch (dummy.type) {
			case SIPC_ARRAY:
			case SIPC_MAP:
				if (depth++ == 16) {
					return -1;
				}
				is_array <<= 1;
				is_array |= (dummy.type == SIPC_ARRAY) ? 1 : 0;
				break;
			case SIPC_ARRAY_END:
			case SIPC_MAP_END:
				if ((is_array & 1) !=
				    (dummy.type == SIPC_ARRAY_END ? 1 : 0)) {
					// mismatched array/map pair
					return -1;
				}
				is_array >>= 1;
				depth--;
				break;
			case SIPC_END:
				return -1;
			default:
				break;
			}
		} while (depth);
		pv->array.end = p->next - 2;
	}

	return 0;
}

enum sipc_msg_type sipc_peek(sipc_parser_t *p)
{
	if (p->next == p->end || *p->next <= ' ') {
		return SIPC_PARSE_ERROR;
	} else {
		return (enum sipc_msg_type)(*p->next);
	}
}

enum sipc_msg_type sipc_start(sipc_parser_t *p)
{
	// msg types must be a printable ascii byte
	// this is really trying to protect against \n (empty line) and \0 (no more lines)
	if (p->next == p->end || *p->next <= ' ') {
		return SIPC_PARSE_ERROR;
	} else {
		return (enum sipc_msg_type)(*(p->next++));
	}
}

int sipc_end(sipc_parser_t *p)
{
	sipc_any_t any;
	for (;;) {
		if (sipc_any(p, &any)) {
			return -1;
		} else if (any.type == SIPC_END) {
			return 0;
		}
	}
}

static const char hex_chars[] = "0123456789abcdef";

static int format_hex(char *p, uint64_t v)
{
	int count = v ? (16 - (leading_zeros_64(v) / 4)) : 1;
	for (int n = (count - 1) * 4; n >= 0; n -= 4) {
		*(p++) = hex_chars[(v >> n) & 15];
	}
	return count;
}

static int format_uint64(char *p, uint64_t v)
{
	unsigned ctz = v ? trailing_zeros_64(v) : 0;
	if (ctz < 8) {
		return format_hex(p, v);
	}
	int i = format_hex(p, v >> ctz);
	p[i++] = 'p';
	return i + format_hex(p + i, ctz);
}

static int format_int64(char *p, int64_t v)
{
	if (v < 0) {
		*(p++) = '-';
		return 1 + format_uint64(p, (uint64_t)(-v));
	} else {
		return format_uint64(p, (uint64_t)v);
	}
}

static int format_double(char *p, double v)
{
	union {
		double f;
		uint64_t u;
	} u;
	u.f = v;

	int negate = (int)(u.u >> 63);
	unsigned uexp = (u.u >> 52) & 0x7FF;
	uint64_t sig = u.u & (((uint64_t)1 << 52) - 1);

	int i = 0;
	if (uexp == 0) {
		// zero and subnormals - encode as zero
		p[i++] = '0';
		return i;
	} else if (uexp == 0x7FF) {
		// infinity & nan
		if (sig) {
			p[i++] = 'n';
			p[i++] = 'a';
			p[i++] = 'n';
			return i;
		} else {
			if (negate) {
				p[i++] = '-';
			}
			p[i++] = 'i';
			p[i++] = 'n';
			p[i++] = 'f';
			return i;
		}
	} else {
		// normal
		if (negate) {
			p[i++] = '-';
		}
		int exp = (int)uexp - 1023;

		// sig is currently in fractional 1.a x 2^b form
		// need to convert to whole form a x 2^b
		sig |= (uint64_t)1 << 52;
		unsigned ctz = trailing_zeros_64(sig);
		sig >>= ctz;
		exp -= 52 - ctz;

		if (0 <= exp && exp < 8) {
			// convert to non-exponent form
			return i + format_hex(p + i, sig << exp);
		}

		// print in <sig>p<exp> form
		i += format_hex(p + i, sig);
		p[i++] = 'p';
		if (exp < 0) {
			p[i++] = '-';
			exp = -exp;
		}
		return i + format_hex(p + i, (uint64_t)exp);
	}
}

static int format_string(char *p, int bufsz, char delim, int n, const char *v)
{
	// format the size
	int i = format_hex(p, (unsigned)n);
	p[i++] = delim;

	if (i + n <= bufsz) {
		memcpy(p + i, v, n);
	}

	return i + n;
}

static int format_array(char *p, int bufsz, char open, char close,
			const char *start, const char *end)
{
	int n = (int)(end - start);
	int need = 2 + n + 1;
	if (need <= bufsz) {
		p[0] = open;
		p[1] = ' ';
		memcpy(p + 2, start, n);
		p[2 + n] = close;
	}
	return need;
}

static int format_arg(char *p, int bufsz, const char **pfmt, va_list ap)
{
	int n;
	const char *str;
	const sipc_any_t *any;

	switch (*((*pfmt)++)) {
	case 'p': // %p - any
		any = va_arg(ap, const sipc_any_t *);
		switch (any->type) {
		case SIPC_BOOL:
			*p = any->b ? 'T' : 'F';
			return 1;
		case SIPC_NEGATIVE_INT:
			*p = '-';
			return 1 + format_uint64(p, any->n);
		case SIPC_POSITIVE_INT:
			return format_uint64(p, any->n);
		case SIPC_DOUBLE:
			return format_double(p, any->d);
		case SIPC_STRING:
			return format_string(p, bufsz, ':', any->string.n,
					     any->string.s);
		case SIPC_BYTES:
			return format_string(p, bufsz, '|', any->bytes.n,
					     (const char *)any->bytes.p);
		case SIPC_ARRAY:
			return format_array(p, bufsz, '[', ']', any->array.next,
					    any->array.end);
		case SIPC_MAP:
			return format_array(p, bufsz, '{', '}', any->map.next,
					    any->map.end);
		default:
			return -1;
		}
	case 'z': // %z..
		switch (*((*pfmt)++)) {
		case 'i': // %li - long
			return format_int64(p, (int64_t)va_arg(ap, intptr_t));
		case 'u': // %lu - unsigned long
			return format_uint64(p,
					     (uint64_t)va_arg(ap, uintptr_t));
		default:
			return -1;
		}

	case 'l': // %l...
		switch (*((*pfmt)++)) {
		case 'i': // %li - long
		case 'd':
			return format_int64(p, (int64_t)va_arg(ap, long));
		case 'u': // %lu - unsigned long
			return format_uint64(
				p, (uint64_t)va_arg(ap, unsigned long));
		case 'l': // %ll...
			switch (*((*pfmt)++)) {
			case 'i': // %lli - long long
			case 'd':
				return format_int64(
					p, (int64_t)va_arg(ap, long long));
			case 'u': // %llu - unsigned long long
				return format_uint64(
					p, (uint64_t)va_arg(
						   ap, unsigned long long));
			default:
				return -1;
			}
		default:
			return -1;
		}
	case 'o': // %o - bool
		*p = va_arg(ap, int) ? 'T' : 'F';
		return 1;
	case 'i': // %i - int
	case 'd':
		return format_int64(p, (int64_t)va_arg(ap, int));
	case 'u': // %u - unsigned
		return format_uint64(p, (uint64_t)va_arg(ap, unsigned));
	case 'f': // %f - double
	case 'e':
	case 'g':
		return format_double(p, va_arg(ap, double));
	case '%': // %% - escaped percent
		*(p++) = '%';
		return 1;
	case '*': // %*s (string) and %*p (bytes)
		n = va_arg(ap, int);
		str = va_arg(ap, const char *);
		switch (*((*pfmt)++)) {
		case 's': // %*s - string
			return format_string(p, bufsz, ':', n, str);
		case 'p': // %*p - bytes
			return format_string(p, bufsz, '|', n, str);
		default:
			return -1;
		}
	case 's': // %s - string
		str = va_arg(ap, const char *);
		n = (int)strlen(str);
		return format_string(p, bufsz, ':', n, str);
	case '.': // %.*s - raw copy
		if (*((*pfmt)++) != '*' || *((*pfmt)++) != 's') {
			return -1;
		}
		n = va_arg(ap, int);
		if (n <= bufsz) {
			str = va_arg(ap, const char *);
			memcpy(p, str, n);
		}
		return n;
	default:
		return -1;
	}
}

// INT64: -1234567890abcdef
// DBL:   -123456789abcdp-3fe

#define MAX_ATOM_SIZE 20

int sipc_vformat(char *p, int bufsz, const char *fmt, va_list ap)
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

int sipc_format(char *buf, int bufsz, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int ret = sipc_vformat(buf, bufsz, fmt, ap);
	va_end(ap);
	return ret;
}

void sipc_frame(char *buf, int sz)
{
	assert(6 <= sz && sz <= 0xFFFF && buf[sz - 1] == '\n' &&
	       buf[4] == '\n');
	buf[0] = hex_chars[sz >> 12];
	buf[1] = hex_chars[(sz >> 8) & 15];
	buf[2] = hex_chars[(sz >> 4) & 15];
	buf[3] = hex_chars[sz & 15];
}

int sipc_unframe(sipc_parser_t *p, const char *buf, int sz)
{
	if (sz < 5) {
		// need more data to be able to parse the header
		return 0;
	}

	int v1 = is_valid_hex(buf[0]);
	int v2 = is_valid_hex(buf[1]);
	int v3 = is_valid_hex(buf[2]);
	int v4 = is_valid_hex(buf[3]);
	int v5 = (buf[4] == '\n');
	if (!(v1 && v2 && v3 && v4 && v5)) {
		// invalid header
		return -1;
	}

	int msgsz = (hex_value(buf[0]) << 12) | (hex_value(buf[1]) << 8) |
		    (hex_value(buf[2] << 4)) | hex_value(buf[3]);

	if (msgsz >= sz) {
		if (sipc_init(p, buf + 5, msgsz - 5)) {
			return -1;
		}
	}

	return msgsz;
}

int sipc_init(sipc_parser_t *p, const char *buf, int sz)
{
	if (sz < 1 || buf[sz - 1] != '\n') {
		return -1;
	}
	p->next = buf;
	p->end = buf + sz;
	return 0;
}
