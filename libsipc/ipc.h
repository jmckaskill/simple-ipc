#pragma once
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <float.h>

enum sipc_msg_type {
	SIPC_PARSE_ERROR = 0,
	SIPC_REQUEST = 'R',
	SIPC_ERROR = 'E',
	SIPC_SUCCESS = 'S',
	SIPC_WINDOWS_HANDLE = 'W',
};

enum sipc_type {
	SIPC_END,
	SIPC_BOOL,
	SIPC_POSITIVE_INT,
	SIPC_NEGATIVE_INT,
	SIPC_DOUBLE,
	SIPC_STRING,
	SIPC_BYTES,
	SIPC_ARRAY,
	SIPC_MAP,
	SIPC_ARRAY_END,
	SIPC_MAP_END,
};

struct sipc_parser {
	const char *next;
	const char *end;
};
typedef struct sipc_parser sipc_parser_t;

struct sipc_any {
	union {
		bool b;
		uint64_t n;
		double d;
		struct {
			const char *s;
			int n;
		} string;
		struct {
			const unsigned char *p;
			int n;
		} bytes;
		sipc_parser_t array, map;
	};
	enum sipc_type type;
};
typedef struct sipc_any sipc_any_t;

// these returns 0 on success, non-zero on error
int sipc_init(sipc_parser_t *p, const char *buf, int sz);
enum sipc_msg_type sipc_peek(sipc_parser_t *p);
enum sipc_msg_type sipc_start(sipc_parser_t *p);
int sipc_end(sipc_parser_t *p);
int sipc_next(sipc_parser_t *p, sipc_any_t *pv);
int sipc_any(sipc_parser_t *p, sipc_any_t *pv);
int sipc_bool(sipc_parser_t *p, bool *pv);
int sipc_int(sipc_parser_t *p, int *pv);
int sipc_uint(sipc_parser_t *p, unsigned *pv);
int sipc_int64(sipc_parser_t *p, int64_t *pv);
int sipc_uint64(sipc_parser_t *p, uint64_t *pv);
int sipc_float(sipc_parser_t *p, float *pv);
int sipc_double(sipc_parser_t *p, double *pv);
int sipc_string(sipc_parser_t *p, int *pn, const char **ps);
int sipc_bytes(sipc_parser_t *p, int *pn, const unsigned char **pp);

// These format a message using printf like syntax to aid in formatting
// an IPC message. The following printf specifiers are supported
// - %o - bool
// - %i,%li,%lli,%zi - int,long,llong,intptr_t
// - %u,%lu,%llu,%zu - unsigned,ulong,ullong,uintptr_t
// - %f - double or float
// - %*s - string - int followed by const char * argument
// - %s - string - null terminated const char * argument
// - %*p - bytes - int followed by const void * argument
// - %p - any - pointer to sipc_any_t
// - %.*s - raw copy - int followed by const char * argument
// - %% - raw % symbol
// Any other character is copied verbatim to the output
// A null byte is appended to the buffer but is not included in the returned byte count
// Returns
// -ve on error
// number of characters written (if < bufsz)
// buffer size needed (if > bufsz)
int sipc_format(char *buf, int bufsz, const char *fmt, ...)
#ifdef __GNUC__
	__attribute__((format(printf, 3, 4)))
#endif
	;
int sipc_vformat(char *buf, int bufsz, const char *fmt, va_list ap)
#ifdef __GNUC__
	__attribute__((format(printf, 3, 0)))
#endif
	;

// This writes the framing header size
// Framed messages are of the form 8bca\n....\n
// The user must have already place dummy characters in the first three bytes
// This will then write the correct characters.
// The provided sz should be the full message size including the header and newline
void sipc_frame(char *buf, int sz);

// returns
// -ve on error
// 0 if more data is needed
// > 0 number of bytes in the message
// if ret >= sz, then p is setup to parse the message
int sipc_unframe(sipc_parser_t *p, const char *buf, int sz);
