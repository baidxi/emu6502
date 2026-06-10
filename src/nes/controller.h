/*
 * NES Emulator - Controller Input
 */

#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

/* NES 手柄按钮掩码 */
enum nes_btn_mask {
    BTN_A      = (1 << 0),
    BTN_B      = (1 << 1),
    BTN_SELECT = (1 << 2),
    BTN_START  = (1 << 3),
    BTN_UP     = (1 << 4),
    BTN_DOWN   = (1 << 5),
    BTN_LEFT   = (1 << 6),
    BTN_RIGHT  = (1 << 7),
};

/* 单个手柄状态 */
typedef struct nes_controller {
    uint8_t buttons;     /* 按钮位域 (由 UI 线程原子写入) */
    uint8_t shift_reg;   /* 移位寄存器 (由模拟线程读写) */
    bool    strobe;      /* 当前 strobe 状态 */
} nes_controller_t;

/* 双打手柄 */
typedef struct nes_ctrl {
    nes_controller_t port1;   /* 玩家1 */
    nes_controller_t port2;   /* 玩家2 */
} nes_ctrl_t;

/* 初始化控制器 */
void ctrl_init(nes_ctrl_t *ctrl);

/* CPU 写 $4016: 设置/清除 strobe */
void ctrl_write_strobe(nes_ctrl_t *ctrl, uint8_t value);

/* CPU 读 $4016/$4017: 返回移位寄存器 bit0 并右移 */
uint8_t ctrl_read_port1(nes_ctrl_t *ctrl);
uint8_t ctrl_read_port2(nes_ctrl_t *ctrl);

/* UI 线程设置按钮 (原子操作) */
void ctrl_set_buttons(nes_controller_t *port, uint8_t mask);
void ctrl_clear_buttons(nes_controller_t *port, uint8_t mask);

#endif /* CONTROLLER_H */
