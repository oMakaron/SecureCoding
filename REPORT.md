# CITS3007 Phase 1 Report - BUN Parser

Group number: TODO

Group members: TODO

Repository URL: TODO

## 1. Output Format and Exit Codes

The executable is named `bun_parser` and is run as:

```sh
./bun_parser path/to/file.bun
```

For valid files, the parser prints a summary to stdout and exits with `0`
(`BUN_OK`). The summary contains the file size, every BUN header field, every
asset record field, a short asset-name prefix, and a short data prefix. Prefixes
are limited to approximately 60 bytes. Printable data is shown as escaped text;
non-printable data is shown as hex bytes.

Example output:

```text
BUN parser summary
File size: 132 bytes
Header:
  magic: 0x304e5542
  version_major: 1
  version_minor: 0
  asset_count: 1
  asset_table_offset: 60
  string_table_offset: 108
  string_table_size: 16
  data_section_offset: 124
  data_section_size: 8
  reserved: 0
Assets: 1
Asset[0]:
  name_offset: 0
  name_length: 5
  data_offset: 0
  data_size: 5
  uncompressed_size: 0
  compression: 0
  type: 1
  checksum: 0x00000000
  flags: 0x00000000
  name_prefix: "hello"
  data_prefix: "world"
```

For invalid files, the parser prints as much of the file as can safely be shown
to stdout, prints one violation per line to stderr, and exits with the most
appropriate non-zero code.

Exit codes:

- `0` (`BUN_OK`): file parsed and validated successfully.
- `1` (`BUN_MALFORMED`): file violates the BUN specification.
- `2` (`BUN_UNSUPPORTED`): file uses an unsupported optional feature.
- `3` (`BUN_ERR_IO`): file opening or reading failed.
- `4` (`BUN_ERR_USAGE`): wrong command-line usage.

Malformed input takes priority over unsupported features. If a file has both a
malformed field and an unsupported field, the program exits with `1`.

## 2. Decisions, Assumptions, and Integer Safety

The parser manually decodes all multi-byte fields from little-endian byte buffers
instead of reading directly into C structs. This avoids relying on implementation
defined struct padding or alignment.

All offset and size arithmetic is checked before use. Helper functions reject
addition and multiplication that would overflow `u64`. Ranges are validated
before `fseek()` or `fread()` is attempted.

The parser validates the header in stages. It first checks the magic and version.
If the version is unsupported, it returns `BUN_UNSUPPORTED` and does not assume
that later sections still follow the BUN 1.0 layout. This avoids incorrectly
classifying an unsupported future format as malformed.

The parser treats the header, asset table, string table, and data section as
sections for overlap checking. Zero-size sections are allowed and do not overlap
other sections, but their declared start offset still must lie within the file.

CRC-32 validation is not implemented. A non-zero checksum is therefore reported
as `BUN_UNSUPPORTED`, as permitted by the specification.

zlib compression is not implemented. Compression value `2` is reported as
`BUN_UNSUPPORTED`, following the project brief.

RLE data is validated without fully decompressing it. The parser checks that the
compressed size is even, that each count is non-zero, and that the streamed
expanded length matches `uncompressed_size`. It also checks whether the declared
uncompressed size is outside the minimum or maximum possible expansion before
scanning the compressed data.

## 3. Libraries Used

The submitted executable uses only the C standard library and the provided
project code. No third-party runtime libraries are required.

The test runner is also plain C and generates temporary BUN files at runtime, so
the submitted code does not need bundled `.bun` fixtures.

## 4. Tools and Evidence

Compilation was performed with strict warnings:

```sh
make clean && make all
```

The project builds with:

```text
gcc -std=c11 -Wall -Wextra -Wpedantic -o bun_parser main.c bun_parse.c
```

Unit tests were run with:

```sh
make clean && make all && make test
```

Current result:

```text
PASS valid empty
PASS valid one asset
PASS bad magic
PASS bad version
PASS name nonprintable
PASS data out of bounds
PASS rle valid
PASS rle zero count
PASS zlib unsupported
PASS checksum unsupported
PASS flags unsupported
All tests passed
```

The provided sample files were swept locally. All files under `samples/valid`
returned `0` with no stderr output. All files under `samples/invalid` returned a
non-zero code with at least one stderr violation.

One test finding changed the implementation: `samples/invalid/02-bad-version.bun`
initially returned `BUN_MALFORMED` because the parser kept validating BUN 1.0
sections after seeing an unsupported version. The parser was changed to stop
section validation after an unsupported version, and the file now returns
`BUN_UNSUPPORTED`.

Sanitizers were run with:

```sh
make clean
make CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -g' all test
```

The sanitizer build completed the unit tests without AddressSanitizer or
UndefinedBehaviorSanitizer findings.

The large RLE stress file was tested with:

```sh
/usr/bin/time -f 'elapsed=%E max_rss_kb=%M' ./bun_parser rle-stress.bun
```

Current result:

```text
rc=1
malformed: asset 0: RLE uncompressed_size 107374182400 exceeds the maximum possible expansion 85176087360
elapsed=0:00.00 max_rss_kb=1548
```

This led to an RLE upper-bound check that rejects impossible decompressed sizes
before scanning hundreds of megabytes of compressed data.

TODO before final submission: replace this paragraph with links to the group's
repository issues and commits showing the corresponding changes. The brief asks
for concrete evidence, and repository links are the best form of evidence.

## 5. Security Discussion

The proposed deployment scenario is risky because the game client would parse
BUN files downloaded from remote servers and containing player-created content.
Even if the files come from Trinity servers, they may still originate from
untrusted users or be corrupted in transit.

Main risks:

- Memory exhaustion if a parser loads entire files, large data sections, or
  decompressed payloads into RAM.
- CPU denial of service from very large compressed assets or many asset records.
- Integer overflow in offset plus size calculations, causing out-of-bounds reads.
- Malformed names or payload metadata causing crashes.
- Unsupported compression, encryption, checksums, or flags being misinterpreted.
- Future client code using asset names as paths and accidentally enabling path
  traversal or unsafe file writes.
- Tampering if BUN files are not authenticated before parsing.

Recommended parser mitigations:

- Keep the streaming design used here; do not allocate based on untrusted file
  sizes without limits.
- Validate all offsets, sizes, alignments, and overlaps before reading.
- Enforce global limits for maximum asset count, maximum displayed prefix work,
  maximum RLE scan work per frame, and maximum uncompressed asset size.
- Treat unsupported features as hard failures.
- Keep parsing separate from loading or executing asset content.
- Fuzz the parser with sanitizers enabled before client deployment.

Recommended BUN format changes:

- Add a signed manifest or whole-file signature so clients can reject tampered
  content before parsing assets.
- Add explicit maximum sizes and counts to the specification.
- Add a canonical section order requirement for all newly written BUN files.
- Add mandatory checksums or authenticated hashes for each asset.
- Add a clear version-negotiation policy for future format changes.

## 6. Coding Standards

The project uses C11 and compiles with `-Wall -Wextra -Wpedantic`.

Coding conventions:

- Keep helper functions small and focused.
- Use `u8`, `u16`, `u32`, and `u64` for BUN on-disk values.
- Use checked arithmetic helpers for offset and size calculations.
- Validate before seeking or reading.
- Keep stdout/stderr formatting in top-level output functions, not hidden inside
  low-level byte readers.
- Avoid unnecessary heap allocation. The current parser uses stack buffers for
  fixed-size reads and streaming chunks.

## 7. Challenges

One challenge was deciding how far to parse after a serious header problem. The
parser now stops after invalid magic or unsupported version, but continues after
some recoverable BUN 1.0 layout problems when it can still safely read more
fields.

Another challenge was RLE validation for very large files. A naive parser could
try to decompress into memory or scan a huge file unnecessarily. The final design
streams RLE data and also rejects impossible declared uncompressed sizes early.

## 8. GenAI Use

GenAI assistance was used to draft and implement the parser, tests, and report
material. Group members must review the code and report, understand the design,
and update this section with the final details required by the unit's academic
conduct expectations.
