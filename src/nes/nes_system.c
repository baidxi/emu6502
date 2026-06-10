/*
 * NES Emulator - System Integration (CPU + PPU + Bus Synchronization)
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "nes_system.h"
#include "bus.h"

LOG_MODULE_REGISTER(nes_system, CONFIG_LOG_DEFAULT_LEVEL);

#define SCANLINES_PER_FRAME 262
#define PPU_CYCLES_PER_SCANLINE 341

void nes_system_init(nes_system_t *sys)
{
    memset(sys, 0, sizeof(nes_system_t));

    /* 初始化各模块 */
    ppu_init(&sys->ppu);
    bus_init(&sys->bus);
    ctrl_init(&sys->ctrl);

    /* 连接总线到各模块 */
    sys->bus.ppu  = &sys->ppu;
    sys->bus.cart = &sys->cart;
    sys->bus.ctrl = &sys->ctrl;
}

int nes_system_load_rom(nes_system_t *sys, const uint8_t *data, size_t size, const char *path)
{
    /* 释放之前的 ROM */
    if (sys->cart.prg_rom || sys->cart.chr_rom || sys->cart.chr_ram) {
        cart_free(&sys->cart);
        memset(&sys->cart, 0, sizeof(sys->cart));
    }

    /* 加载新 ROM */
    int ret = cart_load_rom(&sys->cart, data, size, path);
    if (ret < 0) {
        return ret;
    }

    /* 重新初始化系统并复位 CPU */
    ppu_init(&sys->ppu);
    bus_init(&sys->bus);
    ctrl_init(&sys->ctrl);

    sys->bus.ppu  = &sys->ppu;
    sys->bus.cart = &sys->cart;
    sys->bus.ctrl = &sys->ctrl;

    cpu6502_reset(&sys->cpu, &sys->bus);

    sys->frame_count = 0;
    sys->fps = 0.0f;
    sys->running = true;
    sys->paused = false;

    return 0;
}

int nes_system_run_frame(nes_system_t *sys)
{
    if (!sys->running || sys->paused) {
        return 0;
    }

    /* 首帧诊断 */
    static bool first_frame_diag = true;
    if (first_frame_diag) {
        if(0)LOG_ERR("=== Frame start: PC=%04X MASK=%02X CTRL=%02X ===",
                sys->cpu.pc, sys->ppu.mask, sys->ppu.ctrl);
        cpu6502_trace_enable(20);
        first_frame_diag = false;
    }

    int cpu_cycles_accum = 0;
    int max_instr_per_scanline = 1000;

    for (int scanline = 0; scanline < SCANLINES_PER_FRAME; scanline++) {
        /* 前2帧每50扫描线打印进度 */
        if (sys->frame_count <= 1 && (scanline % 50) == 0) {
            if(0)LOG_ERR("  scanline %d/%d PC=%04X", scanline, SCANLINES_PER_FRAME, sys->cpu.pc);
        }

        /* 注意: 渲染期间不 yield, 避免显示线程在帧渲染途中读取 buffer 导致撕裂 */
        /* 每扫描线: 341 PPU dots = ~113.667 CPU cycles */
        /* 使用 113/114 交替以逼近 113.667 均值 */
        int cpu_cycles_target = (scanline % 3 == 0) ? 114 : 113;

        /* 处理 DMA 传输 */
        if (sys->bus.dma_transfer) {
            bus_dma_transfer(&sys->bus);
            /* DMA 消耗 ~513-514 CPU cycles */
            cpu_cycles_accum += 513;
        }

        /* VBlank NMI: 在扫描线241开始时立即触发 (先PPU设置VBlank, 再触发NMI, 然后CPU) */
        if (scanline == 241) {
            ppu_process_scanline(&sys->ppu, &sys->bus, 241);
            if (sys->ppu.nmi_occurred && sys->ppu.nmi_output) {
                cpu6502_nmi(&sys->cpu, &sys->bus);
                sys->ppu.nmi_occurred = false;  /* 防止在后续 CPU 循环中重复触发 NMI */
            }
        }

        /* 执行 CPU 指令直到达到目标周期 */
        int instr_count = 0;
        while (cpu_cycles_accum < cpu_cycles_target) {
            int cycles = cpu6502_step(&sys->cpu, &sys->bus);
            cpu_cycles_accum += cycles;
            instr_count++;
            /* NMI 检查 */
            if (sys->ppu.nmi_occurred && sys->ppu.nmi_output) {
                cpu6502_nmi(&sys->cpu, &sys->bus);
                sys->ppu.nmi_occurred = false;
            }

            if (instr_count > max_instr_per_scanline) {
                LOG_ERR("CPU stuck: %d instr on scanline %d, PC=%04X",
                        instr_count, scanline, sys->cpu.pc);
                cpu_cycles_accum = cpu_cycles_target;
                break;
            }
        }
        cpu_cycles_accum -= cpu_cycles_target;

        /* PPU 处理扫描线 (241已在上面处理) */
        if (scanline != 241) {
            ppu_process_scanline(&sys->ppu, &sys->bus, scanline);
        }
    }

    /* 帧渲染完成: 交换前后台缓冲, 保证显示线程读取的是完整帧 */
    {
        uint32_t *tmp = sys->ppu.render_buf;
        sys->ppu.render_buf = sys->ppu.display_buf;
        sys->ppu.display_buf = tmp;
    }

    sys->frame_count++;

    /* 帧 yield: 交换缓冲后让出 CPU, 使显示线程读取到完整的 display_buf */
    k_yield();

    /* 第3帧时若 PPU 仍未启用渲染, 打印警告 */
    static bool ppu_disabled_warned = false;
    if (!ppu_disabled_warned && sys->frame_count >= 3 &&
        !(sys->ppu.mask & 0x18)) {
        if(0)LOG_WRN("Frame %u: PPU rendering still disabled (MASK=%02X CTRL=%02X)",
                sys->frame_count, sys->ppu.mask, sys->ppu.ctrl);
        ppu_disabled_warned = true;
    }

    return 0;
}

void nes_system_reset(nes_system_t *sys)
{
    cpu6502_reset(&sys->cpu, &sys->bus);
    ppu_init(&sys->ppu);
    ctrl_init(&sys->ctrl);

    sys->bus.ppu  = &sys->ppu;
    sys->bus.ctrl = &sys->ctrl;
    sys->bus.dma_transfer = false;
}

const uint32_t *nes_system_get_framebuffer(nes_system_t *sys)
{
    return sys->ppu.display_buf;
}

void nes_system_cleanup(nes_system_t *sys)
{
    cart_free(&sys->cart);
    memset(sys, 0, sizeof(nes_system_t));
}
