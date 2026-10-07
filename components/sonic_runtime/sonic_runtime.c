#include "sonic_runtime.h"

#include <string.h>

#define SONIC_RUNTIME_LISTEN_IDLE_MS 120000u
#define SONIC_RUNTIME_DEFAULT_VOLUME 75u

static void sonic_actions_reset(sonic_runtime_actions_t *actions)
{
    memset(actions, 0, sizeof(*actions));
}

static void sonic_runtime_start_rx(sonic_runtime_t *runtime,
                                   uint32_t now_ms,
                                   sonic_runtime_actions_t *actions)
{
    runtime->microphone_active = true;
    runtime->rx_started_ms = now_ms;
    runtime->last_valid_rx_ms = now_ms;
    actions->flags |= SONIC_RUNTIME_ACTION_START_RX;
}

static void sonic_runtime_stop_rx(sonic_runtime_t *runtime,
                                  sonic_runtime_actions_t *actions)
{
    if (runtime->microphone_active) {
        actions->flags |= SONIC_RUNTIME_ACTION_STOP_RX;
    }
    runtime->microphone_active = false;
}

static void sonic_runtime_set_last_transfer(sonic_runtime_t *runtime,
                                            bool is_rx,
                                            sonic_payload_type_t type,
                                            uint16_t message_id,
                                            uint8_t frame_count,
                                            uint8_t byte_length,
                                            uint32_t duration_ms,
                                            sonic_runtime_transfer_result_t result,
                                            sonic_error_t error)
{
    runtime->last_transfer.valid = true;
    runtime->last_transfer.is_rx = is_rx;
    runtime->last_transfer.type = type;
    runtime->last_transfer.message_id = message_id;
    runtime->last_transfer.frame_count = frame_count;
    runtime->last_transfer.byte_length = byte_length;
    runtime->last_transfer.duration_ms = duration_ms;
    runtime->last_transfer.result = result;
    runtime->last_transfer.error = error;
}

static void sonic_runtime_copy_rx_result(sonic_runtime_t *runtime,
                                         const sonic_reassembly_result_t *result)
{
    runtime->rx_result = *result;
    runtime->current_payload_type = result->type;
    runtime->current_payload_length = result->message_length;
    runtime->current_message_id = result->message_id;
    if (result->message_length <= SONIC_MAX_MESSAGE_BYTES) {
        memcpy(runtime->current_payload, result->payload, result->message_length);
    }
    runtime->receive_attempt_frame_count = result->fragment_count;
}

static bool sonic_text_has_passport_glyphs(const uint8_t *bytes, size_t length)
{
    size_t i = 0u;

    while (i < length) {
        uint8_t first = bytes[i];
        uint32_t codepoint;
        size_t count;

        if (first < 0x80u) {
            codepoint = first;
            count = 1u;
        } else if (first >= 0xC2u && first <= 0xDFu) {
            if (length - i < 2u) {
                return false;
            }
            codepoint = ((uint32_t)(first & 0x1Fu) << 6) |
                        (uint32_t)(bytes[i + 1u] & 0x3Fu);
            count = 2u;
        } else if (first >= 0xE0u && first <= 0xEFu) {
            if (length - i < 3u) {
                return false;
            }
            codepoint = ((uint32_t)(first & 0x0Fu) << 12) |
                        ((uint32_t)(bytes[i + 1u] & 0x3Fu) << 6) |
                        (uint32_t)(bytes[i + 2u] & 0x3Fu);
            count = 3u;
        } else if (first >= 0xF0u && first <= 0xF4u) {
            if (length - i < 4u) {
                return false;
            }
            codepoint = ((uint32_t)(first & 0x07u) << 18) |
                        ((uint32_t)(bytes[i + 1u] & 0x3Fu) << 12) |
                        ((uint32_t)(bytes[i + 2u] & 0x3Fu) << 6) |
                        (uint32_t)(bytes[i + 3u] & 0x3Fu);
            count = 4u;
        } else {
            return false;
        }

        if (codepoint != '\n' && (codepoint < 0x20u || codepoint > 0x7Eu)) {
            return false;
        }
        i += count;
    }
    return true;
}

static void sonic_runtime_receive_complete(sonic_runtime_t *runtime,
                                           sonic_runtime_actions_t *actions)
{
    sonic_error_t error;

    sonic_runtime_stop_rx(runtime, actions);
    error = sonic_payload_validate(runtime->current_payload_type,
                                   runtime->current_payload,
                                   runtime->current_payload_length);
    if (error != SONIC_OK) {
        runtime->error = error;
        runtime->state = SONIC_RUNTIME_RX_INVALID_MESSAGE;
        sonic_runtime_set_last_transfer(runtime, true, runtime->current_payload_type,
                                        runtime->current_message_id,
                                        runtime->receive_attempt_frame_count,
                                        runtime->current_payload_length, 0u,
                                        SONIC_RUNTIME_TRANSFER_ERROR, error);
        return;
    }

    if (runtime->current_payload_type == SONIC_TYPE_TEXT &&
        !sonic_text_has_passport_glyphs(runtime->current_payload,
                                        runtime->current_payload_length)) {
        runtime->error = SONIC_UNSUPPORTED_GLYPH;
        runtime->state = SONIC_RUNTIME_RX_UNSUPPORTED_GLYPH;
        sonic_runtime_set_last_transfer(runtime, true, runtime->current_payload_type,
                                        runtime->current_message_id,
                                        runtime->receive_attempt_frame_count,
                                        runtime->current_payload_length, 0u,
                                        SONIC_RUNTIME_TRANSFER_ERROR,
                                        SONIC_UNSUPPORTED_GLYPH);
        return;
    }

    runtime->error = SONIC_OK;
    runtime->state = SONIC_RUNTIME_RX_COMPLETE;
    sonic_runtime_set_last_transfer(runtime, true, runtime->current_payload_type,
                                    runtime->current_message_id,
                                    runtime->receive_attempt_frame_count,
                                    runtime->current_payload_length, 0u,
                                    SONIC_RUNTIME_TRANSFER_COMPLETED, SONIC_OK);
}

static bool sonic_runtime_is_payload_error(sonic_error_t error)
{
    return error == SONIC_INVALID_UTF8 || error == SONIC_INVALID_URL ||
           error == SONIC_INVALID_DEVICE_INFO;
}

static void sonic_runtime_begin_new_send(sonic_runtime_t *runtime,
                                         sonic_runtime_actions_t *actions)
{
    const sonic_runtime_preset_payload_t *preset =
        &runtime->presets[runtime->preset_selection];
    size_t frame_count = 0u;
    sonic_error_t error;

    if (!preset->available) {
        runtime->error = SONIC_INVALID_ARGUMENT;
        return;
    }
    runtime->tx_message_id = runtime->has_allocated_message_id
                                 ? sonic_message_id_next(runtime->tx_message_id)
                                 : runtime->tx_message_id;
    runtime->has_allocated_message_id = true;
    runtime->current_message_id = runtime->tx_message_id;
    memcpy(runtime->current_payload, preset->bytes, preset->length);
    runtime->current_payload_length = preset->length;
    runtime->current_payload_type = preset->type;
    error = sonic_message_fragment(preset->type, runtime->current_message_id,
                                   preset->bytes, preset->length,
                                   runtime->current_frames, SONIC_MAX_FRAGMENTS,
                                   &frame_count);
    if (error != SONIC_OK) {
        runtime->error = error;
        runtime->state = SONIC_RUNTIME_TX_AUDIO_ERROR;
        return;
    }
    runtime->current_frame_count = (uint8_t) frame_count;
    runtime->tx_frame_index = 0u;
    runtime->error = SONIC_OK;
    runtime->state = SONIC_RUNTIME_TX_PREVIEW;
    (void) actions;
}

static void sonic_runtime_transmit_current(sonic_runtime_t *runtime,
                                           sonic_runtime_actions_t *actions)
{
    actions->flags |= SONIC_RUNTIME_ACTION_TRANSMIT;
    actions->frame_count = runtime->current_frame_count;
    actions->volume_percent = runtime->tx_volume_percent;
    actions->resume_rx_after_tx = runtime->resume_rx_after_tx;
    memcpy(actions->frames, runtime->current_frames,
           (size_t) runtime->current_frame_count * SONIC_FRAME_SIZE);
    runtime->state = SONIC_RUNTIME_TX_PREPARING;
    runtime->tx_frame_index = 0u;
}

static void sonic_runtime_handle_rx_frame(sonic_runtime_t *runtime,
                                          const sonic_runtime_event_t *event,
                                          uint32_t now_ms,
                                          sonic_runtime_actions_t *actions)
{
    sonic_reassembly_status_t status;
    sonic_reassembly_result_t result;

    if ((runtime->state != SONIC_RUNTIME_RX_LISTENING &&
         runtime->state != SONIC_RUNTIME_RX_ASSEMBLING) ||
        !runtime->microphone_active) {
        return;
    }
    status = sonic_reassembly_accept(&runtime->reassembly,
                                     event->data.rx_frame.bytes,
                                     event->data.rx_frame.length,
                                     now_ms, &result);
    if (status == SONIC_REASSEMBLY_ERROR) {
        if (result.error == SONIC_FRAGMENT_CONFLICT) {
            sonic_runtime_stop_rx(runtime, actions);
            sonic_runtime_copy_rx_result(runtime, &result);
            runtime->error = result.error;
            runtime->state = SONIC_RUNTIME_RX_FRAME_CONFLICT;
            sonic_runtime_set_last_transfer(runtime, true, result.type,
                                            result.message_id,
                                            result.fragment_count,
                                            result.message_length, 0u,
                                            SONIC_RUNTIME_TRANSFER_ERROR,
                                            result.error);
        } else if (sonic_runtime_is_payload_error(result.error)) {
            sonic_frame_t frame;
            memset(&frame, 0, sizeof(frame));
            (void) sonic_frame_decode(event->data.rx_frame.bytes,
                                      event->data.rx_frame.length, &frame);
            sonic_runtime_stop_rx(runtime, actions);
            runtime->current_message_id = frame.message_id != 0u
                                              ? frame.message_id
                                              : runtime->rx_result.message_id;
            if (frame.message_id != 0u) {
                runtime->current_payload_type = frame.type;
            }
            runtime->error = result.error;
            runtime->state = SONIC_RUNTIME_RX_INVALID_MESSAGE;
            sonic_runtime_set_last_transfer(
                runtime, true, runtime->current_payload_type,
                runtime->current_message_id,
                frame.fragment_count != 0u ? frame.fragment_count
                                           : runtime->rx_result.fragment_count,
                (uint8_t)(runtime->rx_result.message_length +
                          frame.chunk_length),
                0u, SONIC_RUNTIME_TRANSFER_ERROR,
                result.error);
        }
        return;
    }
    if (status == SONIC_REASSEMBLY_IGNORED || status == SONIC_REASSEMBLY_IDLE) {
        return;
    }

    runtime->last_valid_rx_ms = now_ms;
    runtime->rx_result = result;
    if (status == SONIC_REASSEMBLY_IN_PROGRESS ||
        status == SONIC_REASSEMBLY_DUPLICATE) {
        runtime->state = status == SONIC_REASSEMBLY_DUPLICATE &&
                                 runtime->reassembly.received_mask == 0u
                             ? SONIC_RUNTIME_RX_LISTENING
                             : (result.received_fragments > 0u
                                    ? SONIC_RUNTIME_RX_ASSEMBLING
                                    : SONIC_RUNTIME_RX_LISTENING);
        runtime->receive_attempt_frame_count = result.fragment_count;
        return;
    }
    if (status == SONIC_REASSEMBLY_COMPLETE) {
        sonic_runtime_copy_rx_result(runtime, &result);
        sonic_runtime_receive_complete(runtime, actions);
    }
}

static void sonic_runtime_handle_tick(sonic_runtime_t *runtime,
                                      uint32_t now_ms,
                                      sonic_runtime_actions_t *actions)
{
    sonic_reassembly_status_t status;
    sonic_reassembly_result_t result;

    status = sonic_reassembly_poll(&runtime->reassembly, now_ms, &result);
    if (status == SONIC_REASSEMBLY_INCOMPLETE) {
        sonic_runtime_copy_rx_result(runtime, &result);
        sonic_runtime_stop_rx(runtime, actions);
        runtime->error = SONIC_ASSEMBLY_TIMEOUT;
        runtime->state = SONIC_RUNTIME_RX_INCOMPLETE;
        sonic_runtime_set_last_transfer(runtime, true, result.type,
                                        result.message_id,
                                        result.fragment_count,
                                        result.message_length, 0u,
                                        SONIC_RUNTIME_TRANSFER_INCOMPLETE,
                                        SONIC_ASSEMBLY_TIMEOUT);
        return;
    }
    if (status == SONIC_REASSEMBLY_EXPIRED) {
        memset(&runtime->rx_result, 0, sizeof(runtime->rx_result));
        runtime->receive_attempt_frame_count = 0u;
        memset(runtime->current_payload, 0, sizeof(runtime->current_payload));
        runtime->current_payload_length = 0u;
        runtime->current_message_id = 0u;
        runtime->error = SONIC_OK;
        if (runtime->state == SONIC_RUNTIME_RX_ASSEMBLING) {
            runtime->state = SONIC_RUNTIME_RX_LISTENING;
        }
        return;
    }

    if ((runtime->state == SONIC_RUNTIME_RX_LISTENING ||
         runtime->state == SONIC_RUNTIME_RX_ASSEMBLING) &&
        runtime->microphone_active && !runtime->reassembly.active &&
        (uint32_t)(now_ms - runtime->last_valid_rx_ms) >=
            SONIC_RUNTIME_LISTEN_IDLE_MS) {
        sonic_runtime_stop_rx(runtime, actions);
        runtime->state = SONIC_RUNTIME_RX_PAUSED;
    }
}

static void sonic_runtime_handle_button(sonic_runtime_t *runtime,
                                        sonic_runtime_button_t button,
                                        uint32_t now_ms,
                                        sonic_runtime_actions_t *actions)
{
    switch (runtime->state) {
    case SONIC_RUNTIME_HOME:
        if (button == SONIC_RUNTIME_BUTTON_UP) {
            runtime->home_selection = (sonic_runtime_home_item_t)
                ((runtime->home_selection + 2u) % 3u);
        } else if (button == SONIC_RUNTIME_BUTTON_DOWN) {
            runtime->home_selection = (sonic_runtime_home_item_t)
                ((runtime->home_selection + 1u) % 3u);
        } else if (button == SONIC_RUNTIME_BUTTON_OK) {
            if (runtime->home_selection == SONIC_RUNTIME_HOME_RECEIVE) {
                sonic_reassembly_reset(&runtime->reassembly);
                memset(&runtime->rx_result, 0, sizeof(runtime->rx_result));
                runtime->receive_attempt_frame_count = 0u;
                runtime->error = SONIC_OK;
                runtime->state = SONIC_RUNTIME_RX_LISTENING;
                sonic_runtime_start_rx(runtime, now_ms, actions);
            } else if (runtime->home_selection == SONIC_RUNTIME_HOME_SEND) {
                runtime->preset_selection = SONIC_RUNTIME_PRESET_HELLO;
                runtime->state = SONIC_RUNTIME_TX_MENU;
            } else {
                runtime->diagnostics_page = SONIC_RUNTIME_DIAG_SYSTEM;
                runtime->state = SONIC_RUNTIME_DIAGNOSTICS;
            }
        }
        break;
    case SONIC_RUNTIME_RX_LISTENING:
        if (button == SONIC_RUNTIME_BUTTON_OK) {
            sonic_runtime_stop_rx(runtime, actions);
            runtime->state = SONIC_RUNTIME_RX_PAUSED;
        } else if (button == SONIC_RUNTIME_BUTTON_OK_LONG) {
            sonic_runtime_stop_rx(runtime, actions);
            sonic_reassembly_reset(&runtime->reassembly);
            runtime->receive_attempt_frame_count = 0u;
            runtime->state = SONIC_RUNTIME_HOME;
        }
        break;
    case SONIC_RUNTIME_RX_ASSEMBLING:
        if (button == SONIC_RUNTIME_BUTTON_OK_LONG) {
            sonic_runtime_stop_rx(runtime, actions);
            sonic_reassembly_reset(&runtime->reassembly);
            memset(&runtime->rx_result, 0, sizeof(runtime->rx_result));
            runtime->receive_attempt_frame_count = 0u;
            runtime->state = SONIC_RUNTIME_HOME;
        }
        break;
    case SONIC_RUNTIME_RX_PAUSED:
        if (button == SONIC_RUNTIME_BUTTON_OK) {
            if (!runtime->reassembly.active) {
                sonic_reassembly_reset(&runtime->reassembly);
                memset(&runtime->rx_result, 0, sizeof(runtime->rx_result));
                runtime->receive_attempt_frame_count = 0u;
            }
            runtime->state = runtime->reassembly.active
                                 ? SONIC_RUNTIME_RX_ASSEMBLING
                                 : SONIC_RUNTIME_RX_LISTENING;
            sonic_runtime_start_rx(runtime, now_ms, actions);
        } else if (button == SONIC_RUNTIME_BUTTON_OK_LONG) {
            sonic_reassembly_reset(&runtime->reassembly);
            runtime->receive_attempt_frame_count = 0u;
            runtime->state = SONIC_RUNTIME_HOME;
        }
        break;
    case SONIC_RUNTIME_RX_INCOMPLETE:
    case SONIC_RUNTIME_RX_COMPLETE:
    case SONIC_RUNTIME_RX_INVALID_MESSAGE:
    case SONIC_RUNTIME_RX_UNSUPPORTED_GLYPH:
    case SONIC_RUNTIME_RX_FRAME_CONFLICT:
    case SONIC_RUNTIME_RX_AUDIO_ERROR:
        if (button == SONIC_RUNTIME_BUTTON_OK) {
            if (runtime->state != SONIC_RUNTIME_RX_INCOMPLETE) {
                sonic_reassembly_reset(&runtime->reassembly);
                memset(&runtime->rx_result, 0, sizeof(runtime->rx_result));
                runtime->receive_attempt_frame_count = 0u;
            }
            runtime->error = SONIC_OK;
            runtime->state = runtime->state == SONIC_RUNTIME_RX_INCOMPLETE &&
                                     runtime->reassembly.active
                                 ? SONIC_RUNTIME_RX_ASSEMBLING
                                 : SONIC_RUNTIME_RX_LISTENING;
            sonic_runtime_start_rx(runtime, now_ms, actions);
        } else if (button == SONIC_RUNTIME_BUTTON_OK_LONG) {
            sonic_reassembly_reset(&runtime->reassembly);
            runtime->receive_attempt_frame_count = 0u;
            runtime->state = SONIC_RUNTIME_HOME;
        }
        break;
    case SONIC_RUNTIME_TX_MENU:
        if (button == SONIC_RUNTIME_BUTTON_UP) {
            runtime->preset_selection = (sonic_runtime_preset_t)
                ((runtime->preset_selection + SONIC_RUNTIME_PRESET_COUNT - 1u) %
                 SONIC_RUNTIME_PRESET_COUNT);
        } else if (button == SONIC_RUNTIME_BUTTON_DOWN) {
            runtime->preset_selection = (sonic_runtime_preset_t)
                ((runtime->preset_selection + 1u) % SONIC_RUNTIME_PRESET_COUNT);
        } else if (button == SONIC_RUNTIME_BUTTON_OK) {
            sonic_runtime_begin_new_send(runtime, actions);
        } else if (button == SONIC_RUNTIME_BUTTON_OK_LONG) {
            runtime->state = SONIC_RUNTIME_HOME;
        }
        break;
    case SONIC_RUNTIME_TX_PREVIEW:
        if (button == SONIC_RUNTIME_BUTTON_OK) {
            sonic_runtime_transmit_current(runtime, actions);
        } else if (button == SONIC_RUNTIME_BUTTON_OK_LONG) {
            runtime->state = SONIC_RUNTIME_TX_MENU;
        }
        break;
    case SONIC_RUNTIME_TX_PREPARING:
        break;
    case SONIC_RUNTIME_TX_PLAYING:
        if (button == SONIC_RUNTIME_BUTTON_OK_LONG) {
            actions->flags |= SONIC_RUNTIME_ACTION_CANCEL_TX;
        }
        break;
    case SONIC_RUNTIME_TX_COMPLETE:
    case SONIC_RUNTIME_TX_CANCELLED:
    case SONIC_RUNTIME_TX_AUDIO_ERROR:
        if (button == SONIC_RUNTIME_BUTTON_OK) {
            sonic_runtime_transmit_current(runtime, actions);
        } else if (button == SONIC_RUNTIME_BUTTON_OK_LONG) {
            runtime->state = SONIC_RUNTIME_TX_MENU;
        }
        break;
    case SONIC_RUNTIME_DIAGNOSTICS:
        if (button == SONIC_RUNTIME_BUTTON_UP) {
            runtime->diagnostics_page = (sonic_runtime_diagnostics_page_t)
                ((runtime->diagnostics_page + SONIC_RUNTIME_DIAGNOSTICS_PAGE_COUNT - 1u) %
                 SONIC_RUNTIME_DIAGNOSTICS_PAGE_COUNT);
        } else if (button == SONIC_RUNTIME_BUTTON_DOWN) {
            runtime->diagnostics_page = (sonic_runtime_diagnostics_page_t)
                ((runtime->diagnostics_page + 1u) %
                 SONIC_RUNTIME_DIAGNOSTICS_PAGE_COUNT);
        } else if (button == SONIC_RUNTIME_BUTTON_OK_LONG) {
            runtime->state = SONIC_RUNTIME_HOME;
        }
        break;
    }
}

void sonic_runtime_init(sonic_runtime_t *runtime, uint16_t message_id_seed,
                        uint32_t now_ms)
{
    if (runtime == NULL) {
        return;
    }
    memset(runtime, 0, sizeof(*runtime));
    runtime->state = SONIC_RUNTIME_HOME;
    runtime->home_selection = SONIC_RUNTIME_HOME_RECEIVE;
    runtime->preset_selection = SONIC_RUNTIME_PRESET_HELLO;
    runtime->diagnostics_page = SONIC_RUNTIME_DIAG_SYSTEM;
    runtime->current_message_id = sonic_message_id_normalize(message_id_seed);
    runtime->tx_message_id = runtime->current_message_id;
    runtime->last_valid_rx_ms = now_ms;
    runtime->tx_volume_percent = SONIC_RUNTIME_DEFAULT_VOLUME;
    runtime->resume_rx_after_tx = false;
    runtime->error = SONIC_OK;
    sonic_reassembly_reset(&runtime->reassembly);
}

sonic_error_t sonic_runtime_set_preset(sonic_runtime_t *runtime,
                                       sonic_runtime_preset_t preset,
                                       sonic_payload_type_t type,
                                       const uint8_t *payload,
                                       size_t payload_length)
{
    sonic_error_t error;
    sonic_runtime_preset_payload_t *entry;

    if (runtime == NULL || preset >= SONIC_RUNTIME_PRESET_COUNT ||
        payload_length > SONIC_MAX_MESSAGE_BYTES ||
        (payload == NULL && payload_length != 0u)) {
        return SONIC_INVALID_ARGUMENT;
    }
    error = sonic_payload_validate(type, payload, payload_length);
    if (error != SONIC_OK) {
        return error;
    }
    entry = &runtime->presets[preset];
    memset(entry, 0, sizeof(*entry));
    entry->valid = true;
    entry->available = true;
    entry->type = type;
    entry->length = (uint8_t) payload_length;
    if (payload_length != 0u) {
        memcpy(entry->bytes, payload, payload_length);
    }
    return SONIC_OK;
}

void sonic_runtime_set_tx_options(sonic_runtime_t *runtime,
                                  uint8_t volume_percent,
                                  bool resume_rx_after_tx)
{
    if (runtime == NULL) {
        return;
    }
    runtime->tx_volume_percent = volume_percent > 100u ? 100u : volume_percent;
    runtime->resume_rx_after_tx = resume_rx_after_tx;
}

void sonic_runtime_handle(sonic_runtime_t *runtime,
                         const sonic_runtime_event_t *event,
                         uint32_t now_ms,
                         sonic_runtime_actions_t *out_actions)
{
    if (out_actions != NULL) {
        sonic_actions_reset(out_actions);
    }
    if (runtime == NULL || event == NULL || out_actions == NULL) {
        return;
    }

    switch (event->type) {
    case SONIC_RUNTIME_EVENT_BUTTON:
        sonic_runtime_handle_button(runtime, event->data.button,
                                    now_ms, out_actions);
        break;
    case SONIC_RUNTIME_EVENT_TICK:
        sonic_runtime_handle_tick(runtime, now_ms, out_actions);
        break;
    case SONIC_RUNTIME_EVENT_RX_FRAME:
        sonic_runtime_handle_rx_frame(runtime, event, now_ms, out_actions);
        break;
    case SONIC_RUNTIME_EVENT_TX_STARTED:
        if (runtime->state == SONIC_RUNTIME_TX_PREPARING) {
            runtime->state = SONIC_RUNTIME_TX_PLAYING;
        }
        break;
    case SONIC_RUNTIME_EVENT_TX_PROGRESS:
        if (runtime->state == SONIC_RUNTIME_TX_PLAYING &&
            event->data.tx_progress.frame_count == runtime->current_frame_count &&
            event->data.tx_progress.frame_index < runtime->current_frame_count) {
            runtime->tx_frame_index = event->data.tx_progress.frame_index;
        }
        break;
    case SONIC_RUNTIME_EVENT_TX_COMPLETE:
        if (runtime->state == SONIC_RUNTIME_TX_PLAYING ||
            runtime->state == SONIC_RUNTIME_TX_PREPARING) {
            runtime->state = SONIC_RUNTIME_TX_COMPLETE;
            runtime->error = SONIC_OK;
            sonic_runtime_set_last_transfer(runtime, false,
                                            runtime->current_payload_type,
                                            runtime->current_message_id,
                                            runtime->current_frame_count,
                                            runtime->current_payload_length,
                                            event->data.tx_duration_ms,
                                            SONIC_RUNTIME_TRANSFER_COMPLETED,
                                            SONIC_OK);
        }
        break;
    case SONIC_RUNTIME_EVENT_TX_CANCELLED:
        if (runtime->state == SONIC_RUNTIME_TX_PLAYING ||
            runtime->state == SONIC_RUNTIME_TX_PREPARING) {
            runtime->state = SONIC_RUNTIME_TX_CANCELLED;
            runtime->error = SONIC_OK;
            sonic_runtime_set_last_transfer(runtime, false,
                                            runtime->current_payload_type,
                                            runtime->current_message_id,
                                            runtime->current_frame_count,
                                            runtime->current_payload_length,
                                            0u, SONIC_RUNTIME_TRANSFER_CANCELLED,
                                            SONIC_OK);
        }
        break;
    case SONIC_RUNTIME_EVENT_AUDIO_ERROR:
        if (runtime->state == SONIC_RUNTIME_RX_LISTENING ||
            runtime->state == SONIC_RUNTIME_RX_ASSEMBLING ||
            runtime->state == SONIC_RUNTIME_RX_PAUSED) {
            runtime->error = event->data.audio_error;
            sonic_runtime_stop_rx(runtime, out_actions);
            runtime->state = SONIC_RUNTIME_RX_AUDIO_ERROR;
            sonic_runtime_set_last_transfer(runtime, true,
                                            runtime->rx_result.type,
                                            runtime->rx_result.message_id,
                                            runtime->rx_result.fragment_count,
                                            runtime->rx_result.message_length,
                                            0u, SONIC_RUNTIME_TRANSFER_ERROR,
                                            runtime->error);
        } else if (runtime->state == SONIC_RUNTIME_TX_PREPARING ||
                   runtime->state == SONIC_RUNTIME_TX_PLAYING) {
            runtime->error = event->data.audio_error;
            runtime->state = SONIC_RUNTIME_TX_AUDIO_ERROR;
            sonic_runtime_set_last_transfer(runtime, false,
                                            runtime->current_payload_type,
                                            runtime->current_message_id,
                                            runtime->current_frame_count,
                                            runtime->current_payload_length,
                                            0u, SONIC_RUNTIME_TRANSFER_ERROR,
                                            runtime->error);
        }
        break;
    }
}

void sonic_runtime_get_view(const sonic_runtime_t *runtime,
                            sonic_runtime_view_t *out_view)
{
    if (runtime == NULL || out_view == NULL) {
        return;
    }
    memset(out_view, 0, sizeof(*out_view));
    out_view->state = runtime->state;
    out_view->home_selection = runtime->home_selection;
    out_view->preset_selection = runtime->preset_selection;
    out_view->diagnostics_page = runtime->diagnostics_page;
    out_view->message_id = runtime->current_message_id;
    out_view->payload_type = runtime->current_payload_type;
    out_view->payload_length = runtime->current_payload_length;
    out_view->frame_count = runtime->current_frame_count;
    out_view->frame_index = runtime->tx_frame_index;
    out_view->received_fragments = runtime->rx_result.received_fragments;
    out_view->expected_fragments = runtime->rx_result.fragment_count;
    out_view->microphone_active = runtime->microphone_active;
    out_view->error = runtime->error;
    memcpy(out_view->payload, runtime->current_payload,
           runtime->current_payload_length);
    out_view->last_transfer = runtime->last_transfer;
}
