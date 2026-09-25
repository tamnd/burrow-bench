// The Go side of the digest benchmarks.
//
// Each one resets, writes and sums through hash.Hash every time round, the
// same work as BenchmarkHash8K and BenchmarkHash8Bytes in the crypto packages'
// own tests.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"crypto/md5"
	"crypto/sha1"
	"crypto/sha256"
	"crypto/sha3"
	"crypto/sha512"
	"hash"
	"testing"
)

var sinkDigest []byte

func benchDigest(b *testing.B, h hash.Hash, size int) {
	buf := make([]byte, size)
	sum := make([]byte, 0, h.Size())
	b.SetBytes(int64(size))
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		h.Reset()
		h.Write(buf)
		sinkDigest = h.Sum(sum[:0])
	}
}

func BenchmarkMD5Hash8K(b *testing.B)        { benchDigest(b, md5.New(), 8192) }
func BenchmarkMD5Hash8Bytes(b *testing.B)    { benchDigest(b, md5.New(), 8) }
func BenchmarkSHA1Hash8K(b *testing.B)       { benchDigest(b, sha1.New(), 8192) }
func BenchmarkSHA1Hash8Bytes(b *testing.B)   { benchDigest(b, sha1.New(), 8) }
func BenchmarkSHA256Hash8K(b *testing.B)     { benchDigest(b, sha256.New(), 8192) }
func BenchmarkSHA256Hash8Bytes(b *testing.B) { benchDigest(b, sha256.New(), 8) }
func BenchmarkSHA512Hash8K(b *testing.B)     { benchDigest(b, sha512.New(), 8192) }
func BenchmarkSHA512Hash8Bytes(b *testing.B) { benchDigest(b, sha512.New(), 8) }
func BenchmarkSHA3256Hash8K(b *testing.B)    { benchDigest(b, sha3.New256(), 8192) }
func BenchmarkSHA3256Hash8Bytes(b *testing.B) {
	benchDigest(b, sha3.New256(), 8)
}
