#include <stdlib.h>

#include <check.h>

#include "../src/cache.h"
#include "../src/common.h"

// TODO OPT: Mock malloc() / free()

START_TEST(test_cache_new_null)
{
    zseek_mm_t _mm = { 0 };
    zseek_mm_t *mm = &_mm;
    init_mm(mm);

    zseek_cache_t *cache = zseek_cache_new(mm, 0);
    ck_assert(cache == NULL);
}
END_TEST

START_TEST(test_cache_new)
{
    zseek_mm_t _mm = { 0 };
    zseek_mm_t *mm = &_mm;
    init_mm(mm);

    zseek_cache_t *cache = zseek_cache_new(mm, 3);
    ck_assert(cache != NULL);

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
    zseek_mm_t _mm = { 0 };
    zseek_mm_t *mm = &_mm;
    init_mm(mm);

    zseek_cache_t *cache = zseek_cache_new(mm, 2);
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
    zseek_mm_t _mm = { 0 };
    zseek_mm_t *mm = &_mm;
    init_mm(mm);

    zseek_cache_t *cache = zseek_cache_new(mm, 4);
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
    ck_assert(frame.data == NULL);
}
END_TEST

START_TEST(test_cache_find_empty)
{
    zseek_mm_t _mm = { 0 };
    zseek_mm_t *mm = &_mm;
    init_mm(mm);

    zseek_cache_t *cache = zseek_cache_new(mm, 1);
    ck_assert_msg(cache != NULL, "failed to create cache");

    zseek_frame_t found = zseek_cache_find(cache, 1);
    ck_assert(found.data == NULL);

    zseek_cache_free(cache);
}
END_TEST

START_TEST(test_cache_find_present)
{
    zseek_mm_t _mm = { 0 };
    zseek_mm_t *mm = &_mm;
    init_mm(mm);

    zseek_cache_t *cache = zseek_cache_new(mm, 2);
    ck_assert_msg(cache != NULL, "failed to create cache");
    zseek_frame_t frame = {.idx = 1, .len = 512};
    frame.data = malloc(frame.len);
    ck_assert_msg(frame.data != NULL, "failed to create frame");
    ck_assert_msg(zseek_cache_insert(cache, frame), "failed to insert frame");

    zseek_frame_t found = zseek_cache_find(cache, 1);
    ck_assert(found.data == frame.data);
    ck_assert(found.idx == frame.idx);
    ck_assert(found.len == frame.len);

    zseek_cache_free(cache);
}
END_TEST

START_TEST(test_cache_find_absent)
{
    zseek_mm_t _mm = { 0 };
    zseek_mm_t *mm = &_mm;
    init_mm(mm);

    zseek_cache_t *cache = zseek_cache_new(mm, 2);
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
    ck_assert(found.data == NULL);

    zseek_cache_free(cache);
}
END_TEST

START_TEST(test_cache_replace)
{
    zseek_mm_t _mm = { 0 };
    zseek_mm_t *mm = &_mm;
    init_mm(mm);

    zseek_cache_t *cache = zseek_cache_new(mm, 3);
    ck_assert_msg(cache != NULL, "failed to create cache");
    zseek_frame_t frames[4];
    for (int i = 0; i < 4; i++) {
        frames[i] = (zseek_frame_t){.idx = i, .len = 1024};
        frames[i].data = malloc(frames[i].len);
        ck_assert_msg(frames[i].data != NULL, "failed to create frame %d", i);
        ck_assert_msg(zseek_cache_insert(cache, frames[i]),
            "failed to insert frame %d", i);
    }

    zseek_frame_t found = zseek_cache_find(cache, 0);
    ck_assert(found.data == NULL);
    for (int i = 1; i < 4; i++) {
        found = zseek_cache_find(cache, i);
        ck_assert(found.data == frames[i].data);
        ck_assert(found.idx == frames[i].idx);
        ck_assert(found.len == frames[i].len);
    }

    zseek_cache_free(cache);
}
END_TEST

START_TEST(test_cache_memory_usage_null)
{
    ck_assert(zseek_cache_memory_usage(NULL) == 0);
}
END_TEST

START_TEST(test_cache_memory_usage)
{
    zseek_mm_t _mm = { 0 };
    zseek_mm_t *mm = &_mm;
    init_mm(mm);

    zseek_cache_t *cache = zseek_cache_new(mm, 1);
    ck_assert_msg(cache != NULL, "failed to create cache");
    zseek_frame_t frame = {.idx = 1, .len = 512};
    frame.data = malloc(frame.len);
    ck_assert_msg(frame.data != NULL, "failed to create frame %zu", frame.idx);
    ck_assert_msg(zseek_cache_insert(cache, frame), "failed to insert frame");

    ck_assert(zseek_cache_memory_usage(cache) >= frame.len);

    zseek_cache_free(cache);
}
END_TEST

START_TEST(test_cache_entries_null)
{
    ck_assert(zseek_cache_entries(NULL) == 0);
}
END_TEST

START_TEST(test_cache_entries)
{
    zseek_mm_t _mm = { 0 };
    zseek_mm_t *mm = &_mm;
    init_mm(mm);

    zseek_cache_t *cache = zseek_cache_new(mm, 2);
    ck_assert_msg(cache != NULL, "failed to create cache");
    zseek_frame_t frame = {.idx = 1, .len = 512};
    frame.data = malloc(frame.len);
    ck_assert_msg(frame.data != NULL, "failed to create frame %zu", frame.idx);
    ck_assert_msg(zseek_cache_insert(cache, frame), "failed to insert frame");

    ck_assert(zseek_cache_entries(cache) == 1);

    zseek_cache_free(cache);
}
END_TEST

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
    tcase_add_test(tc_core, test_cache_memory_usage_null);
    tcase_add_test(tc_core, test_cache_memory_usage);
    tcase_add_test(tc_core, test_cache_entries_null);
    tcase_add_test(tc_core, test_cache_entries);

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

