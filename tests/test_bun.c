#include "../bun.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  u32 magic;
  u16 version_major;
  u16 version_minor;
  u32 asset_count;
  u64 asset_table_offset;
  u64 string_table_offset;
  u64 string_table_size;
  u64 data_section_offset;
  u64 data_section_size;
  u64 reserved;
} TestHeader;

typedef struct {
  u32 name_offset;
  u32 name_length;
  u64 data_offset;
  u64 data_size;
  u64 uncompressed_size;
  u32 compression;
  u32 type;
  u32 checksum;
  u32 flags;
} TestRecord;

static int failures = 0;

static void write_u16_le(FILE *file, u16 value) {
  fputc(value & 0xffu, file);
  fputc((value >> 8) & 0xffu, file);
}

static void write_u32_le(FILE *file, u32 value) {
  fputc(value & 0xffu, file);
  fputc((value >> 8) & 0xffu, file);
  fputc((value >> 16) & 0xffu, file);
  fputc((value >> 24) & 0xffu, file);
}

static void write_u64_le(FILE *file, u64 value) {
  int shift = 0;

  for (shift = 0; shift < 64; shift += 8) {
    fputc((int)((value >> shift) & 0xffu), file);
  }
}

static void write_header(FILE *file, const TestHeader *header) {
  write_u32_le(file, header->magic);
  write_u16_le(file, header->version_major);
  write_u16_le(file, header->version_minor);
  write_u32_le(file, header->asset_count);
  write_u64_le(file, header->asset_table_offset);
  write_u64_le(file, header->string_table_offset);
  write_u64_le(file, header->string_table_size);
  write_u64_le(file, header->data_section_offset);
  write_u64_le(file, header->data_section_size);
  write_u64_le(file, header->reserved);
}

static void write_record(FILE *file, const TestRecord *record) {
  write_u32_le(file, record->name_offset);
  write_u32_le(file, record->name_length);
  write_u64_le(file, record->data_offset);
  write_u64_le(file, record->data_size);
  write_u64_le(file, record->uncompressed_size);
  write_u32_le(file, record->compression);
  write_u32_le(file, record->type);
  write_u32_le(file, record->checksum);
  write_u32_le(file, record->flags);
}

static void pad_to(FILE *file, long offset) {
  long pos = ftell(file);

  while (pos < offset) {
    fputc('\0', file);
    pos++;
  }
}

static TestHeader base_header(u32 asset_count) {
  TestHeader header;

  memset(&header, 0, sizeof(header));
  header.magic = BUN_MAGIC;
  header.version_major = BUN_VERSION_MAJOR;
  header.version_minor = BUN_VERSION_MINOR;
  header.asset_count = asset_count;
  header.asset_table_offset = BUN_HEADER_SIZE;
  header.string_table_offset = BUN_HEADER_SIZE +
                               (u64)asset_count * BUN_ASSET_RECORD_SIZE;
  header.string_table_size = asset_count == 0u ? 0u : 8u;
  header.data_section_offset = header.string_table_offset + header.string_table_size;
  header.data_section_size = asset_count == 0u ? 0u : 4u;
  return header;
}

static TestRecord base_record(void) {
  TestRecord record;

  memset(&record, 0, sizeof(record));
  record.name_length = 4;
  record.data_size = 4;
  record.type = 1;
  return record;
}

static void write_one_asset_file(const char *path, TestHeader header,
                                 TestRecord record, const u8 name[4],
                                 const u8 data[4]) {
  FILE *file = fopen(path, "wb");

  if (!file) {
    perror(path);
    exit(EXIT_FAILURE);
  }

  write_header(file, &header);
  if (header.asset_count > 0u) {
    pad_to(file, (long)header.asset_table_offset);
    write_record(file, &record);
    pad_to(file, (long)header.string_table_offset);
    fwrite(name, 1, 4, file);
    pad_to(file, (long)(header.string_table_offset + header.string_table_size));
    fwrite(data, 1, 4, file);
    pad_to(file, (long)(header.data_section_offset + header.data_section_size));
  }

  fclose(file);
}

static bun_result_t parse_file(const char *path) {
  BunParseContext ctx = {0};
  BunHeader header = {0};
  bun_result_t result = bun_open(path, &ctx);

  if (result != BUN_OK) {
    return result;
  }

  result = bun_parse_header(&ctx, &header);
  if (result != BUN_ERR_IO && ctx.header_loaded) {
    result = bun_parse_assets(&ctx, &header);
  }
  if (result != BUN_ERR_IO) {
    result = bun_context_result(&ctx);
  }

  bun_close(&ctx);
  return result;
}

static void expect_result(const char *name, const char *path, bun_result_t expected) {
  bun_result_t actual = parse_file(path);

  if (actual != expected) {
    fprintf(stderr, "FAIL %-28s expected %d got %d\n", name, expected, actual);
    failures++;
  } else {
    printf("PASS %s\n", name);
  }
  remove(path);
}

static void test_valid_empty(void) {
  const char *path = "tests/tmp-valid-empty.bun";
  TestHeader header = base_header(0);
  TestRecord record = base_record();
  const u8 name[4] = {'t', 'e', 's', 't'};
  const u8 data[4] = {'d', 'a', 't', 'a'};

  write_one_asset_file(path, header, record, name, data);
  expect_result("valid empty", path, BUN_OK);
}

static void test_valid_one_asset(void) {
  const char *path = "tests/tmp-valid-one.bun";
  TestHeader header = base_header(1);
  TestRecord record = base_record();
  const u8 name[4] = {'t', 'e', 's', 't'};
  const u8 data[4] = {'d', 'a', 't', 'a'};

  write_one_asset_file(path, header, record, name, data);
  expect_result("valid one asset", path, BUN_OK);
}

static void test_bad_magic(void) {
  const char *path = "tests/tmp-bad-magic.bun";
  TestHeader header = base_header(0);
  TestRecord record = base_record();
  const u8 name[4] = {'t', 'e', 's', 't'};
  const u8 data[4] = {'d', 'a', 't', 'a'};

  header.magic = BUN_MAGIC - 1u;
  write_one_asset_file(path, header, record, name, data);
  expect_result("bad magic", path, BUN_MALFORMED);
}

static void test_bad_version(void) {
  const char *path = "tests/tmp-bad-version.bun";
  TestHeader header = base_header(0);
  TestRecord record = base_record();
  const u8 name[4] = {'t', 'e', 's', 't'};
  const u8 data[4] = {'d', 'a', 't', 'a'};

  header.version_major = 2;
  write_one_asset_file(path, header, record, name, data);
  expect_result("bad version", path, BUN_UNSUPPORTED);
}

static void test_name_nonprintable(void) {
  const char *path = "tests/tmp-name-nonprintable.bun";
  TestHeader header = base_header(1);
  TestRecord record = base_record();
  const u8 name[4] = {'t', 'e', 1, 't'};
  const u8 data[4] = {'d', 'a', 't', 'a'};

  write_one_asset_file(path, header, record, name, data);
  expect_result("name nonprintable", path, BUN_MALFORMED);
}

static void test_data_oob(void) {
  const char *path = "tests/tmp-data-oob.bun";
  TestHeader header = base_header(1);
  TestRecord record = base_record();
  const u8 name[4] = {'t', 'e', 's', 't'};
  const u8 data[4] = {'d', 'a', 't', 'a'};

  record.data_size = 8;
  write_one_asset_file(path, header, record, name, data);
  expect_result("data out of bounds", path, BUN_MALFORMED);
}

static void test_rle_valid(void) {
  const char *path = "tests/tmp-rle-valid.bun";
  TestHeader header = base_header(1);
  TestRecord record = base_record();
  const u8 name[4] = {'t', 'e', 's', 't'};
  const u8 data[4] = {10, 'A', 5, 'B'};

  record.compression = BUN_COMPRESSION_RLE;
  record.uncompressed_size = 15;
  write_one_asset_file(path, header, record, name, data);
  expect_result("rle valid", path, BUN_OK);
}

static void test_rle_zero_count(void) {
  const char *path = "tests/tmp-rle-zero.bun";
  TestHeader header = base_header(1);
  TestRecord record = base_record();
  const u8 name[4] = {'t', 'e', 's', 't'};
  const u8 data[4] = {10, 'A', 0, 'B'};

  record.compression = BUN_COMPRESSION_RLE;
  record.uncompressed_size = 10;
  write_one_asset_file(path, header, record, name, data);
  expect_result("rle zero count", path, BUN_MALFORMED);
}

static void test_zlib_unsupported(void) {
  const char *path = "tests/tmp-zlib.bun";
  TestHeader header = base_header(1);
  TestRecord record = base_record();
  const u8 name[4] = {'t', 'e', 's', 't'};
  const u8 data[4] = {'z', 'l', 'i', 'b'};

  record.compression = BUN_COMPRESSION_ZLIB;
  record.uncompressed_size = 4;
  write_one_asset_file(path, header, record, name, data);
  expect_result("zlib unsupported", path, BUN_UNSUPPORTED);
}

static void test_checksum_unsupported(void) {
  const char *path = "tests/tmp-checksum.bun";
  TestHeader header = base_header(1);
  TestRecord record = base_record();
  const u8 name[4] = {'t', 'e', 's', 't'};
  const u8 data[4] = {'d', 'a', 't', 'a'};

  record.checksum = 0x12345678u;
  write_one_asset_file(path, header, record, name, data);
  expect_result("checksum unsupported", path, BUN_UNSUPPORTED);
}

static void test_flags_unsupported(void) {
  const char *path = "tests/tmp-flags.bun";
  TestHeader header = base_header(1);
  TestRecord record = base_record();
  const u8 name[4] = {'t', 'e', 's', 't'};
  const u8 data[4] = {'d', 'a', 't', 'a'};

  record.flags = 0x4u;
  write_one_asset_file(path, header, record, name, data);
  expect_result("flags unsupported", path, BUN_UNSUPPORTED);
}

int main(void) {
  test_valid_empty();
  test_valid_one_asset();
  test_bad_magic();
  test_bad_version();
  test_name_nonprintable();
  test_data_oob();
  test_rle_valid();
  test_rle_zero_count();
  test_zlib_unsupported();
  test_checksum_unsupported();
  test_flags_unsupported();

  if (failures != 0) {
    fprintf(stderr, "%d test(s) failed\n", failures);
    return EXIT_FAILURE;
  }

  puts("All tests passed");
  return EXIT_SUCCESS;
}
