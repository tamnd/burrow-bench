// The Go side of the JSON benchmarks. Each row does what the row of the same
// name in bench/json_bench.c does, on the same document.
//
// encoding/json/v2 arrived in Go 1.27, so the constraint below keeps older
// toolchains building the rest of the suite and lifts this file to 1.27.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

//go:build go1.27

package bench

import (
	"bytes"
	"encoding/json/jsontext"
	"encoding/json/v2"
	"fmt"
	"io"
	"testing"
)

type jbItem struct {
	ID    int      `json:"id"`
	Name  string   `json:"name"`
	Price float64  `json:"price"`
	Tags  []string `json:"tags"`
	OK    bool     `json:"ok"`
}

var (
	sinkJSONBool  bool
	sinkJSONBytes []byte
	sinkJSONItems []jbItem
	sinkJSONAny   any
	sinkJSONInt   int
)

func jsonDoc() []byte {
	var b bytes.Buffer
	b.WriteString("[")
	for i := 0; i < 100; i++ {
		if i > 0 {
			b.WriteString(",")
		}
		ok := "false"
		if i%3 == 0 {
			ok = "true"
		}
		fmt.Fprintf(&b, `{"id":%d,"name":"item %d","price":%d.%02d,"tags":["tea","green","loose"],"ok":%s}`,
			i, i, i/4, (i%4)*25, ok)
	}
	b.WriteString("]")
	return b.Bytes()
}

func BenchmarkJSONValid(b *testing.B) {
	doc := jsontext.Value(jsonDoc())
	for b.Loop() {
		sinkJSONBool = doc.IsValid()
	}
}

func BenchmarkJSONCompact(b *testing.B) {
	ind := jsontext.Value(jsonDoc())
	if err := ind.Indent(); err != nil {
		b.Fatal(err)
	}
	buf := make([]byte, len(ind))
	for b.Loop() {
		copy(buf, ind)
		v := jsontext.Value(buf)
		v.Compact()
		sinkJSONBytes = v
	}
}

func BenchmarkJSONReadTokens(b *testing.B) {
	doc := jsonDoc()
	r := bytes.NewReader(doc)
	d := jsontext.NewDecoder(r)
	for b.Loop() {
		r.Reset(doc)
		d.Reset(r)
		n := 0
		for {
			t, err := d.ReadToken()
			if err != nil {
				if err != io.EOF {
					b.Fatal(err)
				}
				break
			}
			n += int(t.Kind())
		}
		sinkJSONInt = n
	}
}

func BenchmarkJSONMarshalStruct(b *testing.B) {
	var items []jbItem
	if err := json.Unmarshal(jsonDoc(), &items); err != nil {
		b.Fatal(err)
	}
	for b.Loop() {
		sinkJSONBytes, _ = json.Marshal(items)
	}
}

func BenchmarkJSONUnmarshalStruct(b *testing.B) {
	doc := jsonDoc()
	for b.Loop() {
		var items []jbItem
		if err := json.Unmarshal(doc, &items); err != nil {
			b.Fatal(err)
		}
		sinkJSONItems = items
	}
}

func BenchmarkJSONUnmarshalAny(b *testing.B) {
	doc := jsonDoc()
	for b.Loop() {
		var v any
		if err := json.Unmarshal(doc, &v); err != nil {
			b.Fatal(err)
		}
		sinkJSONAny = v
	}
}
