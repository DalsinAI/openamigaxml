# The W3C XML Conformance Test Suite

OXP is checked against the W3C's XML Conformance Test Suite, release
20130923. The suite is test data only: it is never kept in this repository
or shipped with OXP. Fetch it yourself:

| | |
| --- | --- |
| File | `xmlts20130923.tar.gz`, from https://www.w3.org/XML/Test/ |
| Size | 641,522 bytes (the server's Content-Length; Last-Modified 23 September 2013) |
| SHA-256 | `9b61db9f5dbffa545f4b8d78422167083a8568c59bd1129f94138f936cf6fc1f`, measured on 17 October 2026. The page publishes no checksum. The file unpacks to the same bytes as the page's uncompressed `xmlts20130923.tar` |
| Licence | The test page states none. Its FAQ (https://www.w3.org/XML/Test/faq.html) says contributed tests are published by the W3C under the W3C Software License (19980720) |

Run it:

```sh
REF=0 sh oxp/build-host.sh
cc -std=c99 -O2 -Ioxp/include -o oxp/build/oxpcanon oxp/tests/w3c/oxpcanon.c oxp/build/liboxp.a
tar -xzf xmlts20130923.tar.gz          # anywhere outside the repository
python3 -I oxp/tests/w3c/run-w3c.py xmlconf oxp/build/oxpcanon --list
```

`run-w3c.py` needs Python's lxml, to read the catalog's external entities.
It scores the XML 1.0 fifth-edition and Namespaces in XML 1.0 tests, the way
a non-validating parser that loads nothing is scored:

- **valid and invalid tests** must be well-formed. Where the test gives an
  output file and needs no external entity, OXP's canonical form
  (`oxpcanon.c`) must match it too. The DOCTYPE of the second canonical
  form, with its notations and the DTD's PIs, is left out of the comparison,
  because OXP reports neither.
- **not-wf tests** must be refused. One whose error is in an external entity
  is counted as out of scope when OXP accepts it, since OXP never reads one.
- **error tests** are counted, not scored: either answer is allowed.
- **Not run:** XML 1.1, Namespaces 1.1, and tests for earlier editions only.

## Results, 17 October 2026 (OXP 0.1 with this folder's fixes)

| Section | Passed | Of them, output matched | Failed | Out of scope: external entity | Error tests (accepted / refused) | Not run (XML 1.1, earlier editions) |
| --- | ---: | ---: | ---: | ---: | --- | ---: |
| xmltest (James Clark) | 352 | 118 | 0 | 10 | 1 / 0 | 2 |
| japanese (Fuji Xerox) | 6 | 0 | 0 | 0 | 0 / 6 | 0 |
| sun | 153 | 14 | 0 | 5 | 1 / 0 | 0 |
| oasis (NIST) | 336 | 0 | 0 | 11 | 1 / 0 | 0 |
| ibm | 580 | 130 | 0 | 32 | 8 / 1 | 515 |
| eduni/errata-2e | 29 | 0 | 0 | 1 | 3 / 0 | 1 |
| eduni/errata-3e | 13 | 0 | 0 | 0 | 0 / 0 | 0 |
| eduni/errata-4e | 389 | 0 | 0 | 0 | 3 / 0 | 1 |
| eduni/namespaces | 57 | 0 | 0 | 0 | 3 / 0 | 8 |
| eduni/xml-1.1 | 0 | 0 | 0 | 0 | 0 / 0 | 57 |
| **all** | **1,915** | **262** | **0** | **59** | **20 / 7** | **584** |

The six Japanese error tests OXP refuses are documents in encodings it does
not read (Shift_JIS, EUC-JP and the like). The suite allows either answer.

Before the fixes in this folder's pull request, 35 tests failed. Each was a
real fault, now fixed and covered by a case of the Team's own in
`tests/cases`:

- a document shorter than six bytes was refused;
- character references with many leading zeros were refused;
- a malformed XML declaration (no `?>`, or mismatched quotes) was taken as
  the end of the input instead of an error;
- a parameter entity whose text ended right after `ANY>` or a short keyword
  was refused;
- a colon in a PI target or a notation name was allowed;
- a UTF-8 BOM with a declaration naming another encoding was allowed.

The rest were the runner's: a mislocated catalog base, and comparing
second-canonical-form outputs.
