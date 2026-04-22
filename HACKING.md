# Contributor Guide

## Project Shape

The parser has three main source files:

- `bun.h` defines the public parser API, BUN constants, on-disk field structs,
  parse context, and result codes.
- `bun_parse.c` performs all binary decoding, validation, violation collection,
  and human-readable summary printing.
- `main.c` handles command line usage, calls the parser API, and sends output to
  stdout or stderr.

Tests live in `tests/test_bun.c`.

## Coding Rules

- Compile as C11 with `-Wall -Wextra -Wpedantic`.
- Decode on-disk integers manually as little-endian values.
- Do not read directly into C structs from disk; struct padding is not portable.
- Check offset and size arithmetic before adding or multiplying.
- Validate a range before seeking or reading from it.
- Do not load a whole BUN file, data section, or decompressed asset into memory.
- Keep parsing and validation reusable; command line behavior belongs in `main.c`.

## Validation Model

The parser stores human-readable violations in `BunParseContext`. This lets it
continue after non-fatal problems and report multiple issues where safe.

Malformed findings have priority over unsupported findings when choosing the
final exit code. For example, a file with both a bad asset name and an unsupported
checksum exits with `BUN_MALFORMED`.

For unsupported major/minor versions, the parser stops after header validation.
It does not assume that later sections still follow the BUN 1.0 layout.

## Running Tests

```sh
make clean
make all
make test
```

The tests generate temporary BUN files under `tests/tmp-*.bun` and remove them
after each case.

To run with sanitizers:

```sh
make clean
make CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -g' all test
```

To sweep the provided sample files during local development:

```sh
for f in samples/valid/*.bun; do
  ./bun_parser "$f" >/tmp/bun.out 2>/tmp/bun.err || exit 1
  test ! -s /tmp/bun.err || exit 1
done

for f in samples/invalid/*.bun; do
  ./bun_parser "$f" >/tmp/bun.out 2>/tmp/bun.err
  test "$?" -ne 0 || exit 1
  test -s /tmp/bun.err || exit 1
done
```

Do not include generated binaries, object files, `.bun` files, PDFs, zips, or a
`.git` directory in the submitted code zip.
