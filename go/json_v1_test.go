// The Go side of the encoding/json v1 rows in bench/json_bench.c, on the
// document jsonDoc in json_test.go builds. Without the jsonv2 experiment Go's
// v1 is its own older implementation rather than a layer over v2, and that is
// the one most programs run, so it is the one measured.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

//go:build go1.27

package bench

import (
	"bytes"
	jsonv1 "encoding/json"
	"testing"
)

func BenchmarkJSONV1MarshalStruct(b *testing.B) {
	var items []jbItem
	if err := jsonv1.Unmarshal(jsonDoc(), &items); err != nil {
		b.Fatal(err)
	}
	for b.Loop() {
		sinkJSONBytes, _ = jsonv1.Marshal(items)
	}
}

func BenchmarkJSONV1UnmarshalStruct(b *testing.B) {
	doc := jsonDoc()
	for b.Loop() {
		var items []jbItem
		if err := jsonv1.Unmarshal(doc, &items); err != nil {
			b.Fatal(err)
		}
		sinkJSONItems = items
	}
}

func BenchmarkJSONV1Decoder(b *testing.B) {
	doc := jsonDoc()
	for b.Loop() {
		var items []jbItem
		if err := jsonv1.NewDecoder(bytes.NewReader(doc)).Decode(&items); err != nil {
			b.Fatal(err)
		}
		sinkJSONItems = items
	}
}
