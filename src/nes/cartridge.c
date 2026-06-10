/*
 * NES Emulator - Cartridge Loader / Mapper
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdlib.h>

#include "cartridge.h"
#include "../platform/sd_card.h"

LOG_MODULE_REGISTER(cartridge, CONFIG_LOG_DEFAULT_LEVEL);

/* 内联辅助: 读取16位小端 */
static inline uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

int cart_load_rom(nes_cart_t *cart, const uint8_t *data, size_t size, const char *path)
{
    if (size < INES_HEADER_SIZE) {
        LOG_ERR("ROM too small: %zu bytes", size);
        return -EINVAL;
    }

    /* 验证 iNES 魔数 */
    if (data[0] != 'N' || data[1] != 'E' || data[2] != 'S' || data[3] != 0x1A) {
        LOG_ERR("Invalid iNES header: %02X %02X %02X %02X",
                data[0], data[1], data[2], data[3]);
        return -EINVAL;
    }

    /* 解析 iNES 头部 */
    uint8_t prg_banks = data[4];      /* PRG-ROM bank 数 (16KB/bank) */
    uint8_t chr_banks = data[5];      /* CHR-ROM bank 数 (8KB/bank) */
    uint8_t flags6    = data[6];
    uint8_t flags7    = data[7];

    cart->mapper_num = ((flags7 & 0xF0) << 4) | (flags6 >> 4);
    cart->mirror     = (flags6 & 0x01) ? MIRROR_VERTICAL : MIRROR_HORIZONTAL;
    cart->battery    = (flags6 & 0x02) != 0;
    cart->trainer    = (flags6 & 0x04) != 0;
    cart->prg_banks  = prg_banks;
    cart->chr_banks  = chr_banks;

    /* 保存 ROM 路径 */
    if (path) {
        strncpy(cart->rom_path, path, sizeof(cart->rom_path) - 1);
        cart->rom_path[sizeof(cart->rom_path) - 1] = '\0';
    }

    /* 跳过训练器 (512字节) */
    size_t offset = INES_HEADER_SIZE;
    if (cart->trainer) {
        offset += INES_TRAINER_SIZE;
    }

    /* 加载 PRG-ROM */
    cart->prg_rom_size = prg_banks * PRG_BANK_SIZE;
    if (cart->prg_rom_size > 0) {
        cart->prg_rom = k_malloc(cart->prg_rom_size);
        if (!cart->prg_rom) {
            LOG_ERR("Failed to allocate PRG-ROM (%zu bytes)", cart->prg_rom_size);
            return -ENOMEM;
        }
        memcpy(cart->prg_rom, data + offset, cart->prg_rom_size);
        offset += cart->prg_rom_size;
    }

    /* 加载 CHR-ROM (或分配 CHR-RAM) */
    cart->chr_size = chr_banks > 0 ? chr_banks * CHR_BANK_SIZE : CHR_BANK_SIZE;
    if (chr_banks > 0) {
        cart->chr_rom = k_malloc(cart->chr_size);
        if (!cart->chr_rom) {
            LOG_ERR("Failed to allocate CHR-ROM (%zu bytes)", cart->chr_size);
            return -ENOMEM;
        }
        memcpy(cart->chr_rom, data + offset, cart->chr_size);
    } else {
        /* CHR-RAM: 游戏自行写入 */
        cart->chr_ram = k_malloc(CHR_BANK_SIZE);
        if (!cart->chr_ram) {
            LOG_ERR("Failed to allocate CHR-RAM");
            return -ENOMEM;
        }
        memset(cart->chr_ram, 0, CHR_BANK_SIZE);
        cart->chr_size = CHR_BANK_SIZE;
    }

    /* 分配 SRAM (电池备份的游戏使用) */
    if (cart->battery) {
        cart->sram = k_malloc(8192);
        if (!cart->sram) {
            LOG_ERR("Failed to allocate SRAM");
            return -ENOMEM;
        }
        memset(cart->sram, 0, 8192);
        /* 尝试加载存档 */
        cart_sram_load(cart);
    }

    LOG_INF("ROM parsed: Mapper=%d PRG=%dKB CHR=%dKB Battery=%d Mirror=%d",
            cart->mapper_num,
            (int)(cart->prg_rom_size / 1024),
            (int)(cart->chr_size / 1024),
            cart->battery,
            cart->mirror);

    return 0;
}

void cart_free(nes_cart_t *cart)
{
    if (cart->sram && cart->battery) {
        cart_sram_save(cart);
    }
    if (cart->prg_rom) { k_free(cart->prg_rom); cart->prg_rom = NULL; }
    if (cart->chr_rom) { k_free(cart->chr_rom); cart->chr_rom = NULL; }
    if (cart->chr_ram) { k_free(cart->chr_ram); cart->chr_ram = NULL; }
    if (cart->sram)    { k_free(cart->sram);    cart->sram    = NULL; }
    if (cart->mapper_data) { k_free(cart->mapper_data); cart->mapper_data = NULL; }
}

int cart_sram_save(nes_cart_t *cart)
{
    if (!cart->battery || !cart->sram || cart->rom_path[0] == '\0') {
        return 0;
    }

    /* 生成存档路径: <rom_path>.sav */
    char sav_path[320];
    snprintf(sav_path, sizeof(sav_path), "%s.sav", cart->rom_path);

    return sd_card_save_file(sav_path, cart->sram, 8192);
}

int cart_sram_load(nes_cart_t *cart)
{
    /* SRAM 加载 - 基础实现, 依赖外部文件存在性检查 */
    /* 实际加载由上层通过 sd_card API 完成 */
    return 0;
}

uint16_t cart_mirror_address(nes_cart_t *cart, uint16_t addr)
{
    /* 命名表地址: $2000-$2FFF 范围内, 共4个命名表($2000,$2400,$2800,$2C00) */
    uint16_t nt_addr = addr & 0x2FFF;

    switch (cart->mirror) {
    case MIRROR_VERTICAL:
        /* $2000=$2800=A, $2400=$2C00=B: 选 bit10 */
        return 0x2000 | (nt_addr & 0x03FF) | (nt_addr & 0x0400);
    case MIRROR_HORIZONTAL:
        /* $2000=$2400=A, $2800=$2C00=B: 选 bit11→bit10 */
        return 0x2000 | (nt_addr & 0x03FF) | ((nt_addr & 0x0800) >> 1);
    case MIRROR_SINGLE_A:
        return 0x2000 | (nt_addr & 0x03FF);
    case MIRROR_SINGLE_B:
        return 0x2400 | (nt_addr & 0x03FF);
    default:
        return nt_addr;
    }
    /* MIRROR_FOUR_SCREEN: 不镜像, 4个物理命名表, 返回原地址 */
}

/* ===== Mapper 0 (NROM) CPU/PPU 读写 ===== */

uint8_t cart_cpu_read(nes_cart_t *cart, uint16_t addr)
{
    if (addr >= 0x4020 && addr <= 0xFFFF && cart->prg_rom) {
        uint32_t offset = addr - 0x8000;

        /* NROM-128 (16KB): 镜像到 $C000-$FFFF */
        if (cart->prg_rom_size <= PRG_BANK_SIZE) {
            offset &= 0x3FFF;  /* 16KB 范围内循环 */
        }
        /* NROM-256 (32KB): 直接映射 */
        if (offset < cart->prg_rom_size) {
            return cart->prg_rom[offset];
        }
    }

    /* SRAM / WRAM 读取 $6000-$7FFF */
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        if (!cart->sram) {
            cart->sram = k_malloc(8192);
            if (!cart->sram) return 0;
            memset(cart->sram, 0, 8192);
        }
        return cart->sram[addr - 0x6000];
    }

    return 0;
}

void cart_cpu_write(nes_cart_t *cart, uint16_t addr, uint8_t data)
{
    /* SRAM / WRAM 写入 $6000-$7FFF */
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        uint8_t *mem = cart->sram;
        if (!mem) {
            /* 无SRAM时分配WRAM (nestest等需要) */
            cart->sram = k_malloc(8192);
            if (!cart->sram) return;
            memset(cart->sram, 0, 8192);
            mem = cart->sram;
        }
        mem[addr - 0x6000] = data;
        return;
    }

    /* PRG-ROM 不可写 */
    /* Mapper 寄存器: 由各 Mapper 实现处理 */
}

uint8_t cart_ppu_read(nes_cart_t *cart, uint16_t addr)
{
    addr &= 0x3FFF; /* CHR 地址空间 0-16KB */

    if (cart->chr_rom) {
        return cart->chr_rom[addr];
    } else if (cart->chr_ram) {
        return cart->chr_ram[addr];
    }
    return 0;
}

void cart_ppu_write(nes_cart_t *cart, uint16_t addr, uint8_t data)
{
    addr &= 0x3FFF;

    /* CHR-RAM: 可写 */
    if (cart->chr_ram) {
        cart->chr_ram[addr] = data;
    }
    /* CHR-ROM: 不可写 */
}
