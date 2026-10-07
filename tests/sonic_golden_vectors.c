#include "sonic_golden_vectors.h"

static const uint8_t frames_empty_text[1][SONIC_FRAME_SIZE] = {
    "\x53\x4C\x11\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x9D\xEF",
};

static const uint8_t payload_text_a[] = {
    0x41,
};
static const uint8_t frames_text_a[1][SONIC_FRAME_SIZE] = {
    "\x53\x4C\x11\x12\x34\x00\x01\x41\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x30\x45",
};

static const uint8_t payload_max_ascii_text[93] = {
    0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E,
    0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E,
    0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E,
    0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E,
    0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E,
    0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E,
    0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E,
    0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E,
};
static const uint8_t frames_max_ascii_text[3][SONIC_FRAME_SIZE] = {
    "\x53\x4C\x11\x01\x00\x02\x1F\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x47\x56",
    "\x53\x4C\x11\x01\x00\x12\x1F\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x1B\x9E",
    "\x53\x4C\x11\x01\x00\x22\x1F\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E\xFE\xC6",
};

static const uint8_t payload_multi_text[] = {
    0x53, 0x6F, 0x6E, 0x69, 0x63, 0x2D, 0x4C, 0x69, 0x6E, 0x6B, 0x2D, 0x46,
    0x72, 0x61, 0x6D, 0x65, 0x2D, 0x54, 0x65, 0x73, 0x74, 0x2D, 0x56, 0x65,
    0x63, 0x74, 0x6F, 0x72, 0x2D, 0x46, 0x6F, 0x72, 0x2D, 0x54, 0x77, 0x6F,
    0x2D, 0x46, 0x72, 0x61, 0x6D, 0x65, 0x73,
};
static const uint8_t frames_multi_text[2][SONIC_FRAME_SIZE] = {
    "\x53\x4C\x11\x22\x22\x01\x1F\x53\x6F\x6E\x69\x63\x2D\x4C\x69\x6E\x6B\x2D\x46\x72\x61\x6D\x65\x2D\x54\x65\x73\x74\x2D\x56\x65\x63\x74\x6F\x72\x2D\x46\x6F\x30\xDC",
    "\x53\x4C\x11\x22\x22\x11\x0C\x72\x2D\x54\x77\x6F\x2D\x46\x72\x61\x6D\x65\x73\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xB6\x51",
};

static const uint8_t payload_https_url[] = {
    0x68, 0x74, 0x74, 0x70, 0x73, 0x3A, 0x2F, 0x2F, 0x65, 0x78, 0x61, 0x6D,
    0x70, 0x6C, 0x65, 0x2E, 0x63, 0x6F, 0x6D, 0x2F, 0x78,
};
static const uint8_t frames_https_url[1][SONIC_FRAME_SIZE] = {
    "\x53\x4C\x12\x00\x02\x00\x15\x68\x74\x74\x70\x73\x3A\x2F\x2F\x65\x78\x61\x6D\x70\x6C\x65\x2E\x63\x6F\x6D\x2F\x78\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xB5\xB1",
};

static const uint8_t payload_token_nul[] = {
    0xDE, 0x00, 0xAD,
};
static const uint8_t frames_token_nul[1][SONIC_FRAME_SIZE] = {
    "\x53\x4C\x13\x00\x03\x00\x03\xDE\x00\xAD\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x6F\x85",
};

static const uint8_t payload_token_non_utf8[] = {
    0xFF, 0xFE, 0x80,
};
static const uint8_t frames_token_non_utf8[1][SONIC_FRAME_SIZE] = {
    "\x53\x4C\x13\x00\x04\x00\x03\xFF\xFE\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xAD\xFC",
};

static const uint8_t payload_device_info[] = {
    0x01, 0x01, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x64, 0x00, 0x5D, 0x7F,
    0xDE, 0xAD, 0xBE, 0xEF,
};
static const uint8_t frames_device_info[1][SONIC_FRAME_SIZE] = {
    "\x53\x4C\x14\x00\x05\x00\x10\x01\x01\x01\x02\x03\x04\x05\x06\x64\x00\x5D\x7F\xDE\xAD\xBE\xEF\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x64\x04",
};

const sonic_golden_vector_t sonic_golden_vectors[] = {
    {"empty_text", SONIC_TYPE_TEXT, 0x0001u, NULL, 0u, 1u, frames_empty_text},
    {"text_a", SONIC_TYPE_TEXT, 0x1234u, payload_text_a, 1u, 1u, frames_text_a},
    {"max_ascii_text", SONIC_TYPE_TEXT, 0x0100u, payload_max_ascii_text, 93u, 3u, frames_max_ascii_text},
    {"multi_text", SONIC_TYPE_TEXT, 0x2222u, payload_multi_text, 43u, 2u, frames_multi_text},
    {"https_url", SONIC_TYPE_URL, 0x0002u, payload_https_url, 21u, 1u, frames_https_url},
    {"token_nul", SONIC_TYPE_TOKEN, 0x0003u, payload_token_nul, 3u, 1u, frames_token_nul},
    {"token_non_utf8", SONIC_TYPE_TOKEN, 0x0004u, payload_token_non_utf8, 3u, 1u, frames_token_non_utf8},
    {"device_info", SONIC_TYPE_DEVICE_INFO, 0x0005u, payload_device_info, 16u, 1u, frames_device_info},
};

const size_t sonic_golden_vector_count = sizeof(sonic_golden_vectors) / sizeof(sonic_golden_vectors[0]);
