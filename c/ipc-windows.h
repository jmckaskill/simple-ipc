#pragma once
#include "ipc.h"
#include <stdbool.h>

void* ipc_win_connect(const char* path, bool overlapped);
void* ipc_win_accept(const char* path, bool overlapped);

int ipc_parse_handles(sipc_parser_t* p, void** handles, int hnum);
int ipc_append_handle(void *pipe, char *buf, int sz, void *handle);
