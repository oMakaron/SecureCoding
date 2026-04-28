# Report Sections: Secure Coding, Safety Tools, and Deployment Security

## Secure coding practices

The parser was written defensively because BUN files are untrusted binary input. The main design choice was to avoid treating on-disk bytes as in-memory C structs. Although `BunHeader` and `BunAssetRecord` describe the file format, the implementation reads fixed-size byte buffers and decodes each integer explicitly as little-endian data. This avoids undefined behaviour from struct padding, alignment differences, host endianness, and direct pointer casts into binary data.

Offset and size arithmetic is treated as security-sensitive. The parser uses checked helpers such as `add_u64_checked` and `mul_u64_checked` before computing section ends, asset table sizes, asset name ranges, data ranges, and RLE expanded sizes. If an offset calculation would wrap, the parser returns an error instead of seeking to an unintended location. The parser also validates that the header, asset table, string table, and data section all lie inside the file and do not overlap before reading asset records.

The parser avoids reading the full file or full assets into memory. Header and asset records are read in fixed-size buffers, asset names are streamed in small chunks, and only a bounded prefix of each asset name or payload is kept for output. This keeps memory usage sub-linear for large files and reduces the risk of memory exhaustion. RLE data is validated by streaming `(count, byte)` pairs and tracking the expanded size without allocating the full decompressed output. This also prevents decompression bombs from forcing the parser to allocate memory proportional to attacker-controlled uncompressed sizes.

Input validation follows the BUN specification closely. The parser checks the magic number, supported version, required 4-byte section alignment, section bounds, section overlap, asset name bounds, asset data bounds, non-empty printable ASCII asset names, supported compression modes, RLE even-length data, non-zero RLE counts, and matching RLE expanded size. Unsupported optional features such as zlib compression and non-zero checksums are reported as `BUN_UNSUPPORTED` rather than being ignored.

The code separates parsing from presentation. The parser functions return explicit `bun_result_t` values and store short diagnostic details in `BunParseContext`; `main.c` is responsible for printing human-readable output and errors. This makes the parser easier to test because tests can check return codes and context state directly without depending on terminal output. File I/O operations are checked, including `fopen`, `fseek`, `ftell`, `fread`, and `fclose`, and failures are converted into documented error codes.

## Tools used to improve safety and code quality

The project was tested with compiler warnings enabled through the normal build:

```sh
make clean all test
```

This builds with `gcc -std=c11 -Wall -Wextra -Wpedantic` and runs the Check-based test suite. On 28 April 2026, the suite reported `100%: Checks: 22, Failures: 0, Errors: 0`.

AddressSanitizer and UndefinedBehaviorSanitizer were used to look for memory errors and undefined behaviour during the unit tests:

```sh
make clean
make CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -g' test
```

The sanitizer build also reported `100%: Checks: 22, Failures: 0, Errors: 0`, with no ASan or UBSan diagnostics. This gives coverage for the current valid and invalid sample files, including malformed headers, overlapping sections, out-of-bounds names, non-printable names, truncated files, and malformed RLE data.

Valgrind Memcheck was used on the test runner to check dynamic memory behaviour:

```sh
make clean
make tests/test_runner
CK_FORK=no valgrind --leak-check=full --error-exitcode=99 ./tests/test_runner
```

Valgrind reported `All heap blocks were freed -- no leaks are possible` and `ERROR SUMMARY: 0 errors from 0 contexts`, while the test suite again passed all 22 tests. Running with `CK_FORK=no` keeps the Check tests in one process, which makes the Valgrind result easier to interpret.

Cppcheck was used as a lightweight static analysis pass:

```sh
cppcheck --std=c11 --enable=warning,style,performance,portability --inline-suppr main.c bun_parse.c bun.h
```

Cppcheck initially reported several maintainability issues in `bun_parse.c`: unused initial assignments, a redundant assignment before overwrite, and variables whose scope could be reduced. These were fixed by removing unnecessary initial values, removing the redundant per-loop assignment, and moving `copies_to_store` and `record_offset` into narrower scopes. A follow-up Cppcheck run completed with no diagnostics. Although these findings were not exploitable memory bugs, reducing variable lifetime and eliminating redundant state makes later security review easier and lowers the chance of future mistakes.

Clang Static Analyzer was also run through `scan-build`:

```sh
make clean
scan-build --status-bugs make all
```

The analyzer completed with `scan-build: No bugs found.` The available GCC version in the SDE was GCC 9.4, which does not support `-fanalyzer`, so GCC's newer analyzer was not used.

## Security aspects of deploying the BUN parser in a game client

If the parser is deployed in a game client that automatically downloads BUN files, every downloaded BUN file must be treated as attacker-controlled input. A malicious or corrupted BUN file could try to crash the client, exploit parser memory bugs, trigger integer overflows in offset arithmetic, force huge allocations, consume CPU through pathological compression data, or cause the game to load unexpected asset content. Even if the parser itself is memory-safe, automatic parsing creates a denial-of-service risk because a malformed update or player-created package could repeatedly crash clients or prevent regions from loading.

The highest-risk areas are binary parsing, decompression, and trust boundaries. Binary formats are fragile because small offset or size errors can make the parser read outside the intended file region. Compression is risky because the on-disk size may be small while the decompressed size is huge. The BUN format also has flags for encrypted and executable content; these flags should never cause the client to execute content or bypass validation. Asset names should not be used directly as filesystem paths, URLs, script identifiers, or commands without separate application-level validation.

The parser should continue to reject unknown or unsupported features by default. In particular, zlib-compressed assets and non-zero checksums should not be silently accepted unless the parser implements full, tested support for them. Unknown flag bits should remain `BUN_UNSUPPORTED`. Failing closed is safer than guessing how to handle optional features, especially in an auto-download pipeline.

For deployment, BUN files should be authenticated before parsing. The client should download BUN files over TLS and verify a digital signature or trusted manifest hash before passing them to the parser. This protects against tampering, malicious mirrors, compromised caches, and accidental corruption. The manifest should include expected file size, hash, format version, asset count, and any feature flags the client is expected to support.

The client should impose resource limits before and during parsing. Recommended limits include a maximum BUN file size, maximum asset count, maximum string table size, maximum data section size, maximum individual asset size, maximum RLE expanded size, and a timeout or cancellation mechanism for parsing work. These limits should be chosen based on real game asset requirements rather than attacker-controlled header values. The parser should also run in a low-privilege context where possible, with no direct ability to write outside the game's asset cache.

The BUN format could be improved by adding stronger integrity metadata. A whole-file signature and per-asset cryptographic hashes would be more useful for security than optional CRC-32 checksums, because CRC-32 is designed for accidental corruption rather than malicious tampering. The format could also define mandatory maximums or a manifest section that states the expected feature set, compression methods, and content types before the parser processes large data sections.

Finally, the parser should be part of a continuous security testing workflow. Every change to parsing, decompression, or output logic should run unit tests, sanitizer tests, static analysis, and Valgrind where practical. Fuzzing with AFL++ or libFuzzer would be a strong next step because binary parsers often fail on edge cases that are hard to discover manually. Fuzzing should target both header parsing and asset parsing, with seed files covering valid files, malformed offsets, overlapping sections, boundary-size names, and malformed RLE streams.
