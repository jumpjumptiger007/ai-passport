#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "sonic_audio_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*sonic_audio_event_callback_t)(const sonic_audio::Event *event, void *context);

// Starts the single long-lived Sonic audio worker. The callback always runs in
// that worker's task context and must not call blocking BSP or LVGL operations.
esp_err_t sonic_audio_init(sonic_audio_event_callback_t callback, void *context);
esp_err_t sonic_audio_start_rx(void);
esp_err_t sonic_audio_stop_rx(void);
esp_err_t sonic_audio_transmit(
    const uint8_t frames[sonic_audio::kTxFrameLimit][sonic_codec::kFrameBytes],
    size_t frame_count,
    uint8_t volume_percent,
    bool resume_rx);
esp_err_t sonic_audio_cancel_tx(void);
bool sonic_audio_get_diagnostics(sonic_audio::Diagnostics *out_diagnostics);

#ifdef __cplusplus
}
#endif
