/*
 * NES Emulator - Memory Bus / IO Mapping
 */

#include "bus.h"
#include "ppu.h"
#include "cartridge.h"
#include "controller.h"
#include <string.h>
#include <zephyr/kernel.h>

void bus_init(nes_bus_t *bus)
{
    memset(bus->cpu_ram, 0, sizeof(bus->cpu_ram));
    bus->ppu           = NULL;
    bus->cart          = NULL;
    bus->ctrl          = NULL;
    bus->dma_page      = 0;
    bus->dma_transfer  = false;
    bus->dma_data      = 0;
    bus->dma_addr      = 0;
    bus->dma_sync      = false;
}

uint8_t bus_cpu_read(nes_bus_t *bus, uint16_t addr)
{
    /* $0000-$07FF: CPU RAM, $0800-$1FFF: 镜像 */
    if (addr < 0x2000) {
        return bus->cpu_ram[addr & 0x07FF];
    }

    /* $2000-$2007: PPU 寄存器, $2008-$3FFF: 镜像 */
    if (addr < 0x4000) {
        return ppu_read_reg(bus->ppu, addr & 0x2007);
    }

    /* $4016: 手柄1 */
    if (addr == 0x4016) {
        return ctrl_read_port1(bus->ctrl);
    }

    /* $4017: 手柄2 */
    if (addr == 0x4017) {
        return ctrl_read_port2(bus->ctrl);
    }

    /* $4020-$FFFF: 卡带 PRG-ROM/RAM */
    return cart_cpu_read(bus->cart, addr);
}

void bus_cpu_write(nes_bus_t *bus, uint16_t addr, uint8_t data)
{
    /* $0000-$07FF: CPU RAM, $0800-$1FFF: 镜像 */
    if (addr < 0x2000) {
        bus->cpu_ram[addr & 0x07FF] = data;
        return;
    }

    /* $2000-$2007: PPU 寄存器, $2008-$3FFF: 镜像 */
    if (addr < 0x4000) {
        ppu_write_reg(bus->ppu, bus, addr & 0x2007, data);
        return;
    }

    /* $4014: OAM DMA */
    if (addr == 0x4014) {
        bus->dma_page = data;
        bus->dma_transfer = true;
        bus->dma_addr = 0;
        return;
    }

    /* $4016: 手柄 strobe */
    if (addr == 0x4016) {
        ctrl_write_strobe(bus->ctrl, data);
        return;
    }

    /* $4020-$FFFF: 卡带 */
    cart_cpu_write(bus->cart, addr, data);
}

uint8_t bus_ppu_read(nes_bus_t *bus, uint16_t addr)
{
    addr &= 0x3FFF;

    /* $0000-$1FFF: CHR-ROM/RAM (通过卡带) */
    if (addr < 0x2000) {
        return cart_ppu_read(bus->cart, addr);
    }

    /* $2000-$3FFF: 命名表 (VRAM, 经过镜像) */
    if (addr < 0x4000) {
        uint16_t nt_addr = cart_mirror_address(bus->cart, addr);
        return ppu_read_vram(bus->ppu, nt_addr);  /* 保留完整地址 */
    }

    return 0;
}

void bus_ppu_write(nes_bus_t *bus, uint16_t addr, uint8_t data)
{
    addr &= 0x3FFF;

    /* $0000-$1FFF: CHR-ROM/RAM */
    if (addr < 0x2000) {
        cart_ppu_write(bus->cart, addr, data);
        return;
    }

    /* $2000-$3FFF: 命名表 */
    if (addr < 0x4000) {
        uint16_t nt_addr = cart_mirror_address(bus->cart, addr);
        ppu_write_vram(bus->ppu, nt_addr, data);  /* 保留完整地址 */
        return;
    }
}

void bus_dma_transfer(nes_bus_t *bus)
{
    if (!bus->dma_transfer) {
        return;
    }

    /* DMA: 从 CPU 地址空间 $XX00-$XXFF 复制 256 字节到 PPU OAM */
    uint16_t base = (uint16_t)bus->dma_page << 8;

    for (int i = 0; i < 256; i++) {
        bus->dma_data = bus_cpu_read(bus, base | i);
        /* 写入 PPU OAM 通过 $2004 */
        bus->ppu->oam[bus->ppu->oam_addr] = bus->dma_data;
        bus->ppu->oam_addr++;
    }

    bus->dma_transfer = false;
}
