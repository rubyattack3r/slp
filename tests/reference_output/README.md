# Sleep 2.1 reference outputs

These files are the expected outputs distributed with the official Sleep 2.1
source archive (`sleep21-bsd.zip`, source revision
`60ac3ff9dacc3e7b5a6c58be201c5830afbda398`).

`tests/reference_supported.txt` is the executable compatibility allowlist.
The conformance test runs each listed script from `tests/fixtures/` and compares
its output byte-for-byte with the corresponding file here.

`tests/reference_unverified.tsv` accounts for every remaining fixture and
records its current failure mode. The test suite requires the supported and
unverified ledgers to be disjoint and to cover the same 342 names as both the
source and output directories. A fixture therefore cannot be omitted or
silently treated as supported.

The current portable-runtime baseline is 227 verified fixtures and 115
explicitly unverified fixtures. Java helper classes, JAR loading, and tests
that require the original JVM runner remain outside the portable SLP
conformance claim.

This keeps the full upstream script corpus and oracle in-tree while allowing
compatibility to advance in reviewed, test-backed increments.
