#pragma once

#include <stdio.h>
#include <string.h>

/* Khung assert tối giản cho test chạy trên host. */

extern int g_pass, g_fail;
extern const char *g_case;

#define TEST(name)                                                            \
    do {                                                                      \
        g_case = (name);                                                      \
        printf("\n[ %s ]\n", g_case);                                         \
    } while (0)

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        if (cond) {                                                           \
            g_pass++;                                                         \
        } else {                                                              \
            g_fail++;                                                         \
            printf("  FAIL (%s:%d) ", __FILE__, __LINE__);                    \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

#define CHECK_EQ(a, b, ...)                                                   \
    do {                                                                      \
        long _a = (long)(a), _b = (long)(b);                                  \
        if (_a == _b) {                                                       \
            g_pass++;                                                         \
        } else {                                                              \
            g_fail++;                                                         \
            printf("  FAIL (%s:%d) ", __FILE__, __LINE__);                    \
            printf(__VA_ARGS__);                                              \
            printf("  [got %ld, want %ld]\n", _a, _b);                        \
        }                                                                     \
    } while (0)

#define TEST_MAIN_BEGIN(suite)                                                \
    int g_pass = 0, g_fail = 0;                                               \
    const char *g_case = "";                                                  \
    int main(void)                                                            \
    {                                                                         \
        const char *_suite = (suite);                                         \
        printf("=== %s ===\n", _suite);

#define TEST_MAIN_END()                                                       \
        printf("\n--- %s: %d passed, %d failed ---\n", _suite, g_pass, g_fail); \
        return g_fail == 0 ? 0 : 1;                                           \
    }
