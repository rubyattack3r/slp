# Sleep 2.1 reference outputs

These files are the expected outputs distributed with the official Sleep 2.1
source archive (`sleep21-bsd.zip`, source revision
`60ac3ff9dacc3e7b5a6c58be201c5830afbda398`).

`tests/reference_supported.txt` is the executable compatibility allowlist.
The conformance test runs each listed script from `tests/fixtures/` and compares
its output byte-for-byte with the corresponding file here. An output fixture
that is not in the allowlist is deliberately **unverified**, not implicitly
supported.

This keeps the full upstream oracle in-tree while allowing compatibility to
advance in reviewed, test-backed increments.
