#include "sonic_ui_paging.h"
#include "sonic_core.h"

#include <assert.h>
#include <string.h>

static void test_text_pages(void)
{
    static const uint8_t empty[] = "";
    static const uint8_t short_text[] = "Hello from Passport!";
    static const uint8_t multiline[] =
        "first line\nsecond line\nthird line\nfourth line\nfifth line";
    uint8_t maximum[SONIC_MAX_MESSAGE_BYTES];
    sonic_ui_page_t page;

    memset(maximum, 'W', sizeof(maximum));
    assert(sonic_ui_text_page_count(empty, 0u) == 1u);
    assert(sonic_ui_text_page(empty, 0u, 0u, &page));
    assert(page.start == 0u && page.length == 0u);
    assert(sonic_ui_text_page_count(short_text, sizeof(short_text) - 1u) == 1u);
    assert(sonic_ui_text_page_count(multiline, sizeof(multiline) - 1u) == 2u);
    assert(sonic_ui_text_page_count(maximum, sizeof(maximum)) == 2u);
    assert(sonic_ui_text_page(maximum, sizeof(maximum), 0u, &page));
    assert(page.start == 0u && page.length == 72u);
    assert(sonic_ui_text_page(maximum, sizeof(maximum), 1u, &page));
    assert(page.start == 72u && page.length == 21u);
    assert(!sonic_ui_text_page(maximum, sizeof(maximum), 2u, &page));
    assert(sonic_ui_text_page_count(maximum, sizeof(maximum) + 1u) == 0u);
}

static void test_url_pages(void)
{
    uint8_t url[SONIC_MAX_MESSAGE_BYTES];
    sonic_ui_page_t page;

    memcpy(url, "https://", 8u);
    memset(url + 8u, 'a', sizeof(url) - 8u);
    assert(sonic_ui_text_page_count(url, sizeof(url)) == 2u);
    assert(sonic_ui_text_page(url, sizeof(url), 0u, &page));
    assert(page.start == 0u && page.length == 72u);
}

static void test_token_pages_keep_binary_bytes(void)
{
    uint8_t token[SONIC_MAX_MESSAGE_BYTES];
    sonic_ui_page_t page;

    for (size_t i = 0u; i < sizeof(token); ++i) {
        token[i] = (uint8_t)(i * 37u);
    }
    token[0] = 0u;
    token[1] = 0xFFu;
    assert(sonic_ui_token_page_count(0u) == 1u);
    assert(sonic_ui_token_page_count(1u) == 1u);
    assert(sonic_ui_token_page_count(16u) == 1u);
    assert(sonic_ui_token_page_count(sizeof(token)) == 6u);
    assert(sonic_ui_token_page(sizeof(token), 0u, &page));
    assert(page.start == 0u && page.length == 16u);
    assert(token[page.start] == 0u && token[page.start + 1u] == 0xFFu);
    assert(sonic_ui_token_page(sizeof(token), 5u, &page));
    assert(page.start == 80u && page.length == 13u);
    assert(!sonic_ui_token_page(sizeof(token), 6u, &page));
    assert(sonic_ui_token_page_count(sizeof(token) + 1u) == 0u);
}

int main(void)
{
    test_text_pages();
    test_url_pages();
    test_token_pages_keep_binary_bytes();
    return 0;
}
