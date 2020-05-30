package ipc

import (
	"math"
	"math/big"
	"reflect"
	"testing"
)

func TestFormat(t *testing.T) {
	tests := []struct {
		expected string
		got      []byte
		val      interface{}
	}{
		{" T", AppendBool(nil, true), true},
		{" F", AppendBool(nil, false), false},
		{" 0", AppendUint(nil, 0), 0},
		{" ff", AppendUint(nil, 0xff), 0xff},
		{" 1p8", AppendUint(nil, 0x100), 0x100},
		{" 180", AppendUint(nil, 0x180), 0x180},
		{" 1pc", AppendUint(nil, 0x1000), 0x1000},
		{" 1p1f", AppendUint(nil, 0x80000000), 0x80000000},
		{" -ff", AppendInt(nil, -0xff), -0xff},
		{" -7p1c", AppendInt(nil, -0x70000000), -0x70000000},
		{" 1abcdp-e", AppendFloat(nil, 0x1abcdp-14), 0x1abcdp-14},
		{" nan", AppendFloat(nil, math.NaN()), math.NaN()},
		{" inf", AppendFloat(nil, math.Inf(1)), math.Inf(1)},
		{" -inf", AppendFloat(nil, math.Inf(-1)), math.Inf(-1)},
		{" 0", AppendFloat(nil, 0), float64(0)},
		{" 80", AppendFloat(nil, 128), float64(128)},
		{" 1p8", AppendFloat(nil, 256), float64(256)},
		{" 0", AppendFloat(nil, -math.Float64frombits(1)), -math.Float64frombits(1)}, // subnormal

		{" 1abcdp-e", AppendBigFloat(nil, big.NewFloat(0x1abcdp-14)), nil},
		{" inf", AppendBigFloat(nil, big.NewFloat(math.Inf(1))), nil},
		{" -inf", AppendBigFloat(nil, big.NewFloat(math.Inf(-1))), nil},
		{" 0", AppendBigFloat(nil, big.NewFloat(0)), nil},
		{" 80", AppendBigFloat(nil, big.NewFloat(128)), nil},
		{" 1p8", AppendBigFloat(nil, big.NewFloat(256)), nil},
		{" -1p-432", AppendBigFloat(nil, big.NewFloat(-math.Float64frombits(1))), nil}, // subnormal

		{" 3:abc", AppendString(nil, "abc"), "abc"},
		{" 3|123", AppendBytes(nil, []byte("123")), []byte("123")},
	}
	for idx, test := range tests {
		got := string(test.got)
		if test.expected != got {
			t.Errorf("ERR %d, expected %q, got %q", idx, test.expected, got)
		} else {
			t.Logf("OK %d, got %q as expected", idx, got)
		}
		if test.val != nil {
			generic, err := AppendValue(nil, reflect.ValueOf(test.val))
			if err != nil {
				t.Errorf("ERR %d generic, unexpected error %v", idx, err)
			}
			if s := string(generic); test.expected != s {
				t.Errorf("ERR %d generic, expected %q, got %q", idx, test.expected, s)
			} else {
				t.Logf("OK %d generic, got %q for %v as expected", idx, s, test.val)
			}
		}
	}
}

func TestEntry(t *testing.T) {
	expected := "R 5:mycmd 3 1p-1 [ 1 2 3 ] { 3:foo 3:bar } T 3|abc\n"
	b, err := BuildEntry(RequestEntry, "mycmd", 3, 0.5, []int{1, 2, 3}, map[string]string{"foo": "bar"}, true, []byte("abc"))
	if err != nil {
		t.Errorf("got unexpected error %v", err)
	} else if string(b) != expected {
		t.Errorf("expected %q, got %q", expected, string(b))
	} else {
		t.Logf("got expected %q", expected)
	}
}
