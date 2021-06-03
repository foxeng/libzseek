#include <stdlib.h>

#include <check.h>

#include "../src/cache.h"

// TODO OPT: Mock malloc() / free()

START_TEST(test_cache_new_null)
{
    zseek_cache_t *cache = zseek_cache_new(0);
    ck_assert_ptr_null(cache);
}
END_TEST

START_TEST(test_cache_new)
{
    zseek_cache_t *cache = zseek_cache_new(3);
    ck_assert_ptr_nonnull(cache);

    zseek_cache_free(cache);
}
END_TEST

START_TEST(test_cache_insert_null)
{
    zseek_frame_t frame = {NULL, 0, 0};
    ck_assert(!zseek_cache_insert(NULL, frame));
}
END_TEST

START_TEST(test_cache_insert)
{
    zseek_cache_t *cache = zseek_cache_new(2);
    ck_assert_msg(cache != NULL, "failed to create cache");
    zseek_frame_t frame = {.idx = 1, .len = 512};
    frame.data = malloc(frame.len);
    ck_assert_msg(frame.data != NULL, "failed to create frame %zu", frame.idx);

    ck_assert(zseek_cache_insert(cache, frame));

    zseek_cache_free(cache);
}
END_TEST

START_TEST(test_cache_free_null)
{
    zseek_cache_free(NULL);
}
END_TEST

START_TEST(test_cache_free)
{
    zseek_cache_t *cache = zseek_cache_new(4);
    ck_assert_msg(cache != NULL, "failed to create cache");
    zseek_frame_t frame = {.idx = 1, .len = 512};
    frame.data = malloc(frame.len);
    ck_assert_msg(frame.data != NULL, "failed to create frame %zu", frame.idx);
    ck_assert_msg(zseek_cache_insert(cache, frame),
        "failed to insert frame %zu", frame.idx);
    frame = (zseek_frame_t){.idx = 2, .len = 1024};
    frame.data = malloc(frame.len);
    ck_assert_msg(frame.data != NULL, "failed to create frame %zu", frame.idx);
    ck_assert_msg(zseek_cache_insert(cache, frame),
        "failed to insert frame %zu", frame.idx);
    frame = (zseek_frame_t){.idx = 3, .len = 2048};
    frame.data = malloc(frame.len);
    ck_assert_msg(frame.data != NULL, "failed to create frame %zu", frame.idx);
    ck_assert_msg(zseek_cache_insert(cache, frame),
        "failed to insert frame %zu", frame.idx);

    zseek_cache_free(cache);
}
END_TEST

START_TEST(test_cache_find_null)
{
    zseek_frame_t frame = zseek_cache_find(NULL, 0);
    ck_assert_ptr_null(frame.data);
}
END_TEST

START_TEST(test_cache_find_empty)
{
    zseek_cache_t *cache = zseek_cache_new(1);
    ck_assert_msg(cache != NULL, "failed to create cache");

    zseek_frame_t found = zseek_cache_find(cache, 1);
    ck_assert_ptr_null(found.data);

    zseek_cache_free(cache);
}
END_TEST

START_TEST(test_cache_find_present)
{
    zseek_cache_t *cache = zseek_cache_new(2);
    ck_assert_msg(cache != NULL, "failed to create cache");
    zseek_frame_t frame = {.idx = 1, .len = 512};
    frame.data = malloc(frame.len);
    ck_assert_msg(frame.data != NULL, "failed to create frame");
    ck_assert_msg(zseek_cache_insert(cache, frame), "failed to insert frame");

    zseek_frame_t found = zseek_cache_find(cache, 1);
    ck_assert_ptr_eq(found.data, frame.data);
    ck_assert_uint_eq(found.idx, frame.idx);
    ck_assert_uint_eq(found.len, frame.len);

    zseek_cache_free(cache);
}
END_TEST

START_TEST(test_cache_find_absent)
{
    zseek_cache_t *cache = zseek_cache_new(2);
    ck_assert_msg(cache != NULL, "failed to create cache");
    zseek_frame_t frame = {.idx = 1, .len = 512};
    frame.data = malloc(frame.len);
    ck_assert_msg(frame.data != NULL, "failed to create frame %zu", frame.idx);
    ck_assert_msg(zseek_cache_insert(cache, frame),
        "failed to insert frame %zu", frame.idx);
    frame = (zseek_frame_t){.idx = 2, .len = 1024};
    frame.data = malloc(frame.len);
    ck_assert_msg(frame.data != NULL, "failed to create frame %zu", frame.idx);
    ck_assert_msg(zseek_cache_insert(cache, frame),
        "failed to insert frame %zu", frame.idx);

    zseek_frame_t found = zseek_cache_find(cache, 3);
    ck_assert_ptr_null(found.data);

    zseek_cache_free(cache);
}
END_TEST

START_TEST(test_cache_replace)
{
    zseek_cache_t *cache = zseek_cache_new(3);
    ck_assert_msg(cache != NULL, "failed to create cache");
    zseek_frame_t frames[4];
    for (int i = 0; i < 4; i++) {
        frames[i] = (zseek_frame_t){.idx = i, .len = 1024};
        frames[i].data = malloc(frames[i].len);
        ck_assert_msg(frames[i].data != NULL, "failed to create frame %zu", i);
        ck_assert_msg(zseek_cache_insert(cache, frames[i]),
            "failed to insert frame %zu", i);
    }

    zseek_frame_t found = zseek_cache_find(cache, 0);
    ck_assert_ptr_null(found.data);
    for (int i = 1; i < 4; i++) {
        found = zseek_cache_find(cache, i);
        ck_assert_ptr_eq(found.data, frames[i].data);
        ck_assert_uint_eq(found.idx, frames[i].idx);
        ck_assert_uint_eq(found.len, frames[i].len);
    }

    zseek_cache_free(cache);
}

Suite *cache_suite(void)
{
    Suite *s = suite_create("cache");
    TCase *tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_cache_new_null);
    tcase_add_test(tc_core, test_cache_new);
    tcase_add_test(tc_core, test_cache_insert_null);
    tcase_add_test(tc_core, test_cache_insert);
    tcase_add_test(tc_core, test_cache_free_null);
    tcase_add_test(tc_core, test_cache_free);
    tcase_add_test(tc_core, test_cache_find_null);
    tcase_add_test(tc_core, test_cache_find_empty);
    tcase_add_test(tc_core, test_cache_find_present);
    tcase_add_test(tc_core, test_cache_find_absent);
    tcase_add_test(tc_core, test_cache_replace);

    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    Suite *s = cache_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    int number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
