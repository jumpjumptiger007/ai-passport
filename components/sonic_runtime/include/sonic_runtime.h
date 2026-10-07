#ifndef SONIC_RUNTIME_H
#define SONIC_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sonic_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SONIC_RUNTIME_PRESET_COUNT 4u
#define SONIC_RUNTIME_DIAGNOSTICS_PAGE_COUNT 4u

typedef enum {
    SONIC_RUNTIME_HOME = 0,
    SONIC_RUNTIME_RX_LISTENING,
    SONIC_RUNTIME_RX_PAUSED,
    SONIC_RUNTIME_RX_ASSEMBLING,
    SONIC_RUNTIME_RX_COMPLETE,
    SONIC_RUNTIME_RX_INCOMPLETE,
    SONIC_RUNTIME_RX_INVALID_MESSAGE,
    SONIC_RUNTIME_RX_AUDIO_ERROR,
    SONIC_RUNTIME_RX_UNSUPPORTED_GLYPH,
    SONIC_RUNTIME_RX_FRAME_CONFLICT,
    SONIC_RUNTIME_TX_MENU,
    SONIC_RUNTIME_TX_PREVIEW,
    SONIC_RUNTIME_TX_PREPARING,
    SONIC_RUNTIME_TX_PLAYING,
    SONIC_RUNTIME_TX_COMPLETE,
    SONIC_RUNTIME_TX_CANCELLED,
    SONIC_RUNTIME_TX_AUDIO_ERROR,
    SONIC_RUNTIME_DIAGNOSTICS
} sonic_runtime_state_t;

typedef enum {
    SONIC_RUNTIME_HOME_RECEIVE = 0,
    SONIC_RUNTIME_HOME_SEND,
    SONIC_RUNTIME_HOME_DIAGNOSTICS
} sonic_runtime_home_item_t;

typedef enum {
    SONIC_RUNTIME_PRESET_HELLO = 0,
    SONIC_RUNTIME_PRESET_DEMO_URL,
    SONIC_RUNTIME_PRESET_DEVICE_CARD,
    SONIC_RUNTIME_PRESET_TEST_TOKEN
} sonic_runtime_preset_t;

typedef enum {
    SONIC_RUNTIME_DIAG_SYSTEM = 0,
    SONIC_RUNTIME_DIAG_AUDIO,
    SONIC_RUNTIME_DIAG_CODEC,
    SONIC_RUNTIME_DIAG_LAST_TRANSFER
} sonic_runtime_diagnostics_page_t;

typedef enum {
    SONIC_RUNTIME_BUTTON_UP = 0,
    SONIC_RUNTIME_BUTTON_DOWN,
    SONIC_RUNTIME_BUTTON_OK,
    SONIC_RUNTIME_BUTTON_OK_LONG
} sonic_runtime_button_t;

typedef enum {
    SONIC_RUNTIME_EVENT_BUTTON = 0,
    SONIC_RUNTIME_EVENT_TICK,
    SONIC_RUNTIME_EVENT_RX_FRAME,
    SONIC_RUNTIME_EVENT_TX_STARTED,
    SONIC_RUNTIME_EVENT_TX_PROGRESS,
    SONIC_RUNTIME_EVENT_TX_COMPLETE,
    SONIC_RUNTIME_EVENT_TX_CANCELLED,
    SONIC_RUNTIME_EVENT_AUDIO_ERROR
} sonic_runtime_event_type_t;

typedef struct {
    sonic_runtime_event_type_t type;
    union {
        sonic_runtime_button_t button;
        struct {
            const uint8_t *bytes;
            size_t length;
        } rx_frame;
        struct {
            uint8_t frame_index;
            uint8_t frame_count;
        } tx_progress;
        uint32_t tx_duration_ms;
        sonic_error_t audio_error;
    } data;
} sonic_runtime_event_t;

enum {
    SONIC_RUNTIME_ACTION_START_RX = 1u << 0,
    SONIC_RUNTIME_ACTION_STOP_RX = 1u << 1,
    SONIC_RUNTIME_ACTION_TRANSMIT = 1u << 2,
    SONIC_RUNTIME_ACTION_CANCEL_TX = 1u << 3
};

typedef struct {
    uint32_t flags;
    uint8_t frame_count;
    uint8_t volume_percent;
    bool resume_rx_after_tx;
    uint8_t frames[SONIC_MAX_FRAGMENTS][SONIC_FRAME_SIZE];
} sonic_runtime_actions_t;

typedef struct {
    bool valid;
    bool available;
    sonic_payload_type_t type;
    uint8_t length;
    uint8_t bytes[SONIC_MAX_MESSAGE_BYTES];
} sonic_runtime_preset_payload_t;

typedef enum {
    SONIC_RUNTIME_TRANSFER_COMPLETED = 0,
    SONIC_RUNTIME_TRANSFER_INCOMPLETE,
    SONIC_RUNTIME_TRANSFER_CANCELLED,
    SONIC_RUNTIME_TRANSFER_ERROR
} sonic_runtime_transfer_result_t;

typedef struct {
    bool valid;
    bool is_rx;
    sonic_payload_type_t type;
    uint16_t message_id;
    uint8_t frame_count;
    uint8_t byte_length;
    uint32_t duration_ms;
    sonic_runtime_transfer_result_t result;
    sonic_error_t error;
} sonic_runtime_last_transfer_t;

typedef struct {
    sonic_runtime_state_t state;
    sonic_runtime_home_item_t home_selection;
    sonic_runtime_preset_t preset_selection;
    sonic_runtime_diagnostics_page_t diagnostics_page;
    uint16_t message_id;
    sonic_payload_type_t payload_type;
    uint8_t payload_length;
    uint8_t frame_count;
    uint8_t frame_index;
    uint8_t received_fragments;
    uint8_t expected_fragments;
    bool microphone_active;
    sonic_error_t error;
    uint8_t payload[SONIC_MAX_MESSAGE_BYTES];
    sonic_runtime_last_transfer_t last_transfer;
} sonic_runtime_view_t;

typedef struct {
    sonic_runtime_state_t state;
    sonic_runtime_home_item_t home_selection;
    sonic_runtime_preset_t preset_selection;
    sonic_runtime_diagnostics_page_t diagnostics_page;
    sonic_runtime_preset_payload_t presets[SONIC_RUNTIME_PRESET_COUNT];
    sonic_reassembly_t reassembly;
    sonic_reassembly_result_t rx_result;
    sonic_runtime_last_transfer_t last_transfer;
    uint16_t current_message_id;
    uint16_t tx_message_id;
    uint8_t current_payload[SONIC_MAX_MESSAGE_BYTES];
    uint8_t current_payload_length;
    sonic_payload_type_t current_payload_type;
    uint8_t current_frames[SONIC_MAX_FRAGMENTS][SONIC_FRAME_SIZE];
    uint8_t current_frame_count;
    uint8_t tx_frame_index;
    uint8_t tx_volume_percent;
    uint8_t receive_attempt_frame_count;
    uint32_t last_valid_rx_ms;
    uint32_t rx_started_ms;
    bool has_allocated_message_id;
    bool microphone_active;
    bool resume_rx_after_tx;
    sonic_error_t error;
} sonic_runtime_t;

void sonic_runtime_init(sonic_runtime_t *runtime, uint16_t message_id_seed,
                        uint32_t now_ms);
sonic_error_t sonic_runtime_set_preset(sonic_runtime_t *runtime,
                                       sonic_runtime_preset_t preset,
                                       sonic_payload_type_t type,
                                       const uint8_t *payload,
                                       size_t payload_length);
void sonic_runtime_set_tx_options(sonic_runtime_t *runtime,
                                  uint8_t volume_percent,
                                  bool resume_rx_after_tx);
void sonic_runtime_handle(sonic_runtime_t *runtime,
                         const sonic_runtime_event_t *event,
                         uint32_t now_ms,
                         sonic_runtime_actions_t *out_actions);
void sonic_runtime_get_view(const sonic_runtime_t *runtime,
                            sonic_runtime_view_t *out_view);

#ifdef __cplusplus
}
#endif

#endif
