/**
 * Tiny host-side test harness for the C firmware modules.
 *
 * Each `test_<module>.c` is a standalone executable that includes this
 * header, defines test functions, and calls RUN(...) for each then
 * TEST_SUMMARY_AND_EXIT() at the end of main().
 *
 * No external deps. Fits-in-your-head.
 */
#pragma once

#include <stdio.h>
#include <stdint.h>

/* Module-private counters — each test_*.c gets its own copy via static storage. */
static int _t_assertions          = 0;
static int _t_failures            = 0;
static int _t_test_fails_at_start = 0;

#define ASSERT_TRUE(cond)                                                          \
    do {                                                                           \
        _t_assertions++;                                                           \
        if (!(cond)) {                                                             \
            fprintf(stderr, "  FAIL @ %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            _t_failures++;                                                         \
        }                                                                          \
    } while (0)

#define ASSERT_EQ(actual, expected)                                                \
    do {                                                                           \
        _t_assertions++;                                                           \
        long long _a = (long long)(actual);                                        \
        long long _e = (long long)(expected);                                      \
        if (_a != _e) {                                                            \
            fprintf(stderr, "  FAIL @ %s:%d: expected 0x%llx, got 0x%llx\n",       \
                    __FILE__, __LINE__, _e, _a);                                   \
            _t_failures++;                                                         \
        }                                                                          \
    } while (0)

#define RUN(testfn)                                                                \
    do {                                                                           \
        printf("%s\n", #testfn);                                                   \
        _t_test_fails_at_start = _t_failures;                                      \
        testfn();                                                                  \
        if (_t_failures == _t_test_fails_at_start) printf("  ok\n");               \
    } while (0)

#define TEST_SUMMARY_AND_EXIT()                                                    \
    do {                                                                           \
        printf("\n%d assertions, %d failures\n", _t_assertions, _t_failures);      \
        return _t_failures > 0 ? 1 : 0;                                            \
    } while (0)
