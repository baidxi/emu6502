/*
 * NES Emulator - PPU (Picture Processing Unit) Simulator
 */

#ifndef PPU_H
#define PPU_H

#include <stdint.h>
#include <stdbool.h>

/* 前向声明 */
struct nes_bus;

/* NES 原生分辨率 */
#define NES_SCREEN_WIDTH  256
#define NES_SCREEN_HEIGHT 240

/* PPU 内部状态 */
typedef struct nes_ppu {
    /* VRAM 和调色板 */
    uint8_t  vram[4096];             /* 命名表 RAM (4KB, 容纳4屏) */
    uint8_t  palette[32];            /* 调色板 RAM */
    uint8_t  oam[256];               /* 精灵 OAM (64个精灵 x 4字节) */
    uint8_t  secondary_oam[32];      /* 次级 OAM (8个精灵 x 4字节) */

    /* 内部寄存器 */
    uint16_t v;                      /* 当前VRAM地址 (15位) */
    uint16_t t;                      /* 临时VRAM地址 */
    uint8_t  fine_x;                 /* 细粒度X滚动 (3位) */
    uint8_t  data_buf;              /* PPUDATA读取缓冲 */
    uint8_t  latch;                 /* $2005/$2006 写入锁存 (0/1) */

    /* 控制寄存器 */
    uint8_t  ctrl;                   /* $2000 PPUCTRL */
    uint8_t  mask;                   /* $2001 PPUMASK */
    uint8_t  status;                 /* $2002 PPUSTATUS */
    uint8_t  oam_addr;               /* $2003 OAMADDR */

    /* 标志 */
    bool     nmi_occurred;           /* VBlank NMI 标志 */
    bool     nmi_output;             /* NMI 输出使能 */
    bool     sprite_zero_hit;        /* 精灵0命中标志 */
    bool     odd_frame;              /* 奇数帧标志 */
    bool     frame_complete;         /* 帧渲染完成标志 */

    /* 扫描线状态 */
    int      scanline;               /* 当前扫描线 (0-261) */
    int      cycle;                  /* 当前扫描线内的 PPU cycle (0-340) */
    uint8_t  bg_cx;                  /* running coarse_x for background */
    uint8_t  bg_nt;                  /* running nametable h bit */

    /* 双缓冲帧缓冲 (防止渲染/显示竞争导致画面撕裂) */
    uint32_t frame_buffers[2][NES_SCREEN_WIDTH * NES_SCREEN_HEIGHT]; /* RGB888 */
    uint32_t *render_buf;   /* PPU 渲染目标 (后台缓冲) */
    uint32_t *display_buf;  /* 显示线程读取的已完成帧 (前台缓冲) */
} nes_ppu_t;

/* PPU 初始化 */
void ppu_init(nes_ppu_t *ppu);

/* 处理一整条扫描线 (341 PPU cycles) */
void ppu_process_scanline(nes_ppu_t *ppu, struct nes_bus *bus, int scanline);

/* PPU 寄存器读写 (由总线层调用) */
uint8_t ppu_read_reg(nes_ppu_t *ppu, uint16_t addr);
void    ppu_write_reg(nes_ppu_t *ppu, struct nes_bus *bus, uint16_t addr, uint8_t data);

/* PPU VRAM 读写 (由总线层调用) */
uint8_t ppu_read_vram(nes_ppu_t *ppu, uint16_t addr);
void    ppu_write_vram(nes_ppu_t *ppu, uint16_t addr, uint8_t data);

/* NES 调色板: 64色索引 → RGB888 */
uint32_t ppu_palette_color(uint8_t index);

#endif /* PPU_H */
