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

func handler(c *net.UnixConn) error {
	buf := make([]byte, 4096)
	for {
		n, files, err := ipc.ReadUnixMsg(c, buf)
		if err != nil {
			return err
		}
		p, err := ipc.NewParser(buf[:n])
		if err != nil {
			return err
		}
		for !p.Finished() {
			typ, err := p.NextEntry()
			if err != nil {
				return err
			}
			vals, err := p.ParseEntry()
			if err != nil {
				return err
			}
			log.Printf("entry type %c, values %v", typ, vals)
		}
		for _, f := range files {
			log.Printf("have file %v", f.Fd())
			time.Sleep(4 * time.Second)
			f.WriteString("hello from go")
			f.Close()
		}

		if _, err := c.Write(buf[:n]); err != nil {
			return err
		}
	}
}

func main() {
	ln, err := ipc.ListenUnix(os.Args[1], true)
	must(err)

	for {
		c, err := ln.AcceptUnix()
		must(err)
		go func() {
			log.Printf("handler finished %v", handler(c))
		}()
	}
}
