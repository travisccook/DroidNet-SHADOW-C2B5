// The AVR assembly block loops (sm_crc.cpp) against the C byte-at-a-time updates: every length
// 0-255, from 16 starting states and 4 data patterns, and where the tables they read sit in flash.
// AVR only (simavr); the host builds the C loops.
#include <unity.h>

#include <ShadowMap.h>
#include <sm_test_main.h>
#include <stdio.h>

#if defined(__AVR__)

namespace {

uint8_t gData[256];

uint32_t gRng = 0x9E3779B9UL;
uint32_t rnd() {
    gRng ^= gRng << 13;
    gRng ^= gRng >> 17;
    gRng ^= gRng << 5;
    return gRng;
}

void fill(uint8_t pattern) {
    for (uint16_t i = 0; i < sizeof(gData); ++i) {
        switch (pattern) {
            case 0: gData[i] = 0x00; break;
            case 1: gData[i] = 0xFF; break;
            case 2: gData[i] = static_cast<uint8_t>(i); break;
            default: gData[i] = static_cast<uint8_t>(rnd()); break;
        }
    }
}

}  // namespace

static void test_tables_are_256_aligned_in_the_first_64k() {
    // The linker's addresses (24-bit): LPM reads only the first 64 KB.
    const uint32_t a32 = pgm_get_far_address(sm::detail::kCrc32Planes);
    const uint32_t a16 = pgm_get_far_address(sm::detail::kCrc16Planes);
    char msg[48];
    snprintf(msg, sizeof(msg), "planes at %05lX and %05lX", static_cast<unsigned long>(a32),
             static_cast<unsigned long>(a16));
    TEST_MESSAGE(msg);
    TEST_ASSERT_EQUAL_UINT8(0, a32 & 0xFF);
    TEST_ASSERT_EQUAL_UINT8(0, a16 & 0xFF);
    TEST_ASSERT_TRUE(a32 + 4 * 256 <= 0x10000UL);
    TEST_ASSERT_TRUE(a16 + 2 * 256 <= 0x10000UL);
}

static void test_crc16_asm_matches_c() {
    for (uint8_t pattern = 0; pattern < 4; ++pattern) {
        fill(pattern);
        for (uint8_t s = 0; s < 16; ++s) {
            const uint16_t start = s == 0 ? sm::kCrc16Init : static_cast<uint16_t>(rnd());
            uint16_t want = start;
            for (uint16_t n = 0; n <= 255; ++n) {
                if (sm::crc16Block(start, gData, static_cast<uint8_t>(n)) != want) {
                    char msg[56];
                    snprintf(msg, sizeof(msg), "crc16 pattern %u state %04X length %u", pattern, start, n);
                    TEST_FAIL_MESSAGE(msg);
                }
                if (n < 255)
                    want = sm::crc16Update(want, gData[n]);
            }
        }
    }
}

static void test_crc32_asm_matches_c() {
    for (uint8_t pattern = 0; pattern < 4; ++pattern) {
        fill(pattern);
        for (uint8_t s = 0; s < 16; ++s) {
            const uint32_t start = s == 0 ? sm::kCrc32Init : rnd();
            uint32_t want = start;
            for (uint16_t n = 0; n <= 255; ++n) {
                if (sm::crc32Block(start, gData, static_cast<uint8_t>(n)) != want) {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "crc32 pattern %u state %08lX length %u", pattern,
                             static_cast<unsigned long>(start), n);
                    TEST_FAIL_MESSAGE(msg);
                }
                if (n < 255)
                    want = sm::crc32Update(want, gData[n]);
            }
        }
    }
    // From an odd address too (the pointer register walks bytes).
    fill(3);
    uint32_t want = sm::kCrc32Init;
    for (uint8_t i = 1; i < 100; ++i)
        want = sm::crc32Update(want, gData[i]);
    TEST_ASSERT_EQUAL_HEX32(want, sm::crc32Block(sm::kCrc32Init, gData + 1, 99));
}

#else

static void test_tables_are_256_aligned_in_the_first_64k() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_crc16_asm_matches_c() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_crc32_asm_matches_c() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }

#endif

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_tables_are_256_aligned_in_the_first_64k);
    RUN_TEST(test_crc16_asm_matches_c);
    RUN_TEST(test_crc32_asm_matches_c);
    return UNITY_END();
}
