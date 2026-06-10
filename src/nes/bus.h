/*
 * NES Emulator - Memory Bus / IO Mapping
 */

#ifndef BUS_H
#define BUS_H

#include <stdint.h>
#include <stdbool.h>

#include "ppu.h"
#include "cartridge.h"
#include "controller.h"

/* NES 内存总线 */
typedef struct nes_bus {
    uint8_t  cpu_ram[2048];       /* 2KB CPU RAM */

    struct nes_ppu  *ppu;         /* PPU 实例引用 */
    struct nes_cart *cart;        /* 卡带实例引用 */
    struct nes_ctrl *ctrl;        /* 控制器实例引用 */

    /* OAM DMA */
    uint8_t  dma_page;            /* DMA 高字节地址 */
    bool     dma_transfer;        /* DMA 传输中标志 */
    uint8_t  dma_data;            /* DMA 数据缓冲 */
    uint8_t  dma_addr;            /* DMA 当前地址 (低8位) */
    bool     dma_sync;            /* DMA 同步等待标志 */
} nes_bus_t;

/* 总线初始化 */
void bus_init(nes_bus_t *bus);

/* CPU 地址空间读写 */
uint8_t bus_cpu_read(nes_bus_t *bus, uint16_t addr);
void    bus_cpu_write(nes_bus_t *bus, uint16_t addr, uint8_t data);

/* PPU 地址空间读写 */
uint8_t bus_ppu_read(nes_bus_t *bus, uint16_t addr);
void    bus_ppu_write(nes_bus_t *bus, uint16_t addr, uint8_t data);

/* OAM DMA 传输 (通过写 $4014 触发) */
void bus_dma_transfer(nes_bus_t *bus);

#endif /* BUS_H */
