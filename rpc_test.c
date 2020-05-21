#include "rpc.c"

static void test_hex() {
	assert(is_valid_hex('/') == 0);
	assert(is_valid_hex('0') && hex_value('0') == 0x0);
	assert(is_valid_hex('1') && hex_value('1') == 0x1);
	assert(is_valid_hex('2') && hex_value('2') == 0x2);
	assert(is_valid_hex('3') && hex_value('3') == 0x3);
	assert(is_valid_hex('4') && hex_value('4') == 0x4);
	assert(is_valid_hex('5') && hex_value('5') == 0x5);
	assert(is_valid_hex('6') && hex_value('6') == 0x6);
	assert(is_valid_hex('7') && hex_value('7') == 0x7);
	assert(is_valid_hex('8') && hex_value('8') == 0x8);
	assert(is_valid_hex('9') && hex_value('9') == 0x9);
    assert(is_valid_hex(':') == 0);
	assert(is_valid_hex('A') == 0);
    assert(is_valid_hex('@') == 0);
	assert(is_valid_hex('a') && hex_value('a') == 0xa);
	assert(is_valid_hex('b') && hex_value('b') == 0xb);
	assert(is_valid_hex('c') && hex_value('c') == 0xc);
	assert(is_valid_hex('d') && hex_value('d') == 0xd);
	assert(is_valid_hex('e') && hex_value('e') == 0xe);
	assert(is_valid_hex('f') && hex_value('f') == 0xf);
    assert(is_valid_hex('g') == 0);
}

int main() {
    test_hex();
	return 0;
}
