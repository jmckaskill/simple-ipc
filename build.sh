#!/bin/sh
rm -f server test client rpc_test
gcc -g -Og -Wall rpc_test.c -o rpc_test && ./rpc_test || exit 1
gcc -g -Og -Wall server.c rpc.c -o server || exit 1
gcc -g -Os -Wall client.c rpc.c -o client || exit 1
