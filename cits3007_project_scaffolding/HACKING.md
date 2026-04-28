# Contributor guide

## Build and test commands

Run commands from this directory:

```sh
make all
make test
make clean
```

`make test` assumes the Check framework is installed. `setup.sh` installs the
test dependencies used by the Makefile.

## Parser conventions

- Treat every BUN file as untrusted binary input.
- Decode on-disk integers explicitly as little-endian bytes; do not cast file
  buffers directly to C structs.
- Check offset and size arithmetic before seeking or reading.
- Keep parser logic separate from user-facing output. Parser functions should
  return `bun_result_t` values and store diagnostics in `BunParseContext`.
- Keep memory usage bounded. Store only short previews of names and asset data,
  and stream larger regions where validation requires reading them.
- Reject unsupported optional features with `BUN_UNSUPPORTED` rather than
  silently accepting them.
