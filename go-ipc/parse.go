package ipc

import (
	"errors"
	"io"
	"math"
	"math/bits"
)

var ErrInvalid = errors.New("invalid input")
var ErrTooDeep = errors.New("too deep")
var ErrDuplicateKey = errors.New("duplicate key")
var ErrInsufficientData = errors.New("insufficient data")

type ReferenceLookup func([]byte) interface{}

type Parser struct {
	b []byte
}

func (p *Parser) skip(i int) {
	p.b = p.b[i:]
}

func (p *Parser) read() byte {
	ch := p.b[0]
	p.b = p.b[1:]
	return ch
}

func (p *Parser) peek() byte {
	return p.b[0]
}

var (
	hexValid = []byte{
		0x00, 0x00, 0x00, 0x00, // 00-1F
		0x00, 0x00, 0xFF, 0x03, // 20-3F
		0x00, 0x00, 0x00, 0x00, // 40-5F
		0x7E, 0x00, 0x00, 0x00, // 60-7F
		0x00, 0x00, 0x00, 0x00, // 80-9F
		0x00, 0x00, 0x00, 0x00, // A0-BF
		0x00, 0x00, 0x00, 0x00, // C0-DF
		0x00, 0x00, 0x00, 0x00, // E0-FF
	}
	hexLookup = []byte{
		0x00, 0x0a, 0x0b, 0x0c, //
		0x0d, 0x0e, 0x0f, 0x00, //
		0x00, 0x00, 0x00, 0x00, //
		0x00, 0x00, 0x00, 0x00, //
		0x00, 0x01, 0x02, 0x03, //
		0x04, 0x05, 0x06, 0x07, //
		0x08, 0x09,
	}
)

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

func isValidHex(ch byte) bool {
	// Top 5 bits indicates index. Bottom 3 bits indicates
	// which bit within that to use.
	return (hexValid[ch>>3] & (1 << (ch & 7))) != 0
}

func hexValue(ch byte) byte {
	// Index given by ch & 11111b. See lookup above
	// ch must be a valid character per is_valid_hex
	return hexLookup[ch&0x1F]
}

func (p *Parser) readHex(start byte) (v uint64, overflow int, err error) {
	if !isValidHex(start) {
		return 0, 0, ErrInvalid
	}

	if start == '0' {
		// leading zeros are not allowed so this must be just 0
		// We don't need to check the next byte as the next level up will
		// do that for us.
		return 0, 0, nil
	}

	v = uint64(hexValue(start))

	for isValidHex(p.peek()) {
		n := v << 4
		if n < v {
			// We can't fit it in the return value.
			// Now count the number of excess bits.
			overflow := 0
			for isValidHex(p.peek()) {
				p.read()
				overflow += 4
			}
			return v, overflow, nil
		}
		v = n | uint64(hexValue(p.read()))
	}

	return v, 0, nil
}

func (p *Parser) readExponent(overflow int) (int, error) {
	p.skip(1) // skip over 'p'

	negate := false
	ch := p.read()
	if ch == '-' {
		negate = true
		ch = p.read()
	}

	uexp, expoverflow, err := p.readHex(ch)
	if err != nil {
		return 0, err
	}

	uexp += uint64(overflow)

	if expoverflow > 0 || uexp > math.MaxInt32 {
		// the exponent field has overflowed
		// return a max/min exponent, the outer code can convert this to infinity
		if negate {
			return math.MaxInt32, nil
		} else {
			return math.MinInt32, nil
		}
	}

	if negate {
		return -int(uexp), nil
	}
	return int(uexp), nil
}

func (p *Parser) readBytes(overflow int, sz uint64) ([]byte, error) {
	p.skip(1) // | or : separator
	// require at least one byte past the read size so that we don't consume
	// the newline at the end of the message
	if overflow > 0 || sz >= uint64(len(p.b)) {
		return nil, ErrInsufficientData
	}
	ret := p.b[:sz]
	p.b = p.b[sz:]
	return ret, nil
}

func buildFloat64(negate bool, sig uint64, exp int) float64 {
	if sig == 0 {
		return 0
	}
	// convert to fractional form
	// have a x 2^b
	// want 1.a x 2^b
	clz := bits.LeadingZeros64(sig)
	sig <<= clz + 1
	exp += 63 - clz

	// fractional part is 52 bits
	// round up if the 53 bit is set
	if (sig & (1 << (64 - 53))) != 0 {
		sig += 1 << (64 - 53)
	}

	uexp := uint64(0)
	if exp < -1022 {
		// subnormal - round to zero
		sig = 0
		uexp = 0
	} else if exp > 1023 {
		// too large - round to infinity
		sig = 0
		uexp = 0x7FF
	} else {
		// encode into signed bias form
		uexp = uint64(1023 + exp)
	}

	// now encode the result
	u := uint64(0)
	if negate {
		u = 1 << 63
	}
	u |= uexp << 52
	u |= sig >> (64 - 52)

	return math.Float64frombits(u)
}

const maxDepth = 16

func (p *Parser) readNext(depth int) (interface{}, error) {
	if depth > maxDepth {
		return nil, ErrTooDeep
	}

	switch p.read() {
	case '\n':
		return nil, io.EOF
	case ' ':
		break
	default:
		return nil, ErrInvalid
	}

	switch ch := p.read(); ch {
	case 'T':
		return true, nil
	case 'F':
		return false, nil

	case '{':
		ret := map[interface{}]interface{}{}
		for !(p.b[0] == ' ' && p.b[1] == '}') {
			k, err := p.readNext(depth + 1)
			if err != nil {
				return nil, err
			}
			v, err := p.readNext(depth + 1)
			if err != nil {
				return nil, err
			}
			if _, ok := ret[k]; ok {
				return nil, ErrDuplicateKey
			}
			ret[k] = v
		}
		p.skip(2) // " }"
		return ret, nil

	case '[':
		ret := []interface{}{}
		for !(p.b[0] == ' ' && p.b[1] == ']') {
			v, err := p.readNext(depth + 1)
			if err != nil {
				return nil, err
			}
			ret = append(ret, v)
		}
		p.skip(2) // " ]"
		return ret, nil

	case 'n':
		if !(p.b[0] == 'a' && p.b[1] == 'n') {
			return nil, ErrInvalid
		}
		p.skip(2)
		return math.NaN(), nil

	case 'i':
		if !(p.b[0] == 'n' && p.b[1] == 'f') {
			return nil, ErrInvalid
		}
		p.skip(2)
		return math.Inf(1), nil

	case '-':
		if p.b[0] == 'i' && p.b[1] == 'n' && p.b[2] == 'f' {
			p.skip(3)
			return math.Inf(-1), nil
		}
		sig, overflow, err := p.readHex(p.read())
		if err != nil {
			return nil, err
		}

		if p.peek() == 'p' {
			// real with exponent
			// the exponent must be maximized
			if (sig & 1) == 0 {
				return nil, ErrInvalid
			}
			exp, err := p.readExponent(overflow)
			if err != nil {
				return nil, err
			} else if exp < 0 || exp >= bits.LeadingZeros64(sig) {
				return buildFloat64(true, sig, exp), nil
			} else {
				return -int64(sig << exp), nil
			}
		} else {
			// real without exponent
			// this is allowed if the last byte is non-zero
			// as this is the negative branch a value of zero is not allowed
			if (sig & 0xff) == 0 {
				return nil, ErrInvalid
			}
			return -int64(sig), nil
		}

	default:
		sig, overflow, err := p.readHex(ch)
		if err != nil {
			return nil, err
		}

		switch p.peek() {
		default:
			// no exponent - allowed for 0 or the bottom byte being non-zero
			if sig != 0 && (sig&0xff) == 0 {
				return nil, ErrInvalid
			}
			return sig, nil
		case 'p':
			// exponent - significand must be minimized
			if (sig & 1) == 0 {
				return nil, ErrInvalid
			}
			exp, err := p.readExponent(overflow)
			if err != nil {
				return nil, err
			} else if exp < 0 || exp > bits.LeadingZeros64(sig) {
				return buildFloat64(false, sig, exp), nil
			} else {
				return sig << exp, nil
			}
		case '|':
			return p.readBytes(overflow, sig)
		case ':':
			b, err := p.readBytes(overflow, sig)
			return string(b), err
		}
	}
}

func NewParser(b []byte) (*Parser, error) {
	if len(b) > 0 && b[len(b)-1] != ' ' && b[len(b)-1] != '\n' {
		return nil, ErrInvalid
	}
	return &Parser{b}, nil
}

func (p *Parser) Len() int { return len(p.b) }

func (p *Parser) NextEntry() EntryType {
	if len(p.b) == 0 {
		return EndOfMessage
	}
	return EntryType(p.peek())
}

func (p *Parser) ParseEntry() ([]interface{}, error) {
	p.skip(1) // entry type
	ret := []interface{}{}
	for {
		val, err := p.readNext(1)
		if err == io.EOF {
			break
		} else if err != nil {
			return nil, err
		}
		ret = append(ret, val)
	}
	return ret, nil
}
