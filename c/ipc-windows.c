#include "ipc-windows.h"
#include <Windows.h>

void* ipc_win_connect(const char* path, bool overlapped) 
{
	HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
			       0, // no sharing
			       NULL, // default security
			       OPEN_EXISTING, // open existing
			       overlapped ? FILE_FLAG_OVERLAPPED : 0,
			       NULL); // no template
	if (h == INVALID_HANDLE_VALUE) {
		return INVALID_HANDLE_VALUE;
	}

	DWORD dwmode = PIPE_READMODE_MESSAGE;
	if (!SetNamedPipeHandleState(h, &dwmode, NULL, NULL)) {
		CloseHandle(h);
		return INVALID_HANDLE_VALUE;
	}

	return h;
}
void *ipc_win_accept(const char *path, bool overlapped)
{
	HANDLE h = CreateNamedPipeA(path,
		PIPE_ACCESS_DUPLEX | (overlapped ? FILE_FLAG_OVERLAPPED : 0),
		PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
		PIPE_UNLIMITED_INSTANCES,
		4096,
		4096,
		0, // default timeout
		NULL); // default security
	
	if (h == INVALID_HANDLE_VALUE) {
		return INVALID_HANDLE_VALUE;
	}

	if (ConnectNamedPipe(h, NULL) ||
	    GetLastError() == ERROR_PIPE_CONNECTED) {
		return h;
	} else {
		CloseHandle(h);
		return INVALID_HANDLE_VALUE;
	}
}

int ipc_parse_handles(sipc_parser_t *p, void **handles, int hnum)
{
	int n = 0;
	while (sipc_peek(p) == SIPC_WINDOWS_HANDLE) {
		uint64_t u64;
		sipc_start(p);
		if (sipc_uint64(p, &u64) || sipc_end(p) || u64 > UINTPTR_MAX) {
			goto error;
		}
		void *handle = (void *)(uintptr_t)u64;
		if (n > hnum) {
			CloseHandle(handle);
		} else {
			handles[n++] = handle;
		}
	}
	return n;
error:
	for (int i = 0; i < n; i++) {
		CloseHandle(handles[i]);
	}
	return -1;
}

int ipc_append_handle(void *pipe, char *buf, int sz, void *handle)
{
	DWORD flags;
	if (!GetNamedPipeInfo(pipe, &flags, NULL, NULL, NULL)) {
		return -1;
	}
	// get the remote process id
	ULONG pid;
	BOOL ok = (flags & PIPE_SERVER_END) ?
			  GetNamedPipeClientProcessId(pipe, &pid) :
				GetNamedPipeServerProcessId(pipe, &pid);
	if (!ok) {
		return -1;
	}
	HANDLE process = OpenProcess(PROCESS_DUP_HANDLE, FALSE, pid);
	if (process == INVALID_HANDLE_VALUE) {
		return -1;
	}

	void *newhandle;
	if (!DuplicateHandle(GetCurrentProcess(), handle, process,
			&newhandle, DUPLICATE_SAME_ACCESS, FALSE,
			0)) {
		CloseHandle(process);
		return -1;
	}

	int ret = sipc_format(buf, sz, "W %zu\n", newhandle);
	if (ret < 0 || ret > sz) {
		CloseHandle(newhandle);
	}
	CloseHandle(process);
	return ret;
}
