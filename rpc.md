This is an RPC serialization designed to be:

* Relatively easy to use manually with netcat/socat/etc
* Easy to troubleshoot & diagnose (hence text) but still relatively performant
* Still reasonably restrictive to make parsing implementations easy
* Simple to use with sprintf or equivalent
* No string escaping to ensure ease of use and security
* Ability to bootstrap ancillary streams that can be used in high performance or
  more niche applications

Commands are terminated with a newline \n

Each atom should be separated by a single space. Other whitespace must not be
included including \t or \r.

Atoms are one of:

* Signed Integers
* Unsigned Integers
* Floats
* Words
* Strings
* Bytes
* Lists
* Maps
* Ancillary reference

There is no boolean type. Map to 0/1 instead.

APIs should strictly define what atom types they support for a given argument.
Atom types are not interchangeable unless the API supports so. E. G. If an API
requests a float value, a user must send “1e0” instead of “1”.

All atom types are designed to have only a single encoding for a given value.
This allows messages to be byte compared.

| Type      | Regex                                        | Printf       |
|-----------|----------------------------------------------|--------------|
| Integer   | `-?[0-9]+`                                   | `%d` or `%u` |
| Float     | `(-?0x1(\.[0-9a-f]+)?p[+-][0-9]+|nan|-?inf)` | `%a`         |
| String    | `[0-9]+:.*`                                  | `%d:%s`      |
| Bytes     | `[0-9]+|.*`                                  | `%d|%.*s`    |
| Reference | `[0-9]+@`                                    | `%zu@`       |

# Integers

Integers are arbitrary sized. Implementations can either auto size or scale
based off of usage. Always in decimal. Implementations should support up to 64
bit unsigned and signed values. The encoding is a subset of the standard c style
decimal. Numbers must not be zero padded and a leading + must not be included.
The format is `[-]digits` (e.g. 123 -123 0). This is the equivalent of the `%d`
printf format specifier.

# Floats

Floats are encoded using the C99 hexadecimal float encoding `%a`. The format is
`[-]0x1.hexp[+-]digits` (e.g. 0x1.abcdp+23). Positive and negative zero is
supported. The value before the decimal must be 1 (or 0 but only for a 0 value).
The 0 value must always use positive 0 as the exponent.

The float type also supports the following special cases. They must use this
exact string.

| Case              | String  |
|-------------------|---------|
| Positive Infinity | inf     |
| Negative Infinity | -inf    |
| Unknown/Quiet NaN | nan     |
| Positive Zero     | 0x0P+0  |
| Negative Zero     | -0x0P+0 |

Precision is arbitrary. Implementations should support at least double
precision. When reading if a value has more precision then can be stored the
implementation should drop the extra precision. For example an implementation
may convert to 0 instead of producing subnormals or alternatively produce
infinity if the exponent is too large to fit.

# Strings

Strings are similar to netstrings (digits:UTF-8) e.g. 5:hello. Digits indicates
the number of UTF-8 bytes. The string should not contain invalid UTF-8 bytes
(incl. null bytes).

# Bytes

Bytes are similar to strings but use a pipe instead of colon (digits|bytes).
Digits indicates the number of bytes. The data bytes can contain any 8 bit byte.

# Ancillary Reference

File descriptors can be sent as ancillary data. By itself this does nothing. The
sender must also send an argument that references the file descriptor. This is
done by prefixing sending an atom `@digits`. The meaning and interpretation of
the number depends on the transport.

| Transport Type      | Meaning                                   |
|---------------------|-------------------------------------------|
| Unix Domain Socket  | Index into fd list sent with the @ symbol |
| Windows Pipe Server | Handle number in the target process       |
| Quic                | Stream index                              |

Applications that accept file paths or URLs should consider supporting a file
descriptor (and vice versa) in place in the same argument.

# Lists

Lists are of the form [ field1 field2 ]. The opening, closing and each list item
are terminated by a space.

# Maps

Maps are of the form { key1 value1 key2 value2 }. Keys and values can be any
type. Applications may restrict the key and value type. Most APIs would likely
use string keys.

# Commands

Commands and replies are of the form

    <verb> <arguments>... \n

Verbs are strings. Arguments can be any type. Command verbs depend on the API.
Each argument is followed by a space. The command is terminated by a newline.

Reply verbs should support the following standard verbs:

    ok <reply arguments>... \n
    error <error word> [<error description>] \n

Services must support pipelined requests.

Services can implement a maximum line length. A good default is 4096. Requests
larger than that should probably be split up or leverage an ancillary stream.

APIs should support a help command (help \n) that returns a usage string.

Optional arguments should either be supported through a variable number of
arguments (e.g. an API that supports 3+ arguments), an array or map. Generally
an API should use an array for a variable list of items. That way new options
can be supported by adding another argument.

If the API changes in a non-backwards compatible way then the verb should be
changed.

If you want to listen to a signal or stream data, you create a pipe or socket
and send the file descriptor via ancillary data. That pipe can then use this
same protocol or anything else.

Most daemons would expose a single service through a single named domain socket
or pipe server. If a daemon combines multiple services then it should create
multiple domain sockets.

To allow easy startup, client applications should support blocking on socket
creation by retrying in a loop, using inotify or whatever is appropriate.

In the case of a malformed (or too long) request, a server should send `error
malformed \n` and then close the connection.

# Socket Location

The socket location should be shared by an out of band channel. Options
include:

* Environment variable
* Another IPC protocol
* Hard coded location
* Command line argument

The choice depends on the application. A good recommendation is to use an
environment variable with fallback to a hard coded default.

