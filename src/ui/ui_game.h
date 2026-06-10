/*
 * NES Emulator - LVGL Game Screen UI (Canvas + Virtual Gamepad)
 */

#ifndef UI_GAME_H
#define UI_GAME_H

#include <lvgl.h>
#include <stdint.h>

#include "ui_common.h"
#include "../nes/nes_system.h"

/* Canvas 尺寸定义 */
#define NES_NATIVE_W     256
#define NES_NATIVE_H     240
#define NES_SCALE        2
#define NES_CANVAS_W     (NES_NATIVE_W * NES_SCALE)   /* 512 */
#define NES_CANVAS_H     (NES_NATIVE_H * NES_SCALE)   /* 480 */

/**
 * @brief 创建游戏运行界面
 * @param parent 父对象
 * @return 游戏界面容器对象
 */
lv_obj_t *ui_game_create(lv_obj_t *parent);

/**
 * @brief 将 NES 帧缓冲渲染到 Canvas
 * @param nes_fb NES 帧缓冲 (256x240 RGB888)
 */
void ui_game_blit_frame(const uint32_t *nes_fb);

/**
 * @brief 刷新调试信息栏
 * @param sys NES 系统上下文
 * @param settings 应用设置
 */
void ui_game_update_debug_bar(const struct nes_system *sys,
                              const struct app_settings *settings);

/**
 * @brief 销毁游戏界面
 */
void ui_game_destroy(void);

/**
 * @brief 设置虚拟手柄按钮回调 (返回/暂停等)
 */
typedef void (*ui_game_ctrl_cb_t)(int event);
void ui_game_set_ctrl_callback(ui_game_ctrl_cb_t cb);

/**
 * @brief 设置 NES 按钮按下/释放回调 (连接虚拟手柄到模拟器)
 * @param cb 回调: cb(mask, pressed) - mask 为按钮掩码, pressed=true按下
 */
typedef void (*ui_game_btn_cb_t)(uint8_t mask, bool pressed);
void ui_game_set_btn_callback(ui_game_btn_cb_t cb);

/* 游戏界面控制事件 */
enum ui_game_event {
    UI_GAME_EVT_PAUSE,
    UI_GAME_EVT_RESUME,
    UI_GAME_EVT_SETTINGS,
    UI_GAME_EVT_BACK,
};

#endif /* UI_GAME_H */
