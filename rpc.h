#pragma once
#include <stdint.h>
#include <stdarg.h>

enum srpc_type {
	SRPC_END = 0,
	SRPC_INT = 1,
	SRPC_INT64 = 2,
	SRPC_UINT64 = 3,
	SRPC_DOUBLE = 4,
	SRPC_STRING = 5,
	SRPC_BYTES = 6,
	SRPC_REFERENCE = 7,
	SRPC_ARRAY = 8,
	SRPC_MAP = 9,
};

struct srpc_parser {
	const char *next;
	const char *end;
};
typedef struct srpc_parser srpc_parser;

struct srpc_any {
	union {
		int i;
		long long llong;
		unsigned long long ullong;
		uintptr_t ref;
		double d;
		struct {
			int n;
			const char *s;
		} string;
		struct {
			int n;
			const unsigned char *p;
		} bytes;
		struct srpc_parser array, map;
	};
	enum srpc_type type;
};

// these returns 0 on success, -ve on error
int srpc_next(srpc_parser *p, struct srpc_any *pv);
int srpc_any(srpc_parser *p, struct srpc_any *pv);
int srpc_int(srpc_parser *p, int *pv);
int srpc_long(srpc_parser *p, long long *pv);
int srpc_uint(srpc_parser *p, unsigned *pv);
int srpc_ulong(srpc_parser *p, unsigned long long *pv);
int srpc_reference(srpc_parser *p, uintptr_t *pv);
int srpc_float(srpc_parser *p, float *pv);
int srpc_double(srpc_parser *p, double *pv);
int srpc_string(srpc_parser *p, int *pn, const char **ps);
int srpc_bytes(srpc_parser *p, int *pv, const unsigned char **pp);

// These format a message using printf like syntax to aid in formatting
// an RPC message. The following printf specifiers are supported
// - %d - int
// - %u - unsigned
// - %zu - uintptr_t
// - %lld - int64_t
// - %llu - uint64_t
// - %a - double or float
// - %*s - string - int followed by const char * argument
// - %*p - bytes - int followed by const void * argument
// - %p - any - pointer to struct srpc_any
// - %s - raw copy of nul terminated string to the output
// - %.*s - raw copy - int followed by const char * argument
// - %% - raw % symbol
// Any other character is copied verbatim to the output
// To add a reference use %u@ or %zu@ or similar
// A null byte is appended to the buffer but is not included in the returned byte count
// Returns
// -ve on error
// number of characters written (if < bufsz)
// buffer size needed (if > bufsz)
int srpc_format(char *buf, int bufsz, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));
int srpc_vformat(char *buf, int bufsz, const char *fmt, va_list ap)
	__attribute__((format(printf, 3, 0)));

// This writes the framing header size
// Framed messages are of the form 8bc|....\r\n
// The user must have already place dummy characters in the first three bytes
// This will then write the correct characters.
// The provided sz should be the full message size including the header and newline
void srpc_pack(char *buf, int sz);

// returns
// -ve on error
// 0 if more data is needed
// > 0 number of bytes in the message and p is setup with the start and end
int srpc_unpack(srpc_parser *p, char *buf, int sz);
