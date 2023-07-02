#include <stdlib.h>

#include <check.h>

#include "../src/buffer_pool.h"

// TODO OPT: Mock malloc() / free()

START_TEST(test_buffer_pool_new)
{
    size_t capacity = 2;
    zseek_buffer_pool_t *buffer_pool = zseek_buffer_pool_new(capacity);
    ck_assert(buffer_pool != NULL);
    zseek_buffer_pool_free(buffer_pool);
}
END_TEST

START_TEST(test_buffer_pool_get_null)
{
    ck_assert(zseek_buffer_pool_get(NULL, 0) == NULL);
}
END_TEST

START_TEST(test_buffer_pool_get)
{
    size_t capacity = 2;
    zseek_buffer_pool_t *buffer_pool = zseek_buffer_pool_new(capacity);
    ck_assert_msg(buffer_pool != NULL, "failed to create buffer pool");

    zseek_buffer_t *buffer = zseek_buffer_pool_get(buffer_pool, 10);
    ck_assert(buffer != NULL);
    zseek_buffer_pool_ret(buffer_pool, buffer);
    buffer = zseek_buffer_pool_get(buffer_pool, 10);
    ck_assert(buffer != NULL);
    zseek_buffer_pool_ret(buffer_pool, buffer);

    zseek_buffer_pool_free(buffer_pool);
}
END_TEST

START_TEST(test_buffer_pool_free_null)
{
    zseek_buffer_pool_free(NULL);
}
END_TEST

START_TEST(test_buffer_pool_free)
{
    size_t capacity = 2;
    zseek_buffer_pool_t *buffer_pool = zseek_buffer_pool_new(capacity);
    ck_assert_msg(buffer_pool != NULL, "failed to create buffer pool");

    zseek_buffer_t *buffer = zseek_buffer_pool_get(buffer_pool, 10);
    ck_assert_msg(buffer != NULL, "failed to get buffer");
    zseek_buffer_pool_ret(buffer_pool, buffer);

    zseek_buffer_pool_free(buffer_pool);
}
END_TEST

START_TEST(test_buffer_pool_ret_null)
{
    zseek_buffer_pool_ret(NULL, NULL);
}
END_TEST

START_TEST(test_buffer_pool_ret)
{
    ck_assert(zseek_buffer_pool_get(NULL, 0) == NULL);
}
END_TEST


Suite *buffer_pool_suite(void)
{
    Suite *s = suite_create("buffer_pool");
    TCase *tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_buffer_pool_new);
    tcase_add_test(tc_core, test_buffer_pool_get_null);
    tcase_add_test(tc_core, test_buffer_pool_get);
    tcase_add_test(tc_core, test_buffer_pool_free_null);
    tcase_add_test(tc_core, test_buffer_pool_free);
    tcase_add_test(tc_core, test_buffer_pool_ret_null);
    tcase_add_test(tc_core, test_buffer_pool_ret);

    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    Suite *s = buffer_pool_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    int number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
