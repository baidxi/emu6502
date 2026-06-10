/*
 * NES Emulator - System Integration (CPU + PPU + Bus Synchronization)
 */

#ifndef NES_SYSTEM_H
#define NES_SYSTEM_H

#include <stdint.h>
#include <stdbool.h>

#include "cpu_6502.h"
#include "ppu.h"
#include "bus.h"
#include "cartridge.h"
#include "controller.h"

/* NES 系统上下文 */
typedef struct nes_system {
    cpu6502_t   cpu;
    nes_ppu_t   ppu;
    nes_bus_t   bus;
    nes_cart_t  cart;
    nes_ctrl_t  ctrl;

    bool        running;           /* 运行标志 */
    bool        paused;            /* 暂停标志 */
    uint32_t    frame_count;       /* 帧计数 */
    float       fps;               /* 实时帧率 */
} nes_system_t;

/* 系统初始化 */
void nes_system_init(nes_system_t *sys);

/* 加载 ROM 到系统 */
int nes_system_load_rom(nes_system_t *sys, const uint8_t *data, size_t size, const char *path);

/* 运行一帧 (29781 CPU cycles) */
int nes_system_run_frame(nes_system_t *sys);

/* 重置系统 */
void nes_system_reset(nes_system_t *sys);

/* 获取帧缓冲 (只读) */
const uint32_t *nes_system_get_framebuffer(nes_system_t *sys);

/* 清理资源 */
void nes_system_cleanup(nes_system_t *sys);

#endif /* NES_SYSTEM_H */
