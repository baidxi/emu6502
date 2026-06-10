/*
 * NES Emulator - Cartridge Loader / Mapper
 */

#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* iNES 头部大小 */
#define INES_HEADER_SIZE    16
#define INES_TRAINER_SIZE   512

/* PRG-ROM bank 大小 */
#define PRG_BANK_SIZE       16384   /* 16KB */
/* CHR-ROM bank 大小 */
#define CHR_BANK_SIZE       8192    /* 8KB */

/* 卡带镜像模式 */
enum mirror_mode {
    MIRROR_HORIZONTAL = 0,
    MIRROR_VERTICAL   = 1,
    MIRROR_SINGLE_A   = 2,   /* 单屏A ($2000) */
    MIRROR_SINGLE_B   = 3,   /* 单屏B ($2400) */
    MIRROR_FOUR_SCREEN = 4,  /* 四屏 VRAM */
};

/* 卡带数据结构 */
typedef struct nes_cart {
    uint8_t  mapper_num;          /* Mapper 编号 */
    uint8_t  mirror;              /* 镜像模式 */
    bool     battery;             /* 是否有电池备份SRAM */
    bool     trainer;             /* 是否有训练器 */

    uint8_t  *prg_rom;            /* PRG-ROM 数据指针 */
    size_t   prg_rom_size;        /* PRG-ROM 大小 (字节) */
    uint8_t  prg_banks;           /* PRG-ROM bank 数量 (16KB/bank) */

    uint8_t  *chr_rom;            /* CHR-ROM 数据指针 (NULL 则使用 CHR-RAM) */
    uint8_t  *chr_ram;            /* CHR-RAM (如果无 CHR-ROM, 分配 8KB) */
    size_t   chr_size;            /* CHR 数据大小 (字节) */
    uint8_t  chr_banks;           /* CHR-ROM bank 数量 (8KB/bank) */

    uint8_t  *sram;               /* 电池备份 SRAM (8KB) */
    char     rom_path[256];       /* ROM 文件路径 (用于SRAM存档文件命名) */

    /* Mapper 特定状态 (由各 Mapper 实现使用) */
    void     *mapper_data;        /* Mapper 私有数据 */
} nes_cart_t;

/* iNES ROM 加载 */
int cart_load_rom(nes_cart_t *cart, const uint8_t *data, size_t size, const char *path);

/* 卡带释放 */
void cart_free(nes_cart_t *cart);

/* SRAM 存档 */
int cart_sram_save(nes_cart_t *cart);
int cart_sram_load(nes_cart_t *cart);

/* CPU 地址空间读取 (由总线层调用, 经 Mapper 映射) */
uint8_t cart_cpu_read(nes_cart_t *cart, uint16_t addr);
void    cart_cpu_write(nes_cart_t *cart, uint16_t addr, uint8_t data);

/* PPU 地址空间读取 (由总线层调用, 经 Mapper 映射) */
uint8_t cart_ppu_read(nes_cart_t *cart, uint16_t addr);
void    cart_ppu_write(nes_cart_t *cart, uint16_t addr, uint8_t data);

/* 命名表镜像地址转换 */
uint16_t cart_mirror_address(nes_cart_t *cart, uint16_t addr);

#endif /* CARTRIDGE_H */
