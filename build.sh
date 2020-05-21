#!/bin/sh
gcc -g server.c rpc.c -o server || exit 1
gcc -g rpc_test.c -o test && test || exit 1
