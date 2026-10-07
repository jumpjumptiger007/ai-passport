#include "sonic_audio.h"

#include <cstring>

#include "bsp_audio.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace {

constexpr size_t kCommandQueueDepth = 4u;
constexpr UBaseType_t kWorkerPriority = 5u;
constexpr size_t kDiagnosticsPublishEveryRxBlocks = 16u;

enum class CommandType : uint8_t { StartRx, StopRx, Transmit, CancelTx };

struct Command {
    CommandType type = CommandType::StartRx;
    uint8_t frames[sonic_audio::kTxFrameLimit][sonic_codec::kFrameBytes]{};
    size_t frame_count = 0u;
    uint8_t volume_percent = 75u;
    bool resume_rx = false;
};

QueueHandle_t s_commands = nullptr;
QueueHandle_t s_diagnostics = nullptr;
TaskHandle_t s_worker = nullptr;
sonic_audio_event_callback_t s_callback = nullptr;
void *s_callback_context = nullptr;
sonic_audio::Engine s_engine;
bool s_initialized = false;

bool configure_audio(void *, uint32_t hz, uint8_t bits, uint8_t channels) {
    return bsp_audio_init() == ESP_OK && bsp_audio_set_format(hz, bits, channels) == ESP_OK;
}

bool set_audio_volume(void *, uint8_t percent) {
    bsp_audio_set_volume(percent);
    return true;
}

size_t largest_free_block(void *) {
    return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}

bool read_audio(void *, int16_t *samples, size_t sample_count) {
    return bsp_audio_read(samples, sample_count * sizeof(int16_t)) == ESP_OK;
}

bool write_audio(void *, const int16_t *samples, size_t sample_count) {
    return bsp_audio_write(samples, sample_count * sizeof(int16_t)) == ESP_OK;
}

uint64_t now_us(void *) {
    return static_cast<uint64_t>(esp_timer_get_time());
}

void publish_diagnostics() {
    sonic_audio::Diagnostics snapshot = s_engine.diagnostics();
    if (s_worker != nullptr) {
        snapshot.worker_stack_high_water_words = uxTaskGetStackHighWaterMark(s_worker);
    }
    if (s_diagnostics != nullptr) {
        (void) xQueueOverwrite(s_diagnostics, &snapshot);
    }
}

void emit_event(const sonic_audio::Event &event) {
    if (s_callback != nullptr && event.type != sonic_audio::EventType::None) {
        s_callback(&event, s_callback_context);
    }
}

void emit_state_change(sonic_audio::State before, sonic_audio::State after) {
    if (before != after) {
        sonic_audio::Event event;
        event.type = sonic_audio::EventType::StateChanged;
        event.state = after;
        emit_event(event);
    }
}

void process_command(const Command &command, bool *stop_after_tx) {
    const sonic_audio::State before = s_engine.state();
    sonic_audio::Event event;
    switch (command.type) {
        case CommandType::StartRx:
            event = s_engine.start_rx();
            emit_event(event);
            break;
        case CommandType::StopRx:
            if (s_engine.state() == sonic_audio::State::Tx ||
                s_engine.state() == sonic_audio::State::Stopping) {
                *stop_after_tx = true;
                return;
            }
            event = s_engine.stop_rx();
            emit_event(event);
            break;
        case CommandType::Transmit:
            event = s_engine.begin_tx(command.frames, command.frame_count,
                                      command.volume_percent, command.resume_rx);
            emit_event(event);
            break;
        case CommandType::CancelTx:
            // Cancellation is sampled by the worker at the next <=256-sample
            // write boundary, never from a concurrent task.
            break;
    }
    emit_state_change(before, s_engine.state());
}

void worker_task(void *) {
    sonic_audio::Operations operations;
    operations.configure = configure_audio;
    operations.set_volume = set_audio_volume;
    operations.largest_free_block = largest_free_block;
    operations.read = read_audio;
    operations.write = write_audio;
    operations.now_us = now_us;
    const sonic_error_t init_error = s_engine.initialize(
        operations, sonic_ggwave_profile::Candidate::AudibleFastest);
    if (init_error != SONIC_OK) {
        sonic_audio::Event error;
        error.type = sonic_audio::EventType::Error;
        error.state = sonic_audio::State::Error;
        error.error = init_error;
        emit_event(error);
        publish_diagnostics();
        s_worker = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    publish_diagnostics();
    emit_state_change(sonic_audio::State::Error, s_engine.state());

    bool cancel_requested = false;
    bool stop_after_tx = false;
    size_t blocks_since_snapshot = 0u;
    size_t last_tx_frame_index = 0u;
    for (;;) {
        Command command;
        if (s_engine.state() == sonic_audio::State::Rx) {
            if (xQueueReceive(s_commands, &command, 0) == pdTRUE) {
                process_command(command, &stop_after_tx);
            } else {
                const sonic_audio::State before = s_engine.state();
                const sonic_audio::Event event = s_engine.read_rx_once();
                if (event.type == sonic_audio::EventType::FrameReceived ||
                    event.type == sonic_audio::EventType::Error) {
                    emit_event(event);
                }
                emit_state_change(before, s_engine.state());
                if (++blocks_since_snapshot >= kDiagnosticsPublishEveryRxBlocks ||
                    event.type != sonic_audio::EventType::None) {
                    publish_diagnostics();
                    blocks_since_snapshot = 0u;
                }
            }
            continue;
        }

        if (s_engine.state() == sonic_audio::State::Tx ||
            s_engine.state() == sonic_audio::State::Stopping) {
            if (xQueueReceive(s_commands, &command, 0) == pdTRUE) {
                if (command.type == CommandType::CancelTx || command.type == CommandType::StopRx) {
                    cancel_requested = true;
                    stop_after_tx = stop_after_tx || command.type == CommandType::StopRx;
                } else {
                    sonic_audio::Event rejected;
                    rejected.type = sonic_audio::EventType::Error;
                    rejected.state = s_engine.state();
                    rejected.error = SONIC_INVALID_ARGUMENT;
                    emit_event(rejected);
                }
            }
            const sonic_audio::State before = s_engine.state();
            const sonic_audio::Event event = s_engine.tx_step(cancel_requested);
            cancel_requested = false;
            if (event.type == sonic_audio::EventType::Error ||
                event.type == sonic_audio::EventType::TxComplete ||
                event.type == sonic_audio::EventType::TxCancelled ||
                (event.type == sonic_audio::EventType::TxProgress &&
                 event.frame_index != last_tx_frame_index)) {
                emit_event(event);
                last_tx_frame_index = event.frame_index;
            }
            emit_state_change(before, s_engine.state());
            publish_diagnostics();
            if (event.type == sonic_audio::EventType::TxCancelled && stop_after_tx) {
                const sonic_audio::State before_stop = s_engine.state();
                emit_event(s_engine.stop_rx());
                emit_state_change(before_stop, s_engine.state());
                stop_after_tx = false;
            }
            if (event.type == sonic_audio::EventType::TxComplete && stop_after_tx) {
                const sonic_audio::State before_stop = s_engine.state();
                emit_event(s_engine.stop_rx());
                emit_state_change(before_stop, s_engine.state());
                stop_after_tx = false;
            }
            if (event.type == sonic_audio::EventType::Error) {
                vTaskDelay(1);
            }
            continue;
        }

        if (xQueueReceive(s_commands, &command, portMAX_DELAY) == pdTRUE) {
            process_command(command, &stop_after_tx);
            publish_diagnostics();
        }
    }
}

esp_err_t enqueue(const Command &command) {
    if (s_commands == nullptr || s_worker == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueSend(s_commands, &command, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

}  // namespace

extern "C" esp_err_t sonic_audio_init(sonic_audio_event_callback_t callback, void *context) {
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    s_callback = callback;
    s_callback_context = context;
    s_commands = xQueueCreate(kCommandQueueDepth, sizeof(Command));
    s_diagnostics = xQueueCreate(1u, sizeof(sonic_audio::Diagnostics));
    if (s_commands == nullptr || s_diagnostics == nullptr) {
        if (s_commands != nullptr) vQueueDelete(s_commands);
        if (s_diagnostics != nullptr) vQueueDelete(s_diagnostics);
        s_commands = nullptr;
        s_diagnostics = nullptr;
        return ESP_ERR_NO_MEM;
    }
    const BaseType_t created = xTaskCreate(worker_task, "sonic_audio", sonic_audio::kInitialWorkerStackBytes,
                                            nullptr, kWorkerPriority, &s_worker);
    if (created != pdPASS) {
        s_worker = nullptr;
        return ESP_ERR_NO_MEM;
    }
    s_initialized = true;
    return ESP_OK;
}

extern "C" esp_err_t sonic_audio_start_rx(void) {
    Command command;
    command.type = CommandType::StartRx;
    return enqueue(command);
}

extern "C" esp_err_t sonic_audio_stop_rx(void) {
    Command command;
    command.type = CommandType::StopRx;
    return enqueue(command);
}

extern "C" esp_err_t sonic_audio_transmit(
    const uint8_t frames[sonic_audio::kTxFrameLimit][sonic_codec::kFrameBytes],
    size_t frame_count,
    uint8_t volume_percent,
    bool resume_rx) {
    if (frames == nullptr || frame_count == 0u || frame_count > sonic_audio::kTxFrameLimit ||
        volume_percent > 100u) {
        return ESP_ERR_INVALID_ARG;
    }
    Command command;
    command.type = CommandType::Transmit;
    command.frame_count = frame_count;
    command.volume_percent = volume_percent;
    command.resume_rx = resume_rx;
    std::memcpy(command.frames, frames, frame_count * sonic_codec::kFrameBytes);
    return enqueue(command);
}

extern "C" esp_err_t sonic_audio_cancel_tx(void) {
    Command command;
    command.type = CommandType::CancelTx;
    return enqueue(command);
}

extern "C" bool sonic_audio_get_diagnostics(sonic_audio::Diagnostics *out_diagnostics) {
    return out_diagnostics != nullptr && s_diagnostics != nullptr &&
           xQueuePeek(s_diagnostics, out_diagnostics, 0) == pdTRUE;
}
