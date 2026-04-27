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

START_TEST(test_bad_misaligned_section_size) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    bun_open(fixture("invalid/09-misaligned-section-size.bun"), &ctx);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
}
END_TEST

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

/*
add
  1. Integer Boundary & Extremes 
    tcase_add_test(tc_security, test_bad_asset_count_max);
    tcase_add_test(tc_security, test_bad_size_overflow);

      2. Offset Edge & Alignment 
    tcase_add_test(tc_security, test_bad_offset_just_past_eof);
    tcase_add_test(tc_security, test_valid_offset_at_end);

    3. Empty vs. Minimal Structures 
    tcase_add_test(tc_valid, test_valid_zero_assets);

      4. String Table Edges 
    tcase_add_test(tc_strings, test_bad_string_no_null_terminator);

      5. Duplicate / Aliasing Cases 
    tcase_add_test(tc_security, test_bad_duplicate_data_offsets);

      6. Section Ordering Assumptions 
    tcase_add_test(tc_valid, test_valid_unordered_layout);

      7. Repeated Parsing / State Safety 
    tcase_add_test(tc_lifecycle, test_state_parse_assets_before_header);
    tcase_add_test(tc_lifecycle, test_state_double_parse_header);

    8. Lifecycle Misuse 
    tcase_add_test(tc_lifecycle, test_lifecycle_double_close);
    tcase_add_test(tc_lifecycle, test_lifecycle_close_without_open);

    9. Partial / Interrupted Structures 
    tcase_add_test(tc_security, test_bad_truncated_mid_string_table);
    tcase_add_test(tc_security, test_bad_truncated_mid_asset_table);

      10. Cross-field Consistency 
    tcase_add_test(tc_structure, test_bad_header_logic_contradiction);
    tcase_add_test(tc_structure, test_bad_asset_count_mismatch);

      11. Extreme Valid (Stress Test) 
    tcase_add_test(tc_valid, test_valid_stress_complex);
*/

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
    suite_add_tcase(s, tc_valid);

    /* 2. STRUCTURAL CHECKS: Magic numbers, versions, and alignment */
    TCase *tc_structure = tcase_create("\n\nStructure-and-Alignment");
    tcase_add_test(tc_structure, test_bad_magic);
    tcase_add_test(tc_structure, test_bad_version);
    tcase_add_test(tc_structure, test_bad_offset_alignment);
    tcase_add_test(tc_structure, test_bad_misaligned_section_size);
    suite_add_tcase(s, tc_structure);

    /* 3. BOUNDARY & SECURITY: Overlaps and EOF violations */
    TCase *tc_security = tcase_create("\n\nMemory-Boundary-Safety");
    tcase_add_test(tc_security, test_bad_section_past_eof);
    tcase_add_test(tc_security, test_bad_overlapping_sections);
    tcase_add_test(tc_security, test_bad_truncated_file);
    suite_add_tcase(s, tc_security);

    /* 4. STRING TABLE: Name validation and pointer logic */
    TCase *tc_strings = tcase_create("\n\nString-Table-Logic");
    tcase_add_test(tc_strings, test_bad_asset_name_past_string_table);
    tcase_add_test(tc_strings, test_bad_asset_name_nonprintable);
    tcase_add_test(tc_strings, test_bad_overlapping_with_nonprintable);
    tcase_add_test(tc_strings, test_bad_second_asset_empty_name);
    tcase_add_test(tc_strings, test_bad_asset_name_oob);
    tcase_add_test(tc_strings, test_bad_asset_empty_name);
    suite_add_tcase(s, tc_strings);

    /* 5. COMPRESSION: RLE specifics and decompression bombs */
    TCase *tc_compression = tcase_create("\n\nCompression-Hardening");
    tcase_add_test(tc_compression, test_bad_rle_zero_count);
    tcase_add_test(tc_compression, test_bad_rle_bomb);
    tcase_add_test(tc_compression, test_bad_rle_truncated);
    suite_add_tcase(s, tc_compression);

    

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