// The Go side of bench/gob_bench.c, on the same feed of 100 entries.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"bytes"
	"encoding/gob"
	"fmt"
	"io"
	"testing"
)

type GbEntry struct {
	ID    int
	Title string
	Tags  []string
	Score float64
}

type GbFeed struct {
	Entries []GbEntry
}

var sinkGobInt int

func gobFeed() *GbFeed {
	f := &GbFeed{Entries: make([]GbEntry, 100)}
	for i := range f.Entries {
		f.Entries[i] = GbEntry{i, fmt.Sprintf("Item %d and friends", i),
			[]string{"tea", fmt.Sprintf("batch-%d", i%7)}, float64(i) * 1.25}
	}
	return f
}

func BenchmarkGobEncode(b *testing.B) {
	f := gobFeed()
	for b.Loop() {
		if err := gob.NewEncoder(io.Discard).Encode(f); err != nil {
			b.Fatal(err)
		}
	}
}

func BenchmarkGobEncodeAgain(b *testing.B) {
	f := gobFeed()
	e := gob.NewEncoder(io.Discard)
	for b.Loop() {
		if err := e.Encode(f); err != nil {
			b.Fatal(err)
		}
	}
}

func BenchmarkGobDecode(b *testing.B) {
	var buf bytes.Buffer
	if err := gob.NewEncoder(&buf).Encode(gobFeed()); err != nil {
		b.Fatal(err)
	}
	stream := buf.Bytes()
	r := bytes.NewReader(stream)
	for b.Loop() {
		r.Reset(stream)
		var out GbFeed
		if err := gob.NewDecoder(r).Decode(&out); err != nil {
			b.Fatal(err)
		}
		sinkGobInt = len(out.Entries)
	}
}
