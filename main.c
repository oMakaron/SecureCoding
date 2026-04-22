#include <stdio.h>
#include <stdlib.h>

#include "bun.h"

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <file.bun>\n", argv[0]);
    return BUN_ERR_USAGE;
  }
  const char *path = argv[1];

  BunParseContext ctx = {0};
  BunHeader header  = {0};

  bun_result_t result = bun_open(path, &ctx);
  if (result != BUN_OK) {
    fprintf(stderr, "Error: could not open '%s'\n", path);
    return result;
  }

  result = bun_parse_header(&ctx, &header);
  if (result == BUN_ERR_IO) {
    fprintf(stderr, "Error: could not read header from '%s'\n", path);
    bun_close(&ctx);
    return result;
  }

  if (ctx.header_loaded) {
    result = bun_parse_assets(&ctx, &header);
    if (result == BUN_ERR_IO) {
      fprintf(stderr, "Error: could not read asset records from '%s'\n", path);
      bun_close(&ctx);
      return result;
    }
  }

  bun_print_summary(stdout, &ctx, &header);

  result = bun_context_result(&ctx);
  if (result != BUN_OK) {
    bun_print_violations(stderr, &ctx);
  }

  bun_close(&ctx);
  return result;
}
