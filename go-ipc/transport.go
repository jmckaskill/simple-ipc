package ipc

import (
	"net"
	"os"
)

type File interface {
	Fd() uintptr
}

type FileConn interface {
	net.Conn
	ReadFiles(b []byte, files []*os.File) (n, fn int, err error)
	WriteFiles(b []byte, files []File) (n, fn int, err error)
}
