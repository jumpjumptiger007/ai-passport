#ifndef SONIC_UI_PAGING_H
#define SONIC_UI_PAGING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SONIC_UI_TEXT_COLUMNS 18u
#define SONIC_UI_TEXT_LINES_PER_PAGE 4u
#define SONIC_UI_TOKEN_BYTES_PER_PAGE 16u

typedef struct {
    size_t start;
    size_t length;
} sonic_ui_page_t;

size_t sonic_ui_text_page_count(const uint8_t *bytes, size_t length);
bool sonic_ui_text_page(const uint8_t *bytes, size_t length, size_t page_index,
                        sonic_ui_page_t *out_page);
size_t sonic_ui_token_page_count(size_t length);
bool sonic_ui_token_page(size_t length, size_t page_index,
                         sonic_ui_page_t *out_page);

#ifdef __cplusplus
}
#endif

#endif
