// The Go side of bench/asn1_bench.c, on the same list of 100 records.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"encoding/asn1"
	"fmt"
	"testing"
	"time"
)

type AsValidity struct {
	NotBefore, NotAfter time.Time
}

type AsRecord struct {
	Serial    int
	Algorithm asn1.ObjectIdentifier
	Name      string `asn1:"utf8"`
	Validity  AsValidity
	Key       []byte
}

var sinkAsn1Int int

func asn1List() []AsRecord {
	l := make([]AsRecord, 100)
	for i := range l {
		key := make([]byte, 64)
		for j := range key {
			key[j] = byte(i*31 + j)
		}
		l[i] = AsRecord{
			Serial:    1000003 * i,
			Algorithm: asn1.ObjectIdentifier{1, 2, 840, 113549, 1, 1, 11},
			Name:      fmt.Sprintf("host-%d.example.com", i),
			Validity: AsValidity{
				time.Date(2026, time.January, 1+i%28, 0, 0, 0, 0, time.UTC),
				time.Date(2027, time.January, 1+i%28, 0, 0, 0, 0, time.UTC),
			},
			Key: key,
		}
	}
	return l
}

func BenchmarkAsn1Marshal(b *testing.B) {
	l := asn1List()
	for b.Loop() {
		out, err := asn1.Marshal(l)
		if err != nil {
			b.Fatal(err)
		}
		sinkAsn1Int = len(out)
	}
}

func BenchmarkAsn1Unmarshal(b *testing.B) {
	der, err := asn1.Marshal(asn1List())
	if err != nil {
		b.Fatal(err)
	}
	for b.Loop() {
		var out []AsRecord
		if _, err := asn1.Unmarshal(der, &out); err != nil {
			b.Fatal(err)
		}
		sinkAsn1Int = len(out)
	}
}
