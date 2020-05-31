package ipc

import (
	"errors"
	"math"
	"math/big"
	"math/bits"
	"reflect"
)

type EntryType int

const (
	EndOfMessage       EntryType = -1
	RequestEntry       EntryType = 'R'
	ErrorEntry         EntryType = 'E'
	SuccessEntry       EntryType = 'S'
	WindowsHandleEntry EntryType = 'W'
)

const hexChars = "0123456789abcdef"

func appendHex(b []byte, v uint64) []byte {
	buf := [8]byte{}
	i := 7
	for {
		buf[i] = hexChars[v&15]
		v >>= 4
		if i == 0 || v == 0 {
			break
		}
		i--
	}
	return append(b, buf[i:]...)
}

func AppendBool(b []byte, v bool) []byte {
	if v {
		return append(b, " T"...)
	} else {
		return append(b, " F"...)
	}
}

func AppendBigFloat(b []byte, f *big.Float) []byte {
	b = append(b, ' ')
	if f.IsInf() {
		if f.Signbit() {
			return append(b, "-inf"...)
		} else {
			return append(b, "inf"...)
		}
	}
	normexp := f.MantExp(nil)
	minprec := int(f.MinPrec())
	exp := normexp - minprec
	if 0 <= exp && exp < 8 {
		// non-exponent form
		mant, _ := f.Int(nil)
		return mant.Append(b, 16)
	} else {
		// explicit component form
		// first shift up to get a whole number
		mant, _ := new(big.Float).SetMantExp(f, minprec-normexp).Int(nil)
		b = mant.Append(b, 16)
		b = append(b, 'p')
		if exp < 0 {
			b = append(b, '-')
			return appendHex(b, uint64(-exp))
		} else {
			return appendHex(b, uint64(exp))
		}
	}
}

func AppendFloat(b []byte, f float64) []byte {
	u := math.Float64bits(f)
	negate := (u >> 63) != 0
	uexp := (u >> 52) & 0x7FF
	frac := u & ((1 << 52) - 1)

	if uexp == 0 {
		// zero and subnormals - encode as zero
		return append(b, " 0"...)
	} else if uexp == 0x7FF {
		// infinity & nans
		if frac != 0 {
			return append(b, " nan"...)
		} else if negate {
			return append(b, " -inf"...)
		} else {
			return append(b, " inf"...)
		}
	} else {
		// normal
		b = append(b, ' ')
		if negate {
			b = append(b, '-')
		}

		// sig is currently in fractional 1.a x 2^b form
		// need to convert to whole number a x 2^b form
		sig := frac | 1<<52
		ctz := bits.TrailingZeros64(sig)
		sig >>= ctz

		exp := int(uexp) - 1023 - (52 - ctz)
		if 0 <= exp && exp < 8 {
			// convert to non-exponent form
			return appendHex(b, sig<<exp)
		} else {
			// print in <sig>p<exp> form
			b = appendHex(b, sig)
			b = append(b, 'p')
			if exp < 0 {
				b = append(b, '-')
				return appendHex(b, uint64(-exp))
			} else {
				return appendHex(b, uint64(exp))
			}
		}
	}
}

func appendUint(b []byte, negate bool, u uint64) []byte {
	b = append(b, ' ')
	if u == 0 {
		return append(b, '0')
	}
	if negate {
		b = append(b, '-')
	}
	if ctz := bits.TrailingZeros64(u); ctz < 8 {
		return appendHex(b, u)
	} else {
		b = appendHex(b, u>>ctz)
		b = append(b, 'p')
		return appendHex(b, uint64(ctz))
	}
}

func AppendUint(b []byte, u uint64) []byte {
	return appendUint(b, false, u)
}

func AppendInt(b []byte, i int64) []byte {
	if i < 0 {
		return appendUint(b, true, uint64(-i))
	} else {
		return appendUint(b, false, uint64(i))
	}
}

func AppendString(b []byte, s string) []byte {
	b = append(b, ' ')
	b = appendHex(b, uint64(len(s)))
	b = append(b, ':')
	return append(b, s...)
}

func AppendBytes(b, val []byte) []byte {
	b = append(b, ' ')
	b = appendHex(b, uint64(len(val)))
	b = append(b, '|')
	return append(b, val...)
}

func AppendArrayStart(b []byte) []byte {
	return append(b, " ["...)
}

func AppendArrayEnd(b []byte) []byte {
	return append(b, " ]"...)
}

func AppendMapStart(b []byte) []byte {
	return append(b, " {"...)
}

func AppendMapEnd(b []byte) []byte {
	return append(b, " }"...)
}

func AppendEntryStart(b []byte, typ EntryType) []byte {
	return append(b, byte(typ))
}

func AppendEntryEnd(b []byte) []byte {
	return append(b, '\n')
}

func BuildEntry(typ EntryType, vals ...interface{}) ([]byte, error) {
	b := []byte{byte(typ)}
	for _, val := range vals {
		var err error
		b, err = AppendValue(b, reflect.ValueOf(val))
		if err != nil {
			return nil, err
		}
	}
	return AppendEntryEnd(b), nil
}

var ErrUnknownType = errors.New("unknown type")

func AppendValue(b []byte, v reflect.Value) ([]byte, error) {
	switch typ := v.Type(); typ.Kind() {
	case reflect.Int, reflect.Int8, reflect.Int16, reflect.Int32, reflect.Int64:
		return AppendInt(b, v.Int()), nil
	case reflect.Uint, reflect.Uint8, reflect.Uint16, reflect.Uint32, reflect.Uint64, reflect.Uintptr:
		return AppendUint(b, v.Uint()), nil
	case reflect.Bool:
		return AppendBool(b, v.Bool()), nil
	case reflect.Float32, reflect.Float64:
		return AppendFloat(b, v.Float()), nil
	case reflect.Slice:
		if typ.Elem().Kind() == reflect.Uint8 {
			return AppendBytes(b, v.Bytes()), nil
		} else {
			b = AppendArrayStart(b)
			for i, n := 0, v.Len(); i < n; i++ {
				var err error
				b, err = AppendValue(b, v.Index(i))
				if err != nil {
					return nil, err
				}
			}
			return AppendArrayEnd(b), nil
		}
	case reflect.Map:
		b = AppendMapStart(b)
		iter := v.MapRange()
		for iter.Next() {
			var err error
			b, err = AppendValue(b, iter.Key())
			if err != nil {
				return nil, err
			}
			b, err = AppendValue(b, iter.Value())
			if err != nil {
				return nil, err
			}
		}
		return AppendMapEnd(b), nil
	case reflect.String:
		return AppendString(b, v.String()), nil
	default:
		return nil, ErrUnknownType
	}
}
