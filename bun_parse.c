#include "bun.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREFIX_BYTES 60u
#define IO_CHUNK_SIZE 8192u

typedef struct {
  const char *name;
  u64 offset;
  u64 size;
  int range_ok;
} BunSectionRange;

static u16 read_u16_le(const u8 *buf, size_t offset) {
  return (u16)((u16)buf[offset] | (u16)buf[offset + 1] << 8);
}

static u32 read_u32_le(const u8 *buf, size_t offset) {
  return (u32)buf[offset]
     | (u32)buf[offset + 1] << 8
     | (u32)buf[offset + 2] << 16
     | (u32)buf[offset + 3] << 24;
}

static u64 read_u64_le(const u8 *buf, size_t offset) {
  return (u64)buf[offset]
     | (u64)buf[offset + 1] << 8
     | (u64)buf[offset + 2] << 16
     | (u64)buf[offset + 3] << 24
     | (u64)buf[offset + 4] << 32
     | (u64)buf[offset + 5] << 40
     | (u64)buf[offset + 6] << 48
     | (u64)buf[offset + 7] << 56;
}

static int add_u64(u64 a, u64 b, u64 *out) {
  if (UINT64_MAX - a < b) {
    return 0;
  }
  *out = a + b;
  return 1;
}

static int mul_u64(u64 a, u64 b, u64 *out) {
  if (a != 0 && b > UINT64_MAX / a) {
    return 0;
  }
  *out = a * b;
  return 1;
}

static int range_within(u64 offset, u64 size, u64 limit) {
  u64 end = 0;
  return add_u64(offset, size, &end) && offset <= limit && end <= limit;
}

static int ranges_overlap(u64 a_offset, u64 a_size, u64 b_offset, u64 b_size) {
  u64 a_end = 0;
  u64 b_end = 0;

  if (a_size == 0 || b_size == 0) {
    return 0;
  }
  if (!add_u64(a_offset, a_size, &a_end) || !add_u64(b_offset, b_size, &b_end)) {
    return 1;
  }
  return !(a_end <= b_offset || b_end <= a_offset);
}

static int aligned4(u64 value) {
  return value % 4u == 0u;
}

static void add_violation(BunParseContext *ctx, bun_result_t result, const char *fmt, ...) {
  va_list args;

  if (result == BUN_MALFORMED) {
    ctx->malformed_count++;
  } else if (result == BUN_UNSUPPORTED) {
    ctx->unsupported_count++;
  }

  if (ctx->violation_count >= BUN_MAX_VIOLATIONS) {
    ctx->violation_overflow = 1;
    return;
  }

  ctx->violations[ctx->violation_count].result = result;
  va_start(args, fmt);
  vsnprintf(ctx->violations[ctx->violation_count].message,
            BUN_VIOLATION_LEN, fmt, args);
  va_end(args);
  ctx->violation_count++;
}

static int seek_to(BunParseContext *ctx, u64 offset) {
  if (offset > (u64)LONG_MAX) {
    return 0;
  }
  return fseek(ctx->file, (long)offset, SEEK_SET) == 0;
}

static int read_exact_at(BunParseContext *ctx, u64 offset, void *buf, size_t size) {
  if (!range_within(offset, (u64)size, ctx->file_size_u64)) {
    return 0;
  }
  if (!seek_to(ctx, offset)) {
    return 0;
  }
  return fread(buf, 1, size, ctx->file) == size;
}

static void decode_header(const u8 *buf, BunHeader *header) {
  header->magic               = read_u32_le(buf, 0);
  header->version_major       = read_u16_le(buf, 4);
  header->version_minor       = read_u16_le(buf, 6);
  header->asset_count         = read_u32_le(buf, 8);
  header->asset_table_offset  = read_u64_le(buf, 12);
  header->string_table_offset = read_u64_le(buf, 20);
  header->string_table_size   = read_u64_le(buf, 28);
  header->data_section_offset = read_u64_le(buf, 36);
  header->data_section_size   = read_u64_le(buf, 44);
  header->reserved            = read_u64_le(buf, 52);
}

static void decode_asset_record(const u8 *buf, BunAssetRecord *record) {
  record->name_offset       = read_u32_le(buf, 0);
  record->name_length       = read_u32_le(buf, 4);
  record->data_offset       = read_u64_le(buf, 8);
  record->data_size         = read_u64_le(buf, 16);
  record->uncompressed_size = read_u64_le(buf, 24);
  record->compression       = read_u32_le(buf, 32);
  record->type              = read_u32_le(buf, 36);
  record->checksum          = read_u32_le(buf, 40);
  record->flags             = read_u32_le(buf, 44);
}

static void check_section_range(BunParseContext *ctx, const char *name,
                                u64 offset, u64 size, int *range_ok) {
  u64 end = 0;

  *range_ok = 0;
  if (!add_u64(offset, size, &end)) {
    add_violation(ctx, BUN_MALFORMED,
                  "%s section overflows u64 arithmetic: offset=%" PRIu64
                  ", size=%" PRIu64,
                  name, offset, size);
    return;
  }
  if (!range_within(offset, size, ctx->file_size_u64)) {
    add_violation(ctx, BUN_MALFORMED,
                  "%s section lies outside file: offset=%" PRIu64
                  ", size=%" PRIu64 ", file_size=%" PRIu64,
                  name, offset, size, ctx->file_size_u64);
    return;
  }
  *range_ok = 1;
}

static void check_no_section_overlap(BunParseContext *ctx,
                                     const BunSectionRange *sections,
                                     size_t count) {
  size_t i = 0;
  size_t j = 0;

  for (i = 0; i < count; i++) {
    if (!sections[i].range_ok) {
      continue;
    }
    for (j = i + 1; j < count; j++) {
      if (!sections[j].range_ok) {
        continue;
      }
      if (ranges_overlap(sections[i].offset, sections[i].size,
                         sections[j].offset, sections[j].size)) {
        add_violation(ctx, BUN_MALFORMED,
                      "%s section overlaps %s section",
                      sections[i].name, sections[j].name);
      }
    }
  }
}

static bun_result_t validate_printable_name(BunParseContext *ctx,
                                            const BunHeader *header,
                                            const BunAssetRecord *record,
                                            u32 asset_index) {
  u64 remaining = record->name_length;
  u64 offset = header->string_table_offset + (u64)record->name_offset;
  u64 absolute_index = 0;
  u8 buf[IO_CHUNK_SIZE];

  while (remaining > 0) {
    size_t to_read = remaining > IO_CHUNK_SIZE ? IO_CHUNK_SIZE : (size_t)remaining;
    size_t i = 0;

    if (!read_exact_at(ctx, offset, buf, to_read)) {
      return BUN_ERR_IO;
    }
    for (i = 0; i < to_read; i++) {
      if (buf[i] < 0x20u || buf[i] > 0x7eu) {
        add_violation(ctx, BUN_MALFORMED,
                      "asset %" PRIu32
                      ": name contains non-printable byte 0x%02x at name byte %" PRIu64,
                      asset_index, buf[i], absolute_index + (u64)i);
        return BUN_OK;
      }
    }
    offset += (u64)to_read;
    absolute_index += (u64)to_read;
    remaining -= (u64)to_read;
  }

  return BUN_OK;
}

static bun_result_t validate_rle(BunParseContext *ctx, const BunHeader *header,
                                 const BunAssetRecord *record, u32 asset_index) {
  u64 remaining = record->data_size;
  u64 offset = header->data_section_offset + record->data_offset;
  u64 produced = 0;
  u64 pair_count = 0;
  u64 max_possible = 0;
  u8 buf[IO_CHUNK_SIZE];

  if (record->data_size % 2u != 0u) {
    add_violation(ctx, BUN_MALFORMED,
                  "asset %" PRIu32
                  ": RLE data_size must be even, got %" PRIu64,
                  asset_index, record->data_size);
    return BUN_OK;
  }

  pair_count = record->data_size / 2u;
  if (record->uncompressed_size < pair_count) {
    add_violation(ctx, BUN_MALFORMED,
                  "asset %" PRIu32
                  ": RLE uncompressed_size %" PRIu64
                  " is smaller than the minimum possible expansion %" PRIu64,
                  asset_index, record->uncompressed_size, pair_count);
    return BUN_OK;
  }
  if (mul_u64(pair_count, 255u, &max_possible) &&
      record->uncompressed_size > max_possible) {
    add_violation(ctx, BUN_MALFORMED,
                  "asset %" PRIu32
                  ": RLE uncompressed_size %" PRIu64
                  " exceeds the maximum possible expansion %" PRIu64,
                  asset_index, record->uncompressed_size, max_possible);
    return BUN_OK;
  }

  while (remaining > 0) {
    size_t to_read = remaining > IO_CHUNK_SIZE ? IO_CHUNK_SIZE : (size_t)remaining;
    size_t i = 0;

    if (to_read % 2u != 0u) {
      to_read--;
    }
    if (to_read == 0) {
      break;
    }
    if (!read_exact_at(ctx, offset, buf, to_read)) {
      return BUN_ERR_IO;
    }

    for (i = 0; i < to_read; i += 2u) {
      u8 count = buf[i];
      if (count == 0u) {
        add_violation(ctx, BUN_MALFORMED,
                      "asset %" PRIu32
                      ": RLE pair at compressed byte %" PRIu64 " has zero count",
                      asset_index, record->data_size - remaining + (u64)i);
      }
      if (!add_u64(produced, (u64)count, &produced)) {
        add_violation(ctx, BUN_MALFORMED,
                      "asset %" PRIu32 ": RLE uncompressed size overflows u64",
                      asset_index);
        return BUN_OK;
      }
    }

    offset += (u64)to_read;
    remaining -= (u64)to_read;
  }

  if (produced != record->uncompressed_size) {
    add_violation(ctx, BUN_MALFORMED,
                  "asset %" PRIu32
                  ": RLE expands to %" PRIu64
                  " bytes, but uncompressed_size is %" PRIu64,
                  asset_index, produced, record->uncompressed_size);
  }

  return BUN_OK;
}

static bun_result_t validate_asset_record(BunParseContext *ctx,
                                          const BunHeader *header,
                                          const BunAssetRecord *record,
                                          u32 asset_index) {
  u64 name_end = 0;
  u64 data_end = 0;
  int name_range_ok = 0;
  int data_range_ok = 0;
  bun_result_t r = BUN_OK;

  if (record->name_length == 0u) {
    add_violation(ctx, BUN_MALFORMED,
                  "asset %" PRIu32 ": name_length must be non-zero", asset_index);
  }

  if (!add_u64((u64)record->name_offset, (u64)record->name_length, &name_end)) {
    add_violation(ctx, BUN_MALFORMED,
                  "asset %" PRIu32 ": name offset plus length overflows u64",
                  asset_index);
  } else if (name_end > header->string_table_size) {
    add_violation(ctx, BUN_MALFORMED,
                  "asset %" PRIu32
                  ": name range exceeds string table: offset=%" PRIu32
                  ", length=%" PRIu32 ", string_table_size=%" PRIu64,
                  asset_index, record->name_offset, record->name_length,
                  header->string_table_size);
  } else {
    name_range_ok = 1;
  }

  if (!add_u64(record->data_offset, record->data_size, &data_end)) {
    add_violation(ctx, BUN_MALFORMED,
                  "asset %" PRIu32 ": data offset plus size overflows u64",
                  asset_index);
  } else if (data_end > header->data_section_size) {
    add_violation(ctx, BUN_MALFORMED,
                  "asset %" PRIu32
                  ": data range exceeds data section: offset=%" PRIu64
                  ", size=%" PRIu64 ", data_section_size=%" PRIu64,
                  asset_index, record->data_offset, record->data_size,
                  header->data_section_size);
  } else {
    data_range_ok = 1;
  }

  if (ctx->string_table_range_ok && name_range_ok && record->name_length > 0u) {
    r = validate_printable_name(ctx, header, record, asset_index);
    if (r != BUN_OK) {
      return r;
    }
  }

  if (record->checksum != 0u) {
    add_violation(ctx, BUN_UNSUPPORTED,
                  "asset %" PRIu32
                  ": non-zero CRC-32 checksum is unsupported: 0x%08" PRIx32,
                  asset_index, record->checksum);
  }

  if ((record->flags & ~(BUN_FLAG_ENCRYPTED | BUN_FLAG_EXECUTABLE)) != 0u) {
    add_violation(ctx, BUN_UNSUPPORTED,
                  "asset %" PRIu32 ": unsupported flags set: 0x%08" PRIx32,
                  asset_index, record->flags);
  }

  if (record->compression == BUN_COMPRESSION_NONE) {
    if (record->uncompressed_size != 0u) {
      add_violation(ctx, BUN_MALFORMED,
                    "asset %" PRIu32
                    ": uncompressed asset must have uncompressed_size 0, got %" PRIu64,
                    asset_index, record->uncompressed_size);
    }
  } else if (record->compression == BUN_COMPRESSION_RLE) {
    if (ctx->data_section_range_ok && data_range_ok) {
      r = validate_rle(ctx, header, record, asset_index);
      if (r != BUN_OK) {
        return r;
      }
    }
  } else if (record->compression == BUN_COMPRESSION_ZLIB) {
    add_violation(ctx, BUN_UNSUPPORTED,
                  "asset %" PRIu32 ": zlib compression is unsupported", asset_index);
  } else {
    add_violation(ctx, BUN_UNSUPPORTED,
                  "asset %" PRIu32 ": unsupported compression value %" PRIu32,
                  asset_index, record->compression);
  }

  return BUN_OK;
}

static int bytes_look_text(const u8 *buf, size_t size) {
  size_t i = 0;

  for (i = 0; i < size; i++) {
    if ((buf[i] >= 0x20u && buf[i] <= 0x7eu) ||
        buf[i] == '\n' || buf[i] == '\r' || buf[i] == '\t') {
      continue;
    }
    return 0;
  }
  return 1;
}

static void print_escaped_text(FILE *out, const u8 *buf, size_t size) {
  size_t i = 0;

  fputc('"', out);
  for (i = 0; i < size; i++) {
    if (buf[i] == '\\' || buf[i] == '"') {
      fputc('\\', out);
      fputc(buf[i], out);
    } else if (buf[i] == '\n') {
      fputs("\\n", out);
    } else if (buf[i] == '\r') {
      fputs("\\r", out);
    } else if (buf[i] == '\t') {
      fputs("\\t", out);
    } else {
      fputc(buf[i], out);
    }
  }
  fputc('"', out);
}

static void print_bytes(FILE *out, const u8 *buf, size_t size) {
  size_t i = 0;

  if (bytes_look_text(buf, size)) {
    print_escaped_text(out, buf, size);
    return;
  }

  fputs("hex:", out);
  for (i = 0; i < size; i++) {
    fprintf(out, "%s%02x", i == 0 ? " " : " ", buf[i]);
  }
}

static void print_prefix_at(FILE *out, BunParseContext *ctx, u64 offset, u64 size) {
  u8 buf[PREFIX_BYTES];
  size_t to_read = size > PREFIX_BYTES ? PREFIX_BYTES : (size_t)size;

  if (to_read == 0u) {
    fputs("\"\"", out);
    return;
  }
  if (!read_exact_at(ctx, offset, buf, to_read)) {
    fputs("<unavailable>", out);
    return;
  }
  print_bytes(out, buf, to_read);
  if (size > PREFIX_BYTES) {
    fprintf(out, " ... (%" PRIu64 " total bytes)", size);
  }
}

static void print_rle_prefix(FILE *out, BunParseContext *ctx,
                             const BunHeader *header,
                             const BunAssetRecord *record) {
  u64 absolute = header->data_section_offset + record->data_offset;
  u64 remaining = record->data_size;
  u8 compressed[IO_CHUNK_SIZE];
  u8 prefix[PREFIX_BYTES];
  size_t produced = 0;

  while (remaining > 0u && produced < PREFIX_BYTES) {
    size_t to_read = remaining > IO_CHUNK_SIZE ? IO_CHUNK_SIZE : (size_t)remaining;
    size_t i = 0;

    if (to_read % 2u != 0u) {
      to_read--;
    }
    if (to_read == 0u || !read_exact_at(ctx, absolute, compressed, to_read)) {
      break;
    }

    for (i = 0; i < to_read && produced < PREFIX_BYTES; i += 2u) {
      u8 count = compressed[i];
      u8 value = compressed[i + 1u];
      while (count > 0u && produced < PREFIX_BYTES) {
        prefix[produced++] = value;
        count--;
      }
    }

    absolute += (u64)to_read;
    remaining -= (u64)to_read;
  }

  if (produced == 0u && record->uncompressed_size != 0u) {
    fputs("<unavailable>", out);
    return;
  }
  print_bytes(out, prefix, produced);
  if (record->uncompressed_size > PREFIX_BYTES) {
    fprintf(out, " ... (%" PRIu64 " total uncompressed bytes)",
            record->uncompressed_size);
  }
}

static void print_header(FILE *out, const BunHeader *header) {
  fprintf(out, "Header:\n");
  fprintf(out, "  magic: 0x%08" PRIx32 "\n", header->magic);
  fprintf(out, "  version_major: %" PRIu16 "\n", header->version_major);
  fprintf(out, "  version_minor: %" PRIu16 "\n", header->version_minor);
  fprintf(out, "  asset_count: %" PRIu32 "\n", header->asset_count);
  fprintf(out, "  asset_table_offset: %" PRIu64 "\n", header->asset_table_offset);
  fprintf(out, "  string_table_offset: %" PRIu64 "\n", header->string_table_offset);
  fprintf(out, "  string_table_size: %" PRIu64 "\n", header->string_table_size);
  fprintf(out, "  data_section_offset: %" PRIu64 "\n", header->data_section_offset);
  fprintf(out, "  data_section_size: %" PRIu64 "\n", header->data_section_size);
  fprintf(out, "  reserved: %" PRIu64 "\n", header->reserved);
}

static void print_record(FILE *out, BunParseContext *ctx,
                         const BunHeader *header,
                         const BunAssetRecord *record, u32 asset_index) {
  u64 name_end = 0;
  u64 data_end = 0;
  int name_safe = add_u64((u64)record->name_offset,
                          (u64)record->name_length, &name_end) &&
                  name_end <= header->string_table_size &&
                  ctx->string_table_range_ok;
  int data_safe = add_u64(record->data_offset, record->data_size, &data_end) &&
                  data_end <= header->data_section_size &&
                  ctx->data_section_range_ok;

  fprintf(out, "Asset[%" PRIu32 "]:\n", asset_index);
  fprintf(out, "  name_offset: %" PRIu32 "\n", record->name_offset);
  fprintf(out, "  name_length: %" PRIu32 "\n", record->name_length);
  fprintf(out, "  data_offset: %" PRIu64 "\n", record->data_offset);
  fprintf(out, "  data_size: %" PRIu64 "\n", record->data_size);
  fprintf(out, "  uncompressed_size: %" PRIu64 "\n", record->uncompressed_size);
  fprintf(out, "  compression: %" PRIu32 "\n", record->compression);
  fprintf(out, "  type: %" PRIu32 "\n", record->type);
  fprintf(out, "  checksum: 0x%08" PRIx32 "\n", record->checksum);
  fprintf(out, "  flags: 0x%08" PRIx32 "\n", record->flags);

  fputs("  name_prefix: ", out);
  if (name_safe) {
    print_prefix_at(out, ctx,
                    header->string_table_offset + (u64)record->name_offset,
                    (u64)record->name_length);
  } else {
    fputs("<unavailable>", out);
  }
  fputc('\n', out);

  fputs("  data_prefix: ", out);
  if (data_safe) {
    if (record->compression == BUN_COMPRESSION_RLE) {
      print_rle_prefix(out, ctx, header, record);
    } else {
      print_prefix_at(out, ctx,
                      header->data_section_offset + record->data_offset,
                      record->data_size);
    }
  } else {
    fputs("<unavailable>", out);
  }
  fputc('\n', out);
}

bun_result_t bun_open(const char *path, BunParseContext *ctx) {
  if (!path || !ctx) {
    return BUN_ERR_IO;
  }

  memset(ctx, 0, sizeof(*ctx));
  ctx->file = fopen(path, "rb");
  if (!ctx->file) {
    return BUN_ERR_IO;
  }

  if (fseek(ctx->file, 0, SEEK_END) != 0) {
    fclose(ctx->file);
    ctx->file = NULL;
    return BUN_ERR_IO;
  }
  ctx->file_size = ftell(ctx->file);
  if (ctx->file_size < 0) {
    fclose(ctx->file);
    ctx->file = NULL;
    return BUN_ERR_IO;
  }
  ctx->file_size_u64 = (u64)ctx->file_size;
  rewind(ctx->file);

  return BUN_OK;
}

bun_result_t bun_parse_header(BunParseContext *ctx, BunHeader *header) {
  u8 buf[BUN_HEADER_SIZE];
  u64 asset_table_size = 0;
  BunSectionRange sections[4];

  if (!ctx || !header || !ctx->file) {
    return BUN_ERR_IO;
  }
  memset(header, 0, sizeof(*header));

  if (ctx->file_size_u64 < BUN_HEADER_SIZE) {
    add_violation(ctx, BUN_MALFORMED,
                  "header is truncated: file_size=%" PRIu64
                  ", required=%u",
                  ctx->file_size_u64, BUN_HEADER_SIZE);
    return bun_context_result(ctx);
  }

  if (!read_exact_at(ctx, 0, buf, sizeof(buf))) {
    return BUN_ERR_IO;
  }

  decode_header(buf, header);
  ctx->header_loaded = 1;

  if (header->magic != BUN_MAGIC) {
    add_violation(ctx, BUN_MALFORMED,
                  "header magic is invalid: got 0x%08" PRIx32
                  ", expected 0x%08x",
                  header->magic, BUN_MAGIC);
  }

  if (header->version_major != BUN_VERSION_MAJOR ||
      header->version_minor != BUN_VERSION_MINOR) {
    add_violation(ctx, BUN_UNSUPPORTED,
                  "BUN version is unsupported: got %" PRIu16 ".%" PRIu16
                  ", expected %d.%d",
                  header->version_major, header->version_minor,
                  BUN_VERSION_MAJOR, BUN_VERSION_MINOR);
  }

  if (header->magic != BUN_MAGIC ||
      header->version_major != BUN_VERSION_MAJOR ||
      header->version_minor != BUN_VERSION_MINOR) {
    return bun_context_result(ctx);
  }

  if (!aligned4(header->asset_table_offset)) {
    add_violation(ctx, BUN_MALFORMED,
                  "asset_table_offset must be divisible by 4, got %" PRIu64,
                  header->asset_table_offset);
  }
  if (!aligned4(header->string_table_offset)) {
    add_violation(ctx, BUN_MALFORMED,
                  "string_table_offset must be divisible by 4, got %" PRIu64,
                  header->string_table_offset);
  }
  if (!aligned4(header->string_table_size)) {
    add_violation(ctx, BUN_MALFORMED,
                  "string_table_size must be divisible by 4, got %" PRIu64,
                  header->string_table_size);
  }
  if (!aligned4(header->data_section_offset)) {
    add_violation(ctx, BUN_MALFORMED,
                  "data_section_offset must be divisible by 4, got %" PRIu64,
                  header->data_section_offset);
  }
  if (!aligned4(header->data_section_size)) {
    add_violation(ctx, BUN_MALFORMED,
                  "data_section_size must be divisible by 4, got %" PRIu64,
                  header->data_section_size);
  }

  if (!mul_u64((u64)header->asset_count, BUN_ASSET_RECORD_SIZE,
               &asset_table_size)) {
    add_violation(ctx, BUN_MALFORMED,
                  "asset table size overflows u64: asset_count=%" PRIu32,
                  header->asset_count);
  } else {
    check_section_range(ctx, "asset table", header->asset_table_offset,
                        asset_table_size, &ctx->asset_table_range_ok);
  }
  check_section_range(ctx, "string table", header->string_table_offset,
                      header->string_table_size, &ctx->string_table_range_ok);
  check_section_range(ctx, "data", header->data_section_offset,
                      header->data_section_size, &ctx->data_section_range_ok);

  sections[0] = (BunSectionRange){"header", 0, BUN_HEADER_SIZE, 1};
  sections[1] = (BunSectionRange){"asset table", header->asset_table_offset,
                                  asset_table_size, ctx->asset_table_range_ok};
  sections[2] = (BunSectionRange){"string table", header->string_table_offset,
                                  header->string_table_size,
                                  ctx->string_table_range_ok};
  sections[3] = (BunSectionRange){"data", header->data_section_offset,
                                  header->data_section_size,
                                  ctx->data_section_range_ok};
  check_no_section_overlap(ctx, sections, sizeof(sections) / sizeof(sections[0]));

  return bun_context_result(ctx);
}

bun_result_t bun_parse_assets(BunParseContext *ctx, const BunHeader *header) {
  u32 i = 0;

  if (!ctx || !header || !ctx->file) {
    return BUN_ERR_IO;
  }
  if (!ctx->header_loaded) {
    add_violation(ctx, BUN_MALFORMED,
                  "cannot parse assets before a complete header is parsed");
    return bun_context_result(ctx);
  }
  if (header->magic != BUN_MAGIC ||
      header->version_major != BUN_VERSION_MAJOR ||
      header->version_minor != BUN_VERSION_MINOR) {
    return bun_context_result(ctx);
  }
  if (!ctx->asset_table_range_ok) {
    return bun_context_result(ctx);
  }

  for (i = 0; i < header->asset_count; i++) {
    u8 buf[BUN_ASSET_RECORD_SIZE];
    BunAssetRecord record;
    u64 record_offset = header->asset_table_offset +
                        (u64)i * (u64)BUN_ASSET_RECORD_SIZE;
    bun_result_t r = BUN_OK;

    if (!read_exact_at(ctx, record_offset, buf, sizeof(buf))) {
      return BUN_ERR_IO;
    }
    decode_asset_record(buf, &record);
    r = validate_asset_record(ctx, header, &record, i);
    if (r != BUN_OK) {
      return r;
    }
  }

  return bun_context_result(ctx);
}

bun_result_t bun_close(BunParseContext *ctx) {
  assert(ctx);
  assert(ctx->file);

  int res = fclose(ctx->file);
  if (res) {
    return BUN_ERR_IO;
  }

  ctx->file = NULL;
  return BUN_OK;
}

bun_result_t bun_context_result(const BunParseContext *ctx) {
  if (!ctx) {
    return BUN_ERR_IO;
  }
  if (ctx->malformed_count > 0u) {
    return BUN_MALFORMED;
  }
  if (ctx->unsupported_count > 0u) {
    return BUN_UNSUPPORTED;
  }
  return BUN_OK;
}

void bun_print_violations(FILE *out, const BunParseContext *ctx) {
  size_t i = 0;

  if (!out || !ctx) {
    return;
  }
  for (i = 0; i < ctx->violation_count; i++) {
    const char *kind = ctx->violations[i].result == BUN_UNSUPPORTED
                         ? "unsupported"
                         : "malformed";
    fprintf(out, "%s: %s\n", kind, ctx->violations[i].message);
  }
  if (ctx->violation_overflow) {
    fprintf(out, "malformed: additional violations omitted after %u entries\n",
            BUN_MAX_VIOLATIONS);
  }
}

void bun_print_summary(FILE *out, BunParseContext *ctx, const BunHeader *header) {
  u32 i = 0;

  if (!out || !ctx) {
    return;
  }

  fprintf(out, "BUN parser summary\n");
  fprintf(out, "File size: %" PRIu64 " bytes\n", ctx->file_size_u64);

  if (!ctx->header_loaded || !header) {
    fputs("Header: <unavailable>\n", out);
    return;
  }

  print_header(out, header);

  if (!ctx->asset_table_range_ok ||
      header->magic != BUN_MAGIC ||
      header->version_major != BUN_VERSION_MAJOR ||
      header->version_minor != BUN_VERSION_MINOR) {
    fputs("Assets: <unavailable>\n", out);
    return;
  }

  fprintf(out, "Assets: %" PRIu32 "\n", header->asset_count);
  for (i = 0; i < header->asset_count; i++) {
    u8 buf[BUN_ASSET_RECORD_SIZE];
    BunAssetRecord record;
    u64 record_offset = header->asset_table_offset +
                        (u64)i * (u64)BUN_ASSET_RECORD_SIZE;

    if (!read_exact_at(ctx, record_offset, buf, sizeof(buf))) {
      fprintf(out, "Asset[%" PRIu32 "]: <unavailable>\n", i);
      continue;
    }
    decode_asset_record(buf, &record);
    print_record(out, ctx, header, &record, i);
  }
}
