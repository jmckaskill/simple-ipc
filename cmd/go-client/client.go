package main

import (
	"log"
	"os"

	"github.com/jmckaskill/simple-ipc/go-ipc"
)

func must(err error) {
	if err != nil {
		panic(err)
	}
}

func main() {
	log.Printf("opening sock")
	c, err := ipc.DialUnix(os.Args[1])
	must(err)
	log.Printf("sock opened")

	msg, err := ipc.BuildEntry(ipc.RequestEntry, "cmd", 1, 2, 3)
	must(err)

	log.Printf("sending msg %s", msg)
	must(ipc.SendUnixMsg(c, msg, nil))

	rp, wp, err := os.Pipe()
	must(err)
	msg, err = ipc.BuildEntry(ipc.RequestEntry, "pipe")
	must(err)

	log.Printf("sending msg %s with %v", msg, wp)
	must(ipc.SendUnixMsg(c, msg, []ipc.File{wp}))
	wp.Close()

	log.Printf("start read")
	buf := [24]byte{}
	n, err := rp.Read(buf[:])
	log.Printf("read finished %q %v", string(buf[:n]), err)
	rp.Close()

}
