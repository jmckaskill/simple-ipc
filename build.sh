#!/bin/sh
rm -f server test client
gcc -g -Os -Wall rpc_test.c -o test && ./test || exit 1
gcc -g -Os -Wall server.c rpc.c -o server || exit 1
gcc -g -Os -Wall client.c rpc.c -o client || exit 1
