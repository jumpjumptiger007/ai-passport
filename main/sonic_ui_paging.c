#include "sonic_ui_paging.h"

#include "sonic_core.h"

static size_t sonic_ui_codepoint_bytes(const uint8_t *bytes, size_t remaining)
{
    uint8_t first;

    if (remaining == 0u) {
        return 0u;
    }
    first = bytes[0];
    if (first < 0x80u) {
        return 1u;
    }
    if (first >= 0xC2u && first <= 0xDFu) {
        return remaining >= 2u ? 2u : 1u;
    }
    if (first >= 0xE0u && first <= 0xEFu) {
        return remaining >= 3u ? 3u : 1u;
    }
    if (first >= 0xF0u && first <= 0xF4u) {
        return remaining >= 4u ? 4u : 1u;
    }
    return 1u;
}

static size_t sonic_ui_next_text_page(const uint8_t *bytes, size_t length,
                                      size_t start)
{
    size_t offset = start;
    size_t columns = 0u;
    size_t lines = 1u;

    while (offset < length) {
        size_t count = sonic_ui_codepoint_bytes(bytes + offset, length - offset);
        bool newline = count == 1u && bytes[offset] == (uint8_t)'\n';

        if (newline) {
            ++lines;
            columns = 0u;
        } else if (columns >= SONIC_UI_TEXT_COLUMNS) {
            ++lines;
            columns = 0u;
        }
        if (lines > SONIC_UI_TEXT_LINES_PER_PAGE) {
            return offset;
        }
        offset += count;
        if (!newline) {
            ++columns;
        }
    }
    return length;
}

size_t sonic_ui_text_page_count(const uint8_t *bytes, size_t length)
{
    size_t pages = 0u;
    size_t start = 0u;

    if ((bytes == NULL && length != 0u) || length > SONIC_MAX_MESSAGE_BYTES) {
        return 0u;
    }
    do {
        size_t end = sonic_ui_next_text_page(bytes, length, start);
        if (end == start && end < length) {
            end += sonic_ui_codepoint_bytes(bytes + end, length - end);
        }
        ++pages;
        start = end;
    } while (start < length);
    return pages;
}

bool sonic_ui_text_page(const uint8_t *bytes, size_t length, size_t page_index,
                        sonic_ui_page_t *out_page)
{
    size_t start = 0u;
    size_t page = 0u;

    if (out_page == NULL || sonic_ui_text_page_count(bytes, length) == 0u) {
        return false;
    }
    do {
        size_t end = sonic_ui_next_text_page(bytes, length, start);
        if (end == start && end < length) {
            end += sonic_ui_codepoint_bytes(bytes + end, length - end);
        }
        if (page == page_index) {
            out_page->start = start;
            out_page->length = end - start;
            return true;
        }
        ++page;
        start = end;
    } while (start < length);
    return false;
}

size_t sonic_ui_token_page_count(size_t length)
{
    if (length > SONIC_MAX_MESSAGE_BYTES) {
        return 0u;
    }
    return length == 0u ? 1u
                        : (length + SONIC_UI_TOKEN_BYTES_PER_PAGE - 1u) /
                              SONIC_UI_TOKEN_BYTES_PER_PAGE;
}

bool sonic_ui_token_page(size_t length, size_t page_index,
                         sonic_ui_page_t *out_page)
{
    size_t page_count = sonic_ui_token_page_count(length);
    size_t start;
    size_t remaining;

    if (out_page == NULL || page_count == 0u || page_index >= page_count) {
        return false;
    }
    start = page_index * SONIC_UI_TOKEN_BYTES_PER_PAGE;
    remaining = length - start;
    out_page->start = start;
    out_page->length = remaining > SONIC_UI_TOKEN_BYTES_PER_PAGE
                           ? SONIC_UI_TOKEN_BYTES_PER_PAGE
                           : remaining;
    return true;
}
