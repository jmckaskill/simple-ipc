This is an RPC serialization designed to be:

* Relatively easy to use manually with netcat/socat/etc
* Easy to troubleshoot & diagnose (hence text) but still relatively performant
* Still reasonably restrictive to make parsing implementations easy
* Simple to use with sprintf or equivalent
* No string escaping to ensure ease of use and security
* Ability to bootstrap ancillary streams that can be used in high performance or more niche applications

Each atom should be separated by a single space. Other whitespace must not be included including \t or \r.

Atoms are one of:

* Boolean
* Rationals
* Strings
* Bytes
* Lists
* Maps
* Reference

APIs should strictly define what atom types they support for a given argument. Atom types are not interchangeable unless the API supports so.

All atom types are designed to have only a single encoding for a given value.
This allows messages to be byte compared.

| Type      | Regex                        |
| --------- | ---------------------------- |
| Boolean   | `T` or `F`                   |
| Real      | `-?[0-9a-f]+(p-?[0-9a-f]+)?` |
| String    | `[0-9a-f]+:.*`               |
| Bytes     | `[0-9a-f]+|.*`               |
| Reference | `[0-9a-f]+@`                 |
| List Open | `[` and `]`                  |
| Map       | `{` and `}`                  |

# Real

In a C-like language, reals cover unsigned, signed, and float types. The general format is float like and of the form `-?[0-9a-f]+(p-?[0-9a-f]+)?`. This is comprised of the following parts:

* Leading `-`: indicates whether the number is negative
* First hex number: indicates the significand `a`
* Second hex number: indicates the base `b`
 
This forms a number of the form `a x 2^b`, where both `a` and `b` are printed in lowercase hexadecimal.

The real can also be one of the following special items:

* `inf` - positive infinity
* `-inf` - negative infinity
* `nan` - indeterminate number

The base must be absent if the base would otherwise be between 0 and 7 inclusive. In that case the significand is the full value printed as if the base were zero. Otherwise the base must be included and the significand must be minimized (ie shifted right until the least significant bit is 1).

Leading zeros of either the significand or exponent must not be printed.

Negative zero must not be printed. If a user still wants to represent something close to zero on the negative side use negative 1 with a very negative base (eg `-1p-1000`).

The real is arbitrary precision. However every real number has one and only one representation.

Implementations can decide how they want to read an arbitrary precision real in. There are three general approaches:

* Map to an arbitrary precision number like bignum
* Collapse along certain boundaries
* Reject unrepresentable numbers

For example a C implementation without bignum could act as follows:

| Map To Type | Case         | Action                        |
| ----------- | ------------ | ----------------------------- |
| Integer     | Too Large    | Reject                        |
| Integer     | Has Fraction | Reject                        |
| Unsigned    | Negative     | Reject                        |
| Float       | Too Large    | Map to + or - inf             |
| Float       | Too Small    | Map to + or - 0               |
| Float       | Too Detailed | Round to nearest represntable |

Some examples

| Encoding | Value in Decimal  |
| -------- | ----------------- |
| `inf`    | Positive Infinity |
| `-inf`   | Negative Infinity |
| `nan`    | Indeterminate     |
| `-ff`    | -255              |
| `0`      | 0                 |
| `ff`     | 255               |
| `1p8`    | 256               |
| `1p10`   | 65536             |
| `1p-1`   | 0.5               |

# Strings

Strings are encoded in a length prefix form of `[0-9a-f]+:..`. The hex digits before the colon indicate the number of UTF-8 bytes. The data after the colon is the UTF-8 bytes of the string.

# Bytes

Bytes are similar to strings but use a pipe character instead of a colon (ie `[0-9a-f]+|...`). The length indicates the number of bytes in the value. The bytes after the pipe can be any 8 bit bytes.

# Ancillary Reference

File descriptors can be sent as ancillary data. By itself this does nothing. The sender must also send an argument that references the file descriptor. This is done by sending a reference atom of form `[0-9a-f]+@` . The meaning and interpretation of the number depends on the transport.

| Transport Type      | Meaning                                   |
| ------------------- | ----------------------------------------- |
| Unix Domain Socket  | Index into fd list sent with the @ symbol |
| Windows Pipe Server | Handle number in the target process       |
| Quic                | Stream index                              |

# Lists

Lists are of the form `[ field1 field2 ]`. The opening, closing and each list item are seperated by a space.

# Maps

Maps are of the form `{ key1 value1 key2 value2 }`. Each opening, closing brackets, key, and value is seperated by a space. Keys and values can be any type. Applications should specify what key and value types they expect.

# Framing

Most transports require framing. Even datagram oriented transports should be framed to allow multiple messages per datagram. The standard framing is of the form `[0-9a-f]{4} arg1 arg2 ...;\n`. The first hex number indicates the number of bytes of the entire message including the length header and trailing semi-colon and newline. This form limits messages to 65536 bytes which is the maximum datagram size on windows pipe messages and most unix datagram sockets.

Other transports may use different framing. For example QUIC has low overhead framing built-in such that framing is not required.

# Commands

One common use case for this encoding is for RPC request/response type messages. In this case request are encoded in the form (excluding the framing):

    <verb> <arguments>...

Verbs are strings. Arguments can be any type. Command verbs depend on the API. Arguments are seperated by a space.

Replies are of one the following two forms:

    2:ok <reply arguments>...
    5:error <error name> [<optional error description>]

Services should support pipelined requests.

Services can implement a maximum message length. A good default is 65536. Requests larger than that should probably be split up or leverage an ancillary stream.

APIs should support a `help` verb that returns a usage string.

Optional arguments should either be supported through a variable number of arguments (e.g. an API that supports 3+ arguments), an array or map. Generally an API should use an array for a variable list of items. That way new options can be supported by adding another argument.

If the API changes in a non-backwards compatible way then the verb should be changed.

If you want to listen to a signal or stream data, you create a pipe or socket and send the file descriptor via ancillary data. That pipe can then use this same protocol or anything else.

Most daemons would expose a single service through a single named domain socket or pipe server. If a daemon combines multiple services then it should create multiple domain sockets.

To allow easy startup, client applications should support blocking on socket creation by retrying in a loop, using inotify or whatever is appropriate.

In the case of a malformed (or too long) request, a server should reply with a `malformed` error (`5:error 9:malformed`) and then close the connection.

# Socket Location

The socket location should be shared by an out of band channel. Options include:

* Environment variable
* Another IPC protocol
* Hard coded location
* Command line argument

The choice depends on the application. A good recommendation is to use an environment variable with fallback to a hard coded default.
