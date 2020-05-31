package ipc

import (
	"reflect"
	"testing"
)

func TestParse(t *testing.T) {
	expect := []struct {
		typ  EntryType
		vals []interface{}
	}{
		{WindowsHandleEntry, []interface{}{uint64(3)}},
		{RequestEntry, []interface{}{
			"mycmd",
			uint64(3),
			0.5,
			[]interface{}{uint64(1), uint64(2), uint64(3)},
			map[interface{}]interface{}{"foo": "bar"},
			true,
			[]byte("abc"),
		},
		},
	}
	test := "W 3\nR 5:mycmd 3 1p-1 [ 1 2 3 ] { 3:foo 3:bar } T 3|abc\n"
	p, err := NewParser([]byte(test))
	if err != nil {
		t.Fatalf("unexpected error %v", err)
	}
	for idx, entry := range expect {
		typ := p.NextEntry()
		if typ != entry.typ {
			t.Errorf("ERR %d, expected type %d, got %d", idx, entry.typ, typ)
			break
		} else {
			t.Logf("OK %d, got expected type %d", idx, entry.typ)
		}
		vals, err := p.ParseEntry()
		if err != nil {
			t.Errorf("ERR %d getting values %v", idx, err)
		} else if !reflect.DeepEqual(vals, entry.vals) {
			t.Errorf("ERR %d, expected values %v, got %v", idx, entry.vals, vals)
		} else {
			t.Logf("OK %d, got expected values %v", idx, entry.vals)
		}
	}
}
