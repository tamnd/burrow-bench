// The Go side of bench/template_bench.c, on the same template text and the
// same list of 100 records.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"fmt"
	"io"
	"strings"
	"testing"
	"text/template"
)

type TbItem struct {
	Name  string
	Count int
}

var sinkTemplate *template.Template

func tbItems() []TbItem {
	l := make([]TbItem, 100)
	for i := range l {
		l[i] = TbItem{Name: fmt.Sprintf("item-%d", i), Count: i * 7}
	}
	return l
}

func BenchmarkTemplateParseLarge(b *testing.B) {
	text := strings.Repeat("{{1234}}\n", 10000)
	for b.Loop() {
		t, err := template.New("bench").Parse(text)
		if err != nil {
			b.Fatal(err)
		}
		sinkTemplate = t
	}
}

func BenchmarkTemplateExecuteRange(b *testing.B) {
	items := tbItems()
	t := template.Must(template.New("bench").Parse("{{range .}}<li>{{.Name}}: {{.Count}}</li>\n{{end}}"))
	for b.Loop() {
		if err := t.Execute(io.Discard, items); err != nil {
			b.Fatal(err)
		}
	}
}
