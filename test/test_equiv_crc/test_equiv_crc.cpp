// The CRC code against its definitions: the byte-plane tables against the bitwise forms and the
// literal table they replaced, the block functions against byte-at-a-time updates, and the
// compile-time crc32Text/crc16Text against the runtime code. EEPROM records written by older
// firmware must still check, and every line's CRC must stay what zlib computes.
#include <unity.h>

#include <ShadowMap.h>
#include <sm_test_main.h>
#include <stdio.h>
#include <string.h>

namespace {

uint16_t crc16Bitwise(uint16_t crc, uint8_t byte) {
    crc = static_cast<uint16_t>(crc ^ (static_cast<uint16_t>(byte) << 8));
    for (uint8_t i = 0; i < 8; ++i)
        crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    return crc;
}

uint32_t crc32Bitwise(uint32_t crc, uint8_t byte) {
    crc ^= byte;
    for (uint8_t i = 0; i < 8; ++i)
        crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320UL : crc >> 1;
    return crc;
}

// The 1 KB table sm_crc.cpp used up to 0a8b73f.
const uint32_t kOldCrc32Table[256] PROGMEM = {
    0x00000000UL, 0x77073096UL, 0xEE0E612CUL, 0x990951BAUL, 0x076DC419UL, 0x706AF48FUL,
    0xE963A535UL, 0x9E6495A3UL, 0x0EDB8832UL, 0x79DCB8A4UL, 0xE0D5E91EUL, 0x97D2D988UL,
    0x09B64C2BUL, 0x7EB17CBDUL, 0xE7B82D07UL, 0x90BF1D91UL, 0x1DB71064UL, 0x6AB020F2UL,
    0xF3B97148UL, 0x84BE41DEUL, 0x1ADAD47DUL, 0x6DDDE4EBUL, 0xF4D4B551UL, 0x83D385C7UL,
    0x136C9856UL, 0x646BA8C0UL, 0xFD62F97AUL, 0x8A65C9ECUL, 0x14015C4FUL, 0x63066CD9UL,
    0xFA0F3D63UL, 0x8D080DF5UL, 0x3B6E20C8UL, 0x4C69105EUL, 0xD56041E4UL, 0xA2677172UL,
    0x3C03E4D1UL, 0x4B04D447UL, 0xD20D85FDUL, 0xA50AB56BUL, 0x35B5A8FAUL, 0x42B2986CUL,
    0xDBBBC9D6UL, 0xACBCF940UL, 0x32D86CE3UL, 0x45DF5C75UL, 0xDCD60DCFUL, 0xABD13D59UL,
    0x26D930ACUL, 0x51DE003AUL, 0xC8D75180UL, 0xBFD06116UL, 0x21B4F4B5UL, 0x56B3C423UL,
    0xCFBA9599UL, 0xB8BDA50FUL, 0x2802B89EUL, 0x5F058808UL, 0xC60CD9B2UL, 0xB10BE924UL,
    0x2F6F7C87UL, 0x58684C11UL, 0xC1611DABUL, 0xB6662D3DUL, 0x76DC4190UL, 0x01DB7106UL,
    0x98D220BCUL, 0xEFD5102AUL, 0x71B18589UL, 0x06B6B51FUL, 0x9FBFE4A5UL, 0xE8B8D433UL,
    0x7807C9A2UL, 0x0F00F934UL, 0x9609A88EUL, 0xE10E9818UL, 0x7F6A0DBBUL, 0x086D3D2DUL,
    0x91646C97UL, 0xE6635C01UL, 0x6B6B51F4UL, 0x1C6C6162UL, 0x856530D8UL, 0xF262004EUL,
    0x6C0695EDUL, 0x1B01A57BUL, 0x8208F4C1UL, 0xF50FC457UL, 0x65B0D9C6UL, 0x12B7E950UL,
    0x8BBEB8EAUL, 0xFCB9887CUL, 0x62DD1DDFUL, 0x15DA2D49UL, 0x8CD37CF3UL, 0xFBD44C65UL,
    0x4DB26158UL, 0x3AB551CEUL, 0xA3BC0074UL, 0xD4BB30E2UL, 0x4ADFA541UL, 0x3DD895D7UL,
    0xA4D1C46DUL, 0xD3D6F4FBUL, 0x4369E96AUL, 0x346ED9FCUL, 0xAD678846UL, 0xDA60B8D0UL,
    0x44042D73UL, 0x33031DE5UL, 0xAA0A4C5FUL, 0xDD0D7CC9UL, 0x5005713CUL, 0x270241AAUL,
    0xBE0B1010UL, 0xC90C2086UL, 0x5768B525UL, 0x206F85B3UL, 0xB966D409UL, 0xCE61E49FUL,
    0x5EDEF90EUL, 0x29D9C998UL, 0xB0D09822UL, 0xC7D7A8B4UL, 0x59B33D17UL, 0x2EB40D81UL,
    0xB7BD5C3BUL, 0xC0BA6CADUL, 0xEDB88320UL, 0x9ABFB3B6UL, 0x03B6E20CUL, 0x74B1D29AUL,
    0xEAD54739UL, 0x9DD277AFUL, 0x04DB2615UL, 0x73DC1683UL, 0xE3630B12UL, 0x94643B84UL,
    0x0D6D6A3EUL, 0x7A6A5AA8UL, 0xE40ECF0BUL, 0x9309FF9DUL, 0x0A00AE27UL, 0x7D079EB1UL,
    0xF00F9344UL, 0x8708A3D2UL, 0x1E01F268UL, 0x6906C2FEUL, 0xF762575DUL, 0x806567CBUL,
    0x196C3671UL, 0x6E6B06E7UL, 0xFED41B76UL, 0x89D32BE0UL, 0x10DA7A5AUL, 0x67DD4ACCUL,
    0xF9B9DF6FUL, 0x8EBEEFF9UL, 0x17B7BE43UL, 0x60B08ED5UL, 0xD6D6A3E8UL, 0xA1D1937EUL,
    0x38D8C2C4UL, 0x4FDFF252UL, 0xD1BB67F1UL, 0xA6BC5767UL, 0x3FB506DDUL, 0x48B2364BUL,
    0xD80D2BDAUL, 0xAF0A1B4CUL, 0x36034AF6UL, 0x41047A60UL, 0xDF60EFC3UL, 0xA867DF55UL,
    0x316E8EEFUL, 0x4669BE79UL, 0xCB61B38CUL, 0xBC66831AUL, 0x256FD2A0UL, 0x5268E236UL,
    0xCC0C7795UL, 0xBB0B4703UL, 0x220216B9UL, 0x5505262FUL, 0xC5BA3BBEUL, 0xB2BD0B28UL,
    0x2BB45A92UL, 0x5CB36A04UL, 0xC2D7FFA7UL, 0xB5D0CF31UL, 0x2CD99E8BUL, 0x5BDEAE1DUL,
    0x9B64C2B0UL, 0xEC63F226UL, 0x756AA39CUL, 0x026D930AUL, 0x9C0906A9UL, 0xEB0E363FUL,
    0x72076785UL, 0x05005713UL, 0x95BF4A82UL, 0xE2B87A14UL, 0x7BB12BAEUL, 0x0CB61B38UL,
    0x92D28E9BUL, 0xE5D5BE0DUL, 0x7CDCEFB7UL, 0x0BDBDF21UL, 0x86D3D2D4UL, 0xF1D4E242UL,
    0x68DDB3F8UL, 0x1FDA836EUL, 0x81BE16CDUL, 0xF6B9265BUL, 0x6FB077E1UL, 0x18B74777UL,
    0x88085AE6UL, 0xFF0F6A70UL, 0x66063BCAUL, 0x11010B5CUL, 0x8F659EFFUL, 0xF862AE69UL,
    0x616BFFD3UL, 0x166CCF45UL, 0xA00AE278UL, 0xD70DD2EEUL, 0x4E048354UL, 0x3903B3C2UL,
    0xA7672661UL, 0xD06016F7UL, 0x4969474DUL, 0x3E6E77DBUL, 0xAED16A4AUL, 0xD9D65ADCUL,
    0x40DF0B66UL, 0x37D83BF0UL, 0xA9BCAE53UL, 0xDEBB9EC5UL, 0x47B2CF7FUL, 0x30B5FFE9UL,
    0xBDBDF21CUL, 0xCABAC28AUL, 0x53B39330UL, 0x24B4A3A6UL, 0xBAD03605UL, 0xCDD70693UL,
    0x54DE5729UL, 0x23D967BFUL, 0xB3667A2EUL, 0xC4614AB8UL, 0x5D681B02UL, 0x2A6F2B94UL,
    0xB40BBE37UL, 0xC30C8EA1UL, 0x5A05DF1BUL, 0x2D02EF8DUL,
};


uint32_t gRng = 0x12345678UL;
uint32_t rnd() {
    gRng ^= gRng << 13;
    gRng ^= gRng >> 17;
    gRng ^= gRng << 5;
    return gRng;
}

}  // namespace

static void test_check_values() {
    static const uint8_t kCheck[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX16(0x29B1, sm::crc16Block(sm::kCrc16Init, kCheck, sizeof(kCheck)));
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926UL, sm::crc32Final(sm::crc32Block(sm::kCrc32Init, kCheck, sizeof(kCheck))));
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926UL, sm::crc32(kCheck, sizeof(kCheck)));
    TEST_ASSERT_EQUAL_HEX16(sm::kCrc16Init, sm::crc16Block(sm::kCrc16Init, kCheck, 0));
    TEST_ASSERT_EQUAL_HEX32(sm::kCrc32Init, sm::crc32Block(sm::kCrc32Init, kCheck, 0));
}

static void test_crc32_table_is_the_old_one() {
    for (uint16_t i = 0; i < 256; ++i) {
        const uint32_t old = pgm_read_dword(&kOldCrc32Table[i]);
        TEST_ASSERT_EQUAL_HEX32(old, sm::crc32Update(0, static_cast<uint8_t>(i)));
        TEST_ASSERT_EQUAL_HEX32(old, sm::crc32Bits(i, 8));
    }
}

// Every (state, byte) pair on the host (2^24); a spread of states on the emulated Mega.
static void test_crc16_every_state_and_byte() {
#if defined(__AVR__)
    const uint32_t states = 64;
    const uint32_t stride = 1031;
#else
    const uint32_t states = 65536;
    const uint32_t stride = 1;
#endif
    for (uint32_t k = 0; k < states; ++k) {
        const uint16_t crc = static_cast<uint16_t>(k * stride);
        for (uint16_t b = 0; b < 256; ++b) {
            const uint16_t want = crc16Bitwise(crc, static_cast<uint8_t>(b));
            const uint8_t byte = static_cast<uint8_t>(b);
            if (want != sm::crc16Update(crc, byte) || want != sm::crc16Block(crc, &byte, 1) ||
                want != sm::crc16Bits(static_cast<uint16_t>(crc ^ (static_cast<uint16_t>(b) << 8)), 8)) {
                char msg[48];
                snprintf(msg, sizeof(msg), "crc16 differs at %04X/%02X", crc, static_cast<unsigned>(b));
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }
}

static void test_crc32_from_many_states() {
#if defined(__AVR__)
    const uint16_t states = 16;
#else
    const uint16_t states = 1024;
#endif
    for (uint16_t k = 0; k < states; ++k) {
        const uint32_t crc = k == 0 ? sm::kCrc32Init : rnd();
        for (uint16_t b = 0; b < 256; ++b) {
            const uint8_t byte = static_cast<uint8_t>(b);
            const uint32_t want = crc32Bitwise(crc, byte);
            if (want != sm::crc32Update(crc, byte) || want != sm::crc32Block(crc, &byte, 1))
                TEST_FAIL_MESSAGE("crc32 differs from the bitwise form");
        }
    }
}

// Blocks of every length up to 255 against byte-at-a-time updates, from several states; and the
// 255-byte chunking of crc32() past that.
static void test_blocks_match_bytes() {
    static uint8_t data[300];
    for (uint16_t i = 0; i < sizeof(data); ++i)
        data[i] = static_cast<uint8_t>(rnd());
    for (uint8_t s = 0; s < 4; ++s) {
        const uint32_t c32 = s == 0 ? sm::kCrc32Init : rnd();
        const uint16_t c16 = s == 0 ? sm::kCrc16Init : static_cast<uint16_t>(rnd());
        uint32_t want32 = c32;
        uint16_t want16 = c16;
        for (uint16_t n = 0; n <= 255; ++n) {
            TEST_ASSERT_EQUAL_HEX32(want32, sm::crc32Block(c32, data, static_cast<uint8_t>(n)));
            TEST_ASSERT_EQUAL_HEX16(want16, sm::crc16Block(c16, data, static_cast<uint8_t>(n)));
            if (n < 255) {
                want32 = crc32Bitwise(want32, data[n]);
                want16 = crc16Bitwise(want16, data[n]);
            }
        }
    }
    uint32_t want = sm::kCrc32Init;
    for (uint16_t i = 0; i < sizeof(data); ++i)
        want = crc32Bitwise(want, data[i]);
    TEST_ASSERT_EQUAL_HEX32(~want, sm::crc32(data, sizeof(data)));
}

// The compile-time form against the runtime one, for the 24 heads Out::head() starts from.
static void test_crc32_text_matches_runtime() {
    static const char *const kHeads[] = {"@SM SLOT ", "@SM PRESS "};
    static const char *const kCtrls[] = {"F ", "D "};
    static const char *const kMods[] = {"NONE ", "L1 ", "L2 ", "PS ", "CIRCLE ", "CROSS "};
    char text[32];
    uint8_t n = 0;
    for (const char *h : kHeads) {
        for (const char *c : kCtrls) {
            for (const char *m : kMods) {
                snprintf(text, sizeof(text), "%s%s%s", h, c, m);
                const uint32_t runtime =
                    sm::crc32Block(sm::kCrc32Init, reinterpret_cast<const uint8_t *>(text), static_cast<uint8_t>(strlen(text)));
                TEST_ASSERT_EQUAL_HEX32(runtime, sm::crc32Text(sm::kCrc32Init, text));
                ++n;
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT8(24, n);
    const char *s = "123456789";
    TEST_ASSERT_EQUAL_HEX16(0x29B1, sm::crc16Text(sm::kCrc16Init, s));
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_check_values);
    RUN_TEST(test_crc32_table_is_the_old_one);
    RUN_TEST(test_crc16_every_state_and_byte);
    RUN_TEST(test_crc32_from_many_states);
    RUN_TEST(test_blocks_match_bytes);
    RUN_TEST(test_crc32_text_matches_runtime);
    return UNITY_END();
}
