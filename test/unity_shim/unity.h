// Minimal Unity-compatible shim so the native test suite can build and run without network
// access to fetch the real ThrowTheSwitch/Unity library. Covers exactly the macros used by
// test/test_core/test_core.cpp. When you have network access, `pio test -e native` will use
// the real Unity instead (this shim is not picked up by PlatformIO's dependency resolution
// unless you point lib_deps at it) -- see tools/run_native_tests.sh for the offline path.
#pragma once
#include <stdio.h>
#include <string.h>

static int g_unity_tests = 0, g_unity_failures = 0;
static const char* g_unity_current = "";

#define UNITY_BEGIN() do { g_unity_tests = 0; g_unity_failures = 0; } while (0)
// Comma operator, not two statements: written as `return UNITY_END();` the old two-statement form
// put the printf after the return, so the summary line never actually appeared.
#define UNITY_END() (printf("\n%d tests, %d failed\n", g_unity_tests, g_unity_failures), \
                     g_unity_failures > 0 ? 1 : 0)

#define RUN_TEST(f) do { g_unity_tests++; g_unity_current = #f; f(); printf("  ok  %s\n", #f); } while (0)

#define UNITY_FAIL(msg) do { \
  g_unity_failures++; \
  printf("  FAIL %s: %s (%s:%d)\n", g_unity_current, msg, __FILE__, __LINE__); \
} while (0)

#define TEST_ASSERT_TRUE(c)  do { if (!(c)) UNITY_FAIL(#c " is not true"); } while (0)
#define TEST_ASSERT_FALSE(c) do { if (c) UNITY_FAIL(#c " is not false"); } while (0)
#define TEST_ASSERT_NULL(p)     do { if ((p) != NULL) UNITY_FAIL(#p " is not NULL"); } while (0)
#define TEST_ASSERT_NOT_NULL(p) do { if ((p) == NULL) UNITY_FAIL(#p " is NULL"); } while (0)

#define TEST_ASSERT_EQUAL_INT(e, a)    do { if ((long)(e) != (long)(a)) UNITY_FAIL(#a " != " #e); } while (0)
#define TEST_ASSERT_EQUAL_UINT8(e, a)  TEST_ASSERT_EQUAL_INT(e, a)
#define TEST_ASSERT_EQUAL_UINT16(e, a) TEST_ASSERT_EQUAL_INT(e, a)
#define TEST_ASSERT_EQUAL_HEX8(e, a)   TEST_ASSERT_EQUAL_INT(e, a)
#define TEST_ASSERT_EQUAL_HEX16(e, a)  TEST_ASSERT_EQUAL_INT(e, a)
#define TEST_ASSERT_EQUAL_HEX32(e, a)  do { if ((unsigned long)(e) != (unsigned long)(a)) UNITY_FAIL(#a " != " #e); } while (0)
#define TEST_ASSERT_NOT_EQUAL(e, a)    do { if ((long)(e) == (long)(a)) UNITY_FAIL(#a " should differ from " #e); } while (0)
#define TEST_ASSERT_EQUAL_STRING(e, a) do { if (strcmp((e), (a)) != 0) UNITY_FAIL("string mismatch: \"" #a "\" != \"" #e "\""); } while (0)
#define TEST_ASSERT_EQUAL_INT16(e, a)  TEST_ASSERT_EQUAL_INT(e, a)
#define TEST_ASSERT_EQUAL_INT32(e, a)  TEST_ASSERT_EQUAL_INT(e, a)
#define TEST_ASSERT_EQUAL_UINT32(e, a) do { if ((unsigned long)(e) != (unsigned long)(a)) UNITY_FAIL(#a " != " #e); } while (0)
#define TEST_ASSERT_GREATER_THAN_INT(t, a) do { if (!((long)(a) > (long)(t))) UNITY_FAIL(#a " is not greater than " #t); } while (0)
#define TEST_ASSERT_LESS_OR_EQUAL_INT(t, a) do { if (!((long)(a) <= (long)(t))) UNITY_FAIL(#a " is greater than " #t); } while (0)
