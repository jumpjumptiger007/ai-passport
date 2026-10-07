#include "sonic_ui.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "sonic_core.h"

#define UI_COLOR_SCREEN 0x16201Cu
#define UI_COLOR_SURFACE 0x1D2823u
#define UI_COLOR_PANEL 0x26312Cu
#define UI_COLOR_ACTIVE 0x6BD4DCu
#define UI_COLOR_ACTIVE_DIM 0x397B80u
#define UI_COLOR_INK 0xEEF2EDu
#define UI_COLOR_MUTED 0x9BA69Fu
#define UI_COLOR_LINE 0x59645Du
#define UI_COLOR_GREEN 0x91CE74u
#define UI_COLOR_GREEN_TINT 0x91CE74u
#define UI_COLOR_AMBER 0xD6A343u
#define UI_COLOR_AMBER_TINT 0xD6A343u
#define UI_COLOR_RED 0xC86E63u
#define UI_COLOR_RED_TINT 0xC86E63u
#define UI_COLOR_GRAY 0x505C56u
#define UI_COLOR_LIGHT 0xD8D2C6u
#define UI_COLOR_LIGHT_TEXT 0x1A1E1Bu

static lv_obj_t *s_screen;
static lv_obj_t *s_header_title;
static lv_obj_t *s_header_battery;
static lv_obj_t *s_header_battery_fill;
static lv_obj_t *s_section;
static lv_obj_t *s_subsection;
static lv_obj_t *s_content;
static lv_obj_t *s_footer_labels[3];
static lv_obj_t *s_meter_bars[10];
static bool s_meter_visible;
static int s_meter_y_base;
static bool s_meter_paused;
static uint8_t s_meter_phase;
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
    lv_obj_set_style_radius(card, 4, 0);
    lv_obj_set_style_pad_all(card, 7, 0);
    return card;
}

static void label_at(lv_obj_t *parent, const char *text,
                     const lv_font_t *font, uint32_t text_color,
                     int32_t x, int32_t y, int32_t width, int32_t height)
{
    lv_obj_t *label = make_label(parent, text, font, text_color, width, height);
    lv_obj_set_pos(label, x, y);
}

static int32_t text_height(const char *text, const lv_font_t *font,
                           int32_t width)
{
    lv_point_t size;

    lv_text_get_size(&size, text != NULL ? text : "", font, 0, 1, width,
                     LV_TEXT_FLAG_NONE);
    return size.y + 2;
}

static void set_footer(const char *left, const char *center, const char *right)
{
    const char *values[3] = {left, center, right};
    for (size_t i = 0u; i < 3u; ++i) {
        lv_label_set_text(s_footer_labels[i], values[i] != NULL ? values[i] : "");
    }
}

static void set_chip_at(lv_obj_t *parent, const char *text,
                        uint32_t foreground, uint32_t background, int32_t y)
{
    lv_obj_t *chip = lv_label_create(parent);
    lv_label_set_text(chip, text);
    lv_obj_set_style_text_font(chip, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(chip, color(foreground == UI_COLOR_GREEN ||
                                             foreground == UI_COLOR_AMBER ||
                                             foreground == UI_COLOR_RED
                                                 ? UI_COLOR_LIGHT_TEXT : foreground), 0);
    lv_obj_set_style_bg_color(chip, color(background), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(chip, 8, 0);
    lv_obj_set_style_pad_ver(chip, 2, 0);
    lv_obj_set_style_radius(chip, 3, 0);
    lv_obj_align(chip, LV_ALIGN_TOP_LEFT, 12, y);
}

static void set_chip(lv_obj_t *parent, const char *text, uint32_t foreground,
                     uint32_t background)
{
    set_chip_at(parent, text, foreground, background, 21);
}

static void set_header(const sonic_runtime_view_t *view,
                       const sonic_ui_context_t *context)
{
    char battery[12];
    const char *section = "MODE SELECT";
    if ((view->state >= SONIC_RUNTIME_RX_LISTENING &&
         view->state <= SONIC_RUNTIME_RX_AUDIO_ERROR) ||
        view->state == SONIC_RUNTIME_RX_UNSUPPORTED_GLYPH ||
        view->state == SONIC_RUNTIME_RX_FRAME_CONFLICT) section = "RECEIVE";
    else if (view->state >= SONIC_RUNTIME_TX_MENU &&
             view->state <= SONIC_RUNTIME_TX_AUDIO_ERROR) section = "SEND";
    else if (view->state == SONIC_RUNTIME_DIAGNOSTICS) section = "DIAGNOSTICS";
    lv_label_set_text(s_header_title, "SONIC LINK");
    lv_label_set_text(s_section, section);
    if (view->state == SONIC_RUNTIME_RX_COMPLETE ||
        view->state == SONIC_RUNTIME_RX_INCOMPLETE ||
        view->state == SONIC_RUNTIME_RX_INVALID_MESSAGE ||
        view->state == SONIC_RUNTIME_RX_UNSUPPORTED_GLYPH ||
        view->state == SONIC_RUNTIME_RX_FRAME_CONFLICT ||
        view->state == SONIC_RUNTIME_RX_AUDIO_ERROR ||
        view->state == SONIC_RUNTIME_TX_COMPLETE ||
        view->state == SONIC_RUNTIME_TX_CANCELLED ||
        view->state == SONIC_RUNTIME_TX_AUDIO_ERROR) {
        lv_obj_add_flag(s_section, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_section, LV_OBJ_FLAG_HIDDEN);
    }
    if (view->state == SONIC_RUNTIME_TX_MENU) {
        lv_obj_remove_flag(s_subsection, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_subsection, LV_OBJ_FLAG_HIDDEN);
    }
    if (context->battery_percent >= 0 && context->battery_percent <= 100) {
        (void)snprintf(battery, sizeof(battery), "%d%%",
                       context->battery_percent);
    } else {
        battery[0] = '\0';
    }
    lv_label_set_text(s_header_battery, battery);
    lv_obj_set_width(s_header_battery_fill,
                     context->battery_percent >= 0 && context->battery_percent <= 100
                         ? 11 * context->battery_percent / 100 : 0);
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
    static const char *items[] = {"RECEIVE", "SEND", "DIAGNOSTICS"};
    static const char *copies[] = {"Get data from sound", "Play data as sound",
                                   "Check system status"};
    for (size_t i = 0u; i < 3u; ++i) {
        bool selected = i == (size_t)view->home_selection;
        lv_obj_t *row = make_card(s_content, 14, 56 + (int)i * 60, 212, 56,
                                  selected ? UI_COLOR_PANEL : UI_COLOR_SURFACE);
        if (selected) {
            lv_obj_set_style_border_color(row, color(UI_COLOR_ACTIVE), 0);
            lv_obj_set_style_border_width(row, 2, 0);
            lv_obj_t *dot = lv_obj_create(row);
            lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_size(dot, 6, 28);
            lv_obj_set_style_bg_color(dot, color(UI_COLOR_ACTIVE), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(dot, 0, 0);
            lv_obj_set_style_radius(dot, 1, 0);
            lv_obj_align(dot, LV_ALIGN_LEFT_MID, -4, 0);
        }
        label_at(row, items[i], &lv_font_montserrat_14,
                 selected ? UI_COLOR_ACTIVE : UI_COLOR_INK, 10, 4, 178, 20);
        label_at(row, copies[i], &lv_font_montserrat_14,
                 UI_COLOR_MUTED, 10, 25, 178, 18);
    }
    set_footer("UP/DN", "OK Open", "");
}

static void create_meter(int y_base)
{
    for (size_t i = 0u; i < 10u; ++i) {
        s_meter_bars[i] = lv_obj_create(s_content);
        lv_obj_remove_flag(s_meter_bars[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(s_meter_bars[i], 9, 8);
        lv_obj_set_pos(s_meter_bars[i], 42 + (int)i * 15, y_base);
        lv_obj_set_style_bg_color(s_meter_bars[i], color(UI_COLOR_GRAY), 0);
        lv_obj_set_style_bg_opa(s_meter_bars[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_meter_bars[i], 0, 0);
        lv_obj_set_style_radius(s_meter_bars[i], 1, 0);
    }
    s_meter_visible = true;
    s_meter_y_base = y_base;
}

static void update_meter(const sonic_runtime_view_t *view,
                         const sonic_ui_context_t *context)
{
    static const uint8_t wave[] = {3u, 4u, 5u, 6u, 7u, 8u, 7u, 6u, 5u, 4u};

    if (!s_meter_visible) {
        return;
    }
    s_meter_paused = view->state == SONIC_RUNTIME_RX_PAUSED;
    for (size_t i = 0u; i < 10u; ++i) {
        int height = 5;
        if (!s_meter_paused) {
            int rhythm = (int)wave[(i + s_meter_phase) % 10u];
            float level = context->audio_diagnostics_available &&
                          view->microphone_active ? context->microphone_rms : 0.0f;
            int input_gain = level > 500.0f ? 10 : (level > 120.0f ? 5 : 0);
            height = 11 + rhythm * 5 + input_gain;
            int maximum = s_meter_y_base == 132 ? 54 : 43;
            if (height > maximum) height = maximum;
        }
        lv_obj_set_style_bg_color(s_meter_bars[i],
                                  color(s_meter_paused ? UI_COLOR_GRAY : UI_COLOR_ACTIVE), 0);
        lv_obj_set_size(s_meter_bars[i], 9, height);
        lv_obj_set_y(s_meter_bars[i], s_meter_y_base + 54 - height);
    }
    if (!s_meter_paused) s_meter_phase = (uint8_t)((s_meter_phase + 1u) % 10u);
}

static void render_receive_listening(const sonic_runtime_view_t *view)
{
    bool paused = view->state == SONIC_RUNTIME_RX_PAUSED;
    label_at(s_content, paused ? "PAUSED" : "LISTENING",
             &lv_font_montserrat_20, UI_COLOR_INK, 16, 62, 208, 28);
    create_meter(132);
    label_at(s_content, paused ? "Microphone paused" : "Waiting for sound",
             &lv_font_montserrat_14, UI_COLOR_MUTED, 20, 94, 200, 20);
    label_at(s_content, paused ? "Press OK to resume" : "from your phone",
             &lv_font_montserrat_14, UI_COLOR_MUTED, 20, 113, 200, 20);
    lv_obj_t *summary = make_card(s_content, 14, 190, 212, 49, UI_COLOR_SURFACE);
    label_at(summary, paused ? "MIC PAUSED" : "MIC ACTIVE",
             &lv_font_montserrat_14, paused ? UI_COLOR_MUTED : UI_COLOR_GREEN,
             8, 14, 92, 18);
    label_at(summary, "FRAME  - / -", &lv_font_montserrat_14,
             UI_COLOR_INK, 108, 14, 94, 18);
    set_footer("", paused ? "OK Resume" : "OK Pause", "Hold Back");
}

static void render_receiving(const sonic_runtime_view_t *view)
{
    char progress[20];
    char readout[24];
    size_t count = view->expected_fragments;
    size_t received = view->received_fragments;
    int start_x = count == 1u ? 112 : (count == 2u ? 103 : 95);

    label_at(s_content, "RECEIVING", &lv_font_montserrat_20,
             UI_COLOR_INK, 16, 59, 208, 24);
    label_at(s_content, "Assembling message fragments.",
             &lv_font_montserrat_14, UI_COLOR_MUTED, 16, 84, 208, 18);
    for (size_t i = 0u; i < count && i < SONIC_MAX_FRAGMENTS; ++i) {
        lv_obj_t *dot = lv_obj_create(s_content);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(dot, 14, 14);
        lv_obj_set_pos(dot, start_x + (int)i * 24, 155);
        lv_obj_set_style_bg_color(dot, color(i < received ? UI_COLOR_ACTIVE
                                                         : UI_COLOR_GRAY), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(dot, color(i < received ? UI_COLOR_ACTIVE
                                                              : UI_COLOR_LINE), 0);
        lv_obj_set_style_border_width(dot, 1, 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    }
    (void)snprintf(progress, sizeof(progress), "%u / %u",
                   (unsigned)received, (unsigned)count);
    label_at(s_content, progress, &lv_font_montserrat_20,
             UI_COLOR_ACTIVE, 30, 111, 180, 26);
    label_at(s_content, "FRAGMENTS", &lv_font_montserrat_14,
             UI_COLOR_MUTED, 30, 134, 180, 18);
    label_at(s_content, "MESSAGE ID", &lv_font_montserrat_14,
             UI_COLOR_MUTED, 14, 184, 100, 18);
    label_at(s_content, "FRAME", &lv_font_montserrat_14,
             UI_COLOR_MUTED, 14, 205, 100, 18);
    label_at(s_content, "SIGNAL", &lv_font_montserrat_14,
             UI_COLOR_MUTED, 14, 224, 100, 18);
    (void)snprintf(readout, sizeof(readout), "%04X", (unsigned)view->message_id);
    label_at(s_content, readout, &lv_font_montserrat_14,
             UI_COLOR_INK, 134, 184, 90, 18);
    (void)snprintf(readout, sizeof(readout), "%u / %u",
                   (unsigned)view->received_fragments,
                   (unsigned)view->expected_fragments);
    label_at(s_content, readout, &lv_font_montserrat_14,
             UI_COLOR_INK, 134, 205, 90, 18);
    label_at(s_content, "ACTIVE", &lv_font_montserrat_14,
             UI_COLOR_ACTIVE, 134, 224, 90, 18);
    set_footer("", "", "Hold Stop");
}

static void render_text_result(const sonic_runtime_view_t *view, bool url)
{
    sonic_ui_page_t page = {0u, view->payload_length};
    char content[SONIC_MAX_MESSAGE_BYTES + 1u];
    char title[32];
    size_t page_count = sonic_ui_page_count(view);
    int32_t content_height;
    int32_t page_label_y;
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
    content_height = text_height(content, &lv_font_montserrat_14, 192);
    page_label_y = 88 + content_height + 8;
    (void)snprintf(title, sizeof(title), "%s", url ? "URL" : "Message");
    set_chip(s_content, kind, UI_COLOR_GREEN, UI_COLOR_GREEN_TINT);
    label_at(s_content, title, &lv_font_montserrat_20,
             UI_COLOR_INK, 16, 43, 208, 28);
    (void)make_card(s_content, 14, 78, 212, content_height + 20,
                    UI_COLOR_SURFACE);
    label_at(s_content, content, &lv_font_montserrat_14,
             UI_COLOR_INK, 24, 88, 192, content_height);
    if (page_count > 1u) {
        char page_label[20];
        (void)snprintf(page_label, sizeof(page_label), "%u / %u",
                       (unsigned)(s_page_index + 1u), (unsigned)page_count);
        label_at(s_content, page_label, &lv_font_montserrat_14,
                 UI_COLOR_MUTED, 80, page_label_y, 80, 18);
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
             14, y, 98, 18);
    label_at(parent, value, &lv_font_montserrat_14, UI_COLOR_INK,
             114, y, 112, 18);
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(line, 212, 1);
    lv_obj_set_pos(line, 14, y + 18);
    lv_obj_set_style_bg_color(line, color(UI_COLOR_LINE), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
}

static void diag_profile_row(lv_obj_t *parent, const char *value, int y)
{
    label_at(parent, "Profile", &lv_font_montserrat_14, UI_COLOR_MUTED,
             14, y, 78, 18);
    label_at(parent, value, &lv_font_montserrat_14, UI_COLOR_INK,
             98, y, 128, 18);
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(line, 212, 1);
    lv_obj_set_pos(line, 14, y + 18);
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
    diag_profile_row(s_content, profile, 159);
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
    const int32_t title_y = 44;
    const int32_t title_height = text_height(title, &lv_font_montserrat_20, 216);
    const int32_t copy_y = title_y + title_height + 8;
    const int32_t copy_height = text_height(copy, &lv_font_montserrat_14, 204);

    set_chip(s_content, "Attention", UI_COLOR_AMBER, UI_COLOR_AMBER_TINT);
    label_at(s_content, title, &lv_font_montserrat_20,
             UI_COLOR_INK, 12, title_y, 216, title_height);
    label_at(s_content, copy, &lv_font_montserrat_14,
             UI_COLOR_MUTED, 18, copy_y, 204, copy_height);
    if (incomplete) {
        const int32_t progress_y = copy_y + copy_height + 12;
        (void)snprintf(progress, sizeof(progress), "%u / %u parts",
                       (unsigned)view->received_fragments,
                       (unsigned)view->expected_fragments);
        label_at(s_content, progress, &lv_font_montserrat_14,
                 UI_COLOR_INK, 18, progress_y, 204, 20);
        set_footer("", "OK Listen", "Hold Back");
    } else {
        set_footer("", "OK Again", "Hold Back");
    }
}

static void render_audio_error(const char *title, const char *copy)
{
    const int32_t title_y = 44;
    const int32_t title_height = text_height(title, &lv_font_montserrat_20, 216);
    const int32_t copy_y = title_y + title_height + 8;
    const int32_t copy_height = text_height(copy, &lv_font_montserrat_14, 204);

    set_chip(s_content, "Audio error", UI_COLOR_RED, UI_COLOR_RED_TINT);
    label_at(s_content, title, &lv_font_montserrat_20,
             UI_COLOR_INK, 12, title_y, 216, title_height);
    label_at(s_content, copy, &lv_font_montserrat_14,
             UI_COLOR_MUTED, 18, copy_y, 204, copy_height);
    set_footer("", "OK Retry", "Hold Back");
}

static void render_send_menu(const sonic_runtime_view_t *view)
{
    static const char *items[] = {"Hello", "Demo URL", "Device Card", "Test Token"};
    char row[32];
    char index[16];
    const size_t selected = (size_t)view->preset_selection % SONIC_RUNTIME_PRESET_COUNT;
    const size_t previous = (selected + SONIC_RUNTIME_PRESET_COUNT - 1u) % SONIC_RUNTIME_PRESET_COUNT;
    const size_t next = (selected + 1u) % SONIC_RUNTIME_PRESET_COUNT;
    char previous_label[24];
    char next_label[24];
    lv_obj_t *panel = make_card(s_content, 12, 91, 216, 118, UI_COLOR_LIGHT);
    lv_obj_set_style_bg_color(panel, color(UI_COLOR_LIGHT), 0);
    lv_obj_set_style_border_color(panel, color(0x8D8A81u), 0);
    lv_obj_set_style_radius(panel, 3, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    (void)snprintf(index, sizeof(index), "%02u / 04",
                   (unsigned)view->preset_selection + 1u);
    label_at(panel, index, &lv_font_montserrat_14, UI_COLOR_LIGHT_TEXT,
             76, 5, 64, 18);
    label_at(panel, items[selected], &lv_font_montserrat_20, UI_COLOR_LIGHT_TEXT,
             12, 25, 184, 28);
    (void)snprintf(row, sizeof(row), "TYPE  %s", payload_type_name(view->payload_type));
    label_at(panel, row, &lv_font_montserrat_14, UI_COLOR_LIGHT_TEXT,
             12, 56, 184, 18);
    (void)snprintf(row, sizeof(row), "SIZE  %u B", (unsigned)view->payload_length);
    label_at(panel, row, &lv_font_montserrat_14, UI_COLOR_LIGHT_TEXT,
             12, 76, 184, 18);
    (void)snprintf(row, sizeof(row), "FRAMES  %u", (unsigned)view->frame_count);
    label_at(panel, row, &lv_font_montserrat_14, UI_COLOR_LIGHT_TEXT,
             12, 96, 184, 18);
    lv_obj_t *neighbors = make_card(s_content, 8, 215, 224, 27, UI_COLOR_SURFACE);
    (void)snprintf(previous_label, sizeof(previous_label), "< %s", items[previous]);
    (void)snprintf(next_label, sizeof(next_label), "%s >", items[next]);
    label_at(neighbors, previous_label, &lv_font_montserrat_14,
             UI_COLOR_MUTED, 3, 4, 100, 18);
    label_at(neighbors, next_label, &lv_font_montserrat_14,
             UI_COLOR_INK, 109, 4, 100, 18);
    set_footer("UP/DN", "OK Open", "Hold Back");
}

static void render_send_preview(const sonic_runtime_view_t *view)
{
    char payload[SONIC_MAX_MESSAGE_BYTES + 1u];
    char preview[44];
    char details[44];
    char estimate[24];
    size_t payload_length = view->payload_length;
    uint32_t milliseconds = (uint32_t)view->frame_count * 1200u;

    if (view->payload_type == SONIC_TYPE_DEVICE_INFO) {
        sonic_device_info_t info;
        if (sonic_device_info_parse(view->payload, view->payload_length,
                                    &info) == SONIC_OK) {
            (void)snprintf(payload, sizeof(payload), "Model %u\nSonic %u.%u.%u",
                           (unsigned)info.model_id,
                           (unsigned)info.sonic_major,
                           (unsigned)info.sonic_minor,
                           (unsigned)info.sonic_patch);
        } else {
            (void)snprintf(payload, sizeof(payload), "Device information card");
        }
        payload_length = strlen(payload);
    } else if (view->payload_type == SONIC_TYPE_TOKEN) {
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

    size_t preview_length = payload_length;
    if (preview_length > 36u) {
        preview_length = 36u;
        while (preview_length > 0u &&
               (((uint8_t)payload[preview_length] & 0xC0u) == 0x80u)) {
            --preview_length;
        }
    }
    memcpy(preview, payload, preview_length);
    if (preview_length < payload_length) {
        memcpy(preview + preview_length, "...", 4u);
    } else {
        preview[preview_length] = '\0';
    }

    set_chip_at(s_content, "PREVIEW - READ ONLY", UI_COLOR_ACTIVE,
                UI_COLOR_SURFACE, 52);
    label_at(s_content, view->preset_selection == SONIC_RUNTIME_PRESET_HELLO
                           ? "Hello"
                           : view->preset_selection == SONIC_RUNTIME_PRESET_DEMO_URL
                                 ? "Demo URL"
                                 : view->preset_selection == SONIC_RUNTIME_PRESET_DEVICE_CARD
                                       ? "Device Card" : "Test Token",
             &lv_font_montserrat_20, UI_COLOR_INK, 16, 78, 208, 28);
    lv_obj_t *card = make_card(s_content, 14, 110, 212, 92, UI_COLOR_SURFACE);
    label_at(card, view->payload_type == SONIC_TYPE_TEXT ? "TEXT" :
                  view->payload_type == SONIC_TYPE_URL ? "URL" :
                  view->payload_type == SONIC_TYPE_TOKEN ? "TOKEN" : "DEVICE_INFO",
             &lv_font_montserrat_14, UI_COLOR_MUTED, 8, 3, 184, 18);
    label_at(card, preview, &lv_font_montserrat_14,
             UI_COLOR_INK, 8, 25, 184, 60);
    (void)snprintf(details, sizeof(details), "%u B / %u fr / %.8s",
                   (unsigned)view->payload_length,
                   (unsigned)view->frame_count,
                   estimate);
    label_at(s_content, details, &lv_font_montserrat_14,
             UI_COLOR_MUTED, 14, 208, 212, 20);
    set_footer("", "OK Send", "Hold Back");
}

static void render_tx_playing(const sonic_runtime_view_t *view)
{
    char progress[20];
    size_t current = view->frame_index;

    label_at(s_content, "PLAYING SOUND", &lv_font_montserrat_20,
             UI_COLOR_INK, 16, 65, 208, 27);
    label_at(s_content, "Keep the phone nearby.", &lv_font_montserrat_14,
             UI_COLOR_MUTED, 20, 93, 200, 18);
    create_meter(104);
    (void)snprintf(progress, sizeof(progress), "%u / %u",
                   (unsigned)current, (unsigned)view->frame_count);
    label_at(s_content, progress, &lv_font_montserrat_20,
             UI_COLOR_ACTIVE, 30, 169, 180, 28);
    label_at(s_content, "FRAMES PLAYED", &lv_font_montserrat_14,
             UI_COLOR_MUTED, 30, 196, 180, 18);
    set_footer("", "", "Hold Stop");
}

static void render_tx_terminal(const sonic_runtime_view_t *view)
{
    if (view->state == SONIC_RUNTIME_TX_COMPLETE) {
        set_chip(s_content, "SENT", UI_COLOR_GREEN, UI_COLOR_GREEN_TINT);
        label_at(s_content, "Transmission complete",
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
    static const char *tabs[] = {"SYS", "AUDIO", "CODEC", "LAST"};
    char value[36];
    const int y = 85;
    const int page = (int)view->diagnostics_page;
    static const int32_t tab_y = 52;
    static const int32_t row_pitch = 20;

    for (int i = 0; i < 4; ++i) {
        lv_obj_t *tab = make_card(s_content, 14 + i * 53, tab_y, 52, 25,
                                  i == page ? UI_COLOR_LIGHT : UI_COLOR_SURFACE);
        lv_obj_set_style_pad_all(tab, 0, 0);
        lv_obj_set_style_radius(tab, 2, 0);
        lv_obj_set_style_border_color(tab, color(i == page ? 0x9A968Du : UI_COLOR_LINE), 0);
        lv_obj_t *tab_label = make_label(tab, tabs[i], &lv_font_montserrat_14,
                                         i == page ? UI_COLOR_LIGHT_TEXT : UI_COLOR_MUTED,
                                         50, 18);
        lv_obj_set_style_text_align(tab_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_letter_space(tab_label, -1, 0);
        lv_obj_set_pos(tab_label, 0, 3);
    }
    switch (view->diagnostics_page) {
    case SONIC_RUNTIME_DIAG_SYSTEM:
        diag_row(s_content, "Sonic Link", context->sonic_version, y);
        diag_row(s_content, "Build", context->build_fingerprint, y + row_pitch);
        if (context->battery_percent >= 0) {
            (void)snprintf(value, sizeof(value), "%d%%", context->battery_percent);
        } else {
            (void)snprintf(value, sizeof(value), "-");
        }
        diag_row(s_content, "Battery", value, y + 2 * row_pitch);
        (void)snprintf(value, sizeof(value), "%d B", context->free_heap_bytes);
        diag_row(s_content, "Free heap", context->free_heap_bytes >= 0 ? value : "-", y + 3 * row_pitch);
        (void)snprintf(value, sizeof(value), "%d B", context->minimum_free_heap_bytes);
        diag_row(s_content, "Min heap", context->minimum_free_heap_bytes >= 0 ? value : "-", y + 4 * row_pitch);
        (void)snprintf(value, sizeof(value), "%d B", context->largest_free_block_bytes);
        diag_row(s_content, "Largest blk", context->largest_free_block_bytes >= 0 ? value : "-", y + 5 * row_pitch);
        break;
    case SONIC_RUNTIME_DIAG_AUDIO:
        (void)snprintf(value, sizeof(value), "%ukHz/%ub/1ch",
                       (unsigned)(context->sample_rate_hz / 1000u),
                       (unsigned)context->sample_bits);
        diag_row(s_content, "Format", context->sample_rate_hz ? value : "-", y);
        (void)snprintf(value, sizeof(value), "%.1f dBFS",
                       context->audio_diagnostics_available
                           ? (double)context->microphone_dbfs : 0.0);
        diag_row(s_content, "Mic level",
                 context->audio_diagnostics_available ? value : "-", y + row_pitch);
        (void)snprintf(value, sizeof(value), "%u%%",
                       (unsigned)context->speaker_volume_percent);
        diag_row(s_content, "Volume", value, y + 2 * row_pitch);
        diag_row(s_content, "Worker", audio_state_name(context->audio_state), y + 3 * row_pitch);
        (void)snprintf(value, sizeof(value), "%u words",
                       (unsigned)context->worker_stack_high_water_words);
        diag_row(s_content, "Stack free",
                 context->audio_diagnostics_available ? value : "-", y + 4 * row_pitch);
        break;
    case SONIC_RUNTIME_DIAG_CODEC:
        diag_row(s_content, "ggwave", context->ggwave_version, y);
        diag_profile_row(s_content,
                         context->acoustic_profile == 1u ? "Audible Fastest" :
                         context->acoustic_profile == 2u ? "Audible Fast" : "Unknown",
                         y + row_pitch);
        diag_row(s_content, "Frame", "40 bytes", y + 2 * row_pitch);
        diag_row(s_content, "Max message", "93 bytes", y + 3 * row_pitch);
        diag_row(s_content, "DSS", "Enabled", y + 4 * row_pitch);
        (void)snprintf(value, sizeof(value), "%d B", context->codec_heap_bytes);
        diag_row(s_content, "Codec heap", context->codec_heap_bytes >= 0 ? value : "-", y + 5 * row_pitch);
        (void)snprintf(value, sizeof(value), "%llu us",
                       (unsigned long long)context->rx_average_us);
        diag_row(s_content, "RX avg", context->audio_diagnostics_available ? value : "-", y + 6 * row_pitch);
        (void)snprintf(value, sizeof(value), "%llu us",
                       (unsigned long long)context->rx_p99_us);
        diag_row(s_content, "RX p99", context->audio_diagnostics_available ? value : "-", y + 7 * row_pitch);
        break;
    case SONIC_RUNTIME_DIAG_LAST_TRANSFER: {
        const sonic_runtime_last_transfer_t *transfer = &view->last_transfer;
        if (!transfer->valid) {
            diag_row(s_content, "Transfer", "None", y);
            break;
        }
        diag_row(s_content, "Direction", transfer->is_rx ? "Receive" : "Send", y);
        diag_row(s_content, "Type", payload_type_name(transfer->type), y + row_pitch);
        (void)snprintf(value, sizeof(value), "%u / %u B",
                       (unsigned)transfer->frame_count,
                       (unsigned)transfer->byte_length);
        diag_row(s_content, "Frames / B", value, y + 2 * row_pitch);
        (void)snprintf(value, sizeof(value), "%u ms",
                       (unsigned)transfer->duration_ms);
        diag_row(s_content, "Duration", value, y + 3 * row_pitch);
        (void)snprintf(value, sizeof(value), "%04X",
                       (unsigned)transfer->message_id);
        diag_row(s_content, "Message ID", value, y + 4 * row_pitch);
        diag_row(s_content, "Result", transfer_result_name(transfer->result), y + 5 * row_pitch);
        diag_row(s_content, "Error", sonic_error_name(transfer->error), y + 6 * row_pitch);
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
    lv_obj_set_pos(header, 7, 7);
    lv_obj_set_size(header, 226, 28);
    lv_obj_set_style_bg_color(header, color(0x101815u), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, color(0x39423Du), 0);
    lv_obj_set_style_radius(header, 3, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_t *wave = make_label(header, ")))", &lv_font_montserrat_14,
                                UI_COLOR_ACTIVE, 24, 20);
    lv_obj_align(wave, LV_ALIGN_LEFT_MID, 8, 0);
    s_header_title = make_label(header, "SONIC LINK", &lv_font_montserrat_14,
                                UI_COLOR_INK, 112, 20);
    lv_obj_align(s_header_title, LV_ALIGN_LEFT_MID, 34, 0);
    lv_obj_t *battery_outline = lv_obj_create(header);
    lv_obj_remove_flag(battery_outline, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(battery_outline, 169, 9);
    lv_obj_set_size(battery_outline, 17, 9);
    lv_obj_set_style_bg_opa(battery_outline, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(battery_outline, color(UI_COLOR_INK), 0);
    lv_obj_set_style_border_width(battery_outline, 1, 0);
    lv_obj_set_style_radius(battery_outline, 1, 0);
    s_header_battery_fill = lv_obj_create(header);
    lv_obj_remove_flag(s_header_battery_fill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_header_battery_fill, 172, 12);
    lv_obj_set_size(s_header_battery_fill, 9, 3);
    lv_obj_set_style_bg_color(s_header_battery_fill, color(UI_COLOR_INK), 0);
    lv_obj_set_style_bg_opa(s_header_battery_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_header_battery_fill, 0, 0);
    lv_obj_t *battery_tip = lv_obj_create(header);
    lv_obj_remove_flag(battery_tip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(battery_tip, 187, 12);
    lv_obj_set_size(battery_tip, 2, 3);
    lv_obj_set_style_bg_color(battery_tip, color(UI_COLOR_INK), 0);
    lv_obj_set_style_bg_opa(battery_tip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(battery_tip, 0, 0);
    s_header_battery = make_label(header, "", &lv_font_montserrat_14,
                                  UI_COLOR_INK, 31, 20);
    lv_obj_set_style_text_align(s_header_battery, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_header_battery, LV_ALIGN_RIGHT_MID, -8, 0);

    s_subsection = make_label(s_screen, "SELECT PRESET / READ ONLY",
                              &lv_font_montserrat_14, UI_COLOR_INK, 222, 38);
    lv_obj_set_pos(s_subsection, 9, 79);
    lv_obj_set_style_bg_color(s_subsection, color(0x111A17u), 0);
    lv_obj_set_style_bg_opa(s_subsection, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_subsection, color(0x39423Du), 0);
    lv_obj_set_style_border_width(s_subsection, 1, 0);
    lv_obj_set_style_radius(s_subsection, 3, 0);
    lv_obj_set_style_pad_all(s_subsection, 0, 0);
    lv_obj_set_style_text_color(s_subsection, color(UI_COLOR_INK), 0);
    lv_obj_set_style_text_align(s_subsection, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_subsection, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_line_space(s_subsection, 1, 0);
    lv_obj_set_style_pad_top(s_subsection, 8, 0);
    lv_obj_add_flag(s_subsection, LV_OBJ_FLAG_HIDDEN);

    s_content = lv_obj_create(s_screen);
    lv_obj_remove_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_content, 0, 28);
    lv_obj_set_size(s_content, 240, 249);
    lv_obj_set_style_bg_opa(s_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_content, 0, 0);
    lv_obj_set_style_pad_all(s_content, 0, 0);

    s_section = make_label(s_screen, "MODE SELECT", &lv_font_montserrat_14,
                           UI_COLOR_LIGHT_TEXT, 222, 38);
    lv_obj_set_pos(s_section, 9, 39);
    lv_obj_set_style_bg_color(s_section, color(UI_COLOR_LIGHT), 0);
    lv_obj_set_style_bg_opa(s_section, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_section, color(0x8D8A82u), 0);
    lv_obj_set_style_border_width(s_section, 1, 0);
    lv_obj_set_style_radius(s_section, 3, 0);
    lv_obj_set_style_pad_left(s_section, 12, 0);
    lv_obj_set_style_pad_top(s_section, 10, 0);
    lv_obj_set_style_text_color(s_section, color(UI_COLOR_LIGHT_TEXT), 0);

    lv_obj_t *footer = lv_obj_create(s_screen);
    lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(footer, 7, 277);
    lv_obj_set_size(footer, 226, 36);
    lv_obj_set_style_bg_color(footer, color(0x101815u), 0);
    lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(footer, 1, 0);
    lv_obj_set_style_border_color(footer, color(0x39413Du), 0);
    lv_obj_set_style_radius(footer, 3, 0);
    lv_obj_set_style_pad_all(footer, 0, 0);
    for (size_t i = 0u; i < 3u; ++i) {
        static const int32_t footer_x[] = {0, 50, 142};
        static const int32_t footer_width[] = {50, 92, 84};
        s_footer_labels[i] = make_label(footer, "", &lv_font_montserrat_14,
                                        UI_COLOR_INK, footer_width[i], 24);
        lv_obj_set_style_text_align(s_footer_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s_footer_labels[i], footer_x[i], 6);
    }
    lv_screen_load(s_screen);
    s_has_previous = false;
    s_page_index = 0u;
    s_previous_page_index = 0u;
    s_meter_phase = 0u;
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
    s_header_battery = NULL;
    s_header_battery_fill = NULL;
    s_section = NULL;
    s_subsection = NULL;
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
            render_receive_listening(view);
            break;
        case SONIC_RUNTIME_RX_PAUSED:
            render_receive_listening(view);
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
            render_audio_error("Invalid message",
                               "The received data could not be validated.");
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
            set_chip(s_content, "PREPARING", UI_COLOR_ACTIVE,
                     UI_COLOR_SURFACE);
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
