// +build windows

package ipc

import (
	"errors"
	"log"
	"net"
	"os"
	"strconv"
	"syscall"
	"time"
	"unsafe"
)

var (
	kernel32                        = syscall.NewLazyDLL("kernel32.dll")
	procCreateNamedPipeA            = kernel32.NewProc("CreateNamedPipeA")
	procConnectNamedPipe            = kernel32.NewProc("ConnectNamedPipe")
	procSetNamedPipeHandleState     = kernel32.NewProc("SetNamedPipeHandleState")
	procGetNamedPipeInfo            = kernel32.NewProc("GetNamedPipeInfo")
	procGetNamedPipeClientProcessId = kernel32.NewProc("GetNamedPipeClientProcessId")
	procGetNamedPipeServerProcessId = kernel32.NewProc("GetNamedPipeServerProcessId")
	ErrNotImplemented               = errors.New("not implemented")
)

const (
	cPIPE_ACCESS_DUPLEX         = 3
	cPIPE_TYPE_MESSAGE          = 4
	cPIPE_READMODE_MESSAGE      = 2
	cPIPE_WAIT                  = 0
	cPIPE_REJECT_REMOTE_CLIENTS = 8
	cPIPE_UNLIMITED_INSTANCES   = 255
	cPROCESS_DUP_HANDLE         = 0x40

	cERROR_PIPE_CONNECTED = 535
)

type WinPipeAddress struct {
	path string
	pid  int
}

var _ net.Addr = WinPipeAddress{}

func (w WinPipeAddress) Network() string { return "winpipe" }
func (w WinPipeAddress) String() string  { return w.path + ":" + strconv.Itoa(w.pid) }

type WinPipeListener struct {
	createNamedPipeA            uintptr
	connectNamedPipe            uintptr
	getNamedPipeClientProcessId uintptr
	pathz                       []byte
	path                        string
}

var _ net.Listener = (*WinPipeListener)(nil)

func Listen(path string) (*WinPipeListener, error) {
	path = `\\.\pipe\` + path
	log.Printf("listen %v", path)
	if err := procCreateNamedPipeA.Find(); err != nil {
		return nil, err
	}
	if err := procConnectNamedPipe.Find(); err != nil {
		return nil, err
	}
	if err := procGetNamedPipeClientProcessId.Find(); err != nil {
		return nil, err
	}
	return &WinPipeListener{
		createNamedPipeA:            procCreateNamedPipeA.Addr(),
		connectNamedPipe:            procConnectNamedPipe.Addr(),
		getNamedPipeClientProcessId: procGetNamedPipeClientProcessId.Addr(),
		pathz:                       append([]byte(path), 0),
		path:                        path,
	}, nil
}

func (w *WinPipeListener) Close() error   { return ErrNotImplemented }
func (w *WinPipeListener) Addr() net.Addr { return WinPipeAddress{w.path, os.Getpid()} }

func (w *WinPipeListener) Accept() (net.Conn, error) {
	hpipe, _, errno := syscall.Syscall9(w.createNamedPipeA, 8,
		uintptr(unsafe.Pointer(&w.pathz[0])), // LPCSTR lpName
		cPIPE_ACCESS_DUPLEX,                  // DWORD dwOpenMode
		cPIPE_TYPE_MESSAGE|cPIPE_READMODE_MESSAGE|cPIPE_WAIT|cPIPE_REJECT_REMOTE_CLIENTS, // DWORD dwPipeMode
		cPIPE_UNLIMITED_INSTANCES, // DWORD nMaxInstances
		4096,                      // DWORD nOutBufferSize
		4096,                      // DWORD nInBufferSIze
		0,                         // DWORD nDefaultTimeOut
		0,                         // LPSECURITY_ATTRIBUTES lpSecurityAttributes
		0)                         // excess

	if syscall.Handle(hpipe) == syscall.InvalidHandle {
		return nil, syscall.Errno(errno)
	}

	ok, _, errno := syscall.Syscall(w.connectNamedPipe, 2, hpipe, 0, 0)
	if ok == 0 && errno != cERROR_PIPE_CONNECTED {
		syscall.CloseHandle(syscall.Handle(hpipe))
		return nil, syscall.Errno(errno)
	}

	pid := uint32(0)
	ok, _, errno = syscall.Syscall(w.getNamedPipeClientProcessId, 2, hpipe, uintptr(unsafe.Pointer(&pid)), 0)
	if ok == 0 {
		syscall.CloseHandle(syscall.Handle(hpipe))
		return nil, syscall.Errno(errno)
	}

	return &WinPipe{os.NewFile(hpipe, w.path), pid}, nil
}

func Dial(path string) (net.Conn, error) {
	return DialWinPipe(`\\.\pipe\` + path)
}

func DialWinPipe(path string) (*WinPipe, error) {
	f, err := os.Create(path)
	if err != nil {
		return nil, err
	}

	dwmode := uint32(cPIPE_READMODE_MESSAGE)
	ok, _, err := procSetNamedPipeHandleState.Call(f.Fd(), uintptr(unsafe.Pointer(&dwmode)))
	if ok == 0 {
		f.Close()
		return nil, err
	}

	pid := uint32(0)
	ok, _, err = procGetNamedPipeServerProcessId.Call(f.Fd(), uintptr(unsafe.Pointer(&pid)))
	if ok == 0 {
		f.Close()
		return nil, err
	}

	return &WinPipe{f, pid}, nil
}

type WinPipe struct {
	h         *os.File
	remotePid uint32
}

func (w *WinPipe) ReadFiles(b []byte, files []*os.File) (n, fn int, err error) {
	if len(files) == 0 {
		n, err := w.Read(b)
		return n, 0, err
	}

	n, err = w.Read(b)
	if err != nil {
		return 0, 0, err
	}
	p, err := NewParser(b[:n])
	if err != nil {
		return n, 0, err
	}

	fn = 0
	for p.NextEntry() == WindowsHandleEntry {
		vals, err := p.ParseEntry()
		if err != nil {
			return n, 0, err
		} else if len(vals) == 0 {
			continue
		}
		h, ok := vals[0].(uint64)
		if !ok {
			return n, 0, ErrInvalid
		}
		files[fn] = os.NewFile(uintptr(h), "")
		fn++
	}

	n = copy(b, b[n-p.Len():n])
	return
}

func (w *WinPipe) WriteFiles(b []byte, files []File) (n, fn int, err error) {
	if len(files) == 0 {
		n, err := w.Write(b)
		return n, 0, err
	}

	src, err := syscall.GetCurrentProcess()
	if err != nil {
		return 0, 0, err
	}

	tgt, err := syscall.OpenProcess(cPROCESS_DUP_HANDLE, false, w.remotePid)
	if err != nil {
		return 0, 0, err
	}

	out := make([]byte, 0, len(b)+8*len(files))
	for _, f := range files {
		newh := syscall.Handle(0)
		err := syscall.DuplicateHandle(src, syscall.Handle(f.Fd()), tgt, &newh, syscall.DUPLICATE_SAME_ACCESS, false, 0)
		if err == nil {
			out = AppendEntryStart(out, WindowsHandleEntry)
			out = AppendUint(out, uint64(newh))
			out = AppendEntryEnd(out)
		}
	}
	out = append(out, b...)

	n, err = w.h.Write(out)
	if err != nil {
		return 0, 0, err
	}
	return len(b), len(files), nil
}

func (w *WinPipe) Read(b []byte) (n int, err error)   { return w.h.Read(b) }
func (w *WinPipe) Write(b []byte) (n int, err error)  { return w.h.Write(b) }
func (w *WinPipe) Close() error                       { return w.h.Close() }
func (w *WinPipe) LocalAddr() net.Addr                { return WinPipeAddress{w.h.Name(), os.Getpid()} }
func (w *WinPipe) RemoteAddr() net.Addr               { return WinPipeAddress{w.h.Name(), int(w.remotePid)} }
func (w *WinPipe) SetWriteDeadline(t time.Time) error { return w.SetDeadline(t) }
func (w *WinPipe) SetReadDeadline(t time.Time) error  { return w.SetDeadline(t) }

func (w *WinPipe) SetDeadline(t time.Time) error {
	if t.IsZero() {
		return nil
	}
	return ErrNotImplemented
}
