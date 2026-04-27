#include "../bun.h"
#include <check.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

// Helper: terminate abnormally, after printing a message to stderr
void die(const char *fmt, ...){

  va_list args;
  va_start(args, fmt);

  fprintf(stderr, "fatal error: ");
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");

  va_end(args);

  abort();
}


// Helper: open a test fixture by name, relative to the tests/ directory.
static const char *fixture(const char *filename) {
    // For simplicity, tests assume they are run from the project root, and
    // test BUN files live in tests/samples/{valid,invalid}. Adjust if needed.
    static char path[256];
    int res = snprintf(path, sizeof(path), "tests/samples/%s", filename);
    if (res < 0 ) {
      die("snprintf failed: %s, for %s", strerror(errno), filename);
    }
    if ((size_t) res > sizeof(path)) {
      die("filename '%s' too big for buffer (would write %d bytes to %zu-size buffer)",
          filename, res, sizeof(path));
    }
    return path;
}

// Example test suite: header parsing

START_TEST(test_valid_minimal) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    ck_assert_int_eq(bun_open(fixture("valid/01-empty.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    ck_assert_uint_eq(header.magic, BUN_MAGIC);
    bun_close(&ctx);
} END_TEST

START_TEST(test_valid_alt_minimal){
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("valid/02-alt-empty.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_OK);
    ck_assert_uint_eq(header.magic, BUN_MAGIC);
    ck_assert_uint_eq(header.version_major, 1);
    ck_assert_uint_eq(header.version_minor, 0);

    bun_close(&ctx);
}END_TEST


START_TEST(test_valid_one_asset){
    BunParseContext ctx = {0};
    BunHeader header = {0};

    ck_assert_int_eq(bun_open(fixture("valid/03-one-asset.bun"), &ctx), BUN_OK);

    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);

    ck_assert_uint_eq(header.asset_count, 1);

    bun_close(&ctx);
}END_TEST


START_TEST(test_valid_binar_asset){
    BunParseContext ctx = {0};
    BunHeader header = {0};

    ck_assert_int_eq(bun_open(fixture("valid/04-binary-asset.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);

    ck_assert_uint_eq(header.magic, BUN_MAGIC);
    ck_assert_uint_gt(header.asset_count, 0);

    bun_close(&ctx);
}END_TEST


START_TEST(test_valid_multi_asset_stack){
    BunParseContext ctx = {0};
    BunHeader header = {0};

    ck_assert_int_eq(bun_open(fixture("valid/05-multi-assets-slack.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);

    ck_assert_uint_gt(header.asset_count, 1);

    bun_close(&ctx);
}END_TEST

START_TEST(test_valid_rle) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    ck_assert_int_eq(bun_open(fixture("valid/06-rle-valid.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_OK);
    bun_close(&ctx);
} END_TEST


START_TEST(test_valid_zero_assets) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    // This file has a valid magic/version but asset_count = 0
    ck_assert_int_eq(bun_open(fixture("valid/07-zero-assets.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    
    ck_assert_uint_eq(header.asset_count, 0);

    // Should return BUN_OK and simply do nothing
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_OK);
    
    // Ensure assets array was never allocated or is NULL
    ck_assert_ptr_null(ctx.assets);

    bun_close(&ctx);
} END_TEST


START_TEST(test_valid_unordered_layout) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    // File layout: [Data] -> [String Table] -> [Header] 
    // The Header is at the end, but the parser finds it via internal logic
    ck_assert_int_eq(bun_open(fixture("valid/08-unordered.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    
    // Even though it's shuffled, it should still extract data correctly
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_OK);
    
    ck_assert_uint_eq(header.asset_count, 1);
    ck_assert_str_eq(ctx.assets[0].name, "shuffled_asset");

    bun_close(&ctx);
} END_TEST


START_TEST(test_valid_stress_complex) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    // File contains 100 assets, various sizes, mixed RLE and RAW
    ck_assert_int_eq(bun_open(fixture("valid/09-stress-test.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_OK);
    
    // Verify a random asset in the middle to ensure index logic is solid
    ck_assert_uint_eq(header.asset_count, 100);
    ck_assert_ptr_nonnull(ctx.assets[99].data); 
    
    // Check that we didn't leak memory during this large parse
    bun_close(&ctx);
} END_TEST


/*******************************************************************/
/*******************************************************************/

START_TEST(test_bad_magic) {
    BunParseContext ctx = {0};
    BunHeader header = {0};
    
    ck_assert_int_eq(bun_open(fixture("invalid/01-bad-magic.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
} END_TEST

START_TEST(test_bad_version) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    ck_assert_int_eq(bun_open(fixture("invalid/02-bad-version.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_UNSUPPORTED);
    bun_close(&ctx);
} END_TEST

START_TEST(test_bad_offset_alignment) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    ck_assert_int_eq(bun_open(fixture("invalid/03-bad-offset-alignment.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
} END_TEST



START_TEST(test_bad_section_past_eof) {
    BunParseContext ctx = {0};
    BunHeader header = {0};
    ck_assert_int_eq(bun_open(fixture("invalid/04-section-past-eof.bun"), &ctx), BUN_OK);
    // Header is structurally fine
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    // Logic error caught here
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
} END_TEST

START_TEST(test_bad_overlapping_sections) {
    BunParseContext ctx = {0};
    BunHeader header = {0};
    ck_assert_int_eq(bun_open(fixture("invalid/05-overlapping-sections.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
} END_TEST

START_TEST(test_bad_asset_name_past_string_table) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    bun_open(fixture("invalid/06-asset-name-past-string-table.bun"), &ctx);
    
    bun_parse_header(&ctx, &header);
    
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
}
END_TEST

 
START_TEST(test_bad_asset_name_nonprintable) {
    BunParseContext ctx = {0};
    BunHeader header = {0};
    ck_assert_int_eq(bun_open(fixture("invalid/07-asset-name-nonprintable.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
} END_TEST


START_TEST(test_bad_truncated_file) {
    BunParseContext ctx = {0};
    BunHeader header = {0};
    
    bun_open(fixture("invalid/08-truncated-file.bun"), &ctx);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
}
END_TEST


/*******************************************************************/
/*******************************************************************/

START_TEST(test_bad_misaligned_section_size) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    bun_open(fixture("invalid/17-misaligned-section-size.bun"), &ctx);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
}
END_TEST


START_TEST(test_bad_header_logic_contradiction) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    // This file has a string_table_offset that starts BEFORE the asset_table ends.
    // [Header: 0-60] [Asset Table: 60-108] [String Table: 80-...] <-- OVERLAP!
    ck_assert_int_eq(bun_open(fixture("invalid/18-header-contradiction.bun"), &ctx), BUN_OK);
    
    // The parser should detect the overlap during header validation
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_MALFORMED);

    bun_close(&ctx);
} END_TEST


START_TEST(test_bad_asset_count_mismatch) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    // Header says asset_count = 100, but the file ends right after the header.
    // 100 * 48 = 4800 bytes expected, but 0 bytes exist.
    ck_assert_int_eq(bun_open(fixture("invalid/19-asset-count-mismatch.bun"), &ctx), BUN_OK);
    
    // bun_parse_header should calculate: header_size + (count * 48) 
    // and compare it against the actual file size.
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_MALFORMED);

    bun_close(&ctx);
} END_TEST


START_TEST(test_bad_asset_count_max) {
    BunParseContext ctx = {0};
    BunHeader header = {0};
    // Header claims UINT32_MAX assets. malloc(UINT32_MAX * 48) will overflow 
    // to a small number on 32-bit systems or simply fail.
    ck_assert_int_eq(bun_open(fixture("invalid/20-asset-count-max.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
} END_TEST


START_TEST(test_bad_size_overflow) {
    BunParseContext ctx = {0};
    BunHeader header = {0};
    // Claims a data_section_size so large that Header + Size > UINT64_MAX.
    ck_assert_int_eq(bun_open(fixture("invalid/21-size-overflow.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
} END_TEST


START_TEST(test_bad_offset_just_past_eof) {
    BunParseContext ctx = {0};
    BunHeader header = {0};
    // Points to EOF + 1 byte.
    ck_assert_int_eq(bun_open(fixture("invalid/22-offset-past-eof.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
} END_TEST


START_TEST(test_valid_offset_at_end) {
    BunParseContext ctx = {0};
    BunHeader header = {0};
    // An empty section pointing exactly to the end of the file is technically valid.
    ck_assert_int_eq(bun_open(fixture("valid/23-offset-at-eof.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    bun_close(&ctx);
} END_TEST


START_TEST(test_bad_duplicate_data_offsets) {
    BunParseContext ctx = {0};
    BunHeader header = {0};
    ck_assert_int_eq(bun_open(fixture("invalid/24-duplicate-offsets.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    // If your security policy forbids aliasing, this should fail.
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
} END_TEST


START_TEST(test_bad_truncated_mid_string_table) {
    BunParseContext ctx = {0};
    BunHeader header = {0};
    // File ends halfway through a name string.
    ck_assert_int_eq(bun_open(fixture("invalid/25-trunc-string.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
} END_TEST


START_TEST(test_bad_truncated_mid_asset_table) {
    BunParseContext ctx = {0};
    BunHeader header = {0};
    // File ends at byte 24 of a 48-byte AssetRecord.
    ck_assert_int_eq(bun_open(fixture("invalid/26-trunc-asset-record.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
} END_TEST




/***********************************************************************************************************/
/***********************************************************************************************************/

START_TEST(test_bad_overlapping_with_nonprintable) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    bun_open(fixture("invalid/10-overlapping-with-nonprintable.bun"), &ctx);

    bun_parse_header(&ctx, &header);

    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
}
END_TEST

START_TEST(test_bad_second_asset_empty_name) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    bun_open(fixture("invalid/11-second-asset-empty-name.bun"), &ctx);

    bun_parse_header(&ctx, &header);

    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
}
END_TEST

START_TEST(test_bad_asset_name_oob) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    bun_open(fixture("invalid/12-asset-name-oob.bun"), &ctx);

    bun_parse_header(&ctx, &header);

    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
}
END_TEST

START_TEST(test_bad_asset_empty_name) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    bun_open(fixture("invalid/13-asset-empty-name.bun"), &ctx);

    bun_parse_header(&ctx, &header);

    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
}
END_TEST


START_TEST(test_bad_string_no_null_terminator) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    // The file contains a string "DANGER" with length 6, 
    // but no null terminator follows it in the string table.
    ck_assert_int_eq(bun_open(fixture("invalid/27-non-null-terminated.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    
    // This should fail because the parser should validate that 
    // names are valid C strings.
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_MALFORMED);

    bun_close(&ctx);
} END_TEST


/************************************************************************/
/************************************************************************/

START_TEST(test_bad_rle_zero_count) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    ck_assert_int_eq(bun_open(fixture("invalid/14-rle-zero-count.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
} END_TEST


START_TEST(test_bad_rle_bomb) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    ck_assert_int_eq(bun_open(fixture("invalid/15-rle-bomb.bun"), &ctx), BUN_OK);

    /* If header is malformed, that's still a valid rejection */
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_MALFORMED);

    bun_close(&ctx);
}
END_TEST

START_TEST(test_bad_rle_truncated) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    bun_open(fixture("invalid/16-rle-truncated.bun"), &ctx);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
}
END_TEST
/**************************************************************************************/
/**************************************************************************************/

START_TEST(test_state_parse_assets_before_header) {
    BunParseContext ctx = {0};
    BunHeader header = {0};
    
    bun_open(fixture("valid/01-basic.bun"), &ctx);
    
    // ERROR: Attempting to parse assets before the header has been processed.
    // The context won't know where the asset table is yet.
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_STATE_ERROR);
    
    bun_close(&ctx);
} END_TEST

START_TEST(test_state_double_parse_header) {
    BunParseContext ctx = {0};
    BunHeader header = {0};
    
    bun_open(fixture("valid/01-basic.bun"), &ctx);
    bun_parse_header(&ctx, &header);
    
    // ERROR: Calling parse_header twice on the same context should either
    // be ignored or return a state error to prevent redundant processing.
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_STATE_ERROR);
    
    bun_close(&ctx);
} END_TEST

START_TEST(test_lifecycle_double_close) {
    BunParseContext ctx = {0};
    
    bun_open(fixture("valid/01-basic.bun"), &ctx);
    bun_close(&ctx);
    
    // SECURITY: A second close should not crash the program (Double Free).
    // Your bun_close should set pointers to NULL after freeing.
    bun_close(&ctx); 
} END_TEST

START_TEST(test_lifecycle_close_without_open) {
    BunParseContext ctx = {0}; // Zero-initialized
    
    // A robust API allows closing a context that was never opened 
    // without causing a segfault.
    bun_close(&ctx);
} END_TEST



// Assemble a test suite from our tests
static Suite *bun_suite(void) {
    Suite *s = suite_create("\n\nBUN-Parser-Security-Gauntlet");

    /* 1. POSITIVE TESTS: Files that should work perfectly */
    TCase *tc_valid = tcase_create("\n\nValid-Files");
    tcase_add_test(tc_valid, test_valid_minimal); 
    tcase_add_test(tc_valid, test_valid_alt_minimal);
    tcase_add_test(tc_valid, test_valid_one_asset);
    tcase_add_test(tc_valid, test_valid_binar_asset);
    tcase_add_test(tc_valid, test_valid_multi_asset_stack);
    tcase_add_test(tc_valid, test_valid_rle);
    tcase_add_test(tc_valid, test_valid_zero_assets);          
    tcase_add_test(tc_valid, test_valid_unordered_layout);     
    tcase_add_test(tc_valid, test_valid_stress_complex);       
    suite_add_tcase(s, tc_valid);

    /* 2. STRUCTURAL CHECKS: Magic numbers, versions, and alignment */
    TCase *tc_structure = tcase_create("\n\nStructure-and-Alignment");
    tcase_add_test(tc_structure, test_bad_magic);
    tcase_add_test(tc_structure, test_bad_version);
    tcase_add_test(tc_structure, test_bad_offset_alignment);
    tcase_add_test(tc_structure, test_bad_misaligned_section_size);
    tcase_add_test(tc_structure, test_bad_header_logic_contradiction); 
    tcase_add_test(tc_structure, test_bad_asset_count_mismatch);       
    suite_add_tcase(s, tc_structure);

    /* 3. BOUNDARY & SECURITY: Overlaps and EOF violations */
    TCase *tc_security = tcase_create("\n\nMemory-Boundary-Safety");
    tcase_add_test(tc_security, test_bad_section_past_eof);
    tcase_add_test(tc_security, test_bad_overlapping_sections);
    tcase_add_test(tc_security, test_bad_truncated_file);
    tcase_add_test(tc_security, test_bad_asset_count_max);             
    tcase_add_test(tc_security, test_bad_size_overflow);                
    tcase_add_test(tc_security, test_bad_offset_just_past_eof);         
    tcase_add_test(tc_security, test_valid_offset_at_end);              
    tcase_add_test(tc_security, test_bad_duplicate_data_offsets);       
    tcase_add_test(tc_security, test_bad_truncated_mid_string_table);   
    tcase_add_test(tc_security, test_bad_truncated_mid_asset_table);    
    suite_add_tcase(s, tc_security);

    /* 4. STRING TABLE: Name validation and pointer logic */
    TCase *tc_strings = tcase_create("\n\nString-Table-Logic");
    tcase_add_test(tc_strings, test_bad_asset_name_past_string_table);
    tcase_add_test(tc_strings, test_bad_asset_name_nonprintable);
    tcase_add_test(tc_strings, test_bad_overlapping_with_nonprintable);
    tcase_add_test(tc_strings, test_bad_second_asset_empty_name);
    tcase_add_test(tc_strings, test_bad_asset_name_oob);
    tcase_add_test(tc_strings, test_bad_asset_empty_name);
    tcase_add_test(tc_strings, test_bad_string_no_null_terminator);     
    suite_add_tcase(s, tc_strings);

    /* 5. COMPRESSION: RLE specifics and decompression bombs */
    TCase *tc_compression = tcase_create("\n\nCompression-Hardening");
    tcase_add_test(tc_compression, test_bad_rle_zero_count);
    tcase_add_test(tc_compression, test_bad_rle_bomb);
    tcase_add_test(tc_compression, test_bad_rle_truncated);
    suite_add_tcase(s, tc_compression);

    /* 6.  LIFECYCLE: API state safety and cleanup */
    TCase *tc_lifecycle = tcase_create("\n\nAPI-Lifecycle-Safety");
    tcase_add_test(tc_lifecycle, test_state_parse_assets_before_header); 
    tcase_add_test(tc_lifecycle, test_state_double_parse_header);       
    tcase_add_test(tc_lifecycle, test_lifecycle_double_close);          
    tcase_add_test(tc_lifecycle, test_lifecycle_close_without_open);     
    suite_add_tcase(s, tc_lifecycle);
    

    return s;
}

int main(void) {
    Suite   *s  = bun_suite();
    SRunner *sr = srunner_create(s);

    // Set to CK_VERBOSE to see categories passing/failing
    srunner_run_all(sr, CK_VERBOSE);
    
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}