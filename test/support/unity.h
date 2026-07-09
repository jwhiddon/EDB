#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

extern int unity_tests;
extern int unity_failures;

#define TEST_ASSERT_EQUAL_INT(expected, actual) \
    do { \
        int _expected = (expected); \
        int _actual = (actual); \
        if (_expected != _actual) { \
            printf("FAIL %s:%d expected %d got %d\n", __FILE__, __LINE__, _expected, _actual); \
            unity_failures++; \
        } \
    } while (0)

#define TEST_ASSERT_EQUAL_UINT32(expected, actual) \
    do { \
        unsigned long _expected = (expected); \
        unsigned long _actual = (actual); \
        if (_expected != _actual) { \
            printf("FAIL %s:%d expected %lu got %lu\n", __FILE__, __LINE__, _expected, _actual); \
            unity_failures++; \
        } \
    } while (0)

#define TEST_ASSERT_EQUAL_MEMORY(expected, actual, length) \
    do { \
        const void* _expected = (expected); \
        const void* _actual = (actual); \
        size_t _length = (length); \
        if (memcmp(_expected, _actual, _length) != 0) { \
            printf("FAIL %s:%d memory mismatch\n", __FILE__, __LINE__); \
            unity_failures++; \
        } \
    } while (0)

#define TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, length) \
    do { \
        const uint8_t* _expected = (const uint8_t*)(expected); \
        const uint8_t* _actual = (const uint8_t*)(actual); \
        size_t _length = (size_t)(length); \
        for (size_t _i = 0; _i < _length; _i++) { \
            if (_expected[_i] != _actual[_i]) { \
                printf("FAIL %s:%d uint8 mismatch at %u expected %u got %u\n", \
                    __FILE__, __LINE__, (unsigned)_i, _expected[_i], _actual[_i]); \
                unity_failures++; \
                break; \
            } \
        } \
    } while (0)

#define TEST_ASSERT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            printf("FAIL %s:%d condition false\n", __FILE__, __LINE__); \
            unity_failures++; \
        } \
    } while (0)

#define RUN_TEST(func) \
    do { \
        unity_tests++; \
        printf("RUN  %s\n", #func); \
        fflush(stdout); \
        func(); \
    } while (0)

#define UNITY_BEGIN() unity_failures = 0
#define UNITY_END() unity_failures
