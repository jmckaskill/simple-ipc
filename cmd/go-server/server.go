package main

import (
	"log"
	"net"
	"os"
	"time"

	"github.com/jmckaskill/simple-ipc/go-ipc"
)

func must(err error) {
	if err != nil {
		panic(err)
	}
}

func handler(c net.Conn) error {
	buf := make([]byte, 4096)
	files := make([]*os.File, 256)
	for {
		n, filesn, err := c.(ipc.FileConn).ReadFiles(buf, files)
		if err != nil {
			return err
		}
		p, err := ipc.NewParser(buf[:n])
		if err != nil {
			return err
		}
		for typ := p.NextEntry(); typ != ipc.EndOfMessage; typ = p.NextEntry() {
			vals, err := p.ParseEntry()
			if err != nil {
				return err
			}
			log.Printf("entry type %c, values %v", typ, vals)
		}
		for _, f := range files[:filesn] {
			log.Printf("have file %v", f.Fd())
			log.Printf("begin wait")
			time.Sleep(4 * time.Second)
			f.WriteString("hello from go")
			f.Close()
			log.Printf("end wait")
		}

		if _, err := c.Write(buf[:n]); err != nil {
			return err
		}
	}
}

func main() {
	ln, err := ipc.Listen(os.Args[1])
	must(err)

	for {
		c, err := ln.Accept()
		must(err)
		go func() {
			log.Printf("handler finished %v", handler(c))
		}()
	}
}
