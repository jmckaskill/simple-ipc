// +build !windows

package ipc

import (
	"errors"
	"fmt"
	"log"
	"math/rand"
	"net"
	"os"
	"syscall"
)

type UnixConn struct {
	*net.UnixConn
}

var ErrShortWrite = errors.New("short write")

func (c *UnixConn) WriteFiles(b []byte, files []File) (n, fn int, err error) {
	var oob []byte

	if len(files) > 0 {
		fds := make([]int, len(files))
		for i, f := range files {
			fds[i] = int(f.Fd())
		}
		oob = syscall.UnixRights(fds...)
	}

	n, oobn, err := c.WriteMsgUnix(b, oob, nil)
	if err != nil {
		return 0, 0, err
	} else if n < len(b) || oobn < len(oob) {
		return 0, 0, ErrShortWrite
	}
	return len(b), len(files), nil
}

func (c *UnixConn) ReadFiles(buf []byte, files []*os.File) (n, fn int, err error) {
	oob := [128]byte{}
	n, oobn, _, _, err := c.ReadMsgUnix(buf, oob[:])
	if err != nil {
		return n, 0, err
	}
	fn = 0
	if oobn > 0 {
		cmsgs, err := syscall.ParseSocketControlMessage(oob[:oobn])
		if err != nil {
			return n, 0, err
		}
		for i := range cmsgs {
			fds, err := syscall.ParseUnixRights(&cmsgs[i])
			if err == nil {
				for _, fd := range fds {
					if fn < len(files) {
						files[fn] = os.NewFile(uintptr(fd), "")
						fn++
					} else {
						syscall.Close(fd)
					}
				}
			}
		}
	}
	return n, fn, nil
}

func DialUnix(path string) (*UnixConn, error) {
	c, err := net.DialUnix("unixpacket", nil, &net.UnixAddr{
		Net:  "unixpacket",
		Name: path,
	})
	return &UnixConn{c}, err
}

func Dial(path string) (*UnixConn, error) {
	return DialUnix(path)
}

func isAlreadyInUse(err error) bool {
	errno := syscall.Errno(0)
	return errors.As(err, &errno) && errno == syscall.EADDRINUSE
}

var ErrConflict = errors.New("too much conflict on creating socket")

func Listen(path string) (*net.UnixListener, error) {
	return ListenUnix(path, true)
}

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
