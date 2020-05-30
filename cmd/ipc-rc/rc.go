package main

import (
	"encoding/hex"
	"errors"
	"flag"
	"fmt"
	"math"
	"math/big"
	"os"
	"runtime/debug"
	"strconv"
	"strings"
	"unicode/utf8"

	"github.com/c-bata/go-prompt"
	"github.com/jmckaskill/simple-ipc/go-ipc"
)

var Verbose = false

type Exit int

func handleExit() {
	switch v := recover().(type) {
	case nil:
		return
	case Exit:
		os.Exit(int(v))
	default:
		fmt.Println(v)
		fmt.Println(string(debug.Stack()))
	}
}

var seperators = "\t \r,:;]}"

func tokenize(s string) (value, snext string) {
	value = s
	if idx := strings.IndexAny(s, seperators); idx >= 0 {
		value = s[:idx]
	}
	snext = s[len(value):]
	return
}

func decodeString(s string) (value, snext string, err error) {
	buf := []byte{}
	for {
		if len(s) == 0 {
			return "", "", errors.New("no terminating \"")
		} else if s[0] == '"' {
			return string(buf), s[1:], nil
		}
		c, multibyte, snext, err := strconv.UnquoteChar(s, '"')
		if err != nil {
			return "", "", err
		}
		if c < utf8.RuneSelf || !multibyte {
			buf = append(buf, byte(c))
		} else {
			var tmp [utf8.UTFMax]byte
			n := utf8.EncodeRune(tmp[:], c)
			buf = append(buf, tmp[:n]...)
		}
		s = snext
	}
}

func executor(s string) {
	buf := []byte{'R'}
	for len(s) > 0 {
		var tok string
		var err error
		ch := s[0]
		switch ch {
		case '"':
			tok, s, err = decodeString(s[1:])
			if err != nil {
				fmt.Printf("error decoding string - %v\n", err)
				return
			}
			buf = ipc.AppendString(buf, tok)
		case '|':
			tok, s = tokenize(s[1:])
			value, err := hex.DecodeString(tok)
			if err != nil {
				fmt.Printf("error decoding bytes %v - %v\n", tok, err)
				return
			}
			buf = ipc.AppendBytes(buf, value)
		case '\t', ' ', '\r', ',', ':', ';':
			// seperators
			s = s[1:]
		case '[':
			buf = ipc.AppendArrayStart(buf)
			s = s[1:]
		case ']':
			buf = ipc.AppendArrayEnd(buf)
			s = s[1:]
		case '{':
			buf = ipc.AppendMapStart(buf)
			s = s[1:]
		case '}':
			buf = ipc.AppendMapEnd(buf)
			s = s[1:]
		default:
			switch {
			case 'a' <= ch && ch <= 'z' || 'A' <= ch && ch <= 'Z' || ch == '_':
				tok, s = tokenize(s)
				switch {
				case strings.EqualFold(tok, "true"):
					buf = ipc.AppendBool(buf, true)
				case strings.EqualFold(tok, "false"):
					buf = ipc.AppendBool(buf, false)
				case strings.EqualFold(tok, "nan"):
					buf = ipc.AppendFloat(buf, math.NaN())
				case strings.EqualFold(tok, "inf"):
					buf = ipc.AppendFloat(buf, math.Inf(1))
				default:
					buf = ipc.AppendString(buf, tok)
				}
			case '0' <= ch && ch <= '9' || ch == '-':
				tok, s = tokenize(s)
				f, _, err := big.ParseFloat(tok, 0, 64, big.ToNearestEven)
				if err != nil {
					fmt.Printf("error parsing number %v - %v\n", tok, err)
					return
				}
				buf = ipc.AppendBigFloat(buf, f)
			default:
				fmt.Printf("unexpected character %c in input\n", ch)
				return
			}
		}
	}
	if len(buf) > 1 {
		buf = ipc.AppendEntryEnd(buf)
		fmt.Printf("TX %q\n", buf)
	}
}

func completer(t prompt.Document) []prompt.Suggest {
	return nil
}

func main() {
	defer handleExit()
	flag.Usage = func() {
		fmt.Fprintf(flag.CommandLine.Output(), "usage: %s [options] <socket>\n", os.Args[0])
		flag.PrintDefaults()
	}
	flag.BoolVar(&Verbose, "verbose", Verbose, "Verbose output")
	flag.Parse()
	if len(flag.Args()) != 1 {
		flag.Usage()
		os.Exit(2)
	}

	/*
		sockpath := flag.Args()[0]
		c, err := ipc.Dial(sockpath)
		if err != nil {
			fmt.Fprintf(os.Stderr, "failed to connect to %v: %v\n", sockpath, err)
			os.Exit(3)
		}
		if Verbose {
			fmt.Fprintf(os.Stdout, "connected to %v\n", sockpath)
		}
	*/

	prompt.New(executor, completer).Run()
}
