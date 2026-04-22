# BUN parser

This project builds `bun_parser`, a C11 parser and validator for the BUN
(Binary UNified assets) file format.

## Build

```sh
make all
```

The build produces a single executable in the project root:

```sh
./bun_parser
```

## Run

```sh
./bun_parser path/to/file.bun
```

For valid files, the program prints a human-readable summary to standard output
and exits with status `0`.

For malformed or unsupported files, the program prints as much of the file as can
safely be displayed to standard output, prints one violation per line to standard
error, and exits with status `1` or `2`.

## Output Format

The summary starts with the file size, then all BUN header fields, then every
asset record. Asset names and payloads are displayed as short prefixes. Printable
payloads are shown as escaped text; binary payloads are shown as hex bytes.

Example:

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

## Exit Codes

- `0` (`BUN_OK`): the file was parsed and validated successfully.
- `1` (`BUN_MALFORMED`): the file violates the BUN specification.
- `2` (`BUN_UNSUPPORTED`): the file uses a valid but unsupported feature.
- `3` (`BUN_ERR_IO`): file opening or reading failed.
- `4` (`BUN_ERR_USAGE`): the command line arguments were invalid.

If a file contains both malformed data and unsupported features, the parser
returns `1`, because malformed input is unsafe to trust.

## Tests

```sh
make test
```

The test runner is plain C and generates temporary BUN files at runtime. It does
not require bundled `.bun` fixtures or third-party test libraries.

For sanitizer testing during development:

```sh
make clean
make CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -g' all test
```

## Implementation Notes

The parser does not read whole BUN files into memory. It decodes fixed-size
headers and asset records from small buffers, validates ranges before seeking,
and validates RLE data by streaming compressed pairs.

Multi-byte fields are decoded manually as little-endian values. The code does
not `fread()` directly into C structs, because in-memory struct padding is not
part of the on-disk BUN format.
