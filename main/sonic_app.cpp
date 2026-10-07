#include <atomic>
#include <cstdint>
#include <cstring>

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "sonic_audio.h"
#include "sonic_audio_engine.h"
#include "sonic_core.h"
#include "sonic_ggwave_profile.h"
#include "sonic_memory_gate.h"
#include "sonic_runtime.h"
#include "sonic_app_fingerprint.h"
#include "sonic_ui.h"

namespace {

constexpr char kTag[] = "sonic_app";
constexpr char kSonicVersion[] = "1.0.0";
constexpr char kGGWaveVersion[] = "0.4.3";
constexpr uint8_t kAcousticProfile = 1u;  // AUDIBLE_FASTEST candidate.
constexpr uint8_t kSpeakerVolumePercent = 75u;  // Current candidate; hardware calibration is pending.
constexpr UBaseType_t kControllerStackBytes = 6144u;
constexpr UBaseType_t kControllerPriority = 5u;
constexpr size_t kEventQueueDepth = 16u;
constexpr uint32_t kControllerTickMs = 50u;

#ifndef CONFIG_SONIC_DEMO_URL
#define CONFIG_SONIC_DEMO_URL "https://sonic.link/demo"
#endif

enum class AppEventType : uint8_t { Button, Audio };

struct AppEvent {
    AppEventType type = AppEventType::Button;
    bsp_btn_t button = BSP_BTN_OK;
    bsp_btn_ev_t button_event = BSP_BTN_CLICK;
    sonic_audio::Event audio{};
};

QueueHandle_t s_events = nullptr;
sonic_runtime_t s_runtime{};
sonic_ui_context_t s_ui_context{};
std::atomic<bool> s_queue_overflow{false};
uint64_t s_tx_started_ms = 0u;
bool s_memory_snapshot_logged = false;

void fill_build_fingerprint(uint8_t out[4], char printable[9])
{
    const esp_app_desc_t *description = esp_app_get_description();
    const uint8_t empty_prefix[4] = {0u, 0u, 0u, 0u};
    sonic_app_fingerprint_format(
        description != nullptr ? description->app_elf_sha256 : empty_prefix,
        out, printable);
}

const char *candidate_name(sonic_ggwave_profile::Candidate candidate)
{
    switch (candidate) {
    case sonic_ggwave_profile::Candidate::AudibleFastest:
        return "AUDIBLE_FASTEST";
    case sonic_ggwave_profile::Candidate::AudibleFast:
        return "AUDIBLE_FAST";
    default:
        return "UNKNOWN";
    }
}

sonic_error_t runtime_error_for(esp_err_t error, sonic_error_t operation_error)
{
    return error == ESP_OK ? SONIC_OK : operation_error;
}

void render_current_view()
{
    sonic_runtime_view_t view{};
    sonic_runtime_get_view(&s_runtime, &view);
    if (bsp_lvgl_lock(100)) {
        sonic_ui_render(&view, &s_ui_context);
        bsp_lvgl_unlock();
    }
}

void emit_runtime_audio_error(sonic_error_t error)
{
    sonic_runtime_event_t event{};
    sonic_runtime_actions_t actions{};
    event.type = SONIC_RUNTIME_EVENT_AUDIO_ERROR;
    event.data.audio_error = error;
    sonic_runtime_handle(&s_runtime, &event,
                         static_cast<uint32_t>(esp_timer_get_time() / 1000),
                         &actions);
    if ((actions.flags & SONIC_RUNTIME_ACTION_STOP_RX) != 0u) {
        const esp_err_t stop_error = sonic_audio_stop_rx();
        if (stop_error != ESP_OK) {
            ESP_LOGE(kTag, "Unable to enqueue audio stop: %s",
                     esp_err_to_name(stop_error));
        }
    }
    render_current_view();
}

void execute_actions(const sonic_runtime_actions_t &actions)
{
    esp_err_t result = ESP_OK;
    sonic_error_t operation_error = SONIC_AUDIO_INIT_FAILED;

    if ((actions.flags & SONIC_RUNTIME_ACTION_STOP_RX) != 0u) {
        result = sonic_audio_stop_rx();
        operation_error = SONIC_AUDIO_READ_FAILED;
    }
    if (result == ESP_OK &&
        (actions.flags & SONIC_RUNTIME_ACTION_START_RX) != 0u) {
        result = sonic_audio_start_rx();
        operation_error = SONIC_AUDIO_INIT_FAILED;
    }
    if (result == ESP_OK &&
        (actions.flags & SONIC_RUNTIME_ACTION_TRANSMIT) != 0u) {
        result = sonic_audio_transmit(actions.frames, actions.frame_count,
                                      actions.volume_percent,
                                      actions.resume_rx_after_tx);
        operation_error = SONIC_AUDIO_WRITE_FAILED;
    }
    if (result == ESP_OK &&
        (actions.flags & SONIC_RUNTIME_ACTION_CANCEL_TX) != 0u) {
        result = sonic_audio_cancel_tx();
        operation_error = SONIC_AUDIO_WRITE_FAILED;
    }
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Unable to enqueue audio action: %s",
                 esp_err_to_name(result));
        emit_runtime_audio_error(runtime_error_for(result, operation_error));
    }
}

void dispatch_runtime_event(const sonic_runtime_event_t &event,
                            uint32_t now_ms)
{
    sonic_runtime_actions_t actions{};
    sonic_runtime_handle(&s_runtime, &event, now_ms, &actions);
    execute_actions(actions);
    render_current_view();
}

void refresh_ui_context()
{
    sonic_audio::Diagnostics audio{};
    bool available = sonic_audio_get_diagnostics(&audio);
    int battery = bsp_battery_soc();

    s_ui_context.battery_percent = battery >= 0 && battery <= 100 ? battery : -1;
    s_ui_context.free_heap_bytes = static_cast<int>(esp_get_free_heap_size());
    s_ui_context.minimum_free_heap_bytes =
        static_cast<int>(esp_get_minimum_free_heap_size());
    s_ui_context.largest_free_block_bytes = static_cast<int>(
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    s_ui_context.audio_diagnostics_available = available;
    if (available) {
        s_ui_context.sample_rate_hz = audio.sample_rate_hz;
        s_ui_context.sample_bits = audio.sample_bits;
        s_ui_context.channels = audio.channels;
        s_ui_context.speaker_volume_percent = audio.speaker_volume_percent;
        s_ui_context.worker_stack_high_water_words =
            audio.worker_stack_high_water_words;
        s_ui_context.microphone_rms = audio.microphone_rms;
        s_ui_context.microphone_dbfs = audio.microphone_dbfs;
        s_ui_context.codec_heap_bytes = audio.codec_heap_bytes;
        s_ui_context.rx_average_us = audio.rx_processing_average_us;
        s_ui_context.rx_p99_us = audio.rx_processing_p99_us;
        s_ui_context.rx_maximum_us = audio.rx_processing_max_us;
        if (!s_memory_snapshot_logged) {
            sonic_memory_measurement_t measurement{};
            sonic_memory_result_t result{};
            measurement.pre_codec_measured = audio.codec_preflight_measured;
            measurement.codec_required_heap_bytes = audio.codec_required_heap_bytes;
            measurement.pre_codec_largest_free_block_bytes =
                audio.pre_codec_largest_free_block_bytes;
            measurement.runtime_free_heap_bytes =
                static_cast<size_t>(s_ui_context.free_heap_bytes);
            measurement.runtime_minimum_free_heap_bytes =
                static_cast<size_t>(s_ui_context.minimum_free_heap_bytes);
            measurement.runtime_largest_free_block_bytes =
                static_cast<size_t>(s_ui_context.largest_free_block_bytes);
            sonic_memory_evaluate(&measurement, &result);
            ESP_LOGI(kTag,
                     "SONIC_MEM candidate=%s pre_measured=%u pre_largest=%u "
                     "codec_required=%u pre_required=%u pre_ok=%u "
                     "runtime_free=%u runtime_min=%u runtime_largest=%u "
                     "codec_heap=%d runtime_free_ok=%u runtime_min_ok=%u "
                     "runtime_largest_ok=%u thresholds_ok=%u",
                     candidate_name(audio.candidate),
                     audio.codec_preflight_measured ? 1u : 0u,
                     static_cast<unsigned>(audio.pre_codec_largest_free_block_bytes),
                     static_cast<unsigned>(audio.codec_required_heap_bytes),
                     static_cast<unsigned>(result.codec_required_with_reserve_bytes),
                     result.pre_codec_passed ? 1u : 0u,
                     static_cast<unsigned>(measurement.runtime_free_heap_bytes),
                     static_cast<unsigned>(measurement.runtime_minimum_free_heap_bytes),
                     static_cast<unsigned>(measurement.runtime_largest_free_block_bytes),
                     audio.codec_heap_bytes,
                     result.runtime_free_passed ? 1u : 0u,
                     result.runtime_minimum_free_passed ? 1u : 0u,
                     result.runtime_largest_block_passed ? 1u : 0u,
                     result.thresholds_passed ? 1u : 0u);
            s_memory_snapshot_logged = true;
        }
        switch (audio.state) {
        case sonic_audio::State::Idle:
            s_ui_context.audio_state = SONIC_UI_AUDIO_IDLE;
            break;
        case sonic_audio::State::Rx:
            s_ui_context.audio_state = SONIC_UI_AUDIO_RX;
            break;
        case sonic_audio::State::Tx:
        case sonic_audio::State::Stopping:
            s_ui_context.audio_state = SONIC_UI_AUDIO_TX;
            break;
        case sonic_audio::State::Error:
            s_ui_context.audio_state = SONIC_UI_AUDIO_ERROR;
            break;
        }
    }
}

void map_audio_event(const sonic_audio::Event &audio,
                     sonic_runtime_event_t *runtime_event,
                     uint32_t now_ms)
{
    switch (audio.type) {
    case sonic_audio::EventType::FrameReceived:
        runtime_event->type = SONIC_RUNTIME_EVENT_RX_FRAME;
        runtime_event->data.rx_frame.bytes = audio.frame;
        runtime_event->data.rx_frame.length = sizeof(audio.frame);
        break;
    case sonic_audio::EventType::StateChanged:
        if (audio.state == sonic_audio::State::Tx) {
            s_tx_started_ms = now_ms;
            runtime_event->type = SONIC_RUNTIME_EVENT_TX_STARTED;
        }
        break;
    case sonic_audio::EventType::TxProgress:
        runtime_event->type = SONIC_RUNTIME_EVENT_TX_PROGRESS;
        runtime_event->data.tx_progress.frame_index =
            static_cast<uint8_t>(audio.frame_index);
        runtime_event->data.tx_progress.frame_count =
            static_cast<uint8_t>(audio.frame_count);
        break;
    case sonic_audio::EventType::TxComplete:
        runtime_event->type = SONIC_RUNTIME_EVENT_TX_COMPLETE;
        runtime_event->data.tx_duration_ms = static_cast<uint32_t>(
            now_ms >= s_tx_started_ms ? now_ms - s_tx_started_ms : 0u);
        break;
    case sonic_audio::EventType::TxCancelled:
        runtime_event->type = SONIC_RUNTIME_EVENT_TX_CANCELLED;
        break;
    case sonic_audio::EventType::Error:
        runtime_event->type = SONIC_RUNTIME_EVENT_AUDIO_ERROR;
        runtime_event->data.audio_error = audio.error;
        break;
    case sonic_audio::EventType::None:
        break;
    }
}

void process_button(const AppEvent &app_event, uint32_t now_ms)
{
    sonic_runtime_view_t view{};
    sonic_runtime_get_view(&s_runtime, &view);
    if (app_event.button_event != BSP_BTN_CLICK &&
        !(app_event.button == BSP_BTN_OK &&
          app_event.button_event == BSP_BTN_LONG)) {
        return;
    }
    if (view.state == SONIC_RUNTIME_RX_COMPLETE &&
        (app_event.button == BSP_BTN_UP || app_event.button == BSP_BTN_DOWN)) {
        if (sonic_ui_handle_page_button(&view,
                                       app_event.button == BSP_BTN_DOWN)) {
            render_current_view();
            return;
        }
    }

    sonic_runtime_event_t event{};
    event.type = SONIC_RUNTIME_EVENT_BUTTON;
    if (app_event.button_event == BSP_BTN_LONG) {
        event.data.button = SONIC_RUNTIME_BUTTON_OK_LONG;
    } else if (app_event.button == BSP_BTN_UP) {
        event.data.button = SONIC_RUNTIME_BUTTON_UP;
    } else if (app_event.button == BSP_BTN_DOWN) {
        event.data.button = SONIC_RUNTIME_BUTTON_DOWN;
    } else {
        event.data.button = SONIC_RUNTIME_BUTTON_OK;
    }
    dispatch_runtime_event(event, now_ms);
}

void process_audio(const sonic_audio::Event &audio, uint32_t now_ms)
{
    sonic_runtime_event_t event{};
    map_audio_event(audio, &event, now_ms);
    if (event.type != SONIC_RUNTIME_EVENT_BUTTON &&
        event.type != SONIC_RUNTIME_EVENT_TICK) {
        dispatch_runtime_event(event, now_ms);
    }
}

void controller_task(void *)
{
    AppEvent event{};
    uint32_t last_tick = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    uint32_t last_context_refresh = last_tick;

    for (;;) {
        const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
        if (xQueueReceive(s_events, &event, pdMS_TO_TICKS(kControllerTickMs)) ==
            pdTRUE) {
            if (event.type == AppEventType::Button) {
                process_button(event, now);
            } else {
                process_audio(event.audio, now);
            }
        }

        if (s_queue_overflow.exchange(false)) {
            ESP_LOGE(kTag, "Sonic event queue overflowed; stopping audio session");
            emit_runtime_audio_error(SONIC_AUDIO_READ_FAILED);
        }

        const uint32_t after_event =
            static_cast<uint32_t>(esp_timer_get_time() / 1000);
        if (static_cast<uint32_t>(after_event - last_tick) >= kControllerTickMs) {
            sonic_runtime_event_t tick{};
            tick.type = SONIC_RUNTIME_EVENT_TICK;
            dispatch_runtime_event(tick, after_event);
            last_tick = after_event;
        }
        if (static_cast<uint32_t>(after_event - last_context_refresh) >= 1000u) {
            refresh_ui_context();
            render_current_view();
            last_context_refresh = after_event;
        }
    }
}

void on_button(bsp_btn_t button, bsp_btn_ev_t event, void *)
{
    if (s_events == nullptr) {
        return;
    }
    if (event != BSP_BTN_CLICK &&
        !(button == BSP_BTN_OK && event == BSP_BTN_LONG)) {
        return;
    }
    AppEvent app_event{};
    app_event.type = AppEventType::Button;
    app_event.button = button;
    app_event.button_event = event;
    if (xQueueSend(s_events, &app_event, 0) != pdTRUE) {
        s_queue_overflow.store(true);
    }
}

void on_audio_event(const sonic_audio::Event *event, void *)
{
    if (event == nullptr || s_events == nullptr) {
        return;
    }
    AppEvent app_event{};
    app_event.type = AppEventType::Audio;
    app_event.audio = *event;
    if (xQueueSend(s_events, &app_event, 0) != pdTRUE) {
        s_queue_overflow.store(true);
    }
}

esp_err_t configure_presets()
{
    static const uint8_t hello[] = "Hello from AI Passport!";
    static const uint8_t test_token[] = {
        0x00u, 0x4Cu, 0x49u, 0x4Eu, 0x4Bu, 0xFFu, 0x80u, 0x00u,
        0x31u, 0x32u, 0x33u, 0x34u, 0xDEu, 0xADu, 0xBEu, 0xEFu
    };
    const char demo_url[] = CONFIG_SONIC_DEMO_URL;
    const int battery = bsp_battery_soc();
    uint8_t fingerprint[4];
    char fingerprint_text[9];
    fill_build_fingerprint(fingerprint, fingerprint_text);
    sonic_device_info_t device_info{};
    uint8_t device_info_bytes[SONIC_DEVICE_INFO_SIZE];

    device_info.schema_version = 1u;
    device_info.model_id = 1u;
    device_info.sonic_major = 1u;
    device_info.sonic_minor = 0u;
    device_info.sonic_patch = 0u;
    device_info.ggwave_major = 0u;
    device_info.ggwave_minor = 4u;
    device_info.ggwave_patch = 3u;
    device_info.battery_percent = battery >= 0 && battery <= 100
                                      ? static_cast<uint8_t>(battery) : 0xFFu;
    device_info.acoustic_profile = kAcousticProfile;
    device_info.max_message_bytes = SONIC_MAX_MESSAGE_BYTES;
    device_info.capability_bits = 0x7Fu;
    std::memcpy(device_info.build_fingerprint, fingerprint, sizeof(fingerprint));
    if (sonic_device_info_serialize(&device_info, device_info_bytes,
                                   sizeof(device_info_bytes)) != SONIC_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    if (sonic_runtime_set_preset(&s_runtime, SONIC_RUNTIME_PRESET_HELLO,
                                 SONIC_TYPE_TEXT, hello,
                                 sizeof(hello) - 1u) != SONIC_OK ||
        sonic_runtime_set_preset(&s_runtime, SONIC_RUNTIME_PRESET_DEMO_URL,
                                 SONIC_TYPE_URL,
                                 reinterpret_cast<const uint8_t *>(demo_url),
                                 std::strlen(demo_url)) != SONIC_OK ||
        sonic_runtime_set_preset(&s_runtime, SONIC_RUNTIME_PRESET_DEVICE_CARD,
                                 SONIC_TYPE_DEVICE_INFO, device_info_bytes,
                                 sizeof(device_info_bytes)) != SONIC_OK ||
        sonic_runtime_set_preset(&s_runtime, SONIC_RUNTIME_PRESET_TEST_TOKEN,
                                 SONIC_TYPE_TOKEN, test_token,
                                 sizeof(test_token)) != SONIC_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    std::memset(&s_ui_context, 0, sizeof(s_ui_context));
    s_ui_context.battery_percent = battery >= 0 && battery <= 100 ? battery : -1;
    s_ui_context.free_heap_bytes = -1;
    s_ui_context.minimum_free_heap_bytes = -1;
    s_ui_context.largest_free_block_bytes = -1;
    s_ui_context.codec_heap_bytes = -1;
    s_ui_context.audio_state = SONIC_UI_AUDIO_UNAVAILABLE;
    s_ui_context.acoustic_profile = kAcousticProfile;
    std::strncpy(s_ui_context.sonic_version, kSonicVersion,
                 sizeof(s_ui_context.sonic_version) - 1u);
    std::strncpy(s_ui_context.ggwave_version, kGGWaveVersion,
                 sizeof(s_ui_context.ggwave_version) - 1u);
    std::strncpy(s_ui_context.build_fingerprint, fingerprint_text,
                 sizeof(s_ui_context.build_fingerprint) - 1u);
    return ESP_OK;
}

void initialize_runtime()
{
    sonic_runtime_init(&s_runtime, static_cast<uint16_t>(esp_random()),
                       static_cast<uint32_t>(esp_timer_get_time() / 1000));
    sonic_runtime_set_tx_options(&s_runtime, kSpeakerVolumePercent, false);
}

}  // namespace

extern "C" void app_main(void)
{
    ESP_LOGI(kTag, "Starting Sonic Link Passport UI");
    if (bsp_i2c_init() != ESP_OK || bsp_display_init() != ESP_OK ||
        bsp_lvgl_init() == nullptr) {
        ESP_LOGE(kTag, "Display/LVGL initialization failed");
        return;
    }
    bsp_display_backlight(100u);

    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(kTag, "Battery gauge unavailable; hiding battery reading");
    }
    initialize_runtime();
    if (configure_presets() != ESP_OK) {
        ESP_LOGE(kTag, "Sonic Link preset configuration failed");
        return;
    }

    s_events = xQueueCreate(kEventQueueDepth, sizeof(AppEvent));
    if (s_events == nullptr) {
        ESP_LOGE(kTag, "Unable to allocate Sonic Link event queue");
        return;
    }
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(kTag, "Unable to acquire LVGL lock for Home screen");
        return;
    }
    sonic_ui_create();
    sonic_runtime_view_t initial_view{};
    sonic_runtime_get_view(&s_runtime, &initial_view);
    refresh_ui_context();
    sonic_ui_render(&initial_view, &s_ui_context);
    bsp_lvgl_unlock();

    const esp_err_t audio_init = sonic_audio_init(on_audio_event, nullptr);
    if (audio_init != ESP_OK) {
        ESP_LOGE(kTag, "Sonic audio worker initialization failed: %s",
                 esp_err_to_name(audio_init));
    }
    const esp_err_t button_init = bsp_button_init(on_button, nullptr);
    if (button_init != ESP_OK) {
        ESP_LOGE(kTag, "Button initialization failed: %s",
                 esp_err_to_name(button_init));
    }
    if (xTaskCreate(controller_task, "sonic_app", kControllerStackBytes,
                    nullptr, kControllerPriority, nullptr) != pdPASS) {
        ESP_LOGE(kTag, "Unable to create Sonic Link controller task");
        return;
    }
    ESP_LOGI(kTag, "Sonic Link ready; audio=%s buttons=%s",
             audio_init == ESP_OK ? "ready" : "unavailable",
             button_init == ESP_OK ? "ready" : "unavailable");
}
