# BUN parser

This directory contains the Group 19 CITS3007 Phase 1 BUN parser.

## Building

```sh
make all
```

The build produces a single executable named `bun_parser` in this directory.

## Running

```sh
./bun_parser path/to/file.bun
```

For valid files, the parser prints the BUN header, each asset record, a bounded
asset name prefix, a bounded data prefix, and the number of parsed assets. For
invalid or unsupported files, it prints as much safe output as possible and then
prints a diagnostic error to stderr.

## Testing

```sh
make test
```

The test target uses the Check framework. If Check is not installed, run
`setup.sh` or install `check` and `pkg-config` through the package manager.
