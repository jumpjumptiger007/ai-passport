#include "sonic_ui.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "sonic_core.h"

#define UI_COLOR_SCREEN 0xF6F7F4u
#define UI_COLOR_SURFACE 0xFFFFFFu
#define UI_COLOR_BLUE 0x2563EBu
#define UI_COLOR_BLUE_DARK 0x1746A2u
#define UI_COLOR_BLUE_TINT 0xEAF1FFu
#define UI_COLOR_INK 0x111318u
#define UI_COLOR_MUTED 0x626D77u
#define UI_COLOR_LINE 0xD6DCE2u
#define UI_COLOR_GREEN 0x248A52u
#define UI_COLOR_GREEN_TINT 0xEAF7EFu
#define UI_COLOR_AMBER 0xA35D0Bu
#define UI_COLOR_AMBER_TINT 0xFFF4E5u
#define UI_COLOR_RED 0xB33A35u
#define UI_COLOR_RED_TINT 0xFDEEEDu
#define UI_COLOR_GRAY 0x7B858Fu
#define UI_COLOR_GRAY_TINT 0xE9EDF0u

static lv_obj_t *s_screen;
static lv_obj_t *s_header_title;
static lv_obj_t *s_header_page;
static lv_obj_t *s_header_battery;
static lv_obj_t *s_content;
static lv_obj_t *s_footer_labels[3];
static lv_obj_t *s_meter_bars[5];
static bool s_meter_visible;
static sonic_runtime_view_t s_previous_view;
static sonic_ui_context_t s_previous_context;
static bool s_has_previous;
static size_t s_page_index;
static size_t s_previous_page_index;
static uint16_t s_page_message_id;
static sonic_runtime_state_t s_page_state;
static uint8_t s_page_payload_length;
static sonic_payload_type_t s_page_payload_type;

static lv_color_t color(uint32_t value)
{
    return lv_color_hex(value);
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                            const lv_font_t *font, uint32_t text_color,
                            int32_t width, int32_t height)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text != NULL ? text : "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color(text_color), 0);
    lv_obj_set_style_text_line_space(label, 1, 0);
    lv_obj_set_width(label, width);
    if (height > 0) {
        lv_obj_set_height(label, height);
    }
    return label;
}

static lv_obj_t *make_card(lv_obj_t *parent, int32_t x, int32_t y,
                           int32_t width, int32_t height, uint32_t background)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, width, height);
    lv_obj_set_style_bg_color(card, color(background), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, color(UI_COLOR_LINE), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 9, 0);
    return card;
}

static void label_at(lv_obj_t *parent, const char *text,
                     const lv_font_t *font, uint32_t text_color,
                     int32_t x, int32_t y, int32_t width, int32_t height)
{
    lv_obj_t *label = make_label(parent, text, font, text_color, width, height);
    lv_obj_set_pos(label, x, y);
}

static void set_footer(const char *left, const char *center, const char *right)
{
    const char *values[3] = {left, center, right};
    for (size_t i = 0u; i < 3u; ++i) {
        lv_label_set_text(s_footer_labels[i], values[i] != NULL ? values[i] : "");
    }
}

static void set_chip(lv_obj_t *parent, const char *text, uint32_t foreground,
                     uint32_t background)
{
    lv_obj_t *chip = lv_label_create(parent);
    lv_label_set_text(chip, text);
    lv_obj_set_style_text_font(chip, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(chip, color(foreground), 0);
    lv_obj_set_style_bg_color(chip, color(background), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(chip, 8, 0);
    lv_obj_set_style_pad_ver(chip, 2, 0);
    lv_obj_align(chip, LV_ALIGN_TOP_LEFT, 14, 10);
}

static void set_header(const sonic_runtime_view_t *view,
                       const sonic_ui_context_t *context)
{
    char battery[12];
    const char *title = "Sonic Link";

    if (view->state >= SONIC_RUNTIME_RX_LISTENING &&
        view->state <= SONIC_RUNTIME_RX_FRAME_CONFLICT) {
        title = "Receive";
    } else if (view->state >= SONIC_RUNTIME_TX_MENU &&
               view->state <= SONIC_RUNTIME_TX_AUDIO_ERROR) {
        title = "Send";
    } else if (view->state == SONIC_RUNTIME_DIAGNOSTICS) {
        title = "Diagnostics";
    }
    lv_label_set_text(s_header_title, title);
    if (view->state == SONIC_RUNTIME_DIAGNOSTICS) {
        lv_label_set_text_fmt(s_header_page, "%u/4",
                              (unsigned)view->diagnostics_page + 1u);
    } else {
        lv_label_set_text(s_header_page, "");
    }
    if (context->battery_percent >= 0 && context->battery_percent <= 100) {
        (void)snprintf(battery, sizeof(battery), "%d%%",
                       context->battery_percent);
    } else {
        (void)snprintf(battery, sizeof(battery), "-");
    }
    lv_label_set_text(s_header_battery, battery);
}

size_t sonic_ui_page_count(const sonic_runtime_view_t *view)
{
    if (view == NULL || view->state != SONIC_RUNTIME_RX_COMPLETE) {
        return 1u;
    }
    if (view->payload_type == SONIC_TYPE_TEXT ||
        view->payload_type == SONIC_TYPE_URL) {
        return sonic_ui_text_page_count(view->payload, view->payload_length);
    }
    if (view->payload_type == SONIC_TYPE_TOKEN) {
        return sonic_ui_token_page_count(view->payload_length);
    }
    return 1u;
}

static void sync_page_identity(const sonic_runtime_view_t *view)
{
    if (!s_has_previous || view->state != s_page_state ||
        view->message_id != s_page_message_id ||
        view->payload_type != s_page_payload_type ||
        view->payload_length != s_page_payload_length) {
        s_page_index = 0u;
        s_page_state = view->state;
        s_page_message_id = view->message_id;
        s_page_payload_type = view->payload_type;
        s_page_payload_length = view->payload_length;
    }
    if (s_page_index >= sonic_ui_page_count(view)) {
        s_page_index = 0u;
    }
}

bool sonic_ui_handle_page_button(const sonic_runtime_view_t *view,
                                 bool next_page)
{
    size_t page_count = sonic_ui_page_count(view);

    if (page_count <= 1u) {
        return false;
    }
    if (next_page) {
        s_page_index = (s_page_index + 1u) % page_count;
    } else {
        s_page_index = (s_page_index + page_count - 1u) % page_count;
    }
    return true;
}

size_t sonic_ui_current_page(void)
{
    return s_page_index;
}

static void render_home(const sonic_runtime_view_t *view)
{
    static const char *items[] = {"Receive", "Send", "Diagnostics"};
    static const int y_positions[] = {82, 138, 194};

    label_at(s_content, "Choose an action", &lv_font_montserrat_20,
             UI_COLOR_INK, 16, 20, 208, 28);
    for (size_t i = 0u; i < 3u; ++i) {
        bool selected = i == (size_t)view->home_selection;
        lv_obj_t *row = make_card(s_content, 14, y_positions[i], 212, 44,
                                  selected ? UI_COLOR_BLUE_TINT
                                           : UI_COLOR_SURFACE);
        if (selected) {
            lv_obj_set_style_border_color(row, color(0xBFD0F1u), 0);
            lv_obj_t *dot = lv_obj_create(row);
            lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_size(dot, 8, 8);
            lv_obj_set_style_bg_color(dot, color(UI_COLOR_BLUE), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(dot, 0, 0);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_align(dot, LV_ALIGN_LEFT_MID, 1, 0);
        }
        label_at(row, items[i], &lv_font_montserrat_20,
                 selected ? UI_COLOR_BLUE_DARK : UI_COLOR_INK,
                 selected ? 23 : 14, 3, 172, 26);
    }
    set_footer("UP/DN", "OK Open", "");
}

static void create_meter(void)
{
    static const int heights[] = {12, 22, 32, 24, 14};
    int x = 78;

    for (size_t i = 0u; i < 5u; ++i) {
        s_meter_bars[i] = lv_obj_create(s_content);
        lv_obj_remove_flag(s_meter_bars[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(s_meter_bars[i], 8, heights[i]);
        lv_obj_set_pos(s_meter_bars[i], x, 82 - heights[i]);
        lv_obj_set_style_bg_color(s_meter_bars[i], color(UI_COLOR_GRAY_TINT), 0);
        lv_obj_set_style_bg_opa(s_meter_bars[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_meter_bars[i], 0, 0);
        lv_obj_set_style_radius(s_meter_bars[i], LV_RADIUS_CIRCLE, 0);
        x += 17;
    }
    s_meter_visible = true;
}

static void update_meter(const sonic_runtime_view_t *view,
                         const sonic_ui_context_t *context)
{
    static const float thresholds[] = {64.0f, 220.0f, 700.0f, 2200.0f, 7000.0f};
    static const int heights[] = {12, 22, 32, 24, 14};
    bool listening = view->state == SONIC_RUNTIME_RX_LISTENING &&
                     view->microphone_active;

    if (!s_meter_visible) {
        return;
    }
    for (size_t i = 0u; i < 5u; ++i) {
        bool active = listening && context->audio_diagnostics_available &&
                      context->microphone_rms >= thresholds[i];
        lv_obj_set_style_bg_color(s_meter_bars[i],
                                  color(active ? UI_COLOR_BLUE
                                               : UI_COLOR_GRAY_TINT), 0);
        lv_obj_set_size(s_meter_bars[i], 8, active ? heights[i] : 6);
        lv_obj_set_y(s_meter_bars[i], 82 - (active ? heights[i] : 6));
    }
}

static void render_receive_listening(void)
{
    label_at(s_content, "SOUND INPUT", &lv_font_montserrat_14,
             UI_COLOR_BLUE, 16, 12, 208, 20);
    create_meter();
    label_at(s_content, "Listening", &lv_font_montserrat_28,
             UI_COLOR_INK, 12, 97, 216, 42);
    label_at(s_content, "Waiting for sound from your phone.",
             &lv_font_montserrat_14, UI_COLOR_MUTED,
             20, 145, 200, 44);
    set_footer("", "OK Pause", "Hold Back");
}

static void render_receive_paused(void)
{
    label_at(s_content, "PAUSED", &lv_font_montserrat_14,
             UI_COLOR_MUTED, 20, 32, 200, 20);
    lv_obj_t *pause = lv_obj_create(s_content);
    lv_obj_remove_flag(pause, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(pause, 68, 68);
    lv_obj_set_pos(pause, 86, 66);
    lv_obj_set_style_bg_opa(pause, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(pause, color(UI_COLOR_LINE), 0);
    lv_obj_set_style_border_width(pause, 2, 0);
    lv_obj_set_style_radius(pause, LV_RADIUS_CIRCLE, 0);
    label_at(pause, "||", &lv_font_montserrat_28, UI_COLOR_MUTED,
             14, 12, 40, 38);
    label_at(s_content, "Listening paused", &lv_font_montserrat_20,
             UI_COLOR_INK, 12, 150, 216, 30);
    label_at(s_content, "Press OK to resume.", &lv_font_montserrat_14,
             UI_COLOR_MUTED, 20, 188, 200, 28);
    set_footer("", "OK Resume", "Hold Back");
}

static void render_receiving(const sonic_runtime_view_t *view)
{
    char progress[20];
    size_t count = view->expected_fragments;
    size_t received = view->received_fragments;
    int start_x = count == 1u ? 112 : (count == 2u ? 103 : 95);

    label_at(s_content, "RECEIVING", &lv_font_montserrat_14,
             UI_COLOR_BLUE, 16, 25, 208, 20);
    for (size_t i = 0u; i < count && i < SONIC_MAX_FRAGMENTS; ++i) {
        lv_obj_t *dot = lv_obj_create(s_content);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(dot, 14, 14);
        lv_obj_set_pos(dot, start_x + (int)i * 24, 75);
        lv_obj_set_style_bg_color(dot, color(i < received ? UI_COLOR_BLUE
                                                         : UI_COLOR_GRAY_TINT), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(dot, color(i < received ? UI_COLOR_BLUE
                                                              : UI_COLOR_LINE), 0);
        lv_obj_set_style_border_width(dot, 1, 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    }
    (void)snprintf(progress, sizeof(progress), "%u / %u",
                   (unsigned)received, (unsigned)count);
    label_at(s_content, progress, &lv_font_montserrat_28,
             UI_COLOR_INK, 30, 108, 180, 44);
    label_at(s_content, "Keep the phone nearby.", &lv_font_montserrat_14,
             UI_COLOR_MUTED, 20, 166, 200, 28);
    set_footer("", "", "Hold Stop");
}

static void render_text_result(const sonic_runtime_view_t *view, bool url)
{
    sonic_ui_page_t page = {0u, view->payload_length};
    char content[SONIC_MAX_MESSAGE_BYTES + 1u];
    char title[32];
    size_t page_count = sonic_ui_page_count(view);
    const char *kind = url ? "URL received" : "Text received";

    if (page_count > 1u &&
        !sonic_ui_text_page(view->payload, view->payload_length,
                            s_page_index, &page)) {
        page.start = 0u;
        page.length = view->payload_length;
    }
    if (page.length > sizeof(content) - 1u) {
        page.length = sizeof(content) - 1u;
    }
    memcpy(content, view->payload + page.start, page.length);
    content[page.length] = '\0';
    (void)snprintf(title, sizeof(title), "%s", url ? "URL" : "Message");
    set_chip(s_content, kind, UI_COLOR_GREEN, UI_COLOR_GREEN_TINT);
    label_at(s_content, title, &lv_font_montserrat_20,
             UI_COLOR_INK, 16, 43, 208, 28);
    (void)make_card(s_content, 14, 78, 212, 135, UI_COLOR_SURFACE);
    label_at(s_content, content, &lv_font_montserrat_14,
             UI_COLOR_INK, 24, 88, 192, 113);
    if (page_count > 1u) {
        char page_label[20];
        (void)snprintf(page_label, sizeof(page_label), "%u / %u",
                       (unsigned)(s_page_index + 1u), (unsigned)page_count);
        label_at(s_content, page_label, &lv_font_montserrat_14,
                 UI_COLOR_MUTED, 80, 222, 80, 18);
        set_footer("UP/DN", "OK Again", "Hold Back");
    } else {
        set_footer("", "OK Again", "Hold Back");
    }
}

static void render_token_result(const sonic_runtime_view_t *view)
{
    sonic_ui_page_t page = {0u, view->payload_length};
    char hex[SONIC_UI_TOKEN_BYTES_PER_PAGE * 3u + 1u];
    char title[32];
    char label[24];
    size_t page_count = sonic_ui_page_count(view);
    size_t used = 0u;

    if (!sonic_ui_token_page(view->payload_length, s_page_index, &page)) {
        page.start = 0u;
        page.length = 0u;
    }
    for (size_t i = 0u; i < page.length; ++i) {
        int written = snprintf(hex + used, sizeof(hex) - used,
                              i == 0u ? "%02X" : " %02X",
                              (unsigned)view->payload[page.start + i]);
        if (written > 0) {
            used += (size_t)written;
        }
    }
    hex[used] = '\0';
    (void)snprintf(title, sizeof(title), "%u bytes",
                   (unsigned)view->payload_length);
    (void)snprintf(label, sizeof(label), "Bytes %u-%u",
                   page.length == 0u ? 0u : (unsigned)page.start + 1u,
                   (unsigned)(page.start + page.length));
    set_chip(s_content, "Token received", UI_COLOR_GREEN, UI_COLOR_GREEN_TINT);
    label_at(s_content, title, &lv_font_montserrat_20,
             UI_COLOR_INK, 16, 43, 208, 28);
    lv_obj_t *card = make_card(s_content, 14, 78, 212, 118, UI_COLOR_SURFACE);
    label_at(card, label, &lv_font_montserrat_14,
             UI_COLOR_MUTED, 8, 4, 184, 20);
    label_at(card, hex, &lv_font_montserrat_14,
             UI_COLOR_INK, 8, 28, 184, 76);
    if (page_count > 1u) {
        char page_label[20];
        (void)snprintf(page_label, sizeof(page_label), "%u / %u",
                       (unsigned)(s_page_index + 1u), (unsigned)page_count);
        label_at(s_content, page_label, &lv_font_montserrat_14,
                 UI_COLOR_MUTED, 80, 207, 80, 18);
        set_footer("UP/DN", "OK Again", "Hold Back");
    } else {
        set_footer("", "OK Again", "Hold Back");
    }
}

static void diag_row(lv_obj_t *parent, const char *key, const char *value,
                     int y)
{
    label_at(parent, key, &lv_font_montserrat_14, UI_COLOR_MUTED,
             14, y, 100, 19);
    label_at(parent, value, &lv_font_montserrat_14, UI_COLOR_INK,
             116, y, 110, 19);
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(line, 212, 1);
    lv_obj_set_pos(line, 14, y + 22);
    lv_obj_set_style_bg_color(line, color(UI_COLOR_LINE), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
}

static void diag_profile_row(lv_obj_t *parent, const char *value, int y)
{
    label_at(parent, "Profile", &lv_font_montserrat_14, UI_COLOR_MUTED,
             14, y, 78, 19);
    label_at(parent, value, &lv_font_montserrat_14, UI_COLOR_INK,
             102, y, 124, 19);
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(line, 212, 1);
    lv_obj_set_pos(line, 14, y + 22);
    lv_obj_set_style_bg_color(line, color(UI_COLOR_LINE), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
}

static const char *audio_state_name(sonic_ui_audio_state_t state)
{
    switch (state) {
    case SONIC_UI_AUDIO_IDLE: return "Idle";
    case SONIC_UI_AUDIO_RX: return "Listening";
    case SONIC_UI_AUDIO_TX: return "Sending";
    case SONIC_UI_AUDIO_ERROR: return "Error";
    default: return "-";
    }
}

static const char *payload_type_name(sonic_payload_type_t type)
{
    switch (type) {
    case SONIC_TYPE_TEXT: return "TEXT";
    case SONIC_TYPE_URL: return "URL";
    case SONIC_TYPE_TOKEN: return "TOKEN";
    case SONIC_TYPE_DEVICE_INFO: return "DEVICE_INFO";
    default: return "-";
    }
}

static const char *transfer_result_name(sonic_runtime_transfer_result_t result)
{
    switch (result) {
    case SONIC_RUNTIME_TRANSFER_COMPLETED: return "Complete";
    case SONIC_RUNTIME_TRANSFER_INCOMPLETE: return "Incomplete";
    case SONIC_RUNTIME_TRANSFER_CANCELLED: return "Cancelled";
    case SONIC_RUNTIME_TRANSFER_ERROR: return "Error";
    default: return "-";
    }
}

static void render_device_info(const sonic_runtime_view_t *view)
{
    sonic_device_info_t info;
    char version[24];
    char ggwave[24];
    char battery[16];
    char profile[20];
    char max_message[16];
    char fingerprint[12];
    static const char hex[] = "0123456789ABCDEF";

    if (sonic_device_info_parse(view->payload, view->payload_length, &info) !=
        SONIC_OK) {
        set_chip(s_content, "Invalid device card", UI_COLOR_AMBER,
                 UI_COLOR_AMBER_TINT);
        label_at(s_content, "Device information could not be read.",
                 &lv_font_montserrat_14, UI_COLOR_INK, 16, 48, 208, 56);
        set_footer("", "OK Again", "Hold Back");
        return;
    }
    (void)snprintf(version, sizeof(version), "%u.%u.%u", info.sonic_major,
                   info.sonic_minor, info.sonic_patch);
    (void)snprintf(ggwave, sizeof(ggwave), "%u.%u.%u", info.ggwave_major,
                   info.ggwave_minor, info.ggwave_patch);
    if (info.battery_percent <= 100u) {
        (void)snprintf(battery, sizeof(battery), "%u%%",
                       (unsigned)info.battery_percent);
    } else {
        (void)snprintf(battery, sizeof(battery), "-");
    }
    (void)snprintf(profile, sizeof(profile), "%s",
                   info.acoustic_profile == 1u ? "Audible Fastest" :
                   info.acoustic_profile == 2u ? "Audible Fast" : "Unknown");
    (void)snprintf(max_message, sizeof(max_message), "%u B",
                   (unsigned)info.max_message_bytes);
    for (size_t i = 0u; i < sizeof(info.build_fingerprint); ++i) {
        fingerprint[i * 2u] = hex[info.build_fingerprint[i] >> 4];
        fingerprint[i * 2u + 1u] = hex[info.build_fingerprint[i] & 0x0Fu];
    }
    fingerprint[8] = '\0';

    set_chip(s_content, "Device card", UI_COLOR_GREEN, UI_COLOR_GREEN_TINT);
    label_at(s_content, "AI Passport", &lv_font_montserrat_20,
             UI_COLOR_INK, 16, 42, 208, 28);
    diag_row(s_content, "Sonic Link", version, 78);
    diag_row(s_content, "ggwave", ggwave, 105);
    diag_row(s_content, "Battery", battery, 132);
    diag_row(s_content, "Profile", profile, 159);
    diag_row(s_content, "Max message", max_message, 186);
    diag_row(s_content, "Build", fingerprint, 213);
    set_footer("", "OK Again", "Hold Back");
}

static void render_receive_result(const sonic_runtime_view_t *view)
{
    if (view->payload_type == SONIC_TYPE_TEXT) {
        render_text_result(view, false);
    } else if (view->payload_type == SONIC_TYPE_URL) {
        render_text_result(view, true);
    } else if (view->payload_type == SONIC_TYPE_TOKEN) {
        render_token_result(view);
    } else if (view->payload_type == SONIC_TYPE_DEVICE_INFO) {
        render_device_info(view);
    }
}

static void render_recoverable(const char *title, const char *copy,
                               const sonic_runtime_view_t *view)
{
    char progress[24];
    bool incomplete = view->state == SONIC_RUNTIME_RX_INCOMPLETE;

    set_chip(s_content, "Attention", UI_COLOR_AMBER, UI_COLOR_AMBER_TINT);
    label_at(s_content, title, &lv_font_montserrat_20,
             UI_COLOR_INK, 16, 44, 208, 30);
    label_at(s_content, copy, &lv_font_montserrat_14,
             UI_COLOR_MUTED, 18, 82, 204, 46);
    if (incomplete) {
        (void)snprintf(progress, sizeof(progress), "%u / %u parts",
                       (unsigned)view->received_fragments,
                       (unsigned)view->expected_fragments);
        label_at(s_content, progress, &lv_font_montserrat_14,
                 UI_COLOR_INK, 18, 148, 204, 24);
        set_footer("", "OK Listen", "Hold Back");
    } else {
        set_footer("", "OK Again", "Hold Back");
    }
}

static void render_audio_error(const char *title, const char *copy)
{
    set_chip(s_content, "Audio error", UI_COLOR_RED, UI_COLOR_RED_TINT);
    label_at(s_content, title, &lv_font_montserrat_20,
             UI_COLOR_INK, 16, 44, 208, 30);
    label_at(s_content, copy, &lv_font_montserrat_14,
             UI_COLOR_MUTED, 18, 82, 204, 50);
    set_footer("", "OK Retry", "Hold Back");
}

static void render_send_menu(const sonic_runtime_view_t *view)
{
    static const char *items[] = {"Hello", "Demo URL", "Device Card", "Test Token"};

    label_at(s_content, "Choose data", &lv_font_montserrat_14,
             UI_COLOR_BLUE, 16, 12, 208, 20);
    label_at(s_content, "Send", &lv_font_montserrat_28,
             UI_COLOR_INK, 16, 35, 208, 42);
    for (size_t i = 0u; i < SONIC_RUNTIME_PRESET_COUNT; ++i) {
        bool selected = i == (size_t)view->preset_selection;
        lv_obj_t *row = make_card(s_content, 14, 86 + (int)i * 42,
                                  212, 36,
                                  selected ? UI_COLOR_BLUE_TINT
                                           : UI_COLOR_SURFACE);
        if (selected) {
            lv_obj_set_style_border_color(row, color(0xBFD0F1u), 0);
        }
        label_at(row, items[i], &lv_font_montserrat_14,
                 selected ? UI_COLOR_BLUE_DARK : UI_COLOR_INK,
                 10, 0, 188, 24);
    }
    set_footer("UP/DN", "OK Open", "Hold Back");
}

static void render_send_preview(const sonic_runtime_view_t *view)
{
    char payload[SONIC_MAX_MESSAGE_BYTES + 1u];
    char preview[44];
    char details[44];
    char estimate[24];
    uint32_t milliseconds = (uint32_t)view->frame_count * 1200u;

    if (view->payload_type == SONIC_TYPE_TOKEN) {
        for (size_t i = 0u; i < view->payload_length; ++i) {
            payload[i] = (view->payload[i] >= 0x20u && view->payload[i] <= 0x7Eu)
                             ? (char)view->payload[i]
                             : '.';
        }
        payload[view->payload_length] = '\0';
    } else {
        size_t length = view->payload_length;
        if (length > sizeof(payload) - 1u) {
            length = sizeof(payload) - 1u;
        }
        memcpy(payload, view->payload, length);
        payload[length] = '\0';
    }
    if (view->frame_count > 1u) {
        milliseconds += (uint32_t)(view->frame_count - 1u) * 200u;
    }
    (void)snprintf(estimate, sizeof(estimate), "~%u.%u s",
                   (unsigned)(milliseconds / 1000u),
                   (unsigned)((milliseconds % 1000u) / 100u));

    size_t preview_length = view->payload_length;
    if (preview_length > 36u) {
        preview_length = 36u;
        while (preview_length > 0u &&
               (((uint8_t)payload[preview_length] & 0xC0u) == 0x80u)) {
            --preview_length;
        }
    }
    memcpy(preview, payload, preview_length);
    if (preview_length < view->payload_length) {
        memcpy(preview + preview_length, "...", 4u);
    } else {
        preview[preview_length] = '\0';
    }

    set_chip(s_content, "PREVIEW", UI_COLOR_BLUE_DARK, UI_COLOR_BLUE_TINT);
    label_at(s_content, view->preset_selection == SONIC_RUNTIME_PRESET_HELLO
                           ? "Hello"
                           : view->preset_selection == SONIC_RUNTIME_PRESET_DEMO_URL
                                 ? "Demo URL"
                                 : view->preset_selection == SONIC_RUNTIME_PRESET_DEVICE_CARD
                                       ? "Device Card" : "Test Token",
             &lv_font_montserrat_20, UI_COLOR_INK, 16, 43, 208, 28);
    lv_obj_t *card = make_card(s_content, 14, 75, 212, 66, UI_COLOR_SURFACE);
    label_at(card, view->payload_type == SONIC_TYPE_TEXT ? "TEXT" :
                  view->payload_type == SONIC_TYPE_URL ? "URL" :
                  view->payload_type == SONIC_TYPE_TOKEN ? "TOKEN" : "DEVICE_INFO",
             &lv_font_montserrat_14, UI_COLOR_MUTED, 8, 1, 184, 18);
    label_at(card, preview, &lv_font_montserrat_14,
             UI_COLOR_INK, 8, 20, 184, 42);
    (void)snprintf(details, sizeof(details), "%u B / %u fr / %.8s",
                   (unsigned)view->payload_length,
                   (unsigned)view->frame_count,
                   estimate);
    label_at(s_content, details, &lv_font_montserrat_14,
             UI_COLOR_MUTED, 14, 151, 212, 22);
    set_footer("", "OK Send", "Hold Back");
}

static void render_tx_playing(const sonic_runtime_view_t *view)
{
    char progress[20];
    size_t count = view->frame_count;
    size_t current = view->frame_index;
    int start_x = count == 1u ? 112 : (count == 2u ? 103 : 95);

    label_at(s_content, "PLAYING SOUND", &lv_font_montserrat_14,
             UI_COLOR_BLUE, 16, 36, 208, 20);
    label_at(s_content, "Sending", &lv_font_montserrat_28,
             UI_COLOR_INK, 16, 66, 208, 42);
    for (size_t i = 0u; i < count && i < SONIC_MAX_FRAGMENTS; ++i) {
        lv_obj_t *dot = lv_obj_create(s_content);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(dot, 14, 14);
        lv_obj_set_pos(dot, start_x + (int)i * 24, 132);
        lv_obj_set_style_bg_color(dot, color(i < current ? UI_COLOR_BLUE
                                                         : UI_COLOR_GRAY_TINT), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(dot, color(i < current ? UI_COLOR_BLUE
                                                              : UI_COLOR_LINE), 0);
        lv_obj_set_style_border_width(dot, 1, 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    }
    (void)snprintf(progress, sizeof(progress), "%u / %u",
                   (unsigned)current, (unsigned)count);
    label_at(s_content, progress, &lv_font_montserrat_20,
             UI_COLOR_INK, 30, 162, 180, 30);
    set_footer("", "", "Hold Stop");
}

static void render_tx_terminal(const sonic_runtime_view_t *view)
{
    if (view->state == SONIC_RUNTIME_TX_COMPLETE) {
        set_chip(s_content, "SENT", UI_COLOR_GREEN, UI_COLOR_GREEN_TINT);
        label_at(s_content, "Sound transmission completed.",
                 &lv_font_montserrat_20, UI_COLOR_INK, 16, 54, 208, 56);
        label_at(s_content, "Reception is not acknowledged.",
                 &lv_font_montserrat_14, UI_COLOR_MUTED, 18, 122, 204, 42);
        set_footer("", "OK Again", "Hold Back");
    } else if (view->state == SONIC_RUNTIME_TX_CANCELLED) {
        render_recoverable("Interrupted", "Sound transmission was cancelled.", view);
    } else {
        render_audio_error("Audio unavailable", "The sound transfer could not finish.");
    }
}

static void render_diagnostics(const sonic_runtime_view_t *view,
                               const sonic_ui_context_t *context)
{
    char value[36];
    int y = 44;

    label_at(s_content, view->diagnostics_page == SONIC_RUNTIME_DIAG_SYSTEM
                           ? "System"
                           : view->diagnostics_page == SONIC_RUNTIME_DIAG_AUDIO
                                 ? "Audio"
                                 : view->diagnostics_page == SONIC_RUNTIME_DIAG_CODEC
                                       ? "Codec" : "Last Transfer",
             &lv_font_montserrat_20, UI_COLOR_INK, 14, 8, 212, 28);
    switch (view->diagnostics_page) {
    case SONIC_RUNTIME_DIAG_SYSTEM:
        diag_row(s_content, "Sonic Link", context->sonic_version, y);
        diag_row(s_content, "Build", context->build_fingerprint, y + 27);
        if (context->battery_percent >= 0) {
            (void)snprintf(value, sizeof(value), "%d%%", context->battery_percent);
        } else {
            (void)snprintf(value, sizeof(value), "-");
        }
        diag_row(s_content, "Battery", value, y + 54);
        (void)snprintf(value, sizeof(value), "%d B", context->free_heap_bytes);
        diag_row(s_content, "Free heap", context->free_heap_bytes >= 0 ? value : "-", y + 81);
        (void)snprintf(value, sizeof(value), "%d B", context->minimum_free_heap_bytes);
        diag_row(s_content, "Min heap", context->minimum_free_heap_bytes >= 0 ? value : "-", y + 108);
        (void)snprintf(value, sizeof(value), "%d B", context->largest_free_block_bytes);
        diag_row(s_content, "Largest block", context->largest_free_block_bytes >= 0 ? value : "-", y + 135);
        break;
    case SONIC_RUNTIME_DIAG_AUDIO:
        (void)snprintf(value, sizeof(value), "%ukHz/%ub/1ch",
                       (unsigned)(context->sample_rate_hz / 1000u),
                       (unsigned)context->sample_bits);
        diag_row(s_content, "Format", context->sample_rate_hz ? value : "-", y);
        if (context->audio_diagnostics_available) {
            (void)snprintf(value, sizeof(value), "%.1f dBFS",
                           (double)context->microphone_dbfs);
        } else {
            (void)snprintf(value, sizeof(value), "-");
        }
        diag_row(s_content, "Mic level", value, y + 27);
        (void)snprintf(value, sizeof(value), "%u%%",
                       (unsigned)context->speaker_volume_percent);
        diag_row(s_content, "Volume", value, y + 54);
        diag_row(s_content, "Worker", audio_state_name(context->audio_state), y + 81);
        if (context->audio_diagnostics_available) {
            (void)snprintf(value, sizeof(value), "%u words",
                           (unsigned)context->worker_stack_high_water_words);
        } else {
            (void)snprintf(value, sizeof(value), "-");
        }
        diag_row(s_content, "Stack free", value, y + 108);
        break;
    case SONIC_RUNTIME_DIAG_CODEC:
        diag_row(s_content, "ggwave", context->ggwave_version, y);
        diag_profile_row(s_content,
                         context->acoustic_profile == 1u ? "Audible Fastest" :
                         context->acoustic_profile == 2u ? "Audible Fast" : "Unknown",
                         y + 27);
        diag_row(s_content, "Frame", "40 bytes", y + 54);
        diag_row(s_content, "Max message", "93 bytes", y + 81);
        diag_row(s_content, "DSS", "Enabled", y + 108);
        (void)snprintf(value, sizeof(value), "%d B", context->codec_heap_bytes);
        diag_row(s_content, "Codec heap", context->codec_heap_bytes >= 0 ? value : "-", y + 135);
        (void)snprintf(value, sizeof(value), "%llu us",
                       (unsigned long long)context->rx_average_us);
        diag_row(s_content, "RX avg", context->audio_diagnostics_available ? value : "-", y + 162);
        (void)snprintf(value, sizeof(value), "%llu us",
                       (unsigned long long)context->rx_p99_us);
        diag_row(s_content, "RX p99", context->audio_diagnostics_available ? value : "-", y + 189);
        break;
    case SONIC_RUNTIME_DIAG_LAST_TRANSFER: {
        const sonic_runtime_last_transfer_t *transfer = &view->last_transfer;
        if (!transfer->valid) {
            diag_row(s_content, "Transfer", "None", y);
            break;
        }
        diag_row(s_content, "Direction", transfer->is_rx ? "Receive" : "Send", y);
        diag_row(s_content, "Type", payload_type_name(transfer->type), y + 27);
        (void)snprintf(value, sizeof(value), "%u / %u B",
                       (unsigned)transfer->frame_count,
                       (unsigned)transfer->byte_length);
        diag_row(s_content, "Frames / bytes", value, y + 54);
        (void)snprintf(value, sizeof(value), "%u ms",
                       (unsigned)transfer->duration_ms);
        diag_row(s_content, "Duration", value, y + 81);
        (void)snprintf(value, sizeof(value), "%04X",
                       (unsigned)transfer->message_id);
        diag_row(s_content, "Message ID", value, y + 108);
        diag_row(s_content, "Result", transfer_result_name(transfer->result), y + 135);
        diag_row(s_content, "Error", sonic_error_name(transfer->error), y + 162);
        break;
    }
    }
    set_footer("UP/DN", "", "Hold Back");
}

static bool view_changed(const sonic_runtime_view_t *view,
                         const sonic_ui_context_t *context)
{
    if (!s_has_previous || memcmp(&s_previous_view, view, sizeof(*view)) != 0) {
        return true;
    }
    if (view->state == SONIC_RUNTIME_DIAGNOSTICS &&
        memcmp(&s_previous_context, context, sizeof(*context)) != 0) {
        return true;
    }
    return false;
}

bool sonic_ui_create(void)
{
    if (s_screen != NULL) {
        return true;
    }
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_screen, color(UI_COLOR_SCREEN), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);

    lv_obj_t *header = lv_obj_create(s_screen);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, 240, 28);
    lv_obj_set_style_bg_color(header, color(UI_COLOR_SCREEN), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, color(UI_COLOR_LINE), 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    s_header_title = make_label(header, "Sonic Link", &lv_font_montserrat_14,
                                UI_COLOR_MUTED, 122, 20);
    lv_obj_align(s_header_title, LV_ALIGN_LEFT_MID, 12, 0);
    s_header_page = make_label(header, "", &lv_font_montserrat_14,
                               UI_COLOR_MUTED, 34, 20);
    lv_obj_align(s_header_page, LV_ALIGN_RIGHT_MID, -62, 0);
    s_header_battery = make_label(header, "-", &lv_font_montserrat_14,
                                  UI_COLOR_MUTED, 44, 20);
    lv_obj_set_style_text_align(s_header_battery, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_header_battery, LV_ALIGN_RIGHT_MID, -12, 0);

    s_content = lv_obj_create(s_screen);
    lv_obj_remove_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_content, 0, 28);
    lv_obj_set_size(s_content, 240, 256);
    lv_obj_set_style_bg_opa(s_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_content, 0, 0);
    lv_obj_set_style_pad_all(s_content, 0, 0);

    lv_obj_t *footer = lv_obj_create(s_screen);
    lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(footer, 0, 284);
    lv_obj_set_size(footer, 240, 36);
    lv_obj_set_style_bg_color(footer, color(UI_COLOR_SCREEN), 0);
    lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(footer, color(UI_COLOR_LINE), 0);
    lv_obj_set_style_pad_all(footer, 0, 0);
    for (size_t i = 0u; i < 3u; ++i) {
        s_footer_labels[i] = make_label(footer, "", &lv_font_montserrat_14,
                                        UI_COLOR_MUTED, 80, 24);
        lv_obj_set_style_text_align(s_footer_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s_footer_labels[i], (int32_t)i * 80, 6);
    }
    lv_screen_load(s_screen);
    s_has_previous = false;
    s_page_index = 0u;
    s_previous_page_index = 0u;
    s_page_message_id = 0u;
    s_page_state = SONIC_RUNTIME_HOME;
    s_page_payload_type = SONIC_TYPE_TEXT;
    s_page_payload_length = 0u;
    return true;
}

void sonic_ui_destroy(void)
{
    if (s_screen != NULL) {
        lv_obj_delete(s_screen);
    }
    s_screen = NULL;
    s_header_title = NULL;
    s_header_page = NULL;
    s_header_battery = NULL;
    s_content = NULL;
    memset(s_footer_labels, 0, sizeof(s_footer_labels));
    memset(s_meter_bars, 0, sizeof(s_meter_bars));
    s_meter_visible = false;
    s_has_previous = false;
    s_page_index = 0u;
    s_previous_page_index = 0u;
}

void sonic_ui_render(const sonic_runtime_view_t *view,
                     const sonic_ui_context_t *context)
{
    bool changed;

    if (s_screen == NULL || view == NULL || context == NULL) {
        return;
    }
    sync_page_identity(view);
    /* Page navigation is presentation-local and may not change the runtime view. */
    changed = view_changed(view, context) ||
              s_page_index != s_previous_page_index;
    if (changed) {
        lv_obj_clean(s_content);
        memset(s_meter_bars, 0, sizeof(s_meter_bars));
        s_meter_visible = false;
        switch (view->state) {
        case SONIC_RUNTIME_HOME:
            render_home(view);
            break;
        case SONIC_RUNTIME_RX_LISTENING:
            render_receive_listening();
            break;
        case SONIC_RUNTIME_RX_PAUSED:
            render_receive_paused();
            break;
        case SONIC_RUNTIME_RX_ASSEMBLING:
            render_receiving(view);
            break;
        case SONIC_RUNTIME_RX_COMPLETE:
            render_receive_result(view);
            break;
        case SONIC_RUNTIME_RX_INCOMPLETE:
            render_recoverable("Message incomplete",
                               "Some parts were missed. Listen again to complete it.",
                               view);
            break;
        case SONIC_RUNTIME_RX_INVALID_MESSAGE:
            render_recoverable("Invalid message",
                               "The received data could not be validated.", view);
            break;
        case SONIC_RUNTIME_RX_UNSUPPORTED_GLYPH:
            render_recoverable("Text not shown",
                               "Valid text arrived, but this display cannot show every character.",
                               view);
            break;
        case SONIC_RUNTIME_RX_FRAME_CONFLICT:
            render_recoverable("Message changed",
                               "The received parts did not match. Please try again.",
                               view);
            break;
        case SONIC_RUNTIME_RX_AUDIO_ERROR:
            render_audio_error("Receive stopped",
                               "Audio input is unavailable. Check Diagnostics or retry.");
            break;
        case SONIC_RUNTIME_TX_MENU:
            render_send_menu(view);
            break;
        case SONIC_RUNTIME_TX_PREVIEW:
            render_send_preview(view);
            break;
        case SONIC_RUNTIME_TX_PREPARING:
            set_chip(s_content, "PREPARING", UI_COLOR_BLUE_DARK,
                     UI_COLOR_BLUE_TINT);
            label_at(s_content, "Preparing sound", &lv_font_montserrat_20,
                     UI_COLOR_INK, 16, 54, 208, 32);
            set_footer("", "", "");
            break;
        case SONIC_RUNTIME_TX_PLAYING:
            render_tx_playing(view);
            break;
        case SONIC_RUNTIME_TX_COMPLETE:
        case SONIC_RUNTIME_TX_CANCELLED:
        case SONIC_RUNTIME_TX_AUDIO_ERROR:
            render_tx_terminal(view);
            break;
        case SONIC_RUNTIME_DIAGNOSTICS:
            render_diagnostics(view, context);
            break;
        }
        s_previous_view = *view;
        s_previous_context = *context;
        s_previous_page_index = s_page_index;
        s_has_previous = true;
    }
    set_header(view, context);
    update_meter(view, context);
}
