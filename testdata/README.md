# testdata

`e.txt` and `Isaac.Newton-Opticks.txt` are copied unchanged from the Go source tree, go1.27.1, where they are `src/compress/testdata/e.txt` and `src/testdata/Isaac.Newton-Opticks.txt`. Go's compress/flate benchmarks read them, and the flate rows here read the same files so that both sides compress the same bytes. The first is the first 100,000 digits of e. The second is the text of Newton's Opticks from Project Gutenberg, which is in the public domain.

`e.txt.bz2`, `Isaac.Newton-Opticks.txt.bz2` and `random.data.bz2` are copied unchanged from `src/compress/bzip2/testdata` in the same Go release. Go's compress/bzip2 benchmarks decode them, and so do the bzip2 rows here.
