#include <stdlib.h>

#include <check.h>

#include "../src/buffer.h"

// TODO OPT: Mock malloc() / free()

START_TEST(test_buffer_new)
{
    size_t capacity = 5;
    zseek_buffer_t *buffer = zseek_buffer_new(capacity);
    ck_assert(buffer != NULL);
    ck_assert(zseek_buffer_capacity(buffer) >= capacity);
    ck_assert(zseek_buffer_size(buffer) == 0);

    zseek_buffer_free(buffer);
}
END_TEST

START_TEST(test_buffer_push_null)
{
    ck_assert(!zseek_buffer_push(NULL, NULL, 0));
}
END_TEST

START_TEST(test_buffer_push)
{
    zseek_buffer_t *buffer = zseek_buffer_new(0);
    ck_assert_msg(buffer != NULL, "failed to create buffer");

    uint8_t data[] = {0, 1, 2, 3, 4};
    ck_assert(zseek_buffer_push(buffer, data, sizeof(data)));
    ck_assert(zseek_buffer_size(buffer) == sizeof(data) / sizeof(data[0]));

    zseek_buffer_free(buffer);
}
END_TEST

START_TEST(test_buffer_free_null)
{
    zseek_buffer_free(NULL);
}
END_TEST

START_TEST(test_buffer_free)
{
    zseek_buffer_t *buffer = zseek_buffer_new(0);
    ck_assert_msg(buffer != NULL, "failed to create buffer");
    uint8_t data[] = {0, 1, 2, 3, 4};
    ck_assert_msg(zseek_buffer_push(buffer, data, sizeof(data)),
        "failed to push data");

    zseek_buffer_free(buffer);
}
END_TEST

START_TEST(test_buffer_size_null)
{
    ck_assert(zseek_buffer_size(NULL) == 0);
}
END_TEST

START_TEST(test_buffer_size)
{
    zseek_buffer_t *buffer = zseek_buffer_new(0);
    ck_assert_msg(buffer != NULL, "failed to create buffer");
    uint8_t data[] = {0, 1, 2, 3, 4};
    ck_assert_msg(zseek_buffer_push(buffer, data, sizeof(data)),
        "failed to push data");

    ck_assert(zseek_buffer_size(buffer) == sizeof(data));

    zseek_buffer_free(buffer);
}
END_TEST

START_TEST(test_buffer_capacity_null)
{
    ck_assert(zseek_buffer_capacity(NULL) == 0);
}
END_TEST

START_TEST(test_buffer_capacity)
{
    size_t capacity = 4;
    zseek_buffer_t *buffer = zseek_buffer_new(capacity);
    ck_assert_msg(buffer != NULL, "failed to create buffer");

    ck_assert(zseek_buffer_capacity(buffer) >= capacity);

    zseek_buffer_free(buffer);
}
END_TEST

START_TEST(test_buffer_data_null)
{
    ck_assert(zseek_buffer_data(NULL) == NULL);
}
END_TEST

START_TEST(test_buffer_data)
{
    zseek_buffer_t *buffer = zseek_buffer_new(0);
    ck_assert_msg(buffer != NULL, "failed to create buffer");
    uint8_t data[] = {0, 1, 2, 3, 4};
    ck_assert_msg(zseek_buffer_push(buffer, data, sizeof(data)),
        "failed to push data");

    ck_assert(zseek_buffer_data(buffer) != NULL);

    zseek_buffer_free(buffer);
}
END_TEST

START_TEST(test_buffer_reserve_null)
{
    ck_assert(!zseek_buffer_reserve(NULL, 0));
}
END_TEST

START_TEST(test_buffer_reserve)
{
    zseek_buffer_t *buffer = zseek_buffer_new(0);
    ck_assert_msg(buffer != NULL, "failed to create buffer");

    size_t capacity = 6;
    ck_assert(zseek_buffer_reserve(buffer, capacity));
    ck_assert(zseek_buffer_capacity(buffer) >= capacity);

    zseek_buffer_free(buffer);
}
END_TEST

START_TEST(test_buffer_resize_null)
{
    ck_assert(!zseek_buffer_resize(NULL, 0));
}
END_TEST

START_TEST(test_buffer_resize)
{
    zseek_buffer_t *buffer = zseek_buffer_new(0);
    ck_assert_msg(buffer != NULL, "failed to create buffer");
    uint8_t data[] = {0, 1, 2, 3, 4};
    ck_assert_msg(zseek_buffer_push(buffer, data, sizeof(data)),
        "failed to push data");

    size_t size = 10;
    ck_assert(zseek_buffer_resize(buffer, size));
    ck_assert(zseek_buffer_size(buffer) == size);

    zseek_buffer_free(buffer);
}
END_TEST

START_TEST(test_buffer_reset_null)
{
    zseek_buffer_reset(NULL);
}
END_TEST

START_TEST(test_buffer_reset)
{
    zseek_buffer_t *buffer = zseek_buffer_new(0);
    ck_assert_msg(buffer != NULL, "failed to create buffer");
    uint8_t data[] = {0, 1, 2, 3, 4};
    ck_assert_msg(zseek_buffer_push(buffer, data, sizeof(data)),
        "failed to push data");

    zseek_buffer_reset(buffer);
    ck_assert(zseek_buffer_size(buffer) == 0);

    zseek_buffer_free(buffer);
}
END_TEST

Suite *buffer_suite(void)
{
    Suite *s = suite_create("buffer");
    TCase *tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_buffer_new);
    tcase_add_test(tc_core, test_buffer_push_null);
    tcase_add_test(tc_core, test_buffer_push);
    tcase_add_test(tc_core, test_buffer_free_null);
    tcase_add_test(tc_core, test_buffer_free);
    tcase_add_test(tc_core, test_buffer_size_null);
    tcase_add_test(tc_core, test_buffer_size);
    tcase_add_test(tc_core, test_buffer_capacity_null);
    tcase_add_test(tc_core, test_buffer_capacity);
    tcase_add_test(tc_core, test_buffer_data_null);
    tcase_add_test(tc_core, test_buffer_data);
    tcase_add_test(tc_core, test_buffer_reserve_null);
    tcase_add_test(tc_core, test_buffer_reserve);
    tcase_add_test(tc_core, test_buffer_resize_null);
    tcase_add_test(tc_core, test_buffer_resize);
    tcase_add_test(tc_core, test_buffer_reset_null);
    tcase_add_test(tc_core, test_buffer_reset);

    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    Suite *s = buffer_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    int number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
