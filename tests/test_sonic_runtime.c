#include "sonic_runtime.h"

#include <assert.h>
#include <string.h>

static void send_button(sonic_runtime_t *runtime, sonic_runtime_button_t button,
                        uint32_t now_ms, sonic_runtime_actions_t *actions)
{
    sonic_runtime_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = SONIC_RUNTIME_EVENT_BUTTON;
    event.data.button = button;
    sonic_runtime_handle(runtime, &event, now_ms, actions);
}

static void send_tick(sonic_runtime_t *runtime, uint32_t now_ms,
                      sonic_runtime_actions_t *actions)
{
    sonic_runtime_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = SONIC_RUNTIME_EVENT_TICK;
    sonic_runtime_handle(runtime, &event, now_ms, actions);
}

static void send_rx_frame(sonic_runtime_t *runtime,
                          const uint8_t frame[SONIC_FRAME_SIZE],
                          uint32_t now_ms,
                          sonic_runtime_actions_t *actions)
{
    sonic_runtime_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = SONIC_RUNTIME_EVENT_RX_FRAME;
    event.data.rx_frame.bytes = frame;
    event.data.rx_frame.length = SONIC_FRAME_SIZE;
    sonic_runtime_handle(runtime, &event, now_ms, actions);
}

static void start_receive(sonic_runtime_t *runtime, uint32_t now_ms,
                          sonic_runtime_actions_t *actions)
{
    sonic_runtime_init(runtime, 1u, now_ms);
    send_button(runtime, SONIC_RUNTIME_BUTTON_OK, now_ms, actions);
    assert(runtime->state == SONIC_RUNTIME_RX_LISTENING);
    assert(runtime->microphone_active);
    assert(actions->flags == SONIC_RUNTIME_ACTION_START_RX);
}

static void make_frames(sonic_payload_type_t type, uint16_t message_id,
                        const uint8_t *payload, size_t length,
                        uint8_t frames[SONIC_MAX_FRAGMENTS][SONIC_FRAME_SIZE],
                        size_t *frame_count)
{
    assert(sonic_message_fragment(type, message_id, payload, length, frames,
                                  SONIC_MAX_FRAGMENTS, frame_count) == SONIC_OK);
}

static void make_semantically_invalid_fragments(
    sonic_payload_type_t type, uint16_t message_id, const uint8_t *payload,
    size_t length, uint8_t frames[2][SONIC_FRAME_SIZE])
{
    size_t first_length = length / 2u;
    size_t second_length = length - first_length;

    assert(first_length > 0u && second_length > 0u);
    assert(first_length <= SONIC_FRAME_PAYLOAD_SIZE);
    assert(second_length <= SONIC_FRAME_PAYLOAD_SIZE);
    assert(sonic_frame_encode(frames[0], SONIC_FRAME_SIZE, type, message_id,
                              0u, 2u, payload, first_length) == SONIC_OK);
    assert(sonic_frame_encode(frames[1], SONIC_FRAME_SIZE, type, message_id,
                              1u, 2u, payload + first_length,
                              second_length) == SONIC_OK);
}

static void make_invalid_device_info_frame(
    uint16_t message_id, uint8_t frame[SONIC_FRAME_SIZE])
{
    sonic_device_info_t info = {
        .schema_version = 1u,
        .model_id = 1u,
        .sonic_major = 1u,
        .sonic_minor = 0u,
        .sonic_patch = 0u,
        .ggwave_major = 1u,
        .ggwave_minor = 0u,
        .ggwave_patch = 0u,
        .battery_percent = 50u,
        .acoustic_profile = 0u,
        .max_message_bytes = SONIC_MAX_MESSAGE_BYTES,
        .capability_bits = 1u,
        .build_fingerprint = {1u, 2u, 3u, 4u}
    };
    uint8_t payload[SONIC_DEVICE_INFO_SIZE];
    assert(sonic_device_info_serialize(&info, payload, sizeof(payload)) == SONIC_OK);
    assert(sonic_frame_encode(frame, SONIC_FRAME_SIZE, SONIC_TYPE_DEVICE_INFO,
                              message_id, 0u, 1u, payload,
                              sizeof(payload)) == SONIC_OK);
    frame[SONIC_FRAME_HEADER_SIZE] = 0u;
    {
        uint16_t crc = sonic_core_crc16(frame, SONIC_FRAME_CRC_OFFSET);
        frame[SONIC_FRAME_CRC_OFFSET] = (uint8_t)(crc >> 8);
        frame[SONIC_FRAME_CRC_OFFSET + 1u] = (uint8_t)crc;
    }
}

static void test_home_and_diagnostics(void)
{
    sonic_runtime_t runtime;
    sonic_runtime_actions_t actions;
    sonic_runtime_view_t view;

    sonic_runtime_init(&runtime, 0u, 0u);
    sonic_runtime_get_view(&runtime, &view);
    assert(view.state == SONIC_RUNTIME_HOME);
    assert(view.home_selection == SONIC_RUNTIME_HOME_RECEIVE);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK_LONG, 1u, &actions);
    assert(runtime.state == SONIC_RUNTIME_HOME);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_UP, 2u, &actions);
    assert(runtime.home_selection == SONIC_RUNTIME_HOME_DIAGNOSTICS);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_DOWN, 3u, &actions);
    assert(runtime.home_selection == SONIC_RUNTIME_HOME_RECEIVE);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_DOWN, 4u, &actions);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 5u, &actions);
    assert(runtime.state == SONIC_RUNTIME_TX_MENU);
    assert(actions.flags == 0u);

    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK_LONG, 6u, &actions);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_DOWN, 7u, &actions);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 8u, &actions);
    assert(runtime.state == SONIC_RUNTIME_DIAGNOSTICS);
    assert(runtime.diagnostics_page == SONIC_RUNTIME_DIAG_SYSTEM);
    assert(actions.flags == 0u);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_UP, 9u, &actions);
    assert(runtime.diagnostics_page == SONIC_RUNTIME_DIAG_LAST_TRANSFER);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_DOWN, 10u, &actions);
    assert(runtime.diagnostics_page == SONIC_RUNTIME_DIAG_SYSTEM);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK_LONG, 11u, &actions);
    assert(runtime.state == SONIC_RUNTIME_HOME);
    assert(actions.flags == 0u);
}

static void test_receive_pause_idle_and_back(void)
{
    sonic_runtime_t runtime;
    sonic_runtime_actions_t actions;

    start_receive(&runtime, 1000u, &actions);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 1001u, &actions);
    assert(runtime.state == SONIC_RUNTIME_RX_PAUSED);
    assert(!runtime.microphone_active);
    assert(actions.flags == SONIC_RUNTIME_ACTION_STOP_RX);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 1002u, &actions);
    assert(runtime.state == SONIC_RUNTIME_RX_LISTENING);
    assert(runtime.microphone_active);
    assert(actions.flags == SONIC_RUNTIME_ACTION_START_RX);
    send_tick(&runtime, 121001u, &actions);
    assert(runtime.state == SONIC_RUNTIME_RX_LISTENING);
    send_tick(&runtime, 121002u, &actions);
    assert(runtime.state == SONIC_RUNTIME_RX_PAUSED);
    assert(!runtime.microphone_active);
    assert(actions.flags == SONIC_RUNTIME_ACTION_STOP_RX);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK_LONG, 121002u, &actions);
    assert(runtime.state == SONIC_RUNTIME_HOME);
    assert(actions.flags == 0u);
}

static void test_receive_results_and_unsupported_glyph(void)
{
    static const uint8_t text[] = "Hello";
    static const uint8_t url[] = "https://example.com";
    static const uint8_t token[] = {0x00u, 0xFFu, 0x01u};
    static const uint8_t unsupported[] = {0xC3u, 0xA9u};
    const uint8_t *payloads[] = {text, url, token, unsupported};
    const size_t lengths[] = {sizeof(text) - 1u, sizeof(url) - 1u,
                              sizeof(token), sizeof(unsupported)};
    const sonic_payload_type_t types[] = {SONIC_TYPE_TEXT, SONIC_TYPE_URL,
                                          SONIC_TYPE_TOKEN, SONIC_TYPE_TEXT};

    for (size_t case_index = 0; case_index < 4u; ++case_index) {
        sonic_runtime_t runtime;
        sonic_runtime_actions_t actions;
        uint8_t frames[SONIC_MAX_FRAGMENTS][SONIC_FRAME_SIZE];
        size_t frame_count = 0u;
        make_frames(types[case_index], (uint16_t)(10u + case_index),
                    payloads[case_index], lengths[case_index], frames,
                    &frame_count);
        assert(frame_count == 1u);
        start_receive(&runtime, 100u, &actions);
        send_rx_frame(&runtime, frames[0], 101u, &actions);
        if (case_index == 3u) {
            assert(runtime.state == SONIC_RUNTIME_RX_UNSUPPORTED_GLYPH);
            assert(runtime.error == SONIC_UNSUPPORTED_GLYPH);
            assert(runtime.last_transfer.result == SONIC_RUNTIME_TRANSFER_ERROR);
        } else {
            assert(runtime.state == SONIC_RUNTIME_RX_COMPLETE);
            assert(runtime.last_transfer.result ==
                   SONIC_RUNTIME_TRANSFER_COMPLETED);
            assert(runtime.last_transfer.type == types[case_index]);
            assert(runtime.current_payload_length == lengths[case_index]);
            assert(memcmp(runtime.current_payload, payloads[case_index],
                          lengths[case_index]) == 0);
        }
        assert(actions.flags == SONIC_RUNTIME_ACTION_STOP_RX);
        assert(!runtime.microphone_active);
        send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 102u, &actions);
        assert(runtime.state == SONIC_RUNTIME_RX_LISTENING);
        assert(actions.flags == SONIC_RUNTIME_ACTION_START_RX);
    }
}

static void test_receive_semantic_errors_are_not_audio_errors(void)
{
    static const uint8_t invalid_utf8[] = {0xC0u, 'x'};
    static const uint8_t invalid_url[] = "ftp://example.com";
    static const uint8_t invalid_device_info[SONIC_DEVICE_INFO_SIZE] = {0u};
    const uint8_t *payloads[] = {invalid_utf8, invalid_url,
                                 invalid_device_info};
    const size_t lengths[] = {sizeof(invalid_utf8), sizeof(invalid_url) - 1u,
                              sizeof(invalid_device_info)};
    const sonic_payload_type_t types[] = {SONIC_TYPE_TEXT, SONIC_TYPE_URL,
                                          SONIC_TYPE_DEVICE_INFO};
    const sonic_error_t errors[] = {SONIC_INVALID_UTF8, SONIC_INVALID_URL,
                                    SONIC_INVALID_DEVICE_INFO};

    for (size_t case_index = 0; case_index < 3u; ++case_index) {
        sonic_runtime_t runtime;
        sonic_runtime_actions_t actions;
        uint8_t frame[SONIC_FRAME_SIZE];
        uint8_t frames[2][SONIC_FRAME_SIZE];
        start_receive(&runtime, 100u, &actions);
        if (case_index < 2u) {
            make_semantically_invalid_fragments(
                types[case_index], (uint16_t)(0x5000u + case_index),
                payloads[case_index], lengths[case_index], frames);
            send_rx_frame(&runtime, frames[0], 101u, &actions);
            assert(runtime.state == SONIC_RUNTIME_RX_ASSEMBLING);
            send_rx_frame(&runtime, frames[1], 102u, &actions);
        } else {
            make_invalid_device_info_frame((uint16_t)(0x5000u + case_index),
                                           frame);
            send_rx_frame(&runtime, frame, 102u, &actions);
        }
        assert(runtime.state == SONIC_RUNTIME_RX_INVALID_MESSAGE);
        assert(runtime.state != SONIC_RUNTIME_RX_AUDIO_ERROR);
        assert(runtime.error == errors[case_index]);
        assert(runtime.last_transfer.error == errors[case_index]);
        assert(runtime.last_transfer.message_id == (uint16_t)(0x5000u + case_index));
        assert(runtime.last_transfer.result == SONIC_RUNTIME_TRANSFER_ERROR);
        assert(actions.flags == SONIC_RUNTIME_ACTION_STOP_RX);
        assert(!runtime.microphone_active);

        send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 103u, &actions);
        assert(runtime.state == SONIC_RUNTIME_RX_LISTENING);
        assert(actions.flags == SONIC_RUNTIME_ACTION_START_RX);
        send_button(&runtime, SONIC_RUNTIME_BUTTON_OK_LONG, 104u, &actions);
        assert(runtime.state == SONIC_RUNTIME_HOME);
    }
}

static void test_rx_message_id_does_not_change_tx_sequence(void)
{
    static const uint8_t received[] = "remote";
    static const uint8_t outgoing[] = "local";
    uint8_t frames[SONIC_MAX_FRAGMENTS][SONIC_FRAME_SIZE];
    size_t frame_count = 0u;
    sonic_runtime_t runtime;
    sonic_runtime_actions_t actions;

    sonic_runtime_init(&runtime, 0x1234u, 0u);
    assert(sonic_runtime_set_preset(&runtime, SONIC_RUNTIME_PRESET_HELLO,
                                    SONIC_TYPE_TEXT, outgoing,
                                    sizeof(outgoing) - 1u) == SONIC_OK);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 1u, &actions);
    make_frames(SONIC_TYPE_TEXT, 0x7777u, received, sizeof(received) - 1u,
                frames, &frame_count);
    assert(frame_count == 1u);
    send_rx_frame(&runtime, frames[0], 2u, &actions);
    assert(runtime.state == SONIC_RUNTIME_RX_COMPLETE);
    assert(runtime.current_message_id == 0x7777u);

    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK_LONG, 3u, &actions);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_DOWN, 4u, &actions);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 5u, &actions);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 6u, &actions);
    assert(runtime.state == SONIC_RUNTIME_TX_PREVIEW);
    assert(runtime.current_message_id == 0x1234u);
    assert(runtime.tx_message_id == 0x1234u);

    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK_LONG, 7u, &actions);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 8u, &actions);
    assert(runtime.state == SONIC_RUNTIME_TX_PREVIEW);
    assert(runtime.current_message_id == 0x1235u);
    assert(runtime.tx_message_id == 0x1235u);

    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 9u, &actions);
    assert(runtime.state == SONIC_RUNTIME_TX_PREPARING);
    {
        sonic_runtime_event_t event;
        memset(&event, 0, sizeof(event));
        event.type = SONIC_RUNTIME_EVENT_TX_STARTED;
        sonic_runtime_handle(&runtime, &event, 10u, &actions);
        event.type = SONIC_RUNTIME_EVENT_TX_COMPLETE;
        event.data.tx_duration_ms = 500u;
        sonic_runtime_handle(&runtime, &event, 11u, &actions);
    }
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 12u, &actions);
    assert(runtime.state == SONIC_RUNTIME_TX_PREPARING);
    assert(runtime.current_message_id == 0x1235u);
    assert(actions.flags == SONIC_RUNTIME_ACTION_TRANSMIT);
}

static void test_receive_three_fragment_progress_and_device_info(void)
{
    uint8_t token[70];
    uint8_t frames[SONIC_MAX_FRAGMENTS][SONIC_FRAME_SIZE];
    uint8_t device_info_bytes[SONIC_DEVICE_INFO_SIZE];
    size_t frame_count = 0u;
    sonic_device_info_t device_info = {
        .schema_version = 1u,
        .model_id = 1u,
        .sonic_major = 1u,
        .sonic_minor = 2u,
        .sonic_patch = 3u,
        .ggwave_major = 4u,
        .ggwave_minor = 5u,
        .ggwave_patch = 6u,
        .battery_percent = 75u,
        .acoustic_profile = 0u,
        .max_message_bytes = SONIC_MAX_MESSAGE_BYTES,
        .capability_bits = 0x7Fu,
        .build_fingerprint = {0xDEu, 0xADu, 0xBEu, 0xEFu}
    };
    sonic_runtime_t runtime;
    sonic_runtime_actions_t actions;

    for (size_t i = 0; i < sizeof(token); ++i) {
        token[i] = (uint8_t)i;
    }
    make_frames(SONIC_TYPE_TOKEN, 0x3003u, token, sizeof(token), frames,
                &frame_count);
    assert(frame_count == 3u);
    start_receive(&runtime, 100u, &actions);
    send_rx_frame(&runtime, frames[0], 101u, &actions);
    assert(runtime.rx_result.received_fragments == 1u);
    send_rx_frame(&runtime, frames[1], 102u, &actions);
    assert(runtime.rx_result.received_fragments == 2u);
    send_rx_frame(&runtime, frames[2], 103u, &actions);
    assert(runtime.state == SONIC_RUNTIME_RX_COMPLETE);
    assert(runtime.current_payload_length == sizeof(token));
    assert(memcmp(runtime.current_payload, token, sizeof(token)) == 0);

    assert(sonic_device_info_serialize(&device_info, device_info_bytes,
                                       sizeof(device_info_bytes)) == SONIC_OK);
    make_frames(SONIC_TYPE_DEVICE_INFO, 0x3004u, device_info_bytes,
                sizeof(device_info_bytes), frames, &frame_count);
    assert(frame_count == 1u);
    start_receive(&runtime, 200u, &actions);
    send_rx_frame(&runtime, frames[0], 201u, &actions);
    assert(runtime.state == SONIC_RUNTIME_RX_COMPLETE);
    assert(runtime.current_payload_type == SONIC_TYPE_DEVICE_INFO);
    assert(runtime.current_payload_length == SONIC_DEVICE_INFO_SIZE);
    assert(memcmp(runtime.current_payload, device_info_bytes,
                  SONIC_DEVICE_INFO_SIZE) == 0);
}

static void test_receive_assembly_duplicate_conflict_and_retry(void)
{
    static const uint8_t message[] =
        "01234567890123456789012345678901";
    static const uint8_t conflict_message[] =
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    static const uint8_t unrelated_message[] =
        "unrelated frame must not start!!";
    sonic_runtime_t runtime;
    sonic_runtime_actions_t actions;
    uint8_t frames[SONIC_MAX_FRAGMENTS][SONIC_FRAME_SIZE];
    uint8_t other[SONIC_MAX_FRAGMENTS][SONIC_FRAME_SIZE];
    uint8_t unrelated[SONIC_MAX_FRAGMENTS][SONIC_FRAME_SIZE];
    size_t frame_count = 0u;
    size_t other_count = 0u;
    size_t unrelated_count = 0u;

    make_frames(SONIC_TYPE_TEXT, 0x1001u, message, sizeof(message) - 1u,
                frames, &frame_count);
    make_frames(SONIC_TYPE_TEXT, 0x1001u, conflict_message,
                sizeof(conflict_message) - 1u, other, &other_count);
    make_frames(SONIC_TYPE_TOKEN, 0x2002u, (const uint8_t *) unrelated_message,
                sizeof(unrelated_message) - 1u, unrelated, &unrelated_count);
    assert(frame_count == 2u && other_count == 2u && unrelated_count == 2u);

    start_receive(&runtime, 1000u, &actions);
    send_rx_frame(&runtime, unrelated[1], 1001u, &actions);
    assert(runtime.state == SONIC_RUNTIME_RX_LISTENING);
    assert(runtime.rx_result.received_fragments == 0u);
    send_rx_frame(&runtime, frames[0], 1002u, &actions);
    assert(runtime.state == SONIC_RUNTIME_RX_ASSEMBLING);
    assert(runtime.rx_result.received_fragments == 1u);
    send_rx_frame(&runtime, frames[0], 1003u, &actions);
    assert(runtime.state == SONIC_RUNTIME_RX_ASSEMBLING);
    assert(runtime.rx_result.received_fragments == 1u);
    send_tick(&runtime, 11001u, &actions);
    assert(runtime.state == SONIC_RUNTIME_RX_ASSEMBLING);
    assert(runtime.microphone_active);
    assert(actions.flags == 0u);
    send_rx_frame(&runtime, frames[1], 1004u, &actions);
    assert(runtime.state == SONIC_RUNTIME_RX_COMPLETE);
    assert(runtime.current_payload_length == sizeof(message) - 1u);
    assert(memcmp(runtime.current_payload, message, sizeof(message) - 1u) == 0);

    start_receive(&runtime, 2000u, &actions);
    send_rx_frame(&runtime, frames[0], 2001u, &actions);
    send_rx_frame(&runtime, other[0], 2002u, &actions);
    assert(runtime.state == SONIC_RUNTIME_RX_FRAME_CONFLICT);
    assert(runtime.error == SONIC_FRAGMENT_CONFLICT);
    assert(actions.flags == SONIC_RUNTIME_ACTION_STOP_RX);
    assert(!runtime.microphone_active);

    start_receive(&runtime, 3000u, &actions);
    send_rx_frame(&runtime, frames[0], 3010u, &actions);
    send_tick(&runtime, 13010u, &actions);
    assert(runtime.state == SONIC_RUNTIME_RX_INCOMPLETE);
    assert(!runtime.microphone_active);
    assert(actions.flags == SONIC_RUNTIME_ACTION_STOP_RX);
    assert(runtime.last_transfer.result == SONIC_RUNTIME_TRANSFER_INCOMPLETE);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 14000u, &actions);
    assert(runtime.state == SONIC_RUNTIME_RX_ASSEMBLING);
    assert(runtime.microphone_active);
    assert(actions.flags == SONIC_RUNTIME_ACTION_START_RX);
    send_rx_frame(&runtime, frames[1], 15000u, &actions);
    assert(runtime.state == SONIC_RUNTIME_RX_COMPLETE);
    assert(memcmp(runtime.current_payload, message, sizeof(message) - 1u) == 0);

    start_receive(&runtime, 4000u, &actions);
    send_rx_frame(&runtime, frames[0], 4010u, &actions);
    send_tick(&runtime, 14010u, &actions);
    assert(runtime.state == SONIC_RUNTIME_RX_INCOMPLETE);
    assert(runtime.reassembly.active);
    send_tick(&runtime, 34010u, &actions);
    assert(runtime.state == SONIC_RUNTIME_RX_INCOMPLETE);
    assert(!runtime.reassembly.active);
    sonic_runtime_get_view(&runtime, &(sonic_runtime_view_t){0});
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 304011u, &actions);
    assert(runtime.state == SONIC_RUNTIME_RX_LISTENING);
    assert(runtime.rx_result.received_fragments == 0u);
}

static void test_send_preview_cancel_retry_and_id_wrap(void)
{
    static const uint8_t payload[] = {
        's','a','m','e','-','i','d','-','r','e','t','r','y',0u,0xFFu,
        0x01u,0x02u,0x03u,0x04u,0x05u,0x06u,0x07u,0x08u,0x09u,0x0Au,
        0x0Bu,0x0Cu,0x0Du,0x0Eu,0x0Fu,0x10u,0x11u,0x12u,0x13u,0x14u
    };
    sonic_runtime_t runtime;
    sonic_runtime_actions_t actions;
    uint8_t expected[SONIC_MAX_FRAGMENTS][SONIC_FRAME_SIZE];
    size_t expected_count = 0u;
    uint16_t first_id;
    sonic_runtime_event_t event;

    sonic_runtime_init(&runtime, 0xFFFFu, 0u);
    assert(sonic_runtime_set_preset(&runtime, SONIC_RUNTIME_PRESET_TEST_TOKEN,
                                    SONIC_TYPE_TOKEN, payload,
                                    sizeof(payload)) == SONIC_OK);
    sonic_runtime_set_tx_options(&runtime, 60u, false);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_DOWN, 1u, &actions);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 2u, &actions);
    assert(runtime.state == SONIC_RUNTIME_TX_MENU);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_UP, 3u, &actions);
    assert(runtime.preset_selection == SONIC_RUNTIME_PRESET_TEST_TOKEN);
    {
        sonic_runtime_view_t view;
        sonic_runtime_get_view(&runtime, &view);
        assert(view.state == SONIC_RUNTIME_TX_MENU);
        assert(view.payload_type == SONIC_TYPE_TOKEN);
        assert(view.payload_length == sizeof(payload));
        assert(view.frame_count == 2u);
        assert(memcmp(view.payload, payload, sizeof(payload)) == 0);
    }
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 4u, &actions);
    assert(runtime.state == SONIC_RUNTIME_TX_PREVIEW);
    assert(runtime.current_message_id == 0xFFFFu);
    assert(runtime.current_frame_count == 2u);
    assert(runtime.current_payload_type == SONIC_TYPE_TOKEN);
    first_id = runtime.current_message_id;
    make_frames(SONIC_TYPE_TOKEN, first_id, payload, sizeof(payload),
                expected, &expected_count);
    assert(expected_count == runtime.current_frame_count);
    assert(memcmp(expected, runtime.current_frames,
                  expected_count * SONIC_FRAME_SIZE) == 0);

    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 5u, &actions);
    assert(runtime.state == SONIC_RUNTIME_TX_PREPARING);
    assert(actions.flags == SONIC_RUNTIME_ACTION_TRANSMIT);
    assert(actions.frame_count == expected_count);
    assert(actions.volume_percent == 60u);
    assert(!actions.resume_rx_after_tx);
    assert(memcmp(actions.frames, expected,
                  expected_count * SONIC_FRAME_SIZE) == 0);

    memset(&event, 0, sizeof(event));
    event.type = SONIC_RUNTIME_EVENT_TX_STARTED;
    sonic_runtime_handle(&runtime, &event, 6u, &actions);
    assert(runtime.state == SONIC_RUNTIME_TX_PLAYING);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_DOWN, 7u, &actions);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 8u, &actions);
    assert(runtime.state == SONIC_RUNTIME_TX_PLAYING);
    assert(actions.flags == 0u);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK_LONG, 9u, &actions);
    assert(runtime.state == SONIC_RUNTIME_TX_PLAYING);
    assert(actions.flags == SONIC_RUNTIME_ACTION_CANCEL_TX);

    memset(&event, 0, sizeof(event));
    event.type = SONIC_RUNTIME_EVENT_TX_CANCELLED;
    sonic_runtime_handle(&runtime, &event, 10u, &actions);
    assert(runtime.state == SONIC_RUNTIME_TX_CANCELLED);
    assert(runtime.last_transfer.result == SONIC_RUNTIME_TRANSFER_CANCELLED);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 11u, &actions);
    assert(runtime.state == SONIC_RUNTIME_TX_PREPARING);
    assert(runtime.current_message_id == first_id);
    assert(actions.flags == SONIC_RUNTIME_ACTION_TRANSMIT);
    assert(memcmp(actions.frames, expected,
                  expected_count * SONIC_FRAME_SIZE) == 0);

    memset(&event, 0, sizeof(event));
    event.type = SONIC_RUNTIME_EVENT_TX_STARTED;
    sonic_runtime_handle(&runtime, &event, 12u, &actions);
    memset(&event, 0, sizeof(event));
    event.type = SONIC_RUNTIME_EVENT_TX_PROGRESS;
    event.data.tx_progress.frame_index = 1u;
    event.data.tx_progress.frame_count = 2u;
    sonic_runtime_handle(&runtime, &event, 13u, &actions);
    assert(runtime.tx_frame_index == 1u);
    memset(&event, 0, sizeof(event));
    event.type = SONIC_RUNTIME_EVENT_TX_COMPLETE;
    event.data.tx_duration_ms = 2345u;
    sonic_runtime_handle(&runtime, &event, 14u, &actions);
    assert(runtime.state == SONIC_RUNTIME_TX_COMPLETE);
    assert(runtime.last_transfer.result == SONIC_RUNTIME_TRANSFER_COMPLETED);
    assert(runtime.last_transfer.duration_ms == 2345u);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 15u, &actions);
    assert(runtime.state == SONIC_RUNTIME_TX_PREPARING);
    assert(runtime.current_message_id == first_id);
    assert(memcmp(actions.frames, expected,
                  expected_count * SONIC_FRAME_SIZE) == 0);
    memset(&event, 0, sizeof(event));
    event.type = SONIC_RUNTIME_EVENT_TX_STARTED;
    sonic_runtime_handle(&runtime, &event, 15u, &actions);
    event.type = SONIC_RUNTIME_EVENT_TX_COMPLETE;
    event.data.tx_duration_ms = 2000u;
    sonic_runtime_handle(&runtime, &event, 16u, &actions);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK_LONG, 17u, &actions);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 18u, &actions);
    assert(runtime.state == SONIC_RUNTIME_TX_PREVIEW);
    assert(runtime.current_message_id == 1u);
    assert(runtime.current_message_id != 0u);
}

static void test_send_error_and_receive_error(void)
{
    static const uint8_t payload[] = "diagnostic";
    sonic_runtime_t runtime;
    sonic_runtime_actions_t actions;
    sonic_runtime_event_t event;

    sonic_runtime_init(&runtime, 2u, 0u);
    assert(sonic_runtime_set_preset(&runtime, SONIC_RUNTIME_PRESET_HELLO,
                                    SONIC_TYPE_TEXT, payload,
                                    sizeof(payload) - 1u) == SONIC_OK);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_DOWN, 1u, &actions);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 2u, &actions);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 3u, &actions);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 4u, &actions);
    assert(runtime.state == SONIC_RUNTIME_TX_PREPARING);
    memset(&event, 0, sizeof(event));
    event.type = SONIC_RUNTIME_EVENT_AUDIO_ERROR;
    event.data.audio_error = SONIC_AUDIO_WRITE_FAILED;
    sonic_runtime_handle(&runtime, &event, 5u, &actions);
    assert(runtime.state == SONIC_RUNTIME_TX_AUDIO_ERROR);
    assert(runtime.error == SONIC_AUDIO_WRITE_FAILED);
    assert(runtime.last_transfer.error == SONIC_AUDIO_WRITE_FAILED);
    send_button(&runtime, SONIC_RUNTIME_BUTTON_OK, 6u, &actions);
    assert(runtime.state == SONIC_RUNTIME_TX_PREPARING);
    assert(runtime.current_message_id == 2u);

    start_receive(&runtime, 7u, &actions);
    memset(&event, 0, sizeof(event));
    event.type = SONIC_RUNTIME_EVENT_AUDIO_ERROR;
    event.data.audio_error = SONIC_AUDIO_READ_FAILED;
    sonic_runtime_handle(&runtime, &event, 8u, &actions);
    assert(runtime.state == SONIC_RUNTIME_RX_AUDIO_ERROR);
    assert(!runtime.microphone_active);
    assert(actions.flags == SONIC_RUNTIME_ACTION_STOP_RX);
    assert(runtime.last_transfer.error == SONIC_AUDIO_READ_FAILED);
}

int main(void)
{
    test_home_and_diagnostics();
    test_receive_pause_idle_and_back();
    test_receive_results_and_unsupported_glyph();
    test_receive_semantic_errors_are_not_audio_errors();
    test_rx_message_id_does_not_change_tx_sequence();
    test_receive_three_fragment_progress_and_device_info();
    test_receive_assembly_duplicate_conflict_and_retry();
    test_send_preview_cancel_retry_and_id_wrap();
    test_send_error_and_receive_error();
    return 0;
}
