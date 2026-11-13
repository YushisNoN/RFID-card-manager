#include "gui.h"

#include <stdint.h>
#include <stdbool.h>

#include "esp_log.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

#include "esp_lvgl_port.h"
#include "lvgl.h"

LV_FONT_DECLARE(rfid_font_18);

static const char *TAG = "GUI";

#define LCD_HOST SPI3_HOST

#define PIN_NUM_CS      15
#define PIN_NUM_DC      2
#define PIN_NUM_RST     4
#define PIN_NUM_MOSI    12
#define PIN_NUM_SCLK    13
#define PIN_NUM_MISO    14

#define LCD_H_RES       240
#define LCD_V_RES       320

#define LCD_PIXEL_CLOCK_HZ (20 * 1000 * 1000)

// ---------------- Colors ----------------

#define COLOR_BG_TOP   0x0E1530
#define COLOR_BG_BOT   0x070B16
#define COLOR_PANEL    0x151F35
#define COLOR_PANEL2   0x243453
#define COLOR_CYAN     0x35D6F5
#define COLOR_BLUE     0x4C8DFF
#define COLOR_GREEN    0x36E69A
#define COLOR_RED      0xFF536B
#define COLOR_WHITE    0xF2F6FF
#define COLOR_GRAY     0x8493AD

#define HERO_SIZE      140
#define WAVE_COUNT     3
#define WAVE_CYCLE_MS  1500

// ---------------- Objects ----------------

static lv_obj_t *halo;
static lv_obj_t *ring;
static lv_obj_t *disc;
static lv_obj_t *dot;
static lv_obj_t *waves[WAVE_COUNT];
static lv_obj_t *check_line;
static lv_obj_t *cross_line1;
static lv_obj_t *cross_line2;

static lv_obj_t *info_card;
static lv_obj_t *status_label;
static lv_obj_t *hint_label;
static lv_obj_t *timer_bar;

static lv_timer_t *return_timer;

static GUI_State current_state;
static bool state_valid = false;

static void set_state(GUI_State state);

// Points for the drawn icons (must live as long as the lines)
static const lv_point_precise_t check_pts[] = {
    {10, 31}, {24, 45}, {50, 17}
};
static const lv_point_precise_t cross_pts1[] = {
    {14, 14}, {46, 46}
};
static const lv_point_precise_t cross_pts2[] = {
    {46, 14}, {14, 46}
};

// --------------------------------------------------
// Animations
// --------------------------------------------------

static void anim_opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void anim_rotation_cb(void *obj, int32_t v)
{
    lv_arc_set_rotation((lv_obj_t *)obj, v);
}

static void anim_bar_cb(void *obj, int32_t v)
{
    lv_bar_set_value((lv_obj_t *)obj, v, LV_ANIM_OFF);
}

// One animation drives the halo and all waves, so they never desync
static void anim_waiting_cb(void *obj, int32_t v)
{
    (void)obj;

    int32_t h = (v < WAVE_CYCLE_MS / 2) ? v : (WAVE_CYCLE_MS - v);
    lv_obj_set_style_opa(halo, (lv_opa_t)(30 + h * 140 / (WAVE_CYCLE_MS / 2)), 0);

    for (int i = 0; i < WAVE_COUNT; i++) {
        int32_t t = v - i * 200;
        lv_opa_t opa = 50;
        if (t > 0 && t < 600) {
            int32_t tri = (t <= 300) ? t : (600 - t);
            opa = (lv_opa_t)(50 + tri * 205 / 300);
        }
        lv_obj_set_style_opa(waves[i], opa, 0);
    }
}

static void start_waiting_animation(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, halo);
    lv_anim_set_exec_cb(&a, anim_waiting_cb);
    lv_anim_set_values(&a, 0, WAVE_CYCLE_MS);
    lv_anim_set_duration(&a, WAVE_CYCLE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void start_pulse(lv_obj_t *obj, int32_t from, int32_t to,
                        uint32_t duration, uint32_t delay)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, duration);
    lv_anim_set_playback_duration(&a, duration);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void start_spinner(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ring);
    lv_anim_set_exec_cb(&a, anim_rotation_cb);
    lv_anim_set_values(&a, 0, 360);
    lv_anim_set_duration(&a, 1100);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void start_countdown(uint32_t ms)
{
    lv_bar_set_value(timer_bar, 100, LV_ANIM_OFF);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, timer_bar);
    lv_anim_set_exec_cb(&a, anim_bar_cb);
    lv_anim_set_values(&a, 100, 0);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
}

static void stop_all_animations(void)
{
    lv_anim_delete(halo, NULL);
    lv_anim_delete(ring, NULL);
    lv_anim_delete(timer_bar, NULL);
    for (int i = 0; i < WAVE_COUNT; i++) {
        lv_anim_delete(waves[i], NULL);
    }

    lv_obj_set_style_opa(halo, LV_OPA_COVER, 0);
    for (int i = 0; i < WAVE_COUNT; i++) {
        lv_obj_set_style_opa(waves[i], LV_OPA_COVER, 0);
    }
    lv_arc_set_rotation(ring, 0);
}

// --------------------------------------------------
// UI helpers
// --------------------------------------------------

static lv_obj_t *make_box(lv_obj_t *parent, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    return o;
}

static void set_accent_color(uint32_t color)
{
    lv_color_t c = lv_color_hex(color);

    lv_obj_set_style_border_color(halo, c, 0);
    lv_obj_set_style_arc_color(ring, c, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(dot, c, 0);

    for (int i = 0; i < WAVE_COUNT; i++) {
        lv_obj_set_style_arc_color(waves[i], c, LV_PART_MAIN);
    }

    lv_obj_set_style_line_color(check_line, c, 0);
    lv_obj_set_style_line_color(cross_line1, c, 0);
    lv_obj_set_style_line_color(cross_line2, c, 0);

    lv_obj_set_style_border_color(info_card, c, 0);
    lv_obj_set_style_bg_color(timer_bar, c, LV_PART_INDICATOR);
}

static void show_waves(bool show)
{
    for (int i = 0; i < WAVE_COUNT; i++) {
        if (show) lv_obj_remove_flag(waves[i], LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(waves[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (show) lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
}

static void show_check(bool show)
{
    if (show) lv_obj_remove_flag(check_line, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(check_line, LV_OBJ_FLAG_HIDDEN);
}

static void show_cross(bool show)
{
    if (show) {
        lv_obj_remove_flag(cross_line1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(cross_line2, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(cross_line1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cross_line2, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_text(const char *status, const char *hint)
{
    lv_label_set_text(status_label, status);
    lv_label_set_text(hint_label, hint);
}

// --------------------------------------------------
// State management
// --------------------------------------------------

static void return_to_waiting(lv_timer_t *timer)
{
    (void)timer;
    return_timer = NULL;
    set_state(GUI_STATE_WAITING);
}

static void set_state(GUI_State state)
{
    // Repeated WAITING/READING must not restart the animation
    if (state_valid && state == current_state &&
        (state == GUI_STATE_WAITING || state == GUI_STATE_READING)) {
        return;
    }
    current_state = state;
    state_valid = true;

    if (return_timer != NULL) {
        lv_timer_delete(return_timer);
        return_timer = NULL;
    }

    stop_all_animations();

    // defaults
    show_waves(false);
    show_check(false);
    show_cross(false);
    lv_obj_add_flag(timer_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(halo, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_arc_opa(ring, LV_OPA_TRANSP, LV_PART_INDICATOR);

    switch (state) {

        case GUI_STATE_WAITING:
            set_accent_color(COLOR_CYAN);
            show_waves(true);
            set_text("Приложите карту", "Поднесите её к считывателю");
            start_waiting_animation();
            break;

        case GUI_STATE_READING:
            set_accent_color(COLOR_BLUE);
            show_waves(true);
            set_text("Проверяем карту...", "Связываемся с сервером");

            lv_arc_set_angles(ring, 0, 110);
            lv_obj_set_style_arc_opa(ring, LV_OPA_COVER, LV_PART_INDICATOR);
            start_spinner();
            start_pulse(halo, 30, 140, 700, 0);
            break;

        case GUI_STATE_SUCCESS:
            set_accent_color(COLOR_GREEN);
            show_check(true);
            set_text("Карта прочитана!", "Можете забрать плату");

            lv_obj_add_flag(halo, LV_OBJ_FLAG_HIDDEN);
            lv_arc_set_rotation(ring, 270);
            lv_arc_set_angles(ring, 0, 360);
            lv_obj_set_style_arc_opa(ring, LV_OPA_COVER, LV_PART_INDICATOR);

            lv_obj_remove_flag(timer_bar, LV_OBJ_FLAG_HIDDEN);
            start_countdown(5000);
            return_timer = lv_timer_create(return_to_waiting, 5000, NULL);
            break;

        case GUI_STATE_ERROR:
            set_accent_color(COLOR_RED);
            show_cross(true);
            set_text("Карта не распознана", "Попробуйте ещё раз");

            lv_obj_add_flag(halo, LV_OBJ_FLAG_HIDDEN);
            lv_arc_set_rotation(ring, 270);
            lv_arc_set_angles(ring, 0, 360);
            lv_obj_set_style_arc_opa(ring, LV_OPA_COVER, LV_PART_INDICATOR);

            lv_obj_remove_flag(timer_bar, LV_OBJ_FLAG_HIDDEN);
            start_countdown(3000);
            return_timer = lv_timer_create(return_to_waiting, 3000, NULL);
            break;
    }
}

// --------------------------------------------------
// UI creation
// --------------------------------------------------

static lv_obj_t *make_line(lv_obj_t *parent, const lv_point_precise_t *pts,
                           uint32_t count)
{
    lv_obj_t *l = lv_line_create(parent);
    lv_line_set_points(l, pts, count);
    lv_obj_set_style_line_width(l, 6, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    return l;
}

static void create_ui(void)
{
    lv_obj_t *screen = lv_screen_active();

    // Background: vertical gradient
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG_TOP), 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(COLOR_BG_BOT), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    // ---------- Header ----------
    lv_obj_t *header = lv_label_create(screen);
    lv_label_set_text(header, "RFID TERMINAL");
    lv_obj_set_style_text_font(header, &rfid_font_18, 0);
    lv_obj_set_style_text_color(header, lv_color_hex(COLOR_CYAN), 0);
    lv_obj_set_style_text_letter_space(header, 3, 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *subtitle = lv_label_create(screen);
    lv_label_set_text(subtitle, "СИСТЕМА ИДЕНТИФИКАЦИИ");
    lv_obj_set_style_text_font(subtitle, &rfid_font_18, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(COLOR_GRAY), 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 36);

    // ---------- Hero (animated icon) ----------
    lv_obj_t *hero = make_box(screen, HERO_SIZE, HERO_SIZE);
    lv_obj_align(hero, LV_ALIGN_TOP_MID, 0, 72);

    // Pulsing halo
    halo = make_box(hero, HERO_SIZE, HERO_SIZE);
    lv_obj_center(halo);
    lv_obj_set_style_radius(halo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(halo, 2, 0);
    lv_obj_set_style_border_color(halo, lv_color_hex(COLOR_CYAN), 0);

    // Progress / status ring
    ring = lv_arc_create(hero);
    lv_obj_set_size(ring, 124, 124);
    lv_obj_center(ring);
    lv_arc_set_rotation(ring, 0);
    lv_arc_set_bg_angles(ring, 0, 360);
    lv_arc_set_angles(ring, 0, 110);
    lv_obj_remove_style(ring, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, 0, 0);
    lv_obj_set_style_arc_width(ring, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring, lv_color_hex(COLOR_PANEL2), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(ring, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(ring, LV_OPA_TRANSP, LV_PART_INDICATOR);

    // Inner disc
    disc = make_box(hero, 96, 96);
    lv_obj_center(disc);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);

    // Contactless icon: dot + 3 arcs
    const int wave_sizes[WAVE_COUNT] = {28, 44, 60};
    for (int i = 0; i < WAVE_COUNT; i++) {
        lv_obj_t *w = lv_arc_create(disc);
        lv_obj_set_size(w, wave_sizes[i], wave_sizes[i]);
        lv_obj_align(w, LV_ALIGN_CENTER, -13, 0);
        lv_arc_set_bg_angles(w, 315, 45);
        lv_obj_remove_style(w, NULL, LV_PART_KNOB);
        lv_obj_remove_flag(w, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(w, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(w, 0, 0);
        lv_obj_set_style_arc_width(w, 4, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(w, true, LV_PART_MAIN);
        lv_obj_set_style_arc_color(w, lv_color_hex(COLOR_CYAN), LV_PART_MAIN);
        lv_obj_set_style_arc_opa(w, LV_OPA_TRANSP, LV_PART_INDICATOR);
        waves[i] = w;
    }

    dot = make_box(disc, 8, 8);
    lv_obj_align(dot, LV_ALIGN_CENTER, -13, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(COLOR_CYAN), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

    // Check / cross icons
    lv_obj_t *icon_box = make_box(disc, 60, 60);
    lv_obj_center(icon_box);

    check_line  = make_line(icon_box, check_pts, 3);
    cross_line1 = make_line(icon_box, cross_pts1, 2);
    cross_line2 = make_line(icon_box, cross_pts2, 2);

    // ---------- Info card ----------
    info_card = lv_obj_create(screen);
    lv_obj_set_size(info_card, 220, 76);
    lv_obj_align(info_card, LV_ALIGN_TOP_MID, 0, 222);
    lv_obj_remove_flag(info_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(info_card, 18, 0);
    lv_obj_set_style_bg_color(info_card, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(info_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(info_card, 2, 0);
    lv_obj_set_style_border_color(info_card, lv_color_hex(COLOR_CYAN), 0);
    lv_obj_set_style_border_opa(info_card, LV_OPA_60, 0);
    lv_obj_set_style_shadow_width(info_card, 0, 0);
    lv_obj_set_style_pad_all(info_card, 8, 0);
    lv_obj_set_style_pad_row(info_card, 4, 0);
    lv_obj_set_flex_flow(info_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info_card,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    status_label = lv_label_create(info_card);
    lv_obj_set_width(status_label, 200);
    lv_obj_set_style_text_font(status_label, &rfid_font_18, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(COLOR_WHITE), 0);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);

    hint_label = lv_label_create(info_card);
    lv_obj_set_width(hint_label, 200);
    lv_obj_set_style_text_font(hint_label, &rfid_font_18, 0);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(COLOR_GRAY), 0);
    lv_obj_set_style_text_align(hint_label, LV_TEXT_ALIGN_CENTER, 0);

    // ---------- Countdown bar ----------
    timer_bar = lv_bar_create(screen);
    lv_obj_set_size(timer_bar, 120, 4);
    lv_obj_align(timer_bar, LV_ALIGN_TOP_MID, 0, 306);
    lv_bar_set_range(timer_bar, 0, 100);
    lv_obj_set_style_radius(timer_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(timer_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(timer_bar, lv_color_hex(COLOR_PANEL2), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(timer_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(timer_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_add_flag(timer_bar, LV_OBJ_FLAG_HIDDEN);

    set_state(GUI_STATE_WAITING);
}

// --------------------------------------------------
// Display initialization
// --------------------------------------------------

esp_lcd_panel_handle_t gui_init(void)
{
    esp_err_t ret;

    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_NUM_SCLK,
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 40 * sizeof(uint16_t)
    };

    ret = spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);

    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI initialization failed: %s", esp_err_to_name(ret));
        return NULL;
    }

    esp_lcd_panel_io_handle_t io_handle = NULL;

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_NUM_DC,
        .cs_gpio_num = PIN_NUM_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10
    };

    ret = esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD IO initialization failed: %s", esp_err_to_name(ret));
        return NULL;
    }

    esp_lcd_panel_handle_t panel_handle = NULL;

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE
    };

    ret = esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Panel creation failed: %s", esp_err_to_name(ret));
        return NULL;
    }

    if (esp_lcd_panel_reset(panel_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Panel reset failed");
        return NULL;
    }

    if (esp_lcd_panel_init(panel_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Panel initialization failed");
        return NULL;
    }

    esp_lcd_panel_mirror(panel_handle, true, false);
    esp_lcd_panel_disp_on_off(panel_handle, true);

    lvgl_port_cfg_t lvgl_config = ESP_LVGL_PORT_INIT_CONFIG();

    lvgl_config.task_priority = 5;
    lvgl_config.task_max_sleep_ms = 20;
    lvgl_config.timer_period_ms = 5;

    ret = lvgl_port_init(&lvgl_config);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL initialization failed: %s", esp_err_to_name(ret));
        return NULL;
    }

    const lvgl_port_display_cfg_t display_config = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = LCD_H_RES * 40,
        .double_buffer = false,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false
        },
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .sw_rotate = false,
            .swap_bytes = false   // если цвета перепутаны, поставьте true
        }
    };

    lv_display_t *display = lvgl_port_add_disp(&display_config);

    if (display == NULL) {
        ESP_LOGE(TAG, "LVGL display creation failed");
        return NULL;
    }

    if (lvgl_port_lock(0)) {
        create_ui();
        lvgl_port_unlock();
    } else {
        ESP_LOGE(TAG, "LVGL lock failed");
        return NULL;
    }

    ESP_LOGI(TAG, "GUI initialized successfully");

    return panel_handle;
}

// --------------------------------------------------
// Public GUI functions
// --------------------------------------------------

void gui_show_waiting(void)
{
    if (lvgl_port_lock(0)) {
        set_state(GUI_STATE_WAITING);
        lvgl_port_unlock();
    }
}

void gui_show_reading(void)
{
    if (lvgl_port_lock(0)) {
        set_state(GUI_STATE_READING);
        lvgl_port_unlock();
    }
}

void gui_show_success(void)
{
    if (lvgl_port_lock(0)) {
        set_state(GUI_STATE_SUCCESS);
        lvgl_port_unlock();
    }
}

void gui_show_error(void)
{
    if (lvgl_port_lock(0)) {
        set_state(GUI_STATE_ERROR);
        lvgl_port_unlock();
    }
}