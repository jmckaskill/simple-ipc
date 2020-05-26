#pragma once
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <float.h>

enum srpc_type {
	SRPC_END,
	SRPC_BOOL,
	SRPC_POSITIVE_INT,
	SRPC_NEGATIVE_INT,
	SRPC_DOUBLE,
	SRPC_STRING,
	SRPC_BYTES,
	SRPC_REFERENCE,
	SRPC_ARRAY,
	SRPC_MAP,
	SRPC_ARRAY_END,
	SRPC_MAP_END,
};

struct srpc_parser {
	const char *next;
	const char *end;
};
typedef struct srpc_parser srpc_parser_t;

struct srpc_any {
	union {
		bool b;
		uint64_t n;
		double d;
		struct {
			int n;
			const char *s;
		} string;
		struct {
			int n;
			const unsigned char *p;
		} bytes, reference;
		srpc_parser_t array, map;
	};
	enum srpc_type type;
};
typedef struct srpc_any srpc_any_t;

// these returns 0 on success, non-zero on error
int srpc_init(srpc_parser_t *p, char *buf, int sz);
int srpc_next(srpc_parser_t *p, srpc_any_t *pv);
int srpc_any(srpc_parser_t *p, srpc_any_t *pv);
int srpc_bool(srpc_parser_t *p, bool *pv);
int srpc_int(srpc_parser_t *p, int *pv);
int srpc_uint(srpc_parser_t *p, unsigned *pv);
int srpc_int64(srpc_parser_t *p, int64_t *pv);
int srpc_uint64(srpc_parser_t *p, uint64_t *pv);
int srpc_float(srpc_parser_t *p, float *pv);
int srpc_double(srpc_parser_t *p, double *pv);
int srpc_string(srpc_parser_t *p, int *pn, const char **ps);
int srpc_bytes(srpc_parser_t *p, int *pn, const unsigned char **pp);
int srpc_reference(srpc_parser_t *p, int *pn, const unsigned char **pp);

// These format a message using printf like syntax to aid in formatting
// an RPC message. The following printf specifiers are supported
// - %o - bool
// - %i,%li,%lli,%zi - int,long,llong,intptr_t
// - %u,%lu,%llu,%zu - unsigned,ulong,ullong,uintptr_t
// - %f - double or float
// - %*s - string - int followed by const char * argument
// - %s - string - null terminated const char * argument
// - %*p - bytes - int followed by const void * argument
// - %p - any - pointer to srpc_any_t
// - %.*s - raw copy - int followed by const char * argument
// - %% - raw % symbol
// Any other character is copied verbatim to the output
// To add a reference use the raw copy
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
void srpc_pack_stream(char *buf, int sz);

// returns
// -ve on error
// 0 if more data is needed
// > 0 number of bytes in the message and p is setup with the start and end
int srpc_unpack_stream(srpc_parser_t *p, char *buf, int sz);
