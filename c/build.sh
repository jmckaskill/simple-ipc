#!/bin/sh
rm -f server test client ipc_test
gcc -g -Og -Wall ipc_test.c -o ipc_test && ./ipc_test || exit 1
gcc -g -Og -lpthread -Wall server.c ipc.c -o server || exit 1
gcc -g -Os -Wall client.c ipc.c -o client || exit 1
