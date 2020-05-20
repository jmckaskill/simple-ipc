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

All atom types are designed to have only a single encoding. This allows
messages to be byte compared.

# Signed Integers

Integers are arbitrary sized. Implementations can either auto size or scale
based off of usage. Always in decimal. Implementations should support up to 64
bit unsigned and signed values. The encoding is a subset of the standard c style
decimal. Numbers must not be zero padded and a leading + must not be included.
The format is `[-]digits` (e.g. 123 -123 0). This is the equivalent of the `%d`
printf format specifier.

# Unsigned Integers

Similar to signed integers but in hex. The format is `0xdigits` (e.g. 0xabcd).
This is equivalent to the `%#x` printf format specifier.

# Floats

Floats support a subset of the standard float printing. Numbers must not be
zero padded. There is no leading +. The exponent must be always included. The
exponent must be separated by a lowercase e. There is no point separator; use
exponents instead. The format is `[-]digits e [-]digits` (e.g. 314e-2 -12e-1
2e0).

The float type also supports the following special cases. They must use this
exact string.

| Case              | String |
|-------------------|--------|
| Positive Infinity | 0eINF  |
| Negative Infinity | -0eINF |
| Unknown/Quiet NaN | 0eNAN  |
| Zero              | 0e0    |

Precision is arbitrary. Implementations should support at least double
precision.

# Words

Words are a series of characters `[a-zA-Z][-a-zA-Z0-9]` . Words must start with
an alphabetical character. Words are generally used for commands. Commands
should generally be lower case, - seperated.

# Strings

Strings are similar to netstrings (digits’UTF-8’) e.g. 5:’hello’. Digits
indicates the number of UTF-8 bytes within the quotes. The string must not
contain invalid UTF-8 bytes (incl. null bytes). Implementations may decide not
to enforce this if the string is from a trusted source.

# Bytes

Bytes are netstrings (digits:bytes). Digits indicates the number of bytes. The
data bytes can contain any 8 bit byte.

# Ancillary Reference

File descriptors can be sent as ancillary data. By itself this does nothing.
The sender must also send a command that references the file descriptor. This
is done by prefixing an atom (number, word, etc) with @.  The meaning and
interpretation of the @ argument depends on the transport.

| Transport Type      | Meaning                                   |
|---------------------|-------------------------------------------|
| Unix Domain Socket  | Index into fd list sent with the @ symbol |
| Windows Pipe Server | Handle number in the target process       |
| Quic                | Stream index                              |

Applications that accept file paths or URLs should consider supporting a file
descriptor (and vice versa) in place in the same argument.

# Lists

Lists are of the form [field1 field2]. There is no whitespace between the open
bracket and the first argument nor is there whitespace between the close
bracket and the last argument. There is a single space between list items.

# Maps

Maps are of the form {key1 value1 key2 value2}. Keys and values can be any
type. Applications may restrict the key and value type. Most APIs would likely
use string keys.

# Commands

Commands and replies are of the form

    <verb> <arguments>...\n

Verbs are strings/words. Arguments can be any type. Command verbs depend on the
API.

Reply verbs should support the following standard verbs:

    ok <reply arguments>...\n
    error <error word> [<error description>]\n

Services must support pipelined requests.

Services can implement a maximum line length. A good default is 4096. Requests
larger than that should probably be split up or leverage an ancillary stream.

APIs should support a help command (help\n) that returns a usage string.

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
malformed\n` and then close the connection.

# Socket Location

The socket location should be shared by an out of band channel. Options
include:

* Environment variable
* Another IPC protocol
* Hard coded location
* Command line argument

The choice depends on the application. A good recommendation is to use an
environment variable with fallback to a hard coded default.

# Question

Should we do all numbers in hex instead of decimal?
It would make it much simpler.

floating point would look like 7Ap2
Numbers would look like A7
Strings would look like C'hello world!'

We couldn't easily support words in that case. That may not be a terrible
thing... If words used the string encoding, it would be nice to not have to
quote both ends.

R C:hello world!
E 

<hex>
<hex>p<hex>
<hex>:<string>
<hex>|<bytes>
<hex>@<reference>
<word> must start with a-z and can contain a-z or - ie lowercase only
hex only supports uppercase hex characters that way it's distinct from a word
seems a bit arbitrary and annoying for dynamic languages
doesn't seem too hard to prefix it with a size when hard-coding things

3p0
11b0
1.1b1
0x400 b100000...

Equivalent printf encodings
unsigned 0x%X
signed %d
float %A
string :%X:%s
bytes |%X|%.*s

We can support a custom printf to ensure compliance
unsigned int (or smaller) %X
unsigned int64 %Xll
signed int (or smaller) %d
signed int64 %dll
float/double %A
string %*s, expects 2 args: int, cstr
bytes %*p, expects 2 args: int, byte ptr
raw string %s or %.*s (string previously created)
arrays %*pX, %*pXll, %*pd, %*pdll, %*pA, %*ps, %*pp

| Type             | Regex                                        | Printf      | RPC Format       |
|------------------|----------------------------------------------|-------------|------------------|
| Signed Integer   | `-?[0-9]+ `                                  | `%d `       | `%d ` or `%lld ` |
| Unsigned Integer | `0x[0-9A-F]+ `                               | `0x%X `     | `%x ` or `%llx ` |
| Float            | `(-?0X1(.[0-9A-F]+)?P[+-][0-9]+|NAN|-?INF) ` | `%A `       | `%a `            |
| Word             | `[a-z][a-z-]* `                              | Raw         | Raw              |
| String           | `:[0-9]+:.* `                                | `:%d:%s `   | `%*s `           |
| Bytes            | `|[0-9]+|.* `                                | `|%d|%.*s ` | `%*p `           |
| Reference        | `@[^\s]* `                                   | `@%[pd] `   | `@%d ` or `@%p ` |

Prefixes dictate the type:
`-?[0-9]` signed integer
`0x` unsigned integer
`-?0X`, `NAN`, and `-?INF` float
`[a-z]` word
`:` string
`|` bytes

do-something \n
cmd1 123 0xABCD [ :3:abc { key :5:value } ] \n
