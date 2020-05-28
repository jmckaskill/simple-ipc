#!/bin/sh
rm -f server test client ipc_test
gcc -g -O0 -Wall ipc_test.c -o ipc_test && ./ipc_test || exit 1
gcc -g -Os -lpthread -Wall server.c ipc.c unix.c -o server || exit 1
gcc -g -Os -Wall client.c ipc.c unix.c -o client || exit 1
