#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

#include "bun.h"

/**
 * Example helper: convert 4 bytes in `buf`, positioned at `offset`,
 * into a little-endian u32.
 */

static u16 read_u16_le(const u8 *buf, size_t offset) {
  return (u16)buf[offset]
     | (u16)buf[offset + 1] << 8;
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

typedef struct {
  u64 offset;
  u64 size;
} BunSection;

/*
 * Keep arithmetic helpers small and explicit so every offset/size calculation
 * is checked before it is used in fseek()/fread().
 */
static int add_u64_checked(u64 lhs, u64 rhs, u64 *result) {
  if (UINT64_MAX - lhs < rhs) {
    return 0;
  }

  *result = lhs + rhs;
  return 1;
}

static int mul_u64_checked(u64 lhs, u64 rhs, u64 *result) {
  if (lhs != 0 && UINT64_MAX / lhs < rhs) {
    return 0;
  }

  *result = lhs * rhs;
  return 1;
}

static u64 ctx_file_size_u64(const BunParseContext *ctx) {
  assert(ctx->file_size >= 0);
  return (u64)ctx->file_size;
}

static int section_end(const BunSection *section, u64 *end) {
  return add_u64_checked(section->offset, section->size, end);
}

static int section_within_file(const BunSection *section, u64 file_size) {
  u64 end = 0;

  if (!section_end(section, &end)) {
    return 0;
  }

  return section->offset <= file_size && end <= file_size;
}

static int sections_overlap(const BunSection *lhs, const BunSection *rhs) {
  u64 lhs_end = 0;
  u64 rhs_end = 0;

  if (lhs->size == 0 || rhs->size == 0) {
    return 0;
  }
  if (!section_end(lhs, &lhs_end) || !section_end(rhs, &rhs_end)) {
    return 1;
  }

  return lhs->offset < rhs_end && rhs->offset < lhs_end;
}

static int seek_to_u64(FILE *file, u64 offset) {
  if (offset > (u64)LONG_MAX) {
    return 0;
  }

  return fseek(file, (long)offset, SEEK_SET) == 0;
}

static int read_exact(FILE *file, void *buf, size_t size) {
  return fread(buf, 1, size, file) == size;
}

static int read_asset_record(FILE *file, u64 offset, BunAssetRecord *record) {
  u8 buf[BUN_ASSET_RECORD_SIZE];

  if (!seek_to_u64(file, offset)) {
    return 0;
  }
  if (!read_exact(file, buf, sizeof(buf))) {
    return 0;
  }

  record->name_offset       = read_u32_le(buf, 0);
  record->name_length       = read_u32_le(buf, 4);
  record->data_offset       = read_u64_le(buf, 8);
  record->data_size         = read_u64_le(buf, 16);
  record->uncompressed_size = read_u64_le(buf, 24);
  record->compression       = read_u32_le(buf, 32);
  record->type              = read_u32_le(buf, 36);
  record->checksum          = read_u32_le(buf, 40);
  record->flags             = read_u32_le(buf, 44);

  return 1;
}

/*
 * Asset names are required to be non-empty printable ASCII stored inside the
 * string table. Validate them by streaming a small chunk at a time.
 */
static bun_result_t validate_asset_name(BunParseContext *ctx,
                                        const BunHeader *header,
                                        const BunAssetRecord *record) {
  u8  buf[256];
  u64 remaining = (u64)record->name_length;
  u64 offset = 0;
  u64 name_start = 0;

  if (record->name_length == 0) {
    return BUN_MALFORMED;
  }

  if (!add_u64_checked(header->string_table_offset,
                       (u64)record->name_offset,
                       &name_start)) {
    return BUN_MALFORMED;
  }

  while (remaining > 0) {
    size_t chunk = remaining < sizeof(buf) ? (size_t)remaining : sizeof(buf);
    size_t idx = 0;
    u64 chunk_start = 0;

    if (!add_u64_checked(name_start, offset, &chunk_start)) {
      return BUN_MALFORMED;
    }
    if (!seek_to_u64(ctx->file, chunk_start)) {
      return BUN_ERR_IO;
    }
    if (!read_exact(ctx->file, buf, chunk)) {
      return BUN_ERR_IO;
    }

    for (idx = 0; idx < chunk; idx++) {
      if (buf[idx] < 0x20 || buf[idx] > 0x7e) {
        return BUN_MALFORMED;
      }
    }

    remaining -= chunk;
    offset += chunk;
  }

  return BUN_OK;
}

/*
 * RLE validation is done without allocating an output buffer. This keeps the
 * parser's memory use sub-linear even for large data sections.
 */
static bun_result_t validate_rle_data(BunParseContext *ctx,
                                      const BunHeader *header,
                                      const BunAssetRecord *record) {
  u8  buf[512];
  u64 remaining = record->data_size;
  u64 data_start = 0;
  u64 expanded_size = 0;

  if ((record->data_size % 2u) != 0u) {
    return BUN_MALFORMED;
  }

  if (!add_u64_checked(header->data_section_offset,
                       record->data_offset,
                       &data_start)) {
    return BUN_MALFORMED;
  }

  if (!seek_to_u64(ctx->file, data_start)) {
    return BUN_ERR_IO;
  }

  while (remaining > 0) {
    size_t chunk = remaining < sizeof(buf) ? (size_t)remaining : sizeof(buf);
    size_t idx = 0;

    if (!read_exact(ctx->file, buf, chunk)) {
      return BUN_ERR_IO;
    }

    for (idx = 0; idx < chunk; idx += 2) {
      u8 count = buf[idx];

      if (count == 0) {
        return BUN_MALFORMED;
      }
      if (!add_u64_checked(expanded_size, (u64)count, &expanded_size)) {
        return BUN_MALFORMED;
      }
    }

    remaining -= chunk;
  }

  return expanded_size == record->uncompressed_size
       ? BUN_OK
       : BUN_MALFORMED;
}

//
// API implementation
//

bun_result_t bun_open(const char *path, BunParseContext *ctx) {
  // we open the file; seek to the end, to get the size; then jump back to the
  // beginning, ready to start parsing.

  ctx->file = fopen(path, "rb");
  if (!ctx->file) {
    return BUN_ERR_IO;
  }

  if (fseek(ctx->file, 0, SEEK_END) != 0) {
    fclose(ctx->file);
    return BUN_ERR_IO;
  }
  ctx->file_size = ftell(ctx->file);
  if (ctx->file_size < 0) {
    fclose(ctx->file);
    return BUN_ERR_IO;
  }
  rewind(ctx->file);

  return BUN_OK;
}

bun_result_t bun_parse_header(BunParseContext *ctx, BunHeader *header) {
  u8 buf[BUN_HEADER_SIZE];

  // our file is far too short, and cannot be valid!
  // (query: how do we let `main` know that "file was too short"
  // was the exact problem? Where can we put details about the
  // exact validation problem that occurred?)
  if (ctx->file_size < (long)BUN_HEADER_SIZE) {
    return BUN_MALFORMED;
  }

  // slurp the header into `buf`
  if (fread(buf, 1, BUN_HEADER_SIZE, ctx->file) != BUN_HEADER_SIZE) {
    return BUN_ERR_IO;
  }

  // Decode the fixed-size header directly from the on-disk byte buffer.
  header->magic                = read_u32_le(buf, 0);
  header->version_major        = read_u16_le(buf, 4);
  header->version_minor        = read_u16_le(buf, 6);
  header->asset_count          = read_u32_le(buf, 8);
  header->asset_table_offset   = read_u64_le(buf, 12);
  header->string_table_offset  = read_u64_le(buf, 20);
  header->string_table_size    = read_u64_le(buf, 28);
  header->data_section_offset  = read_u64_le(buf, 36);
  header->data_section_size    = read_u64_le(buf, 44);
  header->reserved             = read_u64_le(buf, 52);

  // The magic must match exactly for the file to be a BUN container.
  if (header->magic != BUN_MAGIC) {
    return BUN_MALFORMED;
  }

  // Reject misaligned sections before attempting any deeper parsing.
  if (header->asset_table_offset % 4 != 0
    || header->string_table_offset % 4 != 0
    || header->data_section_offset % 4 != 0
    || header->string_table_size % 4 != 0
    || header->data_section_size % 4 != 0) {
    return BUN_MALFORMED;
  }

  // Version Check
  if (header->version_major != 1
    || header->version_minor != 0) {
    return BUN_UNSUPPORTED;
  }

  return BUN_OK;
}

bun_result_t bun_parse_assets(BunParseContext *ctx, const BunHeader *header) {
  BunSection header_section = {0, BUN_HEADER_SIZE};
  BunSection asset_section = {0, 0};
  BunSection string_section = {header->string_table_offset, header->string_table_size};
  BunSection data_section = {header->data_section_offset, header->data_section_size};
  u64 file_size = ctx_file_size_u64(ctx);
  u64 asset_table_size = 0;
  u64 record_offset = 0;
  u32 idx = 0;
  int saw_unsupported = 0;

  if (!mul_u64_checked((u64)header->asset_count,
                       (u64)BUN_ASSET_RECORD_SIZE,
                       &asset_table_size)) {
    return BUN_MALFORMED;
  }

  asset_section.offset = header->asset_table_offset;
  asset_section.size = asset_table_size;

  if (!section_within_file(&header_section, file_size)
    || !section_within_file(&asset_section, file_size)
    || !section_within_file(&string_section, file_size)
    || !section_within_file(&data_section, file_size)) {
    return BUN_MALFORMED;
  }

  if (sections_overlap(&header_section, &asset_section)
    || sections_overlap(&header_section, &string_section)
    || sections_overlap(&header_section, &data_section)
    || sections_overlap(&asset_section, &string_section)
    || sections_overlap(&asset_section, &data_section)
    || sections_overlap(&string_section, &data_section)) {
    return BUN_MALFORMED;
  }

  for (idx = 0; idx < header->asset_count; idx++) {
    BunAssetRecord record = {0};
    u64 name_end = 0;
    u64 data_end = 0;
    u32 unsupported_flag_bits = 0;
    bun_result_t result = BUN_OK;

    if (!mul_u64_checked((u64)idx, (u64)BUN_ASSET_RECORD_SIZE, &record_offset)) {
      return BUN_MALFORMED;
    }
    if (!add_u64_checked(header->asset_table_offset, record_offset, &record_offset)) {
      return BUN_MALFORMED;
    }
    if (!read_asset_record(ctx->file, record_offset, &record)) {
      return BUN_ERR_IO;
    }

    if (!add_u64_checked((u64)record.name_offset,
                         (u64)record.name_length,
                         &name_end)
      || name_end > header->string_table_size) {
      return BUN_MALFORMED;
    }

    if (!add_u64_checked(record.data_offset, record.data_size, &data_end)
      || data_end > header->data_section_size) {
      return BUN_MALFORMED;
    }

    result = validate_asset_name(ctx, header, &record);
    if (result != BUN_OK) {
      return result;
    }

    unsupported_flag_bits = record.flags & ~(BUN_FLAG_ENCRYPTED | BUN_FLAG_EXECUTABLE);
    if (unsupported_flag_bits != 0u) {
      saw_unsupported = 1;
    }
    if (record.checksum != 0u) {
      saw_unsupported = 1;
    }

    if (record.compression == BUN_COMPRESSION_NONE) {
      if (record.uncompressed_size != 0u) {
        return BUN_MALFORMED;
      }
      continue;
    }

    if (record.compression == BUN_COMPRESSION_RLE) {
      result = validate_rle_data(ctx, header, &record);
      if (result != BUN_OK) {
        return result;
      }
      continue;
    }

    if (record.compression == BUN_COMPRESSION_ZLIB) {
      saw_unsupported = 1;
      continue;
    }

    saw_unsupported = 1;
  }

  return saw_unsupported ? BUN_UNSUPPORTED : BUN_OK;
}

bun_result_t bun_close(BunParseContext *ctx) {
  assert(ctx->file);

  int res = fclose(ctx->file);
  if (res) {
    return BUN_ERR_IO;
  } else {
    ctx->file = NULL;
    return BUN_OK;
  }
}
