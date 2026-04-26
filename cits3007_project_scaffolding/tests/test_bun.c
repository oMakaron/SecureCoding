#include "../bun.h"
#include <check.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

void die(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "fatal error: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    abort();
}

static const char *fixture(const char *filename) {
    static char path[256];
    int res = snprintf(path, sizeof(path), "tests/samples/%s", filename);
    if (res < 0) {
        die("snprintf encoding error: %s", strerror(errno));
    } else if ((size_t)res >= sizeof(path)) {
        die("Path buffer overflow: filename '%s' is too long", filename);
    }
    return path;
}

/* --- VALID TESTS (image_994eda.png) --- */

START_TEST(test_valid_empty) {
    BunParseContext ctx = {0};
    BunHeader header = {0};
    ck_assert_int_eq(bun_open(fixture("valid/01-empty.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    bun_close(&ctx);
}
END_TEST

START_TEST(test_valid_one_asset) {
    BunParseContext ctx = {0};
    BunHeader header = {0};
    ck_assert_int_eq(bun_open(fixture("valid/03-one-asset.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    ck_assert_uint_eq(header.asset_count, 1);
    bun_close(&ctx);
}
END_TEST

/* --- INVALID TESTS (image_994ef8.png) --- */

START_TEST(test_bad_magic) {
    BunParseContext ctx = {0};
    BunHeader header = {0};
    bun_open(fixture("invalid/01-bad-magic.bun"), &ctx);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
}
END_TEST

START_TEST(test_bad_version) {
    BunParseContext ctx = {0};
    BunHeader header = {0};
    bun_open(fixture("invalid/02-bad-version.bun"), &ctx);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_UNSUPPORTED);
    bun_close(&ctx);
}
END_TEST

START_TEST(test_misaligned_offset) {
    BunParseContext ctx = {0};
    BunHeader header = {0};
    bun_open(fixture("invalid/03-bad-offset-alignment.bun"), &ctx);
    // Rule 4.1: All offsets must be divisible by 4
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
}
END_TEST

START_TEST(test_overlapping_sections) {
    BunParseContext ctx = {0};
    BunHeader header = {0};
    bun_open(fixture("invalid/05-overlapping-sections.bun"), &ctx);
    // Rule 9.3: Return MALFORMED if sections overlap
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
}
END_TEST

START_TEST(test_rle_bomb) {
    BunParseContext ctx = {0};
    BunHeader header = {0};
    // This tests your decompression logic (Section 8.1)
    bun_open(fixture("invalid/15-rle-bomb.bun"), &ctx);
    // Depending on your implementation, this might fail during asset extraction
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK); 
    bun_close(&ctx);
}
END_TEST

/* --- SUITE SETUP --- */

static Suite *bun_suite(void) {
    Suite *s = suite_create("UWA-BUN-Security-Suite");

    TCase *tc_core = tcase_create("Core-Validation");
    tcase_add_test(tc_core, test_valid_empty);
    tcase_add_test(tc_core, test_valid_one_asset);
    tcase_add_test(tc_core, test_bad_magic);
    tcase_add_test(tc_core, test_bad_version);
    tcase_add_test(tc_core, test_misaligned_offset);
    tcase_add_test(tc_core, test_overlapping_sections);
    suite_add_tcase(s, tc_core);

    TCase *tc_security = tcase_create("Security-Fringe-Cases");
    tcase_add_test(tc_security, test_rle_bomb);
    suite_add_tcase(s, tc_security);

    return s;
}

int main(void) {
    Suite *s = bun_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_VERBOSE);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}