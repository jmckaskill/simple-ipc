package ipc

import (
	"errors"
	"fmt"
	"io"
	"log"
	"math/rand"
	"net"
	"os"
	"syscall"
)

type UnixConn interface {
	ReadMsgUnix(b, oob []byte) (n, oobn, flags int, addr *net.UnixAddr, err error)
	WriteMsgUnix(b, oob []byte, addr *net.UnixAddr) (n, oobn int, err error)
}

type File interface {
	Fd() uintptr
}

var ErrShortWrite = errors.New("short write")

func SendUnixMsg(c UnixConn, msg []byte, files []File) error {
	var oob []byte

	if len(files) > 0 {
		fds := make([]int, len(files))
		for i, f := range files {
			fds[i] = int(f.Fd())
		}
		oob = syscall.UnixRights(fds...)
	}

	n, oobn, err := c.WriteMsgUnix(msg, oob, nil)
	if err != nil {
		return err
	} else if n < len(msg) || oobn < len(oob) {
		return ErrShortWrite
	}
	return nil
}

func ReadUnixMsg(c UnixConn, buf []byte) (int, []*os.File, error) {
	oob := [128]byte{}
	n, oobn, _, _, err := c.ReadMsgUnix(buf, oob[:])
	if err != nil {
		return n, nil, err
	}
	var files []*os.File
	if oobn > 0 {
		cmsgs, err := syscall.ParseSocketControlMessage(oob[:oobn])
		if err != nil {
			return n, nil, err
		}
		for i := range cmsgs {
			fds, err := syscall.ParseUnixRights(&cmsgs[i])
			if err == nil {
				for _, fd := range fds {
					files = append(files, os.NewFile(uintptr(fd), ""))
				}
			}
		}
	}
	return n, files, nil
}

func DialUnix(path string) (*net.UnixConn, error) {
	return net.DialUnix("unixpacket", nil, &net.UnixAddr{
		Net:  "unixpacket",
		Name: path,
	})
}

func Dial(path string) (io.ReadWriteCloser, error) {
	return DialUnix(path)
}

func isAlreadyInUse(err error) bool {
	errno := syscall.Errno(0)
	return errors.As(err, &errno) && errno == syscall.EADDRINUSE
}

var ErrConflict = errors.New("too much conflict on creating socket")

func ListenUnix(path string, overwrite bool) (*net.UnixListener, error) {
	ln, err := net.ListenUnix("unixpacket", &net.UnixAddr{
		Net:  "unixpacket",
		Name: path,
	})
	if err == nil {
		return ln, nil
	} else if !overwrite || !isAlreadyInUse(err) {
		return nil, err
	}

	var tmppath string

	for i := 0; ; i++ {
		if i == 5 {
			return nil, ErrConflict
		}
		tmppath = fmt.Sprintf("%s.tmp%x", path, rand.Uint64())
		log.Printf("trying %v", tmppath)
		ln, err = net.ListenUnix("unixpacket", &net.UnixAddr{
			Net:  "unixpacket",
			Name: tmppath,
		})
		log.Printf("got %v %v", ln, err)
		if err == nil {
			break
		} else if !isAlreadyInUse(err) {
			return nil, err
		}
	}

	// try and do an atomic overwrite
	log.Printf("trying rename")
	if err := os.Rename(tmppath, path); err != nil {
		ln.Close()
		return nil, err
	}

	return ln, nil
}
