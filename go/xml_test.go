// The Go side of bench/xml_bench.c, on the same feed of 100 entries.
//
// Copyright 2026 The burrow Authors. All rights reserved.
// Use of this source code is governed by a BSD-style licence that can be found
// in the LICENSE file.

package bench

import (
	"bytes"
	"encoding/xml"
	"fmt"
	"io"
	"testing"
)

var sinkXMLInt int

func xmlDoc() []byte {
	var b bytes.Buffer
	b.WriteString("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" +
		"<feed xmlns=\"http://www.w3.org/2005/Atom\" " +
		"xmlns:media=\"http://search.yahoo.com/mrss/\">\n")
	for i := 0; i < 100; i++ {
		fmt.Fprintf(&b, "  <entry id=\"%d\" lang=\"en\">\n"+
			"    <title>Item %d &amp; friends</title>\n"+
			"    <media:thumbnail url=\"https://example.com/%d.jpg\" "+
			"width=\"120\"/>\n"+
			"    <summary>Green tea, loose leaf, %d grams.</summary>\n"+
			"  </entry>\n", i, i, i, 50+i)
	}
	b.WriteString("</feed>\n")
	return b.Bytes()
}

func tokenKind(t xml.Token) int {
	switch t.(type) {
	case xml.StartElement:
		return 1
	case xml.EndElement:
		return 2
	case xml.CharData:
		return 3
	case xml.Comment:
		return 4
	case xml.ProcInst:
		return 5
	case xml.Directive:
		return 6
	}
	return 0
}

func BenchmarkXMLRawTokens(b *testing.B) {
	doc := xmlDoc()
	r := bytes.NewReader(doc)
	for b.Loop() {
		r.Reset(doc)
		d := xml.NewDecoder(r)
		n := 0
		for {
			t, err := d.RawToken()
			if err != nil {
				if err != io.EOF {
					b.Fatal(err)
				}
				break
			}
			n += tokenKind(t)
		}
		sinkXMLInt = n
	}
}

func BenchmarkXMLTokens(b *testing.B) {
	doc := xmlDoc()
	r := bytes.NewReader(doc)
	for b.Loop() {
		r.Reset(doc)
		d := xml.NewDecoder(r)
		n := 0
		for {
			t, err := d.Token()
			if err != nil {
				if err != io.EOF {
					b.Fatal(err)
				}
				break
			}
			n += tokenKind(t)
		}
		sinkXMLInt = n
	}
}

func BenchmarkXMLEncodeTokens(b *testing.B) {
	d := xml.NewDecoder(bytes.NewReader(xmlDoc()))
	var toks []xml.Token
	for {
		t, err := d.Token()
		if err != nil {
			break
		}
		toks = append(toks, xml.CopyToken(t))
	}
	for b.Loop() {
		e := xml.NewEncoder(io.Discard)
		for _, t := range toks {
			if err := e.EncodeToken(t); err != nil {
				b.Fatal(err)
			}
		}
		if err := e.Flush(); err != nil {
			b.Fatal(err)
		}
	}
}

type thumb struct {
	Url   string `xml:"url,attr"`
	Width int    `xml:"width,attr"`
}

type entry struct {
	Id        int    `xml:"id,attr"`
	Lang      string `xml:"lang,attr"`
	Title     string `xml:"title"`
	Thumbnail thumb  `xml:"http://search.yahoo.com/mrss/ thumbnail"`
	Summary   string `xml:"summary"`
}

type feed struct {
	XMLName xml.Name `xml:"http://www.w3.org/2005/Atom feed"`
	Entry   []entry  `xml:"entry"`
}

func xmlFeed() *feed {
	f := &feed{}
	for i := 0; i < 100; i++ {
		f.Entry = append(f.Entry, entry{
			Id:        i,
			Lang:      "en",
			Title:     fmt.Sprintf("Item %d & friends", i),
			Thumbnail: thumb{fmt.Sprintf("https://example.com/%d.jpg", i), 120},
			Summary:   fmt.Sprintf("Green tea, loose leaf, %d grams.", 50+i),
		})
	}
	return f
}

func BenchmarkXMLMarshal(b *testing.B) {
	f := xmlFeed()
	for b.Loop() {
		out, err := xml.Marshal(f)
		if err != nil {
			b.Fatal(err)
		}
		sinkXMLInt = len(out)
	}
}

func BenchmarkXMLEncodeStruct(b *testing.B) {
	f := xmlFeed()
	for b.Loop() {
		e := xml.NewEncoder(io.Discard)
		if err := e.Encode(f); err != nil {
			b.Fatal(err)
		}
	}
}

func BenchmarkXMLUnmarshal(b *testing.B) {
	doc := xmlDoc()
	for b.Loop() {
		var f feed
		if err := xml.Unmarshal(doc, &f); err != nil {
			b.Fatal(err)
		}
		sinkXMLInt = len(f.Entry)
	}
}
