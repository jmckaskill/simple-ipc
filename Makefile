HDRS = libsipc/ipc.h libsipc/ipc-unix.h libsipc/ipc-windows.h
CFLAGS = -Wall -O0 -g -Ilibsipc
LDFLAGS = -g
O = build

.PHONY: all clean test $O/go-client $O/go-server $O/ipc-rc

all: $O/c-client $O/c-server $O/go-server $O/go-client $O/ipc-rc test

$O/%.o: %.c $(HDRS) $(@D)
	@mkdir -p $(@D)
	$(CC) -c -o $@ $< $(CFLAGS)

$O/libsipc_test: $O/libsipc/ipc_test.o
	$(CC) -o $@ $^ $(LDFLAGS)

$O/libsipc.a: $O/libsipc/ipc-unix.o $O/libsipc/ipc-windows.o $O/libsipc/ipc.o
	$(AR) rcs $@ $^

$O/c-client: $O/cmd/c-client/client.o $O/libsipc.a 
	$(CC) -o $@ $^ $(LDFLAGS)

$O/c-server: $O/cmd/c-server/server.o $O/libsipc.a
	$(CC) -o $@ $^ $(LDFLAGS) -lpthread

$O/go-client:
	go build -o $@ ./cmd/go-client

$O/go-server:
	go build -o $@ ./cmd/go-server

$O/ipc-rc:
	go build -o $@ ./cmd/ipc-rc

test: $O/libsipc_test
	$O/libsipc_test
	go test ./go-ipc

clean:
	rm -rf build
