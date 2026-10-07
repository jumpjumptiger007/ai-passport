#ifndef SONIC_UI_H
#define SONIC_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sonic_runtime.h"
#include "sonic_ui_paging.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SONIC_UI_AUDIO_UNAVAILABLE = 0,
    SONIC_UI_AUDIO_IDLE,
    SONIC_UI_AUDIO_RX,
    SONIC_UI_AUDIO_TX,
    SONIC_UI_AUDIO_ERROR
} sonic_ui_audio_state_t;

typedef struct {
    int battery_percent;
    int free_heap_bytes;
    int minimum_free_heap_bytes;
    int largest_free_block_bytes;
    int codec_heap_bytes;
    uint32_t sample_rate_hz;
    uint8_t sample_bits;
    uint8_t channels;
    uint8_t speaker_volume_percent;
    uint32_t worker_stack_high_water_words;
    float microphone_rms;
    float microphone_dbfs;
    uint64_t rx_average_us;
    uint64_t rx_p99_us;
    uint64_t rx_maximum_us;
    uint8_t acoustic_profile;
    sonic_ui_audio_state_t audio_state;
    bool audio_diagnostics_available;
    char sonic_version[16];
    char ggwave_version[16];
    char build_fingerprint[9];
} sonic_ui_context_t;

bool sonic_ui_create(void);
void sonic_ui_destroy(void);
void sonic_ui_render(const sonic_runtime_view_t *view,
                     const sonic_ui_context_t *context);
bool sonic_ui_handle_page_button(const sonic_runtime_view_t *view,
                                 bool next_page);
size_t sonic_ui_current_page(void);
size_t sonic_ui_page_count(const sonic_runtime_view_t *view);

#ifdef __cplusplus
}
#endif

#endif
