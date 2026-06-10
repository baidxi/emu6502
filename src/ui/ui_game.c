/*
 * NES Emulator - LVGL Game Screen UI (Canvas + Virtual Gamepad)
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include "ui_game.h"
#include "ui_common.h"
#include "../nes/nes_system.h"
#include "../nes/controller.h"

/* UI 组件 */
static lv_obj_t *container = NULL;
static lv_obj_t *canvas = NULL;
static lv_obj_t *debug_bar = NULL;
static lv_obj_t *debug_label = NULL;
static ui_game_ctrl_cb_t ctrl_cb = NULL;
static ui_game_btn_cb_t  btn_cb = NULL;

/* Canvas 缓冲区 */
static lv_color_t *canvas_buf = NULL;

/* 虚拟手柄按钮按下/释放回调 → NES 控制器 */
static void on_btn_pressed(lv_event_t *e)
{
    uint8_t mask = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (btn_cb) btn_cb(mask, true);
}

static void on_btn_released(lv_event_t *e)
{
    uint8_t mask = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (btn_cb) btn_cb(mask, false);
}

/* 控制按钮回调 (暂停/设置/返回) */
static void on_ctrl_btn_clicked(lv_event_t *e)
{
    int evt = (int)(uintptr_t)lv_event_get_user_data(e);
    if (ctrl_cb) {
        ctrl_cb(evt);
    }
}

lv_obj_t *ui_game_create(lv_obj_t *parent)
{
    container = lv_obj_create(parent);
    lv_obj_set_size(container, 800, 600);
    lv_obj_set_style_bg_color(container, lv_color_black(), 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_border_width(container, 0, 0);

    /* ===== 左侧 D-pad 区域 (130 x 560) ===== */
    lv_obj_t *dpad_area = lv_obj_create(container);
    lv_obj_set_size(dpad_area, 130, 560);
    lv_obj_align(dpad_area, LV_ALIGN_LEFT_MID, 0, -20);
    lv_obj_set_style_bg_opa(dpad_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dpad_area, 0, 0);
    lv_obj_set_style_pad_all(dpad_area, 0, 0);

    /* 方向键按钮 */
    static const struct {
        lv_align_t align;
        int x_offs, y_offs;
        const char *label;
        uint8_t mask;
    } dpad_btns[] = {
        { LV_ALIGN_CENTER,   0, -30, "^", BTN_UP },
        { LV_ALIGN_CENTER, -30,   0, "<", BTN_LEFT },
        { LV_ALIGN_CENTER,  30,   0, ">", BTN_RIGHT },
        { LV_ALIGN_CENTER,   0,  30, "v", BTN_DOWN },
    };

    for (int i = 0; i < 4; i++) {
        lv_obj_t *btn = lv_btn_create(dpad_area);
        lv_obj_set_size(btn, 50, 50);
        lv_obj_align(btn, dpad_btns[i].align, dpad_btns[i].x_offs, dpad_btns[i].y_offs);
        lv_obj_set_style_radius(btn, 8, 0);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, dpad_btns[i].label);
        lv_obj_center(label);

        lv_obj_add_event_cb(btn, on_btn_pressed,  LV_EVENT_PRESSED,  (void *)(uintptr_t)dpad_btns[i].mask);
        lv_obj_add_event_cb(btn, on_btn_released, LV_EVENT_RELEASED, (void *)(uintptr_t)dpad_btns[i].mask);
    }

    /* ===== 中间游戏画面 Canvas (540 x 560) ===== */
    lv_obj_t *screen_area = lv_obj_create(container);
    lv_obj_set_size(screen_area, 540, 560);
    lv_obj_align(screen_area, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_opa(screen_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen_area, 0, 0);
    lv_obj_set_style_pad_all(screen_area, 0, 0);

    /* Canvas 缓冲区分配 */
    canvas_buf = k_malloc(NES_CANVAS_W * NES_CANVAS_H * sizeof(lv_color_t));
    if (!canvas_buf) {
        return container;
    }
    memset(canvas_buf, 0, NES_CANVAS_W * NES_CANVAS_H * sizeof(lv_color_t));

    canvas = lv_canvas_create(screen_area);
    lv_canvas_set_buffer(canvas, canvas_buf, NES_CANVAS_W, NES_CANVAS_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_center(canvas);

    /* ===== 右侧动作按钮 (130 x 560) ===== */
    lv_obj_t *action_area = lv_obj_create(container);
    lv_obj_set_size(action_area, 130, 560);
    lv_obj_align(action_area, LV_ALIGN_RIGHT_MID, 0, -20);
    lv_obj_set_style_bg_opa(action_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(action_area, 0, 0);
    lv_obj_set_style_pad_all(action_area, 0, 0);

    static const struct {
        int x_offs, y_offs;
        const char *label;
        uint8_t mask;
        int w, h;
    } action_btns[] = {
        { 30,   0, "B", BTN_B,      50, 50 },
        { -30,  0, "A", BTN_A,      50, 50 },
        { 30,  70, "Start",  BTN_START,  55, 35 },
        { -30, 70, "Select", BTN_SELECT, 55, 35 },
    };

    for (int i = 0; i < 4; i++) {
        lv_obj_t *btn = lv_btn_create(action_area);
        lv_obj_set_size(btn, action_btns[i].w, action_btns[i].h);
        lv_obj_align(btn, LV_ALIGN_CENTER, action_btns[i].x_offs, action_btns[i].y_offs);
        lv_obj_set_style_radius(btn, 25, 0);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, action_btns[i].label);
        lv_obj_center(label);

        lv_obj_add_event_cb(btn, on_btn_pressed,  LV_EVENT_PRESSED,  (void *)(uintptr_t)action_btns[i].mask);
        lv_obj_add_event_cb(btn, on_btn_released, LV_EVENT_RELEASED, (void *)(uintptr_t)action_btns[i].mask);
    }

    /* ===== 底部调试/控制栏 (800 x 40) ===== */
    debug_bar = lv_obj_create(container);
    lv_obj_set_size(debug_bar, 800, 40);
    lv_obj_align(debug_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(debug_bar, lv_color_hex(0x16213E), 0);
    lv_obj_set_style_border_width(debug_bar, 0, 0);
    lv_obj_set_style_pad_all(debug_bar, 5, 0);

    debug_label = lv_label_create(debug_bar);
    lv_label_set_text(debug_label, "NES Emulator - Ready");
    lv_obj_set_style_text_color(debug_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(debug_label, &lv_font_montserrat_14, 0);
    lv_obj_align(debug_label, LV_ALIGN_LEFT_MID, 5, 0);

    /* 暂停/设置/返回按钮 */
    static const struct {
        const char *label;
        int evt;
        int x_offs;
    } ctrl_btns[] = {
        { "[暂停]", UI_GAME_EVT_PAUSE,   -180 },
        { "[设置]", UI_GAME_EVT_SETTINGS, -90 },
        { "[返回]", UI_GAME_EVT_BACK,     0 },
    };

    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_btn_create(debug_bar);
        lv_obj_set_size(btn, 60, 28);
        lv_obj_align(btn, LV_ALIGN_RIGHT_MID, ctrl_btns[i].x_offs, 0);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, ctrl_btns[i].label);
        lv_obj_center(label);

        lv_obj_add_event_cb(btn, on_ctrl_btn_clicked, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)ctrl_btns[i].evt);
    }

    return container;
}

void ui_game_blit_frame(const uint32_t *nes_fb)
{
    if (!canvas || !canvas_buf || !nes_fb) return;

    lv_draw_buf_t *draw_buf = lv_canvas_get_draw_buf(canvas);
    if (!draw_buf) return;

    uint8_t *buf = (uint8_t *)draw_buf->data;
    uint32_t stride = draw_buf->header.stride;

    for (int y = 0; y < NES_NATIVE_H; y++) {
        uint16_t *dst_row = (uint16_t *)(buf + (y * NES_SCALE) * stride);
        for (int x = 0; x < NES_NATIVE_W; x++) {
            /* RGB888 → RGB565 转换 */
            uint32_t c = nes_fb[y * NES_NATIVE_W + x];
            uint8_t r = (c >> 16) & 0xFF;
            uint8_t g = (c >> 8) & 0xFF;
            uint8_t b = c & 0xFF;
            uint16_t rgb565 = (uint16_t)((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            int dx = x * NES_SCALE;
            dst_row[dx]         = rgb565;
            dst_row[dx + 1]     = rgb565;
            uint16_t *dst_next = (uint16_t *)(buf + (y * NES_SCALE + 1) * stride);
            dst_next[dx]        = rgb565;
            dst_next[dx + 1]    = rgb565;
        }
    }

    lv_obj_invalidate(canvas);
}

void ui_game_update_debug_bar(const struct nes_system *sys,
                              const struct app_settings *settings)
{
    if (!debug_label || !sys) return;

    if (!settings->show_debug_bar) {
        lv_obj_add_flag(debug_bar, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(debug_bar, LV_OBJ_FLAG_HIDDEN);

    char buf[128];
    snprintk(buf, sizeof(buf),
             "FPS:%.1f | Mapper:%d | PRG:%dKB | CHR:%dKB | Frame:%u",
             sys->fps, sys->cart.mapper_num,
             (int)(sys->cart.prg_rom_size / 1024),
             (int)(sys->cart.chr_size / 1024),
             sys->frame_count);
    lv_label_set_text(debug_label, buf);
}

void ui_game_destroy(void)
{
    if (container) {
        lv_obj_del(container);
        container = NULL;
        canvas = NULL;
        debug_bar = NULL;
        debug_label = NULL;
    }
    if (canvas_buf) {
        k_free(canvas_buf);
        canvas_buf = NULL;
    }
}

void ui_game_set_ctrl_callback(ui_game_ctrl_cb_t cb)
{
    ctrl_cb = cb;
}

void ui_game_set_btn_callback(ui_game_btn_cb_t cb)
{
    btn_cb = cb;
}
